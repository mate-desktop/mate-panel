/*
 * panel-gtk.c: various small extensions to gtk+
 *
 * Copyright (C) 2010 Novell, Inc.
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
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <gtk/gtk.h>

#include "panel-gtk.h"

/*
 * Originally based on code from panel-properties-dialog.c. This part of the
 * code was:
 * Copyright (C) 2005 Vincent Untz <vuntz@gnome.org>
 */

static void
panel_gtk_file_chooser_preview_update (GtkFileChooser *chooser,
				       gpointer data)
{
	GtkWidget *preview;
	char      *filename;
	GdkPixbuf *pixbuf;
	gboolean   have_preview;

	preview = GTK_WIDGET (data);
	filename = gtk_file_chooser_get_preview_filename (chooser);

	if (filename == NULL)
		return;

	pixbuf = gdk_pixbuf_new_from_file_at_size (filename, 128, 128, NULL);
	have_preview = (pixbuf != NULL);
	g_free (filename);

	gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
	if (pixbuf)
		g_object_unref (pixbuf);

	gtk_file_chooser_set_preview_widget_active (chooser,
						    have_preview);
}

void
panel_gtk_file_chooser_add_image_preview (GtkFileChooser *chooser)
{
	GtkFileFilter *filter;
	GtkWidget     *chooser_preview;

	g_return_if_fail (GTK_IS_FILE_CHOOSER (chooser));

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_set_filter (chooser, filter);

	chooser_preview = gtk_image_new ();
	gtk_file_chooser_set_preview_widget (chooser, chooser_preview);
	g_signal_connect (chooser, "update-preview",
			  G_CALLBACK (panel_gtk_file_chooser_preview_update),
			  chooser_preview);
}

/*
 * End of code coming from panel-properties-dialog.c
 */
