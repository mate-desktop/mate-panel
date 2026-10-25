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

#ifdef HAVE_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "na-overflow.h"

enum
{
  CLOSED,
  LAST_SIGNAL
};

static guint overflow_signals[LAST_SIGNAL] = { 0 };

static void na_overflow_position (NaOverflow *self);
static void na_overflow_arm_grab_poll (NaOverflow *self);
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

  /* Wayland menu path: the crossings on the popup decide, see leave_at. */
  if (self->use_menu)
    return self->leave_at == 0;

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

  return px >= ox && px < ox + w && py >= oy && py < oy + h;
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

  gtk_container_set_border_width (GTK_CONTAINER (self), 4);
  gtk_widget_set_name (GTK_WIDGET (self), "na-overflow");
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "background");

  self->grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (self->grid), 0);
  gtk_grid_set_column_spacing (GTK_GRID (self->grid), 0);
  gtk_widget_show (self->grid);

  gtk_container_add (GTK_CONTAINER (self), self->grid);

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
      self->leave_at = 0;
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

  /* INFERIOR = onto a child window, still inside the drawer. */
  if (event->detail == GDK_NOTIFY_INFERIOR)
    return FALSE;

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

  self->leave_at = 0;

  return FALSE;
}
#endif

/* The drawer can lose its grab to a sibling popup of this very client, and
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
 * So: hit-test the grid, translate the coordinates into the target's space, and
 * feed the event to that widget. `forwarding` stops the re-propagation of the
 * fed event from reaching this handler a second time. */
#ifdef HAVE_WAYLAND
static GtkWidget *
na_overflow_grid_child_at (NaOverflow *self, gdouble x, gdouble y)
{
  GList     *children, *l;
  GtkWidget *found = NULL;

  /* The grid is a windowless child of this item, so its children's allocations
   * share the item's window coordinate space - the same space event->x/y are in
   * when the grab attributes the event to this item. */
  children = gtk_container_get_children (GTK_CONTAINER (self->grid));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget    *w = l->data;
      GtkAllocation a;

      if (!gtk_widget_get_visible (w))
        continue;

      gtk_widget_get_allocation (w, &a);
      if (x >= a.x && x < a.x + a.width && y >= a.y && y < a.y + a.height)
        {
          found = g_object_ref (w);
          break;
        }
    }
  g_list_free (children);

  return found;
}

static gboolean
na_overflow_menu_event_shield (GtkWidget *widget,
                               GdkEvent  *event,
                               gpointer   user_data)
{
  NaOverflow *self = user_data;

  if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
    return TRUE;   /* crossings: recorded by the handlers below this one */

  if (self->forwarding)
    return TRUE;   /* our own redispatch coming back up - swallow it */

  {
    GdkEventButton *b      = (GdkEventButton *) event;
    GtkWidget      *target = na_overflow_grid_child_at (self, b->x, b->y);

    if (target != NULL)
      {
        GtkAllocation a;
        GdkEvent     *copy = gdk_event_copy (event);

        gtk_widget_get_allocation (target, &a);
        ((GdkEventButton *) copy)->x = b->x - a.x;
        ((GdkEventButton *) copy)->y = b->y - a.y;

        /* event->window is deliberately left pointing at the item's window:
         * the GTK dispatch from here on uses the widget's own window, and
         * swapping the pointer would need a ref/unref pair for nothing. */

        self->forwarding = TRUE;
        gtk_widget_event (target, copy);
        self->forwarding = FALSE;

        gdk_event_free (copy);
        g_object_unref (target);
      }
  }

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

      g_signal_connect_swapped (self->menu, "deactivate",
                                G_CALLBACK (na_overflow_emit_closed), self);

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
      {
        GtkRequisition req;
        guint        border = gtk_container_get_border_width (GTK_CONTAINER (self));
        GdkRectangle wa     = { 0, 0, 1, 1 };
        gint         px, py;

        if (self->nominal_w > 1)
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

      /* Timestamp the open BEFORE arming the grab watch: the poll's grace is
       * measured from shown_at. foreign_grab_seen starts fresh for this open. */
      self->shown_at          = g_get_monotonic_time ();
      self->foreign_grab_seen = FALSE;

      /* Watch the process grab for the sibling-popup case, see
       * na_overflow_grab_poll_cb. Armed here, not in the constructor: the poll
       * stops itself while the drawer is down. */
      na_overflow_arm_grab_poll (self);

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
  g_return_if_fail (NA_IS_OVERFLOW (self));

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
    na_overflow_position (self);
}
