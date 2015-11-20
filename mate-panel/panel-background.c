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
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <cairo.h>
#if GTK_CHECK_VERSION (3, 0, 0)
#include <cairo-xlib.h>
#endif

#include "panel-background-monitor.h"
#include "panel-util.h"


static gboolean panel_background_composite (PanelBackground *background);
static void load_background_file (PanelBackground *background);


static void
free_prepared_resources (PanelBackground *background)
{
	background->prepared = FALSE;
#if !GTK_CHECK_VERSION (3, 0, 0)
	switch (background->type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		if (background->has_alpha) {
			if (background->pixmap)
				g_object_unref (background->pixmap);
			background->pixmap = NULL;
		} else {
			if (background->colormap && background->color.gdk.pixel)
				gdk_colormap_free_colors (
					background->colormap,
					&background->color.gdk, 1);
			background->color.gdk.pixel = 0;
		}
		break;
	case PANEL_BACK_IMAGE:
		if (background->pixmap)
			g_object_unref (background->pixmap);
		background->pixmap = NULL;
		break;
	default:
		g_assert_not_reached ();
		break;
	}
#endif
}

static void
set_pixbuf_background (PanelBackground *background)
{
#if GTK_CHECK_VERSION (3, 0, 0)
	g_assert (background->composited_pattern != NULL);

	gdk_window_set_background_pattern (background->window, background->composited_pattern);
#else
	g_assert (background->composited_image != NULL);

	gdk_pixbuf_render_pixmap_and_mask_for_colormap (
		background->composited_image,
		background->colormap,
		&background->pixmap, NULL, 128);

	gdk_window_set_back_pixmap (
		background->window, background->pixmap, FALSE);
#endif
}

#if GTK_CHECK_VERSION (3, 0, 0)
void panel_background_apply_css (GtkWidget* widget, PanelBackground *background)
{
	GtkStyleContext     *context;
	PanelBackgroundType  effective_type;

	context = gtk_widget_get_style_context (widget);
	effective_type = panel_background_effective_type (background);
	gtk_widget_reset_style (widget);

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
#endif

static gboolean
panel_background_prepare (PanelBackground *background)
{
	PanelBackgroundType  effective_type;
	GtkWidget           *widget = NULL;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (!background->transformed)
#else
	if (!background->colormap || !background->transformed)
#endif
		return FALSE;

	free_prepared_resources (background);

	effective_type = panel_background_effective_type (background);



	switch (effective_type) {
	case PANEL_BACK_NONE:
#if GTK_CHECK_VERSION (3, 0, 0)
		if (background->default_pattern) {
			/* the theme background-image pattern must be scaled by
			* the width & height of the panel so that when the
			* backing region is cleared
			* (gdk_window_clear_backing_region), the correctly
			* scaled pattern is used */
			cairo_matrix_t m;

			cairo_matrix_init_translate (&m, 0, 0);
			cairo_matrix_scale (&m,
					    1.0 / background->region.width,
					    1.0 / background->region.height);
			cairo_pattern_set_matrix (background->default_pattern, &m);

			gdk_window_set_background_pattern (background->window,
						       background->default_pattern);
#else
		if (background->default_pixmap) {
			if (background->default_pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
				gdk_window_set_back_pixmap (background->window,
							    background->default_pixmap,
							    FALSE);
			else
				gdk_window_set_back_pixmap (background->window,
							    NULL,
							    TRUE);
#endif
		} else
#if GTK_CHECK_VERSION (3, 0, 0)
			gdk_window_set_background_rgba (
				background->window, &background->default_color);
#else
			gdk_window_set_background (
				background->window, &background->default_color);
#endif
		break;
	case PANEL_BACK_COLOR:
		if (background->has_alpha &&
#if GTK_CHECK_VERSION (3, 0, 0)
		    !gdk_window_check_composited_wm(background->window))
#else
		    background->composited_image)
#endif
			set_pixbuf_background (background);
		else {
#if GTK_CHECK_VERSION (3, 0, 0)
			gdk_window_set_background_rgba (background->window,
							&background->color);
#else
			gdk_colormap_alloc_color (
				background->colormap,
				&background->color.gdk,
				FALSE, TRUE);
			gdk_window_set_background (
				background->window, &background->color.gdk);
#endif
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
#if GTK_CHECK_VERSION (3, 0, 0)

	gdk_display_sync (gdk_window_get_display (background->window));
#else
	gdk_display_sync (gdk_drawable_get_display (background->window));
#endif

	gdk_window_get_user_data (GDK_WINDOW (background->window),
				  (gpointer) &widget);

	if (GTK_IS_WIDGET (widget)) {
#if GTK_CHECK_VERSION (3, 0, 0)
		panel_background_apply_css (gtk_widget_get_toplevel(widget), background);
#endif
		gtk_widget_set_app_paintable(widget,TRUE);
		gtk_widget_queue_draw (widget);
	}

	background->prepared = TRUE;

	background->notify_changed (background, background->user_data);

	return TRUE;
}

static void
free_composited_resources (PanelBackground *background)
{
	free_prepared_resources (background);

	background->composited = FALSE;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (background->composited_pattern)
		cairo_pattern_destroy (background->composited_pattern);
	background->composited_pattern = NULL;
#else
	if (background->composited_image)
		g_object_unref (background->composited_image);
	background->composited_image = NULL;
#endif
}

#if GTK_CHECK_VERSION (3, 0, 0)
static void _panel_background_transparency(GdkScreen* screen,PanelBackground* background)
{
	panel_background_composite(background);
}
#endif

static void
background_changed (PanelBackgroundMonitor *monitor,
		    PanelBackground        *background)
{
	GdkPixbuf *tmp;

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

	if (!background->monitor) {
		background->monitor =
			panel_background_monitor_get_for_screen (
#if GTK_CHECK_VERSION (3, 0, 0)
				gdk_window_get_screen (background->window));
#else
				gdk_drawable_get_screen (background->window));
#endif

		background->monitor_signal =
			g_signal_connect (
			background->monitor, "changed",
                        G_CALLBACK (background_changed), background);
	}
#if GTK_CHECK_VERSION (3, 0, 0)
	g_signal_connect(gdk_window_get_screen(background->window), "composited-changed",
			 G_CALLBACK(_panel_background_transparency),
			 background);
#endif
	desktop = panel_background_monitor_get_region (
				background->monitor,
				background->region.x,
				background->region.y,
				background->region.width,
				background->region.height);

	return desktop;
}

#if GTK_CHECK_VERSION (3, 0, 0)
static cairo_pattern_t *
#else
static GdkPixbuf *
#endif
composite_image_onto_desktop (PanelBackground *background)
{
#if !GTK_CHECK_VERSION (3, 0, 0)
	GdkPixbuf       *retval;
	unsigned char   *data;
#endif
	int              width, height;
	cairo_t         *cr;
	cairo_surface_t *surface;
	cairo_pattern_t *pattern;

	if (!background->desktop)
		background->desktop = get_desktop_pixbuf (background);

	if (!background->desktop)
		return NULL;

	width  = gdk_pixbuf_get_width  (background->desktop);
	height = gdk_pixbuf_get_height (background->desktop);

#if GTK_CHECK_VERSION (3, 0, 0)
	surface = gdk_window_create_similar_surface (background->window,
						     CAIRO_CONTENT_COLOR_ALPHA,
						     width, height);
	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy (surface);
		return NULL;
	}
#else
	data = g_malloc (width * height * 4);
	if (!data)
		return NULL;

	surface = cairo_image_surface_create_for_data (data,
						       CAIRO_FORMAT_RGB24,
						       width, height,
						       width * 4);
#endif

	cr = cairo_create (surface);
#if GTK_CHECK_VERSION (3, 0, 0)
	if(!gdk_window_check_composited_wm(background->window)){
#endif
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_paint (cr);

	gdk_cairo_set_source_pixbuf (cr, background->desktop, 0, 0);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);
#if GTK_CHECK_VERSION (3, 0, 0)
	}
#endif

	gdk_cairo_set_source_pixbuf (cr, background->transformed_image, 0, 0);
	pattern = cairo_get_source (cr);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);

	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	cairo_destroy (cr);

#if GTK_CHECK_VERSION (3, 0, 0)
	pattern = cairo_pattern_create_for_surface (surface);
	cairo_surface_destroy (surface);
	return pattern;
#else
	cairo_surface_destroy (surface);
	retval = panel_util_cairo_rgbdata_to_pixbuf (data, width, height);
	g_free (data);
	return retval;
#endif
}

#if GTK_CHECK_VERSION (3, 0, 0)
static cairo_pattern_t *
#else
static GdkPixbuf *
#endif
composite_color_onto_desktop (PanelBackground *background)
{
#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_surface_t *surface;
	cairo_pattern_t *pattern;
	cairo_t *cr;
#else
	guint32 color;
#endif

	if (!background->desktop)
		background->desktop = get_desktop_pixbuf (background);

	if (!background->desktop)
		return NULL;

#if GTK_CHECK_VERSION (3, 0, 0)
	surface = gdk_window_create_similar_surface (background->window,
						     CAIRO_CONTENT_COLOR_ALPHA,
						     background->region.width,
						     background->region.height);
	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy (surface);
		return NULL;
	}

	cr = cairo_create (surface);
#if GTK_CHECK_VERSION (3, 0, 0)
	if(!gdk_window_check_composited_wm(background->window)){
#endif
		gdk_cairo_set_source_pixbuf (cr, background->desktop, 0, 0);
		cairo_paint (cr);
#if GTK_CHECK_VERSION(3, 0, 0)
	}
#endif
	gdk_cairo_set_source_rgba (cr, &background->color);
	cairo_paint (cr);

	cairo_destroy (cr);

	pattern = cairo_pattern_create_for_surface (surface);
	cairo_surface_destroy (surface);

	return pattern;
#else
	color = ((background->color.gdk.red & 0xff00) << 8) +
		 (background->color.gdk.green & 0xff00) +
		 (background->color.gdk.blue >> 8);

	return gdk_pixbuf_composite_color_simple (
			background->desktop, 
			gdk_pixbuf_get_width (background->desktop),
			gdk_pixbuf_get_height (background->desktop),
			GDK_INTERP_NEAREST,
			(255 - (background->color.alpha >> 8)),
			255, color, color);
#endif
}

#if GTK_CHECK_VERSION (3, 0, 0)
static cairo_pattern_t *
get_composited_pattern (PanelBackground *background)
#else
static GdkPixbuf *
get_composited_pixbuf (PanelBackground *background)
#endif
{
#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_pattern_t *retval = NULL;
#else
	GdkPixbuf *retval = NULL;
#endif

	switch (background->type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		retval = composite_color_onto_desktop (background);
		break;
	case PANEL_BACK_IMAGE:
		retval = composite_image_onto_desktop (background);
#if !GTK_CHECK_VERSION (3, 0, 0)
		if (!retval)
			retval = g_object_ref (background->transformed_image);
#endif
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
#if GTK_CHECK_VERSION (3, 0, 0)
				background->composited_pattern =
					get_composited_pattern (background);
#else
			background->composited_image =
				get_composited_pixbuf (background);
#endif
		break;
	case PANEL_BACK_IMAGE:
        if (background->transformed_image) {
#if GTK_CHECK_VERSION (3, 0, 0)
			background->composited_pattern =
				get_composited_pattern (background);
#else
			if (background->has_alpha)
				background->composited_image =
					get_composited_pixbuf (background);
			else
				background->composited_image =
					g_object_ref (background->transformed_image);
#endif
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

static void
disconnect_background_monitor (PanelBackground *background)
{
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

static void
panel_background_update_has_alpha (PanelBackground *background)
{
	gboolean has_alpha = FALSE;

	if (background->type == PANEL_BACK_COLOR)
#if GTK_CHECK_VERSION (3, 0, 0)
		has_alpha = (background->color.alpha < 1.);
#else
		has_alpha = (background->color.alpha != 0xffff);
#endif

	else if (background->type == PANEL_BACK_IMAGE &&
		 background->loaded_image)
		has_alpha = gdk_pixbuf_get_has_alpha (background->loaded_image);

	background->has_alpha = has_alpha;

	if (!has_alpha)
		disconnect_background_monitor (background);
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

#if !GTK_CHECK_VERSION (3, 0, 0)
static void
panel_background_set_gdk_color_no_update (PanelBackground *background,
					  GdkColor        *gdk_color)
{
	g_return_if_fail (gdk_color != NULL);

	background->color.gdk.red   = gdk_color->red;
	background->color.gdk.green = gdk_color->green;
	background->color.gdk.blue  = gdk_color->blue;
}

void
panel_background_set_gdk_color (PanelBackground *background,
				GdkColor        *gdk_color)
{
	g_return_if_fail (gdk_color != NULL);
	g_return_if_fail (background != NULL);

	if (background->color.gdk.red   == gdk_color->red &&
	    background->color.gdk.green == gdk_color->green &&
	    background->color.gdk.blue  == gdk_color->blue)
		return;

	free_transformed_resources (background);
	panel_background_set_gdk_color_no_update (background, gdk_color);
	panel_background_transform (background);
}
#endif

static void
panel_background_set_opacity_no_update (PanelBackground *background,
				        guint16          opacity)
{
#if GTK_CHECK_VERSION (3, 0, 0)
	background->color.alpha = opacity / 65535.0;
#else
	background->color.alpha = opacity;
#endif
	panel_background_update_has_alpha (background);
}

void
panel_background_set_opacity (PanelBackground *background,
			      guint16          opacity)
{
#if GTK_CHECK_VERSION (3, 0, 0)
	if (background->color.alpha == (opacity / 65535.0))
#else
	if (background->color.alpha == opacity)
#endif
		return;

	free_transformed_resources (background);
	panel_background_set_opacity_no_update (background, opacity);
	panel_background_transform (background);
}

static void
panel_background_set_color_no_update (PanelBackground *background,
#if GTK_CHECK_VERSION (3, 0, 0)
				      const GdkRGBA   *color)
#else
				      PanelColor      *color)
#endif
{
	g_return_if_fail (color != NULL);

#if GTK_CHECK_VERSION (3, 0, 0)
	if (gdk_rgba_equal (color, &background->color))
		return;
	background->color = *color;
	panel_background_update_has_alpha (background);
#else
	panel_background_set_gdk_color_no_update (background, &(color->gdk));
	panel_background_set_opacity_no_update (background, color->alpha);
#endif
}

void
panel_background_set_color (PanelBackground *background,
#if GTK_CHECK_VERSION (3, 0, 0)
			    const GdkRGBA   *color)
#else
			    PanelColor      *color)
#endif
{
	g_return_if_fail (color != NULL);

#if GTK_CHECK_VERSION (3, 0, 0)
	if (gdk_rgba_equal (color, &background->color))
#else
	if (background->color.gdk.red   == color->gdk.red &&
	    background->color.gdk.green == color->gdk.green &&
	    background->color.gdk.blue  == color->gdk.blue &&
	    background->color.alpha  == color->alpha)
#endif
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
#if GTK_CHECK_VERSION (3, 0, 0)
		      const GdkRGBA       *color,
#else
		      PanelColor          *color,
#endif
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
#if GTK_CHECK_VERSION (3, 0, 0)
				    GdkRGBA         *color,
				    cairo_pattern_t *pattern)
#else
				    GdkColor        *color,
				    GdkPixmap       *pixmap)
#endif
{
	g_return_if_fail (color != NULL);

	background->default_color = *color;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (pattern)
		cairo_pattern_reference (pattern);

	if (background->default_pattern)
		cairo_pattern_destroy (background->default_pattern);

	background->default_pattern = pattern;
#else
	if (pixmap && pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
		g_object_ref (pixmap);

	if (background->default_pixmap
	    && background->default_pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
		g_object_unref (background->default_pixmap);

	background->default_pixmap = pixmap;
#endif
	if (background->type == PANEL_BACK_NONE)
		panel_background_prepare (background);
}


void
panel_background_realized (PanelBackground *background,
			   GdkWindow       *window)
{
	g_return_if_fail (window != NULL);

#if GTK_CHECK_VERSION (3, 0, 0)
	if (background->window)
#else
	if (background->window && background->colormap && background->gc)
#endif
		return;

#if GTK_CHECK_VERSION (3, 0, 0)
	background->window = g_object_ref (window);
#else
	if (!background->window)
		background->window = g_object_ref (window);

	if (!background->colormap)
		background->colormap =
			g_object_ref (gdk_drawable_get_colormap (window));

	if (!background->gc)
		background->gc = gdk_gc_new (window);
#endif
#if GTK_CHECK_VERSION(3, 0, 0)
	panel_background_prepare_css ();
#endif
	panel_background_prepare (background);
}

void
panel_background_unrealized (PanelBackground *background)
{
	free_prepared_resources (background);

	if (background->window)
		g_object_unref (background->window);
	background->window = NULL;

#if !GTK_CHECK_VERSION (3, 0, 0)
	if (background->colormap)
		g_object_unref (background->colormap);
	background->colormap = NULL;

	if (background->gc)
		g_object_unref (background->gc);
	background->gc = NULL;
#endif
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

	if (background->desktop)
		g_object_unref (background->desktop);
	background->desktop = NULL;

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

#if GTK_CHECK_VERSION (3, 0, 0)
	background->color.red = 0.;
	background->color.blue = 0.;
	background->color.green = 0.;
	background->color.alpha = 1.;
#else
	background->color.gdk.red   = 0;
	background->color.gdk.blue  = 0;
	background->color.gdk.green = 0;
	background->color.gdk.pixel = 0;
	background->color.alpha     = 0xffff;
#endif

	background->image        = NULL;
	background->loaded_image = NULL;

	background->orientation       = GTK_ORIENTATION_HORIZONTAL;
	background->region.x          = -1;
	background->region.y          = -1;
	background->region.width      = -1;
	background->region.height     = -1;
	background->transformed_image = NULL;
#if GTK_CHECK_VERSION (3, 0, 0)
	background->composited_pattern = NULL;
#else
	background->composited_image  = NULL;
#endif

	background->monitor        = NULL;
	background->desktop        = NULL;
	background->monitor_signal = -1;

	background->window   = NULL;
#if !GTK_CHECK_VERSION (3, 0, 0)
	background->pixmap   = NULL;
	background->colormap = NULL;
	background->gc       = NULL;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
	background->default_pattern     = NULL;
#else
	background->default_pixmap      = NULL;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
	background->default_color.red   = 0.;
	background->default_color.green = 0.;
	background->default_color.blue  = 0.;
	background->default_color.alpha = 1.;
#else
	background->default_color.red   = 0;
	background->default_color.green = 0;
	background->default_color.blue  = 0;
	background->default_color.pixel = 0;
#endif

	background->fit_image     = FALSE;
	background->stretch_image = FALSE;
	background->rotate_image  = FALSE;

	background->has_alpha = FALSE;

	background->transformed = FALSE;
	background->composited  = FALSE;
	background->prepared    = FALSE;
}

void
panel_background_free (PanelBackground *background)
{
	disconnect_background_monitor (background);

	free_transformed_resources (background);

	if (background->image)
		g_free (background->image);
	background->image = NULL;

	if (background->loaded_image)
		g_object_unref (background->loaded_image);
	background->loaded_image = NULL;

	if (background->monitor)
		g_object_unref (background->monitor);
	background->monitor = NULL;

	if (background->window)
		g_object_unref (background->window);
	background->window = NULL;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (background->default_pattern)
		cairo_pattern_destroy (background->default_pattern);
	background->default_pattern = NULL;
#else
	if (background->colormap)
		g_object_unref (background->colormap);
	background->colormap = NULL;

	if (background->gc)
		g_object_unref (background->gc);
	background->gc = NULL;

	if (background->default_pixmap
	    && background->default_pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
		g_object_unref (background->default_pixmap);
	background->default_pixmap = NULL;
#endif
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

	if (effective_type == PANEL_BACK_IMAGE ||
	    (effective_type == PANEL_BACK_COLOR && background->has_alpha
#if GTK_CHECK_VERSION (3, 0, 0)
	    && (!gdk_window_check_composited_wm(background->window)))) {
		cairo_surface_t *surface;

		if (!background->composited_pattern)
			return NULL;
#else
	    )) {
		GdkNativeWindow pixmap_xid;

		if (!background->pixmap)
			return NULL;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
		if (cairo_pattern_get_surface (background->composited_pattern, &surface))
			return NULL;

		if (cairo_surface_get_type (surface) != CAIRO_SURFACE_TYPE_XLIB)
			return NULL;

		retval = g_strdup_printf ("pixmap:%d,%d,%d", (guint32)cairo_xlib_surface_get_drawable (surface), x, y);
#else
		pixmap_xid = GDK_WINDOW_XID (GDK_DRAWABLE (background->pixmap));

		retval = g_strdup_printf ("pixmap:%d,%d,%d", pixmap_xid, x, y);
#endif
	} else if (effective_type == PANEL_BACK_COLOR) {
#if GTK_CHECK_VERSION (3, 0, 0)
		gchar *rgba = gdk_rgba_to_string (&background->color);
		retval = g_strdup_printf (
				"color:%s",
				rgba);
		g_free (rgba);
#else
		retval = g_strdup_printf (
				"color:%.4x%.4x%.4x",
				background->color.gdk.red,
				background->color.gdk.green,
				background->color.gdk.blue);
#endif
	} else
		retval = g_strdup ("none:");

	return retval;
}

PanelBackgroundType
panel_background_get_type (PanelBackground *background)
{
	return background->type;
}

#if GTK_CHECK_VERSION (3, 0, 0)
const GdkRGBA *
#else
const PanelColor *
#endif
panel_background_get_color (PanelBackground *background)
{
	return &(background->color);
}

#if !GTK_CHECK_VERSION (3, 0, 0)
const GdkPixmap *
panel_background_get_pixmap (PanelBackground *background)
{
	return background->pixmap;
}
#endif


/* What are we actually rendering - e.g. if we're supposed to
 * be rendering an image, but haven't got a valid image, then
 * we're rendering the default gtk background.
 */
PanelBackgroundType
panel_background_effective_type (PanelBackground *background)
{
	PanelBackgroundType retval;

	retval = background->type;
#if GTK_CHECK_VERSION (3, 0, 0)
	if (background->type == PANEL_BACK_IMAGE && !background->composited_pattern)
#else
	if (background->type == PANEL_BACK_IMAGE && !background->composited_image)
#endif
		retval = PANEL_BACK_NONE;

	return retval;
}

#if !GTK_CHECK_VERSION (3, 0, 0)
static void
panel_background_set_no_background_on_widget (PanelBackground *background,
					      GtkWidget       *widget)
{
	GtkRcStyle *rc_style;

	gtk_widget_set_style (widget, NULL);
	rc_style = gtk_rc_style_new ();
	gtk_widget_modify_style (widget, rc_style);
	g_object_unref (rc_style);
}
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
static cairo_pattern_t *
panel_background_get_pattern_for_widget (PanelBackground *background,
					 GtkWidget       *widget)
#else
static void
panel_background_set_image_background_on_widget (PanelBackground *background,

						 GtkWidget       *widget)
#endif
{
	GtkAllocation    allocation;
	cairo_t         *cr;
	cairo_pattern_t *pattern;
#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_surface_t *surface;
	cairo_surface_t *bg_surface;
	cairo_matrix_t   matrix;
#else
	const GdkPixmap *bg_pixmap;
	GdkPixmap       *pixmap;
	GtkStyle        *style;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
	if (!background->composited_pattern)
		return NULL;

	if (cairo_pattern_get_surface (background->composited_pattern, &bg_surface) != CAIRO_STATUS_SUCCESS)
		return NULL;

	gtk_widget_get_allocation (widget, &allocation);
	surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					      allocation.width, allocation.height);

	cr = cairo_create (surface);
	cairo_set_source_surface (cr, bg_surface, -allocation.x, -allocation.y);
	cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
	cairo_fill (cr);
	cairo_destroy (cr);

	pattern = cairo_pattern_create_for_surface (surface);
	cairo_matrix_init_translate (&matrix, 0, 0);
	cairo_matrix_scale (&matrix, allocation.width, allocation.height);
	cairo_pattern_set_matrix (pattern, &matrix);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_PAD);

	cairo_surface_destroy (surface);

	return pattern;
#else
	bg_pixmap = panel_background_get_pixmap (background);
	if (!bg_pixmap)
		return;

	gtk_widget_get_allocation (widget, &allocation);
	pixmap = gdk_pixmap_new (gtk_widget_get_window (widget),
				 allocation.width,
				 allocation.height,
				 -1);

	cr = gdk_cairo_create (GDK_DRAWABLE (pixmap));
	gdk_cairo_set_source_pixmap (cr, (GdkPixmap *) bg_pixmap,
				     -allocation.x,
				     -allocation.y);
	pattern = cairo_get_source (cr);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);

	cairo_rectangle (cr, 0, 0,
			 allocation.width, allocation.height);
	cairo_fill (cr);

	cairo_destroy (cr);

	style = gtk_style_copy (gtk_widget_get_style (widget));
	if (style->bg_pixmap[GTK_STATE_NORMAL])
		g_object_unref (style->bg_pixmap[GTK_STATE_NORMAL]);
	style->bg_pixmap[GTK_STATE_NORMAL] = g_object_ref (pixmap);
	gtk_widget_set_style (widget, style);
	g_object_unref (style);

	g_object_unref (pixmap);
#endif
}

#if !GTK_CHECK_VERSION (3, 0, 0)
static void
panel_background_set_color_background_on_widget (PanelBackground *background,
						 GtkWidget       *widget)
{
	const PanelColor *color;

	color = panel_background_get_color (background);
	if (color->alpha != 0xffff) {
		panel_background_set_image_background_on_widget (background,
								 widget);
		return;
	}

	gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, &color->gdk);
}

void
panel_background_change_background_on_widget (PanelBackground *background,
					      GtkWidget       *widget)
{
	PanelBackgroundType type;

	panel_background_set_no_background_on_widget (background, widget);

	type = panel_background_get_type (background);

	switch (type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		panel_background_set_color_background_on_widget (background,
								 widget);
		break;
	case PANEL_BACK_IMAGE:
		panel_background_set_image_background_on_widget (background,
								 widget);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}
#endif

