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

	gtk_show_uri_on_window (NULL, uri,
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

GdkPixbuf *
clock_utils_pixbuf_from_svg_resource_at_size (const char *resource,
	                                          int         width,
	                                          int         height)
{
	GInputStream      *stream = NULL;
	RsvgHandle        *handle = NULL;
	RsvgDimensionData  svg_dimensions;
	GdkPixbuf         *pixbuf = NULL;
	cairo_surface_t   *surface = NULL;
	cairo_matrix_t     matrix;
	cairo_t           *cr = NULL;

	stream = g_resources_open_stream (resource, 0, NULL);
	if (!stream)
		goto out;

	handle = rsvg_handle_new ();
	if (!handle)
		goto out;

	if (!rsvg_handle_read_stream_sync (handle, stream, NULL, NULL))
		goto out;

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

out:
	if (handle) {
		rsvg_handle_close (handle, NULL);
		rsvg_handle_free (handle);
	}
	if (stream)
		g_object_unref (stream);

	return pixbuf;
}

