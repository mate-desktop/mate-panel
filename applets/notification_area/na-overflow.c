/* na-overflow.c - overflow popup window for the notification area
 * Copyright (C) 2026 kosmik2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <signal.h>      /* SIGTERM to the clickwatch helper */
#include <math.h>        /* floor() for the cell hit test */

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#ifdef HAVE_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "na-overflow.h"
#include "na-item.h"
#include "status-notifier/sn-item.h"

static gboolean na_overflow_point_in_widget (GtkWidget *widget, gint px, gint py);
static gboolean na_overflow_cell_enter_cb (GtkWidget *child, GdkEventCrossing *event, gpointer user_data);
#ifdef HAVE_WAYLAND
static gboolean na_overflow_hover_motion_cb (GtkWidget *widget, GdkEventMotion *event, gpointer user_data);
static gboolean na_overflow_stripe_draw_cb (GtkWidget *item, cairo_t *cr, gpointer user_data);
#endif
#ifdef HAVE_X11
static gboolean na_overflow_band_draw_cb (GtkWidget *widget, cairo_t *cr, gpointer user_data);
#endif
static void     na_overflow_set_hover (NaOverflow *self, GtkWidget *owned);
static gboolean na_overflow_cell_leave_cb (GtkWidget *child, GdkEventCrossing *event, gpointer user_data);
static void     na_overflow_grid_child_added_cb (GtkContainer *grid, GtkWidget *child, gpointer user_data);


enum
{
  CLOSED,
  LAST_SIGNAL
};

static guint overflow_signals[LAST_SIGNAL] = { 0 };

static void na_overflow_position (NaOverflow *self);
#ifdef HAVE_WAYLAND
static void na_overflow_arm_grab_poll (NaOverflow *self);
#endif
static void na_overflow_cancel_grab_poll (NaOverflow *self);

struct _NaOverflow
{
  GtkWindow     parent_instance;

  GtkWidget    *grid;
  GtkWidget    *relative_to;
  GtkOrientation panel_orientation;
  NaOpenDirection open_direction;

  /* Last position we asked for, so that a size-allocate caused by our own
   * gtk_window_move() does not bounce forever. */
  gint          moved_x;
  gint          moved_y;
  NaOverflowButtonClick click_func;
  gpointer              click_data;
  GDestroyNotify        click_destroy;

  /* The drawer's size as stated by the grid layout (see
   * na_overflow_set_nominal_size), without the container border. 0 until the
   * grid has laid out its cells at least once. */
  GtkWidget    *hover_item;   /* icon under the pointer, owns a ref */
  guint         hover_clear_id; /* deferred hover clear, see cell_leave_cb */
  guint         hover_poll;   /* X11: QueryPointer poll while the drawer is up */
  gint          nominal_w;
  gint          nominal_h;

  /* Wayland: the drawer is shown as a GtkMenu (xdg_popup) anchored to the
   * chevron button, and this window is never mapped - see na_overflow_new().
   * menu_up tracks the popup state so "deactivate" and popdown() cannot
   * recurse into each other. */
  GtkWidget    *menu;
  gboolean      use_menu;
  gboolean      menu_up;

  /* Re-entrancy guard for the click redispatch of the menu path: feeding a
   * button event to the icon under the cursor propagates it back up through
   * this same item handler, which must not redispatch it a second time. */
  gboolean      forwarding;

  /* Watch of the process grab, see na_overflow_grab_poll_cb. */
  guint         grab_poll;

  /* Set once some OTHER widget in this process held the pointer grab while the
   * drawer was up - the SNI context menu of an icon. See the poll. */
  gboolean      foreign_grab_seen;

  /* Where the popup is expected to land, in root coordinates. The popup's own
   * GdkWindow cannot be asked: gdk_window_get_origin() on Wayland answers
   * (0,0) for surfaces (measured), so the auto-hide poll compares against this
   * rect instead, built from the chevron's origin - which IS readable - and
   * the anchors handed to gtk_menu_popup_at_widget(). */
  GdkRectangle  menu_rect;

  /* Wayland menu path: when the pointer left the popup, 0 = inside.
   *
   * This replaces every coordinate comparison. gdk_window_get_device_position()
   * answers root-relative or window-relative depending on whether GDK happens
   * to know the surface's position (measured on the same run: (1907,25) and
   * (27,34) for one 34x34 popup), so "is the pointer inside" is a lottery
   * there. The popup's own enter/leave crossings come from the compositor and
   * are authoritative. */
  gint64        leave_at;

  /* Auto-hide. We deliberately take no seat grab, so neither Escape nor a
   * click outside reaches us: without a grab X hands
   * the event to the window under the pointer, not to us. Polling the pointer
   * position replaces both dismissal paths and blocks nothing - it only reads. */
  guint         hide_timer;      /* poll source, 0 = not running */
  gint64        shown_at;        /* when shown: no polling before the grace */
  gint64        outside_since;   /* when the pointer left the rect, 0 = inside */
  guint         autohide_ms;     /* how long to hold after it leaves, 0 = never */

  /* A press that went to the client must be followed by its release, or the
   * client keeps a stuck pressed state. */
  gboolean      forwarded_press;

  /* Click watching, through a SEPARATE PROCESS.
   *
   * XRecord cannot live in the applet: GDK drains the shared socket first, and
   * combining the two in one process hung the applet (measured twice). A
   * separate process removes the conflict by construction - it has neither GDK
   * nor an Xlib connection in common with us. If it hangs, the panel does not
   * notice; if it fails to start, the pointer poll below still dismisses.
   *
   * Nothing is grabbed: RECORD only duplicates events, so each click still
   * reaches its own window, parked XEmbed icons included. */
  GSubprocess   *cw_proc;
  GDataInputStream *cw_in;
  GCancellable  *cw_cancel;
  gboolean       cw_reading;
};

G_DEFINE_TYPE (NaOverflow, na_overflow, GTK_TYPE_WINDOW)


static void na_overflow_emit_closed (NaOverflow *self);

#ifdef HAVE_WAYLAND
/* GtkMenuShell::deactivate is (GtkMenuShell*, target), so it cannot be wired
 * with g_signal_connect_swapped to a one-argument function.  The menu path is
 * the Wayland one, hence the guard. */
static void
na_overflow_deactivate_cb (GtkMenuShell *menu,
                           gpointer      user_data)
{
  (void) menu;
  na_overflow_emit_closed (user_data);
}
#endif

static void
na_overflow_emit_closed (NaOverflow *self)
{
  if (self->use_menu)
    {
      /* The menu path has its own up/down tracking: "deactivate" arrives while
       * the menu is already tearing down, and popdown() must not re-emit. */
      if (!self->menu_up)
        return;
      self->menu_up = FALSE;
      g_signal_emit (self, overflow_signals[CLOSED], 0);
      return;
    }

  if (gtk_widget_get_visible (GTK_WIDGET (self)))
    g_signal_emit (self, overflow_signals[CLOSED], 0);
}

/* Is the pointer over this popup's own window right now?
 *
 * Compares ROOT coordinates.  Do not "simplify" this back to
 * gdk_window_get_device_position() + a check of its return value: that function
 * returns the GdkWindow UNDER THE DEVICE, and its documented contract is
 * "or %NULL if the window is not known to GDK" (gtk-3-24/gdk/gdkwindow.c).  Over
 * an application's menu - a foreign client's window drawn on top of the popup -
 * it returns NULL while the coordinates are still filled in, so treating the
 * return value as a success flag reads "the pointer is outside" at exactly the
 * moment a menu opened above us, and the flyout dismissed itself under its own
 * menu.  That was the whole bug.
 *
 * The master pointer comes from the seat, not from the event: GdkEventGrabBroken
 * carries no device field, and
 * gdk_device_get_master() is a GTK4 name absent in GTK3.
 *
 * Root coordinates only: na-overflow.c is in the unconditional source list, so it
 * must stay buildable without HAVE_X11 (no gdkx.h / XQueryPointer here). */
static gboolean
na_overflow_pointer_inside (NaOverflow *self)
{
  GdkWindow   *window;
  GdkDisplay  *display;
  GdkSeat     *seat;
  GdkDevice   *pointer;
  gint         px = 0, py = 0;   /* pointer, root coords */
  gint         ox = 0, oy = 0;   /* our window's origin, root coords */
  gint         w, h;

  /* The chevron lives in the panel, outside the drawer: without this the
   * drawer folds 800 ms after opening while the pointer still rests on the
   * button that opened it.  Checked before the per-backend channels, because
   * it is true on every backend. */
  if (self->relative_to != NULL)
    {
      GdkWindow   *rw = gtk_widget_get_window (self->relative_to);
      GdkDevice   *ptr;
      gint         px = 0, py = 0, rx = 0, ry = 0;

      if (rw != NULL)
        {
          ptr = gdk_seat_get_pointer (
                  gdk_display_get_default_seat (gdk_window_get_display (rw)));
          if (ptr != NULL)
            {
              gdk_device_get_position (ptr, NULL, &px, &py);
              gdk_window_get_origin (rw, &rx, &ry);
              if (px >= rx && px < rx + gdk_window_get_width (rw) &&
                  py >= ry && py < ry + gdk_window_get_height (rw))
                return TRUE;
            }
        }
    }

  /* Wayland menu path: the crossings on the popup decide, see leave_at. */
  if (self->use_menu)
    return self->leave_at == 0;

  /* X11 popup, but possibly under XWayland: QueryPointer there reports the
   * pointer frozen at the last position inside an X11 window, so it answers
   * "inside" forever once the pointer is over Wayland surfaces.  A crossing
   * that stamped leave_at is the more truthful channel - trust it. */
  if (self->leave_at != 0)
    return FALSE;

  window = gtk_widget_get_window (GTK_WIDGET (self));
  if (window == NULL)
    return FALSE;

  display = gdk_window_get_display (window);
  seat = gdk_display_get_default_seat (display);
  if (seat == NULL)
    return FALSE;

  pointer = gdk_seat_get_pointer (seat);
  if (pointer == NULL)
    return FALSE;

  gdk_device_get_position (pointer, NULL, &px, &py);
  gdk_window_get_origin (window, &ox, &oy);
  w = gdk_window_get_width (window);
  h = gdk_window_get_height (window);

  if (w <= 0 || h <= 0)
    return FALSE;

  g_printerr ("NADBG geom ptr=%d,%d win=%d,%d+%dx%d inside=%d\n",
              px, py, ox, oy, w, h,
              (int) (px >= ox && px < ox + w && py >= oy && py < oy + h));

  if (px >= ox && px < ox + w && py >= oy && py < oy + h)
    return TRUE;

#ifdef HAVE_X11
  /* The chevron lives in the panel, outside this window: without this the
   * drawer folded 800 ms after opening while the pointer was still on the
   * button that opened it.  X11 only - the Wayland menu path owns its own
   * dismissal and this check made it fold while the pointer sat on the
   * chevron. */
  if (self->relative_to != NULL &&
      na_overflow_point_in_widget (self->relative_to, px, py))
    return TRUE;
#endif

  return FALSE;
}

void
na_overflow_set_button_click (NaOverflow           *self,
                              NaOverflowButtonClick func,
                              gpointer              user_data,
                              GDestroyNotify        destroy)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

  self->click_func = func;
  self->click_data = user_data;
  self->click_destroy = destroy;
}

/* Press/release inside the popup: hand it to the parked icon under the cursor. */
static gboolean
na_overflow_forward_click (NaOverflow     *self,
                           GdkEventButton *event,
                           gboolean        is_press)
{
  if (self->click_func == NULL)
    return FALSE;

  return self->click_func (event, is_press, self->click_data);
}

static gboolean
na_overflow_button_release (GtkWidget      *widget,
                            GdkEventButton *event)
{
  NaOverflow *self = NA_OVERFLOW (widget);

  /* Forward the release only if the press really reached the client, and
   * without re-checking the position: the cursor may have moved away, while the
   * window that saw the press is still holding it down. */
  if (self->forwarded_press)
    {
      self->forwarded_press = FALSE;
      na_overflow_forward_click (self, event, FALSE);
      return TRUE;
    }

  if (GTK_WIDGET_CLASS (na_overflow_parent_class)->button_release_event)
    return GTK_WIDGET_CLASS (na_overflow_parent_class)->button_release_event (widget, event);

  return FALSE;
}

/* --- Click watching, through a helper process --------------------------- */

/* Where the helper lives, most reliable candidate first:
 *
 * 1. NA_CLICKWATCHDIR - the install directory ($(libexecdir)), known at build
 *    time.  The only variant that works after `make install'.  It cannot be
 *    derived from /proc/self/exe: the applet builds both as a separate process
 *    ($(libexecdir)/notification-area-applet) and as a module inside the panel
 *    ($(pkglibdir)/libnotification-area-applet.so), see
 *    NOTIFICATION_AREA_INPROCESS.  In the latter case /proc/self/exe is
 *    mate-panel itself, so a sibling lookup would search the wrong place.
 * 2. The directory of the executable, and one level above it - for running out
 *    of the build tree: libtool puts the applet binary in .libs/ while the
 *    helper lands next to the build directory.
 *
 * Returns NULL if nothing was found: the caller then simply does not enable
 * watching, and the drawer is dismissed by the pointer poll. */
static gchar *
na_overflow_find_clickwatch (void)
{
  g_autofree gchar *exe = NULL;
  g_autofree gchar *exe_dir = NULL;
  gchar *cands[3] = { NULL, NULL, NULL };
  int n = 0;
  int i;
  gchar *found = NULL;

#ifdef NA_CLICKWATCHDIR
  cands[n++] = g_build_filename (NA_CLICKWATCHDIR, "clickwatch", NULL);
#endif

  exe = g_file_read_link ("/proc/self/exe", NULL);
  if (exe != NULL)
    {
      exe_dir = g_path_get_dirname (exe);
      cands[n++] = g_build_filename (exe_dir, "clickwatch", NULL);
      /* one level up: libtool puts the applet binary in .libs/ and the helper
       * next to the build directory */
      cands[n++] = g_build_filename (exe_dir, "..", "clickwatch", NULL);
    }

  for (i = 0; i < n && found == NULL; i++)
    if (g_file_test (cands[i], G_FILE_TEST_IS_EXECUTABLE))
      found = g_strdup (cands[i]);

  for (i = 0; i < n; i++)
    g_free (cands[i]);

  return found;
}

/* "Inside the drawer" from the EXPLICIT click coordinates, not from the pointer
 * position: the event came from another process and may predate a move. */
static gboolean
na_overflow_point_inside (NaOverflow *self, gint px, gint py)
{
  GdkWindow *window = gtk_widget_get_window (GTK_WIDGET (self));
  gint ox = 0, oy = 0, w, h;

  if (window == NULL)
    return FALSE;

  gdk_window_get_origin (window, &ox, &oy);
  w = gdk_window_get_width (window);
  h = gdk_window_get_height (window);
  if (w <= 0 || h <= 0)
    return FALSE;

  return px >= ox && px < ox + w && py >= oy && py < oy + h;
}

/* Point inside a widget's window, in root coordinates. */
static gboolean
na_overflow_point_in_widget (GtkWidget *widget, gint px, gint py)
{
  GdkWindow *window;
  gint ox = 0, oy = 0, w, h;

  if (widget == NULL || !gtk_widget_get_realized (widget))
    return FALSE;

  window = gtk_widget_get_window (widget);
  if (window == NULL)
    return FALSE;

  gdk_window_get_origin (window, &ox, &oy);
  w = gdk_window_get_width (window);
  h = gdk_window_get_height (window);
  if (w <= 0 || h <= 0)
    return FALSE;

  return px >= ox && px < ox + w && py >= oy && py < oy + h;
}

static void na_overflow_read_click_line (NaOverflow *self);

static void
na_overflow_click_line_cb (GObject      *source,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  NaOverflow        *self = user_data;
  GDataInputStream  *in = G_DATA_INPUT_STREAM (source);
  g_autofree gchar  *line = NULL;
  g_autoptr(GError)  error = NULL;
  guint              button, px, py;

  line = g_data_input_stream_read_line_finish (in, result, NULL, &error);

  if (line == NULL)
    {
      /* The helper closed stdout, or we cancelled the read: a normal end,
       * not an error. */
      self->cw_reading = FALSE;
      g_object_unref (self);
      return;
    }

  if (sscanf (line, "PRESS %u %u %u", &button, &px, &py) == 3)
    {
      /* A click on the arrow itself is ours: it toggles the button. Closing on
       * it would drop the button to FALSE and the very same click would flip it
       * straight back - from outside that looks like "closed and reopened, the
       * icons visibly refresh". */
      if (self->relative_to != NULL &&
          na_overflow_point_in_widget (self->relative_to, (gint) px, (gint) py))
        {
          /* our own click: the arrow toggles the button itself */
        }
      else if (!na_overflow_point_inside (self, (gint) px, (gint) py))
        {
          na_overflow_emit_closed (self);
          return;   /* the window is already going away, no point reading on */
        }
    }

  na_overflow_read_click_line (self);
}

static void
na_overflow_read_click_line (NaOverflow *self)
{
  if (self->cw_in == NULL || self->cw_cancel == NULL)
    return;

  self->cw_reading = TRUE;
  /* The callback carries a raw self: hold a reference for as long as a read is
   * in flight, or a popdown during that window leaves it a use-after-free. */
  g_object_ref (self);
  g_data_input_stream_read_line_async (self->cw_in, G_PRIORITY_DEFAULT,
                                       self->cw_cancel,
                                       na_overflow_click_line_cb, self);
}

static void
na_overflow_disarm_click_watch (NaOverflow *self)
{
  if (self->cw_cancel != NULL)
    {
      g_cancellable_cancel (self->cw_cancel);
      g_clear_object (&self->cw_cancel);
    }

  g_clear_object (&self->cw_in);

  if (self->cw_proc != NULL)
    {
      /* SIGTERM: the helper leaves its loop on its own and releases the RECORD
       * context.  force_exit stays as a backstop against a wedged process. */
      g_subprocess_send_signal (self->cw_proc, SIGTERM);
      g_subprocess_force_exit (self->cw_proc);
      g_clear_object (&self->cw_proc);
    }

  self->cw_reading = FALSE;
}

/* Returns TRUE if the helper started.  Any failure is silent and we live on the
 * poll: the watcher must never break showing the drawer. */
static gboolean
na_overflow_arm_click_watch (NaOverflow *self)
{
  g_autofree gchar *path = NULL;
  g_autoptr(GError) error = NULL;

  if (self->cw_proc != NULL)
    return TRUE;

  path = na_overflow_find_clickwatch ();
  if (path == NULL)
    return FALSE;

  self->cw_proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                    &error, path, NULL);
  if (self->cw_proc == NULL)
    return FALSE;

  self->cw_cancel = g_cancellable_new ();
  self->cw_in = g_data_input_stream_new (
                  g_subprocess_get_stdout_pipe (self->cw_proc));

  na_overflow_read_click_line (self);
  return TRUE;
}

/* Auto-hide the drawer when the pointer leaves it.
 *
 * Replaces the two dismissal paths that are unavailable without a seat grab:
 * Escape and a click outside. It only READS (pointer position plus our window
 * origin), so it blocks nothing: no root event mask, no XI2 selection, no
 * global XGrabKey. The session stays entirely free while the drawer is open -
 * verified with an XGrabPointer probe, which returns GrabSuccess.
 *
 * The dwell is deliberately not instant: the user steps into a context menu
 * opened by right-clicking one of OUR icons, and that menu is a foreign window
 * outside our rect. An instant hide would close the drawer exactly as the menu
 * appears. The default dwell survives that trip and does not get in the way.
 */
#define AUTOHIDE_POLL_MS     200
#define AUTOHIDE_GRACE_MS    400   /* the drawer maps while the pointer is on the arrow */
#define AUTOHIDE_DEFAULT_MS  800

static void
na_overflow_cancel_autohide (NaOverflow *self)
{
  if (self->hide_timer != 0)
    {
      g_source_remove (self->hide_timer);
      self->hide_timer = 0;
    }
  self->outside_since = 0;
}

/* Always returns FALSE and re-arms itself. Otherwise g_source_remove() from
 * popdown, while this callback is running, would destroy the source and GLib
 * would then remove it a second time on our FALSE - a double removal. */
static gboolean
na_overflow_autohide_cb (gpointer data)
{
  NaOverflow *self = data;
  gboolean    hide = FALSE;

  self->hide_timer = 0;

  /* On the Wayland menu path the drawer window is never mapped - the popup is
   * what is on screen - so "visible" has to be read from the menu state. */
  if (self->use_menu ? !self->menu_up : !gtk_widget_get_visible (GTK_WIDGET (self)))
    return FALSE;

  /* The grace period is mandatory: the drawer is shown while the pointer is
   * still on the arrow, i.e. OUTSIDE this window. Without it the first poll
   * would close it again. */
  if (g_get_monotonic_time () - self->shown_at
      >= (gint64) AUTOHIDE_GRACE_MS * 1000)
    {
      gint64 now = g_get_monotonic_time ();

      if (na_overflow_pointer_inside (self))
        self->outside_since = 0;
      else if (self->outside_since == 0)
        self->outside_since = now;
      else if (now - self->outside_since >= (gint64) self->autohide_ms * 1000)
        hide = TRUE;

      g_printerr ("NADBG poll inside=%d leave_at=%" G_GINT64_FORMAT
                  " outside_since=%" G_GINT64_FORMAT " autohide=%u hide=%d\n",
                  (int) na_overflow_pointer_inside (self), self->leave_at,
                  self->outside_since, self->autohide_ms, (int) hide);
    }

  if (hide)
    {
      self->outside_since = 0;
      na_overflow_emit_closed (self);
      return FALSE;   /* popdown already removed the source */
    }

  self->hide_timer = g_timeout_add (AUTOHIDE_POLL_MS,
                                    na_overflow_autohide_cb, self);
  return FALSE;
}

/* Preference from the properties dialog: how many milliseconds to keep the
 * drawer after the pointer left. 0 disables auto-hide entirely. Applies live -
 * the next poll tick already uses the new value. */
void
na_overflow_set_autohide_ms (NaOverflow *self,
                             guint       ms)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

  self->autohide_ms = ms;
  self->outside_since = 0;

  if (ms == 0)
    na_overflow_cancel_autohide (self);
  else if ((self->use_menu ? self->menu_up
                           : gtk_widget_get_visible (GTK_WIDGET (self)))
           && self->hide_timer == 0)
    self->hide_timer = g_timeout_add (AUTOHIDE_POLL_MS,
                                      na_overflow_autohide_cb, self);
}

void
na_overflow_set_nominal_size (NaOverflow *self,
                              gint        width,
                              gint        height)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

  self->nominal_w = MAX (width, 0);
  self->nominal_h = MAX (height, 0);
}

void
na_overflow_set_open_direction (NaOverflow      *self,
                                NaOpenDirection  dir)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

  self->open_direction = dir;
}

static gboolean
na_overflow_button_press (GtkWidget      *widget,
                          GdkEventButton *event)
{
  NaOverflow *self = NA_OVERFLOW (widget);

  /* A press that lands INSIDE the popup never dismisses it, whatever the button.
   *
   * The earlier revision reasoned "a press that reached the toplevel was not
   * consumed by an icon, so it must be on our empty area or outside" - that is
   * FALSE, and it was measured: a plain LEFT click on an icon arrived as a
   * toplevel press with the pointer inside, and closed the flyout.  A parked XEmbed icon whose client did not
   * select ButtonPress hands the event to US (that is what X propagation of
   * unselected events does), so an ordinary click on an icon arrives here as a
   * toplevel press with the pointer squarely inside the popup.  Six of the seven
   * XEmbed icons on this tray are like that.
   *
   * Only a press outside may dismiss, the way a menu does. */
  if (na_overflow_pointer_inside (self))
    {
      gboolean fwd;

      /* Forward the press to the parked icon under the cursor: X will not
       * deliver it to the client by itself, because the socket does not select
       * ButtonPress, so it has to be synthesized. */
      fwd = na_overflow_forward_click (self, event, TRUE);
      self->forwarded_press = fwd;

      return TRUE;   /* swallowed: do not act on what is underneath either */
    }

  /* Reached from here on means the press is outside the popup: dismiss, and
   * swallow it so a stray click does not also act on whatever is underneath. */
  na_overflow_emit_closed (self);

  return TRUE;
}

static void
na_overflow_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  GTK_WIDGET_CLASS (na_overflow_parent_class)->size_allocate (widget, allocation);

  if (gtk_widget_get_mapped (widget))
    na_overflow_position (NA_OVERFLOW (widget));
}

static void
na_overflow_dispose (GObject *object)
{
  NaOverflow *self = NA_OVERFLOW (object);

  if (self->click_destroy != NULL && self->click_data != NULL)
    {
      self->click_destroy (self->click_data);
      self->click_data = NULL;
    }
  self->click_func = NULL;

  na_overflow_cancel_autohide (NA_OVERFLOW (object));
  na_overflow_cancel_grab_poll (NA_OVERFLOW (object));
  na_overflow_disarm_click_watch (NA_OVERFLOW (object));

  if (self->hover_clear_id != 0)
    {
      g_source_remove (self->hover_clear_id);
      self->hover_clear_id = 0;
    }

  if (self->hover_item != NULL)
    {
      g_object_unref (self->hover_item);
      self->hover_item = NULL;
    }

  g_clear_object (&self->menu);

  G_OBJECT_CLASS (na_overflow_parent_class)->dispose (object);
}

static void
na_overflow_class_init (NaOverflowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  widget_class->button_press_event = na_overflow_button_press;
  widget_class->button_release_event = na_overflow_button_release;
  widget_class->size_allocate      = na_overflow_size_allocate;
  object_class->dispose            = na_overflow_dispose;

  overflow_signals[CLOSED] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
na_overflow_init (NaOverflow *self)
{
  gtk_window_set_title (GTK_WINDOW (self), _("Tray Icons"));
  gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
  gtk_window_set_gravity (GTK_WINDOW (self), GDK_GRAVITY_STATIC);

  gtk_container_set_border_width (GTK_CONTAINER (self), 0);
  gtk_widget_set_name (GTK_WIDGET (self), "na-overflow");
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "background");
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "na-overflow");

  self->grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (self->grid), 0);

  /* The border used to give the drawer its 4 px breathing room, but it also
   * clipped the hover fill.  A margin on the grid keeps the same visual size
   * without a border (X11 only - the Wayland menu has its own padding). */
#ifdef HAVE_X11
  /* The grid spans the WHOLE drawer.  A margin here put the cells 4 px inside
   * the window, so the highlight (clipped to a cell) left a bare strip at the
   * drawer's left and right edges.  The breathing room moves to the icons. */
  gtk_widget_set_margin_start (self->grid, 0);
  gtk_widget_set_margin_end (self->grid, 0);
  gtk_widget_set_margin_top (self->grid, 0);
  gtk_widget_set_margin_bottom (self->grid, 0);
#endif
  gtk_grid_set_column_spacing (GTK_GRID (self->grid), 0);
  gtk_widget_show (self->grid);

  gtk_container_add (GTK_CONTAINER (self), self->grid);

  /* SNI hover: let GTK paint the button's own background instead of cairo.
   * Five independent reviews converged on this after the cairo variants in the
   * container callback failed (fill painted over by the button's repaint). */
  {
    GtkCssProvider *css = gtk_css_provider_new ();

    /* Suppress the theme's own hover background for the drawer's buttons: the
     * theme repaints it on every PRELIGHT change (and animates it over 400 ms
     * in this theme), which is what wiped the cairo fill and left only a flash
     * while the pointer moved. */
    gtk_css_provider_load_from_data (css,
                                     "window.na-overflow button,"
                                     " window.na-overflow button:hover,"
                                     " window.na-overflow button:active {"
                                     " background-image: none;"
                                     " background-color: transparent;"
                                     " transition: none;"
                                     " box-shadow: none; }",
                                     -1, NULL);
    gtk_style_context_add_provider_for_screen (
        gdk_screen_get_default (),
        GTK_STYLE_PROVIDER (css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref (css);
  }

  /* The theme's button padding made an icon cell request extent+2..4 px
   * (measured: pref 30 for a 27 px extent), so the homogeneous grid stretched
   * every cell to that value and the cells stopped matching the icon size -
   * which is also when icons started to go missing on a live resize. */
  {
    GtkCssProvider *css = gtk_css_provider_new ();

    gtk_css_provider_load_from_data (css,
                                     "button, .background button {"
                                     " padding: 0; margin: 0;"
                                     " border-width: 0; min-width: 0;"
                                     " min-height: 0; outline-width: 0; }",
                                     -1, NULL);
    /* USER priority: at APPLICATION the theme's button padding still won and
     * a cell requested extent+1..3 px (measured: pref 50..53 for a 49 px
     * extent), which made the cells unequal and hid an icon. */
    gtk_style_context_add_provider (gtk_widget_get_style_context (self->grid),
                                    GTK_STYLE_PROVIDER (css),
                                    GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref (css);
  }

#ifdef HAVE_X11
  /* Runtime, not compile-time: a +wayland build must still drive an X11
   * session.  On Wayland the menu path below takes over and this hookup must
   * stay out of its way. */
  if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
  /* X11: the drawer is a plain POPUP window, so the hover machinery the menu
   * path wires up never ran here and the drawer shipped with no highlight.
   * Motion on the drawer resolves the cell under the pointer (the resolver
   * walks up from whatever window sent the event), and every cell is wired
   * for crossings and the cell painter as it is added. */
  g_signal_connect (self->grid, "draw",
                    G_CALLBACK (na_overflow_band_draw_cb), self);

  /* NO motion handler on X11: the query poll below is the single source of
   * truth.  Both channels ran before and fought each other - motion set the
   * icon, the poll a tick later resolved "none" in its own coordinates, so the
   * highlight only survived while the pointer was moving (measured: hover
   * alternated SnItemV0/none every 40 ms). */
  g_signal_connect (self->grid, "add",
                    G_CALLBACK (na_overflow_grid_child_added_cb), self);
    }
#endif

  self->panel_orientation = GTK_ORIENTATION_HORIZONTAL;
  self->open_direction = NA_OPEN_UNKNOWN;
  self->autohide_ms = AUTOHIDE_DEFAULT_MS;
  self->moved_x = G_MININT;
  self->moved_y = G_MININT;

}

#ifdef HAVE_WAYLAND
/* The auto-hide on the menu path is driven by MOTION events, not crossings
 * and not coordinate queries.
 *
 * Both alternatives were measured to lie here:
 *
 *  - gdk_device_get_position()/gdk_window_get_device_position(): the frame of
 *    reference flips between root and surface coordinates depending on whether
 *    GDK happens to know the surface position at the moment (same 34x34 popup
 *    logged (1911,13) and (27,34)); gdk_window_get_origin() answers (0,0).
 *  - Crossing events: an xdg_popup grab CONFINES the pointer to the popup
 *    surface, so the compositor keeps sending enter/leave as if the pointer
 *    were always on it - any jiggle produced a phantom "entered" (drawer
 *    pinned open forever), and grazing an icon a phantom "left" (drawer
 *    closed while the cursor was inside). Both measured, five scenarios.
 *
 * What the grab does give us: every motion event carries coordinates
 * RELATIVE TO THE POPUP SURFACE, negative or oversized when the pointer is
 * physically outside. That is event-carried truth - no query, no race.
 * Inside the allocation -> leave_at = 0; outside -> stamp leave_at. The
 * auto-hide poll then folds the drawer after autohide_ms of continuous
 * outside readings. */
static gboolean
na_overflow_menu_motion_cb (GtkWidget      *widget,
                            GdkEventMotion *event,
                            gpointer        user_data)
{
  NaOverflow *self = user_data;
  GdkWindow  *sw   = gtk_widget_get_window (self->menu);
  gboolean    inside;

  /* Motion that arrived on a DESCENDANT's window (an SNI button is a
   * GtkButton, which owns one) means the pointer is over that button.
   * event->x/y there are button-relative and GTK does not re-translate them as
   * the event propagates, so only the window identity is usable. */
  if (sw != NULL && event->window != sw)
    {
      /* Arrived on a descendant (an SNI button owns a window).  While a hover
       * is live the pointer is over an icon, i.e. inside.  With no live hover
       * this is the pointer's final motion carrying the icon's window AFTER
       * the leave crossing - wayfire delivers no motion once the pointer is
       * off the surface, so this is the only channel that reports "gone". */
      if (self->hover_item != NULL)
        self->leave_at = 0;
      else if (self->leave_at == 0)
        self->leave_at = g_get_monotonic_time ();

      return FALSE;
    }

  /* On the shell's own window the coordinates are ROOT-relative (measured:
   * (1907,15) for a popup recorded at (1886,27)), so they compare against the
   * rect computed at open time. */
  inside = event->x >= self->menu_rect.x &&
           event->x <  self->menu_rect.x + self->menu_rect.width  &&
           event->y >= self->menu_rect.y &&
           event->y <  self->menu_rect.y + self->menu_rect.height;

  if (inside)
    self->leave_at = 0;
  else if (self->leave_at == 0)
    self->leave_at = g_get_monotonic_time ();

  g_printerr ("NADBG motion win=%s x=%.0f y=%.0f inside=%d leave_at=%" G_GINT64_FORMAT "\n",
              (event->window == sw) ? "menu" : "other", event->x, event->y,
              (int) inside, self->leave_at);

  return FALSE;   /* normal propagation continues */
}

/* Crossings on the wrapper item.
 *
 * The motion channel stops delivering once the pointer leaves an icon (measured:
 * after the item's leave crossing, no motion event reached us at all, so the
 * drawer hung "inside" forever). The item's own crossings DO fire on the real
 * move - enter when the pointer comes onto the drawer, leave when it goes off -
 * so they complete what motion cannot: the transition to "outside".
 *
 * Synthetic crossings (grab activation) are ignored, as in gtk_menu_shell:
 * they are the pointer being re-parented by the grab, not the pointer moving.
 * Both handlers return FALSE so the shell and the shield still see the event. */
static gboolean
na_overflow_menu_item_leave_cb (GtkWidget        *widget,
                                GdkEventCrossing *event,
                                gpointer          user_data)
{
  NaOverflow *self = user_data;

  (void) widget;

  if (event->mode == GDK_CROSSING_GTK_GRAB ||
      event->mode == GDK_CROSSING_GTK_UNGRAB ||
      event->mode == GDK_CROSSING_STATE_CHANGED)
    return FALSE;

  /* Only the drawer's OWN surface transition is a real "pointer left".
   * A crossing that arrived on an icon's window (an SNI button owns one)
   * propagates up to this windowless item, and GTK synthesizes the item's
   * counterpart with detail=NONLINEAR (3) - not the INFERIOR (2) that a
   * "still inside, it just moved onto a child" filter tests for. Filtering
   * by detail therefore never matched, so every icon-leave stamped leave_at
   * and the phantom enter cleared it again: leave_at stayed 0 and the
   * auto-hide timer never fired (measured: drawer hangs open after the
   * pointer has been over an icon). Judge by window identity instead. */
  {
    GdkWindow *sw = gtk_widget_get_window (self->menu);
    g_printerr ("NADBG leave win=%s detail=%d mode=%d hover=%d\n",
                (sw != NULL && event->window == sw) ? "menu" : "other",
                event->detail, event->mode, self->hover_item != NULL);
    if (sw == NULL || event->window != sw)
      {
        /* A crossing on an icon's window: stamp TENTATIVELY.  The next enter on
         * a neighbour icon or on the drawer background repairs it within the
         * dwell (see the enter handler).  This is the only channel that reports
         * the pointer leaving on Wayland, where motion stops being delivered. */
        if (self->leave_at == 0)
          self->leave_at = g_get_monotonic_time ();
        return FALSE;
      }
  }

  if (self->leave_at == 0)
    self->leave_at = g_get_monotonic_time ();

  return FALSE;
}

static gboolean
na_overflow_menu_item_enter_cb (GtkWidget        *widget,
                                GdkEventCrossing *event,
                                gpointer          user_data)
{
  NaOverflow *self = user_data;

  (void) widget;

  if (event->mode == GDK_CROSSING_GTK_GRAB ||
      event->mode == GDK_CROSSING_GTK_UNGRAB ||
      event->mode == GDK_CROSSING_STATE_CHANGED)
    return FALSE;

  /* An enter on an ICON's window means the pointer came onto that icon: that
   * repairs a tentative stamp from the previous icon's leave.  With no hover
   * live this is a synthesized counterpart, not real motion - ignore it. */
  {
    GdkWindow *sw = gtk_widget_get_window (self->menu);
    if (sw == NULL || event->window != sw)
      {
        if (self->hover_item != NULL)
          self->leave_at = 0;
        return FALSE;
      }
  }

  self->leave_at = 0;

  return FALSE;
}
#endif

#ifdef HAVE_WAYLAND
/* The grab watch is only reachable from the Wayland menu path, so it is
 * compiled with it: an X11-only build would otherwise carry dead statics
 * (measured: -Wunused-function from GCC 16 on na_overflow_arm_grab_poll,
 * whose sole caller lives inside #ifdef HAVE_WAYLAND).
 *
 * The drawer can lose its grab to a sibling popup of this very client, and
 * then it hangs open forever. Measured flow: right-click an icon inside the
 * drawer -> the SNI context menu pops as another xdg_popup and takes the
 * pointer grab. When that menu closes, the drawer is mapped but event-blind:
 * no motion (so the crossing/motion-driven auto-hide freezes wherever it
 * stood) and no outside click. GTK's own grab-broken handler only deactivates
 * a shell that holds an X11 grab (priv->have_xgrab), so on Wayland it does
 * nothing.
 *
 * The first attempt watched for the process grab becoming EMPTY, which never
 * happens: our own shell stays in the GTK grab list the whole time, so
 * gtk_grab_get_current() keeps returning it (measured - the drawer still hung
 * open). The usable signal is the TRANSITION: the context menu is a second
 * grab in this same process, so while it is up the holder is somebody else,
 * and the instant it closes the holder becomes our shell again. That moment
 * is exactly when the compositor will not give us back its grab, so the
 * drawer is orphaned and must fold - the way a click outside does on X11
 * through the clickwatch helper.
 *
 * The grace window covers the popup itself: gtk_menu_popup_at_widget() grabs
 * on map, which lands after the current event is dispatched, so an early tick
 * could see a momentary "nobody grabs" during our own opening.
 *
 * Polls only; it never blocks and never grabs anything itself. */
#define GRAB_POLL_MS 200

static gboolean
na_overflow_grab_poll_cb (gpointer data)
{
  NaOverflow *self = data;
  GtkWidget  *holder;

  self->grab_poll = 0;

  if (!self->menu_up)
    return FALSE;

  holder = gtk_grab_get_current ();

  if (holder != NULL && holder != GTK_WIDGET (self->menu))
    {
      /* Somebody else in this process owns the pointer: an icon's context
       * menu. Remember it and wait for it to go away. */
      self->foreign_grab_seen = TRUE;
      g_debug ("na-overflow: grab held by %s -> watching for its release",
               G_OBJECT_TYPE_NAME (holder));
    }
  else if (self->foreign_grab_seen &&
           g_get_monotonic_time () - self->shown_at
           >= (gint64) AUTOHIDE_GRACE_MS * 1000)
    {
      /* The sibling menu is gone and the grab is ours (or nobody's): the
       * drawer can no longer receive events, so it is finished. */
      g_debug ("na-overflow: sibling menu released the grab -> closing");
      na_overflow_emit_closed (self);
      return FALSE;
    }

  self->grab_poll = g_timeout_add (GRAB_POLL_MS, na_overflow_grab_poll_cb, self);
  return FALSE;
}

static void
na_overflow_arm_grab_poll (NaOverflow *self)
{
  if (self->grab_poll == 0)
    self->grab_poll = g_timeout_add (GRAB_POLL_MS, na_overflow_grab_poll_cb, self);
}
#endif

/* Cancelling is needed from dispose on every backend, so it stays compiled
 * even when the watch above is not. */
static void
na_overflow_cancel_grab_poll (NaOverflow *self)
{
  if (self->grab_poll != 0)
    {
      g_source_remove (self->grab_poll);
      self->grab_poll = 0;
    }
}

/* Wrapper item handler on the menu path: keeps the shell out of it AND
 * delivers clicks to the icon under the cursor.
 *
 * Why the shield is needed at all:
 *  - GtkMenuShell's release handler deactivates the menu when the release lands
 *    on the selected item (gtkmenushell.c: "menu_item == active_menu_item &&
 *    is_selectable" -> gtk_menu_shell_activate_item -> deactivate). Our item
 *    only carries the icon grid, so that path closed the drawer on every click.
 *  - Its enter_notify selects the item under the pointer, and a selected
 *    GtkMenuItem is prelit: the theme paints one giant highlight row behind the
 *    whole grid.
 * GTK3 GtkMenuItem has no "selectable" property (that is GTK4), so the events
 * are blocked here instead.
 *
 * Why a button event must be REDISPATCHED here rather than left to propagate:
 * the shell grabs the pointer when it pops up (gtk_menu_shell_activate ->
 * gtk_grab_add), so GDK routes BUTTON events to the grab and not to the deepest
 * window - measured: every press and release inside the drawer reports
 * event_widget=GtkMenuItem and the SnItem buttons never see them. Crossings are
 * geometric and do reach the buttons, which is exactly why hover/prelight
 * worked while clicking did nothing. The earlier assumption that "events start
 * at the deepest widget and walk up, so the buttons have already had their
 * turn" is false under a menu grab.
 *
 * So the manual button redispatch is gone; what remains of the shield is the
 * crossing bookkeeping and stopping the shell from seeing presses at all. */
/* Which grid child is under the pointer, resolved from an event WITHOUT
 * trusting a single coordinate space.
 *
 * Measured on the live drawer: every SnItem owns a GdkWindow, so a crossing
 * or motion that arrived on a child's window has found its target by
 * construction - its coordinates are child-relative and meaningless for a
 * hit test.  An event on the menu's own window carries menu-window
 * coordinates, and the grid and its children are windowless there, so their
 * allocations live in exactly that space (measured: icons at +5+6 inside a
 * 70x38 menu - shifted by the menu item's padding, which is why comparing
 * event coordinates against child allocations after translating through the
 * grid picked the wrong icon near cell borders).
 *
 * The hit test is per CELL, not per icon rectangle: refresh_overflow_grid()
 * keeps the grid row-/column-homogeneous with zero spacing, so the drawer
 * tiles into ncols x nrows equal cells and the pointer always falls into
 * exactly one of them.  The icon owning that cell is highlighted, and
 * na_overflow_hover_draw_cb() paints the whole cell - grazing the gaps
 * between icons no longer flickers the highlight off.
 *
 * Routing is done by EVENT OWNER, not by comparing GdkWindow pointers
 * (measured: an event over an icon reports a window that equals neither
 * gtk_widget_get_window(menu) nor gtk_widget_get_window(icon) - GTK owns a
 * separate event window per widget, so every pointer comparison fell into the
 * "foreign window" branch and the highlight never appeared at all).
 * gtk_get_event_widget() answers the widget the event was ADDRESSED to, which
 * is the deepest one under the pointer, and its ancestry decides which
 * coordinate space event->x/y are in. */
static GtkWidget *
na_overflow_hover_resolve (NaOverflow *self,
                           GdkEvent   *event,
                           gdouble     x,
                           gdouble     y)
{
  GList         *children, *l;
  GtkWidget     *found = NULL;
  GtkWidget     *owner = (event != NULL) ? gtk_get_event_widget (event) : NULL;
  GtkAllocation  ga;
  gint           ncols = 0, nrows = 0, col, row;
  gdouble        cw, ch;

  /* A pointer event can arrive before the pending layout has been processed
   * - then every child still reports its pre-map allocation and the hit test
   * picks the first icon whatever the pointer is over (measured: the first
   * right-click after opening opened the leftmost icon's menu, the second
   * one the correct menu).  Force the resize before comparing anything. */
  if (gtk_widget_get_realized (self->grid))
    gtk_container_check_resize (GTK_CONTAINER (self->grid));

  children = gtk_container_get_children (GTK_CONTAINER (self->grid));

  /* Addressed to a descendant of the grid (an icon, or the GtkImage inside
   * one): the pointer is on that icon by construction, and event->x/y are
   * relative to that widget - useless for a hit test, so they are not read.
   * Climb to the child of the grid that owns it: that is the cell occupant. */
  for (GtkWidget *w = owner; w != NULL; w = gtk_widget_get_parent (w))
    {
      GtkWidget *child;

      if (w == self->grid)
        {
          for (child = owner;
               child != NULL && gtk_widget_get_parent (child) != self->grid;
               child = gtk_widget_get_parent (child))
            ;
          if (child != NULL)
            found = g_object_ref (child);
          break;
        }

      if (w == self->menu || w == GTK_WIDGET (self))
        break;
    }

  if (found != NULL)
    {
      g_list_free (children);
      return found;
    }

  /* Anything else - the menu window, the wrapper item, the grid's own empty
   * area: event->x/y are in the menu window's space, which is also the space
   * the grid and its (windowless-above-their-own-window) children allocate
   * in, so the cell math below is valid. */
  gtk_widget_get_allocation (self->grid, &ga);
  if (ga.width <= 0 || ga.height <= 0)
    {
      g_list_free (children);
      return NULL;
    }

  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *w = l->data;
      gint       left = 0, top = 0, wid = 1, hei = 1;

      if (!gtk_widget_get_visible (w))
        continue;

      gtk_container_child_get (GTK_CONTAINER (self->grid), w,
                               "left-attach", &left, "top-attach", &top,
                               "width", &wid, "height", &hei, NULL);
      ncols = MAX (ncols, left + wid);
      nrows = MAX (nrows, top + hei);
    }
  g_list_free (children);

  if (ncols <= 0 || nrows <= 0)
    return NULL;

  cw = (gdouble) ga.width / ncols;
  ch = (gdouble) ga.height / nrows;

  col = (gint) floor ((x - ga.x) / cw);
  row = (gint) floor ((y - ga.y) / ch);

  children = gtk_container_get_children (GTK_CONTAINER (self->grid));
  if (col >= 0 && col < ncols && row >= 0 && row < nrows)
    {
      for (l = children; l != NULL; l = l->next)
        {
          GtkWidget *w = l->data;
          gint       left = 0, top = 0, wid = 1, hei = 1;

          if (!gtk_widget_get_visible (w))
            continue;

          gtk_container_child_get (GTK_CONTAINER (self->grid), w,
                                   "left-attach", &left, "top-attach", &top,
                                   "width", &wid, "height", &hei, NULL);
          if (col >= left && col < left + wid &&
              row >= top  && row < top  + hei)
            {
              found = g_object_ref (w);
              break;
            }
        }
    }
  g_list_free (children);


  return found;
}

/* X11 hover poll.
 *
 * The drawer's window is almost entirely covered by the icons' own windows
 * (SNI buttons, XEmbed clients), so X addresses the pointer's motion to THEM
 * and the drawer sees it only in the few uncovered pixels.  Every reviewer of
 * this problem converged on the same answer: ask the server instead of waiting
 * for an event.  gdk_window_get_device_position() does a QueryPointer on X11
 * and answers the position in the drawer's own coordinates plus the deepest
 * window under the cursor, whoever GTK delivered the event to. */
static gboolean
na_overflow_hover_poll_cb (gpointer data)
{
  NaOverflow     *self = data;
  GdkWindow      *win;
  GdkDisplay     *display;
  GdkDevice      *pointer;
  GdkWindow      *under = NULL;
  gint            x = 0, y = 0;

  if (!gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
      self->hover_poll = 0;
      return G_SOURCE_REMOVE;
    }

  win = gtk_widget_get_window (GTK_WIDGET (self));
  if (win == NULL)
    return G_SOURCE_CONTINUE;

  display = gdk_window_get_display (win);
  pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
  if (pointer == NULL)
    return G_SOURCE_CONTINUE;

  under = gdk_window_get_device_position (win, pointer, &x, &y, NULL);

  {
    GtkWidget *cell;
    gint       w = gdk_window_get_width (win);
    gint       h = gdk_window_get_height (win);

    /* `under` is the deepest CHILD window of the drawer, and the icons are not
     * its children (they are separate X windows), so under==NULL while the
     * pointer sits right in the middle of the drawer (measured: x=72 y=12 with
     * a 128x34 window).  Judge by the coordinates instead. */
    (void) under;
    cell = (x >= 0 && y >= 0 && x < w && y < h)
             ? na_overflow_hover_resolve (self, NULL, x, y)
             : NULL;

    na_overflow_set_hover (self, cell);
  }

  return G_SOURCE_CONTINUE;
}

static void
na_overflow_hover_poll_start (NaOverflow *self)
{
#ifdef HAVE_WAYLAND
  if (gdk_display_get_default () != NULL &&
      GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    return;   /* no truthful pointer position there */
#endif

  /* An X11 popup under XWayland: QueryPointer answers the last position inside
   * an X11 window (measured: frozen at 1808,1020 while the pointer was moved
   * away), so the poll keeps re-lighting the icon the crossings just cleared -
   * that was the one-frame blink on every crossing.  The icons' own crossings
   * drive the highlight there. */
  if (g_getenv ("WAYLAND_DISPLAY") != NULL)
    return;

  if (self->hover_poll != 0)
    return;

  self->hover_poll = g_timeout_add (40, na_overflow_hover_poll_cb, self);
}

/* The SNI button under `w` (w itself, or a descendant): that is the widget
 * whose own background GTK paints. */
static GtkWidget *
na_overflow_find_sni (GtkWidget *w)
{
  if (w == NULL)
    return NULL;

  if (SN_IS_ITEM (w))
    return w;

  if (GTK_IS_CONTAINER (w))
    {
      GList *kids = gtk_container_get_children (GTK_CONTAINER (w));
      GList *l;
      GtkWidget *found = NULL;

      for (l = kids; l != NULL && found == NULL; l = l->next)
        found = na_overflow_find_sni (l->data);

      g_list_free (kids);
      return found;
    }

  return NULL;
}

static void
na_overflow_set_sni_hover (GtkWidget *w,
                           gboolean   on)
{
  GtkWidget       *sni = na_overflow_find_sni (w);
  GtkStyleContext *ctx;

  if (sni == NULL)
    return;

  ctx = gtk_widget_get_style_context (sni);
  if (on)
    gtk_style_context_add_class (ctx, "na-tray-hovered");
  else
    gtk_style_context_remove_class (ctx, "na-tray-hovered");

  gtk_widget_queue_draw (sni);
}

/* Install the hovered cell's occupant. Takes the reference the resolver
 * handed out (or NULL). Both affected children repaint: the painter lives on
 * the children, so queueing the grid alone would redraw nothing. */
static void
na_overflow_set_hover (NaOverflow *self,
                       GtkWidget  *owned)   /* transfer of a ref, may be NULL */
{
  GtkWidget *prev = self->hover_item;

  if (prev == owned)
    {
      /* Not an early return: the cell may have been repainted without the
       * fill in the meantime (the drawer repaints on every crossing), and
       * skipping the queue_draw left the second hover invisible. */
      if (owned != NULL)
        {
          gtk_widget_queue_draw (owned);
          g_object_unref (owned);
        }
      return;
    }

  self->hover_item = owned;

  /* Repaint everything the highlight touches: the two cells, and the item's
   * full-height band (the grid's parent is the wrapper item). */
  {
    GtkWidget *host = gtk_widget_get_parent (self->grid);
    if (host != NULL)
      gtk_widget_queue_draw (host);
  }
  if (prev != NULL)
    {
      na_overflow_set_sni_hover (prev, FALSE);
      gtk_widget_queue_draw (prev);
      g_object_unref (prev);
    }
  if (owned != NULL)
    {
      na_overflow_set_sni_hover (owned, TRUE);
      gtk_widget_queue_draw (owned);
    }

}

#ifdef HAVE_WAYLAND
static gboolean
na_overflow_menu_event_shield (GtkWidget *widget,
                               GdkEvent  *event,
                               gpointer   user_data)
{
  NaOverflow *self = user_data;

  if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
    {
      if (event->type == GDK_ENTER_NOTIFY || event->type == GDK_LEAVE_NOTIFY ||
          event->type == GDK_MOTION_NOTIFY)
        {
          GdkEventCrossing *c = (GdkEventCrossing *) event;
          GtkWidget *under = na_overflow_hover_resolve (self, event, c->x, c->y);

          /* No forwarding any more: the highlight is painted by us, and an
           * event over an icon is ADDRESSED to that icon (it owns a window),
           * so GTK sets its state without help.  The manual re-delivery that
           * used to be here translated coordinates against allocations in
           * another window's space, which is the same trap that made the
           * click redispatch open the wrong menu. */
          na_overflow_set_hover (self, under);
        }
      return FALSE;   /* normal propagation continues */
    }

  /* The icons receive button events natively - the widget under the pointer
   * gets them before this item does.  The manual redispatch that used to be
   * here delivered every click a second time, and because it compared the
   * event's coordinates against allocations in another coordinate space it
   * usually landed on the leftmost icon: measured on the live tray, one
   * right-click opened two menus, the correct one and the first icon's.  All
   * this handler has to do is stop the shell from seeing the event. */
  return TRUE;   /* the shell never sees it: no deactivate, no prelight */
}
#endif

NaOverflow *
na_overflow_new (void)
{
  NaOverflow *self = g_object_new (NA_TYPE_OVERFLOW,
                                   "type", GTK_WINDOW_POPUP,
                                   NULL);

#ifdef HAVE_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    {
      /* Wayland has no override-redirect, and every top-level placement route
       * failed here (measured on wayfire): gtk_window_move() is ignored,
       * gdk_window_get_origin() always reads back (0,0), and layer-shell
       * margins did not move the surface to where we asked.
       *
       * The one popup that positions itself correctly in this environment is
       * the classic menu (panel-menu-button.c): a GtkMenu is an xdg_popup, so
       * the COMPOSITOR places it against the parent surface using the two
       * anchors of gtk_menu_popup_at_widget(). Do exactly that here - move the
       * grid into a menu item and never map this window.
       *
       * XEmbed sockets cannot live in the popup, but on Wayland there are
       * none: every overflow item is an SNI button. */
      GtkWidget *item;

      self->use_menu = TRUE;

      self->menu = g_object_ref_sink (gtk_menu_new ());
      gtk_menu_set_reserve_toggle_size (GTK_MENU (self->menu), FALSE);

      item = gtk_menu_item_new ();

      /* Connected BEFORE the shield: the shield returns TRUE and stops the
       * propagation, so the crossings must be recorded first. */
      g_signal_connect (item, "enter-notify-event",
                        G_CALLBACK (na_overflow_menu_item_enter_cb), self);
      g_signal_connect (item, "leave-notify-event",
                        G_CALLBACK (na_overflow_menu_item_leave_cb), self);

      g_signal_connect (item, "button-press-event",
                        G_CALLBACK (na_overflow_menu_event_shield), self);
      g_signal_connect (item, "button-release-event",
                        G_CALLBACK (na_overflow_menu_event_shield), self);
      g_signal_connect (item, "enter-notify-event",
                        G_CALLBACK (na_overflow_menu_event_shield), self);
      g_signal_connect (item, "leave-notify-event",
                        G_CALLBACK (na_overflow_menu_event_shield), self);

      /* The grid is owned by this window. gtk_container_remove() would drop
       * the last reference and DESTROY it, leaving self->grid dangling into
       * recycled memory (measured: every later GTK_GRID(self->grid) cast hit a
       * GtkImage that had taken the freed block). Hold a ref across the move so
       * it survives the gap between remove and add; the menu item owns it
       * afterwards. */
      g_object_ref (self->grid);
      gtk_container_remove (GTK_CONTAINER (self), self->grid);
      gtk_widget_set_halign (self->grid, GTK_ALIGN_START);
      gtk_widget_set_valign (self->grid, GTK_ALIGN_START);
      gtk_container_add (GTK_CONTAINER (item), self->grid);
      g_object_unref (self->grid);

      gtk_menu_shell_append (GTK_MENU_SHELL (self->menu), item);
      gtk_widget_show_all (item);

      if (g_getenv ("NA_PLAIN") == NULL)
        {
      /* The drawer is a single menu item, so the theme paints that item's
         * prelight across the whole drawer.  The item is only a container here -
         * drop its background so the hover feedback comes from the icon under
         * the pointer (crossings are forwarded to it by the shield below).
         * Padding is deliberately left alone: zeroing it shrinks the cells. */
        {
          GtkStyleContext *ctx = gtk_widget_get_style_context (item);
          GtkCssProvider  *prov = gtk_css_provider_new ();
  
          /* The icons are flattened too (no button chrome of their own):
           * the hover highlight is painted by na_overflow_hover_draw_cb()
           * underneath them, and any opaque button background would hide
           * it again. */
          gtk_css_provider_load_from_data (prov,
              "menuitem.na-overflow-host,"
              "menuitem.na-overflow-host:hover,"
              "menuitem.na-overflow-host:selected {"
              "  background: none;"
              "  background-image: none;"
              "  box-shadow: none; }"
              ".na-overflow-icon {"
              "  background: none;"
              "  background-image: none;"
              "  border-color: transparent;"
              "  box-shadow: none; }", -1, NULL);
          /* For the screen, not for the item's context: a provider added to one
           * widget's context applies to that widget only, so a rule targeting
           * the icons never reached them from here. */
          gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                     GTK_STYLE_PROVIDER (prov),
                                                     GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
          gtk_style_context_add_class (ctx, "na-overflow-host");
          g_object_unref (prov);
        }
  
        g_signal_connect (item, "motion-notify-event",
                          G_CALLBACK (na_overflow_hover_motion_cb), self);
        /* Full-height highlight band on the item (paints the padding around
         * the grid; the cell body is painted by the child painter - see
         * na_overflow_stripe_draw_cb).  connect, not connect_after: the item
         * draws its children in the default handler, and the band has to sit
         * under them. */
        g_signal_connect (item, "draw",
                          G_CALLBACK (na_overflow_stripe_draw_cb), self);
        /* The children only carry the crossing wiring (see
         * na_overflow_grid_child_added_cb); the highlight itself is the
         * full-height band drawn by the stripe handler above - the icon
         * surfaces are transparent (the CSS flattens the button chrome),
         * so it shows through them. */
        g_signal_connect (self->grid, "add",
                          G_CALLBACK (na_overflow_grid_child_added_cb), self);
        {
          GList *ch = gtk_container_get_children (GTK_CONTAINER (self->grid));
          GList *c;
          for (c = ch; c != NULL; c = c->next)
            na_overflow_grid_child_added_cb (GTK_CONTAINER (self->grid),
                                             c->data, self);
          g_list_free (ch);
        }
        gtk_widget_add_events (item, GDK_POINTER_MOTION_MASK);
  
          }

      g_signal_connect (self->menu, "deactivate",
                        G_CALLBACK (na_overflow_deactivate_cb), self);

      /* Auto-hide state, see leave_at. Motion on the shell: under the popup
       * grab the compositor confines the pointer to this surface and every
       * motion event carries surface-relative coordinates - the only channel
       * here that tells the truth about where the pointer is. */
      g_signal_connect (self->menu, "motion-notify-event",
                        G_CALLBACK (na_overflow_menu_motion_cb), self);
    }
#endif

  /* X11: GTK_WINDOW_POPUP, exactly like system-tray/fixedtip.c - the window is
   * override-redirect, so no window manager participates in its placement or
   * stacking.  A managed window cannot be used here: compiz applies its own
   * placement after the map and reports it via configure-notify, which lands
   * later than any move we issue, so the popup flashes in the top-left corner
   * before it can be corrected (measured). */
  return self;
}

GtkWidget *
na_overflow_get_grid (NaOverflow *self)
{
  g_return_val_if_fail (NA_IS_OVERFLOW (self), NULL);

  return self->grid;
}

/* Position the popup next to `relative_to`, on the free side of the panel,
 * clamped to the monitor the panel actually lives on.  Uses the monitor API
 * rather than fixedtip's WidthOfScreen() so a panel on a secondary monitor
 * gets its popup on that same monitor. */
void
na_overflow_position (NaOverflow *self)
{
  GdkScreen      *screen;
  GdkDisplay     *display;
  GdkMonitor     *monitor;
  GdkRectangle    geom;
  GdkWindow      *rel_window;
  GdkWindow      *dw;
  GtkAllocation   rel_alloc;
  GtkRequisition  req;
  guint           border;
  gint            root_x = 0;
  gint            root_y = 0;
  gint            x, y;

  /* The Wayland drawer is an xdg_popup: the compositor places it from the
   * anchors passed to gtk_menu_popup_at_widget(), there is nothing to move. */
  if (self->use_menu)
    return;

  if (self->relative_to == NULL || !gtk_widget_get_realized (self->relative_to))
    return;

  screen = gtk_widget_get_screen (self->relative_to);
  display = gdk_screen_get_display (screen);

  rel_window = gtk_widget_get_window (self->relative_to);
  if (rel_window == NULL)
    return;

  monitor = gdk_display_get_monitor_at_window (display, rel_window);
  if (monitor == NULL)
    return;

  gdk_monitor_get_geometry (monitor, &geom);

  gdk_window_get_origin (rel_window, &root_x, &root_y);
  gtk_widget_get_allocation (self->relative_to, &rel_alloc);
  root_x += rel_alloc.x;
  root_y += rel_alloc.y;

  /* The drawer's size, best source first.
   *
   * 1. The real GdkWindow, once mapped - authoritative.
   * 2. The size the grid layout stated for the cells it just assigned.
   * 3. The children's size request - only as a last resort, because it cannot
   *    be trusted pre-map: an SNI button derives its icon size from the
   *    allocation it receives, so before the first allocation the whole grid
   *    collapses to a single cell (measured: request 34x34 = one 26x26 cell +
   *    border, for a drawer that maps far wider). Clamping with an
   *    underestimated width leaves no room against the screen edge, and the
   *    drawer is pushed off the right of the screen - which is what "glued to
   *    the right edge" looked like. */
  border = gtk_container_get_border_width (GTK_CONTAINER (self));

  dw = gtk_widget_get_window (GTK_WIDGET (self));
  if (dw != NULL && gdk_window_get_width (dw) > 1)
    {
      req.width  = gdk_window_get_width (dw);
      req.height = gdk_window_get_height (dw);
    }
  else if (self->nominal_w > 1)
    {
      req.width  = self->nominal_w + 2 * border;
      req.height = self->nominal_h + 2 * border;
    }
  else
    {
      gtk_widget_get_preferred_size (self->grid, NULL, &req);
      req.width  += 2 * border;
      req.height += 2 * border;
    }

  if (self->open_direction != NA_OPEN_UNKNOWN)
    {
      /* The panel states which edge it sits on; that is the direction the
       * drawer opens in, on both backends. */
      switch (self->open_direction)
        {
        case NA_OPEN_DOWN:
          y = root_y + rel_alloc.height;    /* panel on top: downwards */
          x = root_x;
          break;
        case NA_OPEN_UP:
          y = root_y - req.height;          /* panel at bottom: upwards */
          x = root_x;
          break;
        case NA_OPEN_RIGHT:
          x = root_x + rel_alloc.width;     /* panel on the left: rightwards */
          y = root_y;
          break;
        default: /* NA_OPEN_LEFT */
          x = root_x - req.width;           /* panel on the right: leftwards */
          y = root_y;
          break;
        }
    }
  else if (self->panel_orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      /* No panel told us the edge (a standalone testtray): fall back to the
       * screen geometry, which is only trustworthy where window origins are
       * root-relative, i.e. on X11. */
      if (root_y <= geom.y + geom.height / 2)
        y = root_y + rel_alloc.height;      /* panel at the top: downwards */
      else
        y = root_y - req.height;            /* panel at the bottom: upwards */

      x = root_x;
    }
  else
    {
      if (root_x <= geom.x + geom.width / 2)
        x = root_x + rel_alloc.width;       /* panel on the left: rightwards */
      else
        x = root_x - req.width;             /* panel on the right: leftwards */

      y = root_y;
    }

  x = CLAMP (x, geom.x, geom.x + geom.width  - req.width);
  y = CLAMP (y, geom.y, geom.y + geom.height - req.height);

  if (x == self->moved_x && y == self->moved_y)
    return;

  self->moved_x = x;
  self->moved_y = y;

  gtk_window_move (GTK_WINDOW (self), x, y);
}

/* See the call site in na_overflow_popup(): children placed into the drawer
 * grid before the menu was ever shown stay unmapped, and nothing in the GTK
 * map path lifts them out of that state.  Toggling visibility on the SNI items
 * once the popup is on screen does.  XEmbed sockets are excluded by the NaItem
 * check - hiding a socket unmaps the plug's window, which kills the icon. */
/* Theme-independent hover highlight, painted per GRID CELL.
 *
 * Only the Wayland menu path connects these handlers, and they lean on
 * na_overflow_hover_resolve(), which is Wayland-only. */
/* The highlight: one full-height band over the hovered cell's column.
 *
 * Painted on the wrapper menu item - it is windowless, so this lands on the
 * menu surface beneath the icon subsurfaces, and the icons are transparent
 * (the CSS flattens their button chrome), so the band reads continuous from
 * the drawer's top edge to its bottom edge.  The 6px of item padding above
 * and below the grid is part of the band by design: stopping the fill at the
 * grid's allocation made that padding look like a margin INSIDE the drawer
 * and the highlight like an icon-sized patch (measured).
 *
 * The column comes from the layout, not from allocations: the grid is
 * homogeneous with zero spacing (refresh_overflow_grid), so cell geometry is
 * grid_size / filled columns and the hovered child's left-attach. */
#ifdef HAVE_WAYLAND
static gboolean
na_overflow_stripe_draw_cb (GtkWidget *item,
                            cairo_t   *cr,
                            gpointer   user_data)
{
  NaOverflow    *self = user_data;
  GtkAllocation  ga, ia;
  GList         *children, *l;
  gint           left = 0, wid = 1, top = 0, hei = 1, ncols = 0, nrows = 1;
  gdouble        cw, ch, x0, x1, y0, y1;

  if (g_getenv ("NA_PLAIN") != NULL)
    return FALSE;

  if (self->hover_item == NULL)
    return FALSE;

  gtk_widget_get_allocation (self->grid, &ga);
  gtk_widget_get_allocation (item, &ia);
  if (ga.width <= 0 || ga.height <= 0)
    return FALSE;

  children = gtk_container_get_children (GTK_CONTAINER (self->grid));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *w_child = l->data;
      gint       cl = 0, ct = 0, cwid = 1, chei = 1;

      if (!gtk_widget_get_visible (w_child))
        continue;

      gtk_container_child_get (GTK_CONTAINER (self->grid), w_child,
                               "left-attach", &cl, "top-attach", &ct,
                               "width", &cwid, "height", &chei, NULL);
      ncols = MAX (ncols, cl + cwid);
      nrows = MAX (nrows, ct + chei);

      if (w_child == self->hover_item)
        {
          left = cl;
          wid  = cwid;
          top  = ct;
          hei  = chei;
        }
    }
  g_list_free (children);

  if (ncols <= 0)
    return FALSE;

  cw = (gdouble) ga.width / ncols;

  /* The column's own edges, extended to the drawer's side edges for the
   * first/last column - the same reasoning as the vertical extent. */
  x0 = (left == 0) ? 0 : ga.x + left * cw;
  x1 = (left + wid == ncols) ? ia.width : ga.x + (left + wid) * cw;

  cairo_set_source_rgba (cr, 120 / 255.0, 120 / 255.0, 130 / 255.0, 0.38);
  ch = (gdouble) ga.height / nrows;
  /* Same reasoning as the horizontal extent above: the cell starts at the
   * grid's own origin, not at 0, and the first/last row is extended to the
   * item's edges.  Without ga.y the band sat short by the grid offset and the
   * highlight was clipped vertically (visible on Wayland, where the drawer
   * carries a border the X11 path did not). */
  y0 = (top == 0) ? 0 : ga.y + top * ch;
  y1 = (top + hei == nrows) ? ia.height : ga.y + (top + hei) * ch;
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_fill (cr);


  return FALSE;
}
#endif

/* The icons report their own crossings: motion over an icon's surface never
 * reaches the item handler (measured), but enter/leave on the icon's window
 * are reliable. Synthetic crossings (grab activation) are ignored, as
 * everywhere else in this file. */
#define HOVER_CLEAR_DELAY_MS 120

static gboolean
na_overflow_hover_clear_cb (gpointer data)
{
  NaOverflow *self = data;

  self->hover_clear_id = 0;
  na_overflow_set_hover (self, NULL);
  return G_SOURCE_REMOVE;
}

/* The drawer's "the pointer is inside / has left" stamps, written by the
 * icons' own crossings.  On Wayland these are the only truthful channel: motion
 * delivery stops once the pointer leaves the popup surface and QueryPointer in
 * XWayland keeps answering the last position inside an X11 window. */
static void
na_overflow_note_inside (NaOverflow *self)
{
  if (self->leave_at != 0)
    {
      self->leave_at = 0;
      self->outside_since = 0;
    }
}

static void
na_overflow_note_outside (NaOverflow *self)
{
  if (self->leave_at == 0)
    self->leave_at = g_get_monotonic_time ();
}

static gboolean
na_overflow_cell_enter_cb (GtkWidget        *child,
                           GdkEventCrossing *event,
                           gpointer          user_data)
{
  if (event->mode == GDK_CROSSING_GTK_GRAB ||
      event->mode == GDK_CROSSING_GTK_UNGRAB ||
      event->mode == GDK_CROSSING_STATE_CHANGED)
    return FALSE;

  /* The pointer is over this icon: it is inside the drawer.  Clearing the
   * outside stamp here is what repairs the TENTATIVE stamp that the previous
   * icon's leave wrote a few milliseconds ago. */
  na_overflow_note_inside (NA_OVERFLOW (user_data));

  if (NA_OVERFLOW (user_data)->hover_clear_id != 0)
    {
      g_source_remove (NA_OVERFLOW (user_data)->hover_clear_id);
      NA_OVERFLOW (user_data)->hover_clear_id = 0;
    }

  g_object_ref (child);
  na_overflow_set_hover (user_data, child);
  return FALSE;
}

static gboolean
na_overflow_cell_leave_cb (GtkWidget        *child,
                           GdkEventCrossing *event,
                           gpointer          user_data)
{
  NaOverflow *self = user_data;

  if (event->mode == GDK_CROSSING_GTK_GRAB ||
      event->mode == GDK_CROSSING_GTK_UNGRAB ||
      event->mode == GDK_CROSSING_STATE_CHANGED)
    return FALSE;

  /* Guard against the leave of a cell we already left (enter of the next
   * icon may arrive first under a grab): only clear if WE are lit. */
  if (self->hover_item == child)
    {
      /* Leaving the lit icon is the only reliable "the pointer went away"
       * signal on Wayland (motion stops, QueryPointer freezes): stamp it
       * tentatively - an enter on a neighbour icon repairs it immediately. */
      na_overflow_note_outside (self);

      /* Defer the visual clear: crossing the border between two icons (or
       * between an icon and the drawer's padding) produces leave+enter a few
       * milliseconds apart, and clearing synchronously made the highlight
       * blink once per crossing. */
      if (self->hover_clear_id == 0)
        self->hover_clear_id = g_timeout_add (HOVER_CLEAR_DELAY_MS,
                                              na_overflow_hover_clear_cb, self);
    }
  return FALSE;
}
/* The highlight: one band per hovered COLUMN, painted in the grid's own draw.
 *
 * The grid's draw runs before its children, so the band sits UNDER the icons
 * (whatever kind they are), and a cell is the full grid height - the icons keep
 * a 2 px inset inside it, which this fill covers. */
#ifdef HAVE_X11
static gboolean
na_overflow_band_draw_cb (GtkWidget *widget,
                          cairo_t   *cr,
                          gpointer   user_data)
{
  NaOverflow    *self = user_data;
  GtkAllocation  ga;
  GList         *kids, *l;
  GtkWidget     *cell;
  gint           left = 0, top = 0, wid = 1, hei = 1;
  gint           ncols = 1, nrows = 1;
  gdouble        cw, ch, x0, x1, y0, y1;

  if (g_getenv ("NA_PLAIN") != NULL || self->hover_item == NULL)
    return FALSE;

  if (!gtk_widget_get_realized (self->grid))
    return FALSE;

  gtk_widget_get_allocation (self->grid, &ga);
  if (ga.width <= 0 || ga.height <= 0)
    return FALSE;

  cell = self->hover_item;
  while (cell != NULL && gtk_widget_get_parent (cell) != self->grid)
    cell = gtk_widget_get_parent (cell);
  if (cell == NULL)
    return FALSE;

  gtk_container_child_get (GTK_CONTAINER (self->grid), cell,
                           "left-attach", &left, "top-attach", &top,
                           "width", &wid, "height", &hei, NULL);

  kids = gtk_container_get_children (GTK_CONTAINER (self->grid));
  for (l = kids; l != NULL; l = l->next)
    {
      gint l2 = 0, t2 = 0, w2 = 1, h2 = 1;

      if (!gtk_widget_get_visible (l->data))
        continue;

      gtk_container_child_get (GTK_CONTAINER (self->grid), l->data,
                               "left-attach", &l2, "top-attach", &t2,
                               "width", &w2, "height", &h2, NULL);
      ncols = MAX (ncols, l2 + w2);
      nrows = MAX (nrows, t2 + h2);
    }
  g_list_free (kids);

  /* A cell, not a column: with two rows of icons the drawer is two cells high,
   * so filling 0..ga.height painted the whole column whatever the cursor was
   * over.  Both axes come from the child's attach, as the grid allocates them. */
  cw = (gdouble) ga.width / ncols;
  ch = (gdouble) ga.height / nrows;
  x0 = (gdouble) left * cw;
  x1 = (gdouble) (left + wid) * cw;
  y0 = (gdouble) top * ch;
  y1 = (gdouble) (top + hei) * ch;

  cairo_set_source_rgba (cr, 70 / 255.0, 70 / 255.0, 80 / 255.0, 0.45);
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_fill (cr);

  return FALSE;
}
#endif

/* Every child of the drawer grid gets the crossing wiring once, at the moment
 * it is attached: placeholders are created lazily by refresh_grid(), so a
 * single pass over the children at startup would miss them. */
static void
na_overflow_grid_child_added_cb (GtkContainer *grid,
                                 GtkWidget    *child,
                                 gpointer      user_data)
{
  if (g_object_get_data (G_OBJECT (child), "na-cell-hover") == NULL)
    {
      g_object_set_data (G_OBJECT (child), "na-cell-hover",
                         (gpointer) "na-cell-hover");

#ifdef HAVE_X11
      /* A small inset around the icon: the cell is then a few pixels larger than
       * the artwork, and the highlight (painted per CELL, not per icon) covers
       * that inset too - the drawer gets a little air without a bare strip. */
      gtk_widget_set_margin_start (child, 4);
      gtk_widget_set_margin_end (child, 4);
      gtk_widget_set_margin_top (child, 4);
      gtk_widget_set_margin_bottom (child, 4);
#endif

      /* Motion over an icon's own surface never reaches the item handler
       * (measured: only owner=GtkMenuItem events from the padding areas
       * arrived, so the highlight froze while the pointer was ON an icon).
       * Crossings DO reach the icon reliably - the icons report their own
       * enter/leave. */
      gtk_widget_add_events (child, GDK_ENTER_NOTIFY_MASK |
                                       GDK_LEAVE_NOTIFY_MASK);
      g_signal_connect (child, "enter-notify-event",
                        G_CALLBACK (na_overflow_cell_enter_cb), user_data);
      g_signal_connect (child, "leave-notify-event",
                        G_CALLBACK (na_overflow_cell_leave_cb), user_data);
    }
}

#ifdef HAVE_WAYLAND
static gboolean
na_overflow_hover_motion_cb (GtkWidget      *widget,
                             GdkEventMotion *event,
                             gpointer        user_data)
{
  NaOverflow *self = user_data;
  GtkWidget  *under;


  /* No coordinate translation here on purpose: an event that bubbled up from
   * an icon carries ICON-relative coordinates, which would read as garbage in
   * the grid's space.  The resolver routes by event owner instead. */
  under = na_overflow_hover_resolve (self, (GdkEvent *) event, event->x, event->y);
  na_overflow_set_hover (self, under);
  return FALSE;
}
#endif


gboolean
na_overflow_popup (NaOverflow   *self,
                   GtkWidget    *relative_to,
                   GtkOrientation panel_orientation)
{
  g_return_val_if_fail (NA_IS_OVERFLOW (self), FALSE);
  g_return_val_if_fail (GTK_IS_WIDGET (relative_to), FALSE);

  self->panel_orientation = panel_orientation;

  if (self->relative_to != relative_to)
    {
      self->relative_to = relative_to;
      /* Classic GTK idiom: NULL out the pointer if the widget dies first. */
      g_signal_connect (relative_to, "destroy",
                        G_CALLBACK (gtk_widget_destroyed), &self->relative_to);
    }

  /* Follow the panel to whichever screen it is on. */
  gtk_window_set_screen (GTK_WINDOW (self),
                         gtk_widget_get_screen (relative_to));

  /* Force a fresh placement pass even if the geometry repeats. */
  self->moved_x = G_MININT;
  self->moved_y = G_MININT;

#ifdef HAVE_WAYLAND
  if (self->use_menu)
    {
      /* The classic-menu recipe (panel-menu-button.c): anchor the popup
       * against the button and let the compositor place the xdg_popup.
       * Panel on top: the menu hangs below the button; panel at the bottom:
       * it grows upwards; vertical panels open towards the screen middle,
       * exactly the four cases of panel_menu_button_popup_menu(). */
      GdkGravity widget_anchor = GDK_GRAVITY_NORTH_WEST;
      GdkGravity menu_anchor   = GDK_GRAVITY_NORTH_WEST;
      GdkWindow *rw = gtk_widget_get_window (relative_to);
      GtkAllocation rel_alloc = { 0, 0, 0, 0 };
      gint root_x = 0, root_y = 0;

      if (rw != NULL)
        {
          GdkMonitor  *mon = gdk_display_get_monitor_at_window (
                                 gdk_window_get_display (rw), rw);
          GdkRectangle g  = { 0, 0, 1, 1 };
          gint rx = 0, ry = 0;

          if (mon != NULL)
            gdk_monitor_get_geometry (mon, &g);
          gdk_window_get_origin (rw, &rx, &ry);
          gtk_widget_get_allocation (relative_to, &rel_alloc);
          root_x = rx + rel_alloc.x;
          root_y = ry + rel_alloc.y;

          if (self->open_direction != NA_OPEN_UNKNOWN)
            {
              /* The panel told us which edge it sits on. Authoritative on
               * both backends, and the only correct answer on Wayland:
               * gdk_window_get_origin() answers surface-relative for the
               * panel's layer-shell surface, so the geometry guess below
               * reads a bottom panel as a top one there (measured: ry=1 on a
               * 1080p screen whose panel sits at y=1053). */
              switch (self->open_direction)
                {
                case NA_OPEN_DOWN:
                  widget_anchor = GDK_GRAVITY_SOUTH_WEST;  /* top: hang below */
                  break;
                case NA_OPEN_UP:
                  widget_anchor = GDK_GRAVITY_NORTH_WEST;
                  menu_anchor   = GDK_GRAVITY_SOUTH_WEST;  /* bottom: grow up */
                  break;
                case NA_OPEN_RIGHT:
                  widget_anchor = GDK_GRAVITY_NORTH_EAST;  /* left: open right */
                  break;
                default: /* NA_OPEN_LEFT */
                  widget_anchor = GDK_GRAVITY_NORTH_WEST;
                  menu_anchor   = GDK_GRAVITY_NORTH_EAST;  /* right: open left */
                  break;
                }
            }
          else if (panel_orientation == GTK_ORIENTATION_HORIZONTAL)
            {
              if (ry <= g.y + g.height / 2)
                widget_anchor = GDK_GRAVITY_SOUTH_WEST;  /* top: hang below */
              else
                {
                  widget_anchor = GDK_GRAVITY_NORTH_WEST;
                  menu_anchor   = GDK_GRAVITY_SOUTH_WEST; /* bottom: grow up */
                }
            }
          else
            {
              if (rx <= g.x + g.width / 2)
                widget_anchor = GDK_GRAVITY_NORTH_EAST;   /* left: open right */
              else
                {
                  widget_anchor = GDK_GRAVITY_NORTH_WEST;
                  menu_anchor   = GDK_GRAVITY_NORTH_EAST; /* right: open left */
                }
            }
        }

      /* Same association the classic menu makes, so the popup inherits the
       * button's toplevel surface. */
      gtk_window_set_attached_to (
          GTK_WINDOW (gtk_widget_get_toplevel (self->menu)), relative_to);

      /* Where the popup will land: the anchors pair one corner of the button
       * with one corner of the popup, so the top-left follows from the
       * button's root origin and the two sizes. GTK then clamps the popup into
       * the work area of the monitor - the arrow sits 26px from the right
       * screen edge, so an unclamped rect pointed at memory the pointer never
       * visits, and the auto-hide read "pointer outside" while it was inside.
       * The popup's own surface cannot be asked (gdk_window_get_origin answers
       * (0,0) on Wayland), so this computed+clamped rect is the truth. */
      /* Freeze the drawer's true size into its MINIMUM before anything
       * measures it for the popup.
       *
       * Measured, first open of a session: the layout pass states cell=30x26
       * req=90x26, yet gtk_widget_get_preferred_size() on the same grid
       * answers 78x26 at click time - GTK's requisition cache for a grid whose
       * children have never been allocated still holds the pre-heat numbers.
       * The menu latches THAT at gtk_menu_popup_at_widget(), and an xdg_popup
       * cannot be resized afterwards (gtk_window_resize on a mapped menu is
       * swallowed by GDK, measured), so the icons stayed tight until the
       * second open.
       *
       * The size we computed in refresh_grid() is immune: nominal_w is
       * filled columns x max cell width, derived from the children's own
       * measurements, not from the grid's cache. Pinning it as the grid's
       * minimum forces natural >= minimum by construction, so the item and
       * the shell cannot latch anything smaller. */
      {
        GList *ch = gtk_container_get_children (GTK_CONTAINER (self->grid));
        GList *c;
        gint   nvis = 0;
        gint   fw = 0, fh = 0;

        for (c = ch; c != NULL; c = c->next)
          {
            GtkWidget *w = c->data;

            /* sn-item-v0.update() hides Status="Passive" items, and it runs
             * 10 ms after ANY property change - including the icon-size
             * pre-heat above.  When the first click lands in that window
             * every drawer child is invisible, the grid requests nothing
             * (the size filter reads get_visible only), and the menu
             * latches a 10x30 microstrip with scroll arrows (measured).
             * Inside the drawer the passivity policy is superseded by the
             * layout anyway (layout_overflow_grid shows every cell), so
             * state it here too, before measuring.  XEmbed placeholders
             * keep their invisibility on purpose - only SNI buttons are
             * forced. */
            if (g_str_has_prefix (G_OBJECT_TYPE_NAME (w), "SnItem") &&
                !gtk_widget_get_visible (w))
              gtk_widget_show (w);

            if (gtk_widget_get_visible (w))
              nvis++;
          }
        g_list_free (ch);

        if (nvis > 0 && self->nominal_w > 1)
          {
            GtkRequisition grec;

            gtk_widget_get_preferred_size (self->grid, NULL, &grec);
            fw = MAX (grec.width,  self->nominal_w);
            fh = MAX (grec.height, self->nominal_h);

            gtk_widget_set_size_request (self->grid, fw, fh);
            gtk_widget_queue_resize (self->grid);
            if (gtk_widget_get_parent (self->grid) != NULL)
              gtk_widget_queue_resize (gtk_widget_get_parent (self->grid));
            gtk_widget_queue_resize (self->menu);

          }
      }

      {
        GtkRequisition req;
        guint        border = gtk_container_get_border_width (GTK_CONTAINER (self));
        GdkRectangle wa     = { 0, 0, 1, 1 };
        gint         px, py;

        /* The preferred size is the truth even before the first map, since
         * the freeze above pinned the grid's minimum to the size
         * refresh_grid() computed: the buttons' naturals are final by the
         * time the chevron can be clicked. */
        gtk_widget_get_preferred_size (self->grid, NULL, &req);
        if ((req.width <= 1 || req.height <= 1) && self->nominal_w > 1)
          {
            req.width  = self->nominal_w;
            req.height = self->nominal_h;
          }
        req.width  += 2 * border;
        req.height += 2 * border;

        if (widget_anchor == GDK_GRAVITY_SOUTH_WEST)        /* panel on top */
          {
            px = root_x;
            py = root_y + rel_alloc.height;
          }
        else if (menu_anchor == GDK_GRAVITY_SOUTH_WEST)     /* panel at bottom */
          {
            px = root_x;
            py = root_y - req.height;
          }
        else if (widget_anchor == GDK_GRAVITY_NORTH_EAST)   /* panel on the left */
          {
            px = root_x + rel_alloc.width;
            py = root_y;
          }
        else                                                /* panel on the right */
          {
            px = root_x - req.width;
            py = root_y;
          }

        if (rw != NULL)
          {
            GdkMonitor *mon2 = gdk_display_get_monitor_at_window (
                                   gdk_window_get_display (rw), rw);
            if (mon2 != NULL)
              gdk_monitor_get_workarea (mon2, &wa);
          }

        px = CLAMP (px, wa.x, wa.x + wa.width  - req.width);
        py = CLAMP (py, wa.y, wa.y + wa.height - req.height);

        self->menu_rect.x      = px;
        self->menu_rect.y      = py;
        self->menu_rect.width  = req.width;
        self->menu_rect.height = req.height;
      }

      self->menu_up = TRUE;

      gtk_widget_add_events (GTK_WIDGET (self), GDK_POINTER_MOTION_MASK);

      self->shown_at          = g_get_monotonic_time ();
      self->foreign_grab_seen = FALSE;

      /* Watch the process grab for the sibling-popup case, see
       * na_overflow_grab_poll_cb. Armed here, not in the constructor: the poll
       * stops itself while the drawer is down. */
      na_overflow_arm_grab_poll (self);
      na_overflow_hover_poll_start (self);

      /* Seed leave_at based on where the pointer actually is at open time.
       *
       * The chevron is OUTSIDE the popup, so if the user clicks it and does not
       * move onto the drawer, no crossing ever arrives and leave_at would stay
       * 0 ("inside") forever - the drawer never auto-hides. Check the pointer
       * against the rect computed above and seed leave_at = now when it is
       * outside, so the auto-hide timer starts ticking.
       *
       * Trustworthy exactly here: no menu grab exists yet (the popup opens
       * below), so gdk_device_get_position() answers root coordinates. */
      {
        GdkDisplay *dpy    = gdk_screen_get_display (gtk_widget_get_screen (relative_to));
        GdkSeat    *st     = gdk_display_get_default_seat (dpy);
        GdkDevice  *ptr    = st ? gdk_seat_get_pointer (st) : NULL;
        gint        px     = 0, py = 0;
        gboolean    inside;

        if (ptr != NULL)
          gdk_device_get_position (ptr, NULL, &px, &py);

        inside = (ptr != NULL &&
                  px >= self->menu_rect.x &&
                  px <  self->menu_rect.x + self->menu_rect.width  &&
                  py >= self->menu_rect.y &&
                  py <  self->menu_rect.y + self->menu_rect.height);

        self->leave_at = inside ? 0 : g_get_monotonic_time ();
      }

      /* Pass the triggering event: the chevron's own button-release is still
       * being dispatched when "toggled" opens us. Without the trigger event
       * GTK hands that same release to the menu shell, which reads it as a
       * click outside and deactivates the popup 110-320ms after opening
       * (measured: every open died before the auto-hide grace of 400ms, and
       * no click ever reached an icon). With it, the shell ignores the event
       * that opened the menu. */
      {
        GdkEvent *trigger = gtk_get_current_event ();

        gtk_menu_popup_at_widget (GTK_MENU (self->menu), relative_to,
                                  widget_anchor, menu_anchor, trigger);
        if (trigger != NULL)
          gdk_event_free (trigger);
      }

      gtk_widget_queue_draw (GTK_WIDGET (self->grid));

      /* GTK leaves the children that entered the drawer grid while the menu
       * was still hidden unmapped: measured on the live Wayland tray the menu
       * and the grid were sized correctly (62x38 / 60x26) while both SNI items
       * sat at alloc 26x26 with mapped=FALSE - the drawer painted as an empty
       * box, and the following open collapsed it to 10x30 because unmapped
       * cells stop contributing to the requisition.  Items added after the
       * menu was mapped draw normally, so re-showing them once the popup is on
       * screen is what fixes it.  A zero-delay attempt from the grid's "map"
       * handler is too early - GTK has not finished mapping by then. */

      /* Auto-hide runs on this path too. The menu grab normally dismisses the
       * popup on Escape or an outside click ("deactivate"), but the drawer
       * must also fold away when the pointer simply leaves it, exactly as on
       * X11 - and the poll is the only mechanism that can see that.
       * (shown_at was stamped above, before the grab watch was armed.) */
      self->outside_since = 0;
      if (self->autohide_ms > 0 && self->hide_timer == 0)
        self->hide_timer = g_timeout_add (AUTOHIDE_POLL_MS,
                                          na_overflow_autohide_cb, self);

      return TRUE;
    }
#endif

  na_overflow_position (self);

  gtk_widget_realize (GTK_WIDGET (self));
  gtk_widget_show_all (GTK_WIDGET (self));

  /* And again once mapped: the requisition can still change when the icons get
   * their allocation, which moves the clamped position. */
  na_overflow_position (self);

  /* Items were placed while the window was still unmapped; force a repaint so
   * a missed expose cannot leave the popup blank. */
  gtk_widget_queue_draw (GTK_WIDGET (self->grid));

  /* Dismiss on a click anywhere outside, immediately rather than on a timer.
   * The watcher is a separate process; if it is unavailable, the pointer poll
   * started below still dismisses the popup. */
  na_overflow_arm_click_watch (self);

  /* X11: poll for the pointer instead of waiting for events (the drawer's
   * window is covered by the icons' windows). */
  na_overflow_hover_poll_start (self);

  self->shown_at = g_get_monotonic_time ();
  self->outside_since = 0;
  if (self->autohide_ms > 0 && self->hide_timer == 0)
    self->hide_timer = g_timeout_add (AUTOHIDE_POLL_MS,
                                      na_overflow_autohide_cb, self);

  return TRUE;
}

void
na_overflow_popdown (NaOverflow *self)
{
  if (self->hover_poll != 0)
    {
      g_source_remove (self->hover_poll);
      self->hover_poll = 0;
    }

  g_return_if_fail (NA_IS_OVERFLOW (self));

  /* The painted hover highlight references a child of the drawer: drop it on
   * every close, or the next open starts with a stale cell lit up (and a
   * leaked ref). */
  if (self->hover_item != NULL)
    {
      g_object_unref (self->hover_item);
      self->hover_item = NULL;
    }

  if (self->use_menu)
    {
      /* menu_up=FALSE first, so the "deactivate" that follows cannot re-emit
       * "closed". gtk_menu_popdown() is idempotent (it only deactivates an
       * active shell), and it has to run even when the auto-hide poll already
       * cleared the flag - that path emits "closed" and the grid answers by
       * calling here, so skipping the popdown would leave the popup on screen. */
      self->menu_up = FALSE;
      na_overflow_cancel_autohide (self);
      na_overflow_cancel_grab_poll (self);

      /* Release the pre-popup size freeze (see na_overflow_popup): the next
       * open must re-measure, not inherit this open's numbers. */
      gtk_widget_set_size_request (self->grid, -1, -1);

      gtk_menu_popdown (GTK_MENU (self->menu));
      return;
    }

  na_overflow_cancel_autohide (self);
  na_overflow_disarm_click_watch (self);

  if (gtk_widget_get_visible (GTK_WIDGET (self)))
    gtk_widget_hide (GTK_WIDGET (self));
}

void
na_overflow_reposition (NaOverflow *self)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

  if (gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
      na_overflow_fit_popup (self);
      na_overflow_position (self);
    }
}

/* Make a popped-up drawer follow its children's true size.
 *
 * A GtkMenu latches its requisition when it is popped up: measured on the
 * live tray, the drawer opened 78px wide from the icons' PRE-MAP requests
 * and stayed there although the request became 90 the moment the SNI
 * surfaces mapped.  gtk_widget_queue_resize() on the menu, the grid or the
 * toplevel does nothing for a mapped menu shell - the only thing that moves
 * it is sizing the window directly.  An xdg_popup is client-sized, so this
 * works on Wayland; on X11 the POPUP window is ours to size too.
 *
 * The icons' true width is unknowable before their first map (the SNI
 * surface carries it), so the growth on the very first open of a session is
 * inherent to the design; it happens one frame after the icons map.  The
 * popup keeps its top-left corner and grows right/down - one frame can
 * overhang the work-area clamp by the delta (measured 12px) on a
 * right-edge panel, which is the cheaper alternative to a tight drawer. */
void
na_overflow_fit_popup (NaOverflow *self)
{
  GtkWidget      *toplevel;
  GtkRequisition  req;
  GtkAllocation   alloc;
  gint            w, h;

  g_return_if_fail (NA_IS_OVERFLOW (self));

  /* Wayland menu path: an xdg_popup CANNOT be resized after it is shown -
   * measured: gdk ignores gtk_window_resize() for popups (the size is
   * latched at gtk_menu_popup from the requisition).  The tight first open
   * is fixed at the source instead: refresh_overflow_grid() states the
   * SNI cells at their post-map size (extent + button chrome), so the
   * latch is right the first time.  X11 POPUP windows do honor resize, so
   * the safety net stays compiled for that backend. */
  if (self->use_menu)
    return;

  toplevel = GTK_WIDGET (self);
  if (!gtk_widget_get_mapped (toplevel))
    return;   /* not up: the popup sizes itself from the request at open */

  gtk_widget_get_preferred_size (toplevel, NULL, &req);
  gtk_widget_get_allocation (toplevel, &alloc);

  /* Exact requisition, in BOTH directions: MAX() only ever grew the popup,
   * so lowering the icon size left the drawer at its old width. */
  w = MAX (req.width,  1);
  h = MAX (req.height, 1);
  if (w == alloc.width && h == alloc.height)
    return;

  gtk_window_resize (GTK_WINDOW (toplevel), w, h);

}
