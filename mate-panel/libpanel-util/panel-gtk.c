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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "panel-gtk.h"
#include "panel-cleanup.h"

/*
 * Originally based on code from panel-properties-dialog.c. This part of the
 * code was:
 * Copyright (C) 2005 Vincent Untz <vuntz@gnome.org>
 */

/*There should be only one icon_settings object for the whole panel
 *So we need a global variable here
 */
static GSettings *icon_settings = NULL;

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

GtkWidget*
panel_dialog_add_button (GtkDialog   *dialog,
			 const gchar *button_text,
			 const gchar *icon_name,
			       gint   response_id)
{
	GtkWidget *button;

	button = gtk_button_new_with_mnemonic (button_text);
	gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));

	gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "text-button");
	gtk_widget_set_can_default (button, TRUE);
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, response_id);

	return button;
}

static GtkWidget *
panel_file_chooser_dialog_new_valist (const gchar          *title,
				      GtkWindow            *parent,
				      GtkFileChooserAction  action,
				      const gchar          *first_button_text,
				      va_list               varargs)
{
	GtkWidget *result;
	const char *button_text = first_button_text;
	gint response_id;

	result = g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "title", title,
			       "action", action,
			       NULL);

	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (result), parent);

	while (button_text)
		{
			response_id = va_arg (varargs, gint);

			if (g_strcmp0 (button_text, "process-stop") == 0)
				panel_dialog_add_button (GTK_DIALOG (result), _("_Cancel"), button_text, response_id);
			else if (g_strcmp0 (button_text, "document-open") == 0)
				panel_dialog_add_button (GTK_DIALOG (result), _("_Open"), button_text, response_id);
			else if (g_strcmp0 (button_text, "gtk-ok") == 0)
				panel_dialog_add_button (GTK_DIALOG (result), _("_OK"), button_text, response_id);
			else
				gtk_dialog_add_button (GTK_DIALOG (result), button_text, response_id);

			button_text = va_arg (varargs, const gchar *);
		}

	return result;
}

GtkWidget *
panel_file_chooser_dialog_new (const gchar          *title,
			       GtkWindow            *parent,
			       GtkFileChooserAction  action,
			       const gchar          *first_button_text,
			       ...)
{
	GtkWidget *result;
	va_list varargs;

	va_start (varargs, first_button_text);
	result = panel_file_chooser_dialog_new_valist (title, parent, action,
						       first_button_text,
						       varargs);
	va_end (varargs);

	return result;
}


static void
ensure_icon_settings (void)
{
	if (icon_settings != NULL)
	return;

	icon_settings = g_settings_new ("org.mate.interface");

	panel_cleanup_register (panel_cleanup_unref_and_nullify,
					&icon_settings);
}

GtkWidget *
panel_image_menu_item_new_from_icon (const gchar *icon_name,
				     const gchar *label_name)
{
	gchar *concat;
	GtkWidget *icon;
	GtkStyleContext *context;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *icon_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	if (icon_name)
		icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	else
		icon = gtk_image_new ();

	concat = g_strconcat (label_name, "     ", NULL);
	GtkWidget *label_menu = gtk_label_new_with_mnemonic (concat);
	GtkWidget *menuitem = gtk_menu_item_new ();

	context = gtk_widget_get_style_context (GTK_WIDGET(icon_box));
	gtk_style_context_add_class(context,"mate-panel-menu-icon-box");

	gtk_container_add (GTK_CONTAINER (icon_box), icon);
	gtk_container_add (GTK_CONTAINER (box), icon_box);
	gtk_container_add (GTK_CONTAINER (box), label_menu);

	gtk_container_add (GTK_CONTAINER (menuitem), box);
	gtk_widget_show_all (menuitem);

	ensure_icon_settings();
	g_settings_bind (icon_settings, "menus-have-icons", icon, "visible",
                         G_SETTINGS_BIND_GET);

	g_free (concat);

	return menuitem;
}

GtkWidget *
panel_image_menu_item_new_from_gicon (GIcon       *gicon,
				      const gchar *label_name)
{
	gchar *concat;
	GtkWidget *icon;
	GtkStyleContext *context;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *icon_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	if (gicon)
		icon = gtk_image_new_from_gicon (gicon, GTK_ICON_SIZE_MENU);
	else
		icon = gtk_image_new ();

	concat = g_strconcat (label_name, "     ", NULL);
	GtkWidget *label_menu = gtk_label_new_with_mnemonic (concat);
	GtkWidget *menuitem = gtk_menu_item_new ();

	context = gtk_widget_get_style_context (GTK_WIDGET(icon_box));
	gtk_style_context_add_class(context,"mate-panel-menu-icon-box");

	gtk_container_add (GTK_CONTAINER (icon_box), icon);
	gtk_container_add (GTK_CONTAINER (box), icon_box);
	gtk_container_add (GTK_CONTAINER (box), label_menu);

	gtk_container_add (GTK_CONTAINER (menuitem), box);
	gtk_widget_show_all (menuitem);

	ensure_icon_settings();
	g_settings_bind (icon_settings, "menus-have-icons", icon, "visible",
                         G_SETTINGS_BIND_GET);

	g_free (concat);

	return menuitem;
}

GtkWidget *
panel_check_menu_item_new (GtkWidget *widget_check)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *menuitem = gtk_menu_item_new ();
	GtkWidget *label_name = gtk_bin_get_child (GTK_BIN (widget_check));
	gchar *concat = g_strconcat (gtk_label_get_label (GTK_LABEL (label_name)), "     ", NULL);

	gtk_label_set_text_with_mnemonic (GTK_LABEL (label_name), concat);

	gtk_widget_set_margin_start (widget_check, 2);
	gtk_widget_set_margin_start (gtk_bin_get_child (GTK_BIN (widget_check)), 11);
	gtk_box_pack_start (GTK_BOX (box), widget_check, FALSE, FALSE, 5);

	gtk_container_add (GTK_CONTAINER (menuitem), box);
	gtk_widget_show_all (menuitem);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label_name), menuitem);

	g_free (concat);

	return menuitem;
}
