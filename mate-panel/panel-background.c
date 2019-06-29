/*
 * panel-background.c: panel background rendering
 *
 * Copyright (C) 2002, 2003 Sun Microsystems, Inc.
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
 *
 * Authors:
 *      Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "panel-background.h"

#include <string.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <cairo.h>

#ifdef HAVE_X11
#include <xstuff.h>
#include <cairo-xlib.h>
#endif

#include "panel-util.h"


static gboolean panel_background_composite (PanelBackground *background);
static void load_background_file (PanelBackground *background);


static void
set_pixbuf_background (PanelBackground *background)
{
	g_assert (background->composited_pattern != NULL);

	gdk_window_set_background_pattern (background->window, background->composited_pattern);
}

void panel_background_apply_css (PanelBackground *background, GtkWidget *widget)
{
	GtkStyleContext     *context;
	PanelBackgroundType  effective_type;

	context = gtk_widget_get_style_context (widget);
	effective_type = panel_background_effective_type (background);

	switch (effective_type) {
	case PANEL_BACK_NONE:
		gtk_style_context_remove_class (context, "mate-custom-panel-background");
		break;
	case PANEL_BACK_COLOR:
	case PANEL_BACK_IMAGE:
		gtk_style_context_add_class (context, "mate-custom-panel-background");
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
panel_background_prepare_css ()
{
	GtkCssProvider      *provider;

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider,
					 ".mate-custom-panel-background{\n"
					 " background-color: rgba (0, 0, 0, 0);\n"
					 " background-image: none;\n"
					 "}",
					 -1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}

static gboolean
panel_background_prepare (PanelBackground *background)
{
	PanelBackgroundType  effective_type;
	GtkWidget           *widget = NULL;

	if (!background->transformed)
		return FALSE;

	effective_type = panel_background_effective_type (background);

	switch (effective_type) {
	case PANEL_BACK_NONE:
		if (background->default_pattern) {
			/* the theme background-image pattern must be scaled by
			* the width & height of the panel so that when the
			* backing region is cleared
			* (gdk_window_clear_backing_region), the correctly
			* scaled pattern is used */
			cairo_matrix_t m;
			cairo_surface_t *surface;
			double width, height;

			surface = NULL;
			width = 1.0;
			height = 1.0;
			cairo_pattern_get_surface(background->default_pattern, &surface);
			/* catch invalid images (e.g. -gtk-gradient) before scaling and rendering */
			if (surface != NULL ){
				cairo_surface_reference(surface);
				width = cairo_image_surface_get_width (surface);
				height = cairo_image_surface_get_height (surface);
				cairo_matrix_init_translate (&m, 0, 0);
				cairo_matrix_scale (&m,
						width / background->region.width,
						height / background->region.height);
				cairo_pattern_set_matrix (background->default_pattern, &m);

				gdk_window_set_background_pattern (background->window,
											background->default_pattern);
			}
			else {
				g_warning ("%s", "unsupported value of 'background-image' in GTK+ theme (such as '-gtk-gradient')");
				/* use any background color that has been set if image is invalid */
				gdk_window_set_background_rgba (
				background->window, &background->default_color);
			}
			cairo_surface_destroy(surface);
		} else
			gdk_window_set_background_rgba (
				background->window, &background->default_color);
		break;

	case PANEL_BACK_COLOR:
#ifdef HAVE_X11
		if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()) &&
		    background->has_alpha &&
		    !gdk_window_check_composited_wm(background->window))
			set_pixbuf_background (background);
		else
#endif // HAVE_X11
		{ // Not using X11, or pixbuf background not needed
			gdk_window_set_background_rgba (background->window,
			                                &background->color);
		}
		break;

	case PANEL_BACK_IMAGE:
		set_pixbuf_background (background);
		break;

	default:
		g_assert_not_reached ();
		break;
	}

	/* Panel applets may use the panel's background pixmap to
	 * decide how to draw themselves.  Therefore, we need to
	 * make sure that all drawing has been completed before
	 * the applet looks at the pixmap. */
	gdk_display_sync (gdk_window_get_display (background->window));

	gdk_window_get_user_data (GDK_WINDOW (background->window),
				  (gpointer) &widget);

	if (GTK_IS_WIDGET (widget)) {
		panel_background_apply_css (background, gtk_widget_get_toplevel(widget));
		gtk_widget_set_app_paintable(widget,TRUE);
		gtk_widget_queue_draw (widget);
	}

	background->notify_changed (background, background->user_data);

	return TRUE;
}

static void
free_composited_resources (PanelBackground *background)
{
	background->composited = FALSE;

	if (background->composited_pattern)
		cairo_pattern_destroy (background->composited_pattern);
	background->composited_pattern = NULL;
}

#ifdef HAVE_X11

static void _panel_background_transparency(GdkScreen* screen,PanelBackground* background)
{
	panel_background_composite(background);
}

static void
background_changed (PanelBackgroundMonitor *monitor,
		    PanelBackground        *background)
{
	GdkPixbuf *tmp;

	g_return_if_fail (GDK_IS_X11_DISPLAY (gdk_display_get_default ()));

	tmp = background->desktop;

	background->desktop = panel_background_monitor_get_region (
					background->monitor,
					background->region.x,
					background->region.y,
					background->region.width,
					background->region.height);

	if (tmp)
		g_object_unref (tmp);

	panel_background_composite (background);
}

static GdkPixbuf *
get_desktop_pixbuf (PanelBackground *background)
{
	GdkPixbuf *desktop;

	g_return_val_if_fail (GDK_IS_X11_DISPLAY (gdk_display_get_default ()), NULL);

	if (!background->monitor) {
		background->monitor =
			panel_background_monitor_get_for_screen (
				gdk_window_get_screen (background->window));

		background->monitor_signal =
			g_signal_connect (
			background->monitor, "changed",
                        G_CALLBACK (background_changed), background);
	}
	g_signal_connect(gdk_window_get_screen(background->window), "composited-changed",
			 G_CALLBACK(_panel_background_transparency),
			 background);

	desktop = panel_background_monitor_get_region (
				background->monitor,
				background->region.x,
				background->region.y,
				background->region.width,
				background->region.height);

	return desktop;
}

#endif // HAVE_X11

static cairo_pattern_t *
composite_image_onto_desktop (PanelBackground *background)
{
	int              width, height;
	cairo_t         *cr;
	cairo_surface_t *surface;
	cairo_pattern_t *pattern;

	width  = background->region.width;
	height = background->region.height;

	surface = gdk_window_create_similar_surface (background->window,
						     CAIRO_CONTENT_COLOR_ALPHA,
						     width, height);
	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy (surface);
		return NULL;
	}

	cr = cairo_create (surface);

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		if (!background->desktop)
			background->desktop = get_desktop_pixbuf (background);

		if(!gdk_window_check_composited_wm (background->window)){
			cairo_set_source_rgb (cr, 1, 1, 1);
			cairo_paint (cr);

			if (background->desktop) {
				gdk_cairo_set_source_pixbuf (cr, background->desktop, 0, 0);
				cairo_rectangle (cr, 0, 0, width, height);
				cairo_fill (cr);
			}
		}
	}
#endif // HAVE_X11

	gdk_cairo_set_source_pixbuf (cr, background->transformed_image, 0, 0);
	pattern = cairo_get_source (cr);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);

	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	cairo_destroy (cr);

	pattern = cairo_pattern_create_for_surface (surface);
	cairo_surface_destroy (surface);
	return pattern;
}

static cairo_pattern_t *
composite_color_onto_desktop (PanelBackground *background)
{
	cairo_surface_t *surface;
	cairo_pattern_t *pattern;
	cairo_t *cr;

	surface = gdk_window_create_similar_surface (background->window,
						     CAIRO_CONTENT_COLOR_ALPHA,
						     background->region.width,
						     background->region.height);
	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy (surface);
		return NULL;
	}

	cr = cairo_create (surface);

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		if (!background->desktop)
			background->desktop = get_desktop_pixbuf (background);

		if(!gdk_window_check_composited_wm (background->window)){
			if (background->desktop) {
				gdk_cairo_set_source_pixbuf (cr, background->desktop, 0, 0);
				cairo_paint (cr);
			}
		}
	}
#endif // HAVE_X11

	gdk_cairo_set_source_rgba (cr, &background->color);
	cairo_paint (cr);

	cairo_destroy (cr);

	pattern = cairo_pattern_create_for_surface (surface);
	cairo_surface_destroy (surface);

	return pattern;
}

static cairo_pattern_t *
get_composited_pattern (PanelBackground *background)
{
	cairo_pattern_t *retval = NULL;

	switch (background->type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		retval = composite_color_onto_desktop (background);
		break;
	case PANEL_BACK_IMAGE:
		retval = composite_image_onto_desktop (background);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	return retval;
}

static gboolean
panel_background_composite (PanelBackground *background)
{
	if (!background->transformed)
		return FALSE;

	free_composited_resources (background);

	switch (background->type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		if (background->has_alpha)
				background->composited_pattern =
					get_composited_pattern (background);
		break;
	case PANEL_BACK_IMAGE:
        if (background->transformed_image) {
			background->composited_pattern =
				get_composited_pattern (background);
		}
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	background->composited = TRUE;


	panel_background_prepare (background);

	return TRUE;
}

static void
free_transformed_resources (PanelBackground *background)
{
	free_composited_resources (background);

	background->transformed = FALSE;

	if (background->type != PANEL_BACK_IMAGE)
		return;

	if (background->transformed_image)
		g_object_unref (background->transformed_image);
	background->transformed_image = NULL;
}

static GdkPixbuf *
get_scaled_and_rotated_pixbuf (PanelBackground *background)
{
	GdkPixbuf *scaled;
	GdkPixbuf *retval;
	int        orig_width, orig_height;
	int        panel_width, panel_height;
	int        width, height;

	load_background_file (background);
	if (!background->loaded_image)
		return NULL;

	orig_width  = gdk_pixbuf_get_width  (background->loaded_image);
	orig_height = gdk_pixbuf_get_height (background->loaded_image);

	panel_width  = background->region.width;
	panel_height = background->region.height;

	width  = orig_width;
	height = orig_height;

	if (background->fit_image) {
		switch (background->orientation) {
		case GTK_ORIENTATION_HORIZONTAL:
			width  = orig_width * panel_height / orig_height;
			height = panel_height;
			break;
		case GTK_ORIENTATION_VERTICAL:
			if (background->rotate_image) {
				width  = orig_width * panel_width / orig_height;
				height = panel_width;
			} else {
				width  = panel_width;
				height = orig_height * panel_width / orig_width;
			}
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	} else if (background->stretch_image) {
		if (background->orientation == GTK_ORIENTATION_VERTICAL &&
		    background->rotate_image) {
			width  = panel_height;
			height = panel_width;
		} else {
			width  = panel_width;
			height = panel_height;
		}
	} else if (background->orientation == GTK_ORIENTATION_VERTICAL &&
		   background->rotate_image) {
		int tmp = width;
		width = height;
		height = tmp;
	}

	if (width == orig_width &&
	    height == orig_height) {
		scaled = background->loaded_image;
		g_object_ref (scaled);
	} else {
		scaled = gdk_pixbuf_scale_simple (
				background->loaded_image,
				width, height,
				GDK_INTERP_BILINEAR);
	}

	if (background->rotate_image &&
	    background->orientation == GTK_ORIENTATION_VERTICAL) {
		if (!background->has_alpha) {
			guchar *dest;
			guchar *src;
			int     x, y;
			int     destrowstride;
			int     srcrowstride;

			retval = gdk_pixbuf_new (
				GDK_COLORSPACE_RGB, FALSE, 8, height, width);

			dest          = gdk_pixbuf_get_pixels (retval);
			destrowstride = gdk_pixbuf_get_rowstride (retval);
			src           = gdk_pixbuf_get_pixels (scaled);
			srcrowstride  = gdk_pixbuf_get_rowstride (scaled);

			for (y = 0; y < height; y++)
				for (x = 0; x < width; x++) {
					guchar *dstptr = & ( dest [3*y + destrowstride * (width - x - 1)] );
					guchar *srcptr = & ( src [y * srcrowstride + 3*x] );
					dstptr[0] = srcptr[0];
					dstptr[1] = srcptr[1];
					dstptr[2] = srcptr[2];
				}

			g_object_unref (scaled);
		} else {
			guint32 *dest;
			guint32 *src;
			int     x, y;
			int     destrowstride;
			int     srcrowstride;

			retval = gdk_pixbuf_new (
				GDK_COLORSPACE_RGB, TRUE, 8, height, width);

			dest          = (guint32 *) gdk_pixbuf_get_pixels (retval);
			destrowstride =             gdk_pixbuf_get_rowstride (retval) / 4;
			src           = (guint32 *) gdk_pixbuf_get_pixels (scaled);
			srcrowstride  =             gdk_pixbuf_get_rowstride (scaled) / 4;

			for (y = 0; y < height; y++)
				for (x = 0; x < width; x++)
					dest [y + destrowstride * (width - x - 1)] =
						src [y * srcrowstride + x];

			g_object_unref (scaled);
		}
	} else
		retval = scaled;

	return retval;
}

static gboolean
panel_background_transform (PanelBackground *background)
{
	if (background->region.width == -1)
		return FALSE;

	free_transformed_resources (background);

	if (background->type == PANEL_BACK_IMAGE)
		background->transformed_image =
			get_scaled_and_rotated_pixbuf (background);

	background->transformed = TRUE;

	panel_background_composite (background);

	return TRUE;
}

#ifdef HAVE_X11
static void
disconnect_background_monitor (PanelBackground *background)
{
	g_return_if_fail (GDK_IS_X11_DISPLAY (gdk_display_get_default ()));
	if (background->monitor) {
		g_signal_handler_disconnect (
			background->monitor, background->monitor_signal);
		background->monitor_signal = -1;
		g_object_unref (background->monitor);
	}
	background->monitor = NULL;

	if (background->desktop)
		g_object_unref (background->desktop);
	background->desktop = NULL;
}
#endif // HAVE_X11

static void
panel_background_update_has_alpha (PanelBackground *background)
{
	gboolean has_alpha = FALSE;

	if (background->type == PANEL_BACK_COLOR)
		has_alpha = (background->color.alpha < 1.);

	else if (background->type == PANEL_BACK_IMAGE &&
		 background->loaded_image)
		has_alpha = gdk_pixbuf_get_has_alpha (background->loaded_image);

	background->has_alpha = has_alpha;

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		if (!has_alpha)
			disconnect_background_monitor (background);
	}
#endif // HAVE_X11
}

static void
load_background_file (PanelBackground *background)
{
	GError *error = NULL;

	if (!g_file_test (background->image, G_FILE_TEST_IS_REGULAR))
		return;

	//FIXME add a monitor on the file so that we reload the background
	//when it changes
	background->loaded_image =
		gdk_pixbuf_new_from_file (background->image, &error);
	if (!background->loaded_image) {
		g_assert (error != NULL);
		g_warning (G_STRLOC ": unable to open '%s': %s",
			   background->image, error->message);
		g_error_free (error);
	}

	panel_background_update_has_alpha (background);
}

void
panel_background_set_type (PanelBackground     *background,
			   PanelBackgroundType  type)
{
	if (background->type == type)
		return;

	free_transformed_resources (background);

	background->type = type;

	panel_background_update_has_alpha (background);

	panel_background_transform (background);
}

static void
panel_background_set_opacity_no_update (PanelBackground *background,
				        guint16          opacity)
{
	background->color.alpha = opacity / 65535.0;
	panel_background_update_has_alpha (background);
}

void
panel_background_set_opacity (PanelBackground *background,
			      guint16          opacity)
{
	if (background->color.alpha == (opacity / 65535.0))
		return;

	free_transformed_resources (background);
	panel_background_set_opacity_no_update (background, opacity);
	panel_background_transform (background);
}

static void
panel_background_set_color_no_update (PanelBackground *background,
				      const GdkRGBA   *color)
{
	g_return_if_fail (color != NULL);

	if (gdk_rgba_equal (color, &background->color))
		return;

	background->color = *color;
	panel_background_update_has_alpha (background);
}

void
panel_background_set_color (PanelBackground *background,
			    const GdkRGBA   *color)
{
	g_return_if_fail (color != NULL);

	if (gdk_rgba_equal (color, &background->color))
		return;

	free_transformed_resources (background);
	panel_background_set_color_no_update (background, color);
	panel_background_transform (background);
}

static void
panel_background_set_image_no_update (PanelBackground *background,
				      const char      *image)
{
	if (background->loaded_image)
		g_object_unref (background->loaded_image);
	background->loaded_image = NULL;

	if (background->image)
		g_free (background->image);
	background->image = NULL;

	if (image && image [0])
		background->image = g_strdup (image);

	panel_background_update_has_alpha (background);
}

void
panel_background_set_image (PanelBackground *background,
			    const char      *image)
{
	if (!background->image && !image)
		return;

	if (background->image && image && !strcmp (background->image, image))
		return;

	free_transformed_resources (background);
	panel_background_set_image_no_update (background, image);
	panel_background_transform (background);
}

static void
panel_background_set_fit_no_update (PanelBackground *background,
				    gboolean         fit_image)
{
	background->fit_image = fit_image != FALSE;
}

void
panel_background_set_fit (PanelBackground *background,
			  gboolean         fit_image)
{
	fit_image = fit_image != FALSE;

	if (background->fit_image == fit_image)
		return;

	free_transformed_resources (background);
	panel_background_set_fit_no_update (background, fit_image);
	panel_background_transform (background);
}

static void
panel_background_set_stretch_no_update (PanelBackground *background,
				        gboolean         stretch_image)
{
	background->stretch_image = stretch_image != FALSE;
}

void
panel_background_set_stretch (PanelBackground *background,
			      gboolean         stretch_image)
{
	stretch_image = stretch_image != FALSE;

	if (background->stretch_image == stretch_image)
		return;

	free_transformed_resources (background);
	panel_background_set_stretch_no_update (background, stretch_image);
	panel_background_transform (background);
}

static void
panel_background_set_rotate_no_update (PanelBackground *background,
				       gboolean         rotate_image)
{
	background->rotate_image = rotate_image != FALSE;
}

void
panel_background_set_rotate (PanelBackground *background,
			     gboolean         rotate_image)
{
	rotate_image = rotate_image != FALSE;

	if (background->rotate_image == rotate_image)
		return;

	free_transformed_resources (background);
	panel_background_set_rotate_no_update (background, rotate_image);
	panel_background_transform (background);
}

void
panel_background_set (PanelBackground     *background,
		      PanelBackgroundType  type,
		      const GdkRGBA       *color,
		      const char          *image,
		      gboolean             fit_image,
		      gboolean             stretch_image,
		      gboolean             rotate_image)
{
	panel_background_set_color_no_update (background, color);
	panel_background_set_image_no_update (background, image);
	panel_background_set_fit_no_update (background, fit_image);
	panel_background_set_stretch_no_update (background, stretch_image);
	panel_background_set_rotate_no_update (background, rotate_image);
	panel_background_set_type (background, type);
}

void
panel_background_set_default_style (PanelBackground *background,
				    GdkRGBA         *color,
				    cairo_pattern_t *pattern)
{
	g_return_if_fail (color != NULL);

	background->default_color = *color;

	if (pattern)
		cairo_pattern_reference (pattern);

	if (background->default_pattern)
		cairo_pattern_destroy (background->default_pattern);

	background->default_pattern = pattern;
	if (background->type == PANEL_BACK_NONE)
		panel_background_prepare (background);
}


void
panel_background_realized (PanelBackground *background,
			   GdkWindow       *window)
{
	g_return_if_fail (window != NULL);

	if (background->window)
		return;

	background->window = g_object_ref (window);

	panel_background_prepare_css ();

	panel_background_prepare (background);
}

void
panel_background_unrealized (PanelBackground *background)
{
	if (background->window)
		g_object_unref (background->window);
	background->window = NULL;
}

void
panel_background_change_region (PanelBackground *background,
				GtkOrientation   orientation,
				int              x,
				int              y,
				int              width,
				int              height)
{
	gboolean need_to_retransform = FALSE;
	gboolean need_to_reprepare = FALSE;

	if (background->region.x == x &&
	    background->region.y == y &&
	    background->region.width == width &&
	    background->region.height == height &&
	    background->orientation == orientation)
		return;

	/* we only need to retransform anything
	   on size/orientation changes if the
	   background is an image and some
	   conditions are met */
	if (background->type == PANEL_BACK_IMAGE) {
		if (background->orientation != orientation &&
		    background->rotate_image) {
			/* if orientation changes and we are rotating */
			need_to_retransform = TRUE;
		} else if ((background->region.width != width ||
			    background->region.height != height) &&
			   (background->fit_image ||
			    background->stretch_image)) {
			/* or if the size changes and we are
			   stretching or fitting the image */
			need_to_retransform = TRUE;
		}
	}

	/* if size changed, we at least need
	   to "prepare" the background again */
	if (background->region.width != width ||
	    background->region.height != height)
		need_to_reprepare = TRUE;

	background->region.x      = x;
	background->region.y      = y;
	background->region.width  = width;
	background->region.height = height;

	background->orientation = orientation;

#ifdef HAVE_X11
	if (background->desktop)
		g_object_unref (background->desktop);
	background->desktop = NULL;
#endif // HAVE_X11

	if (need_to_retransform || ! background->transformed)
		/* only retransform the background if we have in
		   fact changed size/orientation */
		panel_background_transform (background);
	else if (background->has_alpha || ! background->composited)
		/* only do compositing if we have some alpha
		   value to worry about */
		panel_background_composite (background);
	else if (need_to_reprepare)
		/* at least we must prepare the background
		   if the size changed */
		panel_background_prepare (background);
}

void
panel_background_init (PanelBackground              *background,
		       PanelBackgroundChangedNotify  notify_changed,
		       gpointer                      user_data)
{
	background->type = PANEL_BACK_NONE;
	background->notify_changed = notify_changed;
	background->user_data = user_data;

	background->color.red = 0.;
	background->color.blue = 0.;
	background->color.green = 0.;
	background->color.alpha = 1.;

	background->image        = NULL;
	background->loaded_image = NULL;

	background->orientation       = GTK_ORIENTATION_HORIZONTAL;
	background->region.x          = -1;
	background->region.y          = -1;
	background->region.width      = -1;
	background->region.height     = -1;
	background->transformed_image = NULL;
	background->composited_pattern = NULL;

#ifdef HAVE_X11
	background->monitor        = NULL;
	background->desktop        = NULL;
	background->monitor_signal = -1;
#endif // HAVE_X11

	background->window   = NULL;

	background->default_pattern     = NULL;

	background->default_color.red   = 0.;
	background->default_color.green = 0.;
	background->default_color.blue  = 0.;
	background->default_color.alpha = 1.;

	background->fit_image     = FALSE;
	background->stretch_image = FALSE;
	background->rotate_image  = FALSE;

	background->has_alpha = FALSE;

	background->transformed = FALSE;
	background->composited  = FALSE;
}

void
panel_background_free (PanelBackground *background)
{
#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		disconnect_background_monitor (background);
	}
#endif // HAVE_X11

	free_transformed_resources (background);

	if (background->image)
		g_free (background->image);
	background->image = NULL;

	if (background->loaded_image)
		g_object_unref (background->loaded_image);
	background->loaded_image = NULL;

#ifdef HAVE_X11
	if (background->monitor)
		g_object_unref (background->monitor);
	background->monitor = NULL;
#endif // HAVE_X11

	if (background->window)
		g_object_unref (background->window);
	background->window = NULL;

	if (background->default_pattern)
		cairo_pattern_destroy (background->default_pattern);
	background->default_pattern = NULL;
}

char *
panel_background_make_string (PanelBackground *background,
			      int              x,
			      int              y)
{
	PanelBackgroundType  effective_type;
	char                *retval;

	retval = NULL;

	effective_type = panel_background_effective_type (background);

#ifdef HAVE_X11
	if (is_using_x11 () &&
	    (effective_type == PANEL_BACK_IMAGE ||
	     (effective_type == PANEL_BACK_COLOR &&
	      background->has_alpha &&
	      !gdk_window_check_composited_wm(background->window)))) {

		cairo_surface_t *surface;

		if (!background->composited_pattern)
			return NULL;

		if (cairo_pattern_get_surface (background->composited_pattern, &surface) != CAIRO_STATUS_SUCCESS)
			return NULL;

		if (cairo_surface_get_type (surface) != CAIRO_SURFACE_TYPE_XLIB)
			return NULL;

		retval = g_strdup_printf ("pixmap:%d,%d,%d", (guint32)cairo_xlib_surface_get_drawable (surface), x, y);
	} else
#endif
	if (effective_type == PANEL_BACK_COLOR) {
		gchar *rgba = gdk_rgba_to_string (&background->color);
		retval = g_strdup_printf (
				"color:%s",
				rgba);
		g_free (rgba);
	} else
		retval = g_strdup ("none:");

	return retval;
}

PanelBackgroundType
panel_background_get_type (PanelBackground *background)
{
	return background->type;
}

const GdkRGBA *
panel_background_get_color (PanelBackground *background)
{
	return &(background->color);
}

/* What are we actually rendering - e.g. if we're supposed to
 * be rendering an image, but haven't got a valid image, then
 * we're rendering the default gtk background.
 */
PanelBackgroundType
panel_background_effective_type (PanelBackground *background)
{
	PanelBackgroundType retval;

	retval = background->type;
	if (background->type == PANEL_BACK_IMAGE && !background->composited_pattern)
		retval = PANEL_BACK_NONE;

	return retval;
}
