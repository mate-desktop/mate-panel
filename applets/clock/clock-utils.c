/*
 * clock-utils.c
 *
 * Copyright (C) 2007 Vincent Untz <vuntz@gnome.org>
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
 *      Vincent Untz <vuntz@gnome.org>
 *
 * Most of the original code comes from clock.c
 */

#include "config.h"

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <librsvg/rsvg.h>

#include "clock.h"

#include "clock-utils.h"

gboolean
clock_locale_supports_am_pm (void)
{
#ifdef HAVE_NL_LANGINFO
        const char *am;

        am = nl_langinfo (AM_STR);
        return (am[0] != '\0');
#else
	return TRUE;
#endif
}

ClockFormat
clock_locale_format (void)
{
	return clock_locale_supports_am_pm () ?
		CLOCK_FORMAT_12 : CLOCK_FORMAT_24;
}

void
clock_utils_display_help (GtkWidget  *widget,
			  const char *doc_id,
			  const char *link_id)
{
	GError *error = NULL;
	char   *uri;

	if (link_id)
		uri = g_strdup_printf ("help:%s/%s", doc_id, link_id);
	else
		uri = g_strdup_printf ("help:%s", doc_id);

	gtk_show_uri (gtk_widget_get_screen (widget), uri,
		      gtk_get_current_event_time (), &error);

	g_free (uri);

	if (error &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		g_error_free (error);
	else if (error) {
		GtkWidget *parent;
		GtkWidget *dialog;
		char      *primary;

		if (GTK_IS_WINDOW (widget))
			parent = widget;
		else
			parent = NULL;

		primary = g_markup_printf_escaped (
				_("Could not display help document '%s'"),
				doc_id);
		dialog = gtk_message_dialog_new (
				parent ? GTK_WINDOW (parent) : NULL,
				GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"%s", primary);

		gtk_message_dialog_format_secondary_text (
					GTK_MESSAGE_DIALOG (dialog),
					"%s", error->message);

		g_error_free (error);
		g_free (primary);

		g_signal_connect (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);

		gtk_window_set_icon_name (GTK_WINDOW (dialog), CLOCK_ICON);
		gtk_window_set_screen (GTK_WINDOW (dialog),
				       gtk_widget_get_screen (widget));

		if (parent == NULL) {
			/* we have no parent window */
			gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog),
							  FALSE);
			gtk_window_set_title (GTK_WINDOW (dialog),
					      _("Error displaying help document"));
		}

		gtk_widget_show (dialog);
	}
}

#if !GTK_CHECK_VERSION (3, 0, 0)
/* code copied from GTK3 gdkpixbuf-drawable.c */
static cairo_format_t
gdk_cairo_format_for_content (cairo_content_t content)
{
  switch (content)
    {
    case CAIRO_CONTENT_COLOR:
      return CAIRO_FORMAT_RGB24;
    case CAIRO_CONTENT_ALPHA:
      return CAIRO_FORMAT_A8;
    case CAIRO_CONTENT_COLOR_ALPHA:
    default:
      return CAIRO_FORMAT_ARGB32;
    }
}

static cairo_surface_t *
gdk_cairo_surface_coerce_to_image (cairo_surface_t *surface,
                                   cairo_content_t  content,
                                   int              src_x,
                                   int              src_y,
                                   int              width,
                                   int              height)
{
  cairo_surface_t *copy;
  cairo_t *cr;

  copy = cairo_image_surface_create (gdk_cairo_format_for_content (content),
                                     width,
                                     height);

  cr = cairo_create (copy);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface (cr, surface, -src_x, -src_y);
  cairo_paint (cr);
  cairo_destroy (cr);

  return copy;
}

static void
convert_alpha (guchar *dest_data,
               int     dest_stride,
               guchar *src_data,
               int     src_stride,
               int     src_x,
               int     src_y,
               int     width,
               int     height)
{
  int x, y;

  src_data += src_stride * src_y + src_x * 4;

  for (y = 0; y < height; y++) {
    guint32 *src = (guint32 *) src_data;

    for (x = 0; x < width; x++) {
      guint alpha = src[x] >> 24;

      if (alpha == 0)
        {
          dest_data[x * 4 + 0] = 0;
          dest_data[x * 4 + 1] = 0;
          dest_data[x * 4 + 2] = 0;
        }
      else
        {
          dest_data[x * 4 + 0] = (((src[x] & 0xff0000) >> 16) * 255 + alpha / 2) / alpha;
          dest_data[x * 4 + 1] = (((src[x] & 0x00ff00) >>  8) * 255 + alpha / 2) / alpha;
          dest_data[x * 4 + 2] = (((src[x] & 0x0000ff) >>  0) * 255 + alpha / 2) / alpha;
        }
      dest_data[x * 4 + 3] = alpha;
    }

    src_data += src_stride;
    dest_data += dest_stride;
  }
}

static void
convert_no_alpha (guchar *dest_data,
                  int     dest_stride,
                  guchar *src_data,
                  int     src_stride,
                  int     src_x,
                  int     src_y,
                  int     width,
                  int     height)
{
  int x, y;

  src_data += src_stride * src_y + src_x * 4;

  for (y = 0; y < height; y++) {
    guint32 *src = (guint32 *) src_data;

    for (x = 0; x < width; x++) {
      dest_data[x * 3 + 0] = src[x] >> 16;
      dest_data[x * 3 + 1] = src[x] >>  8;
      dest_data[x * 3 + 2] = src[x];
    }

    src_data += src_stride;
    dest_data += dest_stride;
  }
}

static GdkPixbuf *
gdk_pixbuf_get_from_surface  (cairo_surface_t *surface,
                              gint             src_x,
                              gint             src_y,
                              gint             width,
                              gint             height)
{
  cairo_content_t content;
  GdkPixbuf *dest;

  /* General sanity checks */
  g_return_val_if_fail (surface != NULL, NULL);
  g_return_val_if_fail (width > 0 && height > 0, NULL);

  content = cairo_surface_get_content (surface) | CAIRO_CONTENT_COLOR;
  dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                         !!(content & CAIRO_CONTENT_ALPHA),
                         8,
                         width, height);

  if (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_IMAGE &&
      cairo_image_surface_get_format (surface) == gdk_cairo_format_for_content (content))
    surface = cairo_surface_reference (surface);
  else
    {
      surface = gdk_cairo_surface_coerce_to_image (surface, content,
						   src_x, src_y,
						   width, height);
      src_x = 0;
      src_y = 0;
    }
  cairo_surface_flush (surface);
  if (cairo_surface_status (surface) || dest == NULL)
    {
      cairo_surface_destroy (surface);
      return NULL;
    }

  if (gdk_pixbuf_get_has_alpha (dest))
    convert_alpha (gdk_pixbuf_get_pixels (dest),
                   gdk_pixbuf_get_rowstride (dest),
                   cairo_image_surface_get_data (surface),
                   cairo_image_surface_get_stride (surface),
                   src_x, src_y,
                   width, height);
  else
    convert_no_alpha (gdk_pixbuf_get_pixels (dest),
                      gdk_pixbuf_get_rowstride (dest),
                      cairo_image_surface_get_data (surface),
                      cairo_image_surface_get_stride (surface),
                      src_x, src_y,
                      width, height);

  cairo_surface_destroy (surface);
  return dest;
}
#endif

GdkPixbuf *
clock_utils_pixbuf_from_svg_file_at_size (const char *name, int width, int height)
{
	RsvgHandle        *handle = NULL;
	RsvgDimensionData  svg_dimensions;
	GdkPixbuf         *pixbuf = NULL;
	cairo_surface_t   *surface = NULL;
	cairo_matrix_t     matrix;
	cairo_t           *cr = NULL;

	handle = rsvg_handle_new_from_file (name, NULL);
	if (!handle)
		return NULL;

	rsvg_handle_get_dimensions (handle, &svg_dimensions);

	surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
	cr = cairo_create (surface);
	cairo_matrix_init_scale (&matrix,
				 ((double) width / svg_dimensions.width),
				 ((double) height / svg_dimensions.height));
	cairo_transform (cr, &matrix);
	rsvg_handle_render_cairo (handle, cr);
	cairo_destroy (cr);

	pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, width, height);
	cairo_surface_destroy (surface);

	rsvg_handle_close (handle, NULL);

	return pixbuf;
}

