/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2007 Christian Persch
 * Copyright (C) 2017 Colomban Wendling <cwendling@hypra.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* Well, actuall'y is the Tray itself, the container for the items.  But
 * NaTray is already taken for the XEMBED part, so for now it's called NaGrid,
 * don't make a big deal out of it. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "na-grid.h"

#ifdef HAVE_X11 
#include "system-tray/na-tray.h"
#endif

#include "status-notifier/sn-host-v0.h"
#include "status-notifier/sn-item.h"
#include "na-overflow.h"

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#include <X11/Xlib.h>
#include <string.h>
#endif

#define MIN_ICON_SIZE_DEFAULT 24

/* How many columns the overflow popup uses before wrapping.  Windows-like:
 * a small grid rather than one long row. */
#define NA_OVERFLOW_COLUMNS 5

typedef struct
{
  GtkGrid        *grid;
  GtkOrientation  orientation;
  gint            rows;
  gint            cols;
  gint            index;
} LayoutData;

struct _NaGrid
{
  GtkGrid    parent;

  gint       icon_padding;
  gint       icon_size;

  gint       min_icon_size;
  gint       cols;
  gint       rows;
  gint       length;

  GSList    *hosts;
  GSList    *items;

  /* Overflow state */
  GHashTable *hidden_ids;
  GHashTable *overflow_ids;
  GtkWidget  *chevron;
  NaOverflow *overflow;
  GSList     *parked;      /* HIDDEN items, referenced by us while detached */
  gchar      *chevron_icon;/* currently set icon name, to avoid churn */

  /* XEmbed parking (see the "XEmbed parking" block below) */
  GSList     *overflow_order;  /* every OVERFLOW item, drawer cell order */
  GSList     *xembed_pending;  /* marked, waiting for its square allocation */
  GSList     *xembed_parked;   /* window currently living in the drawer */
  GtkWidget  *fwd_item;        /* icon the press went to (release follows it) */
  gint        parked_extent;   /* cell size the parked windows were sized for */
  guint       size_idle;       /* idle re-asking the panel for our width, 0 = none */
  gint        size_pending;    /* width that idle should request */
  guint       park_idle;       /* idle source id, 0 when none pending */

  guint       relaying;
  guint       relayout_dirty;
  guint       relayout_pass;   /* depth of the retry loop below */
  guint       destroying;
};

enum
{
  PROP_0,
  PROP_ICON_PADDING,
  PROP_ICON_SIZE
};

G_DEFINE_TYPE (NaGrid, na_grid, GTK_TYPE_GRID)

static void refresh_grid (NaGrid *self);
static NaHost *find_sni_host (NaGrid *self);
static gint   compute_icon_extent (NaGrid *self);
static void unpark_xembed (NaGrid *self, GtkWidget *item);
static void unpark_all_xembed (NaGrid *self);
static void reposition_parked (NaGrid *self);
static void schedule_park (NaGrid *self);
static void na_grid_orientation_notify (GObject    *object,
                                        GParamSpec *pspec,
                                        NaGrid     *self);

static gint
compare_items (gconstpointer a,
               gconstpointer b)
{
  NaItem *item1;
  NaItem *item2;
  NaItemCategory c1;
  NaItemCategory c2;
  const gchar *id1;
  const gchar *id2;

  item1 = (NaItem *) a;
  item2 = (NaItem *) b;

  c1 = na_item_get_category (item1);
  c2 = na_item_get_category (item2);

  if (c1 < c2)
    return -1;
  else if (c1 > c2)
    return 1;

  id1 = na_item_get_id (item1);
  id2 = na_item_get_id (item2);

  return g_strcmp0 (id1, id2);
}

static void
layout_place (GtkWidget  *widget,
              LayoutData *data)
{
  gint col, row, left_attach, top_attach;

  /* row / col number depends on whether we are horizontal or vertical */
  if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      col = data->index / data->rows;
      row = data->index % data->rows;
    }
  else
    {
      row = data->index / data->cols;
      col = data->index % data->cols;
    }

  /* only update item position if it has changed from current */
  gtk_container_child_get (GTK_CONTAINER (data->grid),
                           widget,
                           "left-attach", &left_attach,
                           "top-attach", &top_attach,
                           NULL);

  if (left_attach != col || top_attach != row)
    {
      gtk_container_child_set (GTK_CONTAINER (data->grid),
                               widget,
                               "left-attach", col,
                               "top-attach", row,
                               NULL);
    }

  /* increment to index of next item */
  data->index++;
}

static NaItemState
item_state (NaGrid *self,
            NaItem *item)
{
  const gchar *id;

  id = na_item_get_id (item);
  if (id == NULL)
    return NA_ITEM_STATE_PINNED;

  if (g_hash_table_contains (self->hidden_ids, id))
    return NA_ITEM_STATE_HIDDEN;

  if (g_hash_table_contains (self->overflow_ids, id))
    return NA_ITEM_STATE_OVERFLOW;

  return NA_ITEM_STATE_PINNED;
}

/* The popup deliberately stays open after an icon is activated, exactly like
 * the Windows notification-overflow flyout: the user can work through several
 * hidden icons in one visit instead of reopening it every time.  Dismissal is
 * the chevron, a click outside and Escape - all of which already work.
 *
 * This used to auto-dismiss, and two revisions of that were wrong:
 * - dismissing on "button-press-event" hid the window before the release
 *   arrived, so SnItemV0's clicked vfunc (sn-item.c:
 *   button_class->clicked = sn_item_clicked) never fired and the application
 *   was not activated at all - the icons looked busy and did nothing;
 * - keeping the popup open was blamed for the frozen input of 2026-10-19, but
 *   the actual cause was that the "closed" handler forgot to hide the window,
 *   so the seat grab outlived any way of dismissing it.  With the withdrawal
 *   paths fixed (na-overflow.c) staying open holds no such risk.
 *
 * No per-item signal handler is needed any more, which also removes the
 * connect/disconnect bookkeeping from place_item(). */

/* ------------------------------------------------------------------ *
 * XEmbed parking by moving the SOCKET WINDOW.
 *
 * An XEmbed icon cannot be reparented as a widget: gtk_container_remove() on a
 * GtkSocket brings the plug down (measured, probe1), and GTK3 has no protocol
 * unplug/replug left.  What does work, measured (probe6c/probe6e): keep the
 * widget a child of THIS grid forever and relocate only its GdkWindow into the
 * drawer's toplevel window.  The client's plug window never moves - its parent
 * travels, so the XEMBED relationship is untouched.
 *
 * Discipline the measurements impose (violating any of these killed the plug):
 *   - the socket lives inside a GtkGrid with an explicit size request, never as
 *     the sole child of a bare GtkWindow (such a window forces full-area
 *     allocation in the same step as the reparent);
 *   - never resize the socket window with raw X calls.  GTK's own relayout
 *     traffic against a parked socket is harmless - queue_resize, sibling
 *     changes, panel resize, even changing the parked widget's own
 *     size_request all survived (probe6e P2..P7) - but our own
 *     gdk_window_move_resize() killed it.  So: make the icon square WHILE IT IS
 *     STILL IN THE PANEL (GTK does that resize), then only ever move it.
 *   - the destination must be a plain container window, never another
 *     GtkSocket's window (that aborts GDK: error_trap_pop assertion).
 * ------------------------------------------------------------------ */

static void
unpark_xembed (NaGrid   *self,
               GtkWidget *item)
{
  GdkWindow *sw, *target;

  self->xembed_parked = g_slist_remove (self->xembed_parked, item);
  self->xembed_pending = g_slist_remove (self->xembed_pending, item);

  sw = gtk_widget_get_window (item);
  target = gtk_widget_get_window (GTK_WIDGET (self));

  /* Unconditional: the item is only ever unparked when it was parked, and
   * comparing GTK's notion of the parent window proves nothing - GTK still
   * believes the socket hangs off the panel grid precisely because we never
   * touched the widget tree.  (First version did compare, so the window was
   * never brought home.) */
  if (sw != NULL && target != NULL)
    gdk_window_reparent (sw, target, 0, 0);

  /* Back into the panel's layout.  Only the two properties this code ever
   * touched are restored: the expand flags belong to the upstream placement
   * below (place_item sets hexpand/vexpand for every non-parked path), and
   * re-setting them here would fight it. */
  gtk_widget_set_child_visible (item, TRUE);
  gtk_widget_set_no_show_all (item, FALSE);
}

static void
unpark_all_xembed (NaGrid *self)
{
  while (self->xembed_parked != NULL)
    unpark_xembed (self, self->xembed_parked->data);
}

/* The placeholder box a parked socket owns inside the drawer grid.
 *
 * A parked icon cannot become a child of the drawer grid as a WIDGET
 * (reparenting it kills the plug), so without help the grid knows nothing
 * about it: no cell is reserved, and any position computed by hand drifts out
 * of step with the real layout.  Measured drift: the drawer came out 148x34
 * for 5 items = 28px per cell, while drawer_cell() placed windows at the 26px
 * `extent` pitch - 2px of theme padding per cell, accumulating with the index,
 * which is exactly the overlap reported on the live tray.
 *
 * So each parked item gets an invisible windowless box attached to its cell.
 * Its allocation is the ground truth for both where the socket window goes and
 * where it is painted: the grid and the box are windowless, so the allocation
 * is already expressed in the drawer toplevel's window coordinates - the same
 * space gdk_window_move() uses. */
static GtkWidget *
parked_placeholder (GtkWidget *drawer_grid,
                    GtkWidget *item)
{
  GList *children, *l;
  GtkWidget *found = NULL;

  children = gtk_container_get_children (GTK_CONTAINER (drawer_grid));
  for (l = children; l != NULL; l = l->next)
    {
      if (g_object_get_data (G_OBJECT (l->data), "na-parked-item") == item)
        {
          found = l->data;
          break;
        }
    }
  g_list_free (children);

  return found;
}

/* Position every parked socket window at its cell, and paint it too when a
 * cairo context is given.  Window ops are MOVE/SHOW ONLY - see the discipline
 * block above. */
#ifdef HAVE_X11
/* The parked XEmbed item under the drawer's cursor, or NULL.
 *
 * NOT event->x/y: with owner_events=TRUE a press arriving from a foreign plug
 * window is reported with event->x/y RELATIVE to that window, not to the
 * drawer, so a hit test against the grid allocations would match no cell at
 * all. Take the pointer's root coordinates and subtract the drawer window's
 * origin - the same frame the windowless placeholder boxes are allocated in. */
static GtkWidget *
parked_icon_at (NaGrid         *self,
                GdkEventButton *event)
{
  GSList    *l;
  GtkWidget *drawer_grid;
  GdkWindow *dw;
  gint       ox = 0, oy = 0;   /* drawer window origin, root coords */
  gdouble    lx, ly;           /* cursor in drawer coordinates */

  if (self->overflow == NULL || self->xembed_parked == NULL)
    return NULL;

  dw = gtk_widget_get_window (GTK_WIDGET (self->overflow));
  if (dw == NULL)
    return NULL;

  gdk_window_get_origin (dw, &ox, &oy);
  lx = event->x_root - ox;
  ly = event->y_root - oy;

  /* x_root/y_root/x/y in GdkEventButton are gdouble, NOT int: a %d format
   * would read garbage here. Explicit casts also where coordinates go to Xlib. */
  drawer_grid = na_overflow_get_grid (self->overflow);

  for (l = self->overflow_order; l != NULL; l = l->next)
    {
      GtkWidget      *w = l->data;
      GtkWidget      *ph;
      GtkAllocation   a;

      if (!g_slist_find (self->xembed_parked, w))
        continue;

      ph = parked_placeholder (drawer_grid, w);
      if (ph == NULL)
        continue;

      gtk_widget_get_allocation (ph, &a);
      if (a.width <= 1 || a.height <= 1)
        continue;

      if (lx >= a.x && lx < a.x + a.width &&
          ly >= a.y && ly < a.y + a.height)
        return w;
    }

  return NULL;
}

/* Deliver a click to the client whose plug window is parked in the drawer.
 *
 * There is no other way. GtkSocket deliberately unselects ButtonPressMask on
 * its window (gtk-3-24/gtk/gtksocket.c, gtk_socket_realize): "we don't need
 * this for GtkSocket, so we unselect it here, fixing the crashes in firefox" -
 * measured: all 7 sockets of this tray carry the mask 0x005b8000, without
 * ButtonPress. So a press on a parked icon is not delivered to the client by
 * X. The only remaining route is a synthetic XSendEvent straight into the plug
 * window - exactly Direct mode in
 * xembed-sni-proxy (plasma-workspace/xembed-sni-proxy/sniproxy.cpp).
 *
 * X delivers an event only if the client SELECTED it on that window. Measured
 * on a live tray: of 7 XEmbed icons exactly one does - screenlets-daemon.py
 * (0x0063807f); the other six are 0x00638000, and for them no route works,
 * XTEST included: that is an X protocol rule, not a limit of this
 * implementation. */
static gboolean
parked_icon_send_button (GtkWidget     *item,
                         GdkEventButton *event,
                         gboolean       is_press)
{
  GdkWindow  *plug;
  GdkDisplay *gdisplay;
  Display    *dpy;
  Window      plug_xid, root;
  XButtonEvent xb;
  gint        px = 0, py = 0;
  Window      child_ret = None;

  if (!GTK_IS_SOCKET (item))
    return FALSE;

  plug = gtk_socket_get_plug_window (GTK_SOCKET (item));
  if (plug == NULL)
    return FALSE;

  gdisplay = gdk_window_get_display (plug);
  dpy      = GDK_WINDOW_XDISPLAY (plug);
  plug_xid = GDK_WINDOW_XID (plug);
  root     = RootWindow (dpy, DefaultScreen (dpy));

  gdk_x11_display_error_trap_push (gdisplay);

  /* Cursor root coordinates -> coordinates INSIDE the plug window: GTK matches
   * x/y against its own window geometry, so translate to there. */
  /* child_return must NOT be NULL: unlike the GDK wrappers, Xlib writes all
   * three out-parameters unconditionally (libX11 XTranslateCoordinates+0xa9 is
   * `mov %rax,(%r15)`), so a NULL argument meant a write to address 0 -
   * "segfault at 0 ... error 6", four applet deaths on right click. */
  if (!XTranslateCoordinates (dpy, root, plug_xid,
                              (int) event->x_root, (int) event->y_root,
                              &px, &py, &child_ret))
  {
  /* Assignment, not (void): GCC does not treat a cast as silencing
   * warn_unused_result. No need to handle the failure here - if translate
   * failed the window is gone and the click simply does not go. */
  gdk_x11_display_error_trap_pop_ignored (gdisplay);
  return FALSE;
  }

  memset (&xb, 0, sizeof (xb));
  xb.type        = is_press ? ButtonPress : ButtonRelease;
  xb.display     = dpy;
  xb.window      = plug_xid;
  xb.root        = root;
  xb.subwindow   = None;
  xb.time        = event->time;
  xb.x           = px;
  xb.y           = py;
  xb.x_root      = (int) event->x_root;   /* gdouble -> int */
  xb.y_root      = (int) event->y_root;
  xb.same_screen = True;
  xb.send_event  = True;
  xb.state       = event->state;
  xb.button      = event->button;

  /* propagate=False: this window only; the mask is the one the client must
     have selected, or X discards the event. */
  XSendEvent (dpy, plug_xid, False,
              is_press ? ButtonPressMask : ButtonReleaseMask,
              (XEvent *) &xb);
  XFlush (dpy);

  /* The pop result is not examined here: XSendEvent is asynchronous, so an
     error (window gone between translate and send) arrives later. We only
     check that the trap itself reported no error. */
  {
    int err = gdk_x11_display_error_trap_pop (gdisplay);
    return (err == 0);
  }
}
#endif /* HAVE_X11 */

/* Popup callback for a press/release inside the drawer. */
static gboolean
na_grid_overflow_button_cb (GdkEventButton *event,
                            gboolean        is_press,
                            gpointer        user_data)
{
#ifdef HAVE_X11
  NaGrid  *self = user_data;
  GtkWidget *item;

  /* The release goes to the SAME window that got the press (the cursor may
   * have moved off), so the target is remembered in the grid rather than
   * re-found by position. We hold a reference to it: the client can die between
   * press and release, the socket is destroyed on plug-removed, and without the
   * ref this would be a use-after-free. */
  if (is_press)
    {
      GtkWidget *hit = parked_icon_at (self, event);

      if (hit != self->fwd_item)
        {
          if (self->fwd_item != NULL)
            g_object_unref (self->fwd_item);
          self->fwd_item = (hit != NULL) ? g_object_ref (hit) : NULL;
        }
    }

  item = self->fwd_item;
  if (item == NULL)
    return FALSE;

  {
    gboolean ok;

    /* Deliver BEFORE dropping the ref: the unref may be the last one and
     * destroy the widget, while the window must stay alive through XSendEvent. */
    ok = parked_icon_send_button (item, event, is_press);

    if (!is_press)
      {
        self->fwd_item = NULL;
        g_object_unref (item);   /* the ref taken on press */
      }

    return ok;
  }
#else
  (void) event; (void) is_press; (void) user_data;
  return FALSE;
#endif
}

static void
parked_place_and_paint (NaGrid *self,
                        cairo_t *cr)
{
  GSList *l;
  GtkWidget *drawer_grid;

  if (self->xembed_parked == NULL || self->overflow == NULL)
    return;

  drawer_grid = na_overflow_get_grid (self->overflow);

  for (l = self->overflow_order; l != NULL; l = l->next)
    {
      GtkWidget *w = l->data;
      GtkWidget *ph;
      GdkWindow *sw;
      GtkAllocation a;
      gint sw_w, sw_h, x, y;

      if (!g_slist_find (self->xembed_parked, w))
        continue;

      sw = gtk_widget_get_window (w);
      if (sw == NULL)
        continue;

      ph = parked_placeholder (drawer_grid, w);
      if (ph == NULL)
        continue;

      gtk_widget_get_allocation (ph, &a);
      if (a.width <= 1 || a.height <= 1)
        continue;   /* not laid out yet; a later draw pass will place it */

      /* Centre the socket's own window inside its cell: the cell can be a
       * couple of pixels wider than the icon because of theme padding, and
       * centring keeps clear of the neighbours either way. */
      sw_w = gdk_window_get_width (sw);
      sw_h = gdk_window_get_height (sw);
      x = a.x + (a.width  - sw_w) / 2;
      y = a.y + (a.height - sw_h) / 2;

      gdk_window_show (sw);
      gdk_window_move (sw, x, y);

      if (cr != NULL)
        {
          cairo_save (cr);
          gdk_cairo_set_source_window (cr, sw, x, y);
          cairo_rectangle (cr, x, y, sw_w, sw_h);
          cairo_clip (cr);
          cairo_paint (cr);
          cairo_restore (cr);
        }
    }
}

/* Paint the parked sockets into the drawer.
 *
 * Mandatory, not decoration: every tray socket is created composited
 * (na-tray-child.c calls gdk_window_set_composited(), depth 32), which means
 * the X server does NOT display it as an ordinary child - its content lives in
 * a pixmap and only the client that redirects it can draw it.  That is why
 * upstream paints XEmbed icons itself in na_grid_draw() via
 * na_item_draw_on_parent().  A parked socket's window is a child of the DRAWER
 * toplevel now, so nothing paints it: measured with probe7 - the window is
 * mapped, IsViewable, at the right coordinates, its pixmap holds the real icon
 * (mean RGB 44,82,103, luminance 0..245) - and it is invisible.
 *
 *
 * Positions come from the placeholders' allocations, never from arithmetic -
 * see parked_placeholder().  connect_after on the drawer, so its background
 * and the SNI buttons are drawn first. */
static gboolean
parked_draw_cb (GtkWidget *overflow_window,
                cairo_t   *cr,
                NaGrid    *self)
{
  parked_place_and_paint (self, cr);

  return FALSE;
}

/* Place every parked socket window at its cell.  Also (re)maps them:
 * child_visible=FALSE leaves the window unmapped, and the drawer closing
 * unmaps the whole toplevel, so a reopen needs an explicit show. */
static void
reposition_parked (NaGrid *self)
{
  parked_place_and_paint (self, NULL);

  /* The sockets are invisible to the X server (composited) - see
   * parked_draw_cb() - so moving their windows shows nothing.  The drawer has
   * to be repainted for them to appear. */
  if (self->overflow != NULL)
    gtk_widget_queue_draw (GTK_WIDGET (self->overflow));
}

/* Park every pending socket window.  Synchronous on purpose: the idle version
 * ran AFTER the drawer was shown, so a newly assigned icon appeared one frame
 * late.  Returns TRUE when everything pending was handled.
 *
 * There is deliberately NO allocation gate here.  The first version required
 * gtk_widget_get_allocated_width() == extent before parking, but the item is
 * already out of the layout by then (child_visible=FALSE), and GTK gives such
 * a child a 1x1 allocation - measured twice, in probe4 and again on the live
 * tray - so the gate could never pass, the idle re-armed itself forever, and
 * the icon sat unmapped on the panel while the loop hammered the socket with
 * set_size_request() + resize queues every frame.  That churn is what left the
 * client drawing a blank icon.  The X window keeps its real 26x26 size across
 * child_visible=FALSE (measured on the live tray), so nothing needs fixing up:
 * we move the window and it arrives the right size. */
static gboolean
park_xembed_now (NaGrid *self)
{
  GSList *l;
  gint extent;

  if (self->destroying || self->xembed_pending == NULL)
    return TRUE;

  /* The drawer only has a window once it has been shown at least once, and
   * there is nowhere to park a socket window before that.  Leaving the items
   * in xembed_pending is correct: chevron_toggled_cb() re-schedules the park
   * on every opening, so they land in the drawer the first time it opens.
   * Until then the icon is simply off the panel - which is what "in the
   * overflow" means anyway, and matches the Windows flyout. */
  if (self->overflow == NULL ||
      !gtk_widget_get_realized (GTK_WIDGET (self->overflow)) ||
      gtk_widget_get_window (GTK_WIDGET (self->overflow)) == NULL)
    return FALSE;   /* no drawer window yet: nothing to park into */

  extent = compute_icon_extent (self);

  for (l = self->overflow_order; l != NULL; l = l->next)
    {
      GtkWidget *w = l->data;
      GdkWindow *sw, *dst;

      if (!g_slist_find (self->xembed_pending, w) ||
          g_slist_find (self->xembed_parked, w))
        continue;

      sw = gtk_widget_get_window (w);
      if (sw == NULL)
        continue;

      dst = gtk_widget_get_window (GTK_WIDGET (self->overflow));
      if (dst == NULL)
        continue;

      gdk_window_reparent (sw, dst, 0, 0);
      self->xembed_parked = g_slist_prepend (self->xembed_parked, w);
      self->xembed_pending = g_slist_remove (self->xembed_pending, w);
      self->parked_extent = extent;
    }

  reposition_parked (self);

  return (self->xembed_pending == NULL);
}

static gboolean
park_xembed_idle (gpointer data)
{
  NaGrid *self = data;

  self->park_idle = 0;

  /* Single shot.  If the drawer has no window yet the items simply stay
   * pending and chevron_toggled_cb() parks them on the first open - re-arming
   * from here is what turned into the busy loop described above. */
  park_xembed_now (self);
  return FALSE;
}

static void
schedule_park (NaGrid *self)
{
  if (self->park_idle == 0)
    self->park_idle = g_idle_add (park_xembed_idle, self);
}

/* Move an item to the container that matches its state.  The item is
 * referenced across the detach/attach pair because nothing would own it in
 * between otherwise. */
static void
place_item (NaGrid     *self,
            GtkWidget  *item,
            NaItemState state)
{
  GtkWidget *target;
  GtkWidget *parent;

  switch (state)
    {
    case NA_ITEM_STATE_PINNED:
      target = GTK_WIDGET (self);
      break;
    case NA_ITEM_STATE_OVERFLOW:
      target = na_overflow_get_grid (self->overflow);
      break;
    case NA_ITEM_STATE_HIDDEN:
    default:
      target = NULL;
      break;
    }

  parent = gtk_widget_get_parent (item);

  /* An XEmbed socket already living in the drawer stays there while its state
   * is still OVERFLOW: there is nothing to redo, and re-marking it here would
   * only leave a stale entry in xembed_pending. */
  if (state == NA_ITEM_STATE_OVERFLOW && !SN_IS_ITEM (item) &&
      g_slist_find (self->xembed_parked, item))
    return;

  /* Everything else has to come home first, or the window would be left in the
   * drawer while the widget is being placed on the panel. */
  if (g_slist_find (self->xembed_parked, item) ||
      g_slist_find (self->xembed_pending, item))
    {
      unpark_xembed (self, item);
      parent = gtk_widget_get_parent (item);
    }

  if (state == NA_ITEM_STATE_OVERFLOW && !SN_IS_ITEM (item))
    {
      /* System-tray icon into the drawer: the WIDGET stays a child of this
       * grid (moving it would kill the plug), only its window travels.
       *
       * No hexpand/size_request games: the socket already carries its panel
       * size in the X window (26x26 on this tray), child_visible=FALSE frees
       * the cell, and the window keeps its size while unmapped.  Touching the
       * size here would either be a GTK resize of a widget that is leaving the
       * layout or a raw resize of the window - the latter is lethal. */
      if (parent == NULL)
        gtk_container_add (GTK_CONTAINER (self), item);  /* sinks the float */

      gtk_widget_set_no_show_all (item, TRUE);
      gtk_widget_set_child_visible (item, FALSE);

      /* Which cell it ends up in is decided by refresh_grid(), AFTER the
       * icon columns are known - see the parking pass there. */

      if (!g_slist_find (self->xembed_pending, item))
        self->xembed_pending = g_slist_prepend (self->xembed_pending, item);
      schedule_park (self);
      return;
    }

  if (target == NULL)
    {
      if (!SN_IS_ITEM (item))
        {
          /* Hidden system-tray icon: hide it where it stands.  Parking is not
           * an option - gtk_container_remove() on a GtkSocket brings the plug
           * down (measured: "plug-removed" arrives, socket_probe step 4), and
           * a raw XReparentWindow out of it is caught by GtkPlug itself
           * (gtkplug.c:941 plus an assert in gtk_plug_set_is_child).
           *
           * Staying attached is harmless for the layout, measured: GtkGrid
           * excludes invisible children from its size request (three visible
           * 30px children with a fourth hidden one attached at column 40 still
           * requested 90px, not 1200px), and putting a visible child in the
           * cell an invisible one occupies produces no diagnostic.
           *
           * na_grid_draw() has to skip invisible children explicitly: the
           * XEmbed alpha path paints the socket window straight into the
           * parent and never consults gtk_widget_is_drawable(). */
          if (parent == NULL)
            gtk_container_add (GTK_CONTAINER (self), item);  /* sinks the float */

          gtk_widget_hide (item);
          return;
        }

      /* Hidden.  ref_sink because an item that has never been placed is still
       * floating: the plain ref would warn and gain nothing. */
      /* Parking is safe because the SNI host keeps its own ref on every item
       * it created (sn-host-v0.c: g_object_ref_sink at construction), and
       * only SNI items ever park (item_state pins XEmbed items).  Our parked
       * ref is an addition, never the last one: dropping it below cannot
       * resurrect-free the widget. */
      if (!g_slist_find (self->parked, item))
        {
          g_object_ref_sink (item);
          self->parked = g_slist_prepend (self->parked, item);
        }

      if (parent != NULL)
        gtk_container_remove (GTK_CONTAINER (parent), item);
      return;
    }

  if (parent == target)
    {
      if (state != NA_ITEM_STATE_OVERFLOW)
        gtk_widget_set_size_request (item, -1, -1);

      /* A hidden system-tray icon is still a child of this grid, so coming
       * back from HIDDEN lands here, in the early return: the container is
       * already right and only the visibility has to be restored.  Without
       * the show below the icon would stay invisible forever. */
      if (!gtk_widget_get_visible (item))
        gtk_widget_show (item);

      return;
    }

  if (state != NA_ITEM_STATE_OVERFLOW)
    gtk_widget_set_size_request (item, -1, -1);

  if (g_slist_find (self->parked, item))
    {
      self->parked = g_slist_remove (self->parked, item);
      g_object_unref (item);
      parent = NULL;   /* a parked item is by definition unparented */
    }

  gtk_widget_set_hexpand (item, TRUE);
  gtk_widget_set_vexpand (item, TRUE);

  if (parent != NULL)
    {
      /* Reparenting: nothing owns the item between remove and add, so hold it
       * across the gap.  A freshly arrived item is still floating and
       * gtk_container_add() sinks it for us, so it must not take this path. */
      g_object_ref (item);
      gtk_container_remove (GTK_CONTAINER (parent), item);
      gtk_container_add (GTK_CONTAINER (target), item);
      g_object_unref (item);
    }
  else
    gtk_container_add (GTK_CONTAINER (target), item);

  gtk_widget_show (item);
}

/* The cell size an overflow icon must take.
 *
 * On the panel the cell height is imposed by the panel itself, and
 * sn_item_v0_size_allocate() derives effective_icon_size from that
 * allocation.  In the popup there is no such outer constraint: the window is
 * sized from the children's request, the request comes from the icon size, and
 * the icon size comes from the allocation - a circle that collapses to 1x1 and
 * yields an empty popup.  Breaking it requires stating the size explicitly. */
static gint
compute_icon_extent (NaGrid *self)
{
  GtkAllocation allocation;
  GtkOrientation orientation;
  gint          extent;

  if (self->icon_size > 0)
    return self->icon_size;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
  gtk_widget_get_allocation (GTK_WIDGET (self), &allocation);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    extent = (self->rows > 0) ? allocation.height / self->rows : 0;
  else
    extent = (self->cols > 0) ? allocation.width / self->cols : 0;

  if (extent < self->min_icon_size)
    extent = self->min_icon_size;

  return extent;
}

static void
layout_overflow_grid (NaGrid  *self,
                      GSList  *overflow_items)
{
  GtkGrid  *grid = GTK_GRID (na_overflow_get_grid (self->overflow));
  gint      cols = NA_OVERFLOW_COLUMNS;
  gint      extent;
  gint      n    = 0;
  gint      cell_w, max_cell_w = 0, max_cell_h = 0;

  /* Homogeneous again now that every overflow item owns a real cell in this
   * grid (SNI buttons directly, parked sockets through a placeholder box):
   * cells are uniform and positions come from allocations instead of
   * arithmetic, so theme padding can no longer drift one kind against the
   * other. */
  gtk_grid_set_row_homogeneous (grid, TRUE);
  gtk_grid_set_column_homogeneous (grid, TRUE);

  extent = compute_icon_extent (self);

  /* Drop placeholders whose item left the overflow.  Comparing the stored
   * pointer is safe: g_slist_find only compares, never dereferences, so a
   * freed item cannot be followed. */
  {
    GList *children = gtk_container_get_children (GTK_CONTAINER (grid));
    GList *c;

    for (c = children; c != NULL; c = c->next)
      {
        gpointer owner = g_object_get_data (G_OBJECT (c->data), "na-parked-item");

        if (owner != NULL && !g_slist_find (overflow_items, owner))
          gtk_widget_destroy (c->data);
      }
    g_list_free (children);
  }

  for (GSList *l = overflow_items; l != NULL; l = l->next)
    {
      GtkWidget *widget = GTK_WIDGET (l->data);
      GtkWidget *cell;
      gint left_attach, top_attach;
      gint col = n % cols;
      gint row = n / cols;

      if (SN_IS_ITEM (widget))
        cell = widget;
      else
        {
          /* Parked system-tray icon: reserve its cell with an invisible
           * windowless box, see parked_placeholder(). */
          cell = parked_placeholder (GTK_WIDGET (grid), widget);
          if (cell == NULL)
            {
              cell = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
              g_object_set_data (G_OBJECT (cell), "na-parked-item", widget);
              gtk_container_add (GTK_CONTAINER (grid), cell);
            }
        }

      /* The cell has to be at least as wide as the icon window that goes into
       * it.  The PANEL decides that width: a parked socket keeps being a child
       * of the panel grid and GtkSocket resizes its window to whatever cell it
       * is attached to, and those cells are not all `extent` - an SNI button's
       * column measured 28px against 26px for an XEmbed one, and the chevron's
       * is 32px (probe_grid_cv).  Asking for `extent` only would then centre a
       * 28px window in a 26px cell and spill 1px onto each neighbour, which is
       * exactly the overlap being fixed here. */
      if (SN_IS_ITEM (widget))
        {
          gtk_widget_set_size_request (cell, extent, extent);
          cell_w = extent;
        }
      else
        {
          GdkWindow *sw = gtk_widget_get_window (widget);
          gint       sw_w = (sw != NULL) ? gdk_window_get_width (sw) : extent;

          cell_w = MAX (sw_w, extent);
          gtk_widget_set_size_request (cell, cell_w, extent);
        }

      /* The grid is homogeneous, so every cell ends up the size of the largest
       * one requested; track it to state the drawer's size up front. */
      if (cell_w > max_cell_w)
        max_cell_w = cell_w;
      if (extent > max_cell_h)
        max_cell_h = extent;

      /* Expansion must go: place_item() set it for every item (upstream
       * behaviour, needed on the panel), and here it lets the SNI buttons
       * absorb the whole spare width while parked windows keep the extent
       * pitch - that is the overlap. */
      gtk_widget_set_hexpand (cell, FALSE);
      gtk_widget_set_vexpand (cell, FALSE);

      gtk_container_child_get (GTK_CONTAINER (grid), cell,
                               "left-attach", &left_attach,
                               "top-attach", &top_attach,
                               NULL);

      if (left_attach != col || top_attach != row)
        gtk_container_child_set (GTK_CONTAINER (grid), cell,
                                 "left-attach", col,
                                 "top-attach", row,
                                 NULL);

      gtk_widget_show (cell);
      n++;
    }

  /* No explicit size reservation any more: with placeholders in the grid its
   * own natural size already covers every overflow item. */
  gtk_widget_set_size_request (GTK_WIDGET (grid), -1, -1);

  /* State the drawer's size for the placement math. Its own size request is
   * worthless before the first allocation (see na_overflow_set_nominal_size),
   * but the layout above knows exactly how many cells of what size it placed:
   * the grid is homogeneous, so it is the largest cell times the filled rows
   * and columns. */
  if (n > 0 && max_cell_w > 0)
    na_overflow_set_nominal_size (self->overflow,
                                  MIN (n, cols) * max_cell_w,
                                  ((n + cols - 1) / cols) * max_cell_h);
  else
    na_overflow_set_nominal_size (self->overflow, 0, 0);
}

static void
update_chevron_icon (NaGrid *self)
{
  GdkDisplay      *display;
  GdkMonitor      *monitor;
  GdkRectangle     geom;
  GdkWindow       *window;
  GtkAllocation    alloc;
  const gchar     *icon;
  gchar           *next;
  gint             root_x = 0, root_y = 0;

  if (self->chevron == NULL || !gtk_widget_get_realized (self->chevron))
    return;

  window = gtk_widget_get_window (self->chevron);
  if (window == NULL)
    return;

  display = gdk_window_get_display (window);
  monitor = gdk_display_get_monitor_at_window (display, window);
  if (monitor == NULL)
    return;

  gdk_monitor_get_geometry (monitor, &geom);
  gdk_window_get_origin (window, &root_x, &root_y);
  gtk_widget_get_allocation (self->chevron, &alloc);
  root_x += alloc.x;
  root_y += alloc.y;

  if (gtk_orientable_get_orientation (GTK_ORIENTABLE (self)) == GTK_ORIENTATION_HORIZONTAL)
    /* Panel at the bottom of the screen opens upwards, and vice versa. */
    icon = (root_y > geom.y + geom.height / 2) ? "pan-up-symbolic" : "pan-down-symbolic";
  else
    icon = (root_x > geom.x + geom.width / 2) ? "pan-start-symbolic" : "pan-end-symbolic";

  next = g_strdup (icon);
  if (g_strcmp0 (self->chevron_icon, next) == 0)
    {
      g_free (next);
      return;
    }

  g_free (self->chevron_icon);
  self->chevron_icon = next;

  gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->chevron))),
                                self->chevron_icon, GTK_ICON_SIZE_MENU);
  gtk_image_set_pixel_size (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->chevron))),
                            self->icon_size > 0 ? self->icon_size
                                                : CLAMP (self->min_icon_size, 16, 24));
}

/* The panel drives the applet's toplevel size through the plug negotiation, and
 * that costs a round trip: when the requisition changes, the matching allocation
 * arrives one layout pass later.  Usually the round trip re-arms itself; with
 * parked icons in play it sometimes does not, and the applet keeps the OLD width
 * forever - the newest icon is then squeezed into the chevron's cell (measured on
 * the live tray: 7 icons pinned, grid_req=214, toplevel_req=214, toplevel_alloc
 * stuck at 188 for 20+ s, and a second dconf poke was enough to unfreeze it).
 *
 * So whenever a layout pass ends with requisition != allocation, ask for the size
 * explicitly once from an idle (not inline: resizing during size_allocate is how
 * resize loops start). */
static gboolean
request_size_idle (gpointer data)
{
  NaGrid *self = data;
  GtkWidget *top;

  self->size_idle = 0;

  if (self->destroying || self->size_pending <= 0)
    return FALSE;

  top = gtk_widget_get_toplevel (GTK_WIDGET (self));
  if (top == NULL || !GTK_IS_WINDOW (top) || !gtk_widget_get_realized (top))
    return FALSE;

  if (gtk_widget_get_allocated_width (top) != self->size_pending)
    gtk_window_resize (GTK_WINDOW (top), self->size_pending, -1);

  self->size_pending = 0;
  return FALSE;
}

static void
refresh_grid (NaGrid *self)
{
  GSList *pinned;
  GSList *overflow_items;
  LayoutData data;
  gint n_cells;
  gint rows, cols;
  GtkAllocation allocation;
  GtkOrientation orientation;
  gboolean show_chevron;

  if (self->destroying)
    return;

  if (self->relaying)
    {
      /* Reentrant call from a container add/remove below: remember that we
       * have to run again instead of nesting the layout pass. */
      self->relayout_dirty = TRUE;
      return;
    }

  self->relaying = TRUE;
  self->relayout_pass = 0;

  pinned = NULL;
  overflow_items = NULL;

  for (GSList *l = self->items; l != NULL; l = l->next)
    {
      NaItem    *item = l->data;
      NaItemState state = item_state (self, item);

      place_item (self, GTK_WIDGET (item), state);

      switch (state)
        {
        case NA_ITEM_STATE_PINNED:
          pinned = g_slist_prepend (pinned, item);
          break;
        case NA_ITEM_STATE_OVERFLOW:
          overflow_items = g_slist_prepend (overflow_items, item);
          break;
        default:
          break;
        }
    }

  /* Both lists were built by prepending. */
  pinned = g_slist_reverse (pinned);
  overflow_items = g_slist_reverse (overflow_items);

  show_chevron = (overflow_items != NULL);
  if (self->chevron != NULL && gtk_widget_get_visible (self->chevron) != show_chevron)
    {
      if (show_chevron)
        gtk_widget_show (self->chevron);
      else
        {
          gtk_widget_hide (self->chevron);
          na_overflow_popdown (self->overflow);
        }
    }

  /* Cell order of the drawer, both kinds of items together. */
  g_slist_free (self->overflow_order);
  self->overflow_order = g_slist_copy (overflow_items);

  /* A different cell size means the parked windows were sized for the old
   * one.  Bringing them back is what makes the new size reachable at all -
   * and it has to happen in the panel, where GTK may resize them. */
  if (self->xembed_parked != NULL && self->parked_extent != 0 &&
      self->parked_extent != compute_icon_extent (self))
    {
      unpark_all_xembed (self);
      schedule_park (self);
    }

  n_cells = g_slist_length (pinned) + (show_chevron ? 1 : 0);

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
  gtk_widget_get_allocation (GTK_WIDGET (self), &allocation);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      gtk_grid_set_row_homogeneous (GTK_GRID (self), TRUE);
      gtk_grid_set_column_homogeneous (GTK_GRID (self), FALSE);
      rows = MAX (1, allocation.height / self->min_icon_size);
      cols = MAX (1, n_cells / rows);
      if (n_cells % rows)
        cols++;
    }
  else
    {
      gtk_grid_set_row_homogeneous (GTK_GRID (self), FALSE);
      gtk_grid_set_column_homogeneous (GTK_GRID (self), TRUE);
      cols = MAX (1, allocation.width / self->min_icon_size);
      rows = MAX (1, n_cells / cols);
      if (n_cells % cols)
        rows++;
    }

  self->cols = cols;
  self->rows = rows;
  self->length = n_cells;

  data.grid        = GTK_GRID (self);
  data.orientation = orientation;
  data.rows        = rows;
  data.cols        = cols;
  data.index       = 0;

  /* Windows order: the arrow comes first, the visible icons follow it, so the
   * row does not shift sideways when icons arrive or leave the overflow. */
  if (show_chevron)
    layout_place (self->chevron, &data);

  g_slist_foreach (pinned, (GFunc) layout_place, &data);

  /* Parked XEmbed sockets stay children of THIS grid (unparenting them kills
   * the plug), so GtkGrid still hands them an allocation and GtkSocket resizes
   * the plug window to match it.  Which cell they get therefore decides the
   * size of the live icon window, and two measured traps rule it out:
   *
   *  - leaving them at their stale attach: child_visible=FALSE is honoured by
   *    size_allocate but NOT by size request, which filters on
   *    gtk_widget_get_visible() only, so each parked socket reserves a whole
   *    26px column.  Live tray with 4 icons parked: 216px instead of 190, and
   *    a hole in the row (sockets at x=84, 110, 164).
   *  - putting them in the chevron's column: that one is 32px wide, so the
   *    socket window becomes 32x26 and the drawer - which places cells at the
   *    26px extent pitch - overlaps them again.
   *
   * The cell is the CHEVRON's (0,0): it exists exactly when there are overflow
   * items (show_chevron is the same condition), so it reserves nothing extra.
   * The earlier choice - first cell of the second stripe - left a ghost column
   * when nothing was pinned: measured 58px for an empty tray (chevron 32 + a
   * phantom 26) instead of 32.  Overlapping siblings are legal in GtkGrid
   * (probe_grid_cv phase 2: chevron keeps its 32px, natural width unchanged),
   * and child_visible=FALSE keeps the sockets out of the panel's drawing.
   * The price is a 32x26 plug window instead of 26x26, which the drawer absorbs
   * by sizing each cell to MAX(window width, extent). */
  {
    const gint col = 0;
    const gint row = 0;

    for (GSList *l = overflow_items; l != NULL; l = l->next)
      {
        GtkWidget *w = GTK_WIDGET (l->data);

        if (SN_IS_ITEM (w))
          continue;   /* SNI items really live in the drawer grid */

        gtk_container_child_set (GTK_CONTAINER (self), w,
                                 "left-attach", col,
                                 "top-attach", row,
                                 NULL);
      }
  }

  layout_overflow_grid (self, overflow_items);

  if (show_chevron)
    {
      na_overflow_reposition (self->overflow);
      reposition_parked (self);
    }

  /* Requisition vs allocation disagreement means the panel has not caught up
   * with this layout pass.  extent is height-derived, so only the width is
   * meaningful here; a 1x1 allocation during startup is not a mismatch worth
   * fighting (the real size arrives with the first allocate). */
  if (!self->destroying && allocation.width > 1 && allocation.height > 1)
    {
      gint min_w = 0, nat_w = 0;
      gtk_widget_get_preferred_width (GTK_WIDGET (self), &min_w, &nat_w);
      if (nat_w > 0 && nat_w != allocation.width)
        {
          self->size_pending = nat_w;
          if (self->size_idle == 0)
            self->size_idle = g_idle_add (request_size_idle, self);
        }
    }

  g_slist_free (pinned);
  g_slist_free (overflow_items);

  self->relaying = FALSE;

  if (self->relayout_dirty)
    {
      /* One reentry is expected (container add/remove fires size-allocate).
       * More than a handful means the layout oscillates: stop instead of
       * recursing until the stack is gone. */
      if (++self->relayout_pass > 5)
        {
          g_warning ("notification-area: overflow relayout oscillating, "
                     "giving up after %u passes", self->relayout_pass);
          self->relayout_dirty = FALSE;
          return;
        }
      self->relayout_dirty = FALSE;
      refresh_grid (self);
    }
}

void
na_grid_set_min_icon_size (NaGrid *grid,
                           gint    min_icon_size)
{
  g_return_if_fail (NA_IS_GRID (grid));

  grid->min_icon_size = min_icon_size;

  refresh_grid (grid);
}

static void
chevron_toggled_cb (GtkToggleButton *button,
                    NaGrid          *self)
{
  if (!gtk_toggle_button_get_active (button))
    {
      na_overflow_popdown (self->overflow);
      return;
    }

  if (!na_overflow_popup (self->overflow,
                          GTK_WIDGET (button),
                          gtk_orientable_get_orientation (GTK_ORIENTABLE (self))))
    {
      /* The popup refused to open (no seat grab available).  Leave the arrow
       * unpressed rather than showing a button that lies about its state. */
      g_signal_handlers_block_by_func (button, chevron_toggled_cb, self);
      gtk_toggle_button_set_active (button, FALSE);
      g_signal_handlers_unblock_by_func (button, chevron_toggled_cb, self);
      return;
    }

  /* Opening the drawer does not go through refresh_grid.  Park synchronously
   * here - na_overflow_popup() has already realized and shown the drawer, so
   * its window exists - and then place.  Going through the idle instead would
   * show the drawer one frame without the icon and add it afterwards. */
  if (!park_xembed_now (self))
    schedule_park (self);   /* not everything fitted yet: finish from the queue */
  reposition_parked (self);
}

static void
overflow_closed_cb (NaOverflow *overflow,
                    NaGrid     *self)
{
  if (self->destroying)
    return;

  g_signal_handlers_block_by_func (self->chevron, chevron_toggled_cb, self);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->chevron), FALSE);
  g_signal_handlers_unblock_by_func (self->chevron, chevron_toggled_cb, self);

  /* Hiding the popup is the whole point of this handler.  The managed-window
   * revision used to hide it from focus-out_event(); switching to an
   * override-redirect window removed that path (a POPUP window never gets a
   * focus-out), so this "closed" handler is the only thing left that can
   * withdraw the seat grab.  Without the popdown below the grab stayed held
   * forever: pointer and keyboard belonged to a popup that could not be
   * dismissed, and killing the panel was the only escape. */
  na_overflow_popdown (self->overflow);
}

static void
item_added_cb (NaHost *host,
               NaItem *item,
               NaGrid *self)
{
  g_return_if_fail (NA_IS_HOST (host));
  g_return_if_fail (NA_IS_ITEM (item));
  g_return_if_fail (NA_IS_GRID (self));

  g_object_bind_property (self, "orientation",
                          item, "orientation",
                          G_BINDING_SYNC_CREATE);

  self->items = g_slist_prepend (self->items, item);
  self->items = g_slist_sort (self->items, compare_items);

  refresh_grid (self);
}

static void
item_removed_cb (NaHost *host,
                 NaItem *item,
                 NaGrid *self)
{
  GtkWidget *widget = GTK_WIDGET (item);
  GtkWidget *parent;

  g_return_if_fail (NA_IS_HOST (host));
  g_return_if_fail (NA_IS_ITEM (item));
  g_return_if_fail (NA_IS_GRID (self));

  /* During dispose the containers go away anyway and refresh_grid is
   * already gated; only keep our bookkeeping consistent for the host
   * teardown that runs in between. */
  if (self->destroying)
    {
      self->items = g_slist_remove (self->items, item);
      return;
    }

  parent = gtk_widget_get_parent (widget);
  if (parent != NULL)
    gtk_container_remove (GTK_CONTAINER (parent), widget);

  if (g_slist_find (self->parked, item))
    {
      self->parked = g_slist_remove (self->parked, item);
      g_object_unref (item);
    }

  self->items = g_slist_remove (self->items, item);

  refresh_grid (self);
}

static void
na_grid_init (NaGrid *self)
{
  self->icon_padding = 0;
  self->icon_size = 0;

  self->min_icon_size = MIN_ICON_SIZE_DEFAULT;
  self->cols = 1;
  self->rows = 1;
  self->length = 0;

  self->hosts = NULL;
  self->items = NULL;

  gtk_grid_set_row_homogeneous (GTK_GRID (self), TRUE);
  gtk_grid_set_column_homogeneous (GTK_GRID (self), TRUE);

  self->hidden_ids  = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->overflow_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* na_overflow_new() returns a GtkWindow, which is a GInitiallyUnowned: the
   * reference it comes with is FLOATING.  Nothing ever parents this drawer (it
   * is an override-redirect top-level), so the float is never sunk implicitly
   * and the g_clear_object() in na_grid_dispose() would unref a floating
   * object - ref_count 0 - finalising the drawer while its children are still
   * alive.  Measured: every single applet teardown (mate-panel --replace) died
   * in na_grid_dispose -> g_object_unref, preceded by two "g_signal_handlers_
   * destroy: instance with invalid (NULL) class pointer" criticals.  14 crashes
   * across this session, one per deploy.  sn-host-v0.c:126 sinks the items it
   * stores the same way. */
  self->overflow = g_object_ref_sink (na_overflow_new ());
  na_overflow_set_button_click (self->overflow, na_grid_overflow_button_cb,
                                self, NULL);
  g_signal_connect (self->overflow, "closed",
                    G_CALLBACK (overflow_closed_cb), self);
  /* Parked sockets are composited windows: the X server will not show them, so
   * the drawer has to paint them itself (see parked_draw_cb).  connect_after so
   * the drawer's own background and SNI icons are drawn first. */
  g_signal_connect_after (self->overflow, "draw",
                          G_CALLBACK (parked_draw_cb), self);

  self->chevron = gtk_toggle_button_new ();
  gtk_button_set_relief (GTK_BUTTON (self->chevron), GTK_RELIEF_NONE);
  gtk_button_set_image (GTK_BUTTON (self->chevron),
                        gtk_image_new_from_icon_name ("pan-down-symbolic", GTK_ICON_SIZE_MENU));
  gtk_image_set_pixel_size (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->chevron))), 16);
  gtk_widget_set_can_focus (self->chevron, TRUE);
  gtk_widget_set_tooltip_text (self->chevron, _("Show hidden tray icons"));
  atk_object_set_name (gtk_widget_get_accessible (self->chevron),
                       _("Show hidden tray icons"));
  gtk_widget_set_no_show_all (self->chevron, TRUE);
  gtk_widget_show (self->chevron);
  /* Placeholder position; refresh_grid() puts it in the last cell. */
  gtk_grid_attach (GTK_GRID (self), self->chevron, 0, 0, 1, 1);
  gtk_widget_hide (self->chevron);

  g_signal_connect (self->chevron, "toggled",
                    G_CALLBACK (chevron_toggled_cb), self);

  /* main.c drives orientation changes through gtk_orientable_set_orientation();
   * without this the chevron would keep pointing at the old screen edge. */
  g_signal_connect (self, "notify::orientation",
                    G_CALLBACK (na_grid_orientation_notify), self);

  self->chevron_icon = NULL;
}

static void
add_host (NaGrid *self,
          NaHost *host)
{
  self->hosts = g_slist_prepend (self->hosts, host);

  g_object_bind_property (self, "icon-padding", host, "icon-padding",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
  g_object_bind_property (self, "icon-size", host, "icon-size",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_signal_connect_object (host, "item-added",
                           G_CALLBACK (item_added_cb), self, 0);
  g_signal_connect_object (host, "item-removed",
                           G_CALLBACK (item_removed_cb), self, 0);
}

static void
na_grid_style_updated (GtkWidget *widget)
{
  NaGrid          *self = NA_GRID (widget);
  GtkStyleContext *context;
  GSList          *node;

  if (GTK_WIDGET_CLASS (na_grid_parent_class)->style_updated)
    GTK_WIDGET_CLASS (na_grid_parent_class)->style_updated (widget);

  context = gtk_widget_get_style_context (widget);

  for (node = self->hosts; node; node = node->next)
    {
      gtk_style_context_save (context);
      na_host_style_updated (node->data, context);
      gtk_style_context_restore (context);
    }

  /* The chevron follows the icon size the theme asked for. */
  update_chevron_icon (self);
}

/* Custom drawing because system-tray items need weird stuff. */
static gboolean
na_grid_draw (GtkWidget *grid,
              cairo_t   *cr)
{
  GList *child;
  GList *children = gtk_container_get_children (GTK_CONTAINER (grid));

  for (child = children; child; child = child->next)
    {
      /* Skip children that are not on the panel BEFORE draw_on_parent(): the
       * XEmbed alpha path cairo-paints the socket window into the parent
       * directly and never consults gtk_widget_is_drawable().  Two states end
       * up here: hidden-in-place (visible=FALSE) and parked into the drawer
       * (child_visible=FALSE, still visible=TRUE - so a visible-only check
       * misses it and paints a window that no longer lives here). */
      if (!gtk_widget_get_visible (child->data) ||
          !gtk_widget_get_child_visible (child->data))
        continue;

      if (! NA_IS_ITEM (child->data) ||
          ! na_item_draw_on_parent (child->data, grid, cr))
	{
	  if (gtk_widget_is_drawable (child->data) &&
	      gtk_cairo_should_draw_window (cr, gtk_widget_get_window (child->data)))
	    gtk_container_propagate_draw (GTK_CONTAINER (grid), child->data, cr);
	}
    }

  g_list_free (children);

  return TRUE;
}

static void
na_grid_realize (GtkWidget *widget)
{
  NaGrid *self = NA_GRID (widget);
  GdkScreen *screen;
  GdkDisplay *display;
  GSettings *settings;

  GTK_WIDGET_CLASS (na_grid_parent_class)->realize (widget);

  display = gdk_display_get_default ();
  /* Instantiate the hosts now we have a screen */
  screen = gtk_widget_get_screen (GTK_WIDGET (self));
#ifdef HAVE_X11
  if  (GDK_IS_X11_DISPLAY (display))
  {
    GtkOrientation orientation;
    NaHost *tray_host;

    orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
    tray_host = na_tray_new_for_screen (screen, orientation);
    g_object_bind_property (self, "orientation",
                          tray_host, "orientation",
                          G_BINDING_DEFAULT);

    add_host (self, tray_host);
  }
#endif
  settings = g_settings_new ("org.mate.panel");
  if (g_settings_get_boolean (settings, "enable-sni-support") &&
      ! find_sni_host (self))
    add_host (self, sn_host_v0_new ());
  g_object_unref (settings);

  update_chevron_icon (self);
}

/* The StatusNotifierItem host is pure D-Bus, unlike the XEmbed tray host which
 * is bound to a GdkScreen.  It therefore survives unrealize - see there. */
static NaHost *
find_sni_host (NaGrid *self)
{
  for (GSList *l = self->hosts; l != NULL; l = l->next)
    if (SN_IS_HOST_V0 (l->data))
      return l->data;

  return NULL;
}

static void
na_grid_unrealize (GtkWidget *widget)
{
  NaGrid *self = NA_GRID (widget);
  GSList *l;
  GSList *next;

  /* Parked socket windows are children of the drawer toplevel, which is about
   * to be withdrawn; bring them home first so nothing is left pointing at a
   * window we are about to unmap under a destroyed panel. */
  unpark_all_xembed (self);

  /* Withdraw the popup first.  It is a separate toplevel, so the unrealize
   * cascade below never touches it: it would stay mapped, positioned against
   * a chevron that is about to lose its GdkWindow, and - the dangerous part -
   * still holding the seat grab over a popup nothing can dismiss anymore.
   * The SNI host deliberately survives unrealize, the popup must not.
   * (Review finding 2026-10-20: this was the one live path to a frozen
   * input that did not go through dispose().) */
  if (self->overflow != NULL)
    na_overflow_popdown (self->overflow);

  /* The XEmbed tray is bound to a GdkScreen, so it is rebuilt on every
   * realize.  The StatusNotifierItem host is pure D-Bus and must NOT be.
   *
   * Dropping it here would release our org.kde.StatusNotifierHost-* name, and
   * the watcher then sets IsStatusNotifierHostRegistered to FALSE and emits
   * StatusNotifierHostRegistered (libstatus-notifier-watcher/gf-sn-watcher-v0.c,
   * name_vanished_cb, GF_WATCH_TYPE_HOST branch).  Clients are entitled to read
   * that flag as "no tray anymore".
   *
   * It would also tear down and rebuild every SNI item with its D-Bus proxy and
   * dbusmenu connection on each unrealize.  The items are not lost permanently
   * - the watcher keeps its own RegisteredStatusNotifierItems list, which only
   * the ITEM branch of name_vanished_cb prunes, so a fresh host re-reads them
   * in register_host_cb() and the icons come back.  But with the overflow
   * feature some of those widgets live in the popup grid rather than in this
   * grid, so the churn is visible, and there is no reason to pay for it.
   *
   * Keeping the host also keeps self->items meaningful: unrefing the tray host
   * makes it emit item-removed for its own icons, which prunes exactly those
   * from the bookkeeping.  Freeing the list wholesale would orphan the SNI
   * items that are still alive. */
  for (l = self->hosts; l != NULL; l = next)
    {
      NaHost *host = l->data;   /* read BEFORE removal: g_slist_free frees the
                                 * node, and its first word (the data pointer)
                                 * is immediately overwritten with the slice
                                 * allocator's free-list link.  Reading l->data
                                 * after g_slist_remove() hands g_object_unref()
                                 * that garbage - it segfaulted in
                                 * g_type_check_instance on a live session
                                 * (gdb backtrace 2026-10-19 23:42:29). */

      next = l->next;

      if (SN_IS_HOST_V0 (host))
        continue;

      self->hosts = g_slist_remove (self->hosts, host);
      g_object_unref (host);
    }

  GTK_WIDGET_CLASS (na_grid_parent_class)->unrealize (widget);
}

static void
na_grid_size_allocate (GtkWidget     *widget,
                       GtkAllocation *allocation)
{
  GTK_WIDGET_CLASS (na_grid_parent_class)->size_allocate (widget, allocation);
  update_chevron_icon (NA_GRID (widget));
  refresh_grid (NA_GRID (widget));
}

static void
na_grid_orientation_notify (GObject    *object,
                            GParamSpec *pspec,
                            NaGrid     *self)
{
  refresh_grid (self);
}

static void
na_grid_dispose (GObject *object)
{
  NaGrid *self = NA_GRID (object);

  self->destroying = TRUE;

  /* unrealize deliberately keeps the SNI host alive, so this is the only place
   * that can drop it.  Do it before the rest of the bookkeeping goes away:
   * unrefing a host emits item-removed for each of its icons, and that handler
   * still expects self->items to be intact. */
  if (self->hosts != NULL)
    {
      g_slist_free_full (self->hosts, g_object_unref);
      self->hosts = NULL;
    }

  if (self->park_idle != 0)
    {
      g_source_remove (self->park_idle);
      self->park_idle = 0;
    }

  if (self->size_idle != 0)
    {
      g_source_remove (self->size_idle);
      self->size_idle = 0;
    }

  if (self->fwd_item != NULL)
    {
      g_object_unref (self->fwd_item);
      self->fwd_item = NULL;
    }

  unpark_all_xembed (self);
  g_clear_pointer (&self->overflow_order, g_slist_free);
  g_clear_pointer (&self->xembed_pending, g_slist_free);
  g_clear_pointer (&self->xembed_parked, g_slist_free);

  if (self->overflow != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->overflow,
                                            G_CALLBACK (parked_draw_cb), self);
      na_overflow_popdown (self->overflow);
      g_signal_handlers_disconnect_by_func (self->overflow,
                                            G_CALLBACK (overflow_closed_cb), self);
      g_clear_object (&self->overflow);
    }

  /* Drop our bookkeeping before the containers go away, so that no callback
   * fired during teardown can see a half-built item list. */
  g_clear_pointer (&self->items, g_slist_free);

  if (self->parked != NULL)
    {
      g_slist_free_full (self->parked, g_object_unref);
      self->parked = NULL;
    }

  g_clear_pointer (&self->hidden_ids, g_hash_table_destroy);
  g_clear_pointer (&self->overflow_ids, g_hash_table_destroy);
  g_clear_pointer (&self->chevron_icon, g_free);

  G_OBJECT_CLASS (na_grid_parent_class)->dispose (object);
}

static void
na_grid_get_property (GObject    *object,
                      guint       property_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  NaGrid *self = NA_GRID (object);

  switch (property_id)
    {
      case PROP_ICON_PADDING:
        g_value_set_int (value, self->icon_padding);
        break;

      case PROP_ICON_SIZE:
        g_value_set_int (value, self->icon_size);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
na_grid_set_property (GObject      *object,
                      guint         property_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  NaGrid *self = NA_GRID (object);

  switch (property_id)
    {
      case PROP_ICON_PADDING:
        self->icon_padding = g_value_get_int (value);
        break;

      case PROP_ICON_SIZE:
        self->icon_size = g_value_get_int (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
na_grid_class_init (NaGridClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->get_property = na_grid_get_property;
  gobject_class->set_property = na_grid_set_property;
  gobject_class->dispose      = na_grid_dispose;

  widget_class->draw = na_grid_draw;
  widget_class->realize = na_grid_realize;
  widget_class->unrealize = na_grid_unrealize;
  widget_class->style_updated = na_grid_style_updated;
  widget_class->size_allocate = na_grid_size_allocate;

  g_object_class_install_property (gobject_class, PROP_ICON_PADDING,
    g_param_spec_int ("icon-padding",
                      "Padding around icons",
                      "Padding that should be put around icons, in pixels",
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ICON_SIZE,
    g_param_spec_int ("icon-size",
                      "Icon size",
                      "If non-zero, hardcodes the size of the icons in pixels",
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

GtkWidget *
na_grid_new (GtkOrientation  orientation)
{
  return g_object_new (NA_TYPE_GRID,
                       "orientation", orientation,
                       NULL);
}

void
na_grid_force_redraw (NaGrid *grid)
{
  GSList *node;

  for (node = grid->hosts; node; node = node->next)
    na_host_force_redraw (node->data);
}

static void
replace_id_list (GHashTable          *table,
                 const gchar *const *ids)
{
  g_hash_table_remove_all (table);

  if (ids == NULL)
    return;

  for (guint i = 0; ids[i] != NULL; i++)
    {
      if (ids[i][0] != '\0')
        g_hash_table_add (table, g_strdup (ids[i]));
    }
}

void
na_grid_set_hidden_icons (NaGrid             *grid,
                          const gchar *const *ids)
{
  g_return_if_fail (NA_IS_GRID (grid));

  replace_id_list (grid->hidden_ids, ids);
  refresh_grid (grid);
}

void
na_grid_set_overflow_icons (NaGrid             *grid,
                            const gchar *const *ids)
{
  g_return_if_fail (NA_IS_GRID (grid));

  replace_id_list (grid->overflow_ids, ids);
  refresh_grid (grid);
}

/* The auto-hide delay lives in the drawer itself: the grid only forwards the
 * value, because the properties dialog talks to the applet, not to the drawer. */
void
na_grid_set_overflow_autohide_ms (NaGrid *grid,
                                  guint   ms)
{
  g_return_if_fail (NA_IS_GRID (grid));

  if (grid->overflow != NULL)
    na_overflow_set_autohide_ms (grid->overflow, ms);
}

/* Dismiss the drawer from the outside, without waiting for auto-hide.
 *
 * The properties dialog needs this: changing an icon's state moves it between
 * the panel and the drawer, and doing that with the drawer visible serves no
 * purpose - the user is looking at the dialog anyway. With the default
 * auto-hide the drawer would close by itself, but at overflow-autohide-ms = 0
 * (auto-hide off) it would stay up and the move would happen with the drawer
 * visible - and that combination is where the panel size race was observed
 * (the grid asks for 214px, the panel hands out 188px for one more pass).
 * Closing it explicitly removes the scenario under any setting. */
void
na_grid_popdown_overflow (NaGrid *grid)
{
  g_return_if_fail (NA_IS_GRID (grid));

  if (grid->overflow != NULL)
    na_overflow_popdown (grid->overflow);
}

/* Is a root-coordinate point inside the arrow.
 *
 * Computed by translating into the toplevel and taking the toplevel's origin,
 * not gdk_window_get_origin(arrow): the button may have no GdkWindow of its
 * own, in which case the coordinates would be garbage. */
gboolean
na_grid_is_chevron_at (NaGrid *grid,
                       gint    root_x,
                       gint    root_y)
{
  GtkWidget *toplevel;
  gint tx = 0, ty = 0, ox = 0, oy = 0, w, h;

  g_return_val_if_fail (NA_IS_GRID (grid), FALSE);

  if (grid->chevron == NULL || !gtk_widget_get_realized (grid->chevron))
    return FALSE;

  toplevel = gtk_widget_get_toplevel (grid->chevron);
  if (toplevel == NULL || !gtk_widget_get_realized (toplevel))
    return FALSE;

  if (!gtk_widget_translate_coordinates (grid->chevron, toplevel, 0, 0, &tx, &ty))
    return FALSE;

  gdk_window_get_origin (gtk_widget_get_window (toplevel), &ox, &oy);
  ox += tx;
  oy += ty;

  w = gtk_widget_get_allocated_width (grid->chevron);
  h = gtk_widget_get_allocated_height (grid->chevron);
  if (w <= 0 || h <= 0)
    return FALSE;

  return root_x >= ox && root_x < ox + w && root_y >= oy && root_y < oy + h;
}

const GSList *
na_grid_get_items (NaGrid *grid)
{
  g_return_val_if_fail (NA_IS_GRID (grid), NULL);

  return grid->items;
}

NaItemState
na_grid_get_item_state (NaGrid *grid,
                        NaItem *item)
{
  g_return_val_if_fail (NA_IS_GRID (grid), NA_ITEM_STATE_PINNED);
  g_return_val_if_fail (NA_IS_ITEM (item), NA_ITEM_STATE_PINNED);

  return item_state (grid, item);
}

/* Which half of the tray an item belongs to, for labelling in the preferences
 * dialog.  Both halves can now be moved into the popup; they differ only in how
 * (StatusNotifierItem parks as a widget, XEmbed as a window - see place_item()). */
gboolean
na_grid_item_is_sni (NaGrid *grid,
                     NaItem *item)
{
  g_return_val_if_fail (NA_IS_GRID (grid), FALSE);
  g_return_val_if_fail (NA_IS_ITEM (item), FALSE);

  return SN_IS_ITEM (item);
}
