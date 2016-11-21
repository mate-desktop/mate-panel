/*
 * panel-multiscreen.c: Multi-screen and Xinerama support for the panel.
 *
 * Copyright (C) 2001 George Lebl <jirka@5z.com>
 *               2002 Sun Microsystems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 * USA
 *
 * Authors: George Lebl <jirka@5z.com>,
 *          Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <gdk/gdkx.h>

#include "panel-multiscreen.h"

#include <string.h>

static int            screens     = 0;
static int           *monitors    = NULL;
static GdkRectangle **geometries  = NULL;
static gboolean       initialized = FALSE;
static gboolean       have_randr  = FALSE;
static guint          reinit_id   = 0;

#ifdef HAVE_RANDR
static gboolean
_panel_multiscreen_output_should_be_first (Display       *xdisplay,
					   RROutput       output,
					   XRROutputInfo *info,
					   RROutput       primary)
{
	if (primary)
		return output == primary;

	Atom           connector_type_atom;
	Atom           actual_type;
	int            actual_format;
	unsigned long  nitems;
	unsigned long  bytes_after;
	unsigned char *prop;
	char          *connector_type;
	gboolean       retval;

	connector_type_atom = XInternAtom (xdisplay, "ConnectorType", False);

	if (XRRGetOutputProperty (xdisplay, output, connector_type_atom,
				  0, 100, False, False, None,
				  &actual_type, &actual_format,
				  &nitems, &bytes_after, &prop) == Success) {
		if (actual_type == XA_ATOM && nitems == 1 && actual_format == 32) {
			connector_type = XGetAtomName (xdisplay, prop[0]);
			retval = g_strcmp0 (connector_type, "Panel") == 0;
			XFree (connector_type);
			return retval;
		}
	}

	/* Fallback (see https://bugs.freedesktop.org/show_bug.cgi?id=26736)
	 * "LVDS" is the oh-so-intuitive name that X gives to laptop LCDs.
	 * It can actually be LVDS0, LVDS-0, Lvds, etc.
	 */
	return (g_ascii_strncasecmp (info->name, "LVDS", strlen ("LVDS")) == 0);
}
#endif

static gboolean
panel_multiscreen_get_randr_monitors_for_screen (GdkScreen     *screen,
						 int           *monitors_ret,
						 GdkRectangle **geometries_ret)
{
#ifdef HAVE_RANDR
	Display            *xdisplay;
	Window              xroot;
	XRRScreenResources *resources;
	RROutput            primary;
	GArray             *geometries;
	int                 i;

	if (!have_randr)
		return FALSE;

	/* GTK+ 2.14.x uses the Xinerama API, instead of RANDR, to get the
	 * monitor geometries. It does this to avoid calling
	 * XRRGetScreenResources(), which is slow as it re-detects all the
	 * monitors --- note that XRRGetScreenResourcesCurrent() had not been
	 * introduced yet.  Using Xinerama in GTK+ has the bad side effect that
	 * gdk_screen_get_monitor_plug_name() will return NULL, as Xinerama
	 * does not provide that information, unlike RANDR.
	 *
	 * Here we need to identify the output names, so that we can put the
	 * built-in LCD in a laptop *before* all other outputs.  This is so
	 * that mate-panel will normally prefer to appear on the "native"
	 * display rather than on an external monitor.
	 *
	 * To get the output names and geometries, we will not use
	 * gdk_screen_get_n_monitors() and friends, but rather we will call
	 * XRR*() directly.
	 *
	 * See https://bugzilla.novell.com/show_bug.cgi?id=479684 for this
	 * particular bug, and and
	 * http://bugzilla.gnome.org/show_bug.cgi?id=562944 for a more
	 * long-term solution.
	 */

	xdisplay = GDK_SCREEN_XDISPLAY (screen);
	xroot = GDK_WINDOW_XID (gdk_screen_get_root_window (screen));

	resources = XRRGetScreenResourcesCurrent (xdisplay, xroot);
	if (resources->noutput == 0) {
		/* This might happen if nothing tried to get randr
		 * resources from the server before, so we need an
		 * active probe. See comment #27 in
		 * https://bugzilla.gnome.org/show_bug.cgi?id=597101 */
		XRRFreeScreenResources (resources);
		resources = XRRGetScreenResources (xdisplay, xroot);
	}

	if (!resources)
		return FALSE;

	primary = XRRGetOutputPrimary (xdisplay, xroot);

	geometries = g_array_sized_new (FALSE, FALSE,
					sizeof (GdkRectangle),
					resources->noutput);

	for (i = 0; i < resources->noutput; i++) {
		XRROutputInfo *output;

		output = XRRGetOutputInfo (xdisplay, resources,
					   resources->outputs[i]);

		if (output->connection != RR_Disconnected &&
		    output->crtc != 0) {
			XRRCrtcInfo  *crtc;
			GdkRectangle  rect;

			crtc = XRRGetCrtcInfo (xdisplay, resources,
					       output->crtc);

			rect.x	    = crtc->x;
			rect.y	    = crtc->y;
			rect.width  = crtc->width;
			rect.height = crtc->height;

			XRRFreeCrtcInfo (crtc);

			if (_panel_multiscreen_output_should_be_first (xdisplay,
								       resources->outputs[i],
								       output, primary))
				g_array_prepend_vals (geometries, &rect, 1);
			else
				g_array_append_vals (geometries, &rect, 1);
		}

		XRRFreeOutputInfo (output);
	}

	XRRFreeScreenResources (resources);

	if (geometries->len == 0) {
		/* This can happen in at least one case:
		 * https://bugzilla.novell.com/show_bug.cgi?id=543876 where all
		 * monitors appear disconnected (possibly because the  screen
		 * is behing a KVM switch) -- see comment #8.
		 * There might be other cases too, so we stay on the safe side.
		 */
		g_array_free (geometries, TRUE);
		return FALSE;
	}

	*monitors_ret = geometries->len;
	*geometries_ret = (GdkRectangle *) g_array_free (geometries, FALSE);

	return TRUE;
#else
	return FALSE;
#endif
}

static void
panel_multiscreen_get_gdk_monitors_for_screen (GdkScreen     *screen,
					       int           *monitors_ret,
					       GdkRectangle **geometries_ret)
{
	int           num_monitors;
	GdkRectangle *geometries;
	int           i;

	num_monitors = gdk_screen_get_n_monitors (screen);
	geometries = g_new (GdkRectangle, num_monitors);

	for (i = 0; i < num_monitors; i++)
		gdk_screen_get_monitor_geometry (screen, i, &(geometries[i]));

	*monitors_ret = num_monitors;
	*geometries_ret = geometries;
}

static void
panel_multiscreen_get_raw_monitors_for_screen (GdkScreen     *screen,
					       int           *monitors_ret,
					       GdkRectangle **geometries_ret)
{
	gboolean res;

	*monitors_ret = 0;
	*geometries_ret = NULL;

	res = panel_multiscreen_get_randr_monitors_for_screen (screen,
							       monitors_ret,
							       geometries_ret);
	if (res && *monitors_ret > 0)
		return;

	panel_multiscreen_get_gdk_monitors_for_screen (screen,
						       monitors_ret,
						       geometries_ret);
}

static inline gboolean
rectangle_overlaps (GdkRectangle *a,
		    GdkRectangle *b)
{
	return gdk_rectangle_intersect (a, b, NULL);
}

static long
pixels_in_rectangle (GdkRectangle *r)
{
	return (long) (r->width * r->height);
}

static void
panel_multiscreen_compress_overlapping_monitors (int           *num_monitors_inout,
						 GdkRectangle **geometries_inout)
{
	int           num_monitors;
	GdkRectangle *geometries;
	int           i;

	num_monitors = *num_monitors_inout;
	geometries = *geometries_inout;

	/* http://bugzilla.gnome.org/show_bug.cgi?id=530969
	 * https://bugzilla.novell.com/show_bug.cgi?id=310208
	 * and many other such bugs...
	 *
	 * RANDR sometimes gives us monitors that overlap (i.e. outputs whose
	 * bounding rectangles overlap). This is sometimes right and sometimes
	 * wrong:
	 *
	 *   * Right - two 1024x768 outputs at the same offset (0, 0) that show
	 *     the same thing.  Think "laptop plus projector with the same
	 *     resolution".
	 *
	 *   * Wrong - one 1280x1024 output ("laptop internal LCD") and another
	 *     1024x768 output ("external monitor"), both at offset (0, 0).
	 *     There is no way for the monitor with the small resolution to
	 *     show the complete image from the laptop's LCD, unless one uses
	 *     panning (but nobody wants panning, right!?).
	 *
	 * With overlapping monitors, we may end up placing the panel with
	 * respect to the "wrong" one.  This is always wrong, as the panel
	 * appears "in the middle of the screen" of the monitor with the
	 * smaller resolution, instead of at the edge.
	 *
	 * Our strategy is to find the subsets of overlapping monitors, and
	 * "compress" each such set to being like if there were a single
	 * monitor with the biggest resolution of each of that set's monitors.
	 * Say we have four monitors
	 *
	 *      A, B, C, D
	 *
	 * where B and D overlap.  In that case, we'll generate a new list that
	 * looks like
	 *
	 *      A, MAX(B, D), C
	 *
	 * with three monitors.
	 *
	 * NOTE FOR THE FUTURE: We could avoid most of this mess if we had a
	 * concept of a "primary monitor". Also, we could look at each
	 * output's name or properties to see if it is the built-in LCD in a
	 * laptop. However, with GTK+ 2.14.x we don't get output names, since
	 * it gets the list outputs from Xinerama, not RANDR (and Xinerama
	 * doesn't provide output names).
	 */

	for (i = 0; i < num_monitors; i++) {
		long max_pixels;
		int  j;

		max_pixels = pixels_in_rectangle (&geometries[i]);

		j = i + 1;

		while (j < num_monitors) {
			if (rectangle_overlaps (&geometries[i],
						&geometries[j])) {
				long pixels;

				pixels = pixels_in_rectangle (&geometries[j]);
				if (pixels > max_pixels) {
					max_pixels = pixels;
					/* keep the maximum */
					geometries[i] = geometries[j];
				}

				/* Shift the remaining monitors to the left */
				if (num_monitors - j - 1 > 0)
					memmove (&geometries[j],
						 &geometries[j + 1],
						 sizeof (geometries[0]) * (num_monitors - j - 1));

				num_monitors--;
				g_assert (num_monitors > 0);
			} else
				j++;
		}
	}

	*num_monitors_inout = num_monitors;
	*geometries_inout = geometries;
}

static void
panel_multiscreen_get_monitors_for_screen (GdkScreen     *screen,
					   int           *monitors_ret,
					   GdkRectangle **geometries_ret)
{
	panel_multiscreen_get_raw_monitors_for_screen (screen,
						       monitors_ret,
						       geometries_ret);
	panel_multiscreen_compress_overlapping_monitors (monitors_ret,
							 geometries_ret);
}

static gboolean
panel_multiscreen_reinit_idle (gpointer data)
{
	panel_multiscreen_reinit ();
	reinit_id = 0;

	return FALSE;
}

static void
panel_multiscreen_queue_reinit (void)
{
	if (reinit_id)
		return;

	reinit_id = g_idle_add (panel_multiscreen_reinit_idle, NULL);
}

static void
panel_multiscreen_init_randr (GdkDisplay *display)
{
#ifdef HAVE_RANDR
	Display *xdisplay;
	int      event_base, error_base;
#endif

	have_randr = FALSE;

#ifdef HAVE_RANDR
	xdisplay = GDK_DISPLAY_XDISPLAY (display);

	/* We don't remember the event/error bases, as we expect to get "screen
	 * changed" events from GdkScreen instead.
	 */

	if (XRRQueryExtension (xdisplay, &event_base, &error_base)) {
		int major, minor;

		XRRQueryVersion (xdisplay, &major, &minor);
		if ((major == 1 && minor >= 3) || major > 1)
			have_randr = TRUE;
	}
#endif
}

void
panel_multiscreen_init (void)
{
	GdkDisplay *display;
	int         i;

	if (initialized)
		return;

	display = gdk_display_get_default ();
	screens = 1;

	panel_multiscreen_init_randr (display);

	monitors   = g_new0 (int, screens);
	geometries = g_new0 (GdkRectangle *, screens);

	for (i = 0; i < screens; i++) {
		GdkScreen *screen;

		screen = gdk_display_get_screen (display, i);

		/* We connect to both signals to be on the safe side, but in
		 * theory, it should be enough to only connect to
		 * monitors-changed. Since we'll likely get two signals, we do
		 * the real callback in the idle loop. */
		g_signal_connect (screen, "size-changed",
				  G_CALLBACK (panel_multiscreen_queue_reinit), NULL);
		g_signal_connect (screen, "monitors-changed",
				  G_CALLBACK (panel_multiscreen_queue_reinit), NULL);

		panel_multiscreen_get_monitors_for_screen (screen,
							   &(monitors[i]),
							   &(geometries[i]));
	}

	initialized = TRUE;
}

void
panel_multiscreen_reinit (void)
{
	GdkScreen *screen;
	GList     *toplevels, *l;

	if (monitors)
		g_free (monitors);

	if (geometries) {
		int j;

		for (j = 0; j < screens; j++)
			g_free (geometries[j]);
		g_free (geometries);
	}

	screen = gdk_screen_get_default ();
	g_signal_handlers_disconnect_by_func (screen, panel_multiscreen_queue_reinit, NULL);

	initialized = FALSE;
	panel_multiscreen_init ();

	toplevels = gtk_window_list_toplevels ();

	for (l = toplevels; l; l = l->next)
		gtk_widget_queue_resize (l->data);

	g_list_free (toplevels);
}

int
panel_multiscreen_screens (void)
{
	return screens;
}

int
panel_multiscreen_monitors (GdkScreen *screen)
{
	int n_screen;

	n_screen = gdk_screen_get_number (screen);

	g_return_val_if_fail (n_screen >= 0 && n_screen < screens, 1);

	return monitors [n_screen];
}

int
panel_multiscreen_x (GdkScreen *screen,
		     int        monitor)
{
	int n_screen;

	n_screen = gdk_screen_get_number (screen);

	g_return_val_if_fail (n_screen >= 0 && n_screen < screens, 0);
	g_return_val_if_fail (monitor >= 0 && monitor < monitors [n_screen], 0);

	return geometries [n_screen][monitor].x;
}

int
panel_multiscreen_y (GdkScreen *screen,
		     int        monitor)
{
	int n_screen;

	n_screen = gdk_screen_get_number (screen);

	g_return_val_if_fail (n_screen >= 0 && n_screen < screens, 0);
	g_return_val_if_fail (monitor >= 0 && monitor < monitors [n_screen], 0);

	return geometries [n_screen][monitor].y;
}

int
panel_multiscreen_width (GdkScreen *screen,
			 int        monitor)
{
	int n_screen;

	n_screen = gdk_screen_get_number (screen);

	g_return_val_if_fail (n_screen >= 0 && n_screen < screens, 0);
	g_return_val_if_fail (monitor >= 0 && monitor < monitors [n_screen], 0);

	return geometries [n_screen][monitor].width;
}

int
panel_multiscreen_height (GdkScreen *screen,
			  int        monitor)
{
	int n_screen;

	n_screen = gdk_screen_get_number (screen);

	g_return_val_if_fail (n_screen >= 0 && n_screen < screens, 0);
	g_return_val_if_fail (monitor >= 0 && monitor < monitors [n_screen], 0);

	return geometries [n_screen][monitor].height;
}

int
panel_multiscreen_locate_widget_monitor (GtkWidget *widget)
{
	GtkWidget *toplevel;
	int        retval = -1;

	toplevel = gtk_widget_get_toplevel (widget);
	if (!toplevel)
		return -1;

	g_object_get (toplevel, "monitor", &retval, NULL);

	return retval;
}

static int
axis_distance (int p, int axis_start, int axis_size)
{
	if (p >= axis_start && p < axis_start + axis_size)
		return 0;
	else if (p < axis_start)
		return (axis_start - p);
	else
		return (p - (axis_start + axis_size - 1));
}

/* The panel can't use gdk_screen_get_monitor_at_point() since it has its own
 * view of which monitors are present. Look at get_monitors_for_screen() above
 * to see why. */
int
panel_multiscreen_get_monitor_at_point (GdkScreen *screen,
					int        x,
					int        y)
{
	int n_screen;
	int i;
	int n_monitors;
	GdkRectangle *geoms;
	int min_dist_squared;
	int closest_monitor;

	/* not -1 as callers expect a real monitor */
	g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

	n_screen = gdk_screen_get_number (screen);

	n_monitors = monitors[n_screen];
	geoms = geometries[n_screen];

	min_dist_squared = G_MAXINT32;
	closest_monitor = 0;

	for (i = 0; i < n_monitors; i++) {
		int dist_x, dist_y;
		int dist_squared;

		dist_x = axis_distance (x, geoms[i].x, geoms[i].width);
		dist_y = axis_distance (y, geoms[i].y, geoms[i].height);

		if (dist_x == 0 && dist_y == 0)
			return i;

		dist_squared = dist_x * dist_x + dist_y * dist_y;

		if (dist_squared < min_dist_squared) {
			min_dist_squared = dist_squared;
			closest_monitor = i;
		}
	}

	return closest_monitor;
}

typedef struct {
	int x0;
	int y0;
	int x1;
	int y1;
} MonitorBounds;

static inline void
get_monitor_bounds (int            n_screen,
		    int            n_monitor,
		    MonitorBounds *bounds)
{
	g_assert (n_screen >= 0 && n_screen < screens);
	g_assert (n_monitor >= 0 || n_monitor < monitors [n_screen]);
	g_assert (bounds != NULL);

	bounds->x0 = geometries [n_screen][n_monitor].x;
	bounds->y0 = geometries [n_screen][n_monitor].y;
	bounds->x1 = bounds->x0 + geometries [n_screen][n_monitor].width;
	bounds->y1 = bounds->y0 + geometries [n_screen][n_monitor].height;
}

/* determines whether a given monitor is along the visible
 * edge of the logical screen.
 */
void
panel_multiscreen_is_at_visible_extreme (GdkScreen *screen,
					 int        n_monitor,
					 gboolean  *leftmost,
					 gboolean  *rightmost,
					 gboolean  *topmost,
					 gboolean  *bottommost)
{
	MonitorBounds monitor;
	int           n_screen, i;

	n_screen = gdk_screen_get_number (screen);

	*leftmost   = TRUE;
	*rightmost  = TRUE;
	*topmost    = TRUE;
	*bottommost = TRUE;

	g_return_if_fail (n_screen >= 0 && n_screen < screens);
	g_return_if_fail (n_monitor >= 0 && n_monitor < monitors [n_screen]);

	get_monitor_bounds (n_screen, n_monitor, &monitor);

	/* go through each monitor and try to find one either right,
	 * below, above, or left of the specified monitor
	 */

	for (i = 0; i < monitors [n_screen]; i++) {
		MonitorBounds iter;

		if (i == n_monitor) continue;

		get_monitor_bounds (n_screen, i, &iter);

		if ((iter.y0 >= monitor.y0 && iter.y0 <  monitor.y1) ||
		    (iter.y1 >  monitor.y0 && iter.y1 <= monitor.y1)) {
			if (iter.x0 < monitor.x0)
				*leftmost = FALSE;
			if (iter.x1 > monitor.x1)
				*rightmost = FALSE;
		}

		if ((iter.x0 >= monitor.x0 && iter.x0 <  monitor.x1) ||
		    (iter.x1 >  monitor.x0 && iter.x1 <= monitor.x1)) {
			if (iter.y0 < monitor.y0)
				*topmost = FALSE;
			if (iter.y1 > monitor.y1)
				*bottommost = FALSE;
		}
	}
}

