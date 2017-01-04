/*
 * panel-error.c: an easy-to-use error dialog
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Originally based on code from panel-util.c (there was no relevant copyright
 * header at the time).
 *
 * Originally based on code from panel-util.c (there was no relevant copyright
 * header at the time), but the code was:
 * Copyright (C) Novell, Inc. (for the panel_g_utf8_strstrcase() code)
 * Copyright (C) Dennis Cranston (for the panel_g_lookup_in_data_dirs() code)
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
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include "panel-error.h"

GtkWidget *
panel_error_dialog (GtkWindow  *parent,
		    GdkScreen  *screen,
		    const char *dialog_class,
		    gboolean    auto_destroy,
		    const char *primary_text,
		    const char *secondary_text)
{
	GtkWidget *dialog;
	char      *freeme;

	freeme = NULL;

	if (primary_text == NULL) {
		g_warning ("NULL dialog");
		 /* No need to translate this, this should NEVER happen */
		freeme = g_strdup_printf ("Error with displaying error "
					  "for dialog of class %s",
					  dialog_class);
		primary_text = freeme;
	}

	dialog = gtk_message_dialog_new (parent, 0, GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE, "%s", primary_text);
	if (secondary_text != NULL)
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", secondary_text);

	if (screen)
		gtk_window_set_screen (GTK_WINDOW (dialog), screen);

	if (!parent) {
		gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);
		/* FIXME: We need a title in this case, but we don't know what
		 * the format should be. Let's put something simple until
		 * the following bug gets fixed:
		 * http://bugzilla.gnome.org/show_bug.cgi?id=165132 */
		gtk_window_set_title (GTK_WINDOW (dialog), _("Error"));
	}

	gtk_widget_show_all (dialog);

	if (auto_destroy)
		g_signal_connect_swapped (G_OBJECT (dialog), "response",
					  G_CALLBACK (gtk_widget_destroy),
					  G_OBJECT (dialog));

	if (freeme)
		g_free (freeme);

	return dialog;
}
