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

#include "na-overflow.h"

enum
{
  CLOSED,
  LAST_SIGNAL
};

static guint overflow_signals[LAST_SIGNAL] = { 0 };

static void na_overflow_position (NaOverflow *self);

struct _NaOverflow
{
  GtkWindow     parent_instance;

  GtkWidget    *grid;
  GtkWidget    *relative_to;
  GtkOrientation panel_orientation;

  /* Last position we asked for, so that a size-allocate caused by our own
   * gtk_window_move() does not bounce forever. */
  gint          moved_x;
  gint          moved_y;
  NaOverflowButtonClick click_func;
  gpointer              click_data;
  GDestroyNotify        click_destroy;

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

  if (!gtk_widget_get_visible (GTK_WIDGET (self)))
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
  else if (gtk_widget_get_visible (GTK_WIDGET (self)) && self->hide_timer == 0)
    self->hide_timer = g_timeout_add (AUTOHIDE_POLL_MS,
                                      na_overflow_autohide_cb, self);
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
  na_overflow_disarm_click_watch (NA_OVERFLOW (object));

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
  self->autohide_ms = AUTOHIDE_DEFAULT_MS;
  self->moved_x = G_MININT;
  self->moved_y = G_MININT;

}

NaOverflow *
na_overflow_new (void)
{
  /* GTK_WINDOW_POPUP, exactly like system-tray/fixedtip.c: the window is
   * override-redirect, so no window manager participates in its placement or
   * stacking.  A managed window cannot be used here - compiz applies its own
   * placement after the map and reports it via configure-notify, which lands
   * later than any move we issue, so the popup flashes in the top-left corner
   * before it can be corrected (measured). */
  return g_object_new (NA_TYPE_OVERFLOW,
                       "type", GTK_WINDOW_POPUP,
                       NULL);
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
  GtkAllocation   rel_alloc;
  GtkRequisition  req;
  gint            root_x = 0;
  gint            root_y = 0;
  gint            x, y;

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

  gtk_widget_get_preferred_size (GTK_WIDGET (self), NULL, &req);

  if (self->panel_orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      /* Horizontal panel: the popup opens towards the middle of the screen. */
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

  /* Realize first, so the position below is applied to a window that exists
   * and the map already happens at the right place - that is what removes the
   * flash a managed window cannot avoid. */
  gtk_widget_realize (GTK_WIDGET (self));
  na_overflow_position (self);

  gtk_widget_show_all (GTK_WIDGET (self));

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

  /* The requisition can change once the window is on screen and the icons got
   * their allocation, so place once more after the show. */
  na_overflow_position (self);

  return TRUE;
}

void
na_overflow_popdown (NaOverflow *self)
{
  g_return_if_fail (NA_IS_OVERFLOW (self));

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
