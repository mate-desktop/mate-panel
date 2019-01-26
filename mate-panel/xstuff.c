/*
 * MATE panel x stuff
 *
 * Copyright (C) 2000, 2001 Eazel, Inc.
 *               2002 Sun Microsystems Inc.
 *
 * Authors: George Lebl <jirka@5z.com>
 *          Mark McLoughlin <mark@skynet.ie>
 *
 *  Contains code from the Window Maker window manager
 *
 *  Copyright (c) 1997-2002 Alfredo K. Kojima

 */

#include <config.h>

#ifndef HAVE_X11
#error file should only be built when HAVE_X11 is enabled
#endif

#include <string.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>

#include "panel-enums.h"
#include "xstuff.h"

static int (* xstuff_old_xio_error_handler) (Display *) = NULL;
static int (* xstuff_old_x_error_handler)   (Display *, XErrorEvent *);
static gboolean xstuff_display_is_dead = FALSE;

/* Zoom animation */
#define MINIATURIZE_ANIMATION_FRAMES_Z   1
#define MINIATURIZE_ANIMATION_STEPS_Z    6
/* the delay per draw */
#define MINIATURIZE_ANIMATION_DELAY_Z    10

/* zoom factor, steps and delay if composited (factor must be odd) */
#define ZOOM_FACTOR 5
#define ZOOM_STEPS  14
#define ZOOM_DELAY 10

gboolean is_using_x11 ()
{
	return GDK_IS_X11_DISPLAY (gdk_display_get_default ());
}

typedef struct {
	int size;
	int size_start;
	int size_end;
	PanelOrientation orientation;
	double opacity;
	GdkPixbuf *pixbuf;
	guint timeout_id;
} CompositedZoomData;

static gboolean
zoom_timeout (GtkWidget *window)
{
	gtk_widget_queue_draw (window);
	return TRUE;
}

static gboolean
idle_destroy (gpointer data)
{
	gtk_widget_destroy (GTK_WIDGET (data));

	return FALSE;
}

static gboolean
zoom_draw (GtkWidget *widget,
	     cairo_t *cr,
	     gpointer        user_data)
{
	CompositedZoomData *zoom;

	zoom = user_data;

	if (zoom->size >= zoom->size_end) {
		if (zoom->timeout_id)
			g_source_remove (zoom->timeout_id);
		zoom->timeout_id = 0;

		gtk_widget_hide (widget);
		g_idle_add (idle_destroy, widget);

		g_object_unref (zoom->pixbuf);
		zoom->pixbuf = NULL;

		g_slice_free (CompositedZoomData, zoom);
	} else {
		GdkPixbuf *scaled;
		int width, height;
		int x = 0, y = 0;

		gtk_window_get_size (GTK_WINDOW (widget), &width, &height);

		zoom->size += MAX ((zoom->size_end - zoom->size_start) / ZOOM_STEPS, 1);
		zoom->opacity -= 1.0 / ((double) ZOOM_STEPS + 1);

		scaled = gdk_pixbuf_scale_simple (zoom->pixbuf,
						  zoom->size, zoom->size,
						  GDK_INTERP_BILINEAR);

		switch (zoom->orientation) {
		case PANEL_ORIENTATION_TOP:
			x = (width - gdk_pixbuf_get_width (scaled)) / 2;
			y = 0;
			break;

		case PANEL_ORIENTATION_RIGHT:
			x = width - gdk_pixbuf_get_width (scaled);
			y = (height - gdk_pixbuf_get_height (scaled)) / 2;
			break;

		case PANEL_ORIENTATION_BOTTOM:
			x = (width - gdk_pixbuf_get_width (scaled)) / 2;
			y = height - gdk_pixbuf_get_height (scaled);
			break;

		case PANEL_ORIENTATION_LEFT:
			x = 0;
			y = (height - gdk_pixbuf_get_height (scaled)) / 2;
			break;
		}

		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba (cr, 0, 0, 0, 0.0);
		cairo_rectangle (cr, 0, 0, width, height);
		cairo_fill (cr);

		gdk_cairo_set_source_pixbuf (cr, scaled, x, y);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_paint_with_alpha (cr, MAX (zoom->opacity, 0));

		g_object_unref (scaled);
	}

	return FALSE;
}

static void
draw_zoom_animation_composited (GdkScreen *gscreen,
				int x, int y, int w, int h,
				GdkPixbuf *pixbuf,
				PanelOrientation orientation)
{
	GtkWidget *win;
	CompositedZoomData *zoom;
	int wx = 0, wy = 0;

	w += 2;
	h += 2;

	zoom = g_slice_new (CompositedZoomData);
	zoom->size = w;
	zoom->size_start = w;
	zoom->size_end = w * ZOOM_FACTOR;
	zoom->orientation = orientation;
	zoom->opacity = 1.0;
	zoom->pixbuf = g_object_ref (pixbuf);
	zoom->timeout_id = 0;

	win = gtk_window_new (GTK_WINDOW_POPUP);

	gtk_window_set_screen (GTK_WINDOW (win), gscreen);
	gtk_window_set_keep_above (GTK_WINDOW (win), TRUE);
	gtk_window_set_decorated (GTK_WINDOW (win), FALSE);
	gtk_widget_set_app_paintable(win, TRUE);
	gtk_widget_set_visual (win, gdk_screen_get_rgba_visual (gscreen));

	gtk_window_set_gravity (GTK_WINDOW (win), GDK_GRAVITY_STATIC);
	gtk_window_set_default_size (GTK_WINDOW (win),
				     w * ZOOM_FACTOR, h * ZOOM_FACTOR);

	switch (zoom->orientation) {
	case PANEL_ORIENTATION_TOP:
		wx = x - w * (ZOOM_FACTOR / 2);
		wy = y;
		break;

	case PANEL_ORIENTATION_RIGHT:
		wx = x - w * (ZOOM_FACTOR - 1);
		wy = y - h * (ZOOM_FACTOR / 2);
		break;

	case PANEL_ORIENTATION_BOTTOM:
		wx = x - w * (ZOOM_FACTOR / 2);
		wy = y - h * (ZOOM_FACTOR - 1);
		break;

	case PANEL_ORIENTATION_LEFT:
		wx = x;
		wy = y - h * (ZOOM_FACTOR / 2);
		break;
	}

	gtk_window_move (GTK_WINDOW (win), wx, wy);

	g_signal_connect (G_OBJECT (win), "draw",
			 G_CALLBACK (zoom_draw), zoom);

	/* see doc for gtk_widget_set_app_paintable() */
	gtk_widget_realize (win);
	gdk_window_set_background_pattern (gtk_widget_get_window (win), NULL);
	gtk_widget_show (win);

	zoom->timeout_id = g_timeout_add (ZOOM_DELAY,
					  (GSourceFunc) zoom_timeout,
					  win);
}

static void
draw_zoom_animation (GdkScreen *gscreen,
		     int x, int y, int w, int h,
		     int fx, int fy, int fw, int fh,
		     int steps)
{
#define FRAMES (MINIATURIZE_ANIMATION_FRAMES_Z)
	float cx[FRAMES], cy[FRAMES], cw[FRAMES], ch[FRAMES];
	int cxi[FRAMES], cyi[FRAMES], cwi[FRAMES], chi[FRAMES];
	float xstep, ystep, wstep, hstep;
	int i, j;
	GC frame_gc;
	XGCValues gcv;
	GdkColor color = { 65535, 65535, 65535 };
	Display *dpy;
	Window root_win;
	int screen;
	int depth;

	dpy = gdk_x11_display_get_xdisplay (gdk_screen_get_display (gscreen));
	root_win = GDK_WINDOW_XID (gdk_screen_get_root_window (gscreen));
	screen = gdk_x11_screen_get_screen_number (gscreen);
	depth = DefaultDepth(dpy,screen);

	/* frame GC */
	gcv.function = GXxor;
	/* this will raise the probability of the XORed color being different
	 * of the original color in PseudoColor when not all color cells are
	 * initialized */
	if (DefaultVisual(dpy, screen)->class==PseudoColor)
		gcv.plane_mask = (1<<(depth-1))|1;
	else
		gcv.plane_mask = AllPlanes;
	gcv.foreground = color.pixel;
	if (gcv.foreground == 0)
		gcv.foreground = 1;
	gcv.line_width = 1;
	gcv.subwindow_mode = IncludeInferiors;
	gcv.graphics_exposures = False;

	frame_gc = XCreateGC(dpy, root_win, GCForeground|GCGraphicsExposures
			     |GCFunction|GCSubwindowMode|GCLineWidth
			     |GCPlaneMask, &gcv);

	xstep = (float)(fx-x)/steps;
	ystep = (float)(fy-y)/steps;
	wstep = (float)(fw-w)/steps;
	hstep = (float)(fh-h)/steps;

	for (j=0; j<FRAMES; j++) {
		cx[j] = (float)x;
		cy[j] = (float)y;
		cw[j] = (float)w;
		ch[j] = (float)h;
		cxi[j] = (int)cx[j];
		cyi[j] = (int)cy[j];
		cwi[j] = (int)cw[j];
		chi[j] = (int)ch[j];
	}
	XGrabServer(dpy);
	for (i=0; i<steps; i++) {
		for (j=0; j<FRAMES; j++) {
			XDrawRectangle(dpy, root_win, frame_gc, cxi[j], cyi[j], cwi[j], chi[j]);
		}
		XFlush(dpy);
#if (MINIATURIZE_ANIMATION_DELAY_Z > 0)
		usleep(MINIATURIZE_ANIMATION_DELAY_Z);
#else
		usleep(10);
#endif
		for (j=0; j<FRAMES; j++) {
			XDrawRectangle(dpy, root_win, frame_gc,
				       cxi[j], cyi[j], cwi[j], chi[j]);
			if (j<FRAMES-1) {
				cx[j]=cx[j+1];
				cy[j]=cy[j+1];
				cw[j]=cw[j+1];
				ch[j]=ch[j+1];

				cxi[j]=cxi[j+1];
				cyi[j]=cyi[j+1];
				cwi[j]=cwi[j+1];
				chi[j]=chi[j+1];

			} else {
				cx[j]+=xstep;
				cy[j]+=ystep;
				cw[j]+=wstep;
				ch[j]+=hstep;

				cxi[j] = (int)cx[j];
				cyi[j] = (int)cy[j];
				cwi[j] = (int)cw[j];
				chi[j] = (int)ch[j];
			}
		}
	}

	for (j=0; j<FRAMES; j++) {
		XDrawRectangle(dpy, root_win, frame_gc,
				       cxi[j], cyi[j], cwi[j], chi[j]);
	}
	XFlush(dpy);
#if (MINIATURIZE_ANIMATION_DELAY_Z > 0)
	usleep(MINIATURIZE_ANIMATION_DELAY_Z);
#else
	usleep(10);
#endif
	for (j=0; j<FRAMES; j++) {
		XDrawRectangle(dpy, root_win, frame_gc,
				       cxi[j], cyi[j], cwi[j], chi[j]);
	}

	XUngrabServer(dpy);
	XFreeGC (dpy, frame_gc);
}
#undef FRAMES

void
xstuff_zoom_animate (GtkWidget *widget,
		     cairo_surface_t *surface,
		     PanelOrientation orientation,
		     GdkRectangle *opt_rect)
{
	GdkScreen *gscreen;
	GdkRectangle rect, dest;
	GtkAllocation allocation;
	GdkMonitor *monitor;
	GdkDisplay *display;

	if (opt_rect)
		rect = *opt_rect;
	else {
		gdk_window_get_origin (gtk_widget_get_window (widget), &rect.x, &rect.y);
		gtk_widget_get_allocation (widget, &allocation);
		if (!gtk_widget_get_has_window (widget)) {
			rect.x += allocation.x;
			rect.y += allocation.y;
		}
		rect.height = allocation.height;
		rect.width = allocation.width;
	}

	gscreen = gtk_widget_get_screen (widget);

	if (gdk_screen_is_composited (gscreen) && surface) {
		GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface (surface,
				0, 0,
				cairo_image_surface_get_width (surface),
				cairo_image_surface_get_height (surface));
		draw_zoom_animation_composited (gscreen,
				rect.x, rect.y,
				rect.width, rect.height,
				pixbuf, orientation);
		g_object_unref (pixbuf);
	} else {
		display = gdk_screen_get_display (gscreen);
		monitor = gdk_display_get_monitor_at_window (display,
							     gtk_widget_get_window (widget));
		gdk_monitor_get_geometry (monitor, &dest);

		draw_zoom_animation (gscreen,
				     rect.x, rect.y, rect.width, rect.height,
				     dest.x, dest.y, dest.width, dest.height,
				     MINIATURIZE_ANIMATION_STEPS_Z);
	}
}

static int
xstuff_xio_error_handler (Display *display)
{
	xstuff_display_is_dead = TRUE;

	if (xstuff_old_xio_error_handler)
		return xstuff_old_xio_error_handler (display);

	return 0;
}

gboolean
xstuff_is_display_dead (void)
{
	return xstuff_display_is_dead;
}

static int
xstuff_x_error_handler (Display *display, XErrorEvent *error)
{
	if (!error->error_code)
		return 0;

	/* If we got a BadDrawable or a BadWindow, we ignore it for now.
	 * FIXME: We need to somehow distinguish real errors from
	 * X-server-induced errors. Keeping a list of windows for which we will
	 * ignore BadDrawables would be a good idea.  */
	if (error->error_code == BadDrawable ||
	    error->error_code == BadWindow)
		return 0;

	return xstuff_old_x_error_handler (display, error);
}

void
xstuff_init (void)
{
	xstuff_old_xio_error_handler = XSetIOErrorHandler (xstuff_xio_error_handler);
	xstuff_old_x_error_handler = XSetErrorHandler (xstuff_x_error_handler);
}
