/* Wayland backend for the Window Selector applet */

/*
 * Copyright (C) 2026 MATE Desktop Team
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

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be compiled when HAVE_WAYLAND is enabled
#endif

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <glib/gi18n.h>
#include <libxfce4windowing/libxfce4windowing.h>

#include "wayland-window-menu.h"

#define SELECTOR_MAX_WIDTH 50

/* a window in the drag-and-drop payload is identified in-process by the
 * XfwWindow pointer, matching the tasklist backend (see wayland-backend.c) */
static GtkTargetEntry source_targets[] = {
	{ "application/x-wnck-window-id", 0, 0 }
};

typedef struct {
	GtkWidget *menu;
	GtkWidget *root_item;
	GtkWidget *image;
	GtkWidget *submenu;
	GtkWidget *no_windows_item;
	GHashTable *window_hash;
	XfwScreen *screen;
	XfwWorkspaceManager *workspace_manager;
} WaylandWindowMenu;

static void rebuild_menu (WaylandWindowMenu *data);

/* ---- Helpers ---- */

static XfwWorkspace *
get_active_workspace (WaylandWindowMenu *data)
{
	GList *workspaces, *l;

	if (!data->workspace_manager)
		return NULL;

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);
	for (l = workspaces; l != NULL; l = l->next)
	{
		XfwWorkspace *ws = l->data;
		XfwWorkspaceState state = xfw_workspace_get_state (ws);
		if (state & XFW_WORKSPACE_STATE_ACTIVE)
			return ws;
	}

	return NULL;
}

static void
update_root_icon (WaylandWindowMenu *data)
{
	XfwWindow *window;
	GIcon *icon = NULL;

	window = xfw_screen_get_active_window (data->screen);
	if (window)
	{
		XfwApplication *app = xfw_window_get_application (window);
		if (app)
			icon = xfw_application_get_gicon (app);
		if (icon == NULL)
			icon = xfw_window_get_gicon (window);
	}

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (data->image), icon,
		                        GTK_ICON_SIZE_LARGE_TOOLBAR);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (data->image),
		                            "mate-panel-window-menu",
		                            GTK_ICON_SIZE_LARGE_TOOLBAR);
}

/* ---- Per-window signal handlers ---- */

static void
on_menu_item_activate (GtkMenuItem *item, XfwWindow *window)
{
	xfw_window_activate (window, NULL,
	                      gtk_get_current_event_time (), NULL);
}

static void
on_drag_begin (GtkWidget *widget, GdkDragContext *context, XfwWindow *window)
{
	GdkPixbuf *icon;
	gint scale = gtk_widget_get_scale_factor (widget);

	icon = xfw_window_get_icon (window, 16, scale);
	if (icon != NULL)
		gtk_drag_set_icon_pixbuf (context, icon, 0, 0);
}

static void
on_drag_data_get (GtkWidget *widget, GdkDragContext *context,
                  GtkSelectionData *selection_data, guint info, guint time,
                  XfwWindow *window)
{
	gulong wid = (gulong) window;
	gtk_selection_data_set (selection_data,
	                        gtk_selection_data_get_target (selection_data),
	                        8, (guchar *) &wid, sizeof (wid));
}

static void
set_icon_dimmed (GtkWidget *image, gboolean dimmed)
{
	GtkStyleContext *context = gtk_widget_get_style_context (image);

	if (dimmed)
		gtk_style_context_add_class (context, "wnck-selector-dimmed");
	else
		gtk_style_context_remove_class (context, "wnck-selector-dimmed");
}

static void
on_window_name_changed (XfwWindow *window, GtkWidget *item)
{
	GtkWidget *label = g_object_get_data (G_OBJECT (item), "label-widget");
	const char *name;

	if (label == NULL)
		return;

	name = xfw_window_get_name (window);
	if (name == NULL)
		name = "";
	gtk_label_set_label (GTK_LABEL (label), name);
}

static void
on_window_icon_changed (XfwWindow *window, GtkWidget *item)
{
	GtkWidget *image = g_object_get_data (G_OBJECT (item), "icon-widget");
	GIcon *icon = NULL;
	XfwApplication *app;

	if (image == NULL)
		return;

	app = xfw_window_get_application (window);
	if (app != NULL)
		icon = xfw_application_get_gicon (app);
	if (icon == NULL)
		icon = xfw_window_get_gicon (window);

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (image), icon,
		                        GTK_ICON_SIZE_MENU);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (image), "unknown",
		                            GTK_ICON_SIZE_MENU);
}

static void
on_window_state_changed (XfwWindow *window, XfwWindowState changed_mask,
                         XfwWindowState new_state, GtkWidget *item)
{
	if (changed_mask & XFW_WINDOW_STATE_SKIP_TASKLIST)
	{
		if (xfw_window_is_skip_tasklist (window))
			gtk_widget_hide (item);
		else
			gtk_widget_show (item);
	}

	if (changed_mask & XFW_WINDOW_STATE_URGENT)
	{
		GtkWidget *label = g_object_get_data (G_OBJECT (item),
		                                     "label-widget");
		if (label == NULL)
			return;

		if (xfw_window_is_urgent (window))
		{
			PangoAttrList *attrs = pango_attr_list_new ();
			pango_attr_list_insert (attrs,
			                        pango_attr_weight_new (PANGO_WEIGHT_BOLD));
			gtk_label_set_attributes (GTK_LABEL (label), attrs);
			pango_attr_list_unref (attrs);
		}
		else
		{
			gtk_label_set_attributes (GTK_LABEL (label), NULL);
		}
	}

	if (changed_mask & XFW_WINDOW_STATE_MINIMIZED)
	{
		GtkWidget *image = g_object_get_data (G_OBJECT (item),
		                                      "icon-widget");
		if (image != NULL)
			set_icon_dimmed (image,
			                 (new_state & XFW_WINDOW_STATE_MINIMIZED) != 0);
	}
}

/* ---- Menu item creation ---- */

static GtkWidget *
create_window_item (WaylandWindowMenu *data, XfwWindow *window)
{
	GtkWidget *item, *box, *image, *label;
	const char *name;
	GIcon *icon = NULL;
	XfwApplication *app;
	XfwWindowState state;
	PangoAttrList *attrs;

	name = xfw_window_get_name (window);
	if (name == NULL)
		name = "";

	/* Keep the window alive while it is tracked by the menu */
	g_object_ref (window);

	item = gtk_menu_item_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

	/* icon */
	image = gtk_image_new ();
	app = xfw_window_get_application (window);
	if (app != NULL)
		icon = xfw_application_get_gicon (app);
	if (icon == NULL)
		icon = xfw_window_get_gicon (window);

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (image), icon,
		                        GTK_ICON_SIZE_MENU);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (image), "unknown",
		                            GTK_ICON_SIZE_MENU);

	/* label */
	label = gtk_label_new (name);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (label), SELECTOR_MAX_WIDTH);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);

	/* bold for urgent windows */
	state = xfw_window_get_state (window);
	if (state & XFW_WINDOW_STATE_URGENT)
	{
		attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs,
		                        pango_attr_weight_new (PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes (GTK_LABEL (label), attrs);
		pango_attr_list_unref (attrs);
	}

	/* dimmed for minimized windows */
	if (state & XFW_WINDOW_STATE_MINIMIZED)
		set_icon_dimmed (image, TRUE);

	gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 2);
	gtk_container_add (GTK_CONTAINER (item), box);

	g_object_set_data (G_OBJECT (item), "icon-widget", image);
	g_object_set_data (G_OBJECT (item), "label-widget", label);

	g_signal_connect (item, "activate",
	                  G_CALLBACK (on_menu_item_activate), window);

	g_signal_connect (window, "name-changed",
	                  G_CALLBACK (on_window_name_changed), item);
	g_signal_connect (window, "icon-changed",
	                  G_CALLBACK (on_window_icon_changed), item);
	g_signal_connect (window, "state-changed",
	                  G_CALLBACK (on_window_state_changed), item);

	/* drag the menu item to move the window (matches WnckSelector) */
	gtk_drag_source_set (item, GDK_BUTTON1_MASK,
	                     source_targets, G_N_ELEMENTS (source_targets),
	                     GDK_ACTION_MOVE);
	g_signal_connect (item, "drag-begin",
	                  G_CALLBACK (on_drag_begin), window);
	g_signal_connect (item, "drag-data-get",
	                  G_CALLBACK (on_drag_data_get), window);

	g_hash_table_insert (data->window_hash, window, item);

	gtk_widget_show_all (item);
	return item;
}

static void
append_window_item (WaylandWindowMenu *data, XfwWindow *window)
{
	GtkWidget *item = create_window_item (data, window);
	gtk_menu_shell_append (GTK_MENU_SHELL (data->submenu), item);
}

static void
add_workspace_label (WaylandWindowMenu *data, XfwWorkspace *ws)
{
	GtkWidget *item, *label;
	const char *ws_name;
	char *markup;

	ws_name = xfw_workspace_get_name (ws);
	if (ws_name == NULL || ws_name[0] == '\0')
		ws_name = xfw_workspace_get_id (ws);

	item = gtk_separator_menu_item_new ();
	label = gtk_label_new ("");

	markup = g_markup_printf_escaped ("<span size=\"small\" style=\"italic\">%s</span>",
	                                  ws_name);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	gtk_label_set_xalign (GTK_LABEL (label), 1.0);
	g_free (markup);

	gtk_widget_show (label);
	gtk_container_add (GTK_CONTAINER (item), label);
	gtk_menu_shell_append (GTK_MENU_SHELL (data->submenu), item);
}

/* ---- Disconnect / rebuild ---- */

static void
disconnect_window_signals (WaylandWindowMenu *data)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, data->window_hash);
	while (g_hash_table_iter_next (&iter, &key, &value))
		g_signal_handlers_disconnect_by_data (key, value);

	g_hash_table_remove_all (data->window_hash);
}

static void
rebuild_menu (WaylandWindowMenu *data)
{
	GList *children, *l, *wins, *workspaces, *wl;
	XfwWorkspace *active_ws;
	gboolean added_separator;

	/* 1. Disconnect per-window signals from previous menu items */
	disconnect_window_signals (data);

	/* 2. Remove all existing children from the submenu */
	children = gtk_container_get_children (GTK_CONTAINER (data->submenu));
	for (l = children; l != NULL; l = l->next)
		gtk_widget_destroy (GTK_WIDGET (l->data));
	g_list_free (children);

	data->no_windows_item = NULL;

	wins = xfw_screen_get_windows_stacked (data->screen);
	active_ws = get_active_workspace (data);

	if (data->workspace_manager == NULL)
	{
		/* No workspace support: flat list of all windows */
		for (l = wins; l != NULL; l = l->next)
		{
			XfwWindow *w = l->data;
			if (xfw_window_is_skip_tasklist (w))
				continue;
			append_window_item (data, w);
		}
	}
	else
	{
		/* Add pinned + current workspace windows first */
		for (l = wins; l != NULL; l = l->next)
		{
			XfwWindow *w = l->data;
			XfwWorkspace *ws;

			if (xfw_window_is_skip_tasklist (w))
				continue;

			ws = xfw_window_get_workspace (w);
			/* Windows whose workspace is not known yet report NULL; keep
			 * them in the current-workspace group so they aren't dropped */
			if (xfw_window_is_pinned (w) ||
			    ws == NULL ||
			    ws == active_ws)
			{
				append_window_item (data, w);
			}
		}

		/* Add other workspace sections */
		added_separator = FALSE;

		workspaces = xfw_workspace_manager_list_workspaces (
			data->workspace_manager);

		for (wl = workspaces; wl != NULL; wl = wl->next)
		{
			XfwWorkspace *ws = wl->data;
			XfwWorkspaceState ws_state;
			gboolean has_windows;
			GList *wwl;

			if (ws == active_ws)
				continue;

			ws_state = xfw_workspace_get_state (ws);
			if (ws_state & XFW_WORKSPACE_STATE_HIDDEN)
				continue;

			/* Check if this workspace has any windows */
			has_windows = FALSE;
			for (wwl = wins; wwl != NULL; wwl = wwl->next)
			{
				XfwWindow *w = wwl->data;
				if (xfw_window_is_skip_tasklist (w))
					continue;
				if (xfw_window_get_workspace (w) == ws)
				{
					has_windows = TRUE;
					break;
				}
			}

			if (!has_windows)
				continue;

			if (!added_separator)
			{
				GtkWidget *sep =
					gtk_separator_menu_item_new ();
				gtk_menu_shell_append (
					GTK_MENU_SHELL (data->submenu),
					sep);
				added_separator = TRUE;
			}

			add_workspace_label (data, ws);

			for (wwl = wins; wwl != NULL; wwl = wwl->next)
			{
				XfwWindow *w = wwl->data;
				if (xfw_window_is_skip_tasklist (w))
					continue;
				if (xfw_window_get_workspace (w) == ws)
					append_window_item (data, w);
			}
		}
	}

	/* "No Windows Open" item */
	{
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (_ ("No Windows Open"));
		gtk_widget_set_sensitive (item, FALSE);
		gtk_menu_shell_append (GTK_MENU_SHELL (data->submenu), item);
		data->no_windows_item = item;

		if (g_hash_table_size (data->window_hash) > 0)
			gtk_widget_hide (item);
		else
			gtk_widget_show (item);
	}
}

/* ---- Screen signal handlers (live updates while menu is visible) ---- */

static void
on_window_opened (XfwScreen *screen, XfwWindow *window,
                  WaylandWindowMenu *data)
{
	/* refresh the button icon for the initially-enumerated windows too */
	update_root_icon (data);

	if (!gtk_widget_get_mapped (data->submenu))
		return;

	if (g_hash_table_contains (data->window_hash, window))
		return;

	if (xfw_window_is_skip_tasklist (window))
		return;

	append_window_item (data, window);
	gtk_menu_reposition (GTK_MENU (data->submenu));

	if (data->no_windows_item != NULL)
		gtk_widget_hide (data->no_windows_item);
}

static void
on_window_closed (XfwScreen *screen, XfwWindow *window,
                  WaylandWindowMenu *data)
{
	GtkWidget *item;

	if (!gtk_widget_get_mapped (data->submenu))
		return;

	item = g_hash_table_lookup (data->window_hash, window);
	if (item == NULL)
		return;

	g_signal_handlers_disconnect_by_data (window, item);
	g_hash_table_remove (data->window_hash, window);
	gtk_widget_destroy (item);
	gtk_menu_reposition (GTK_MENU (data->submenu));

	if (g_hash_table_size (data->window_hash) == 0 &&
	    data->no_windows_item != NULL)
	{
		gtk_widget_show (data->no_windows_item);
	}
}

static void
on_active_window_changed (XfwScreen *screen, XfwWindow *previous_window,
                          WaylandWindowMenu *data)
{
	update_root_icon (data);
}

/* ---- Workspace manager signal handlers ---- */

static void
on_workspace_created (XfwWorkspaceManager *manager, XfwWorkspace *workspace,
                      WaylandWindowMenu *data)
{
	/* Defer to next rebuild; the menu groups by workspace. */
}

static void
on_workspace_destroyed (XfwWorkspaceManager *manager, XfwWorkspace *workspace,
                        WaylandWindowMenu *data)
{
	/* Defer to next rebuild. */
}

/* ---- Scroll event for window switching ---- */

static gboolean
on_scroll_event (GtkWidget *widget, GdkEventScroll *event,
                 WaylandWindowMenu *data)
{
	XfwWorkspace *active_ws;
	GList *windows, *l;
	GdkScrollDirection direction;
	XfwWindow *prev;
	gboolean found_active;

	if (event->direction == GDK_SCROLL_SMOOTH)
	{
		if (event->delta_y < 0)
			direction = GDK_SCROLL_UP;
		else if (event->delta_y > 0)
			direction = GDK_SCROLL_DOWN;
		else
			return TRUE;
	}
	else
	{
		direction = event->direction;
	}

	if (direction != GDK_SCROLL_UP && direction != GDK_SCROLL_DOWN)
		return TRUE;

	active_ws = get_active_workspace (data);
	windows = xfw_screen_get_windows_stacked (data->screen);

	prev = NULL;
	found_active = FALSE;

	for (l = windows; l != NULL; l = l->next)
	{
		XfwWindow *w = l->data;

		if (xfw_window_is_skip_tasklist (w))
			continue;

		if (active_ws != NULL && !xfw_window_is_pinned (w) &&
		    !xfw_window_is_on_workspace (w, active_ws))
		{
			continue;
		}

		if (xfw_window_is_active (w))
		{
			if (direction == GDK_SCROLL_UP && prev != NULL)
			{
				xfw_window_activate (prev, NULL,
				                     event->time, NULL);
				return TRUE;
			}
			if (direction == GDK_SCROLL_DOWN)
				found_active = TRUE;
			continue;
		}

		if (found_active)
		{
			xfw_window_activate (w, NULL, event->time, NULL);
			return TRUE;
		}

		prev = w;
	}

	/* Wrapping: if scrolling up at the beginning, activate last window */
	if (direction == GDK_SCROLL_UP && prev != NULL)
	{
		xfw_window_activate (prev, NULL, event->time, NULL);
		return TRUE;
	}

	return TRUE;
}

/* ---- Menu show handler ---- */

static void
on_menu_show (GtkWidget *menu, WaylandWindowMenu *data)
{
	rebuild_menu (data);
}

/* ---- Cleanup ---- */

static void
wayland_window_menu_free (WaylandWindowMenu *data)
{
	if (data->window_hash != NULL)
	{
		disconnect_window_signals (data);
		g_hash_table_destroy (data->window_hash);
		data->window_hash = NULL;
	}

	if (data->workspace_manager != NULL)
	{
		/* borrowed from the screen: only disconnect, do not unref */
		g_signal_handlers_disconnect_by_data (data->workspace_manager,
		                                      data);
		data->workspace_manager = NULL;
	}

	if (data->screen != NULL)
	{
		g_signal_handlers_disconnect_by_data (data->screen, data);
		g_object_unref (data->screen);
		data->screen = NULL;
	}

	g_free (data);
}

/* ---- Public API ---- */

GtkWidget *
wayland_window_menu_new (void)
{
	WaylandWindowMenu *data;
	XfwScreen *screen;
	GdkDisplay *display;
	GtkCssProvider *provider;

	display = gdk_display_get_default ();
	if (display == NULL || !GDK_IS_WAYLAND_DISPLAY (display))
		return gtk_label_new (_ ("[Window menu not supported on this platform]"));

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	screen = xfw_screen_get_default ();
	if (screen == NULL)
		return gtk_label_new (_ ("[Shell does not support "
		                         "WLR Foreign Toplevel Control]"));

	data = g_new0 (WaylandWindowMenu, 1);
	/* xfw_screen_get_default() returns a referenced screen */
	data->screen = screen;
	data->window_hash = g_hash_table_new_full (g_direct_hash,
	                                           g_direct_equal,
	                                           g_object_unref,
	                                           NULL);

	/* borrowed from the screen, which we keep alive */
	data->workspace_manager = xfw_screen_get_workspace_manager (screen);

	/* Build the menubar (matches WnckSelector's GtkMenuBar structure) */
	data->menu = gtk_menu_bar_new ();

	/* Root item: icon of active window */
	data->root_item = gtk_menu_item_new ();
	data->image = gtk_image_new ();
	gtk_container_add (GTK_CONTAINER (data->root_item), data->image);

	/* Submenu: the actual window list */
	data->submenu = gtk_menu_new ();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (data->root_item),
	                           data->submenu);
	gtk_menu_shell_append (GTK_MENU_SHELL (data->menu), data->root_item);

	/* Remove menubar borders to match panel aesthetics; the
	 * dimmed-window icon style is needed inside the popup submenu,
	 * which is a separate window, so use a screen-wide provider */
	gtk_widget_set_name (data->menu, "wnck-selector-menubar");
	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider,
	                                 "#wnck-selector-menubar {\n"
	                                 "  border-width: 0px;\n"
	                                 "}\n"
	                                 ".wnck-selector-dimmed {\n"
	                                 "  opacity: 0.5;\n"
	                                 "}",
	                                 -1, NULL);
	gtk_style_context_add_provider_for_screen (
		gtk_widget_get_screen (data->menu),
		GTK_STYLE_PROVIDER (provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);

	g_object_set_data_full (G_OBJECT (data->menu),
	                        "wayland_window_menu_data",
	                        data,
	                        (GDestroyNotify) wayland_window_menu_free);

	g_signal_connect (data->submenu, "show",
	                  G_CALLBACK (on_menu_show), data);

	/* Screen signals for live updates while menu is visible */
	g_signal_connect (data->screen, "window-opened",
	                  G_CALLBACK (on_window_opened), data);
	g_signal_connect (data->screen, "window-closed",
	                  G_CALLBACK (on_window_closed), data);
	g_signal_connect (data->screen, "active-window-changed",
	                  G_CALLBACK (on_active_window_changed), data);

	/* Workspace manager signals */
	if (data->workspace_manager != NULL)
	{
		g_signal_connect (data->workspace_manager,
		                  "workspace-created",
		                  G_CALLBACK (on_workspace_created), data);
		g_signal_connect (data->workspace_manager,
		                  "workspace-destroyed",
		                  G_CALLBACK (on_workspace_destroyed), data);
	}

	/* Set initial root icon to active window */
	update_root_icon (data);

	/* Scroll-to-switch-windows */
	gtk_widget_add_events (data->menu, GDK_SCROLL_MASK);
	g_signal_connect (data->menu, "scroll-event",
	                  G_CALLBACK (on_scroll_event), data);

	gtk_widget_show (data->root_item);
	gtk_widget_show (data->image);
	gtk_widget_show (data->menu);
	return data->menu;
}
