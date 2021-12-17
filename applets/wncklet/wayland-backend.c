/* Wncklet applet Wayland backend */

/*
 * Copyright (C) 2019 William Wold
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

#include <gdk/gdkwayland.h>

#include "wayland-backend.h"
#include "wayland-protocol/wlr-foreign-toplevel-management-unstable-v1-client.h"

static const int window_button_width = 140;

typedef struct
{
	GtkWidget *list;
	GtkWidget *outer_box;
	struct zwlr_foreign_toplevel_manager_v1 *manager;
} TasklistManager;

typedef struct
{
	GtkWidget *button;
	GtkWidget *label;
	struct zwlr_foreign_toplevel_handle_v1 *toplevel;
	gboolean active;
} ToplevelTask;

static const char *tasklist_manager_key = "tasklist_manager";
static const char *toplevel_task_key = "toplevel_task";

static gboolean has_initialized = FALSE;
static struct wl_registry *wl_registry_global = NULL;
static uint32_t foreign_toplevel_manager_global_id = 0;
static uint32_t foreign_toplevel_manager_global_version = 0;

static ToplevelTask *toplevel_task_new (TasklistManager *tasklist, struct zwlr_foreign_toplevel_handle_v1 *handle);

static void
wl_registry_handle_global (void *_data,
			   struct wl_registry *registry,
			   uint32_t id,
			   const char *interface,
			   uint32_t version)
{
	/* pull out needed globals */
	if (strcmp (interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
	{
		g_warn_if_fail (zwlr_foreign_toplevel_manager_v1_interface.version == 2);
		foreign_toplevel_manager_global_id = id;
		foreign_toplevel_manager_global_version =
			MIN((uint32_t)zwlr_foreign_toplevel_manager_v1_interface.version, version);
	}
}

static void
wl_registry_handle_global_remove (void *_data,
				  struct wl_registry *_registry,
				  uint32_t id)
{
	if (id == foreign_toplevel_manager_global_id)
	{
		foreign_toplevel_manager_global_id = 0;
	}
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = wl_registry_handle_global,
    .global_remove = wl_registry_handle_global_remove,
};

static void
wayland_tasklist_init_if_needed (void)
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

	if (!foreign_toplevel_manager_global_id)
		g_warning ("%s not supported by Wayland compositor",
			   zwlr_foreign_toplevel_manager_v1_interface.name);

	has_initialized = TRUE;
}

static void
foreign_toplevel_manager_handle_toplevel (void *data,
					  struct zwlr_foreign_toplevel_manager_v1 *manager,
					  struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
	TasklistManager *tasklist = data;
	ToplevelTask *task = toplevel_task_new (tasklist, toplevel);
	gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 2);
}

static void
foreign_toplevel_manager_handle_finished (void *data,
					  struct zwlr_foreign_toplevel_manager_v1 *manager)
{
	TasklistManager *tasklist = data;

	tasklist->manager = NULL;
	zwlr_foreign_toplevel_manager_v1_destroy (manager);

	if (tasklist->outer_box)
		g_object_set_data (G_OBJECT (tasklist->outer_box),
				   tasklist_manager_key,
				   NULL);

	g_free (tasklist);
}

static const struct zwlr_foreign_toplevel_manager_v1_listener foreign_toplevel_manager_listener = {
	.toplevel = foreign_toplevel_manager_handle_toplevel,
	.finished = foreign_toplevel_manager_handle_finished,
};

static void
tasklist_manager_disconnected_from_widget (TasklistManager *tasklist)
{
	if (tasklist->list)
	{
		GList *children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));
		for (GList *iter = children; iter != NULL; iter = g_list_next (iter))
			gtk_widget_destroy (GTK_WIDGET (iter->data));
		g_list_free(children);
		tasklist->list = NULL;
	}

	if (tasklist->outer_box)
		tasklist->outer_box = NULL;

	if (tasklist->manager)
		zwlr_foreign_toplevel_manager_v1_stop (tasklist->manager);
}

static TasklistManager *
tasklist_manager_new (void)
{
	if (!foreign_toplevel_manager_global_id)
		return NULL;

	TasklistManager *tasklist = g_new0 (TasklistManager, 1);
	tasklist->list = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_set_homogeneous (GTK_BOX (tasklist->list), TRUE);
	tasklist->outer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (tasklist->outer_box), tasklist->list, FALSE, FALSE, 0);
	gtk_widget_show (tasklist->list);
	tasklist->manager = wl_registry_bind (wl_registry_global,
					     foreign_toplevel_manager_global_id,
					     &zwlr_foreign_toplevel_manager_v1_interface,
					     foreign_toplevel_manager_global_version);
	zwlr_foreign_toplevel_manager_v1_add_listener (tasklist->manager,
						       &foreign_toplevel_manager_listener,
						       tasklist);
	g_object_set_data_full (G_OBJECT (tasklist->outer_box),
				tasklist_manager_key,
				tasklist,
				(GDestroyNotify)tasklist_manager_disconnected_from_widget);
	return tasklist;
}

static void
foreign_toplevel_handle_title (void *data,
			       struct zwlr_foreign_toplevel_handle_v1 *toplevel,
			       const char *title)
{
	ToplevelTask *task = data;

	if (task->label)
	{
		gtk_label_set_label (GTK_LABEL (task->label), title);
	}
}

static void
foreign_toplevel_handle_app_id (void *data,
				struct zwlr_foreign_toplevel_handle_v1 *toplevel,
				const char *app_id)
{
	/* ignore */
}

static void
foreign_toplevel_handle_output_enter (void *data,
				      struct zwlr_foreign_toplevel_handle_v1 *toplevel,
				      struct wl_output *output)
{
	/* ignore */
}

static void
foreign_toplevel_handle_output_leave (void *data,
				      struct zwlr_foreign_toplevel_handle_v1 *toplevel,
				      struct wl_output *output)
{
	/* ignore */
}

static void
foreign_toplevel_handle_state (void *data,
			       struct zwlr_foreign_toplevel_handle_v1 *toplevel,
			       struct wl_array *state)
{
	ToplevelTask *task = data;

	task->active = FALSE;

	enum zwlr_foreign_toplevel_handle_v1_state *i;
	wl_array_for_each (i, state)
	{
		switch (*i)
		{
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
			task->active = TRUE;
			break;

		default:
			break;
		}
	}

	gtk_button_set_relief (GTK_BUTTON (task->button), task->active ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE);
}

static void
foreign_toplevel_handle_done (void *data,
			      struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
	/* ignore */
}

static void
foreign_toplevel_handle_closed (void *data,
				struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
	ToplevelTask *task = data;
	if (task->button)
		gtk_widget_destroy (task->button);
}

static const struct zwlr_foreign_toplevel_handle_v1_listener foreign_toplevel_handle_listener = {
	.title = foreign_toplevel_handle_title,
	.app_id = foreign_toplevel_handle_app_id,
	.output_enter = foreign_toplevel_handle_output_enter,
	.output_leave = foreign_toplevel_handle_output_leave,
	.state = foreign_toplevel_handle_state,
	.done = foreign_toplevel_handle_done,
	.closed = foreign_toplevel_handle_closed,
};

static void
toplevel_task_disconnected_from_widget (ToplevelTask *task)
{
	struct zwlr_foreign_toplevel_handle_v1 *toplevel = task->toplevel;

	task->button = NULL;
	task->label = NULL;
	task->toplevel = NULL;

	if (toplevel)
		zwlr_foreign_toplevel_handle_v1_destroy (toplevel);

	g_free (task);
}

static void
toplevel_task_handle_clicked (GtkButton *button, ToplevelTask *task)
{
	if (task->toplevel)
	{
		if (task->active)
		{
			zwlr_foreign_toplevel_handle_v1_set_minimized (task->toplevel);
		}
		else
		{
			GdkDisplay *gdk_display = gtk_widget_get_display (GTK_WIDGET (button));
			GdkSeat *gdk_seat = gdk_display_get_default_seat (gdk_display);
			struct wl_seat *wl_seat = gdk_wayland_seat_get_wl_seat (gdk_seat);
			zwlr_foreign_toplevel_handle_v1_activate (task->toplevel, wl_seat);
		}
	}
}

static gboolean on_toplevel_button_press (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	/* Assume event is a button press */
	if (((GdkEventButton*)event)->button == GDK_BUTTON_SECONDARY)
	{
		/* Returning true for secondary clicks suppresses the applet's default context menu,
		 * which we do not want to show up for task buttons */
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

static ToplevelTask *
toplevel_task_new (TasklistManager *tasklist, struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
	ToplevelTask *task = g_new0 (ToplevelTask, 1);

	task->button = gtk_button_new ();
	g_signal_connect (task->button, "clicked", G_CALLBACK (toplevel_task_handle_clicked), task);

	task->label = gtk_label_new ("");
	gtk_label_set_max_width_chars (GTK_LABEL (task->label), 1);
	gtk_widget_set_size_request (task->label, window_button_width, -1);
	gtk_label_set_ellipsize (GTK_LABEL (task->label), PANGO_ELLIPSIZE_END);
	gtk_container_add (GTK_CONTAINER(task->button), task->label);

	gtk_widget_show_all (task->button);

	task->toplevel = toplevel;
	zwlr_foreign_toplevel_handle_v1_add_listener (toplevel,
						      &foreign_toplevel_handle_listener,
						      task);
	g_object_set_data_full (G_OBJECT (task->button),
				toplevel_task_key,
				task,
				(GDestroyNotify)toplevel_task_disconnected_from_widget);

	g_signal_connect (task->button, "button-press-event",
			  G_CALLBACK (on_toplevel_button_press),
			  task);

	return task;
}

GtkWidget*
wayland_tasklist_new ()
{
	wayland_tasklist_init_if_needed ();
	TasklistManager *tasklist = tasklist_manager_new ();
	if (!tasklist)
		return gtk_label_new ("Shell does not support WLR Foreign Toplevel Control");
	return tasklist->outer_box;
}

static TasklistManager *
tasklist_widget_get_tasklist (GtkWidget* tasklist_widget)
{
	return g_object_get_data (G_OBJECT (tasklist_widget), tasklist_manager_key);
}

void
wayland_tasklist_set_orientation (GtkWidget* tasklist_widget, GtkOrientation orient)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail(tasklist);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (tasklist->list), orient);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (tasklist->outer_box), orient);
}
