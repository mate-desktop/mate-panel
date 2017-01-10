/* -*- mode: C; c-file-style: "linux" -*- */
/*
 * libwnck based tasklist applet.
 * (C) 2001 Red Hat, Inc
 * (C) 2001 Alexander Larsson
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#include <gio/gio.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-desktop-utils.h>

#include "wncklet.h"
#include "window-list.h"

#define WINDOW_LIST_ICON "mate-panel-window-list"
#define WINDOW_LIST_SCHEMA "org.mate.panel.applet.window-list"

typedef struct {
	GtkWidget* applet;
	GtkWidget* tasklist;

	gboolean include_all_workspaces;
	WnckTasklistGroupingType grouping;
	gboolean move_unminimized_windows;

	GtkOrientation orientation;
	int size;

	GtkIconTheme* icon_theme;

	/* Properties: */
	GtkWidget* properties_dialog;
	GtkWidget* show_current_radio;
	GtkWidget* show_all_radio;
	GtkWidget* never_group_radio;
	GtkWidget* auto_group_radio;
	GtkWidget* always_group_radio;
	GtkWidget* minimized_windows_label;
	GtkWidget* move_minimized_radio;
	GtkWidget* change_workspace_radio;

	GSettings* settings;
} TasklistData;

static void call_system_monitor(GtkAction* action, TasklistData* tasklist);
static void display_properties_dialog(GtkAction* action, TasklistData* tasklist);
static void display_help_dialog(GtkAction* action, TasklistData* tasklist);
static void display_about_dialog(GtkAction* action, TasklistData* tasklist);

static void tasklist_update(TasklistData* tasklist)
{
	if (tasklist->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		gtk_widget_set_size_request(GTK_WIDGET(tasklist->tasklist), -1, tasklist->size);
	}
	else
	{
		gtk_widget_set_size_request(GTK_WIDGET(tasklist->tasklist), tasklist->size, -1);
	}

	wnck_tasklist_set_grouping(WNCK_TASKLIST(tasklist->tasklist), tasklist->grouping);
	wnck_tasklist_set_include_all_workspaces(WNCK_TASKLIST(tasklist->tasklist), tasklist->include_all_workspaces);
	wnck_tasklist_set_switch_workspace_on_unminimize(WNCK_TASKLIST(tasklist->tasklist), tasklist->move_unminimized_windows);
}

static void response_cb(GtkWidget* widget, int id, TasklistData* tasklist)
{
	if (id == GTK_RESPONSE_HELP)
	{
		wncklet_display_help(widget, "mate-user-guide", "windowlist-prefs", WINDOW_LIST_ICON);
	}
	else
	{
		gtk_widget_hide(widget);
	}
}

static void applet_realized(MatePanelApplet* applet, TasklistData* tasklist)
{
	tasklist->icon_theme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(tasklist->applet));
}

static void applet_change_orient(MatePanelApplet* applet, MatePanelAppletOrient orient, TasklistData* tasklist)
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

	if (new_orient == tasklist->orientation)
		return;

	tasklist->orientation = new_orient;

#if WNCK_CHECK_VERSION (3, 4, 6)
	wnck_tasklist_set_orientation (WNCK_TASKLIST (tasklist->tasklist), new_orient);
#endif
	tasklist_update(tasklist);
}

static void applet_change_background(MatePanelApplet* applet, MatePanelAppletBackgroundType type, GdkColor* color, cairo_pattern_t* pattern, TasklistData* tasklist)
{
	switch (type)
	{
		case PANEL_NO_BACKGROUND:
		case PANEL_COLOR_BACKGROUND:
		case PANEL_PIXMAP_BACKGROUND:
			wnck_tasklist_set_button_relief(WNCK_TASKLIST(tasklist->tasklist), GTK_RELIEF_NONE);
			break;
	}
}

static void applet_change_pixel_size(MatePanelApplet* applet, gint size, TasklistData* tasklist)
{
	if (tasklist->size == size)
		return;

	tasklist->size = size;

	tasklist_update(tasklist);
}

static void destroy_tasklist(GtkWidget* widget, TasklistData* tasklist)
{
	g_object_unref(tasklist->settings);

	if (tasklist->properties_dialog)
		gtk_widget_destroy(tasklist->properties_dialog);

	g_free(tasklist);
}

/* TODO: this is sad, should be used a function to retrieve  applications from
 *  .desktop or some like that. */
static const char* system_monitors[] = {
	"mate-system-monitor",
	"gnome-system-monitor",
};

static const GtkActionEntry tasklist_menu_actions[] = {
	{
		"TasklistSystemMonitor",
		"utilities-system-monitor",
		N_("_System Monitor"),
		NULL,
		NULL,
		G_CALLBACK(call_system_monitor)
	},
	{
		"TasklistPreferences",
		GTK_STOCK_PROPERTIES,
		N_("_Preferences"),
		NULL,
		NULL,
		G_CALLBACK(display_properties_dialog)
	},
	{
		"TasklistHelp",
		GTK_STOCK_HELP,
		N_("_Help"),
		NULL,
		NULL,
		G_CALLBACK(display_help_dialog)
	},
	{
		"TasklistAbout",
		GTK_STOCK_ABOUT,
		N_("_About"),
		NULL,
		NULL,
		G_CALLBACK(display_about_dialog)
	}
};

static void tasklist_properties_update_content_radio(TasklistData* tasklist)
{
	GtkWidget* button;

	if (tasklist->show_current_radio == NULL)
		return;

	if (tasklist->include_all_workspaces)
	{
		button = tasklist->show_all_radio;
	}
	else
	{
		button = tasklist->show_current_radio;
	}

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);

	gtk_widget_set_sensitive(tasklist->minimized_windows_label, tasklist->include_all_workspaces);
	gtk_widget_set_sensitive(tasklist->move_minimized_radio, tasklist->include_all_workspaces);
	gtk_widget_set_sensitive(tasklist->change_workspace_radio, tasklist->include_all_workspaces);
}

static void display_all_workspaces_changed(GSettings* settings, gchar* key, TasklistData* tasklist)
{
	gboolean value;

	value = g_settings_get_boolean(settings, key);

	tasklist->include_all_workspaces = (value != 0);
	tasklist_update(tasklist);

	tasklist_properties_update_content_radio(tasklist);
}

static GtkWidget* get_grouping_button(TasklistData* tasklist, WnckTasklistGroupingType type)
{
	switch (type)
	{
		default:
		case WNCK_TASKLIST_NEVER_GROUP:
			return tasklist->never_group_radio;
			break;
		case WNCK_TASKLIST_AUTO_GROUP:
			return tasklist->auto_group_radio;
			break;
		case WNCK_TASKLIST_ALWAYS_GROUP:
			return tasklist->always_group_radio;
			break;
	}
}

static void group_windows_changed(GSettings* settings, gchar* key, TasklistData* tasklist)
{
	WnckTasklistGroupingType type;
	GtkWidget* button;

	type = g_settings_get_enum (settings, key);

	tasklist->grouping = type;
	tasklist_update(tasklist);

	button = get_grouping_button(tasklist, type);

	if (button && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	}
}

static void tasklist_update_unminimization_radio(TasklistData* tasklist)
{
	GtkWidget* button;

	if (tasklist->move_minimized_radio == NULL)
		return;

	if (tasklist->move_unminimized_windows)
	{
		button = tasklist->move_minimized_radio;
	}
	else
	{
		button = tasklist->change_workspace_radio;
	}

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
}


static void move_unminimized_windows_changed(GSettings* settings, gchar* key, TasklistData* tasklist)
{
	gboolean value;

	value = g_settings_get_boolean(settings, key);

	tasklist->move_unminimized_windows = (value != 0);
	tasklist_update(tasklist);

	tasklist_update_unminimization_radio(tasklist);
}

static void setup_gsettings(TasklistData* tasklist)
{
	tasklist->settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (tasklist->applet), WINDOW_LIST_SCHEMA);

	g_signal_connect (tasklist->settings,
					  "changed::display-all-workspaces",
					  G_CALLBACK (display_all_workspaces_changed),
					  tasklist);
	g_signal_connect (tasklist->settings,
					  "changed::group-windows",
					  G_CALLBACK (group_windows_changed),
					  tasklist);
	g_signal_connect (tasklist->settings,
					  "changed::move-unminimized-windows",
					  G_CALLBACK (move_unminimized_windows_changed),
					  tasklist);
}

static void applet_size_allocate(GtkWidget *widget, GtkAllocation *allocation, TasklistData *tasklist)
{
	int len;
	const int* size_hints;

	size_hints = wnck_tasklist_get_size_hint_list (WNCK_TASKLIST (tasklist->tasklist), &len);

	g_assert(len % 2 == 0);

	mate_panel_applet_set_size_hints(MATE_PANEL_APPLET(tasklist->applet), size_hints, len, 0);
}

static GdkPixbuf* icon_loader_func(const char* icon, int size, unsigned int flags, void* data)
{
	TasklistData* tasklist;
	GdkPixbuf* retval;
	char* icon_no_extension;
	char* p;

	tasklist = data;

	if (icon == NULL || strcmp(icon, "") == 0)
		return NULL;

	if (g_path_is_absolute(icon))
	{
		if (g_file_test(icon, G_FILE_TEST_EXISTS))
		{
			return gdk_pixbuf_new_from_file_at_size(icon, size, size, NULL);
		}
		else
		{
			char* basename;

			basename = g_path_get_basename(icon);
			retval = icon_loader_func(basename, size, flags, data);
			g_free(basename);

			return retval;
		}
	}

	/* This is needed because some .desktop files have an icon name *and*
	* an extension as icon */
	icon_no_extension = g_strdup(icon);
	p = strrchr(icon_no_extension, '.');

	if (p && (strcmp(p, ".png") == 0 || strcmp(p, ".xpm") == 0 || strcmp(p, ".svg") == 0))
	{
		*p = 0;
	}

	retval = gtk_icon_theme_load_icon(tasklist->icon_theme, icon_no_extension, size, 0, NULL);
	g_free(icon_no_extension);

	return retval;
}

gboolean window_list_applet_fill(MatePanelApplet* applet)
{
	TasklistData* tasklist;
	GtkActionGroup* action_group;
	gchar* ui_path;
	GtkCssProvider  *provider;
	GdkScreen *screen;

	tasklist = g_new0(TasklistData, 1);

	tasklist->applet = GTK_WIDGET(applet);

	provider = gtk_css_provider_new ();
	screen = gdk_screen_get_default ();
	gtk_css_provider_load_from_data (provider,
										".mate-panel-menu-bar button,\n"
										" #tasklist-button {\n"
										" padding: 0px;\n"
										" margin: 0px;\n }",
										-1, NULL);
	gtk_style_context_add_provider_for_screen (screen,
										GTK_STYLE_PROVIDER (provider),
										GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);

	mate_panel_applet_set_flags(MATE_PANEL_APPLET(tasklist->applet), MATE_PANEL_APPLET_EXPAND_MAJOR | MATE_PANEL_APPLET_EXPAND_MINOR | MATE_PANEL_APPLET_HAS_HANDLE);

	setup_gsettings(tasklist);

	tasklist->include_all_workspaces = g_settings_get_boolean (tasklist->settings, "display-all-workspaces");

	tasklist->grouping = g_settings_get_enum (tasklist->settings, "group-windows");

	tasklist->move_unminimized_windows = g_settings_get_boolean (tasklist->settings, "move-unminimized-windows");

	tasklist->size = mate_panel_applet_get_size(applet);

	switch (mate_panel_applet_get_orient(applet))
	{
		case MATE_PANEL_APPLET_ORIENT_LEFT:
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			tasklist->orientation = GTK_ORIENTATION_VERTICAL;
			break;
		case MATE_PANEL_APPLET_ORIENT_UP:
		case MATE_PANEL_APPLET_ORIENT_DOWN:
		default:
			tasklist->orientation = GTK_ORIENTATION_HORIZONTAL;
			break;
	}

	tasklist->tasklist = wnck_tasklist_new();

#if WNCK_CHECK_VERSION (3, 4, 6)
	wnck_tasklist_set_orientation (WNCK_TASKLIST (tasklist->tasklist), tasklist->orientation);
	wnck_tasklist_set_middle_click_close (WNCK_TASKLIST (tasklist->tasklist), TRUE);
#endif

	wnck_tasklist_set_icon_loader(WNCK_TASKLIST(tasklist->tasklist), icon_loader_func, tasklist, NULL);

	g_signal_connect(G_OBJECT(tasklist->tasklist), "destroy", G_CALLBACK(destroy_tasklist), tasklist);

	g_signal_connect(G_OBJECT(tasklist->applet), "size_allocate", G_CALLBACK(applet_size_allocate), tasklist);
	tasklist_update(tasklist);
	gtk_widget_show(tasklist->tasklist);

	gtk_container_add(GTK_CONTAINER(tasklist->applet), tasklist->tasklist);

	g_signal_connect(G_OBJECT(tasklist->applet), "realize", G_CALLBACK(applet_realized), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_orient", G_CALLBACK(applet_change_orient), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_size", G_CALLBACK(applet_change_pixel_size), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_background", G_CALLBACK(applet_change_background), tasklist);

	mate_panel_applet_set_background_widget(MATE_PANEL_APPLET(tasklist->applet), GTK_WIDGET(tasklist->applet));

	action_group = gtk_action_group_new("Tasklist Applet Actions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, tasklist_menu_actions, G_N_ELEMENTS(tasklist_menu_actions), tasklist);


	/* disable the item of system monitor, if not exists.
	 * example, mate-system-monitor, o gnome-system-monitor */
	char* programpath;
	int i;

	for (i = 0; i < G_N_ELEMENTS(system_monitors); i += 1)
	{
		programpath = g_find_program_in_path(system_monitors[i]);
		
		if (programpath != NULL)
		{
			g_free(programpath);
			/* we give up */
			goto _system_monitor_found;
		}
		
		/* search another */
	}

	/* system monitor not found */
	gtk_action_set_visible(gtk_action_group_get_action(action_group, "TasklistSystemMonitor"), FALSE);
	
	_system_monitor_found:;
	/* end of system monitor item */
	

	ui_path = g_build_filename(WNCK_MENU_UI_DIR, "window-list-menu.xml", NULL);
	mate_panel_applet_setup_menu_from_file(MATE_PANEL_APPLET(tasklist->applet), ui_path, action_group);
	g_free(ui_path);

	if (mate_panel_applet_get_locked_down(MATE_PANEL_APPLET(tasklist->applet)))
	{
		GtkAction* action;

		action = gtk_action_group_get_action(action_group, "TasklistPreferences");
		gtk_action_set_visible(action, FALSE);
	}

	g_object_unref(action_group);

	gtk_widget_show(tasklist->applet);

	return TRUE;
}

static void call_system_monitor(GtkAction* action, TasklistData* tasklist)
{
	char *programpath;
	int i;

	for (i = 0; i < G_N_ELEMENTS(system_monitors); i += 1)
	{
		programpath = g_find_program_in_path(system_monitors[i]);

		if (programpath != NULL)
		{
			g_free(programpath);

			mate_gdk_spawn_command_line_on_screen(gtk_widget_get_screen(tasklist->applet),
				      system_monitors[i],
				      NULL);
			return;
		}
	}
}


static void display_help_dialog(GtkAction* action, TasklistData* tasklist)
{
	wncklet_display_help(tasklist->applet, "mate-user-guide", "windowlist", WINDOW_LIST_ICON);
}

static void display_about_dialog(GtkAction* action, TasklistData* tasklist)
{
	static const gchar* authors[] = {
		"Perberos <perberos@gmail.com>",
		"Steve Zesch <stevezesch2@gmail.com>",
		"Stefano Karapetsas <stefano@karapetsas.com>",
		"Alexander Larsson <alla@lysator.liu.se>",
		NULL
	};

	const char* documenters [] = {
		"Sun GNOME Documentation Team <gdocteam@sun.com>",
		NULL
	};

	char copyright[] = \
		"Copyright \xc2\xa9 2012-2016 MATE developers\n"
		"Copyright \xc2\xa9 2011 Perberos\n"
		"Copyright \xc2\xa9 2002 Red Hat, Inc.";

	gtk_show_about_dialog(GTK_WINDOW(tasklist->applet),
		"program-name", _("Window List"),
		"authors", authors,
		"comments", _("The Window List shows a list of all windows in a set of buttons and lets you browse them."),
		"copyright", copyright,
		"documenters", documenters,
		"icon-name", WINDOW_LIST_ICON,
		"logo-icon-name", WINDOW_LIST_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		"website", "http://www.mate-desktop.org/",
		NULL);
}

static void group_windows_toggled(GtkToggleButton* button, TasklistData* tasklist)
{
	if (gtk_toggle_button_get_active(button))
	{
		gchar *value;
		value = g_object_get_data (G_OBJECT (button), "group_value");
		g_settings_set_string (tasklist->settings, "group-windows", value);
	}
}

static void move_minimized_toggled(GtkToggleButton* button, TasklistData* tasklist)
{
	g_settings_set_boolean(tasklist->settings, "move-unminimized-windows", gtk_toggle_button_get_active(button));
}

static void display_all_workspaces_toggled(GtkToggleButton* button, TasklistData* tasklist)
{
	g_settings_set_boolean(tasklist->settings, "display-all-workspaces", gtk_toggle_button_get_active(button));
}

#define WID(s) GTK_WIDGET(gtk_builder_get_object(builder, s))

static void setup_sensitivity(TasklistData* tasklist, GtkBuilder* builder, const char* wid1, const char* wid2, const char* wid3, const char* key)
{
	GtkWidget* w;

	if (g_settings_is_writable(tasklist->settings, key))
	{
		return;
	}

	w = WID(wid1);
	g_assert(w != NULL);
	gtk_widget_set_sensitive(w, FALSE);

	if (wid2 != NULL)
	{
		w = WID(wid2);
		g_assert(w != NULL);
		gtk_widget_set_sensitive(w, FALSE);
	}

	if (wid3 != NULL)
	{
		w = WID(wid3);
		g_assert(w != NULL);
		gtk_widget_set_sensitive(w, FALSE);
	}
}

static void setup_dialog(GtkBuilder* builder, TasklistData* tasklist)
{
	GtkWidget* button;

	tasklist->show_current_radio = WID("show_current_radio");
	tasklist->show_all_radio = WID("show_all_radio");

	setup_sensitivity(tasklist, builder, "show_current_radio", "show_all_radio", NULL, "display-all-workspaces" /* key */);

	tasklist->never_group_radio = WID("never_group_radio");
	tasklist->auto_group_radio = WID("auto_group_radio");
	tasklist->always_group_radio = WID("always_group_radio");

	setup_sensitivity(tasklist, builder, "never_group_radio", "auto_group_radio", "always_group_radio", "group-windows" /* key */);

	tasklist->minimized_windows_label = WID("minimized_windows_label");
	tasklist->move_minimized_radio = WID("move_minimized_radio");
	tasklist->change_workspace_radio = WID("change_workspace_radio");

	setup_sensitivity(tasklist, builder, "move_minimized_radio", "change_workspace_radio", NULL, "move-unminimized-windows" /* key */);

	/* Window grouping: */
	button = get_grouping_button(tasklist, tasklist->grouping);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	g_object_set_data(G_OBJECT(tasklist->never_group_radio), "group_value", "never");
	g_object_set_data(G_OBJECT(tasklist->auto_group_radio), "group_value", "auto");
	g_object_set_data(G_OBJECT(tasklist->always_group_radio), "group_value", "always");

	g_signal_connect(G_OBJECT(tasklist->never_group_radio), "toggled", (GCallback) group_windows_toggled, tasklist);
	g_signal_connect(G_OBJECT(tasklist->auto_group_radio), "toggled", (GCallback) group_windows_toggled, tasklist);
	g_signal_connect(G_OBJECT(tasklist->always_group_radio), "toggled", (GCallback) group_windows_toggled, tasklist);

	/* move window when unminimizing: */
	tasklist_update_unminimization_radio(tasklist);
	g_signal_connect(G_OBJECT(tasklist->move_minimized_radio), "toggled", (GCallback) move_minimized_toggled, tasklist);

	/* Tasklist content: */
	tasklist_properties_update_content_radio (tasklist);
	g_signal_connect(G_OBJECT(tasklist->show_all_radio), "toggled", (GCallback) display_all_workspaces_toggled, tasklist);

	g_signal_connect_swapped(WID("done_button"), "clicked", (GCallback) gtk_widget_hide, tasklist->properties_dialog);
	g_signal_connect(tasklist->properties_dialog, "response", G_CALLBACK(response_cb), tasklist);
}

static void display_properties_dialog(GtkAction* action, TasklistData* tasklist)
{
	if (tasklist->properties_dialog == NULL)
	{
		GtkBuilder* builder;
		GError* error;

		builder = gtk_builder_new();
		gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

		error = NULL;
		gtk_builder_add_from_file(builder, TASKLIST_BUILDERDIR "/window-list.ui", &error);

		if (error)
		{
			g_warning("Error loading preferences: %s", error->message);
			g_error_free(error);
			return;
		}

		tasklist->properties_dialog = WID("tasklist_properties_dialog");

		g_object_add_weak_pointer(G_OBJECT(tasklist->properties_dialog), (void**) &tasklist->properties_dialog);

		setup_dialog(builder, tasklist);

		g_object_unref(builder);
	}

	gtk_window_set_icon_name(GTK_WINDOW(tasklist->properties_dialog), WINDOW_LIST_ICON);

	gtk_window_set_resizable(GTK_WINDOW(tasklist->properties_dialog), FALSE);
	gtk_window_set_screen(GTK_WINDOW(tasklist->properties_dialog), gtk_widget_get_screen(tasklist->applet));
	gtk_window_present(GTK_WINDOW(tasklist->properties_dialog));
}
