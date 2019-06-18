/*
 * panel-background-monitor.c:
 *
 * Copyright (C) 2001, 2002 Ian McKellar <yakk@yakk.net>
 *                     2002 Sun Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *      Ian McKellar <yakk@yakk.net>
 *      Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#ifndef HAVE_X11
#error file should only be compiled when HAVE_X11 is enabled
#endif

#include <glib.h>
#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-bg.h>

#include "panel-background-monitor.h"
#include "panel-util.h"

enum {
	CHANGED,
	LAST_SIGNAL
};

static void panel_background_monitor_changed (PanelBackgroundMonitor *monitor);

static GdkFilterReturn panel_background_monitor_xevent_filter (GdkXEvent *xevent,
							       GdkEvent  *event,
							       gpointer   data);

struct _PanelBackgroundMonitorClass {
	GObjectClass   parent_class;
	void         (*changed) (PanelBackgroundMonitor *monitor);
};

struct _PanelBackgroundMonitor {
	GObject    parent_instance;

	GdkScreen *screen;

	Window     xwindow;
	GdkWindow *gdkwindow;

	Atom       xatom;
	GdkAtom    gdkatom;

	cairo_surface_t *surface;
	GdkPixbuf *gdkpixbuf;

	int        width;
	int        height;

	gboolean   display_grabbed;
};

G_DEFINE_TYPE (PanelBackgroundMonitor, panel_background_monitor, G_TYPE_OBJECT)

static PanelBackgroundMonitor *global_background_monitor = NULL;

static guint signals [LAST_SIGNAL] = { 0 };

gboolean gdk_window_check_composited_wm(GdkWindow* window)
{
	g_return_val_if_fail (GDK_IS_X11_WINDOW (window), TRUE);
	return gdk_screen_is_composited(gdk_window_get_screen(window));
}

static void
panel_background_monitor_finalize (GObject *object)
{
	PanelBackgroundMonitor *monitor;

	monitor = PANEL_BACKGROUND_MONITOR (object);

	gdk_window_remove_filter (
		monitor->gdkwindow, panel_background_monitor_xevent_filter, monitor);
	g_signal_handlers_disconnect_by_func (monitor->screen,
		panel_background_monitor_changed, monitor);

	if (monitor->surface)
		cairo_surface_destroy (monitor->surface);
	monitor->surface= NULL;

	if (monitor->gdkpixbuf)
		g_object_unref (monitor->gdkpixbuf);
	monitor->gdkpixbuf = NULL;

	G_OBJECT_CLASS (panel_background_monitor_parent_class)->finalize (object);
}

static void
panel_background_monitor_class_init (PanelBackgroundMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals [CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelBackgroundMonitorClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	object_class->finalize = panel_background_monitor_finalize;
}

static void
panel_background_monitor_init (PanelBackgroundMonitor *monitor)
{
	monitor->screen = NULL;

	monitor->gdkwindow = NULL;
	monitor->xwindow   = None;

	monitor->gdkatom = gdk_atom_intern_static_string ("_XROOTPMAP_ID");
	monitor->xatom   = gdk_x11_atom_to_xatom (monitor->gdkatom);

	monitor->surface = NULL;
	monitor->gdkpixbuf = NULL;

	monitor->display_grabbed = FALSE;
}

static void
panel_background_monitor_connect_to_screen (PanelBackgroundMonitor *monitor,
					    GdkScreen              *screen)
{
	if (monitor->screen != NULL && monitor->gdkwindow != NULL) {
		gdk_window_remove_filter (monitor->gdkwindow,
					  panel_background_monitor_xevent_filter,
					  monitor);
	}

	monitor->screen = screen;
	g_signal_connect_swapped (screen, "size-changed",
	    G_CALLBACK (panel_background_monitor_changed), monitor);

	monitor->gdkwindow = gdk_screen_get_root_window (screen);
	monitor->xwindow   = GDK_WINDOW_XID (monitor->gdkwindow);

	gdk_window_add_filter (
		monitor->gdkwindow, panel_background_monitor_xevent_filter, monitor);

	gdk_window_set_events (
		monitor->gdkwindow,
		gdk_window_get_events (monitor->gdkwindow) | GDK_PROPERTY_CHANGE_MASK);
}

static PanelBackgroundMonitor *
panel_background_monitor_new (GdkScreen *screen)
{
	PanelBackgroundMonitor *monitor;

	monitor = g_object_new (PANEL_TYPE_BACKGROUND_MONITOR, NULL);

	panel_background_monitor_connect_to_screen (monitor, screen);

	return monitor;
}

PanelBackgroundMonitor *
panel_background_monitor_get_for_screen (GdkScreen *screen)
{
	g_return_val_if_fail (GDK_IS_X11_SCREEN (screen), NULL);

	if (!global_background_monitor) {
		global_background_monitor = panel_background_monitor_new (screen);

		g_object_add_weak_pointer (G_OBJECT (global_background_monitor),
		                           (void **) &global_background_monitor);

		return global_background_monitor;
	}

	return g_object_ref (global_background_monitor);
}

static void
panel_background_monitor_changed (PanelBackgroundMonitor *monitor)
{
	if (monitor->surface)
		cairo_surface_destroy (monitor->surface);
	monitor->surface = NULL;

	if (monitor->gdkpixbuf)
		g_object_unref (monitor->gdkpixbuf);
	monitor->gdkpixbuf = NULL;

	g_signal_emit (monitor, signals [CHANGED], 0);
}

static GdkFilterReturn
panel_background_monitor_xevent_filter (GdkXEvent *xevent,
					GdkEvent  *event,
					gpointer   data)
{
	PanelBackgroundMonitor *monitor;
	XEvent                 *xev;

	g_return_val_if_fail (PANEL_IS_BACKGROUND_MONITOR (data), GDK_FILTER_CONTINUE);

	monitor = PANEL_BACKGROUND_MONITOR (data);
	xev     = (XEvent *) xevent;

	if (xev->type == PropertyNotify &&
	    xev->xproperty.atom == monitor->xatom &&
	    xev->xproperty.window == monitor->xwindow)
		panel_background_monitor_changed (monitor);

	return GDK_FILTER_CONTINUE;
}

static GdkPixbuf *
panel_background_monitor_tile_background (PanelBackgroundMonitor *monitor,
					  int                     width,
					  int                     height)
{
	GdkPixbuf *retval;
	int        tilewidth, tileheight;

	retval = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);

	tilewidth  = gdk_pixbuf_get_width (monitor->gdkpixbuf);
	tileheight = gdk_pixbuf_get_height (monitor->gdkpixbuf);

	if (tilewidth == 1 && tileheight == 1) {
		guchar  *pixels;
		int      n_channels;
		guint32  pixel = 0;

		n_channels = gdk_pixbuf_get_n_channels (monitor->gdkpixbuf);
		pixels     = gdk_pixbuf_get_pixels (monitor->gdkpixbuf);

		if (pixels) {
			if (n_channels == 4)
				pixel = ((guint32 *) pixels) [0];
			else if (n_channels == 3)
				pixel = pixels [0] << 24 | pixels [1] << 16 | pixels [2] << 8;
		}

		gdk_pixbuf_fill (retval, pixel);
	} else {
		unsigned char   *data;
		cairo_t         *cr;
		cairo_surface_t *surface;
		cairo_pattern_t *pattern;

		data = g_malloc (width * height * 4);
		if (!data)
			return NULL;

		surface = cairo_image_surface_create_for_data (data,
							       CAIRO_FORMAT_RGB24,
							       width, height,
							       width * 4);
		cr = cairo_create (surface);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_paint (cr);

		gdk_cairo_set_source_pixbuf (cr, monitor->gdkpixbuf, 0, 0);
		pattern = cairo_get_source (cr);
		cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
		cairo_rectangle (cr, 0, 0, width, height);
		cairo_fill (cr);

		cairo_destroy (cr);
		cairo_surface_destroy (surface);

		retval = panel_util_cairo_rgbdata_to_pixbuf (data,
							     width, height);

		g_free (data);
	}

	return retval;
}

static void
panel_background_monitor_setup_pixbuf (PanelBackgroundMonitor *monitor)
{
	GdkDisplay  *display;
	int          rwidth, rheight;
	int          pwidth, pheight;

	display = gdk_screen_get_display (monitor->screen);

	gdk_x11_display_grab (display);
	monitor->display_grabbed = TRUE;

	if (!monitor->surface)
		monitor->surface = mate_bg_get_surface_from_root (monitor->screen);

	if (!monitor->surface)
	{
		g_warning ("couldn't get background pixmap\n");
		gdk_x11_display_ungrab (display);
		monitor->display_grabbed = FALSE;
		return;
	}

	pwidth = cairo_xlib_surface_get_width (monitor->surface);
	pheight = cairo_xlib_surface_get_height (monitor->surface);

	gdk_window_get_geometry (monitor->gdkwindow,
				 NULL, NULL, &rwidth, &rheight);

	monitor->width  = MIN (pwidth,  rwidth);
	monitor->height = MIN (pheight, rheight);

	g_assert (monitor->gdkpixbuf == NULL);
	monitor->gdkpixbuf = gdk_pixbuf_get_from_surface (monitor->surface,
													  0, 0,
													  monitor->width, monitor->height);

	gdk_x11_display_ungrab (display);
	monitor->display_grabbed = FALSE;

	if (monitor->gdkpixbuf == NULL)
		return;

	if ((monitor->width < rwidth || monitor->height < rheight)) {
		GdkPixbuf *tiled;

		tiled = panel_background_monitor_tile_background (
						monitor, rwidth, rheight);
		g_object_unref (monitor->gdkpixbuf);
		monitor->gdkpixbuf = tiled;

		monitor->width  = rwidth;
		monitor->height = rheight;
	}
}

GdkPixbuf *
panel_background_monitor_get_region (PanelBackgroundMonitor *monitor,
				     int                     x,
				     int                     y,
				     int                     width,
				     int                     height)
{
	GdkPixbuf *pixbuf, *tmpbuf;
	int        subwidth, subheight;
	int        subx, suby;

	g_return_val_if_fail (monitor, NULL);
	g_return_val_if_fail (GDK_IS_X11_WINDOW (monitor->gdkwindow), NULL);

	if (!monitor->gdkpixbuf)
		panel_background_monitor_setup_pixbuf (monitor);

	if (!monitor->gdkpixbuf)
		return NULL;

	subwidth  = MIN (width,  monitor->width - x);
	subheight = MIN (height, monitor->height - y);
	/* if x or y are negative numbers */
	subwidth  = MIN (subwidth, width + x);
	subheight  = MIN (subheight, height + y);

	subx = MAX (x, 0);
	suby = MAX (y, 0);

	if ((subwidth <= 0) || (subheight <= 0) ||
	    (monitor->width-x < 0) || (monitor->height-y < 0) )
		/* region is completely offscreen */
		return gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8,
				       width, height);

	pixbuf = gdk_pixbuf_new_subpixbuf (
			monitor->gdkpixbuf, subx, suby, subwidth, subheight);

	if ((subwidth < width) || (subheight < height)) {
		tmpbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8,
					 width, height);
		gdk_pixbuf_copy_area (pixbuf, 0, 0, subwidth, subheight,
				      tmpbuf, (x < 0) ? -x : 0, (y < 0) ? -y : 0);
		g_object_unref (pixbuf);
		pixbuf = tmpbuf;
	}

	return pixbuf;
}
