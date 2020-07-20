/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * libwnck based pager applet.
 * (C) 2001 Alexander Larsson
 * (C) 2001 Red Hat, Inc
 *
 * Authors: Alexander Larsson
 *
 */

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#include <string.h>

#include <mate-panel-applet.h>
#include <mate-panel-applet-gsettings.h>

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#include <gio/gio.h>

#include <libmate-desktop/mate-gsettings.h>

#include "workspace-switcher.h"

#include "wncklet.h"

/* even 16 is pretty darn dubious. */
#define MAX_REASONABLE_ROWS 16
#define DEFAULT_ROWS 1

#define WORKSPACE_SWITCHER_SCHEMA "org.mate.panel.applet.workspace-switcher"

#define NEVER_SENSITIVE "never_sensitive"
#define MARCO_GENERAL_SCHEMA "org.mate.Marco.general"
#define NUM_WORKSPACES "num-workspaces"
#define MARCO_WORKSPACES_SCHEMA "org.mate.Marco.workspace-names"
#define WORKSPACE_NAME "name-1"

#define WORKSPACE_SWITCHER_ICON "mate-panel-workspace-switcher"

typedef enum {
	PAGER_WM_MARCO,
	PAGER_WM_METACITY,
	PAGER_WM_COMPIZ,
	PAGER_WM_I3,
	PAGER_WM_UNKNOWN
} PagerWM;

typedef struct {
	GtkWidget* applet;

	GtkWidget* pager;

	WnckScreen* screen;
	PagerWM wm;

	/* Properties: */
	GtkWidget* properties_dialog;
	GtkWidget* workspaces_frame;
	GtkWidget* workspace_names_label;
	GtkWidget* workspace_names_scroll;
	GtkWidget* display_workspaces_toggle;
	GtkWidget* wrap_workspaces_toggle;
	GtkWidget* all_workspaces_radio;
	GtkWidget* current_only_radio;
	GtkWidget* num_rows_spin;	       /* for vertical layout this is cols */
	GtkWidget* label_row_col;
	GtkWidget* num_workspaces_spin;
	GtkWidget* workspaces_tree;
	GtkListStore* workspaces_store;
	GtkCellRenderer* cell;

	GtkOrientation orientation;
	int n_rows;				/* for vertical layout this is cols */
	WnckPagerDisplayMode display_mode;
	gboolean display_all;
	gboolean wrap_workspaces;

	GSettings* settings;
} PagerData;

static void display_properties_dialog(GtkAction* action, PagerData* pager);
static void display_help_dialog(GtkAction* action, PagerData* pager);
static void display_about_dialog(GtkAction* action, PagerData* pager);
static void destroy_pager(GtkWidget* widget, PagerData* pager);

static void pager_update(PagerData* pager)
{
	wnck_pager_set_orientation(WNCK_PAGER(pager->pager), pager->orientation);
	wnck_pager_set_n_rows(WNCK_PAGER(pager->pager), pager->n_rows);
	wnck_pager_set_show_all(WNCK_PAGER(pager->pager), pager->display_all);

	if (pager->wm == PAGER_WM_MARCO)
		wnck_pager_set_display_mode(WNCK_PAGER(pager->pager), pager->display_mode);
	else if (pager->wm == PAGER_WM_METACITY)
		wnck_pager_set_display_mode(WNCK_PAGER(pager->pager), pager->display_mode);
	else if (pager->wm == PAGER_WM_I3)
		wnck_pager_set_display_mode(WNCK_PAGER(pager->pager), pager->display_mode);
	else
		wnck_pager_set_display_mode(WNCK_PAGER(pager->pager), WNCK_PAGER_DISPLAY_CONTENT);
}

static void update_properties_for_wm(PagerData* pager)
{
	switch (pager->wm)
	{
		case PAGER_WM_MARCO:
			if (pager->workspaces_frame)
				gtk_widget_show(pager->workspaces_frame);
			if (pager->workspace_names_label)
				gtk_widget_show(pager->workspace_names_label);
			if (pager->workspace_names_scroll)
				gtk_widget_show(pager->workspace_names_scroll);
			if (pager->display_workspaces_toggle)
				gtk_widget_show(pager->display_workspaces_toggle);
			if (pager->cell)
				g_object_set (pager->cell, "editable", TRUE, NULL);
			break;
		case PAGER_WM_METACITY:
			if (pager->workspaces_frame)
				gtk_widget_show(pager->workspaces_frame);
			if (pager->workspace_names_label)
				gtk_widget_show(pager->workspace_names_label);
			if (pager->workspace_names_scroll)
				gtk_widget_show(pager->workspace_names_scroll);
			if (pager->display_workspaces_toggle)
				gtk_widget_show(pager->display_workspaces_toggle);
			if (pager->cell)
				g_object_set (pager->cell, "editable", TRUE, NULL);
			break;
		case PAGER_WM_I3:
			if (pager->workspaces_frame)
				gtk_widget_show(pager->workspaces_frame);
			if (pager->num_workspaces_spin)
				gtk_widget_set_sensitive(pager->num_workspaces_spin, FALSE);
			if (pager->workspace_names_label)
				gtk_widget_hide(pager->workspace_names_label);
			if (pager->workspace_names_scroll)
				gtk_widget_hide(pager->workspace_names_scroll);
			if (pager->display_workspaces_toggle)
				gtk_widget_show(pager->display_workspaces_toggle);
			if (pager->cell)
				g_object_set (pager->cell, "editable", FALSE, NULL);
			break;
		case PAGER_WM_COMPIZ:
			if (pager->workspaces_frame)
				gtk_widget_show(pager->workspaces_frame);
			if (pager->workspace_names_label)
				gtk_widget_hide(pager->workspace_names_label);
			if (pager->workspace_names_scroll)
				gtk_widget_hide(pager->workspace_names_scroll);
			if (pager->display_workspaces_toggle)
				gtk_widget_hide(pager->display_workspaces_toggle);
			if (pager->cell)
				g_object_set (pager->cell, "editable", FALSE, NULL);
			break;
		case PAGER_WM_UNKNOWN:
			if (pager->workspaces_frame)
				gtk_widget_hide(pager->workspaces_frame);
			break;
		default:
			g_assert_not_reached();
	}

	if (pager->properties_dialog) {
	        gtk_widget_hide (pager->properties_dialog);
	        gtk_widget_unrealize (pager->properties_dialog);
	        gtk_widget_show (pager->properties_dialog);
	}
}

static void window_manager_changed(WnckScreen* screen, PagerData* pager)
{
	const char *wm_name;

	wm_name = wnck_screen_get_window_manager_name(screen);

	if (!wm_name)
		pager->wm = PAGER_WM_UNKNOWN;
	else if (strcmp(wm_name, "Metacity (Marco)") == 0)
		pager->wm = PAGER_WM_MARCO;
	else if (strcmp(wm_name, "Metacity") == 0)
		pager->wm = PAGER_WM_METACITY;
	else if (strcmp(wm_name, "i3") == 0)
		pager->wm = PAGER_WM_I3;
	else if (strcmp(wm_name, "Compiz") == 0)
		pager->wm = PAGER_WM_COMPIZ;
	else
		pager->wm = PAGER_WM_UNKNOWN;

	update_properties_for_wm(pager);
	pager_update(pager);
}

static void applet_realized(MatePanelApplet* applet, PagerData* pager)
{
	pager->screen = wncklet_get_screen(GTK_WIDGET(applet));

	window_manager_changed(pager->screen, pager);
	wncklet_connect_while_alive(pager->screen, "window_manager_changed", G_CALLBACK(window_manager_changed), pager, pager->applet);
}

static void applet_unrealized(MatePanelApplet* applet, PagerData* pager)
{
	pager->screen = NULL;
	pager->wm = PAGER_WM_UNKNOWN;
}

static void applet_change_orient(MatePanelApplet* applet, MatePanelAppletOrient orient, PagerData* pager)
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

	if (new_orient == pager->orientation)
		return;

	pager->orientation = new_orient;
	pager_update(pager);

	if (pager->label_row_col)
		gtk_label_set_text(GTK_LABEL(pager->label_row_col), pager->orientation == GTK_ORIENTATION_HORIZONTAL ? _("rows") : _("columns"));
}

static void applet_change_background(MatePanelApplet* applet, MatePanelAppletBackgroundType type, GdkColor* color, cairo_pattern_t *pattern, PagerData* pager)
{
        GtkStyleContext *new_context;
        gtk_widget_reset_style (GTK_WIDGET (pager->pager));
        new_context = gtk_style_context_new ();
        gtk_style_context_set_path (new_context, gtk_widget_get_path (GTK_WIDGET (pager->pager)));
        g_object_unref (new_context);

        wnck_pager_set_shadow_type (WNCK_PAGER (pager->pager),
                type == PANEL_NO_BACKGROUND ? GTK_SHADOW_NONE : GTK_SHADOW_IN);
}

static void applet_style_updated (MatePanelApplet *applet, GtkStyleContext *context)
{
	GtkCssProvider *provider;
	GdkRGBA color;
	gchar *color_str;
	gchar *bg_css;

	provider = gtk_css_provider_new ();

	/* Provide a fallback color for the highlighted workspace based on the current theme */
	gtk_style_context_lookup_color (context, "theme_selected_bg_color", &color);
	color_str = gdk_rgba_to_string (&color);
	bg_css = g_strconcat (".wnck-pager:selected {\n"
		              "	background-color:", color_str, ";\n"
		              "}", NULL);
	gtk_css_provider_load_from_data (provider, bg_css, -1, NULL);
	g_free (bg_css);
	g_free (color_str);

	gtk_style_context_add_provider (context,
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);
	g_object_unref (provider);
}

/* Replacement for the default scroll handler that also cares about the wrapping property.
 * Alternative: Add behaviour to libwnck (to the WnckPager widget).
 */
static gboolean applet_scroll(MatePanelApplet* applet, GdkEventScroll* event, PagerData* pager)
{
	GdkScrollDirection absolute_direction;
	int index;
	int n_workspaces;
	int n_columns;
	int in_last_row;

	if (event->type != GDK_SCROLL)
		return FALSE;

	if (event->direction == GDK_SCROLL_SMOOTH)
		return FALSE;

	index = wnck_workspace_get_number(wnck_screen_get_active_workspace(pager->screen));
	n_workspaces = wnck_screen_get_workspace_count(pager->screen);
	n_columns = n_workspaces / pager->n_rows;

	if (n_workspaces % pager->n_rows != 0)
		n_columns++;

	in_last_row    = n_workspaces % n_columns;

	absolute_direction = event->direction;

	if (gtk_widget_get_direction(GTK_WIDGET(applet)) == GTK_TEXT_DIR_RTL)
	{
		switch (event->direction)
		{
			case GDK_SCROLL_RIGHT:
				absolute_direction = GDK_SCROLL_LEFT;
				break;
			case GDK_SCROLL_LEFT:
				absolute_direction = GDK_SCROLL_RIGHT;
				break;
			default:
				break;
		}
	}

	switch (absolute_direction)
	{
		case GDK_SCROLL_DOWN:
			if (index + n_columns < n_workspaces)
			{
				index += n_columns;
			}
			else if (pager->wrap_workspaces && index == n_workspaces - 1)
			{
				index = 0;
			}
			else if ((index < n_workspaces - 1 && index + in_last_row != n_workspaces - 1) || (index == n_workspaces - 1 && in_last_row != 0))
			{
				index = (index % n_columns) + 1;
			}
			break;

		case GDK_SCROLL_RIGHT:
			if (index < n_workspaces - 1)
			{
				index++;
			}
			else if (pager->wrap_workspaces)
			{
			        index = 0;
			}
			break;

		case GDK_SCROLL_UP:
			if (index - n_columns >= 0)
			{
				index -= n_columns;
			}
			else if (index > 0)
			{
				index = ((pager->n_rows - 1) * n_columns) + (index % n_columns) - 1;
			}
			else if (pager->wrap_workspaces)
			{
				index = n_workspaces - 1;
			}

			if (index >= n_workspaces)
				index -= n_columns;
			break;

		case GDK_SCROLL_LEFT:
			if (index > 0)
			{
				index--;
			}
			else if (pager->wrap_workspaces)
			{
				index = n_workspaces - 1;
			}
			break;
		default:
			g_assert_not_reached();
			break;
	}

	wnck_workspace_activate(wnck_screen_get_workspace(pager->screen, index), event->time);

	return TRUE;
}

static const GtkActionEntry pager_menu_actions[] = {
	{
		"PagerPreferences",
		"document-properties",
		N_("_Preferences"),
		NULL,
		NULL,
		G_CALLBACK(display_properties_dialog)
	},
	{
		"PagerHelp",
		"help-browser",
		N_("_Help"),
		NULL,
		NULL,
		G_CALLBACK(display_help_dialog)
	},
	{
		"PagerAbout",
		"help-about",
		N_("_About"),
		NULL,
		NULL,
		G_CALLBACK(display_about_dialog)
	}
};

static void num_rows_changed(GSettings* settings, gchar* key, PagerData* pager)
{
	int n_rows = DEFAULT_ROWS;

	n_rows = g_settings_get_int (settings, key);

	n_rows = CLAMP(n_rows, 1, MAX_REASONABLE_ROWS);

	pager->n_rows = n_rows;
	pager_update(pager);

	if (pager->num_rows_spin && gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pager->num_rows_spin)) != n_rows)
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(pager->num_rows_spin), pager->n_rows);
}

static void display_workspace_names_changed(GSettings* settings, gchar* key, PagerData* pager)
{
	gboolean value = FALSE; /* Default value */

	value = g_settings_get_boolean (settings, key);

	if (value)
	{
		pager->display_mode = WNCK_PAGER_DISPLAY_NAME;
	}
	else
	{
		pager->display_mode = WNCK_PAGER_DISPLAY_CONTENT;
	}

	pager_update(pager);

	if (pager->display_workspaces_toggle && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pager->display_workspaces_toggle)) != value)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->display_workspaces_toggle), value);
	}
}


static void all_workspaces_changed(GSettings* settings, gchar* key, PagerData* pager)
{
	gboolean value = TRUE; /* Default value */

	value = g_settings_get_boolean (settings, key);

	pager->display_all = value;
	pager_update(pager);

	if (pager->all_workspaces_radio)
	{
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pager->all_workspaces_radio)) != value)
		{
			if (value)
			{
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->all_workspaces_radio), TRUE);
			}
			else
			{
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->current_only_radio), TRUE);
			}
		}

		if (!g_object_get_data(G_OBJECT(pager->num_rows_spin), NEVER_SENSITIVE))
			gtk_widget_set_sensitive(pager->num_rows_spin, value);
	}
}

static void wrap_workspaces_changed(GSettings* settings, gchar* key, PagerData* pager)
{
	gboolean value = FALSE; /* Default value */

	value = g_settings_get_boolean (settings, key);

	pager->wrap_workspaces = value;

	if (pager->wrap_workspaces_toggle && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pager->wrap_workspaces_toggle)) != value)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->wrap_workspaces_toggle), value);
	}
}

static void setup_gsettings(PagerData* pager)
{
	pager->settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (pager->applet), WORKSPACE_SWITCHER_SCHEMA);

	g_signal_connect (pager->settings,
					  "changed::num-rows",
					  G_CALLBACK (num_rows_changed),
					  pager);
	g_signal_connect (pager->settings,
					  "changed::display-workspace-names",
					  G_CALLBACK (display_workspace_names_changed),
					  pager);
	g_signal_connect (pager->settings,
					  "changed::display-all-workspaces",
					  G_CALLBACK (all_workspaces_changed),
					  pager);
	g_signal_connect (pager->settings,
					  "changed::wrap-workspaces",
					  G_CALLBACK (wrap_workspaces_changed),
					  pager);

}

gboolean workspace_switcher_applet_fill(MatePanelApplet* applet)
{
	PagerData* pager;
	GtkActionGroup* action_group;
	gboolean display_names;

	pager = g_new0(PagerData, 1);

	pager->applet = GTK_WIDGET(applet);

	mate_panel_applet_set_flags(MATE_PANEL_APPLET(pager->applet), MATE_PANEL_APPLET_EXPAND_MINOR);

	setup_gsettings(pager);

	pager->n_rows = g_settings_get_int(pager->settings, "num-rows");

	pager->n_rows = CLAMP(pager->n_rows, 1, MAX_REASONABLE_ROWS);

	display_names = g_settings_get_boolean(pager->settings, "display-workspace-names");

	pager->wrap_workspaces = g_settings_get_boolean(pager->settings, "wrap-workspaces");

	if (display_names)
	{
		pager->display_mode = WNCK_PAGER_DISPLAY_NAME;
	}
	else
	{
		pager->display_mode = WNCK_PAGER_DISPLAY_CONTENT;
	}

	pager->display_all = g_settings_get_boolean(pager->settings, "display-all-workspaces");

	switch (mate_panel_applet_get_orient(applet))
	{
		case MATE_PANEL_APPLET_ORIENT_LEFT:
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			pager->orientation = GTK_ORIENTATION_VERTICAL;
			break;
		case MATE_PANEL_APPLET_ORIENT_UP:
		case MATE_PANEL_APPLET_ORIENT_DOWN:
		default:
			pager->orientation = GTK_ORIENTATION_HORIZONTAL;
			break;
	}

	pager->pager = wnck_pager_new();
	pager->screen = NULL;
	pager->wm = PAGER_WM_UNKNOWN;
	wnck_pager_set_shadow_type(WNCK_PAGER(pager->pager), GTK_SHADOW_IN);

	GtkStyleContext *context;
	context = gtk_widget_get_style_context (GTK_WIDGET (applet));
	gtk_style_context_add_class (context, "wnck-applet");
	context = gtk_widget_get_style_context (pager->pager);
	gtk_style_context_add_class (context, "wnck-pager");

	g_signal_connect(G_OBJECT(pager->pager), "destroy", G_CALLBACK(destroy_pager), pager);

	/* overwrite default WnckPager widget scroll-event */
	g_signal_connect(G_OBJECT(pager->pager), "scroll-event", G_CALLBACK(applet_scroll), pager);

	gtk_container_add(GTK_CONTAINER(pager->applet), pager->pager);

	g_signal_connect(G_OBJECT(pager->applet), "realize", G_CALLBACK(applet_realized), pager);
	g_signal_connect(G_OBJECT(pager->applet), "unrealize", G_CALLBACK(applet_unrealized), pager);
	g_signal_connect(G_OBJECT(pager->applet), "change_orient", G_CALLBACK(applet_change_orient), pager);
	g_signal_connect(G_OBJECT(pager->applet), "change_background", G_CALLBACK(applet_change_background), pager);
	g_signal_connect(G_OBJECT(pager->applet), "style-updated", G_CALLBACK(applet_style_updated), context);

	gtk_widget_show(pager->pager);
	gtk_widget_show(pager->applet);

	mate_panel_applet_set_background_widget(MATE_PANEL_APPLET(pager->applet), GTK_WIDGET(pager->applet));

	action_group = gtk_action_group_new("WorkspaceSwitcher Applet Actions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, pager_menu_actions, G_N_ELEMENTS(pager_menu_actions), pager);
	mate_panel_applet_setup_menu_from_resource (MATE_PANEL_APPLET (pager->applet),
	                                            WNCKLET_RESOURCE_PATH "workspace-switcher-menu.xml",
	                                            action_group);

	if (mate_panel_applet_get_locked_down(MATE_PANEL_APPLET(pager->applet)))
	{
		GtkAction *action;

		action = gtk_action_group_get_action(action_group, "PagerPreferences");
		gtk_action_set_visible(action, FALSE);
	}

	g_object_unref(action_group);

	return TRUE;
}


static void display_help_dialog(GtkAction* action, PagerData* pager)
{
	wncklet_display_help(pager->applet, "mate-user-guide", "overview-workspaces", WORKSPACE_SWITCHER_ICON);
}

static void display_about_dialog(GtkAction* action, PagerData* pager)
{
	static const gchar* authors[] = {
		"Perberos <perberos@gmail.com>",
		"Steve Zesch <stevezesch2@gmail.com>",
		"Stefano Karapetsas <stefano@karapetsas.com>",
		"Alexander Larsson <alla@lysator.liu.se>",
		NULL
	};

	const char* documenters[] = {
		"John Fleck <jfleck@inkstain.net>",
		"Sun GNOME Documentation Team <gdocteam@sun.com>",
		NULL
	};

	gtk_show_about_dialog(GTK_WINDOW(pager->applet),
		"program-name", _("Workspace Switcher"),
		"title", _("About Workspace Switcher"),
		"authors", authors,
		"comments", _("The Workspace Switcher shows you a small version of your workspaces that lets you manage your windows."),
		"copyright", _("Copyright \xc2\xa9 2002 Red Hat, Inc.\n"
                               "Copyright \xc2\xa9 2011 Perberos\n"
                               "Copyright \xc2\xa9 2012-2020 MATE developers"),
		"documenters", documenters,
		"icon-name", WORKSPACE_SWITCHER_ICON,
		"logo-icon-name", WORKSPACE_SWITCHER_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		"website", "http://www.mate-desktop.org/",
		NULL);
}

static void wrap_workspaces_toggled(GtkToggleButton* button, PagerData* pager)
{
	g_settings_set_boolean(pager->settings, "wrap-workspaces", gtk_toggle_button_get_active(button));
}

static void display_workspace_names_toggled(GtkToggleButton* button, PagerData* pager)
{
	g_settings_set_boolean(pager->settings, "display-workspace-names", gtk_toggle_button_get_active(button));
}

static void all_workspaces_toggled(GtkToggleButton* button, PagerData* pager)
{
	g_settings_set_boolean(pager->settings, "display-all-workspaces", gtk_toggle_button_get_active(button));
}

static void num_rows_value_changed(GtkSpinButton* button, PagerData* pager)
{
	g_settings_set_int(pager->settings, "num-rows", gtk_spin_button_get_value_as_int(button));
}

static void update_workspaces_model(PagerData* pager)
{
	int nr_ws, i;
	WnckWorkspace* workspace;
	GtkTreeIter iter;

	nr_ws = wnck_screen_get_workspace_count(pager->screen);

	if (pager->properties_dialog)
	{
		if (nr_ws != gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pager->num_workspaces_spin)))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(pager->num_workspaces_spin), nr_ws);

		gtk_list_store_clear(pager->workspaces_store);

		for (i = 0; i < nr_ws; i++)
		{
			workspace = wnck_screen_get_workspace(pager->screen, i);
			gtk_list_store_append(pager->workspaces_store, &iter);
			gtk_list_store_set(pager->workspaces_store, &iter, 0, wnck_workspace_get_name(workspace), -1);
		}
	}
}

static void workspace_renamed(WnckWorkspace* space, PagerData* pager)
{
	int i;
	GtkTreeIter iter;

	i = wnck_workspace_get_number(space);

	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(pager->workspaces_store), &iter, NULL, i))
		gtk_list_store_set(pager->workspaces_store, &iter, 0, wnck_workspace_get_name(space), -1);
}

static void workspace_created(WnckScreen* screen, WnckWorkspace* space, PagerData* pager)
{
	g_return_if_fail(WNCK_IS_SCREEN(screen));

	update_workspaces_model(pager);

	wncklet_connect_while_alive(space, "name_changed", G_CALLBACK(workspace_renamed), pager, pager->properties_dialog);
}

static void workspace_destroyed(WnckScreen* screen, WnckWorkspace* space, PagerData* pager)
{
	g_return_if_fail(WNCK_IS_SCREEN(screen));
	update_workspaces_model(pager);
}

static void num_workspaces_value_changed(GtkSpinButton* button, PagerData* pager)
{
	wnck_screen_change_workspace_count(pager->screen, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pager->num_workspaces_spin)));
}

static gboolean workspaces_tree_focused_out(GtkTreeView* treeview, GdkEventFocus* event, PagerData* pager)
{
	GtkTreeSelection* selection;

	selection = gtk_tree_view_get_selection(treeview);
	gtk_tree_selection_unselect_all(selection);
	return TRUE;
}

static void workspace_name_edited(GtkCellRendererText* cell_renderer_text, const gchar* path, const gchar* new_text, PagerData* pager)
{
	const gint* indices;
	WnckWorkspace* workspace;
	GtkTreePath* p;

	p = gtk_tree_path_new_from_string(path);
	indices = gtk_tree_path_get_indices(p);
	workspace = wnck_screen_get_workspace(pager->screen, indices[0]);

	if (workspace != NULL)
	{
		gchar* temp_name = g_strdup(new_text);

		wnck_workspace_change_name(workspace, g_strstrip(temp_name));

		g_free(temp_name);
	}
	else
	{
		g_warning("Edited name of workspace %d which no longer exists", indices[0]);
	}

	gtk_tree_path_free(p);
}

static void properties_dialog_destroyed(GtkWidget* widget, PagerData* pager)
{
	pager->properties_dialog = NULL;
	pager->workspaces_frame = NULL;
	pager->workspace_names_label = NULL;
	pager->workspace_names_scroll = NULL;
	pager->display_workspaces_toggle = NULL;
	pager->wrap_workspaces_toggle = NULL;
	pager->all_workspaces_radio = NULL;
	pager->current_only_radio = NULL;
	pager->num_rows_spin = NULL;
	pager->label_row_col = NULL;
	pager->num_workspaces_spin = NULL;
	pager->workspaces_tree = NULL;
	pager->workspaces_store = NULL;
}

static gboolean delete_event(GtkWidget* widget, gpointer data)
{
	gtk_widget_destroy(widget);
	return TRUE;
}

static void response_cb(GtkWidget* widget, int id, PagerData* pager)
{
	if (id == GTK_RESPONSE_HELP)
		wncklet_display_help(widget, "mate-user-guide", "overview-workspaces", WORKSPACE_SWITCHER_ICON);
	else
		gtk_widget_destroy(widget);
}

static void close_dialog(GtkWidget* button, gpointer data)
{
	PagerData* pager = data;
	GtkTreeViewColumn* col;
	GtkCellArea *area;
	GtkCellEditable *edit_widget;

	/* This is a hack. The "editable" signal for GtkCellRenderer is emitted
	only on button press or focus cycle. Hence when the user changes the
	name and closes the preferences dialog without a button-press he would
	lose the name changes. So, we call the gtk_cell_editable_editing_done
	to stop the editing. Thanks to Paolo for a better crack than the one I had.
	*/

	col = gtk_tree_view_get_column(GTK_TREE_VIEW(pager->workspaces_tree), 0);

	area = gtk_cell_layout_get_area (GTK_CELL_LAYOUT (col));
	edit_widget = gtk_cell_area_get_edit_widget (area);
	if (edit_widget)
		gtk_cell_editable_editing_done (edit_widget);

	gtk_widget_destroy(pager->properties_dialog);
}

#define WID(s) GTK_WIDGET(gtk_builder_get_object(builder, s))

static void
setup_sensitivity(PagerData* pager, GtkBuilder* builder, const char* wid1, const char* wid2, const char* wid3, GSettings* settings, const char* key)
{
	GtkWidget* w;

	if ((settings != NULL) && g_settings_is_writable(settings, key))
	{
		return;
	}

	w = WID(wid1);
	g_assert(w != NULL);
	g_object_set_data(G_OBJECT(w), NEVER_SENSITIVE, GINT_TO_POINTER(1));
	gtk_widget_set_sensitive(w, FALSE);

	if (wid2 != NULL)
	{
		w = WID(wid2);
		g_assert(w != NULL);
		g_object_set_data(G_OBJECT(w), NEVER_SENSITIVE, GINT_TO_POINTER(1));
		gtk_widget_set_sensitive(w, FALSE);
	}

	if (wid3 != NULL)
	{
		w = WID(wid3);
		g_assert(w != NULL);
		g_object_set_data(G_OBJECT(w), NEVER_SENSITIVE, GINT_TO_POINTER(1));
		gtk_widget_set_sensitive(w, FALSE);
	}
}

static void setup_dialog(GtkBuilder* builder, PagerData* pager)
{
	gboolean value;
	GtkTreeViewColumn* column;
	GtkCellRenderer* cell;
	int nr_ws, i;
	GSettings *marco_general_settings = NULL;
	GSettings *marco_workspaces_settings = NULL;

	if (mate_gsettings_schema_exists(MARCO_GENERAL_SCHEMA))
		marco_general_settings = g_settings_new (MARCO_GENERAL_SCHEMA);
	if (mate_gsettings_schema_exists(MARCO_WORKSPACES_SCHEMA))
		marco_workspaces_settings = g_settings_new (MARCO_WORKSPACES_SCHEMA);

	pager->workspaces_frame = WID("workspaces_frame");
	pager->workspace_names_label = WID("workspace_names_label");
	pager->workspace_names_scroll = WID("workspace_names_scroll");

	pager->display_workspaces_toggle = WID("workspace_name_toggle");
	setup_sensitivity(pager, builder, "workspace_name_toggle", NULL, NULL, pager->settings, "display-workspace-names" /* key */);

	pager->wrap_workspaces_toggle = WID("workspace_wrap_toggle");
	setup_sensitivity(pager, builder, "workspace_wrap_toggle", NULL, NULL, pager->settings, "wrap-workspaces" /* key */);

	pager->all_workspaces_radio = WID("all_workspaces_radio");
	pager->current_only_radio = WID("current_only_radio");
	setup_sensitivity(pager, builder, "all_workspaces_radio", "current_only_radio", "label_row_col", pager->settings, "display-all-workspaces" /* key */);

	pager->num_rows_spin = WID("num_rows_spin");
	pager->label_row_col = WID("label_row_col");
	setup_sensitivity(pager, builder, "num_rows_spin", NULL, NULL, pager->settings, "num-rows" /* key */);

	pager->num_workspaces_spin = WID("num_workspaces_spin");
	setup_sensitivity(pager, builder, "num_workspaces_spin", NULL, NULL, marco_general_settings, NUM_WORKSPACES /* key */);

	pager->workspaces_tree = WID("workspaces_tree_view");
	setup_sensitivity(pager, builder, "workspaces_tree_view", NULL, NULL, marco_workspaces_settings, WORKSPACE_NAME /* key */);

	if (marco_general_settings != NULL)
		g_object_unref (marco_general_settings);
	if (marco_workspaces_settings != NULL)
		g_object_unref (marco_workspaces_settings);


	/* Wrap workspaces: */
	if (pager->wrap_workspaces_toggle)
	{
		/* make sure the toggle button resembles the value of wrap_workspaces */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->wrap_workspaces_toggle), pager->wrap_workspaces);
	}

	g_signal_connect(G_OBJECT(pager->wrap_workspaces_toggle), "toggled", (GCallback) wrap_workspaces_toggled, pager);

	/* Display workspace names: */

	g_signal_connect(G_OBJECT(pager->display_workspaces_toggle), "toggled", (GCallback) display_workspace_names_toggled, pager);

	if (pager->display_mode == WNCK_PAGER_DISPLAY_NAME)
	{
		value = TRUE;
	}
	else
	{
		value = FALSE;
	}

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->display_workspaces_toggle), value);

	/* Display all workspaces: */
	g_signal_connect(G_OBJECT(pager->all_workspaces_radio), "toggled", (GCallback) all_workspaces_toggled, pager);

	if (pager->display_all)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->all_workspaces_radio), TRUE);

		if (!g_object_get_data(G_OBJECT(pager->num_rows_spin), NEVER_SENSITIVE))
		{
			gtk_widget_set_sensitive(pager->num_rows_spin, TRUE);
		}
	}
	else
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pager->current_only_radio), TRUE);
		gtk_widget_set_sensitive(pager->num_rows_spin, FALSE);
	}

	/* Num rows: */
	g_signal_connect(G_OBJECT(pager->num_rows_spin), "value_changed", (GCallback) num_rows_value_changed, pager);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(pager->num_rows_spin), pager->n_rows);
	gtk_label_set_text(GTK_LABEL(pager->label_row_col), pager->orientation == GTK_ORIENTATION_HORIZONTAL ? _("rows") : _("columns"));

	g_signal_connect(pager->properties_dialog, "destroy", G_CALLBACK(properties_dialog_destroyed), pager);
	g_signal_connect(pager->properties_dialog, "delete_event", G_CALLBACK(delete_event), pager);
	g_signal_connect(pager->properties_dialog, "response", G_CALLBACK(response_cb), pager);

	g_signal_connect(WID("done_button"), "clicked", (GCallback) close_dialog, pager);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(pager->num_workspaces_spin), wnck_screen_get_workspace_count(pager->screen));
	g_signal_connect(G_OBJECT(pager->num_workspaces_spin), "value_changed", (GCallback) num_workspaces_value_changed, pager);

	wncklet_connect_while_alive(pager->screen, "workspace_created", G_CALLBACK(workspace_created), pager, pager->properties_dialog);

	wncklet_connect_while_alive(pager->screen, "workspace_destroyed", G_CALLBACK(workspace_destroyed), pager, pager->properties_dialog);

	g_signal_connect(G_OBJECT(pager->workspaces_tree), "focus_out_event", (GCallback) workspaces_tree_focused_out, pager);

	pager->workspaces_store = gtk_list_store_new(1, G_TYPE_STRING, NULL);
	update_workspaces_model(pager);
	gtk_tree_view_set_model(GTK_TREE_VIEW(pager->workspaces_tree), GTK_TREE_MODEL(pager->workspaces_store));

	g_object_unref(pager->workspaces_store);

	cell = g_object_new(GTK_TYPE_CELL_RENDERER_TEXT, "editable", TRUE, NULL);
	pager->cell = cell;
	column = gtk_tree_view_column_new_with_attributes("workspace", cell, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pager->workspaces_tree), column);
	g_signal_connect(cell, "edited", (GCallback) workspace_name_edited, pager);

	nr_ws = wnck_screen_get_workspace_count(pager->screen);

	for (i = 0; i < nr_ws; i++)
	{
		wncklet_connect_while_alive(G_OBJECT(wnck_screen_get_workspace(pager->screen, i)), "name_changed", G_CALLBACK(workspace_renamed), pager, pager->properties_dialog);
	}

	update_properties_for_wm(pager);
}

static void display_properties_dialog(GtkAction* action, PagerData* pager)
{
	if (pager->properties_dialog == NULL)
	{
		GtkBuilder* builder;

		builder = gtk_builder_new();
		gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);
		gtk_builder_add_from_resource (builder, WNCKLET_RESOURCE_PATH "workspace-switcher.ui", NULL);

		pager->properties_dialog = WID("pager_properties_dialog");

		g_object_add_weak_pointer(G_OBJECT(pager->properties_dialog), (gpointer*) &pager->properties_dialog);

		setup_dialog(builder, pager);

		g_object_unref(builder);
	}

	gtk_window_set_icon_name(GTK_WINDOW(pager->properties_dialog), WORKSPACE_SWITCHER_ICON);
	gtk_window_set_screen(GTK_WINDOW(pager->properties_dialog), gtk_widget_get_screen(pager->applet));
	gtk_window_present(GTK_WINDOW(pager->properties_dialog));
}

static void destroy_pager(GtkWidget* widget, PagerData* pager)
{
	g_signal_handlers_disconnect_by_data (pager->settings, pager);

	g_object_unref (pager->settings);

	if (pager->properties_dialog)
		gtk_widget_destroy(pager->properties_dialog);
	g_free(pager);
}
