/* -*- mode: C; c-file-style: "linux" -*- */
/*
 * libwnck based tasklist applet.
 * (C) 2001 Red Hat, Inc
 * (C) 2001 Alexander Larsson
 *
 * Authors: Alexander Larsson
 *
 */

#include <config.h>

#include <string.h>

#include <mate-panel-applet.h>
#include <mate-panel-applet-gsettings.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#endif /* HAVE_X11 */

#ifdef HAVE_WAYLAND
#include <gdk/gdkwayland.h>
#include "wayland-backend.h"
#endif /* HAVE_WAYLAND */

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-desktop-utils.h>

#include "wncklet.h"
#include "window-list.h"

#define WINDOW_LIST_ICON "mate-panel-window-list"
#define WINDOW_LIST_SCHEMA "org.mate.panel.applet.window-list"

#ifdef HAVE_WINDOW_PREVIEWS
#define WINDOW_LIST_PREVIEW_SCHEMA "org.mate.panel.applet.window-list-previews"
#endif /* HAVE_WINDOW_PREVIEWS */

enum {
  TASKLIST_NEVER_GROUP,
  TASKLIST_AUTO_GROUP,
  TASKLIST_ALWAYS_GROUP
};

typedef struct {
	GtkWidget* applet;
	GtkWidget* tasklist;
#ifdef HAVE_WINDOW_PREVIEWS
	GtkWidget* preview;
#endif

	GtkOrientation orientation;
	int size;
#if !defined(WNCKLET_INPROCESS) && !GTK_CHECK_VERSION (3, 23, 0)
	gboolean needs_hints;
#endif

	GtkIconTheme* icon_theme;

	/* Properties: */
	GtkWidget* properties_dialog;
	GtkWidget* display_all_workspaces_radio;
#ifdef HAVE_WINDOW_PREVIEWS
	GtkWidget* window_thumbnail_box;
	GtkWidget* show_thumbnails_check;
	GtkWidget* thumbnail_size_label;
	GtkWidget* thumbnail_size_spin;
	GtkWidget* thumbnail_box;
#endif
	GtkWidget* never_group_radio;
	GtkWidget* auto_group_radio;
	GtkWidget* always_group_radio;
	GtkWidget* restore_to_current_workspace_radio;
	GtkWidget* mouse_scroll_check;
	GtkWidget* minimized_windows_box;
	GtkWidget* window_grouping_box;
	GtkWidget* window_list_content_box;

	GSettings* settings;
#ifdef HAVE_WINDOW_PREVIEWS
	GSettings* preview_settings;
#endif
} TasklistData;

static void call_system_monitor(GtkAction* action, TasklistData* tasklist);
static void display_properties_dialog(GtkAction* action, TasklistData* tasklist);
static void display_help_dialog(GtkAction* action, TasklistData* tasklist);
static void display_about_dialog(GtkAction* action, TasklistData* tasklist);
static void destroy_tasklist(GtkWidget* widget, TasklistData* tasklist);

static void tasklist_set_size_request(TasklistData* tasklist)
{
	if (tasklist->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		gtk_widget_set_size_request(GTK_WIDGET(tasklist->tasklist), -1, tasklist->size);
	}
	else
	{
		gtk_widget_set_size_request(GTK_WIDGET(tasklist->tasklist), tasklist->size, -1);
	}
}

static void tasklist_apply_orientation(TasklistData* tasklist)
{
#ifdef HAVE_X11
	if (WNCK_IS_TASKLIST(tasklist->tasklist))
	{
		wnck_tasklist_set_orientation(WNCK_TASKLIST(tasklist->tasklist), tasklist->orientation);
	}
#endif /* HAVE_X11 */

#ifdef HAVE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()))
	{
		wayland_tasklist_set_orientation(tasklist->tasklist, tasklist->orientation);
	}
#endif
}

static void tasklist_set_button_relief(TasklistData* tasklist, GtkReliefStyle relief)
{
#ifdef HAVE_X11
	if (WNCK_IS_TASKLIST(tasklist->tasklist))
	{
		wnck_tasklist_set_button_relief(WNCK_TASKLIST(tasklist->tasklist), relief);
	}
#endif /* HAVE_X11 */

	/* Not implemented for Wayland */
}

static const int* tasklist_get_size_hint_list(TasklistData* tasklist, int* n_elements)
{
#ifdef HAVE_X11
	if (WNCK_IS_TASKLIST(tasklist->tasklist))
	{
		return wnck_tasklist_get_size_hint_list(WNCK_TASKLIST(tasklist->tasklist), n_elements);
	}
	else
#endif /* HAVE_X11 */

	{
		/* Not implemented for Wayland */
		*n_elements = 0;
		return NULL;
	}
}

static void on_tasklist_properties_dialog_response(GtkWidget* widget, int id, TasklistData* tasklist)
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
    tasklist_apply_orientation (tasklist);
    tasklist_set_size_request(tasklist);
}

static void applet_change_background(MatePanelApplet* applet, MatePanelAppletBackgroundType type, GdkColor* color, cairo_pattern_t* pattern, TasklistData* tasklist)
{
	switch (type)
	{
		case PANEL_NO_BACKGROUND:
		case PANEL_COLOR_BACKGROUND:
		case PANEL_PIXMAP_BACKGROUND:
			tasklist_set_button_relief(tasklist, GTK_RELIEF_NONE);
			break;
	}
}

#ifdef HAVE_X11
#ifdef HAVE_WINDOW_PREVIEWS
static cairo_surface_t*
preview_window_thumbnail (WnckWindow   *wnck_window,
                          TasklistData *tasklist,
                          int          *thumbnail_width,
                          int          *thumbnail_height,
                          int          *thumbnail_scale)
{
	GdkWindow *window;
	GdkWindow *window_wrapper = NULL;
	Window win;
	cairo_surface_t *thumbnail;
	cairo_t *cr;
	double ratio;
	int width, height, scale;

	win = wnck_window_get_xid (wnck_window);

	if ((window = gdk_x11_window_lookup_for_display (gdk_display_get_default (), win)) == NULL)
	{
		if ((window = gdk_x11_window_foreign_new_for_display (gdk_display_get_default (), win)) == NULL)
		{
			return NULL;
		}
		else
		{
			window_wrapper = window;
		}
	}
	else
	{
		g_object_ref (window);
	}

	*thumbnail_scale = scale = gdk_window_get_scale_factor (window);
	width = gdk_window_get_width (window) * scale;
	height = gdk_window_get_height (window) * scale;

	int thumbnail_size = g_settings_get_int (tasklist->preview_settings, "thumbnail-window-size");

	/* Scale to configured size while maintaining aspect ratio */
	if (width > height)
	{
		int max_size = MIN (width, thumbnail_size * scale);
		ratio = (double) max_size / (double) width;
		*thumbnail_width = max_size;
		*thumbnail_height = (int) ((double) height * ratio);
	}
	else
	{
		int max_size = MIN (height, thumbnail_size * scale);
		ratio = (double) max_size / (double) height;
		*thumbnail_height = max_size;
		*thumbnail_width = (int) ((double) width * ratio);
	}

	thumbnail = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
	                                        *thumbnail_width,
	                                        *thumbnail_height);
	cairo_surface_set_device_scale (thumbnail, scale, scale);
	cr = cairo_create (thumbnail);
	cairo_scale (cr, ratio, ratio);
	gdk_cairo_set_source_window (cr, window, 0, 0);
	cairo_paint (cr);
	cairo_destroy (cr);

	if (window_wrapper)
		g_object_unref (window_wrapper);
	g_object_unref (window);

	return thumbnail;
}

#define PREVIEW_PADDING 5
static void
preview_window_reposition (TasklistData    *tasklist,
                           cairo_surface_t *thumbnail,
                           int              width,
                           int              height,
                           int              scale)
{
	GdkMonitor *monitor;
	GdkRectangle monitor_geom;
	int x_pos, y_pos;

	/* Set position at pointer, then re-adjust from there to just outside of the pointer */
	gtk_window_set_position (GTK_WINDOW (tasklist->preview), GTK_WIN_POS_MOUSE);
	gtk_window_get_position (GTK_WINDOW (tasklist->preview), &x_pos, &y_pos);

	/* Get geometry of monitor where tasklist is located to calculate correct position of preview */
	monitor = gdk_display_get_monitor_at_point (gdk_display_get_default (), x_pos, y_pos);
	gdk_monitor_get_geometry (monitor, &monitor_geom);

	/* Add padding to clear the panel */
	switch (mate_panel_applet_get_orient (MATE_PANEL_APPLET (tasklist->applet)))
	{
		case MATE_PANEL_APPLET_ORIENT_LEFT:
			x_pos = monitor_geom.width + monitor_geom.x - (width/scale + tasklist->size) - PREVIEW_PADDING;
			break;
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			x_pos = tasklist->size + PREVIEW_PADDING;
			break;
		case MATE_PANEL_APPLET_ORIENT_UP:
			y_pos = monitor_geom.height + monitor_geom.y - (height/scale + tasklist->size) - PREVIEW_PADDING;
			break;
		case MATE_PANEL_APPLET_ORIENT_DOWN:
		default:
			y_pos = tasklist->size + PREVIEW_PADDING;
			break;
	}

	gtk_window_move (GTK_WINDOW (tasklist->preview), x_pos, y_pos);
}

static gboolean preview_window_draw (GtkWidget *widget, cairo_t *cr, cairo_surface_t *thumbnail)
{
	GtkStyleContext *context;

	context = gtk_widget_get_style_context (widget);
	gtk_render_icon_surface (context, cr, thumbnail, 0, 0);

	return FALSE;
}

static gboolean applet_enter_notify_event (WnckTasklist *tl, GList *wnck_windows, TasklistData *tasklist)
{
	cairo_surface_t *thumbnail;
	WnckWindow *wnck_window = NULL;
	int n_windows;
	int thumbnail_width;
	int thumbnail_height;
	int thumbnail_scale;

	if (tasklist->preview != NULL)
	{
		gtk_widget_destroy (tasklist->preview);
		tasklist->preview = NULL;
	}

	if (!g_settings_get_boolean (tasklist->preview_settings, "show-window-thumbnails") || wnck_windows == NULL)
		return FALSE;

	n_windows = g_list_length (wnck_windows);
	/* TODO: Display a list of stacked thumbnails for grouped windows. */
	if (n_windows == 1)
	{
		GList* l = wnck_windows;
		if (l != NULL)
			wnck_window = (WnckWindow*)l->data;
	}

	if (wnck_window == NULL)
		return FALSE;

	/* Do not show preview if window is not visible nor in current workspace */
	if (!wnck_window_is_visible_on_workspace (wnck_window,
						  wnck_screen_get_active_workspace (wnck_screen_get_default ())))
		return FALSE;

	thumbnail = preview_window_thumbnail (wnck_window, tasklist, &thumbnail_width, &thumbnail_height, &thumbnail_scale);

	if (thumbnail == NULL)
		return FALSE;

	/* Create window to display preview */
	tasklist->preview = gtk_window_new (GTK_WINDOW_POPUP);

	gtk_widget_set_app_paintable (tasklist->preview, TRUE);
	gtk_window_set_default_size (GTK_WINDOW (tasklist->preview), thumbnail_width/thumbnail_scale, thumbnail_height/thumbnail_scale);
	gtk_window_set_resizable (GTK_WINDOW (tasklist->preview), TRUE);

	preview_window_reposition (tasklist, thumbnail, thumbnail_width, thumbnail_height, thumbnail_scale);

	gtk_widget_show (tasklist->preview);

	g_signal_connect_data (G_OBJECT (tasklist->preview), "draw", G_CALLBACK (preview_window_draw), thumbnail, (GClosureNotify) G_CALLBACK (cairo_surface_destroy), 0);

	return FALSE;
}

static gboolean applet_leave_notify_event (WnckTasklist *tl, GList *wnck_windows, TasklistData *tasklist)
{
	if (tasklist->preview != NULL)
	{
		gtk_widget_destroy (tasklist->preview);
		tasklist->preview = NULL;
	}

	return FALSE;
}
#endif /* HAVE_WINDOW_PREVIEWS */
#endif /* HAVE_X11 */

static void applet_change_pixel_size(MatePanelApplet* applet, gint size, TasklistData* tasklist)
{
	if (tasklist->size == size)
		return;

	tasklist->size = size;

	tasklist_set_size_request(tasklist);
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
		"document-properties",
		N_("_Preferences"),
		NULL,
		NULL,
		G_CALLBACK(display_properties_dialog)
	},
	{
		"TasklistHelp",
		"help-browser",
		N_("_Help"),
		NULL,
		NULL,
		G_CALLBACK(display_help_dialog)
	},
	{
		"TasklistAbout",
		"help-about",
		N_("_About"),
		NULL,
		NULL,
		G_CALLBACK(display_about_dialog)
	}
};

static void applet_size_allocate(GtkWidget *widget, GtkAllocation *allocation, TasklistData *tasklist)
{
	int len;
	const int* size_hints;

	size_hints = tasklist_get_size_hint_list (tasklist, &len);

	g_assert(len % 2 == 0);

#if !defined(WNCKLET_INPROCESS) && !GTK_CHECK_VERSION (3, 23, 0)
	/* HACK: When loading the WnckTasklist initially, it reports size hints as though there were
	 * no elements in the Tasklist. This causes a rendering issue when built out-of-process in
	 * HiDPI displays. We keep a flag to skip size hinting until WnckTasklist has something to
	 * show. */
	if (!tasklist->needs_hints)
	{
		int i;
		for (i = 0; i < len; i++)
		{
			if (size_hints[i])
			{
				tasklist->needs_hints = TRUE;
				break;
			}
		}
	}

	if (tasklist->needs_hints)
#endif
		mate_panel_applet_set_size_hints(MATE_PANEL_APPLET(tasklist->applet), size_hints, len, 0);
}

#ifdef HAVE_X11
/* Currently only used on X11, but should work on Wayland as well when needed */
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
#endif /* HAVE_X11 */

gboolean window_list_applet_fill(MatePanelApplet* applet)
{
	TasklistData* tasklist;
	GtkActionGroup* action_group;
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

	tasklist->settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (tasklist->applet), WINDOW_LIST_SCHEMA);
#ifdef HAVE_WINDOW_PREVIEWS
	tasklist->preview_settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (tasklist->applet), WINDOW_LIST_PREVIEW_SCHEMA);
#endif

	tasklist->size = mate_panel_applet_get_size(applet);

#if !defined(WNCKLET_INPROCESS) && !GTK_CHECK_VERSION (3, 23, 0)
	tasklist->needs_hints = FALSE;
#endif

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

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		tasklist->tasklist = wnck_tasklist_new();

		wnck_tasklist_set_middle_click_close (WNCK_TASKLIST (tasklist->tasklist), TRUE);
		wnck_tasklist_set_icon_loader(WNCK_TASKLIST(tasklist->tasklist), icon_loader_func, tasklist, NULL);

#ifdef HAVE_WINDOW_PREVIEWS
		g_signal_connect(G_OBJECT(tasklist->tasklist), "task_enter_notify", G_CALLBACK(applet_enter_notify_event), tasklist);
		g_signal_connect(G_OBJECT(tasklist->tasklist), "task_leave_notify", G_CALLBACK(applet_leave_notify_event), tasklist);
#endif /* HAVE_WINDOW_PREVIEWS */
	}
	else
#endif /* HAVE_X11 */

#ifdef HAVE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		tasklist->tasklist = wayland_tasklist_new();
	}
	else
#endif /* HAVE_WAYLAND */

	{
		tasklist->tasklist = gtk_label_new ("[Tasklist not supported on this platform]");
	}

	tasklist_apply_orientation(tasklist);

	g_signal_connect(G_OBJECT(tasklist->tasklist), "destroy", G_CALLBACK(destroy_tasklist), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "size_allocate", G_CALLBACK(applet_size_allocate), tasklist);

	gtk_container_add(GTK_CONTAINER(tasklist->applet), tasklist->tasklist);

	g_signal_connect(G_OBJECT(tasklist->applet), "realize", G_CALLBACK(applet_realized), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_orient", G_CALLBACK(applet_change_orient), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_size", G_CALLBACK(applet_change_pixel_size), tasklist);
	g_signal_connect(G_OBJECT(tasklist->applet), "change_background", G_CALLBACK(applet_change_background), tasklist);

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


	mate_panel_applet_setup_menu_from_resource (MATE_PANEL_APPLET (tasklist->applet),
	                                            WNCKLET_RESOURCE_PATH "window-list-menu.xml",
	                                            action_group);

	if (mate_panel_applet_get_locked_down(MATE_PANEL_APPLET(tasklist->applet)))
	{
		GtkAction* action;

		action = gtk_action_group_get_action(action_group, "TasklistPreferences");
		gtk_action_set_visible(action, FALSE);
	}

	g_object_unref(action_group);

	tasklist_set_size_request(tasklist);
	gtk_widget_show(tasklist->tasklist);
	gtk_widget_show(tasklist->applet);

	return TRUE;
}

static void call_system_monitor(GtkAction* action, TasklistData* tasklist)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS(system_monitors); i += 1)
	{
		char *programpath = g_find_program_in_path(system_monitors[i]);

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

	gtk_show_about_dialog(GTK_WINDOW(tasklist->applet),
		"program-name", _("Window List"),
		"title", _("About Window List"),
		"authors", authors,
		"comments", _("The Window List shows a list of all windows in a set of buttons and lets you browse them."),
		"copyright", _("Copyright \xc2\xa9 2002 Red Hat, Inc.\n"
		               "Copyright \xc2\xa9 2011 Perberos\n"
		               "Copyright \xc2\xa9 2012-2021 MATE developers"),
		"documenters", documenters,
		"icon-name", WINDOW_LIST_ICON,
		"logo-icon-name", WINDOW_LIST_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		"website", PACKAGE_URL,
		NULL);
}

static void mouse_scrolling_callback(GSettings* settings, gchar *key, TasklistData* tasklist)
{
    wnck_tasklist_set_scroll_enabled (WNCK_TASKLIST(tasklist->tasklist), g_settings_get_boolean (settings, "mouse-scrolling"));
}

static void move_unminimized_windows_callback(GSettings* settings, gchar *key, TasklistData* tasklist)
{
    wnck_tasklist_set_switch_workspace_on_unminimize(WNCK_TASKLIST(tasklist->tasklist), g_settings_get_boolean (settings, "move-unminimized-windows"));
}

static void display_all_workspaces_callback(GSettings* settings, gchar *key, TasklistData* tasklist)
{
    wnck_tasklist_set_include_all_workspaces(WNCK_TASKLIST(tasklist->tasklist), g_settings_get_boolean (settings, "display-all-workspaces"));
}

#ifdef HAVE_WINDOW_PREVIEWS
static void on_thumbnail_size_spin_value_changed(GtkSpinButton* button, TasklistData* tasklist)
{
	g_settings_set_int(tasklist->preview_settings, "thumbnail-window-size", gtk_spin_button_get_value_as_int(button));
}
#endif

static void on_never_group_radio_toggled(GtkRadioButton* button, TasklistData* tasklist)
{
    g_settings_set_enum (tasklist->settings, "group-windows", TASKLIST_NEVER_GROUP);
    wnck_tasklist_set_grouping(WNCK_TASKLIST(tasklist->tasklist), WNCK_TASKLIST_NEVER_GROUP);
}

static void on_auto_group_radio_toggled(GtkRadioButton* button, TasklistData* tasklist)
{
    g_settings_set_enum (tasklist->settings, "group-windows", TASKLIST_AUTO_GROUP);
    wnck_tasklist_set_grouping(WNCK_TASKLIST(tasklist->tasklist), WNCK_TASKLIST_AUTO_GROUP);
}

static void on_always_group_radio_toggled(GtkRadioButton* button, TasklistData* tasklist)
{
    g_settings_set_enum (tasklist->settings, "group-windows", TASKLIST_ALWAYS_GROUP);
    wnck_tasklist_set_grouping(WNCK_TASKLIST(tasklist->tasklist), WNCK_TASKLIST_ALWAYS_GROUP);
}

#define GET_WIDGET(s) GTK_WIDGET(gtk_builder_get_object(builder, s))

static void setup_dialog(GtkBuilder* builder, TasklistData* tasklist)
{
	tasklist->display_all_workspaces_radio = GET_WIDGET("display_all_workspaces_radio");
	tasklist->never_group_radio = GET_WIDGET("never_group_radio");
	tasklist->auto_group_radio = GET_WIDGET("auto_group_radio");
	tasklist->always_group_radio = GET_WIDGET("always_group_radio");

#ifdef HAVE_WINDOW_PREVIEWS
	tasklist->window_thumbnail_box = GET_WIDGET("window_thumbnail_box");
	tasklist->show_thumbnails_check = GET_WIDGET("show_thumbnails_check");
	tasklist->thumbnail_size_spin = GET_WIDGET("thumbnail_size_spin");
	tasklist->thumbnail_box = GET_WIDGET("thumbnail_box");

	g_settings_bind(tasklist->preview_settings,
                    "show-window-thumbnails",
                    tasklist->show_thumbnails_check,
                    "active",
                    G_SETTINGS_BIND_DEFAULT);

	gtk_widget_set_sensitive (tasklist->thumbnail_box, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tasklist->show_thumbnails_check)));
	g_object_bind_property(tasklist->show_thumbnails_check, "active", tasklist->thumbnail_box, "sensitive", G_BINDING_DEFAULT);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tasklist->thumbnail_size_spin),
                              (gdouble) g_settings_get_int (tasklist->preview_settings, "thumbnail-window-size"));
#else
	gtk_widget_hide(GET_WIDGET("window_thumbnail_box"));
#endif

	tasklist->restore_to_current_workspace_radio = GET_WIDGET("restore_to_current_workspace_radio");
	tasklist->mouse_scroll_check = GET_WIDGET("mouse_scroll_check");
	tasklist->minimized_windows_box = GET_WIDGET("minimized_windows_box");
	tasklist->window_grouping_box = GET_WIDGET("window_grouping_box");
	tasklist->window_list_content_box = GET_WIDGET("window_list_content_box");

	g_settings_bind (tasklist->settings,
                     "move-unminimized-windows",
                      tasklist->restore_to_current_workspace_radio,
                     "active",
                      G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (tasklist->settings,
					  "changed::move-unminimized-windows",
					  G_CALLBACK (move_unminimized_windows_callback),
					  tasklist);

	g_settings_bind (tasklist->settings,
                     "mouse-scrolling",
                      tasklist->mouse_scroll_check,
                     "active",
                      G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (tasklist->settings,
					  "changed::mouse-scrolling",
					  G_CALLBACK (mouse_scrolling_callback),
					  tasklist);

	g_settings_bind(tasklist->settings,
                    "display-all-workspaces",
                    tasklist->display_all_workspaces_radio,
                    "active",
                    G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (tasklist->settings,
					  "changed::display-all-workspaces",
					  G_CALLBACK (display_all_workspaces_callback),
					  tasklist);

	gtk_widget_set_sensitive (tasklist->minimized_windows_box, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tasklist->display_all_workspaces_radio)));
	g_object_bind_property(tasklist->display_all_workspaces_radio, "active", tasklist->minimized_windows_box, "sensitive", G_BINDING_DEFAULT);

#ifdef HAVE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
        gtk_widget_set_sensitive(tasklist->window_list_content_box, FALSE);
        gtk_widget_set_sensitive(tasklist->window_grouping_box, FALSE);
        gtk_widget_set_sensitive(tasklist->minimized_windows_box, FALSE);
        gtk_widget_set_sensitive(tasklist->mouse_scroll_check, FALSE);
    #ifdef HAVE_WINDOW_PREVIEWS
        gtk_widget_set_sensitive(tasklist->window_thumbnail_box, FALSE);
    #endif
    }
#endif /* HAVE_WAYLAND */
}

static void display_properties_dialog(GtkAction* action, TasklistData* tasklist)
{
	if (tasklist->properties_dialog == NULL)
	{
		GtkBuilder* builder = gtk_builder_new_from_resource(WNCKLET_RESOURCE_PATH "window-list.ui");

		gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

		tasklist->properties_dialog = GET_WIDGET("tasklist_properties_dialog");

		g_object_add_weak_pointer(G_OBJECT(tasklist->properties_dialog), (void**) &tasklist->properties_dialog);

		setup_dialog(builder, tasklist);

		gtk_builder_add_callback_symbols (builder,
#ifdef HAVE_WINDOW_PREVIEWS
		                                  "on_thumbnail_size_spin_value_changed", G_CALLBACK (on_thumbnail_size_spin_value_changed),
#endif
		                                  "on_never_group_radio_toggled", G_CALLBACK (on_never_group_radio_toggled),
		                                  "on_auto_group_radio_toggled", G_CALLBACK (on_auto_group_radio_toggled),
		                                  "on_always_group_radio_toggled", G_CALLBACK (on_always_group_radio_toggled),
		                                  "on_tasklist_properties_dialog_response", G_CALLBACK (on_tasklist_properties_dialog_response),
		                                  NULL);

		gtk_builder_connect_signals(builder, tasklist);

		g_object_unref(builder);
	}

	gtk_window_set_screen(GTK_WINDOW(tasklist->properties_dialog), gtk_widget_get_screen(tasklist->applet));
	gtk_window_present(GTK_WINDOW(tasklist->properties_dialog));
}

static void destroy_tasklist(GtkWidget* widget, TasklistData* tasklist)
{
	g_signal_handlers_disconnect_by_data (G_OBJECT (tasklist->applet), tasklist);

#ifdef HAVE_WINDOW_PREVIEWS
	g_signal_handlers_disconnect_by_data (G_OBJECT (tasklist->tasklist), tasklist);
	g_signal_handlers_disconnect_by_data (tasklist->preview_settings, tasklist);
	g_object_unref(tasklist->preview_settings);
#endif

	g_signal_handlers_disconnect_by_data (tasklist->settings, tasklist);

	g_object_unref(tasklist->settings);

	if (tasklist->properties_dialog)
		gtk_widget_destroy(tasklist->properties_dialog);

#ifdef HAVE_WINDOW_PREVIEWS
	if (tasklist->preview)
		gtk_widget_destroy(tasklist->preview);
#endif

	g_free(tasklist);
}
