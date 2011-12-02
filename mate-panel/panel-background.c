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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "panel-background.h"

#include <string.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <cairo.h>

#include "panel-background-monitor.h"
#include "panel-util.h"


static gboolean panel_background_composite (PanelBackground *background);
static void load_background_file (PanelBackground *background);


static void
free_prepared_resources (PanelBackground *background)
{
	background->prepared = FALSE;

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
}

static void
set_pixbuf_background (PanelBackground *background)
{
	g_assert (background->composited_image != NULL);

	gdk_pixbuf_render_pixmap_and_mask_for_colormap (
		background->composited_image,
		background->colormap,
		&background->pixmap, NULL, 128);

	gdk_window_set_back_pixmap (
		background->window, background->pixmap, FALSE);
}

static gboolean
panel_background_prepare (PanelBackground *background)
{
	PanelBackgroundType  effective_type;
	GtkWidget           *widget = NULL;

	if (!background->colormap || !background->transformed)
		return FALSE;

	free_prepared_resources (background);

	effective_type = panel_background_effective_type (background);

	switch (effective_type) {
	case PANEL_BACK_NONE:
		if (background->default_pixmap) {
			if (background->default_pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
				gdk_window_set_back_pixmap (background->window,
							    background->default_pixmap,
							    FALSE);
			else
				gdk_window_set_back_pixmap (background->window,
							    NULL,
							    TRUE);
		} else
			gdk_window_set_background (
				background->window, &background->default_color);
		break;
	case PANEL_BACK_COLOR:
		if (background->has_alpha &&
		    background->composited_image)
			set_pixbuf_background (background);
		else {
			gdk_colormap_alloc_color (
				background->colormap,
				&background->color.gdk,
				FALSE, TRUE);
			gdk_window_set_background (
				background->window, &background->color.gdk);
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
	gdk_display_sync (gdk_drawable_get_display (background->window));

	gdk_window_get_user_data (GDK_WINDOW (background->window),
				  (gpointer) &widget);

	if (GTK_IS_WIDGET (widget))
	  gtk_widget_queue_draw (widget);

	background->prepared = TRUE;

	background->notify_changed (background, background->user_data);

	return TRUE;
}

static void
free_composited_resources (PanelBackground *background)
{
	free_prepared_resources (background);

	background->composited = FALSE;

	if (background->composited_image)
		g_object_unref (background->composited_image);
	background->composited_image = NULL;
}

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
				gdk_drawable_get_screen (background->window));

		background->monitor_signal =
			g_signal_connect (
			background->monitor, "changed",
                        G_CALLBACK (background_changed), background);
	}

	desktop = panel_background_monitor_get_region (
				background->monitor,
				background->region.x,
				background->region.y,
				background->region.width,
				background->region.height);

	return desktop;
}

static GdkPixbuf *
composite_image_onto_desktop (PanelBackground *background)
{
	GdkPixbuf       *retval;
	int              width, height;
	unsigned char   *data;
	cairo_t         *cr;
	cairo_surface_t *surface;
	cairo_pattern_t *pattern;

	if (!background->desktop)
		background->desktop = get_desktop_pixbuf (background);

	if (!background->desktop)
		return NULL;

	width  = gdk_pixbuf_get_width  (background->desktop);
	height = gdk_pixbuf_get_height (background->desktop);

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

	gdk_cairo_set_source_pixbuf (cr, background->desktop, 0, 0);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	gdk_cairo_set_source_pixbuf (cr, background->transformed_image, 0, 0);
	pattern = cairo_get_source (cr);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	cairo_destroy (cr);
	cairo_surface_destroy (surface);

	retval = panel_util_cairo_rgbdata_to_pixbuf (data, width, height);

	g_free (data);

	return retval;
}

static GdkPixbuf *
composite_color_onto_desktop (PanelBackground *background)
{
	guint32 color;

	if (!background->desktop)
		background->desktop = get_desktop_pixbuf (background);

	if (!background->desktop)
		return NULL;

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
}

static GdkPixbuf *
get_composited_pixbuf (PanelBackground *background)
{
	GdkPixbuf *retval = NULL;

	switch (background->type) {
	case PANEL_BACK_NONE:
		break;
	case PANEL_BACK_COLOR:
		retval = composite_color_onto_desktop (background);
		break;
	case PANEL_BACK_IMAGE:
		retval = composite_image_onto_desktop (background);
		if (!retval)
			retval = g_object_ref (background->transformed_image);
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
			background->composited_image =
				get_composited_pixbuf (background);
		break;
	case PANEL_BACK_IMAGE:
		if (background->transformed_image) {
			if (background->has_alpha)
				background->composited_image =
					get_composited_pixbuf (background);
			else
				background->composited_image =
					g_object_ref (background->transformed_image);
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
		has_alpha = (background->color.alpha != 0xffff);

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

	if (background->color.gdk.red   == gdk_color->red &&
	    background->color.gdk.green == gdk_color->green &&
	    background->color.gdk.blue  == gdk_color->blue)
		return;

	free_transformed_resources (background);
	panel_background_set_gdk_color_no_update (background, gdk_color);
	panel_background_transform (background);
}

static void
panel_background_set_opacity_no_update (PanelBackground *background,
				        guint16          opacity)
{
	background->color.alpha = opacity;
	panel_background_update_has_alpha (background);
}

void
panel_background_set_opacity (PanelBackground *background,
			      guint16          opacity)
{
	if (background->color.alpha == opacity)
		return;

	free_transformed_resources (background);
	panel_background_set_opacity_no_update (background, opacity);
	panel_background_transform (background);
}

static void
panel_background_set_color_no_update (PanelBackground *background,
				      PanelColor      *color)
{
	g_return_if_fail (color != NULL);

	panel_background_set_gdk_color_no_update (background, &(color->gdk));
	panel_background_set_opacity_no_update (background, color->alpha);
}

void
panel_background_set_color (PanelBackground *background,
			    PanelColor      *color)
{
	g_return_if_fail (color != NULL);

	if (background->color.gdk.red   == color->gdk.red &&
	    background->color.gdk.green == color->gdk.green &&
	    background->color.gdk.blue  == color->gdk.blue &&
	    background->color.alpha  == color->alpha)
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
		      PanelColor          *color,
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
				    GdkColor        *color,
				    GdkPixmap       *pixmap)
{
	g_return_if_fail (color != NULL);

	background->default_color = *color;

	if (pixmap && pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
		g_object_ref (pixmap);

	if (background->default_pixmap
	    && background->default_pixmap != (GdkPixmap*) GDK_PARENT_RELATIVE)
		g_object_unref (background->default_pixmap);

	background->default_pixmap = pixmap;

	if (background->type == PANEL_BACK_NONE)
		panel_background_prepare (background);
}

void
panel_background_realized (PanelBackground *background,
			   GdkWindow       *window)
{
	g_return_if_fail (window != NULL);

	if (background->window && background->colormap && background->gc)
		return;

	if (!background->window)
		background->window = g_object_ref (window);

	if (!background->colormap)
		background->colormap =
			g_object_ref (gdk_drawable_get_colormap (window));

	if (!background->gc)
		background->gc = gdk_gc_new (window);

	panel_background_prepare (background);
}

void
panel_background_unrealized (PanelBackground *background)
{
	free_prepared_resources (background);

	if (background->window)
		g_object_unref (background->window);
	background->window = NULL;

	if (background->colormap)
		g_object_unref (background->colormap);
	background->colormap = NULL;

	if (background->gc)
		g_object_unref (background->gc);
	background->gc = NULL;
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

	background->color.gdk.red   = 0;
	background->color.gdk.blue  = 0;
	background->color.gdk.green = 0;
	background->color.gdk.pixel = 0;
	background->color.alpha     = 0xffff;

	background->image        = NULL;
	background->loaded_image = NULL;

	background->orientation       = GTK_ORIENTATION_HORIZONTAL;
	background->region.x          = -1;
	background->region.y          = -1;
	background->region.width      = -1;
	background->region.height     = -1;
	background->transformed_image = NULL;
	background->composited_image  = NULL;

	background->monitor        = NULL;
	background->desktop        = NULL;
	background->monitor_signal = -1;

	background->pixmap   = NULL;
	background->window   = NULL;
	background->colormap = NULL;
	background->gc       = NULL;

	background->default_pixmap      = NULL;
	background->default_color.red   = 0;
	background->default_color.green = 0;
	background->default_color.blue  = 0;
	background->default_color.pixel = 0;

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
	    (effective_type == PANEL_BACK_COLOR && background->has_alpha)) {
		GdkNativeWindow pixmap_xid;

		if (!background->pixmap)
			return NULL;

		pixmap_xid = gdk_x11_drawable_get_xid (
				GDK_DRAWABLE (background->pixmap));

		retval = g_strdup_printf ("pixmap:%d,%d,%d", pixmap_xid, x, y);

	} else if (effective_type == PANEL_BACK_COLOR)
		retval = g_strdup_printf (
				"color:%.4x%.4x%.4x",
				background->color.gdk.red,
				background->color.gdk.green,
				background->color.gdk.blue);
	else
		retval = g_strdup ("none:");

        return retval;

}

PanelBackgroundType
panel_background_get_type (PanelBackground *background)
{
	return background->type;
}

const PanelColor *
panel_background_get_color (PanelBackground *background)
{
	return &(background->color);
}

const GdkPixmap *
panel_background_get_pixmap (PanelBackground *background)
{
	return background->pixmap;
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
	if (background->type == PANEL_BACK_IMAGE && !background->composited_image)
		retval = PANEL_BACK_NONE;

	return retval;
}

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

static void
panel_background_set_image_background_on_widget (PanelBackground *background,
						 GtkWidget       *widget)
{
	const GdkPixmap *bg_pixmap;
	GtkAllocation    allocation;
	GdkPixmap       *pixmap;
	cairo_t         *cr;
	cairo_pattern_t *pattern;
	GtkStyle        *style;

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
}

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
