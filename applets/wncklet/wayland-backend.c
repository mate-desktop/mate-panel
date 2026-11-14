/* Wncklet applet Wayland backend */

/*
 * Copyright (C) 2019 William Wold
 * Copyright (C) 2026 Aleksey Samoilov
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

/* This file only implements the windowing-system half of the tasklist. All of
 * the widget logic lives in tasklist-core.c and is shared with the other
 * backends.
 */

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be compiled when HAVE_WAYLAND is enabled
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkwayland.h>
#include <libxfce4windowing/libxfce4windowing.h>

#include "tasklist-backend.h"
#include "wayland-backend.h"

static gpointer
wayland_get_screen (void)
{
	return xfw_screen_get_default ();
}

static GList *
wayland_list_windows (gpointer screen)
{
	return xfw_screen_get_windows (screen);
}

static gpointer
wayland_get_active_window (gpointer screen)
{
	return xfw_screen_get_active_window (screen);
}

static const gchar *
wayland_get_window_name (gpointer window)
{
	return xfw_window_get_name (window);
}

static GIcon *
wayland_get_window_gicon (gpointer window)
{
	return xfw_window_get_gicon (window);
}

static GdkPixbuf *
wayland_get_window_pixbuf (gpointer window, gint size, gint scale)
{
	return xfw_window_get_icon (window, size, scale);
}

static TasklistWindowState
wayland_get_window_state (gpointer window)
{
	XfwWindowState state;
	TasklistWindowState result = 0;

	state = xfw_window_get_state (window);

	if (state & XFW_WINDOW_STATE_ACTIVE)
		result |= TASKLIST_STATE_ACTIVE;
	if (state & XFW_WINDOW_STATE_MAXIMIZED)
		result |= TASKLIST_STATE_MAXIMIZED;
	if (state & XFW_WINDOW_STATE_MINIMIZED)
		result |= TASKLIST_STATE_MINIMIZED;
	if (state & XFW_WINDOW_STATE_FULLSCREEN)
		result |= TASKLIST_STATE_FULLSCREEN;
	if (state & XFW_WINDOW_STATE_URGENT)
		result |= TASKLIST_STATE_URGENT;

	return result;
}

static gpointer
wayland_get_window_application (gpointer window)
{
	return xfw_window_get_application (window);
}

static gulong
wayland_get_window_id (gpointer window)
{
	/* On Wayland there is no window XID, so the handle is used to identify
	 * a window in the drag-and-drop payload */
	return (gulong) window;
}

static gboolean
wayland_window_is_skip_tasklist (gpointer window)
{
	return xfw_window_is_skip_tasklist (window);
}

static const gchar *
wayland_get_app_name (gpointer app)
{
	return xfw_application_get_name (app);
}

static GIcon *
wayland_get_app_gicon (gpointer app)
{
	return xfw_application_get_gicon (app);
}

static const gchar *
wayland_get_fallback_icon_name (void)
{
	return "unknown";
}

/* xfw reports a single workspace per window, so the active one is the active
 * workspace of the group that window's workspace belongs to. */
static gboolean
wayland_window_is_on_active_workspace (gpointer window)
{
	XfwWorkspace *workspace, *active;
	XfwWorkspaceGroup *group;

	workspace = xfw_window_get_workspace (window);
	if (workspace == NULL)
		return TRUE;

	group = xfw_workspace_get_workspace_group (workspace);
	if (group == NULL)
		return TRUE;

	active = xfw_workspace_group_get_active_workspace (group);
	if (active == NULL)
		return TRUE;

	return xfw_window_is_on_workspace (window, active);
}

static void
wayland_window_move_to_workspace (gpointer window)
{
	XfwWorkspace *workspace;
	GError *error = NULL;

	workspace = xfw_window_get_workspace (window);
	if (workspace == NULL)
		return;

	if (!xfw_workspace_activate (workspace, &error))
	{
		g_clear_error (&error);
	}
}

static void
wayland_activate_window (gpointer window, guint32 user_time)
{
	xfw_window_activate (window, NULL, user_time, NULL);
}

static void
wayland_close_window (gpointer window, guint32 timestamp)
{
	xfw_window_close (window, timestamp, NULL);
}

static void
wayland_set_window_maximized (gpointer window, gboolean maximized)
{
	xfw_window_set_maximized (window, maximized, NULL);
}

static void
wayland_set_window_minimized (gpointer window, gboolean minimized)
{
	xfw_window_set_minimized (window, minimized, NULL);
}

typedef struct
{
	GtkWidget *menu, *minimize, *maximize, *shade, *move, *resize;
	GtkWidget *on_top, *stick, *unstick;
	GtkWidget *move_left, *move_right, *move_up, *move_down, *move_other;
	GtkWidget *close, *close_all;
	GList *windows;
} WaylandContextMenu;

static void
wayland_context_menu_free (WaylandContextMenu *menu)
{
	g_list_free (menu->windows);
	g_free (menu);
}

static XfwWindow *
wayland_menu_window (GtkMenuItem *item)
{
	return g_object_get_data (G_OBJECT (item), "wayland-menu-window");
}

static gboolean
wayland_window_has_state (XfwWindow *window, XfwWindowState state)
{
	return (xfw_window_get_state (window) & state) != 0;
}

static void
wayland_menu_maximize (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_set_maximized (window, !wayland_window_has_state (window, XFW_WINDOW_STATE_MAXIMIZED), NULL);
}

static void
wayland_menu_minimize (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_set_minimized (window, !wayland_window_has_state (window, XFW_WINDOW_STATE_MINIMIZED), NULL);
}

static void
wayland_menu_shade (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_set_shaded (window, !xfw_window_is_shaded (window), NULL);
}

static void
wayland_menu_move (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_start_move (window, NULL);
}

static void
wayland_menu_resize (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_start_resize (window, NULL);
}

static void
wayland_menu_above (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_set_above (window, !xfw_window_is_above (window), NULL);
}

static void
wayland_menu_pin (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_set_pinned (window, GPOINTER_TO_INT (user_data), NULL);
}

static void
wayland_menu_workspace (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	XfwWorkspace *workspace = g_object_get_data (G_OBJECT (item), "target-workspace");
	if (window && workspace)
		xfw_window_move_to_workspace (window, workspace, NULL);
}

static void
wayland_menu_close (GtkMenuItem *item, gpointer user_data)
{
	XfwWindow *window = wayland_menu_window (item);
	if (window)
		xfw_window_close (window, 0, NULL);
}

static void
wayland_menu_close_all (GtkMenuItem *item, gpointer user_data)
{
	WaylandContextMenu *menu = user_data;
	GList *l;
	for (l = menu->windows; l != NULL; l = l->next)
		xfw_window_close (l->data, 0, NULL);
}

static GtkWidget *
wayland_menu_append (GtkWidget *shell, GtkWidget *item, GCallback callback, gpointer data)
{
	gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
	if (callback)
		g_signal_connect (item, "activate", callback, data);
	return item;
}

static XfwWorkspace *
wayland_menu_current_workspace (XfwWindow *window)
{
	XfwWorkspace *workspace = xfw_window_get_workspace (window);
	XfwWorkspaceManager *manager;
	GList *l;

	if (workspace)
		return workspace;
	manager = xfw_screen_get_workspace_manager (xfw_screen_get_default ());
	if (!manager)
		return NULL;
	for (l = xfw_workspace_manager_list_workspaces (manager); l != NULL; l = l->next)
		if (xfw_workspace_get_state (l->data) & XFW_WORKSPACE_STATE_ACTIVE)
			return l->data;
	return NULL;
}

static void
wayland_menu_update (WaylandContextMenu *menu, XfwWindow *window, gboolean grouping)
{
	XfwWindowCapabilities capabilities = xfw_window_get_capabilities (window);
	XfwWorkspaceManager *manager = xfw_screen_get_workspace_manager (xfw_screen_get_default ());
	XfwWorkspace *current = wayland_menu_current_workspace (window);
	GtkWidget *submenu;
	GList *l, *children;
	gboolean minimized = wayland_window_has_state (window, XFW_WINDOW_STATE_MINIMIZED);
	gboolean maximized = wayland_window_has_state (window, XFW_WINDOW_STATE_MAXIMIZED);
	gboolean shaded = xfw_window_is_shaded (window);
	gboolean above = xfw_window_is_above (window);
	gboolean can_change_workspace = capabilities & XFW_WINDOW_CAPABILITIES_CAN_CHANGE_WORKSPACE;
	struct { GtkWidget *item; XfwDirection direction; } directions[] = {
		{ menu->move_left, XFW_DIRECTION_LEFT }, { menu->move_right, XFW_DIRECTION_RIGHT },
		{ menu->move_up, XFW_DIRECTION_UP }, { menu->move_down, XFW_DIRECTION_DOWN }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (directions); i++)
	{
		XfwWorkspace *target = current ? xfw_workspace_get_neighbor (current, directions[i].direction) : NULL;
		g_object_set_data (G_OBJECT (directions[i].item), "target-workspace", target);
		gtk_widget_set_sensitive (directions[i].item, can_change_workspace && target != NULL);
	}

	gtk_menu_item_set_label (GTK_MENU_ITEM (menu->minimize), minimized ? _("Unminimize") : _("Mi_nimize"));
	gtk_menu_item_set_label (GTK_MENU_ITEM (menu->maximize), maximized ? _("Unma_ximize") : _("Ma_ximize"));
	gtk_menu_item_set_label (GTK_MENU_ITEM (menu->shade), shaded ? _("_Unroll") : _("Roll _Up"));
	gtk_widget_set_sensitive (menu->minimize, capabilities & (minimized ?
		XFW_WINDOW_CAPABILITIES_CAN_UNMINIMIZE : XFW_WINDOW_CAPABILITIES_CAN_MINIMIZE));
	gtk_widget_set_sensitive (menu->maximize, capabilities & (maximized ?
		XFW_WINDOW_CAPABILITIES_CAN_UNMAXIMIZE : XFW_WINDOW_CAPABILITIES_CAN_MAXIMIZE));
	gtk_widget_set_sensitive (menu->shade, capabilities & (shaded ?
		XFW_WINDOW_CAPABILITIES_CAN_UNSHADE : XFW_WINDOW_CAPABILITIES_CAN_SHADE));
	gtk_widget_set_sensitive (menu->move, capabilities & XFW_WINDOW_CAPABILITIES_CAN_MOVE);
	gtk_widget_set_sensitive (menu->resize, capabilities & XFW_WINDOW_CAPABILITIES_CAN_RESIZE);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu->on_top), above);
	gtk_widget_set_sensitive (menu->on_top, capabilities & (above ?
		XFW_WINDOW_CAPABILITIES_CAN_UNPLACE_ABOVE : XFW_WINDOW_CAPABILITIES_CAN_PLACE_ABOVE));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (xfw_window_is_pinned (window) ? menu->stick : menu->unstick), TRUE);
	gtk_widget_set_sensitive (menu->stick, can_change_workspace);
	gtk_widget_set_sensitive (menu->unstick, can_change_workspace);

	submenu = gtk_menu_new ();
	for (l = manager ? xfw_workspace_manager_list_workspaces (manager) : NULL; l != NULL; l = l->next)
	{
		XfwWorkspace *workspace = l->data;
		GtkWidget *item;
		const gchar *name;
		if (xfw_workspace_get_state (workspace) & XFW_WORKSPACE_STATE_HIDDEN)
			continue;
		name = xfw_workspace_get_name (workspace);
		if (!name)
			name = xfw_workspace_get_id (workspace);
		item = gtk_menu_item_new_with_label (name ? name : "");
		g_object_set_data (G_OBJECT (item), "wayland-menu-window", window);
		g_object_set_data (G_OBJECT (item), "target-workspace", workspace);
		gtk_widget_set_sensitive (item, can_change_workspace && workspace != current);
		wayland_menu_append (submenu, item, G_CALLBACK (wayland_menu_workspace), NULL);
	}
	gtk_widget_show_all (submenu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu->move_other), submenu);
	children = gtk_container_get_children (GTK_CONTAINER (submenu));
	gtk_widget_set_sensitive (menu->move_other, can_change_workspace && children != NULL);
	g_list_free (children);
	gtk_widget_set_visible (menu->close_all, grouping);
}

static void
wayland_show_window_menu (GtkWidget *button, GdkEvent *event, gpointer window,
				  GList *group_windows, gboolean grouping)
{
	WaylandContextMenu *menu = g_new0 (WaylandContextMenu, 1);
	GtkWidget *shell;
	GSList *radio_group = NULL;
	GList *items, *l;

	menu->windows = g_list_copy (group_windows);
	menu->menu = shell = gtk_menu_new ();
	menu->minimize = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Mi_nimize")), G_CALLBACK (wayland_menu_minimize), NULL);
	menu->maximize = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Ma_ximize")), G_CALLBACK (wayland_menu_maximize), NULL);
	menu->shade = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Roll _Up")), G_CALLBACK (wayland_menu_shade), NULL);
	menu->move = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("_Move")), G_CALLBACK (wayland_menu_move), NULL);
	menu->resize = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("_Resize")), G_CALLBACK (wayland_menu_resize), NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (shell), gtk_separator_menu_item_new ());
	menu->on_top = wayland_menu_append (shell, gtk_check_menu_item_new_with_mnemonic (_("Always on _Top")), G_CALLBACK (wayland_menu_above), NULL);
	menu->stick = gtk_radio_menu_item_new_with_mnemonic (radio_group, _("_Always on Visible Workspace"));
	radio_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menu->stick));
	wayland_menu_append (shell, menu->stick, G_CALLBACK (wayland_menu_pin), GINT_TO_POINTER (TRUE));
	menu->unstick = gtk_radio_menu_item_new_with_mnemonic (radio_group, _("_Only on This Workspace"));
	wayland_menu_append (shell, menu->unstick, G_CALLBACK (wayland_menu_pin), GINT_TO_POINTER (FALSE));
	menu->move_left = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Move to Workspace _Left")), G_CALLBACK (wayland_menu_workspace), NULL);
	menu->move_right = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Move to Workspace R_ight")), G_CALLBACK (wayland_menu_workspace), NULL);
	menu->move_up = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Move to Workspace _Up")), G_CALLBACK (wayland_menu_workspace), NULL);
	menu->move_down = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Move to Workspace _Down")), G_CALLBACK (wayland_menu_workspace), NULL);
	menu->move_other = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Move to Another _Workspace")), NULL, NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (shell), gtk_separator_menu_item_new ());
	menu->close = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("_Close")), G_CALLBACK (wayland_menu_close), NULL);
	menu->close_all = wayland_menu_append (shell, gtk_menu_item_new_with_mnemonic (_("Close All Windows")), G_CALLBACK (wayland_menu_close_all), menu);

	items = gtk_container_get_children (GTK_CONTAINER (shell));
	for (l = items; l != NULL; l = l->next)
		g_object_set_data (G_OBJECT (l->data), "wayland-menu-window", window);
	g_list_free (items);
	g_object_set_data_full (G_OBJECT (shell), "wayland-context-menu", menu,
			(GDestroyNotify) wayland_context_menu_free);
	gtk_widget_show_all (shell);
	wayland_menu_update (menu, window, grouping);
	g_signal_connect_swapped (shell, "deactivate", G_CALLBACK (gtk_widget_destroy), shell);
	gtk_menu_popup_at_widget (GTK_MENU (shell), button, GDK_GRAVITY_NORTH_WEST,
			GDK_GRAVITY_SOUTH_WEST, event);
}

static void
wayland_set_window_button_geometry (gpointer window,
					GdkWindow *relative_to,
					const GdkRectangle *rect)
{
	GError *error = NULL;

	if (!xfw_window_set_button_geometry (window, relative_to, rect, &error))
		g_clear_error (&error);
}

/* The core widget is passed in as the signal user data, so that the core can
 * stop every handler it owns with a single
 * g_signal_handlers_disconnect_by_data() on itself. */

static void
wayland_screen_window_opened (XfwScreen *screen, XfwWindow *window, gpointer core)
{
	tasklist_core_window_added (GTK_WIDGET (core), window);
}

static void
wayland_screen_window_closed (XfwScreen *screen, XfwWindow *window, gpointer core)
{
	tasklist_core_window_removed (GTK_WIDGET (core), window);
}

static void
wayland_screen_active_window_changed (XfwScreen *screen,
				      XfwWindow *previous_window,
				      gpointer core)
{
	tasklist_core_active_window_changed (GTK_WIDGET (core));
}

static void
wayland_window_state_changed (XfwWindow *window,
			      XfwWindowState changed_mask,
			      XfwWindowState new_state,
			      gpointer core)
{
	tasklist_core_window_state_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_name_changed (XfwWindow *window, gpointer core)
{
	tasklist_core_window_name_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_icon_changed (XfwWindow *window, gpointer core)
{
	tasklist_core_window_icon_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_application_changed (XfwWindow *window,
				   GParamSpec *pspec,
				   gpointer core)
{
	tasklist_core_window_application_changed (GTK_WIDGET (core), window);
}

static void
wayland_app_icon_changed (XfwApplication *app, gpointer core)
{
	tasklist_core_app_icon_changed (GTK_WIDGET (core), app);
}

static void
wayland_app_name_changed (XfwApplication *app, GParamSpec *pspec, gpointer core)
{
	tasklist_core_app_name_changed (GTK_WIDGET (core), app);
}

static void
wayland_set_screen_tracking (GtkWidget *core, gpointer screen, gboolean track)
{
	if (track)
	{
		g_signal_connect (screen, "window-opened",
				  G_CALLBACK (wayland_screen_window_opened), core);
		g_signal_connect (screen, "window-closed",
				  G_CALLBACK (wayland_screen_window_closed), core);
		g_signal_connect (screen, "active-window-changed",
				  G_CALLBACK (wayland_screen_active_window_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (screen, core);
	}
}

static void
wayland_set_window_tracking (GtkWidget *core, gpointer window, gboolean track)
{
	if (track)
	{
		g_signal_connect (window, "state-changed",
				  G_CALLBACK (wayland_window_state_changed), core);
		g_signal_connect (window, "name-changed",
				  G_CALLBACK (wayland_window_name_changed), core);
		g_signal_connect (window, "icon-changed",
				  G_CALLBACK (wayland_window_icon_changed), core);
		g_signal_connect (window, "notify::application",
				  G_CALLBACK (wayland_window_application_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (window, core);
	}
}

static void
wayland_set_app_tracking (GtkWidget *core, gpointer app, gboolean track)
{
	if (track)
	{
		g_signal_connect (app, "icon-changed",
				  G_CALLBACK (wayland_app_icon_changed), core);
		g_signal_connect (app, "notify::name",
				  G_CALLBACK (wayland_app_name_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (app, core);
	}
}

static const TasklistBackend wayland_backend = {
	.get_screen               = wayland_get_screen,
	.list_windows             = wayland_list_windows,
	.get_active_window        = wayland_get_active_window,
	.get_window_name          = wayland_get_window_name,
	.get_window_gicon         = wayland_get_window_gicon,
	.get_window_pixbuf        = wayland_get_window_pixbuf,
	.get_window_state         = wayland_get_window_state,
	.get_window_application   = wayland_get_window_application,
	.get_window_id            = wayland_get_window_id,
	.window_is_skip_tasklist  = wayland_window_is_skip_tasklist,
	.get_app_name             = wayland_get_app_name,
	.get_app_gicon            = wayland_get_app_gicon,
	.get_fallback_icon_name   = wayland_get_fallback_icon_name,
	.window_is_on_active_workspace = wayland_window_is_on_active_workspace,
	.window_move_to_workspace = wayland_window_move_to_workspace,
	.activate_window          = wayland_activate_window,
	.close_window             = wayland_close_window,
	.set_window_maximized     = wayland_set_window_maximized,
	.set_window_minimized     = wayland_set_window_minimized,
	.show_window_menu         = wayland_show_window_menu,
	.set_window_button_geometry = wayland_set_window_button_geometry,
	.set_screen_tracking      = wayland_set_screen_tracking,
	.set_window_tracking      = wayland_set_window_tracking,
	.set_app_tracking         = wayland_set_app_tracking,
};

const TasklistBackend *
wayland_tasklist_backend (void)
{
	GdkDisplay *display;

	display = gdk_display_get_default ();
	if (display == NULL || !GDK_IS_WAYLAND_DISPLAY (display))
		return NULL;

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	return &wayland_backend;
}
