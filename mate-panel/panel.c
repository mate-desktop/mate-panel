/* Mate panel: Initialization routines
 * (C) 1997,1998,1999,2000 the Free Software Foundation
 * (C) 2000 Eazel, Inc.
 *
 * Authors: Federico Mena
 *          Miguel de Icaza
 *          George Lebl
 */

#include <config.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#ifdef HAVE_X11
#include <gtk/gtkx.h> /* for GTK_IS_SOCKET */
#endif

#include <libpanel-util/panel-glib.h>
#include <libpanel-util/panel-gtk.h>

#include "panel.h"

#include "applet.h"
#include "drawer.h"
#include "button-widget.h"
#include "launcher.h"
#include "panel-context-menu.h"
#include "panel-util.h"
#include "panel-config-global.h"
#include "panel-profile.h"
#include "panel-applet-frame.h"
#include "panel-action-button.h"
#include "panel-menu-bar.h"
#include "panel-separator.h"
#include "panel-toplevel.h"
#include "panel-menu-button.h"
#include "panel-globals.h"
#include "panel-lockdown.h"
#include "panel-icon-names.h"

enum {
	TARGET_URL,
	TARGET_NETSCAPE_URL,
	TARGET_DIRECTORY,
	TARGET_COLOR,
	TARGET_APPLET,
	TARGET_APPLET_INTERNAL,
	TARGET_ICON_INTERNAL,
	TARGET_BGIMAGE,
	TARGET_BACKGROUND_RESET
};

/*we call this recursively*/
static void orient_change_foreach(GtkWidget *w, gpointer data);

void
orientation_change (AppletInfo  *info,
		    PanelWidget *panel)
{
	PanelOrientation orientation;

	orientation = panel_widget_get_applet_orientation (panel);

	switch (info->type) {
	case PANEL_OBJECT_APPLET:
		mate_panel_applet_frame_change_orientation (
				MATE_PANEL_APPLET_FRAME (info->widget), orientation);
		break;
	case PANEL_OBJECT_MENU:
	case PANEL_OBJECT_LAUNCHER:
	case PANEL_OBJECT_ACTION:
		button_widget_set_orientation (BUTTON_WIDGET (info->widget), orientation);
		break;
	case PANEL_OBJECT_MENU_BAR:
		panel_menu_bar_set_orientation (PANEL_MENU_BAR (info->widget), orientation);
		break;
	case PANEL_OBJECT_DRAWER: {
		Drawer      *drawer = info->data;
		PanelWidget *panel_widget;

		panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

		button_widget_set_orientation (BUTTON_WIDGET (info->widget), orientation);

		gtk_widget_queue_resize (GTK_WIDGET (drawer->toplevel));
		gtk_container_foreach (GTK_CONTAINER (panel_widget),
				       orient_change_foreach,
				       panel_widget);
		}
		break;
	case PANEL_OBJECT_SEPARATOR:
		panel_separator_set_orientation (PANEL_SEPARATOR (info->widget),
						 orientation);
		break;
	default:
		break;
	}
}

static void
orient_change_foreach(GtkWidget *w, gpointer data)
{
	AppletInfo *info = g_object_get_data (G_OBJECT (w), "applet_info");
	PanelWidget *panel = data;

	orientation_change(info,panel);
}


static void
panel_orient_change (GtkWidget *widget, gpointer data)
{
	gtk_container_foreach(GTK_CONTAINER(widget),
			      orient_change_foreach,
			      widget);
}

/*we call this recursively*/
static void size_change_foreach(GtkWidget *w, gpointer data);

void
size_change (AppletInfo  *info,
	     PanelWidget *panel)
{
	if (info->type == PANEL_OBJECT_APPLET)
		mate_panel_applet_frame_change_size (
			MATE_PANEL_APPLET_FRAME (info->widget), panel->sz);
}

static void
size_change_foreach(GtkWidget *w, gpointer data)
{
	AppletInfo *info = g_object_get_data (G_OBJECT (w), "applet_info");
	PanelWidget *panel = data;

	size_change(info,panel);
}


static void
panel_size_change (GtkWidget *widget, gpointer data)
{
	gtk_container_foreach(GTK_CONTAINER(widget), size_change_foreach,
			      widget);
}

void
back_change (AppletInfo  *info,
	     PanelWidget *panel)
{
	switch (info->type) {
	case PANEL_OBJECT_APPLET:
		mate_panel_applet_frame_change_background (
		MATE_PANEL_APPLET_FRAME (info->widget), panel->toplevel->background.type);
		break;
	case PANEL_OBJECT_MENU_BAR:
		panel_menu_bar_change_background (PANEL_MENU_BAR (info->widget));
		break;
	case PANEL_OBJECT_SEPARATOR:
		panel_separator_change_background (PANEL_SEPARATOR (info->widget));
		break;
	default:
		break;
	}
}

static void
back_change_foreach (GtkWidget   *widget,
		     PanelWidget *panel)
{
	AppletInfo *info;

	info = g_object_get_data (G_OBJECT (widget), "applet_info");

	back_change (info, panel);
}

static void
panel_back_change (GtkWidget *widget, gpointer data)
{
	gtk_container_foreach (GTK_CONTAINER (widget),
			       (GtkCallback) back_change_foreach,
			       widget);

#ifdef FIXME_FOR_NEW_CONFIG
	/*update the configuration box if it is displayed*/
	update_config_back(PANEL_WIDGET(widget));
#endif /* FIXME_FOR_NEW_CONFIG */
}

static void
mate_panel_applet_added(GtkWidget *widget, GtkWidget *applet, gpointer data)
{
	AppletInfo    *info;

	info = g_object_get_data (G_OBJECT (applet), "applet_info");

	orientation_change(info,PANEL_WIDGET(widget));
	size_change(info,PANEL_WIDGET(widget));
	back_change(info,PANEL_WIDGET(widget));
}

static void
mate_panel_applet_removed(GtkWidget *widget, GtkWidget *applet, gpointer data)
{
	PanelToplevel *toplevel;
	AppletInfo    *info;

	toplevel = PANEL_WIDGET (widget)->toplevel;
	info = g_object_get_data (G_OBJECT (applet), "applet_info");

	if (info->type == PANEL_OBJECT_DRAWER) {
		Drawer *drawer = info->data;

		if (drawer->toplevel)
			panel_toplevel_queue_auto_hide (toplevel);
	}
}

static gboolean
deactivate_idle (gpointer data)
{
	PanelData *pd = data;
	pd->deactivate_idle = 0;

	pd->insertion_pos = -1;

	return FALSE;
}

static void
context_menu_deactivate (GtkWidget *w,
			 PanelData *pd)
{
	if (pd->deactivate_idle == 0)
		pd->deactivate_idle = g_idle_add (deactivate_idle, pd);

	panel_toplevel_pop_autohide_disabler (PANEL_TOPLEVEL (pd->panel));
}

static void
context_menu_show (GtkWidget *w,
		   PanelData *pd)
{
	panel_toplevel_push_autohide_disabler (PANEL_TOPLEVEL (pd->panel));
}

static void
panel_recreate_context_menu (PanelData *pd)
{
	if (pd->menu)
		g_object_unref (pd->menu);
	pd->menu = NULL;
}

static void
panel_destroy (PanelToplevel *toplevel,
	       PanelData     *pd)
{
	panel_lockdown_notify_remove (G_CALLBACK (panel_recreate_context_menu),
				      pd);

	if (pd->menu) {
		g_signal_handlers_disconnect_by_func (pd->menu,
						      context_menu_deactivate,
						      pd);
		g_object_unref (pd->menu);
	}
	pd->menu = NULL;

	pd->panel = NULL;

	if (pd->deactivate_idle != 0)
		g_source_remove (pd->deactivate_idle);
	pd->deactivate_idle = 0;

	g_object_set_data (G_OBJECT (toplevel), "PanelData", NULL);

	panel_list = g_slist_remove (panel_list, pd);
	g_free (pd);
}

static void
mate_panel_applet_move(PanelWidget *panel, GtkWidget *widget, gpointer data)
{
	AppletInfo *info;

	info = g_object_get_data (G_OBJECT (widget), "applet_info");

	g_return_if_fail (info);

	mate_panel_applet_save_position (info, info->id, FALSE);
}

static GtkWidget *
panel_menu_get (PanelWidget *panel, PanelData *pd)
{
	if (!pd->menu) {
		pd->menu = g_object_ref_sink (panel_context_menu_create (panel));
		g_signal_connect (pd->menu, "deactivate",
				  G_CALLBACK (context_menu_deactivate),
				  pd);
		g_signal_connect (pd->menu, "show",
				  G_CALLBACK (context_menu_show), pd);
	}

	return pd->menu;
}

static GtkWidget *
make_popup_panel_menu (PanelWidget *panel_widget)
{
	PanelData *pd;
	GtkWidget *menu;

	if (!panel_widget) {
		PanelToplevel *toplevel;

		toplevel = PANEL_TOPLEVEL (((PanelData *) panel_list->data)->panel);

		panel_widget = panel_toplevel_get_panel_widget (toplevel);
	}

	pd = g_object_get_data (G_OBJECT (panel_widget->toplevel), "PanelData");
	menu = panel_menu_get (panel_widget, pd);
	g_object_set_data (G_OBJECT (menu), "menu_panel", panel_widget);

	return menu;
}

static gboolean
panel_popup_menu (PanelToplevel *toplevel,
		  guint          button,
		  guint32        activate_time)
{
	PanelWidget *panel_widget;
	GtkWidget   *menu;
	PanelData   *panel_data;
	GdkEvent    *current_event;

	panel_widget = panel_toplevel_get_panel_widget (toplevel);
	panel_data   = g_object_get_data (G_OBJECT (toplevel), "PanelData");

	current_event = gtk_get_current_event ();
	if (current_event && current_event->type == GDK_BUTTON_PRESS)
		panel_data->insertion_pos = panel_widget_get_cursorloc (panel_widget);
	else
		panel_data->insertion_pos = -1;

	if (current_event)
		gdk_event_free (current_event);

	menu = make_popup_panel_menu (panel_widget);
	if (!menu)
		return FALSE;

	gtk_menu_set_screen (GTK_MENU (menu),
			     gtk_window_get_screen (GTK_WINDOW (toplevel)));

	gtk_window_set_attached_to (GTK_WINDOW (gtk_widget_get_toplevel (menu)),
				    GTK_WIDGET (toplevel));
	gtk_menu_popup_at_pointer (GTK_MENU (menu), NULL);

	return TRUE;
}

static gboolean
panel_popup_menu_signal (PanelToplevel *toplevel)
{
	return panel_popup_menu (toplevel, 3, GDK_CURRENT_TIME);
}

static gboolean
panel_button_press_event (PanelToplevel  *toplevel,
			  GdkEventButton *event)
{
	if (event->button != 3)
		return FALSE;

	return panel_popup_menu (toplevel, event->button, event->time);
}

static gboolean
panel_key_press_event (GtkWidget   *widget,
		       GdkEventKey *event)
{
#ifdef HAVE_X11
	/*
  	 * If the focus widget is a GtkSocket, i.e. the
	 * focus is in an applet in another process, then key
	 * bindings do not work. We get around this by
	 * activating the key bindings here.
	 *
	 * Will always be false when not using X
	 */
	if (GTK_IS_SOCKET (gtk_window_get_focus (GTK_WINDOW (widget))) &&
	    event->keyval == GDK_KEY_F10 &&
	    (event->state & gtk_accelerator_get_default_mod_mask ()) == GDK_CONTROL_MASK)
		return gtk_bindings_activate (G_OBJECT (widget),
					      event->keyval,
					      event->state);
#endif

	return FALSE;
}

static gboolean
set_background_image_from_uri (PanelToplevel *toplevel,
			       const char    *uri)
{
	char *image;

	if ( ! panel_profile_background_key_is_writable (toplevel, "type") ||
	     ! panel_profile_background_key_is_writable (toplevel, "image"))
		return FALSE;

	if (!(image = g_filename_from_uri (uri, NULL, NULL)))
		return FALSE;

	panel_profile_set_background_image (toplevel, image);
	panel_profile_set_background_type (toplevel, PANEL_BACK_IMAGE);

	g_free (image);

	return FALSE;
}

static gboolean
set_background_color (PanelToplevel *toplevel,
		      guint16       *dropped)
{
	GdkRGBA color;

	if (!dropped)
		return FALSE;

	if ( ! panel_profile_background_key_is_writable (toplevel, "color") ||
	     ! panel_profile_background_key_is_writable (toplevel, "type"))
		return FALSE;

	color.red   = dropped [0];
	color.green = dropped [1];
	color.blue  = dropped [2];
	color.alpha = 1.;

	panel_profile_set_background_color (toplevel, &color);
	panel_profile_set_background_type (toplevel, PANEL_BACK_COLOR);

	return TRUE;
}

static gboolean
drop_url (PanelWidget *panel,
	  int          position,
	  const char  *url)
{
	enum {
		NETSCAPE_URL_URL,
		NETSCAPE_URL_NAME
	};
	char **netscape_url;
	char  *name;
	char  *comment;

	g_return_val_if_fail (url != NULL, FALSE);

	if (!panel_profile_id_lists_are_writable ())
		return FALSE;

	netscape_url = g_strsplit (url, "\n", 2);
	if (!netscape_url ||
	    PANEL_GLIB_STR_EMPTY (netscape_url[NETSCAPE_URL_URL])) {
		g_strfreev (netscape_url);
		return FALSE;
	}

	comment = g_strdup_printf (_("Open URL: %s"),
				   netscape_url[NETSCAPE_URL_URL]);

	if (PANEL_GLIB_STR_EMPTY (netscape_url[NETSCAPE_URL_NAME]))
		name = netscape_url[NETSCAPE_URL_URL];
	else
		name = netscape_url[NETSCAPE_URL_NAME];

	panel_launcher_create_from_info (panel->toplevel, position, FALSE,
					 netscape_url[NETSCAPE_URL_URL],
					 name, comment, PANEL_ICON_REMOTE);

	g_free (comment);
	g_strfreev (netscape_url);

	return TRUE;
}

static gboolean
drop_menu (PanelWidget *panel,
	   int          position,
	   const char  *menu_filename,
	   const char  *menu_path)
{
	if (!panel_profile_id_lists_are_writable ())
		return FALSE;

	return panel_menu_button_create (panel->toplevel,
					 position,
					 menu_filename,
					 menu_path,
					 menu_path != NULL,
					 NULL);

}

static gboolean
drop_uri (PanelWidget *panel,
	  int          position,
	  const char  *uri,
	  const char  *fallback_icon)
{
	char  *name;
	char  *comment;
	char  *buf;
	char  *icon;
	GFile *file;

	if (!panel_profile_id_lists_are_writable ())
		return FALSE;

	name = panel_util_get_label_for_uri (uri);
	icon = panel_util_get_icon_for_uri (uri);
	if (!icon)
		icon = g_strdup (fallback_icon);

	/* FIXME: we might get icons like "folder-music" that might not exist in
	 * the icon theme. This would usually be okay if we could use fallback
	 * icons (and get "folder" this way). However, this is not possible for
	 * launchers: this could be an application that uses an icon named
	 * folder-magic-app, for which we don't want fallbacks. We just want to
	 * go to hicolor. */

	file = g_file_new_for_uri (uri);
	buf = g_file_get_parse_name (file);
	g_object_unref (file);
	/* Translators: %s is a URI */
	comment = g_strdup_printf (_("Open '%s'"), buf);
	g_free (buf);

	panel_launcher_create_from_info (panel->toplevel, position, FALSE,
					 uri, name, comment, icon);

	g_free (name);
	g_free (comment);
	g_free (icon);

	return TRUE;
}

static gboolean
drop_caja_desktop_uri (PanelWidget *panel,
			   int          pos,
			   const char  *uri)
{
	gboolean    success;
	const char *id;
	const char *basename;

	if (g_ascii_strncasecmp (uri, "x-caja-desktop:///",
				 strlen ("x-caja-desktop:///")) != 0)
			return FALSE;

	success = TRUE;
	id = panel_profile_get_toplevel_id (panel->toplevel);
	basename = uri + strlen ("x-caja-desktop:///");

	if (strncmp (basename, "trash", strlen ("trash")) == 0)
		mate_panel_applet_frame_create (panel->toplevel, pos,
					   "OAFIID:MATE_Panel_TrashApplet");
	else if (strncmp (basename, "home", strlen ("home")) == 0)
		panel_launcher_create_with_id (id, pos,
					       "caja-home.desktop");
	else if (strncmp (basename, "computer", strlen ("computer")) == 0)
		panel_launcher_create_with_id (id, pos,
					       "caja-computer.desktop");
	else if (strncmp (basename, "network", strlen ("network")) == 0)
		panel_launcher_create_with_id (id, pos,
					       "caja-scheme.desktop");
	else
		success = FALSE;

	return success;
}

static gboolean
drop_urilist (PanelWidget *panel,
	      int          pos,
	      char        *urilist)
{
	char     **uris;
	gboolean   success;
	int        i;

	uris = g_uri_list_extract_uris (urilist);

	success = TRUE;
	for (i = 0; uris[i]; i++) {
		GFile      *file;
		GFileInfo  *info;
		const char *uri;

		uri = uris[i];

		if (g_ascii_strncasecmp (uri, "http:", strlen ("http:")) == 0 ||
		    g_ascii_strncasecmp (uri, "https:", strlen ("https:")) == 0 ||
		    g_ascii_strncasecmp (uri, "ftp:", strlen ("ftp:")) == 0 ||
		    g_ascii_strncasecmp (uri, "gopher:", strlen ("gopher:")) == 0 ||
		    g_ascii_strncasecmp (uri, "ghelp:", strlen ("ghelp:")) == 0 ||
		    g_ascii_strncasecmp (uri, "help:", strlen ("help:")) == 0 ||
		    g_ascii_strncasecmp (uri, "man:", strlen ("man:")) == 0 ||
		    g_ascii_strncasecmp (uri, "info:", strlen ("info:")) == 0) {
			/* FIXME: probably do this only on link,
			 * in fact, on link always set up a link,
			 * on copy do all the other stuff.  Or something. */
			if ( ! drop_url (panel, pos, uri))
				success = FALSE;
			continue;
		}

		if (g_ascii_strncasecmp (uri, "x-caja-desktop:",
					 strlen ("x-caja-desktop:")) == 0) {
			success = drop_caja_desktop_uri (panel, pos, uri);
			continue;
		}

		file = g_file_new_for_uri (uri);
		info = g_file_query_info (file,
					  "standard::type,"
					  "standard::content-type,"
					  "access::can-execute",
					  G_FILE_QUERY_INFO_NONE,
					  NULL, NULL);

		if (info) {
			const char *mime;
			GFileType   type;
			gboolean    can_exec;

			mime = g_file_info_get_content_type (info);
			type = g_file_info_get_file_type (info);
			can_exec = g_file_info_get_attribute_boolean (info,
								      G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);

			if (mime &&
			    g_str_has_prefix (mime, "image")) {
				if (!set_background_image_from_uri (panel->toplevel, uri))
					success = FALSE;
			} else if (mime &&
				   (!strcmp (mime, "application/x-mate-app-info") ||
				    !strcmp (mime, "application/x-desktop") ||
				    !strcmp (mime, "application/x-kde-app-info"))) {
				if (panel_profile_id_lists_are_writable ())
					panel_launcher_create (panel->toplevel, pos, uri);
				else
					success = FALSE;
			} else if (type != G_FILE_TYPE_DIRECTORY && can_exec) {
				char *filename;

				filename = g_file_get_path (file);

				if (panel_profile_id_lists_are_writable ())
					/* executable and local, so add a
					 * launcher with it */
					ask_about_launcher (filename, panel,
							    pos, TRUE);
				else
					success = FALSE;
				g_free (filename);
			} else {
				if (!drop_uri (panel, pos, uri,
					       PANEL_ICON_UNKNOWN))
					success = FALSE;
			}
		} else {
			if (!drop_uri (panel, pos, uri, PANEL_ICON_UNKNOWN))
				success = FALSE;
		}

		g_clear_object (&info);
		g_object_unref (file);
	}

	g_strfreev (uris);

	return success;
}

static gboolean
drop_internal_icon (PanelWidget *panel,
		    int          pos,
		    const char  *icon_name,
		    int          action)
{
	Launcher *old_launcher = NULL;

	if (!icon_name)
		return FALSE;

	if (!panel_profile_id_lists_are_writable ())
		return FALSE;

	if (action == GDK_ACTION_MOVE)
		old_launcher = find_launcher (icon_name);

	if (!panel_launcher_create_copy (panel->toplevel, pos, icon_name))
		return FALSE;

	if (old_launcher && old_launcher->button) {
		if (old_launcher->prop_dialog) {
			g_signal_handler_disconnect (old_launcher->button,
						     old_launcher->destroy_handler);
			launcher_properties_destroy (old_launcher);
		}
		panel_profile_delete_object (old_launcher->info);
	}

	return TRUE;
}

static gboolean
move_applet (PanelWidget *panel, int pos, int applet_index)
{
	GSList     *applet_list;
	AppletInfo *info;
	GtkWidget  *parent;

	applet_list = mate_panel_applet_list_applets ();

	info = g_slist_nth_data (applet_list, applet_index);

	if ( ! mate_panel_applet_can_freely_move (info))
		return FALSE;

	if (pos < 0)
		pos = 0;

	parent = gtk_widget_get_parent (info->widget);

	if (info != NULL &&
	    info->widget != NULL &&
	    parent != NULL &&
	    PANEL_IS_WIDGET (parent)) {
		GSList *forb;
		forb = g_object_get_data (G_OBJECT (info->widget),
					  MATE_PANEL_APPLET_FORBIDDEN_PANELS);
		if ( ! g_slist_find (forb, panel))
			panel_widget_reparent (PANEL_WIDGET (parent),
					       panel,
					       info->widget,
					       pos);
	}

	return TRUE;
}

static gboolean
drop_internal_applet (PanelWidget *panel, int pos, const char *applet_type,
		      int action)
{
	int applet_index = -1;
	gboolean remove_applet = FALSE;
	gboolean success = FALSE;

	if (applet_type == NULL)
		return FALSE;

	if (sscanf (applet_type, "MENU:%d", &applet_index) == 1 ||
	    sscanf (applet_type, "DRAWER:%d", &applet_index) == 1) {
		if (action != GDK_ACTION_MOVE)
			g_warning ("Only MOVE supported for menus/drawers");
		success = move_applet (panel, pos, applet_index);

	} else if (strncmp (applet_type, "MENU:", strlen ("MENU:")) == 0) {
		const char *menu;
		const char *menu_path;

		menu = &applet_type[strlen ("MENU:")];
		menu_path = strchr (menu, '/');

		if (!menu_path) {
			if (strncmp (menu, "MAIN", strlen ("MAIN")) == 0)
				success = drop_menu (panel, pos, NULL, NULL);
			else
				success = drop_menu (panel, pos, menu, NULL);
		} else {
			char *menu_filename;

			menu_filename = g_strndup (menu, menu_path - menu);
			menu_path++;
			success = drop_menu (panel, pos,
					     menu_filename, menu_path);
			g_free (menu_filename);
		}

	} else if (!strcmp (applet_type, "DRAWER:NEW")) {
		if (panel_profile_id_lists_are_writable ()) {
			panel_drawer_create (panel->toplevel, pos, NULL, FALSE, NULL);
			success = TRUE;
		} else {
			success = FALSE;
		}

	} else if (!strncmp (applet_type, "ACTION:", strlen ("ACTION:"))) {
		if (panel_profile_id_lists_are_writable ()) {
			remove_applet = panel_action_button_load_from_drag (
							panel->toplevel,
							pos,
							applet_type,
							&applet_index);
			success = TRUE;
		} else {
			success = FALSE;
		}

	} else if (!strcmp (applet_type, "MENUBAR:NEW")) {
		if (panel_profile_id_lists_are_writable ()) {
			panel_menu_bar_create (panel->toplevel, pos);
			success = TRUE;
		} else {
			success = FALSE;
		}

	} else if (!strcmp(applet_type,"SEPARATOR:NEW")) {
		if (panel_profile_id_lists_are_writable ()) {
			panel_separator_create (panel->toplevel, pos);
			success = TRUE;
		} else {
			success = FALSE;
		}

	} else if (!strcmp(applet_type,"LAUNCHER:ASK")) {
		if (panel_profile_id_lists_are_writable ()) {
			ask_about_launcher (NULL, panel, pos, TRUE);
			success = TRUE;
		} else {
			success = FALSE;
		}
	}

	if (remove_applet &&
	    action == GDK_ACTION_MOVE) {
		AppletInfo *info;
		GSList     *applet_list;

		applet_list = mate_panel_applet_list_applets ();

		info = g_slist_nth_data (applet_list, applet_index);

		if (info)
			panel_profile_delete_object (info);
	}

	return success;
}

static GtkTargetList *
get_target_list (void)
{
	static GtkTargetEntry drop_types [] = {
		{ "text/uri-list",                       0, TARGET_URL },
		{ "x-url/http",                          0, TARGET_NETSCAPE_URL },
		{ "x-url/ftp",                           0, TARGET_NETSCAPE_URL },
		{ "_NETSCAPE_URL",                       0, TARGET_NETSCAPE_URL },
		{ "application/x-panel-directory",       0, TARGET_DIRECTORY },
		{ "application/x-mate-panel-applet-iid",      0, TARGET_APPLET },
		{ "application/x-mate-panel-applet-internal", 0, TARGET_APPLET_INTERNAL },
		{ "application/x-panel-icon-internal",   0, TARGET_ICON_INTERNAL },
		{ "application/x-color",                 0, TARGET_COLOR },
		{ "property/bgimage",                    0, TARGET_BGIMAGE },
		{ "x-special/mate-reset-background",    0, TARGET_BACKGROUND_RESET },
	};
	static GtkTargetList *target_list = NULL;

	if (!target_list) {
		gint length = sizeof (drop_types) / sizeof (drop_types [0]);

		target_list = gtk_target_list_new (drop_types, length);
	}

	return target_list;
}

gboolean
panel_check_dnd_target_data (GtkWidget      *widget,
			     GdkDragContext *context,
			     guint          *ret_info,
			     GdkAtom        *ret_atom)
{
	GList *l;

	g_return_val_if_fail (widget, FALSE);

	if (!PANEL_IS_TOPLEVEL  (widget) &&
	    !BUTTON_IS_WIDGET (widget))
		return FALSE;

	if (!(gdk_drag_context_get_actions (context) & (GDK_ACTION_COPY|GDK_ACTION_MOVE)))
		return FALSE;

	for (l = gdk_drag_context_list_targets (context); l; l = l->next) {
		GdkAtom atom;
		guint   info;

		atom = GDK_POINTER_TO_ATOM (l->data);

		if (gtk_target_list_find (get_target_list (), atom, &info)) {
			if (ret_info)
				*ret_info = info;

			if (ret_atom)
				*ret_atom = atom;
			break;
		}
	}

	return l ? TRUE : FALSE;
}

static void
do_highlight (GtkWidget *widget, gboolean highlight)
{
	gboolean have_drag;

	/* FIXME: what's going on here ? How are we highlighting
	 *        the toplevel widget ? I don't think we are ...
	 */

	have_drag = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
							"have-drag"));
	if(highlight) {
		if(!have_drag) {
			g_object_set_data (G_OBJECT (widget), "have-drag",
					   GINT_TO_POINTER (TRUE));
			gtk_drag_highlight (widget);
		}
	} else {
		if(have_drag) {
			g_object_set_data (G_OBJECT (widget),
					   "have-drag", NULL);
			gtk_drag_unhighlight (widget);
		}
	}
}

gboolean
panel_check_drop_forbidden (PanelWidget    *panel,
			    GdkDragContext *context,
			    guint           info,
			    guint           time_)
{
	if (!panel)
		return FALSE;

	if (panel_lockdown_get_locked_down ())
		return FALSE;

	if (info == TARGET_APPLET_INTERNAL) {
		GtkWidget *source_widget;

		source_widget = gtk_drag_get_source_widget (context);

		if (BUTTON_IS_WIDGET (source_widget)) {
			GSList *forb;

			forb = g_object_get_data (G_OBJECT (source_widget),
						  MATE_PANEL_APPLET_FORBIDDEN_PANELS);

			if (g_slist_find (forb, panel))
				return FALSE;
		}
	}

	if (info == TARGET_ICON_INTERNAL ||
	    info == TARGET_APPLET_INTERNAL) {
		if (gdk_drag_context_get_actions (context) & GDK_ACTION_MOVE)
			gdk_drag_status (context, GDK_ACTION_MOVE, time_);
		else
			gdk_drag_status (context,
					 gdk_drag_context_get_suggested_action (context),
					 time_);

	} else if (gdk_drag_context_get_actions (context) & GDK_ACTION_COPY)
		gdk_drag_status (context, GDK_ACTION_COPY, time_);
	else
		gdk_drag_status (context,
				 gdk_drag_context_get_suggested_action (context),
				 time_);

	return TRUE;

}

static gboolean
drag_motion_cb (GtkWidget	   *widget,
		GdkDragContext     *context,
		gint                x,
		gint                y,
		guint               time)
{
	PanelToplevel *toplevel;
	PanelWidget   *panel_widget;
	guint          info;

	g_return_val_if_fail (PANEL_IS_TOPLEVEL (widget), FALSE);

	if (!panel_check_dnd_target_data (widget, context, &info, NULL))
		return FALSE;

	toplevel = PANEL_TOPLEVEL (widget);
	panel_widget = panel_toplevel_get_panel_widget (toplevel);

	if (!panel_check_drop_forbidden (panel_widget, context, info, time))
		return FALSE;

	do_highlight (widget, TRUE);

	panel_toplevel_unhide (toplevel);

	return TRUE;
}

static gboolean
drag_drop_cb (GtkWidget	        *widget,
	      GdkDragContext    *context,
	      gint               x,
	      gint               y,
	      guint              time,
	      Launcher          *launcher)
{
	GdkAtom ret_atom = NULL;

	if (!panel_check_dnd_target_data (widget, context, NULL, &ret_atom))
		return FALSE;

	gtk_drag_get_data (widget, context, ret_atom, time);

	return TRUE;
}

static void
drag_leave_cb (GtkWidget	*widget,
	       GdkDragContext   *context,
	       guint             time,
	       Launcher         *launcher)
{
	PanelToplevel *toplevel;

	do_highlight (widget, FALSE);

	toplevel = PANEL_TOPLEVEL (widget);
	panel_toplevel_queue_auto_hide (toplevel);
}

void
panel_receive_dnd_data (PanelWidget      *panel,
			guint             info,
			int               pos,
			GtkSelectionData *selection_data,
			GdkDragContext   *context,
			guint             time_)
{
	const guchar *data;
	gboolean      success = FALSE;

	if (panel_lockdown_get_locked_down ()) {
		gtk_drag_finish (context, FALSE, FALSE, time_);
		return;
	}

	data = gtk_selection_data_get_data (selection_data);

	switch (info) {
	case TARGET_URL:
		success = drop_urilist (panel, pos, (char *)data);
		break;
	case TARGET_NETSCAPE_URL:
		success = drop_url (panel, pos, (char *)data);
		break;
	case TARGET_COLOR:
		success = set_background_color (panel->toplevel, (guint16 *) data);
		break;
	case TARGET_BGIMAGE:
		success = set_background_image_from_uri (panel->toplevel, (char *) data);
		break;
	case TARGET_BACKGROUND_RESET:
		if (panel_profile_background_key_is_writable (panel->toplevel, "type")) {
			panel_profile_set_background_type (panel->toplevel, PANEL_BACK_NONE);
			success = TRUE;
		} else {
			success = FALSE;
		}
		break;
	case TARGET_DIRECTORY:
		success = drop_uri (panel, pos, (char *)data,
				    PANEL_ICON_FOLDER);
		break;
	case TARGET_APPLET:
		if (!gtk_selection_data_get_data (selection_data)) {
			gtk_drag_finish (context, FALSE, FALSE, time_);
			return;
		}
		if (panel_profile_id_lists_are_writable ()) {
			mate_panel_applet_frame_create (panel->toplevel, pos, (char *) data);
			success = TRUE;
		} else {
			success = FALSE;
		}
		break;
	case TARGET_APPLET_INTERNAL:
		success = drop_internal_applet (panel, pos, (char *)data,
						gdk_drag_context_get_selected_action (context));
		break;
	case TARGET_ICON_INTERNAL:
		success = drop_internal_icon (panel, pos, (char *)data,
					      gdk_drag_context_get_selected_action (context));
		break;
	default:
		gtk_drag_finish (context, FALSE, FALSE, time_);
		return;
	}

	gtk_drag_finish (context, success, FALSE, time_);
}

static void
drag_data_recieved_cb (GtkWidget	*widget,
		       GdkDragContext   *context,
		       gint              x,
		       gint              y,
		       GtkSelectionData *selection_data,
		       guint             info,
		       guint             time)
{
	PanelWidget *panel_widget;
	int          pos;

	g_return_if_fail (PANEL_IS_TOPLEVEL (widget));

	/* we use this only to really find out the info, we already
	   know this is an ok drop site and the info that got passed
	   to us is bogus (it's always 0 in fact) */
	if (!panel_check_dnd_target_data (widget, context, &info, NULL)) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	panel_widget = panel_toplevel_get_panel_widget (PANEL_TOPLEVEL (widget));

	pos = panel_widget_get_cursorloc (panel_widget);

	/*
	 * -1 passed to mate_panel_applet_register will turn on
	 * the insert_at_pos flag for panel_widget_add_full,
	 * which will not place it after the first applet.
	 */
	if(pos < 0)
		pos = -1;
	else if(pos > panel_widget->size)
		pos = panel_widget->size;

	panel_receive_dnd_data (
		panel_widget, info, pos, selection_data, context, time);
}

static void
panel_widget_setup(PanelWidget *panel)
{
	g_signal_connect (G_OBJECT(panel),
			  "applet_added",
			  G_CALLBACK(mate_panel_applet_added),
			  NULL);
	g_signal_connect (G_OBJECT(panel),
			  "applet_removed",
			  G_CALLBACK(mate_panel_applet_removed),
			  NULL);
	g_signal_connect (G_OBJECT(panel),
			  "applet_move",
			  G_CALLBACK(mate_panel_applet_move),
			  NULL);
	g_signal_connect (G_OBJECT (panel),
			  "back_change",
			  G_CALLBACK (panel_back_change),
			  NULL);
	g_signal_connect (G_OBJECT (panel),
			  "size_change",
			  G_CALLBACK (panel_size_change),
			  NULL);
}

PanelData *
panel_setup (PanelToplevel *toplevel)
{
	PanelWidget *panel_widget;
	PanelData   *pd;

	g_return_val_if_fail (PANEL_IS_TOPLEVEL (toplevel), NULL);

	panel_widget = panel_toplevel_get_panel_widget (toplevel);

	pd = g_new0 (PanelData,1);
	pd->menu = NULL;
	pd->panel = GTK_WIDGET (toplevel);
	pd->insertion_pos = -1;
	pd->deactivate_idle = 0;

	panel_list = g_slist_append (panel_list, pd);

	g_object_set_data (G_OBJECT (toplevel), "PanelData", pd);

	panel_lockdown_notify_add (G_CALLBACK (panel_recreate_context_menu),
				   pd);

	panel_widget_setup (panel_widget);

	g_signal_connect (toplevel, "drag_data_received",
			  G_CALLBACK (drag_data_recieved_cb), NULL);
	g_signal_connect (toplevel, "drag_motion",
			  G_CALLBACK (drag_motion_cb), NULL);
	g_signal_connect (toplevel, "drag_leave",
			  G_CALLBACK (drag_leave_cb), NULL);
	g_signal_connect (toplevel, "drag_drop",
			  G_CALLBACK (drag_drop_cb), NULL);

	gtk_drag_dest_set (GTK_WIDGET (toplevel), 0, NULL, 0, 0);

	g_signal_connect (toplevel, "key-press-event",
			  G_CALLBACK (panel_key_press_event), NULL);
	g_signal_connect (toplevel, "button-press-event",
			  G_CALLBACK (panel_button_press_event), NULL);
	g_signal_connect (toplevel, "popup-menu",
			  G_CALLBACK (panel_popup_menu_signal), NULL);

	g_signal_connect_swapped (toplevel, "notify::orientation",
				  G_CALLBACK (panel_orient_change), panel_widget);

	g_signal_connect (toplevel, "destroy", G_CALLBACK (panel_destroy), pd);

	return pd;
}

GdkScreen *
panel_screen_from_panel_widget (PanelWidget *panel)
{
	g_return_val_if_fail (PANEL_IS_WIDGET (panel), NULL);
	g_return_val_if_fail (PANEL_IS_TOPLEVEL (panel->toplevel), NULL);

	return gtk_window_get_screen (GTK_WINDOW (panel->toplevel));
}

gboolean
panel_is_applet_right_stick (GtkWidget *applet)
{
	GtkWidget   *parent;
	PanelWidget *panel_widget;

	g_return_val_if_fail (GTK_IS_WIDGET (applet), FALSE);

	parent = gtk_widget_get_parent (applet);

	g_return_val_if_fail (PANEL_IS_WIDGET (parent), FALSE);

	panel_widget = PANEL_WIDGET (parent);

	if (!panel_toplevel_get_expand (panel_widget->toplevel))
		return FALSE;

	return panel_widget_is_applet_stuck (panel_widget, applet);
}

static void
panel_delete_without_query (PanelToplevel *toplevel)
{
	PanelWidget *panel_widget;

	panel_widget = panel_toplevel_get_panel_widget (toplevel);

	if (panel_toplevel_get_is_attached (toplevel) &&
	    panel_widget->master_widget) {
		AppletInfo *info;

		info = g_object_get_data (G_OBJECT (panel_widget->master_widget),
					  "applet_info");

		panel_profile_delete_object (info);
	} else
		panel_profile_delete_toplevel (toplevel);
}

static void
panel_deletion_response (GtkWidget     *dialog,
			 int            response,
			 PanelToplevel *toplevel)
{
	if (response == GTK_RESPONSE_OK) {
		panel_push_window_busy (dialog);
		panel_delete_without_query (toplevel);
		panel_pop_window_busy (dialog);
	}

	gtk_widget_destroy (dialog);
}

static void
panel_deletion_destroy_dialog (GtkWidget *widget,
			       PanelToplevel *toplevel)
{
	panel_toplevel_pop_autohide_disabler (toplevel);
	g_object_set_data (G_OBJECT (toplevel), "panel-delete-dialog", NULL);
}

GtkWidget *
panel_deletion_dialog (PanelToplevel *toplevel)
{

	GtkWidget *dialog;
	char *text1;
	char *text2;

	if (panel_toplevel_get_is_attached (toplevel)) {
		text1 = _("Delete this drawer?");
		text2 = _("When a drawer is deleted, the drawer and its\n"
			 "settings are lost.");
	} else {
		text1 = _("Delete this panel?");
		text2 = _("When a panel is deleted, the panel and its\n"
			 "settings are lost.");
	}

	dialog = gtk_message_dialog_new (
			GTK_WINDOW (toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_NONE,
			"%s", text1);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          "%s", text2);

	panel_dialog_add_button (GTK_DIALOG (dialog),
				 _("_Cancel"), "process-stop",
				 GTK_RESPONSE_CANCEL);

	panel_dialog_add_button (GTK_DIALOG (dialog),
				 _("_Delete"), "edit-delete",
				 GTK_RESPONSE_OK);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	gtk_window_set_screen (GTK_WINDOW (dialog),
				gtk_window_get_screen (GTK_WINDOW (toplevel)));

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

	 g_signal_connect (dialog, "destroy",
                           G_CALLBACK (panel_deletion_destroy_dialog),
                           toplevel);

	g_object_set_data (G_OBJECT (toplevel), "panel-delete-dialog", dialog);
	panel_toplevel_push_autohide_disabler (toplevel);

	return dialog;
}

static void
panel_query_deletion (PanelToplevel *toplevel)
{
	GtkWidget *dialog;

	dialog = g_object_get_data (G_OBJECT (toplevel), "panel-delete-dialog");

	if (dialog) {
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	dialog = panel_deletion_dialog (toplevel);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (panel_deletion_response),
			  toplevel);

	g_signal_connect_object (toplevel, "destroy",
				 G_CALLBACK (gtk_widget_destroy),
				 dialog,
				 G_CONNECT_SWAPPED);

	gtk_widget_show_all (dialog);
}

void
panel_delete (PanelToplevel *toplevel)
{
	PanelWidget *panel_widget;

	panel_widget = panel_toplevel_get_panel_widget (toplevel);

	if (!panel_global_config_get_confirm_panel_remove () ||
	    !g_list_length (panel_widget->applet_list)) {
		panel_delete_without_query (toplevel);
		return;
	}

	panel_query_deletion (toplevel);
}
