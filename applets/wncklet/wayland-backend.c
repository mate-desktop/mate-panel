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
#include <gio/gdesktopappinfo.h>

#include "wayland-backend.h"
#include "wayland-protocol/wlr-foreign-toplevel-management-unstable-v1-client.h"

/*shorter than wnck-tasklist due to common use of larger fonts*/
#define TASKLIST_TEXT_MAX_WIDTH 16

/*In the future this could be changable from the panel-prefs dialog*/
static const int max_button_width = 180;
static const int icon_size = 16;

typedef struct
{
	GtkWidget *menu;
	GtkWidget *maximize;
	GtkWidget *minimize;
	GtkWidget *on_top;
	GtkWidget *close;
} ContextMenu;

typedef struct
{
	GtkWidget *list;
	GtkWidget *outer_box;
	ContextMenu *context_menu;
	struct zwlr_foreign_toplevel_manager_v1 *manager;
} TasklistManager;

typedef struct
{
	GtkWidget *button;
	GtkWidget *icon;
	GtkWidget *label;
	struct zwlr_foreign_toplevel_handle_v1 *toplevel;
	gboolean active;
	gboolean maximized;
	gboolean minimized;
	gboolean fullscreen;
} ToplevelTask;

static const char *tasklist_manager_key = "tasklist_manager";
static const char *toplevel_task_key = "toplevel_task";

static gboolean has_initialized = FALSE;
static struct wl_registry *wl_registry_global = NULL;
static uint32_t foreign_toplevel_manager_global_id = 0;
static uint32_t foreign_toplevel_manager_global_version = 0;

static ToplevelTask *toplevel_task_new (TasklistManager *tasklist, struct zwlr_foreign_toplevel_handle_v1 *handle);

guint buttons, tasklist_width;

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
	gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);
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

	if (tasklist->context_menu)
	{
		gtk_widget_destroy (tasklist->context_menu->menu);
		g_free (tasklist->context_menu);
		tasklist->context_menu = NULL;
	}
}

static void
menu_on_maximize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task->toplevel) {
		if (task->maximized) {
			zwlr_foreign_toplevel_handle_v1_unset_maximized (task->toplevel);
		} else {
			zwlr_foreign_toplevel_handle_v1_set_maximized (task->toplevel);
		}
	}
}

static void
menu_on_minimize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task->toplevel) {
		if (task->minimized) {
			zwlr_foreign_toplevel_handle_v1_unset_minimized (task->toplevel);
		} else {
			zwlr_foreign_toplevel_handle_v1_set_minimized (task->toplevel);
		}
	}
}

static void
menu_on_close (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task->toplevel) {
		zwlr_foreign_toplevel_handle_v1_close (task->toplevel);
	}
}

static ContextMenu *
context_menu_new ()
{
	ContextMenu *menu = g_new0 (ContextMenu, 1);
	menu->menu = gtk_menu_new ();
	menu->maximize = gtk_menu_item_new ();
	menu->minimize = gtk_menu_item_new ();
	menu->on_top = gtk_check_menu_item_new_with_label ("Always On Top");
	menu->close = gtk_menu_item_new_with_label ("Close");

	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->maximize);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->minimize);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), gtk_separator_menu_item_new ());
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->on_top);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), gtk_separator_menu_item_new ());
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->close);

	gtk_widget_show_all (menu->menu);

	g_signal_connect (menu->maximize, "activate", G_CALLBACK (menu_on_maximize), NULL);
	g_signal_connect (menu->minimize, "activate", G_CALLBACK (menu_on_minimize), NULL);
	g_signal_connect (menu->close, "activate", G_CALLBACK (menu_on_close), NULL);
	gtk_widget_set_sensitive (menu->on_top, FALSE);
	return menu;
}

static TasklistManager *
tasklist_manager_new (void)
{
	if (!foreign_toplevel_manager_global_id)
		return NULL;

	TasklistManager *tasklist = g_new0 (TasklistManager, 1);
	tasklist->list = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
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
	tasklist->context_menu = context_menu_new ();
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
	ToplevelTask *task = data;

	gchar *app_id_lower = g_utf8_strdown (app_id, -1);
	gchar *desktop_app_id = g_strdup_printf ("%s.desktop", app_id_lower);
	GDesktopAppInfo *app_info = g_desktop_app_info_new (desktop_app_id);

	if (app_info) {
		GIcon *icon = g_app_info_get_icon (G_APP_INFO (app_info));
		if (icon) {
			gtk_image_set_from_gicon (GTK_IMAGE (task->icon), icon, GTK_ICON_SIZE_MENU);
			goto cleanup;
		}
	}
	gtk_image_set_from_icon_name (GTK_IMAGE (task->icon), app_id_lower, GTK_ICON_SIZE_MENU);

cleanup:
	if (app_info) {
		g_object_unref (G_OBJECT (app_info));
	}
	g_free (app_id_lower);
	g_free (desktop_app_id);
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
	task->maximized = FALSE;
	task->minimized = FALSE;
	task->fullscreen = FALSE;

	enum zwlr_foreign_toplevel_handle_v1_state *i;
	wl_array_for_each (i, state)
	{
		switch (*i)
		{
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
			task->active = TRUE;
			break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
			task->maximized = TRUE;
			break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
			task->minimized = TRUE;
			break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
			task->fullscreen = TRUE;
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
	{
		GtkOrientation orient;
		GtkWidget *button;
		GtkWidget  *box;
		GtkWidget *outer_box = gtk_widget_get_parent (GTK_WIDGET (task->button));
		gtk_widget_destroy (task->button);
		buttons = buttons -1;

		if (buttons == 0)
			return;

		/* We don't need to modify button size on a vertical panel*/
		orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (outer_box));
		if (orient == GTK_ORIENTATION_VERTICAL)
			return;

		/* Horizontal panel: if buttons can now fit
		 * with both labels and icons show them
		 */
		if (tasklist_width / buttons > icon_size * 3)
		{
			GList* children = gtk_container_get_children (GTK_CONTAINER (outer_box));
			while (children != NULL)
			{
				button = GTK_WIDGET (children->data);

				/* If maximum width buttons fix, expand to that dimension*/
				if (buttons * max_button_width < tasklist_width)
					gtk_widget_set_size_request (button, max_button_width, -1);

				/* Otherwise expand remaining buttons to fill the tasklist*/
				else
					gtk_widget_set_size_request (button, tasklist_width / buttons, -1);

				gtk_widget_show_all (button);
				children = children->next;
			}
		}
		/* If buttons with icons will fit, bring them back*/
		else if (tasklist_width / buttons > icon_size * 2)
		{
			GtkWidget *widget;
			GList* children = gtk_container_get_children (GTK_CONTAINER (outer_box));
			while (children != NULL)
			{
				button = GTK_WIDGET (children->data);
				box = gtk_bin_get_child (GTK_BIN (button));
				GList* contents = gtk_container_get_children (GTK_CONTAINER (box));
				while (contents != NULL)
				{
					widget = GTK_WIDGET (contents->data);
					if (GTK_IS_LABEL (widget))
						gtk_widget_hide (widget);

					if (GTK_IS_IMAGE (widget))
						gtk_widget_show (widget);

					contents = contents->next;
					gtk_widget_show (box);
					gtk_widget_show (button);
				}

				children = children->next;
				gtk_widget_set_size_request (button, tasklist_width / buttons, -1);
			}
		}
		/* If we still cannot fit labels or icons, just fill the available space*/
		else
		{
			GList* children = gtk_container_get_children (GTK_CONTAINER (outer_box));
			while (children != NULL)
			{
				button = GTK_WIDGET (children->data);
				gtk_widget_set_size_request (button, tasklist_width / buttons, -1);
				children = children->next;
			}
		}
	}
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
	task->icon = NULL;
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

static gboolean on_toplevel_button_press (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist)
{
	/* Assume event is a button press */
	if (((GdkEventButton*)event)->button == GDK_BUTTON_SECONDARY)
	{
		ContextMenu *menu = tasklist->context_menu;
		ToplevelTask *task = g_object_get_data (G_OBJECT (button), toplevel_task_key);

		g_object_set_data (G_OBJECT (menu->maximize), toplevel_task_key, task);
		g_object_set_data (G_OBJECT (menu->minimize), toplevel_task_key, task);
		g_object_set_data (G_OBJECT (menu->close), toplevel_task_key, task);

		gtk_menu_item_set_label (GTK_MENU_ITEM (menu->minimize),
				task->minimized ? "Unminimize" : "Minimize");
		gtk_menu_item_set_label (GTK_MENU_ITEM (menu->maximize),
				task->maximized ? "Unmaximize" : "Maximize");

		gtk_menu_popup_at_widget (GTK_MENU (menu->menu), button,
				GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST, event);
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
	GtkWidget *button;
	GtkOrientation orient;

	buttons = buttons + 1;
	orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box));
	task->button = gtk_button_new ();
	g_signal_connect (task->button, "clicked", G_CALLBACK (toplevel_task_handle_clicked), task);

	task->icon = gtk_image_new_from_icon_name ("unknown", icon_size);

	task->label = gtk_label_new ("");
	gtk_label_set_max_width_chars (GTK_LABEL (task->label), TASKLIST_TEXT_MAX_WIDTH);
	gtk_label_set_ellipsize (GTK_LABEL (task->label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign (GTK_LABEL (task->label), 0.0);

	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (box), task->icon, FALSE, FALSE, 6);
	gtk_box_pack_start (GTK_BOX (box), task->label, TRUE, TRUE, 2);

	gtk_container_add (GTK_CONTAINER (task->button), box);
	gtk_widget_set_name (task->button , "tasklist-button");
	gtk_widget_show_all (task->button);

	/* Buttons on a vertical panel are not affected by how many are needed
	 * GTK handles compressing contents as needed as the window width tells
	 * GTK how much space to allocate the label and icon. Buttons will use
	 * the full width of a vertical panel without any special attention
	 * so break out here instead of breaking the vertical panel case
	 */

	if (orient == GTK_ORIENTATION_VERTICAL)
	{
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
			  tasklist);

	return task;
	}

	/* On horizontal panels, GTK does not by default limit the width of the tasklist
	 * as it does not run out of space in the window until the entire panel is used,
	 * leaving buttons at full width until then and overflowing all other applets
	 *
	 * Thus we must get the tasklist's allocated width when extra space remains,
	 * which will be most of the distance between the handle and the next applet
	 * From there, we can expand buttons and/or hide elements as needed
	 */


	tasklist_width = gtk_widget_get_allocated_width (GTK_WIDGET (tasklist->outer_box));

	/* First button can be buggy with this so hardcode it to expand to the limit */
	if (buttons == 1)
		gtk_widget_set_size_request (task->button, max_button_width, -1);

	/* if the number of buttons forces width to less than 3x the icon size, shrink them  */
	if ((buttons != 0) && (tasklist_width > 1 )&& (tasklist_width / buttons < (icon_size * 3)))
	{
		/* adjust the current button first or it can be missed */
		if (tasklist_width / buttons > icon_size * 2)
		{
			gtk_widget_hide (task->label);
			gtk_widget_show (task->icon);
		}
		else
		{
			gtk_widget_show (task->label);
			gtk_widget_hide (task->icon);
		}
		gtk_widget_show (box);
		gtk_widget_show (task->button);

		/* iterate over all the buttons, first hide labels
		 * then hide icons and bring back labels
		 */
		GtkWidget *widget;

		GList* children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));
		while (children != NULL)
		{
			button = GTK_WIDGET (children->data);
			box = gtk_bin_get_child (GTK_BIN (button));

			/* hide labels of all buttons but show icons if only icons will fit */
			if (tasklist_width / buttons > icon_size * 2)
			{
				/* find the icon and the label, show just the icon */
				GList* contents = gtk_container_get_children (GTK_CONTAINER (box));

				while (contents != NULL)
				{
					widget = GTK_WIDGET (contents->data);
					if (GTK_IS_LABEL (widget))
						gtk_widget_hide (widget);

					if (GTK_IS_IMAGE (widget))
						gtk_widget_show (widget);

					contents = contents->next;
				}
			}
			else
			{
				/* find the icon and the label, show just the label as it is more
				 * compressable than the icon. Though less meaningful at this size,
				 * it is enough to keep the tasklist from disappearing on themes
				 * that do not set borders around tasklist buttons.
				 * This is same behavior as on x11 save that an extreme number of
				 * buttons (50+ on 700px of space) can still overflow
				 */

				GList* contents = gtk_container_get_children (GTK_CONTAINER (box));
				while (contents != NULL)
				{
					widget = GTK_WIDGET (contents->data);
					if (GTK_IS_LABEL (widget))
						gtk_widget_show (widget);

					if (GTK_IS_IMAGE (widget))
						gtk_widget_hide (widget);

					contents = contents->next;
				}
			}
			/*expand buttons with labels or everything hidden to fit remaining space*/
			gtk_widget_set_size_request (button, tasklist_width / buttons, -1);
			/*show the button and any contents that fit, then get the next button*/
			gtk_widget_show (box);
			gtk_widget_show (button);

			children = children->next;
		}
	}
	else
	{
		GList* children = gtk_container_get_children (GTK_CONTAINER(tasklist->list));
		while (children != NULL)
		{
			button = GTK_WIDGET (children->data);
			if (((buttons ) * max_button_width < tasklist_width) || (buttons == 1))

				/*Don't let buttons expand over the maximum button size*/
				gtk_widget_set_size_request (button, max_button_width, -1);

			else
				/*if full width buttons won't fit, size them to just fill the tasklist*/
				gtk_widget_set_size_request (button, tasklist_width / buttons, -1);

			children = children->next;
		}
	gtk_widget_show_all (task->button);
	}

	/*Reset the tasklist width after button adjustments*/
	tasklist_width = gtk_widget_get_allocated_width (GTK_WIDGET (tasklist->outer_box));

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
			  tasklist);

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
