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

#include <gtk/gtk.h>
#include <libxfce4windowing/libxfce4windowing.h>

#include "wayland-workspace.h"

typedef struct _GroupSignal GroupSignal;

struct _GroupSignal {
	XfwWorkspaceGroup *group;
	gulong             handlers[3]; /* active-changed, added, removed */
};

typedef struct _WaylandWorkspaceData WaylandWorkspaceData;

struct _WaylandWorkspaceData {
	GtkWidget *grid;

	XfwScreen *screen;
	XfwWorkspaceManager *workspace_manager;
	GList *group_signals; /* list of GroupSignal* */

	GtkOrientation orientation;
	int n_rows;
	gboolean display_all;
	gboolean display_names;
	gboolean ignore_toggle;
};

static void rebuild_ui (WaylandWorkspaceData *data);

/* ---- Helpers ---- */

static int
count_visible_workspaces (WaylandWorkspaceData *data)
{
	GList *workspaces, *l;
	int n = 0;

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);

	for (l = workspaces; l != NULL; l = l->next)
	{
		XfwWorkspace *ws = l->data;
		XfwWorkspaceState state = xfw_workspace_get_state (ws);

		if (data->display_all || !(state & XFW_WORKSPACE_STATE_HIDDEN))
			n++;
	}

	return MAX (n, 1);
}

static XfwWorkspace *
get_nth_workspace (WaylandWorkspaceData *data, int index)
{
	GList *workspaces, *l;
	int i = 0;

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);

	for (l = workspaces; l != NULL; l = l->next)
	{
		XfwWorkspace *ws = l->data;
		XfwWorkspaceState state = xfw_workspace_get_state (ws);

		if (data->display_all || !(state & XFW_WORKSPACE_STATE_HIDDEN))
		{
			if (i == index)
				return ws;
			i++;
		}
	}

	return NULL;
}

/* ---- Build / rebuild the button grid ---- */

static void
on_button_toggled (GtkToggleButton *button, WaylandWorkspaceData *data)
{
	int index;

	if (data->ignore_toggle)
		return;

	if (gtk_toggle_button_get_active (button))
	{
		index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "ws-index"));
		XfwWorkspace *ws = get_nth_workspace (data, index);
		if (ws)
		{
			GError *error = NULL;
			if (!xfw_workspace_activate (ws, &error))
			{
				g_warning ("Failed to activate workspace: %s",
					   error ? error->message : "unknown error");
				g_clear_error (&error);
			}
		}
	}
	else
	{
		/* User clicked the already-active button — keep it toggled on */
		data->ignore_toggle = TRUE;
		gtk_toggle_button_set_active (button, TRUE);
		data->ignore_toggle = FALSE;
	}
}

static void
rebuild_ui (WaylandWorkspaceData *data)
{
	GList *children, *l;
	int n_spaces, i, col, row;
	int rows, cols;

	/* Destroy old buttons */
	children = gtk_container_get_children (GTK_CONTAINER (data->grid));
	for (l = children; l != NULL; l = l->next)
		gtk_widget_destroy (GTK_WIDGET (l->data));
	g_list_free (children);

	n_spaces = count_visible_workspaces (data);

	/* Determine grid dimensions */
	if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		rows = data->n_rows;
		cols = (n_spaces + rows - 1) / rows;
	}
	else
	{
		cols = data->n_rows;
		rows = (n_spaces + cols - 1) / cols;
	}

	gtk_grid_set_column_homogeneous (GTK_GRID (data->grid), TRUE);
	gtk_grid_set_row_homogeneous (GTK_GRID (data->grid), TRUE);

	/* Create buttons */
	for (i = 0; i < n_spaces; i++)
	{
		XfwWorkspace *ws;
		const char *name;
		char *free_name = NULL;
		GtkWidget *button;
		XfwWorkspaceState ws_state;

		ws = get_nth_workspace (data, i);

		if (data->display_names && ws)
		{
			name = xfw_workspace_get_name (ws);
			if (!name || name[0] == '\0')
				name = xfw_workspace_get_id (ws);
			if (!name)
				name = "";
		}
		else
		{
			free_name = g_strdup_printf ("%d", i + 1);
			name = free_name;
		}

		button = gtk_toggle_button_new_with_label (name);
		g_free (free_name);

		gtk_widget_set_name (button, "wnck-pager-button");
		g_object_set_data (G_OBJECT (button), "ws-index", GINT_TO_POINTER (i));

		/* Set active state */
		ws_state = ws ? xfw_workspace_get_state (ws) : XFW_WORKSPACE_STATE_NONE;
		data->ignore_toggle = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
					      (ws_state & XFW_WORKSPACE_STATE_ACTIVE) != 0);
		data->ignore_toggle = FALSE;

		g_signal_connect (button, "toggled",
				  G_CALLBACK (on_button_toggled), data);

		/* Calculate grid position */
		if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
		{
			col = i / rows;
			row = i % rows;
		}
		else
		{
			col = i % cols;
			row = i / cols;
		}

		gtk_grid_attach (GTK_GRID (data->grid), button, col, row, 1, 1);
	}

	gtk_widget_show_all (data->grid);
}

/* ---- Signal handlers on workspace groups ---- */

static void
on_active_workspace_changed (XfwWorkspaceGroup *group,
			     XfwWorkspace      *previous,
			     WaylandWorkspaceData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_added (XfwWorkspaceGroup *group,
		    XfwWorkspace      *workspace,
		    WaylandWorkspaceData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_removed (XfwWorkspaceGroup *group,
		      XfwWorkspace      *workspace,
		      WaylandWorkspaceData *data)
{
	rebuild_ui (data);
}

/* ---- Group signal management ---- */

static void
disconnect_group_signals (WaylandWorkspaceData *data)
{
	for (GList *l = data->group_signals; l != NULL; l = l->next)
	{
		GroupSignal *gs = l->data;
		g_signal_handler_disconnect (gs->group, gs->handlers[0]);
		g_signal_handler_disconnect (gs->group, gs->handlers[1]);
		g_signal_handler_disconnect (gs->group, gs->handlers[2]);
		g_free (gs);
	}

	g_list_free (data->group_signals);
	data->group_signals = NULL;
}

static void
connect_group_signals (WaylandWorkspaceData *data)
{
	GList *groups, *l;

	disconnect_group_signals (data);

	groups = xfw_workspace_manager_list_workspace_groups (data->workspace_manager);
	if (!groups)
		return;

	for (l = groups; l != NULL; l = l->next)
	{
		XfwWorkspaceGroup *group = l->data;
		GroupSignal *gs = g_new0 (GroupSignal, 1);

		gs->group = group;
		gs->handlers[0] = g_signal_connect (group, "active-workspace-changed",
						    G_CALLBACK (on_active_workspace_changed), data);
		gs->handlers[1] = g_signal_connect (group, "workspace-added",
						    G_CALLBACK (on_workspace_added), data);
		gs->handlers[2] = g_signal_connect (group, "workspace-removed",
						    G_CALLBACK (on_workspace_removed), data);

		data->group_signals = g_list_append (data->group_signals, gs);
	}
}

static void
on_workspace_group_created (XfwWorkspaceManager *manager,
			    XfwWorkspaceGroup   *group,
			    WaylandWorkspaceData *data)
{
	connect_group_signals (data);
	rebuild_ui (data);
}

static void
on_workspace_group_destroyed (XfwWorkspaceManager *manager,
			      XfwWorkspaceGroup   *group,
			      WaylandWorkspaceData *data)
{
	connect_group_signals (data);
	rebuild_ui (data);
}

static void
on_workspace_created (XfwWorkspaceManager *manager,
		      XfwWorkspace        *workspace,
		      WaylandWorkspaceData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_destroyed (XfwWorkspaceManager *manager,
			XfwWorkspace        *workspace,
			WaylandWorkspaceData *data)
{
	rebuild_ui (data);
}

/* ---- Cleanup ---- */

static void
wayland_pager_data_free (WaylandWorkspaceData *data)
{
	disconnect_group_signals (data);
	g_free (data);
}

/* ---- Public API ---- */

GtkWidget *
wayland_workspace_new (void)
{
	WaylandWorkspaceData *data;

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	data = g_new0 (WaylandWorkspaceData, 1);
	data->orientation = GTK_ORIENTATION_HORIZONTAL;
	data->n_rows = 1;
	data->display_all = TRUE;
	data->display_names = FALSE;
	data->ignore_toggle = FALSE;

	data->grid = gtk_grid_new ();
	gtk_widget_set_size_request (data->grid, 48, -1);

	GtkStyleContext *context = gtk_widget_get_style_context (data->grid);
	gtk_style_context_add_class (context, "wnck-pager");

	g_object_set_data_full (G_OBJECT (data->grid),
				"wayland_pager_data",
				data,
				(GDestroyNotify) wayland_pager_data_free);

	data->screen = xfw_screen_get_default ();
	if (!data->screen)
	{
		g_warning ("xfw_screen_get_default() failed");
		return data->grid;
	}

	data->workspace_manager = xfw_screen_get_workspace_manager (data->screen);
	if (!data->workspace_manager)
	{
		g_warning ("xfw_screen_get_workspace_manager() failed");
		return data->grid;
	}

	g_signal_connect (data->workspace_manager, "workspace-group-created",
			  G_CALLBACK (on_workspace_group_created), data);
	g_signal_connect (data->workspace_manager, "workspace-group-destroyed",
			  G_CALLBACK (on_workspace_group_destroyed), data);

	g_signal_connect (data->workspace_manager, "workspace-created",
			  G_CALLBACK (on_workspace_created), data);
	g_signal_connect (data->workspace_manager, "workspace-destroyed",
			  G_CALLBACK (on_workspace_destroyed), data);

	connect_group_signals (data);
	rebuild_ui (data);

	return data->grid;
}

static WaylandWorkspaceData *
pager_widget_get_data (GtkWidget *pager_widget)
{
	return g_object_get_data (G_OBJECT (pager_widget), "wayland_pager_data");
}

void
wayland_workspace_set_orientation (GtkWidget *pager_widget, GtkOrientation orientation)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->orientation == orientation)
		return;

	data->orientation = orientation;
	rebuild_ui (data);
}

void
wayland_workspace_set_rows (GtkWidget *pager_widget, int n_rows)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	n_rows = CLAMP (n_rows, 1, 16);

	if (data->n_rows == n_rows)
		return;

	data->n_rows = n_rows;
	rebuild_ui (data);
}

void
wayland_workspace_set_show_all (GtkWidget *pager_widget, gboolean show_all)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->display_all == show_all)
		return;

	data->display_all = show_all;
	rebuild_ui (data);
}

void
wayland_workspace_set_show_names (GtkWidget *pager_widget, gboolean show_names)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->display_names == show_names)
		return;

	data->display_names = show_names;
	rebuild_ui (data);
}

int
wayland_workspace_get_count (GtkWidget *pager_widget)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);

	g_return_val_if_fail (data, 0);

	return g_list_length (xfw_workspace_manager_list_workspaces (data->workspace_manager));
}

const char *
wayland_workspace_get_name (GtkWidget *pager_widget, int index)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	XfwWorkspace *ws;

	g_return_val_if_fail (data, "workspace");

	ws = get_nth_workspace (data, index);
	if (ws)
	{
		const char *name = xfw_workspace_get_name (ws);
		if (name && name[0] != '\0')
			return name;
		return xfw_workspace_get_id (ws);
	}

	return "workspace";
}

int
wayland_workspace_get_active_index (GtkWidget *pager_widget)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	GList *workspaces, *l;
	int i = 0;

	g_return_val_if_fail (data, 0);

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);

	for (l = workspaces; l != NULL; l = l->next)
	{
		XfwWorkspace *ws = l->data;
		XfwWorkspaceState state = xfw_workspace_get_state (ws);

		if (state & XFW_WORKSPACE_STATE_ACTIVE)
			return i;
		i++;
	}

	return 0;
}

void
wayland_workspace_activate_nth (GtkWidget *pager_widget, int index)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	XfwWorkspace *ws;

	g_return_if_fail (data);

	ws = get_nth_workspace (data, index);
	if (ws)
	{
		GError *error = NULL;
		if (!xfw_workspace_activate (ws, &error))
		{
			g_warning ("Failed to activate workspace: %s",
				   error ? error->message : "unknown error");
			g_clear_error (&error);
		}
	}
}
