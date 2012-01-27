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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

#include "nothing.h"
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

static void
panel_context_menu_show_help (GtkWidget *w,
			      gpointer data)
{
	panel_show_help (gtk_widget_get_screen (w),
			 "user-guide", "gospanel-1", NULL);
}

static gboolean
panel_context_menu_check_for_screen (GtkWidget *w,
				     GdkEvent *ev,
				     gpointer data)
{
	static int times = 0;
	if (ev->type != GDK_KEY_PRESS)
		return FALSE;
	if (ev->key.keyval == GDK_f ||
	    ev->key.keyval == GDK_F) {
		times++;
		if (times == 3) {
			times = 0;
			start_screen_check ();
		}
	}
	return FALSE;
}

static void
panel_context_menu_show_about_dialog (GtkWidget *menuitem)
{
	static GtkWidget *about = NULL;
	char             *authors [] = {
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
		N_("And many, many others..."),
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

	/* Note: we don't use gtk_show_about_dialog() since some applets can
	 * be loaded in this process and we don't want to share the about
	 * dialog */
	about = gtk_about_dialog_new ();
	g_object_set (about,
		      "program-name",  _("The MATE Panel"),
		      "version", VERSION,
		      "copyright", "Copyright \xc2\xa9 1997-2003 Free Software Foundation, Inc.",
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
	g_signal_connect (about, "event",
			  G_CALLBACK (panel_context_menu_check_for_screen),
			  NULL);

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
panel_context_menu_build_edition (PanelWidget *panel_widget,
				  GtkWidget   *menu)
{
	GtkWidget *menuitem;
	GtkWidget *image;

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("_Add to Panel..."));
	image = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_signal_connect (G_OBJECT (menuitem), "activate",
	      	       	  G_CALLBACK (panel_addto_present), panel_widget);

	if (!panel_profile_id_lists_are_writable ())
		gtk_widget_set_sensitive (menuitem, FALSE);

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("_Properties"));
	image = gtk_image_new_from_stock (GTK_STOCK_PROPERTIES,
					  GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect_swapped (menuitem, "activate",
				  G_CALLBACK (panel_properties_dialog_present), 
				  panel_widget->toplevel);

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("_Delete This Panel"));
	image = gtk_image_new_from_stock (GTK_STOCK_DELETE,
					  GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	g_signal_connect_swapped (G_OBJECT (menuitem), "activate",
				  G_CALLBACK (panel_context_menu_delete_panel),
				  panel_widget->toplevel);
	g_signal_connect (G_OBJECT (menu), "show",
			  G_CALLBACK (panel_context_menu_setup_delete_panel_item),
			  menuitem);

	add_menu_separator (menu);

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("_New Panel"));
	image = gtk_image_new_from_stock (GTK_STOCK_NEW, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
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
	GtkWidget *image;

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

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	image = gtk_image_new_from_stock (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (retval), menuitem);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (panel_context_menu_show_help), NULL);

	menuitem = gtk_image_menu_item_new_with_mnemonic (_("A_bout Panels"));
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT,
					  GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (retval), menuitem);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (panel_context_menu_show_about_dialog),
			  NULL);
	
	//FIXME: can we get rid of this? (needed by menu_get_panel())
	g_object_set_data (G_OBJECT (retval), "menu_panel", panel);

	return retval;
}
