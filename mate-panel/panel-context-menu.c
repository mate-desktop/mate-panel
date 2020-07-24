/*
 * panel-context-menu.c: context menu for the panels
 *
 * Copyright (C) 2004 Vincent Untz
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
 * Most of the original code come from menu.c
 *
 * Authors:
 *	Vincent Untz <vincent@vuntz.net>
 *
 */

#include <config.h>

#include "panel-context-menu.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

#include <libpanel-util/panel-error.h>
#include <libpanel-util/panel-show.h>
#include <libpanel-util/panel-gtk.h>

#include "panel-util.h"
#include "panel.h"
#include "menu.h"
#include "applet.h"
#include "panel-config-global.h"
#include "panel-profile.h"
#include "panel-properties-dialog.h"
#include "panel-lockdown.h"
#include "panel-addto.h"
#include "panel-icon-names.h"
#include "panel-reset.h"

static void
panel_context_menu_show_help (GtkWidget *w,
			      gpointer data)
{
	panel_show_help (gtk_widget_get_screen (w),
			 "mate-user-guide", "gospanel-1", NULL);
}

static void
panel_context_menu_show_about_dialog (GtkWidget *menuitem)
{
	static GtkWidget *about = NULL;
	char             *authors [] = {
		/* MATE */
		"Perberos <perberos@gmail.com>",
		"Stefano Karapetsas <stefano@karapetsas.com>",
		"Steve Zesch <stevezesch2@gmail.com>",
		/* GNOME */
		"Alex Larsson <alexl@redhat.com>",
		"Anders Carlsson <andersca@gnu.org>",
		"Arvind Samptur <arvind.samptur@wipro.com>",
		"Darin Adler <darin@bentspoon.com>",
		"Elliot Lee <sopwith@redhat.com>",
		"Federico Mena <quartic@gimp.org>",
		"George Lebl <jirka@5z.com>",
		"Glynn Foster <glynn.foster@sun.com>",
		"Ian Main <imain@gtk.org>",
		"Ian McKellar <yakk@yakk.net>",
		"Jacob Berkman <jberkman@andrew.cmu.edu>",
		"Mark McLoughlin <mark@skynet.ie>",
		"Martin Baulig <baulig@suse.de>",
		"Miguel de Icaza <miguel@kernel.org>",
		"Owen Taylor <otaylor@redhat.com>",
		"Padraig O'Briain <padraig.obriain@sun.com>",
		"Seth Nickell <snickell@stanford.edu>",
		"Stephen Browne <stephen.browne@sun.com>",
		"Tom Tromey <tromey@cygnus.com>",
		"Vincent Untz <vuntz@gnome.org>",
		N_("And many, many others…"),
		NULL
	};
	char *documenters[] = {
	        "Alexander Kirillov <kirillov@math.sunysb.edu>",
		"Dan Mueth <d-mueth@uchicago.edu>",
		"Dave Mason <dcm@redhat.com>",
		NULL
	  };
	int   i;

	if (about) {
		gtk_window_set_screen (GTK_WINDOW (about),
				       menuitem_to_screen (menuitem));
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	for (i = 0; authors [i]; i++)
		authors [i] = _(authors [i]);

	about = gtk_about_dialog_new ();
	g_object_set (about,
		      "program-name",  _("The MATE Panel"),
		      "version", VERSION,
		      "copyright", _("Copyright \xc2\xa9 1997-2003 Free Software Foundation, Inc.\n"
		                     "Copyright \xc2\xa9 2004 Vincent Untz\n"
		                     "Copyright \xc2\xa9 2011-2020 MATE developers"),
		      "comments", _("This program is responsible for launching other "
				    "applications and provides useful utilities."),
		      "authors", authors,
		      "documenters", documenters,
		      "title", _("About the MATE Panel"),
		      "translator-credits", _("translator-credits"),
		      "logo-icon-name", PANEL_ICON_PANEL,
		      NULL);

	gtk_window_set_screen (GTK_WINDOW (about),
			       menuitem_to_screen (menuitem));
	g_signal_connect (about, "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
			  &about);

	g_signal_connect (about, "response",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	gtk_widget_show (about);
}

static void
panel_context_menu_create_new_panel (GtkWidget *menuitem)
{
	panel_profile_create_toplevel (gtk_widget_get_screen (menuitem));
}

static void
panel_context_menu_delete_panel (PanelToplevel *toplevel)
{
	if (panel_toplevel_is_last_unattached (toplevel)) {
		panel_error_dialog (GTK_WINDOW (toplevel),
				    gtk_window_get_screen (GTK_WINDOW (toplevel)),
				    "cannot_delete_last_panel", TRUE,
				    _("Cannot delete this panel"),
				    _("You must always have at least one panel."));
		return;
	}

        panel_delete (toplevel);
}

static void
panel_context_menu_setup_delete_panel_item (GtkWidget *menu,
					    GtkWidget *menuitem)
{
	PanelWidget *panel_widget;
	gboolean     sensitive;

	panel_widget = menu_get_panel (menu);

	g_assert (PANEL_IS_TOPLEVEL (panel_widget->toplevel));

	sensitive =
		!panel_toplevel_is_last_unattached (panel_widget->toplevel) &&
		!panel_lockdown_get_locked_down () &&
		panel_profile_id_lists_are_writable ();

	gtk_widget_set_sensitive (menuitem, sensitive);
}

static void
panel_reset_response (GtkWidget     *dialog,
			 int            response)
{
	if (response == GTK_RESPONSE_OK) {
		panel_reset ();
	}

	gtk_widget_destroy (dialog);
}

static void
query_panel_reset (PanelToplevel *toplevel)
{
	GtkWidget *dialog;
	char *text1;
	char *text2;

	text1 = _("Reset all panels?");
	text2 = _("When the panels are reset, all \n"
			 "custom settings are lost.");

	dialog = gtk_message_dialog_new (
			GTK_WINDOW (toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_NONE,
			"%s", text1);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          "%s", text2);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				_("_Cancel"), GTK_RESPONSE_CANCEL,
				_("_Reset Panels"), GTK_RESPONSE_OK,
				NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (panel_reset_response),
			  NULL);

	gtk_widget_show_all (dialog);
}

static void
panel_context_menu_build_edition (PanelWidget *panel_widget,
				  GtkWidget   *menu)
{
	GtkWidget *menuitem;

	gtk_menu_set_reserve_toggle_size (GTK_MENU (menu), FALSE);

	menuitem = panel_image_menu_item_new_from_icon ("list-add", _("_Add to Panel…"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_signal_connect (G_OBJECT (menuitem), "activate",
	      	       	  G_CALLBACK (panel_addto_present), panel_widget);

	if (!panel_profile_id_lists_are_writable ())
		gtk_widget_set_sensitive (menuitem, FALSE);

	menuitem = panel_image_menu_item_new_from_icon ("document-properties", _("_Properties"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect_swapped (menuitem, "activate",
				  G_CALLBACK (panel_properties_dialog_present),
				  panel_widget->toplevel);

	add_menu_separator (menu);

	menuitem = panel_image_menu_item_new_from_icon ("document-revert", _("_Reset All Panels"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect_swapped (menuitem, "activate",
			  G_CALLBACK (query_panel_reset), panel_widget->toplevel);

	menuitem = panel_image_menu_item_new_from_icon ("edit-delete", _("_Delete This Panel"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect_swapped (G_OBJECT (menuitem), "activate",
				  G_CALLBACK (panel_context_menu_delete_panel),
				  panel_widget->toplevel);
	g_signal_connect (G_OBJECT (menu), "show",
			  G_CALLBACK (panel_context_menu_setup_delete_panel_item),
			  menuitem);

	add_menu_separator (menu);

	menuitem = panel_image_menu_item_new_from_icon ("document-new", _("_New Panel"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (panel_context_menu_create_new_panel),
			  NULL);
	gtk_widget_set_sensitive (menuitem,
				  panel_profile_id_lists_are_writable ());

	add_menu_separator (menu);
}

GtkWidget *
panel_context_menu_create (PanelWidget *panel)
{
	GtkWidget *retval;
	GtkWidget *menuitem;

	if (panel->master_widget) {
		gpointer    *pointer;
		AppletInfo  *info;

		pointer = g_object_get_data (G_OBJECT (panel->master_widget),
					     "applet_info");

		g_assert (pointer != NULL);
		info = (AppletInfo *) pointer;

		if (info->menu == NULL) {
			info->menu = mate_panel_applet_create_menu (info);
		}

		return info->menu;
	}

	retval = create_empty_menu ();
	gtk_widget_set_name (retval, "mate-panel-context-menu");

	if (!panel_lockdown_get_locked_down ())
		panel_context_menu_build_edition (panel, retval);

	menuitem = panel_image_menu_item_new_from_icon ("help-browser", _("_Help"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (retval), menuitem);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (panel_context_menu_show_help), NULL);

	menuitem = panel_image_menu_item_new_from_icon ("help-about", _("A_bout Panels"));

	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (retval), menuitem);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (panel_context_menu_show_about_dialog),
			  NULL);

	//FIXME: can we get rid of this? (needed by menu_get_panel())
	g_object_set_data (G_OBJECT (retval), "menu_panel", panel);

/* Set up theme and transparency support */
	GtkWidget *toplevel = gtk_widget_get_toplevel (retval);
/* Fix any failures of compiz/other wm's to communicate with gtk for transparency */
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(toplevel));
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	gtk_widget_set_visual(GTK_WIDGET(toplevel), visual);
/* Set menu and it's toplevel window to follow panel theme */
	GtkStyleContext *context;
	context = gtk_widget_get_style_context (GTK_WIDGET(toplevel));
	gtk_style_context_add_class(context,"gnome-panel-menu-bar");
	gtk_style_context_add_class(context,"mate-panel-menu-bar");

	return retval;
}
