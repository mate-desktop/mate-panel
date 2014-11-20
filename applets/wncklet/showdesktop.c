/* -*- mode: C; c-file-style: "linux" -*- */
/* "Show desktop" panel applet */

/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Developed by Havoc Pennington
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
 */

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#include <libmate-desktop/mate-aboutdialog.h>

#include "wncklet.h"
#include "showdesktop.h"

#include <string.h>

#define TIMEOUT_ACTIVATE_SECONDS 1
#define SHOW_DESKTOP_ICON "user-desktop"


typedef struct {
	/* widgets */
	GtkWidget* applet;
	GtkWidget* button;
	GtkWidget* image;

	GtkOrientation orient;
	int size;

	WnckScreen* wnck_screen;

	guint showing_desktop: 1;
	guint button_activate;

	GtkIconTheme* icon_theme;
} ShowDesktopData;

static void display_help_dialog(GtkAction* action, ShowDesktopData* sdd);
static void display_about_dialog(GtkAction* action, ShowDesktopData* sdd);

static void update_icon(ShowDesktopData* sdd);
static void update_button_state(ShowDesktopData* sdd);
static void update_button_display(ShowDesktopData* sdd);

static void theme_changed_callback(GtkIconTheme* icon_theme, ShowDesktopData* sdd);

static void button_toggled_callback(GtkWidget* button, ShowDesktopData* sdd);
static void show_desktop_changed_callback(WnckScreen* screen, ShowDesktopData* sdd);

/* this is when the panel orientation changes */

static void applet_change_orient(MatePanelApplet* applet, MatePanelAppletOrient orient, ShowDesktopData* sdd)
{
	GtkOrientation new_orient;

	switch (orient)
	{
		case MATE_PANEL_APPLET_ORIENT_LEFT:
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			new_orient = GTK_ORIENTATION_VERTICAL;
			break;
		case MATE_PANEL_APPLET_ORIENT_UP:
		case MATE_PANEL_APPLET_ORIENT_DOWN:
		default:
			new_orient = GTK_ORIENTATION_HORIZONTAL;
			break;
	}

	if (new_orient == sdd->orient)
		return;

	sdd->orient = new_orient;

	update_icon (sdd);
}

/* this is when the panel size changes */
static void button_size_allocated(GtkWidget* button, GtkAllocation* allocation, ShowDesktopData* sdd)
{
	/*
	if ((sdd->orient == GTK_ORIENTATION_HORIZONTAL) && (sdd->size == allocation->height))
	{
		return;
	}
	else if ((sdd->orient == GTK_ORIENTATION_VERTICAL) && (sdd->size == allocation->width))
	{
		return;
	}
	*/

	if (((sdd->orient == GTK_ORIENTATION_HORIZONTAL) && (sdd->size == allocation->height)) || ((sdd->orient == GTK_ORIENTATION_VERTICAL) && (sdd->size == allocation->width)))
		return;

	switch (sdd->orient)
	{
		case GTK_ORIENTATION_HORIZONTAL:
			sdd->size = allocation->height;
			break;
		case GTK_ORIENTATION_VERTICAL:
			sdd->size = allocation->width;
			break;
	}

	update_icon(sdd);
}

static void update_icon(ShowDesktopData* sdd)
{
	GtkStyle* style;
	int width, height;
	GdkPixbuf* icon;
	GdkPixbuf* scaled;
	int icon_size;
	GError* error;
	int focus_width = 0;
	int focus_pad = 0;
	int thickness = 0;

	if (!sdd->icon_theme)
		return;

	gtk_widget_style_get (sdd->button, "focus-line-width", &focus_width, "focus-padding", &focus_pad, NULL);

	style = gtk_widget_get_style(sdd->button);

	switch (sdd->orient)
	{
		case GTK_ORIENTATION_HORIZONTAL:
			thickness = style->ythickness;
			break;
		case GTK_ORIENTATION_VERTICAL:
			thickness = style->xthickness;
			break;
	}

	icon_size = sdd->size - 2 * (focus_width + focus_pad + thickness);

	if (icon_size < 22)
		icon_size = 16;
	else if (icon_size < 32)
		icon_size = 22;
	else if (icon_size < 48)
		icon_size = 32;

	error = NULL;
	icon = gtk_icon_theme_load_icon (sdd->icon_theme, SHOW_DESKTOP_ICON, icon_size, 0, &error);

	if (icon == NULL)
	{
		g_printerr(_("Failed to load %s: %s\n"), SHOW_DESKTOP_ICON, error ? error->message : _("Icon not found"));

		if (error)
		{
			g_error_free(error);
			error = NULL;
		}

		gtk_image_set_from_stock(GTK_IMAGE (sdd->image), GTK_STOCK_MISSING_IMAGE, GTK_ICON_SIZE_SMALL_TOOLBAR);
		return;
	}

	width = gdk_pixbuf_get_width(icon);
	height = gdk_pixbuf_get_height(icon);

	scaled = NULL;

	/* Make it fit on the given panel */
	switch (sdd->orient)
	{
		case GTK_ORIENTATION_HORIZONTAL:
			width = (icon_size * width) / height;
			height = icon_size;
			break;
		case GTK_ORIENTATION_VERTICAL:
			height = (icon_size * height) / width;
			width = icon_size;
			break;
	}

	scaled = gdk_pixbuf_scale_simple(icon, width, height, GDK_INTERP_BILINEAR);

	if (scaled != NULL)
	{
		gtk_image_set_from_pixbuf(GTK_IMAGE(sdd->image), scaled);
		g_object_unref(scaled);
	}
	else
	{
		gtk_image_set_from_pixbuf (GTK_IMAGE (sdd->image), icon);
	}

	g_object_unref (icon);
}

static const GtkActionEntry show_desktop_menu_actions[] = {
	{
		"ShowDesktopHelp",
		GTK_STOCK_HELP,
		N_("_Help"),
		NULL,
		NULL,
		G_CALLBACK(display_help_dialog)
	},
	{
		"ShowDesktopAbout",
		GTK_STOCK_ABOUT,
		N_("_About"),
		NULL,
		NULL,
		G_CALLBACK(display_about_dialog)
	}
};

/* This updates things that should be consistent with the button's appearance,
 * and update_button_state updates the button appearance itself
 */
static void update_button_display(ShowDesktopData* sdd)
{
	const char* tip;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdd->button)))
	{
		tip = _("Click here to restore hidden windows.");
	}
	else
	{
		tip = _("Click here to hide all windows and show the desktop.");
	}

	gtk_widget_set_tooltip_text(sdd->button, tip);
}

static void update_button_state(ShowDesktopData* sdd)
{
	if (sdd->showing_desktop)
	{
		g_signal_handlers_block_by_func(G_OBJECT(sdd->button), G_CALLBACK(button_toggled_callback), sdd);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdd->button), TRUE);
		g_signal_handlers_unblock_by_func(G_OBJECT(sdd->button), G_CALLBACK(button_toggled_callback), sdd);
	}
	else
	{
		g_signal_handlers_block_by_func(G_OBJECT(sdd->button), G_CALLBACK(button_toggled_callback), sdd);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdd->button), FALSE);
		g_signal_handlers_unblock_by_func(G_OBJECT(sdd->button), G_CALLBACK(button_toggled_callback), sdd);
	}

	update_button_display (sdd);
}

static void applet_destroyed(GtkWidget* applet, ShowDesktopData* sdd)
{
	if (sdd->button_activate != 0)
	{
		g_source_remove(sdd->button_activate);
		sdd->button_activate = 0;
	}

	if (sdd->wnck_screen != NULL)
	{
		g_signal_handlers_disconnect_by_func(sdd->wnck_screen, show_desktop_changed_callback, sdd);
		sdd->wnck_screen = NULL;
	}

	if (sdd->icon_theme != NULL)
	{
		g_signal_handlers_disconnect_by_func(sdd->icon_theme, theme_changed_callback, sdd);
		sdd->icon_theme = NULL;
	}

	g_free (sdd);
}

static gboolean do_not_eat_button_press(GtkWidget* widget, GdkEventButton* event)
{
	if (event->button != 1)
	{
		g_signal_stop_emission_by_name(widget, "button_press_event");
	}

	return FALSE;
}

static gboolean button_motion_timeout(gpointer data)
{
	ShowDesktopData* sdd = (ShowDesktopData*) data;

	sdd->button_activate = 0;

	g_signal_emit_by_name(G_OBJECT(sdd->button), "clicked", sdd);

	return FALSE;
}

static void button_drag_leave(GtkWidget* widget, GdkDragContext* context, guint time, ShowDesktopData* sdd)
{
	if (sdd->button_activate != 0)
	{
		g_source_remove(sdd->button_activate);
		sdd->button_activate = 0;
	}
}

static gboolean button_drag_motion(GtkWidget* widget, GdkDragContext* context, gint x, gint y, guint time, ShowDesktopData* sdd)
{
	if (sdd->button_activate == 0)
		sdd->button_activate = g_timeout_add_seconds (TIMEOUT_ACTIVATE_SECONDS, button_motion_timeout, sdd);

	gdk_drag_status(context, 0, time);

	return TRUE;
}

static void show_desktop_applet_realized(MatePanelApplet* applet, gpointer data)
{
	ShowDesktopData* sdd;
	GdkScreen* screen;

	sdd = (ShowDesktopData*) data;

	if (sdd->wnck_screen != NULL)
		g_signal_handlers_disconnect_by_func(sdd->wnck_screen, show_desktop_changed_callback, sdd);

	if (sdd->icon_theme != NULL)
		g_signal_handlers_disconnect_by_func(sdd->icon_theme, theme_changed_callback, sdd);

	screen = gtk_widget_get_screen(sdd->applet);
	sdd->wnck_screen = wnck_screen_get(gdk_screen_get_number (screen));

	if (sdd->wnck_screen != NULL)
		wncklet_connect_while_alive(sdd->wnck_screen, "showing_desktop_changed", G_CALLBACK(show_desktop_changed_callback), sdd, sdd->applet);
	else
		g_warning("Could not get WnckScreen!");

	show_desktop_changed_callback(sdd->wnck_screen, sdd);

	sdd->icon_theme = gtk_icon_theme_get_for_screen (screen);
	wncklet_connect_while_alive(sdd->icon_theme, "changed", G_CALLBACK(theme_changed_callback), sdd, sdd->applet);

	update_icon (sdd);
}

static void theme_changed_callback(GtkIconTheme* icon_theme, ShowDesktopData* sdd)
{
	update_icon (sdd);
}

gboolean show_desktop_applet_fill(MatePanelApplet* applet)
{
	ShowDesktopData* sdd;
	GtkActionGroup* action_group;
	gchar* ui_path;
	AtkObject* atk_obj;
#if GTK_CHECK_VERSION (3, 0, 0)
        GtkCssProvider  *provider;
#endif

	mate_panel_applet_set_flags(applet, MATE_PANEL_APPLET_EXPAND_MINOR);

	sdd = g_new0(ShowDesktopData, 1);

	sdd->applet = GTK_WIDGET(applet);

	sdd->image = gtk_image_new();

	switch (mate_panel_applet_get_orient(applet))
	{
		case MATE_PANEL_APPLET_ORIENT_LEFT:
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			sdd->orient = GTK_ORIENTATION_VERTICAL;
			break;
		case MATE_PANEL_APPLET_ORIENT_UP:
		case MATE_PANEL_APPLET_ORIENT_DOWN:
		default:
			sdd->orient = GTK_ORIENTATION_HORIZONTAL;
			break;
	}

	sdd->size = mate_panel_applet_get_size(MATE_PANEL_APPLET(sdd->applet));

	g_signal_connect(G_OBJECT(sdd->applet), "realize", G_CALLBACK(show_desktop_applet_realized), sdd);

	sdd->button = gtk_toggle_button_new ();

	gtk_widget_set_name (sdd->button, "showdesktop-button");
#if GTK_CHECK_VERSION (3, 0, 0)
        provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (provider,
                                         "#showdesktop-button {\n"
                                         " -GtkWidget-focus-line-width: 0px;\n"
                                         " -GtkWidget-focus-padding: 0px;\n"
					 "}",
                                         -1, NULL);
        gtk_style_context_add_provider (gtk_widget_get_style_context (sdd->button),
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (provider);
#else
	gtk_rc_parse_string ("\n"
		"   style \"showdesktop-button-style\"\n"
		"   {\n"
		"      GtkWidget::focus-line-width=0\n"
		"      GtkWidget::focus-padding=0\n"
		"   }\n"
		"\n"
		"    widget \"*.showdesktop-button\" style \"showdesktop-button-style\"\n"
		"\n");
#endif

	atk_obj = gtk_widget_get_accessible(sdd->button);
	atk_object_set_name (atk_obj, _("Show Desktop Button"));
	g_signal_connect(G_OBJECT(sdd->button), "button_press_event", G_CALLBACK(do_not_eat_button_press), NULL);

	g_signal_connect(G_OBJECT(sdd->button), "toggled", G_CALLBACK(button_toggled_callback), sdd);

	gtk_container_set_border_width(GTK_CONTAINER(sdd->button), 0);
	gtk_container_add(GTK_CONTAINER(sdd->button), sdd->image);
	gtk_container_add(GTK_CONTAINER(sdd->applet), sdd->button);

	g_signal_connect (G_OBJECT(sdd->button), "size_allocate", G_CALLBACK(button_size_allocated), sdd);

	/* FIXME: Update this comment. */
	/* we have to bind change_orient before we do applet_widget_add
	   since we need to get an initial change_orient signal to set our
	   initial oriantation, and we get that during the _add call */
	g_signal_connect(G_OBJECT (sdd->applet), "change_orient", G_CALLBACK (applet_change_orient), sdd);

	mate_panel_applet_set_background_widget(MATE_PANEL_APPLET (sdd->applet), GTK_WIDGET(sdd->applet));

	action_group = gtk_action_group_new("ShowDesktop Applet Actions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, show_desktop_menu_actions, G_N_ELEMENTS (show_desktop_menu_actions), sdd);
	ui_path = g_build_filename(WNCK_MENU_UI_DIR, "showdesktop-menu.xml", NULL);
	mate_panel_applet_setup_menu_from_file(MATE_PANEL_APPLET(sdd->applet), ui_path, action_group);
	g_free(ui_path);
	g_object_unref(action_group);

	g_signal_connect(G_OBJECT(sdd->applet), "destroy", G_CALLBACK(applet_destroyed), sdd);

	gtk_drag_dest_set(GTK_WIDGET(sdd->button), 0, NULL, 0, 0);

	g_signal_connect(G_OBJECT(sdd->button), "drag_motion", G_CALLBACK (button_drag_motion), sdd);
	g_signal_connect(G_OBJECT(sdd->button), "drag_leave", G_CALLBACK (button_drag_leave), sdd);

	gtk_widget_show_all(sdd->applet);

	return TRUE;
}

static void display_help_dialog(GtkAction* action, ShowDesktopData* sdd)
{
	wncklet_display_help(sdd->applet, "mate-user-guide", "gospanel-564", SHOW_DESKTOP_ICON);
}

static void display_about_dialog(GtkAction* action, ShowDesktopData* sdd)
{
	static const gchar* authors[] = {
		"Perberos <perberos@gmail.com>",
		"Steve Zesch <stevezesch2@gmail.com>",
		"Stefano Karapetsas <stefano@karapetsas.com>",
		"Havoc Pennington <hp@redhat.com>",
		NULL
	};
	static const char* documenters[] = {
		"Sun GNOME Documentation Team <gdocteam@sun.com>",
		NULL
	};

	/* Translator credits */
	//const char* translator_credits = _("translator-credits");
	char copyright[] = \
		"Copyright \xc2\xa9 2011 Perberos\n"
		"Copyright \xc2\xa9 2002 Red Hat, Inc.";

	mate_show_about_dialog(GTK_WINDOW(sdd->applet),
		"program-name", _("Show Desktop Button"),
		"authors", authors,
		"comments", _("This button lets you hide all windows and show the desktop."),
		"copyright", copyright,
		"documenters", documenters,
		"icon-name", SHOW_DESKTOP_ICON,
		"logo-icon-name", SHOW_DESKTOP_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		"website", "http://www.mate-desktop.org/",
		NULL);
}

static void button_toggled_callback(GtkWidget* button, ShowDesktopData* sdd)
{
	if (!gdk_x11_screen_supports_net_wm_hint(gtk_widget_get_screen(button), gdk_atom_intern("_NET_SHOWING_DESKTOP", FALSE)))
	{
		static GtkWidget* dialog = NULL;

		if (dialog && gtk_widget_get_screen(dialog) != gtk_widget_get_screen(button))
			gtk_widget_destroy (dialog);

		if (dialog)
		{
			gtk_window_present(GTK_WINDOW(dialog));
			return;
		}

		dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, _("Your window manager does not support the show desktop button, or you are not running a window manager."));

		g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer) &dialog);

		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);

		gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
		gtk_window_set_screen(GTK_WINDOW(dialog), gtk_widget_get_screen(button));
		gtk_widget_show(dialog);

		return;
	}

	if (sdd->wnck_screen != NULL)
		wnck_screen_toggle_showing_desktop(sdd->wnck_screen, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));

	update_button_display (sdd);
}

static void show_desktop_changed_callback(WnckScreen* screen, ShowDesktopData* sdd)
{
	if (sdd->wnck_screen != NULL)
		sdd->showing_desktop = wnck_screen_get_showing_desktop(sdd->wnck_screen);

	update_button_state (sdd);
}
