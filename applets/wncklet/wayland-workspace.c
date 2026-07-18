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

#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>

#include "wayland-workspace.h"
#include "wayland-protocol/ext-workspace-v1-client.h"

typedef struct _WaylandWorkspaceData WaylandWorkspaceData;

typedef struct {
	struct ext_workspace_handle_v1 *handle;
	WaylandWorkspaceData *pager;
	char *name;
	gboolean active;
	gboolean hidden;
	gboolean urgent;
} WaylandWorkspace;

typedef struct {
	struct ext_workspace_group_handle_v1 *handle;
	WaylandWorkspaceData *pager;
	GList *workspaces;
} WaylandWorkspaceGroup;

struct _WaylandWorkspaceData {
	GtkWidget *grid;
	GtkWidget *outer_box;

	struct ext_workspace_manager_v1 *manager;
	GList *workspace_groups;
	GList *pending_workspaces;

	GtkOrientation orientation;
	int n_rows;
	gboolean display_all;
	gboolean display_names;
};

static struct wl_registry *wl_registry_global = NULL;
static uint32_t workspace_manager_global_id = 0;
static uint32_t workspace_manager_global_version = 0;
static gboolean has_initialized = FALSE;

static void rebuild_ui (WaylandWorkspaceData *data);

static void
wl_registry_handle_global (void *_data,
			   struct wl_registry *registry,
			   uint32_t id,
			   const char *interface,
			   uint32_t version)
{
	if (strcmp (interface, ext_workspace_manager_v1_interface.name) == 0)
	{
		workspace_manager_global_id = id;
		workspace_manager_global_version =
			MIN ((uint32_t) ext_workspace_manager_v1_interface.version, version);
	}
}

static void
wl_registry_handle_global_remove (void *_data,
				  struct wl_registry *_registry,
				  uint32_t id)
{
	if (id == workspace_manager_global_id)
	{
		workspace_manager_global_id = 0;
	}
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = wl_registry_handle_global,
	.global_remove = wl_registry_handle_global_remove,
};

static void
wayland_workspace_init_if_needed (void)
{
	if (has_initialized)
		return;

	GdkDisplay *gdk_display = gdk_display_get_default ();
	g_return_if_fail (gdk_display);
	g_return_if_fail (GDK_IS_WAYLAND_DISPLAY (gdk_display));

	struct wl_display *wl_display = gdk_wayland_display_get_wl_display (gdk_display);
	wl_registry_global = wl_display_get_registry (wl_display);
	wl_registry_add_listener (wl_registry_global, &wl_registry_listener, NULL);
	wl_display_roundtrip (wl_display);

	if (!workspace_manager_global_id)
		g_warning ("%s not supported by Wayland compositor",
			   ext_workspace_manager_v1_interface.name);

	has_initialized = TRUE;
}

static WaylandWorkspace *
wayland_workspace_new_from_handle (struct ext_workspace_handle_v1 *handle,
				   WaylandWorkspaceData *pager)
{
	WaylandWorkspace *ws = g_new0 (WaylandWorkspace, 1);
	ws->handle = handle;
	ws->pager = pager;
	ws->name = g_strdup ("");
	ws->active = FALSE;
	ws->hidden = FALSE;
	ws->urgent = FALSE;
	return ws;
}

static void
wayland_workspace_free (WaylandWorkspace *ws)
{
	if (ws)
	{
		g_free (ws->name);
		g_free (ws);
	}
}

static WaylandWorkspaceGroup *
wayland_workspace_group_new (struct ext_workspace_group_handle_v1 *handle,
			     WaylandWorkspaceData *pager)
{
	WaylandWorkspaceGroup *group = g_new0 (WaylandWorkspaceGroup, 1);
	group->handle = handle;
	group->pager = pager;
	group->workspaces = NULL;
	return group;
}

static void
wayland_workspace_group_free (WaylandWorkspaceGroup *group)
{
	if (group)
	{
		g_list_free_full (group->workspaces, (GDestroyNotify) wayland_workspace_free);
		g_free (group);
	}
}

static void
workspace_handle_id (void *data,
		     struct ext_workspace_handle_v1 *handle,
		     const char *id)
{
}

static void
workspace_handle_name (void *data,
		       struct ext_workspace_handle_v1 *handle,
		       const char *name)
{
	WaylandWorkspace *ws = data;
	g_free (ws->name);
	ws->name = g_strdup (name ? name : "");
}

static void
workspace_handle_coordinates (void *data,
			      struct ext_workspace_handle_v1 *handle,
			      struct wl_array *coordinates)
{
}

static void
workspace_handle_state (void *data,
			struct ext_workspace_handle_v1 *handle,
			uint32_t state)
{
	WaylandWorkspace *ws = data;
	ws->active = (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
	ws->hidden = (state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN) != 0;
	ws->urgent = (state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT) != 0;
}

static void
workspace_handle_capabilities (void *data,
			       struct ext_workspace_handle_v1 *handle,
			       uint32_t capabilities)
{
}

static void
workspace_handle_removed (void *data,
			  struct ext_workspace_handle_v1 *handle)
{
}

static const struct ext_workspace_handle_v1_listener workspace_handle_listener = {
	.id = workspace_handle_id,
	.name = workspace_handle_name,
	.coordinates = workspace_handle_coordinates,
	.state = workspace_handle_state,
	.capabilities = workspace_handle_capabilities,
	.removed = workspace_handle_removed,
};

static void
workspace_group_handle_capabilities (void *data,
				     struct ext_workspace_group_handle_v1 *handle,
				     uint32_t capabilities)
{
}

static void
workspace_group_handle_output_enter (void *data,
				     struct ext_workspace_group_handle_v1 *handle,
				     struct wl_output *output)
{
}

static void
workspace_group_handle_output_leave (void *data,
				     struct ext_workspace_group_handle_v1 *handle,
				     struct wl_output *output)
{
}

static void
workspace_group_handle_workspace_enter (void *data,
					struct ext_workspace_group_handle_v1 *handle,
					struct ext_workspace_handle_v1 *workspace_handle)
{
	WaylandWorkspaceGroup *group = data;
	WaylandWorkspaceData *pager = group->pager;
	GList *l;
	WaylandWorkspace *ws = NULL;

	for (l = pager->pending_workspaces; l != NULL; l = l->next)
	{
		WaylandWorkspace *ws_candidate = l->data;
		if (ws_candidate->handle == workspace_handle)
		{
			ws = ws_candidate;
			pager->pending_workspaces = g_list_delete_link (pager->pending_workspaces, l);
			break;
		}
	}

	if (!ws)
	{
		ws = wayland_workspace_new_from_handle (workspace_handle, pager);
		ext_workspace_handle_v1_add_listener (workspace_handle,
						      &workspace_handle_listener,
						      ws);
	}

	group->workspaces = g_list_append (group->workspaces, ws);
}

static void
workspace_group_handle_workspace_leave (void *data,
					struct ext_workspace_group_handle_v1 *handle,
					struct ext_workspace_handle_v1 *workspace_handle)
{
	WaylandWorkspaceGroup *group = data;
	GList *l;

	for (l = group->workspaces; l != NULL; l = l->next)
	{
		WaylandWorkspace *ws = l->data;
		if (ws->handle == workspace_handle)
		{
			group->workspaces = g_list_delete_link (group->workspaces, l);
			wayland_workspace_free (ws);
			break;
		}
	}
}

static void
workspace_group_handle_removed (void *data,
				struct ext_workspace_group_handle_v1 *handle)
{
}

static const struct ext_workspace_group_handle_v1_listener workspace_group_handle_listener = {
	.capabilities = workspace_group_handle_capabilities,
	.output_enter = workspace_group_handle_output_enter,
	.output_leave = workspace_group_handle_output_leave,
	.workspace_enter = workspace_group_handle_workspace_enter,
	.workspace_leave = workspace_group_handle_workspace_leave,
	.removed = workspace_group_handle_removed,
};

static void
manager_handle_workspace_group (void *data,
				struct ext_workspace_manager_v1 *manager,
				struct ext_workspace_group_handle_v1 *group_handle)
{
	WaylandWorkspaceData *pager = data;

	WaylandWorkspaceGroup *group = wayland_workspace_group_new (group_handle, pager);
	ext_workspace_group_handle_v1_add_listener (group_handle,
						    &workspace_group_handle_listener,
						    group);
	pager->workspace_groups = g_list_append (pager->workspace_groups, group);
}

static void
manager_handle_workspace (void *data,
			  struct ext_workspace_manager_v1 *manager,
			  struct ext_workspace_handle_v1 *workspace_handle)
{
	WaylandWorkspaceData *pager = data;
	WaylandWorkspace *ws = wayland_workspace_new_from_handle (workspace_handle, pager);

	ext_workspace_handle_v1_add_listener (workspace_handle,
					      &workspace_handle_listener,
					      ws);

	pager->pending_workspaces = g_list_append (pager->pending_workspaces, ws);
}

static void
manager_handle_done (void *data,
		     struct ext_workspace_manager_v1 *manager)
{
	WaylandWorkspaceData *pager = data;
	GList *l;

	/* Fallback: if workspaces are still pending (compositor didn't send
	 * workspace_enter for them), assign them to the first group */
	if (pager->pending_workspaces != NULL && pager->workspace_groups != NULL)
	{
		WaylandWorkspaceGroup *first_group = pager->workspace_groups->data;

		for (l = pager->pending_workspaces; l != NULL; l = l->next)
		{
			WaylandWorkspace *ws = l->data;
			first_group->workspaces = g_list_append (first_group->workspaces, ws);
		}
		g_list_free (pager->pending_workspaces);
		pager->pending_workspaces = NULL;
	}

	rebuild_ui (pager);
}

static void
manager_handle_finished (void *data,
			 struct ext_workspace_manager_v1 *manager)
{
	WaylandWorkspaceData *pager = data;

	pager->manager = NULL;
	ext_workspace_manager_v1_destroy (manager);

	if (pager->outer_box)
		g_object_set_data (G_OBJECT (pager->outer_box),
				   "wayland_pager_data",
				   NULL);
}

static const struct ext_workspace_manager_v1_listener manager_listener = {
	.workspace_group = manager_handle_workspace_group,
	.workspace = manager_handle_workspace,
	.done = manager_handle_done,
	.finished = manager_handle_finished,
};

static void
workspace_button_clicked (GtkButton *button, WaylandWorkspace *ws)
{
	if (ws->handle && ws->pager->manager)
	{
		ext_workspace_handle_v1_activate (ws->handle);
		ext_workspace_manager_v1_commit (ws->pager->manager);
	}
}

static void
rebuild_ui (WaylandWorkspaceData *data)
{
	GList *l;
	int n_workspaces = 0;
	int n_cols, n_visual_rows;
	int row, col;
	int i;

	/* Remove existing children */
	if (data->grid)
	{
		gtk_widget_destroy (data->grid);
		data->grid = NULL;
	}

	/* Count visible workspaces */
	for (l = data->workspace_groups; l != NULL; l = l->next)
	{
		WaylandWorkspaceGroup *group = l->data;
		GList *wl;
		for (wl = group->workspaces; wl != NULL; wl = wl->next)
		{
			WaylandWorkspace *ws = wl->data;
			if (!ws->hidden || data->display_all)
				n_workspaces++;
		}
	}

	if (n_workspaces == 0)
	{
		/* Show at least one empty slot */
		n_workspaces = 1;
	}

	/* Calculate grid dimensions */
	if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		n_visual_rows = data->n_rows;
		n_cols = n_workspaces / n_visual_rows;
		if (n_workspaces % n_visual_rows != 0)
			n_cols++;
	}
	else
	{
		n_cols = data->n_rows;
		n_visual_rows = n_workspaces / n_cols;
		if (n_workspaces % n_cols != 0)
			n_visual_rows++;
	}

	data->grid = gtk_grid_new ();
	gtk_grid_set_column_homogeneous (GTK_GRID (data->grid), TRUE);
	gtk_grid_set_row_homogeneous (GTK_GRID (data->grid), TRUE);
	gtk_container_add (GTK_CONTAINER (data->outer_box), data->grid);

	/* Create buttons for each workspace */
	i = 0;
	for (l = data->workspace_groups; l != NULL; l = l->next)
	{
		WaylandWorkspaceGroup *group = l->data;
		GList *wl;
		for (wl = group->workspaces; wl != NULL; wl = wl->next)
		{
			WaylandWorkspace *ws = wl->data;
			GtkWidget *button;
			GtkWidget *label;
			char *text;

			if (ws->hidden && !data->display_all)
				continue;

			button = gtk_button_new ();
			gtk_widget_set_name (button, "wnck-pager-button");

			if (data->display_names && ws->name && ws->name[0] != '\0')
				text = g_strdup (ws->name);
			else
				text = g_strdup_printf ("%d", i + 1);

			label = gtk_label_new (text);
			g_free (text);

			gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
			gtk_label_set_max_width_chars (GTK_LABEL (label), 8);
			gtk_container_add (GTK_CONTAINER (button), label);

			g_signal_connect (button, "clicked",
					  G_CALLBACK (workspace_button_clicked), ws);

			g_object_set_data (G_OBJECT (button), "workspace_data", ws);

			/* Add CSS class for active workspace styling */
			GtkStyleContext *context = gtk_widget_get_style_context (button);
			gtk_style_context_add_class (context, "wnck-pager");
			if (ws->active)
				gtk_style_context_add_class (context, "selected");

			if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
			{
				row = i % n_visual_rows;
				col = i / n_visual_rows;
			}
			else
			{
				col = i % n_cols;
				row = i / n_cols;
			}

			gtk_grid_attach (GTK_GRID (data->grid), button, col, row, 1, 1);
			gtk_widget_show_all (button);

			i++;
		}
	}

	/* If no workspaces were visible, show an empty placeholder */
	if (i == 0)
	{
		GtkWidget *button = gtk_button_new ();
		gtk_widget_set_sensitive (button, FALSE);
		gtk_widget_set_name (button, "wnck-pager-button");
		GtkWidget *label = gtk_label_new ("1");
		gtk_container_add (GTK_CONTAINER (button), label);
		gtk_grid_attach (GTK_GRID (data->grid), button, 0, 0, 1, 1);
		gtk_widget_show_all (button);
	}

	gtk_widget_show (data->grid);
}

static void
wayland_pager_disconnected (WaylandWorkspaceData *data)
{
	if (data->grid)
	{
		gtk_widget_destroy (data->grid);
		data->grid = NULL;
	}

	if (data->manager)
		ext_workspace_manager_v1_stop (data->manager);

	g_list_free_full (data->workspace_groups, (GDestroyNotify) wayland_workspace_group_free);
	data->workspace_groups = NULL;

	g_list_free_full (data->pending_workspaces, (GDestroyNotify) wayland_workspace_free);
	data->pending_workspaces = NULL;
}

GtkWidget *
wayland_workspace_new (void)
{
	wayland_workspace_init_if_needed ();

	WaylandWorkspaceData *data = g_new0 (WaylandWorkspaceData, 1);
	data->orientation = GTK_ORIENTATION_HORIZONTAL;
	data->n_rows = 1;
	data->display_all = TRUE;
	data->display_names = FALSE;

	data->outer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show (data->outer_box);

	g_object_set_data_full (G_OBJECT (data->outer_box),
				"wayland_pager_data",
				data,
				(GDestroyNotify) wayland_pager_disconnected);

	if (!workspace_manager_global_id)
	{
		g_warning ("ext_workspace_manager_v1 not available");
		return data->outer_box;
	}

	data->manager = wl_registry_bind (wl_registry_global,
					   workspace_manager_global_id,
					   &ext_workspace_manager_v1_interface,
					   workspace_manager_global_version);

	ext_workspace_manager_v1_add_listener (data->manager,
					       &manager_listener,
					       data);

	/* Trigger initial roundtrip to get workspace state */
	GdkDisplay *gdk_display = gdk_display_get_default ();
	if (gdk_display)
	{
		struct wl_display *wl_display = gdk_wayland_display_get_wl_display (gdk_display);
		wl_display_roundtrip (wl_display);
	}

	return data->outer_box;
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

	if (data->grid)
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

	if (data->grid)
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

	if (data->grid)
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

	if (data->grid)
		rebuild_ui (data);
}

int
wayland_workspace_get_count (GtkWidget *pager_widget)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	int count = 0;
	GList *l;

	g_return_val_if_fail (data, 0);

	for (l = data->workspace_groups; l != NULL; l = l->next)
	{
		WaylandWorkspaceGroup *group = l->data;
		count += g_list_length (group->workspaces);
	}

	return count;
}

const char *
wayland_workspace_get_name (GtkWidget *pager_widget, int index)
{
	WaylandWorkspaceData *data = pager_widget_get_data (pager_widget);
	int i = 0;
	GList *l, *wl;

	g_return_val_if_fail (data, "workspace");

	for (l = data->workspace_groups; l != NULL; l = l->next)
	{
		WaylandWorkspaceGroup *group = l->data;
		for (wl = group->workspaces; wl != NULL; wl = wl->next)
		{
			WaylandWorkspace *ws = wl->data;
			if (i == index)
				return ws->name ? ws->name : "";
			i++;
		}
	}

	return "workspace";
}
