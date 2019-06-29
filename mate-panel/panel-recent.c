/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * panel-recent.c
 *
 * Copyright (C) 2002 James Willcox <jwillcox@gnome.org>
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
 * 	James Willcox <jwillcox@gnome.org>
 */

#include <config.h>

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libpanel-util/panel-error.h>
#include <libpanel-util/panel-show.h>
#include <libpanel-util/panel-gtk.h>

#include "menu.h"
#include "panel-util.h"
#include "panel-globals.h"
#include "panel-recent.h"
#include "panel-stock-icons.h"
#include "panel-icon-names.h"

static gboolean
show_uri (const char *uri, const char *mime_type, GdkScreen *screen,
	  GError **error)
{
	return panel_show_uri_force_mime_type (screen, uri, mime_type,
					       gtk_get_current_event_time (),
					       error);
}


static void
recent_documents_activate_cb (GtkRecentChooser *chooser,
			      gpointer          data)
{
	GtkRecentInfo *recent_info;
	const char    *uri;
	const char    *mime_type;
	GdkScreen     *screen;
	GError        *error = NULL;

	screen = gtk_widget_get_screen (GTK_WIDGET (chooser));

	recent_info = gtk_recent_chooser_get_current_item (chooser);
	uri = gtk_recent_info_get_uri (recent_info);
	mime_type = gtk_recent_info_get_mime_type (recent_info);
	//FIXME gtk_recent_info_get_application_info() could be useful

	if (show_uri (uri, mime_type, screen, &error) != TRUE) {
		char *uri_utf8;

		uri_utf8 = g_filename_to_utf8 (uri, -1, NULL, NULL, NULL);
		//FIXME this could fail... Maybe we want gtk_recent_info_get_display_name()

		if (error) {
			char *primary;
			primary = g_strdup_printf (_("Could not open recently used document \"%s\""),
						   uri_utf8);
			panel_error_dialog (NULL, screen,
					    "cannot_open_recent_doc", TRUE,
					    primary, error->message);
			g_free (primary);
			g_error_free (error);
		} else {
			char *primary;
			char *secondary;
			primary = g_strdup_printf (_("Could not open recently used document \"%s\""),
						   uri_utf8);
			secondary = g_strdup_printf (_("An unknown error occurred while trying to open \"%s\"."),
						     uri_utf8);
			panel_error_dialog (NULL, screen,
					    "cannot_open_recent_doc", TRUE,
					    primary, secondary);
			g_free (primary);
			g_free (secondary);
		}

		g_free (uri_utf8);
	}

	/* we can unref it only after having used the data we fetched from it */
	gtk_recent_info_unref (recent_info);
}

static void
panel_recent_manager_changed_cb (GtkRecentManager *manager,
				 GtkWidget        *menu_item)
{
	int size;

	g_object_get (manager, "size", &size, NULL);

	gtk_widget_set_sensitive (menu_item, size > 0);
}

static GtkWidget *clear_recent_dialog = NULL;

static void
clear_dialog_response (GtkWidget        *widget,
		       int               response,
		       GtkRecentManager *manager)
{
        if (response == GTK_RESPONSE_ACCEPT)
		gtk_recent_manager_purge_items (manager, NULL);

	gtk_widget_destroy (widget);
}

static void
recent_documents_clear_cb (GtkMenuItem      *menuitem,
                           GtkRecentManager *manager)
{
	gpointer tmp;

	if (clear_recent_dialog != NULL) {
		gtk_window_set_screen (GTK_WINDOW (clear_recent_dialog),
				       gtk_widget_get_screen (GTK_WIDGET (menuitem)));
		gtk_window_present (GTK_WINDOW (clear_recent_dialog));
		return;
	}

	clear_recent_dialog = gtk_message_dialog_new (NULL,
						      0 /* flags */,
						      GTK_MESSAGE_WARNING,
						      GTK_BUTTONS_NONE,
						      _("Clear the Recent Documents list?"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (clear_recent_dialog),
						  _("If you clear the Recent Documents list, you clear the following:\n"
						    "\342\200\242 All items from the Places \342\206\222 Recent Documents menu item.\n"
						    "\342\200\242 All items from the recent documents list in all applications."));

	panel_dialog_add_button (GTK_DIALOG (clear_recent_dialog),
				 _("_Cancel"), "process-stop",
				 GTK_RESPONSE_CANCEL);

	gtk_dialog_add_button (GTK_DIALOG (clear_recent_dialog),
			       PANEL_STOCK_CLEAR,
			       GTK_RESPONSE_ACCEPT);

	gtk_container_set_border_width (GTK_CONTAINER (clear_recent_dialog), 6);

	gtk_window_set_title (GTK_WINDOW (clear_recent_dialog),
			      _("Clear Recent Documents"));

	gtk_dialog_set_default_response (GTK_DIALOG (clear_recent_dialog),
					 GTK_RESPONSE_ACCEPT);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (clear_recent_dialog),
					  FALSE);

	g_signal_connect (clear_recent_dialog, "response",
			  G_CALLBACK (clear_dialog_response), manager);

	g_signal_connect (clear_recent_dialog, "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
			  &clear_recent_dialog);

	tmp = &clear_recent_dialog;
	g_object_add_weak_pointer (G_OBJECT (clear_recent_dialog), tmp);

	gtk_window_set_screen (GTK_WINDOW (clear_recent_dialog),
			       gtk_widget_get_screen (GTK_WIDGET (menuitem)));
	gtk_widget_show (clear_recent_dialog);
}

void
panel_recent_append_documents_menu (GtkWidget        *top_menu,
				    GtkRecentManager *manager)
{
	GtkWidget      *recent_menu;
	GtkWidget      *menu_item;
	int             size;

	menu_item = gtk_image_menu_item_new ();
	setup_menuitem_with_icon (menu_item,
				  panel_menu_icon_get_size (),
				  NULL,
				  PANEL_ICON_RECENT,
				  _("Recent Documents"));
	recent_menu = gtk_recent_chooser_menu_new_for_manager (manager);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), recent_menu);

	g_signal_connect (G_OBJECT (recent_menu), "button_press_event",
			  G_CALLBACK (menu_dummy_button_press_event), NULL);

	gtk_menu_shell_append (GTK_MENU_SHELL (top_menu), menu_item);
	gtk_widget_show_all (menu_item);

	gtk_recent_chooser_set_local_only (GTK_RECENT_CHOOSER (recent_menu),
					   FALSE);
	gtk_recent_chooser_set_show_tips (GTK_RECENT_CHOOSER (recent_menu),
					  TRUE);
	gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (recent_menu),
					  GTK_RECENT_SORT_MRU);

	g_signal_connect (GTK_RECENT_CHOOSER (recent_menu),
			  "item-activated",
			  G_CALLBACK (recent_documents_activate_cb),
			  NULL);

	//FIXME this is not possible with GtkRecent...: egg_recent_view_gtk_set_icon_size (view, panel_menu_icon_get_size ());

	g_signal_connect_object (manager, "changed",
				 G_CALLBACK (panel_recent_manager_changed_cb),
				 menu_item, 0);

	size = 0;
	g_object_get (manager, "size", &size, NULL);
	gtk_widget_set_sensitive (menu_item, size > 0);

	add_menu_separator (recent_menu);

	menu_item = gtk_image_menu_item_new ();
	setup_menuitem_with_icon (menu_item,
				   panel_menu_icon_get_size (),
				   NULL,
				   "edit-clear",
				   _("Clear Recent Documents..."));
	panel_util_set_tooltip_text (menu_item,
				     _("Clear all items from the recent documents list"));
	gtk_menu_shell_append (GTK_MENU_SHELL (recent_menu), menu_item);

	g_signal_connect (menu_item, "activate",
			  G_CALLBACK (recent_documents_clear_cb),
			  manager);
}
