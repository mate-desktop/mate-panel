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

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be compiled when HAVE_WAYLAND is enabled
#endif

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <libxfce4windowing/libxfce4windowing.h>

#include "wayland-backend.h"

/*shorter than wnck-tasklist due to common use of larger fonts*/
#define TASKLIST_TEXT_MAX_WIDTH 16

/* On Wayland there is no window XID, the XfwWindow pointer is used to
 * identify a window in the drag-and-drop payload */
#define wayland_task_window_get_wid(window) ((gulong) (window))

/*In the future this could be changable from the panel-prefs dialog*/
static const int max_button_width = 180;
static const int icon_size = 16;
int full_button_width;
static guint buttons, tasklist_width;

typedef struct
{
	GtkWidget *menu;
	GtkWidget *maximize;
	GtkWidget *minimize;
	GtkWidget *on_top;
	GtkWidget *close;
	GtkWidget *close_all;
} ContextMenu;

typedef struct _GroupTask GroupTask;

typedef struct
{
	GtkWidget *list;
	GtkWidget *outer_box;
	ContextMenu *context_menu;
	XfwScreen *screen;
	GList *tasks;
	gboolean scroll_enabled;
	gboolean middle_click_close;
	WaylandTasklistGroupingType grouping;
	gboolean auto_grouping;
	gboolean auto_grouping_applied;
	guint auto_grouping_idle;
	gboolean rebuilding;
	GHashTable *apps;
	GHashTable *window_to_group;
} TasklistManager;

typedef struct
{
	GtkWidget *button;
	GtkWidget *icon;
	GtkWidget *label;
	XfwWindow *window;
	gulong state_changed_id;
	gulong name_changed_id;
	gulong icon_changed_id;
	gboolean active;
	gboolean maximized;
	gboolean minimized;
	gboolean fullscreen;
	gboolean urgent;
	TasklistManager *tasklist;
	GroupTask *group;
	gulong application_changed_id;
	gint natural_width;
} ToplevelTask;

struct _GroupTask
{
	TasklistManager *tasklist;
	XfwApplication *app;
	GtkWidget *button;
	GtkWidget *box;
	GtkWidget *icon;
	GtkWidget *label;
	GtkWidget *counter_label;
	GList *members;
	guint n_windows;
	gboolean active;
	gboolean minimized;
	gboolean urgent;
};

static const char *tasklist_manager_key = "tasklist_manager";
static const char *toplevel_task_key = "toplevel_task";
static const char *group_task_key = "group_task";

static GtkTargetEntry source_targets[] =
{
	{ "application/x-wnck-window-id", 0, 0 }
};

static ToplevelTask *toplevel_task_new (TasklistManager *tasklist, XfwWindow *window);
static gboolean tasklist_scroll_event (GtkWidget *widget, GdkEventScroll *event, TasklistManager *tasklist);
static void group_task_update (GroupTask *group);
static void group_task_update_name (GroupTask *group);
static void group_task_update_name_for (GroupTask *group, ToplevelTask *task);
static void group_task_add_window (GroupTask *group, ToplevelTask *task);
static void group_task_remove_window (GroupTask *group, ToplevelTask *task);
static void tasklist_assign_to_group (TasklistManager *tasklist, ToplevelTask *task);
static void group_task_child_state_changed (GroupTask *group);
static void tasklist_rebuild (TasklistManager *tasklist);

static void
update_task_state (ToplevelTask *task)
{
	XfwWindowState state;

	if (!task || !task->window)
		return;

	state = xfw_window_get_state (task->window);
	task->active = (state & XFW_WINDOW_STATE_ACTIVE) != 0;
	task->maximized = (state & XFW_WINDOW_STATE_MAXIMIZED) != 0;
	task->minimized = (state & XFW_WINDOW_STATE_MINIMIZED) != 0;
	task->fullscreen = (state & XFW_WINDOW_STATE_FULLSCREEN) != 0;
	task->urgent = (state & XFW_WINDOW_STATE_URGENT) != 0;

	if (task->button)
	{
		if (task->urgent)
			gtk_style_context_add_class (gtk_widget_get_style_context (task->button), "urgent");
		else
			gtk_style_context_remove_class (gtk_widget_get_style_context (task->button), "urgent");

		gtk_button_set_relief (GTK_BUTTON (task->button),
				       task->active ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE);
	}

	if (task->group)
		group_task_child_state_changed (task->group);
}

static void
adjust_buttons (GtkContainer *tasklist_list, int button_space, int buttons, ToplevelTask *task)
{
	GtkWidget *widget, *button, *box;

	/*catch the case of an added button that can be missed
	 *Note that button space can come up zero on a first button
	 */
	if (buttons < 2)
	{
		if(task)
		{
			gtk_widget_set_size_request (task->button, full_button_width, -1);
		}
	}

	if ((task) && (button_space > 0) && (button_space < icon_size * 3))
	{
		gtk_widget_hide (task->icon);
	}
	else if (task)
	{
		gtk_widget_show (task->icon);
	}

	if ((task) && (button_space > 0) && (button_space < icon_size))
	{
		gtk_widget_hide (task->label);
	}
	else if (task)
	{
		gtk_widget_show (task->label);
	}

	GList* children = gtk_container_get_children (GTK_CONTAINER (tasklist_list));

	while (children != NULL)
	{
		button = GTK_WIDGET (children->data);
		box = gtk_bin_get_child (GTK_BIN (button));

		/* keep buttons that are hidden as group members hidden,
		 * even when there is enough space to show them at full width */
		{
			ToplevelTask *child_task =
				g_object_get_data (G_OBJECT (button), toplevel_task_key);
			if (child_task && child_task->group)
			{
				children = children->next;
				continue;
			}
		}

		/* group buttons are shown/hidden solely by the grouping code,
		 * never by adjust_buttons (otherwise a hidden group button for a
		 * single-window application would be force-shown again) */
		if (g_object_get_data (G_OBJECT (button), group_task_key) != NULL)
		{
			children = children->next;
			continue;
		}

		if ((buttons < 2) || (buttons * full_button_width < tasklist_width * 0.75))
		{
			gtk_widget_set_size_request (button, full_button_width, -1);
			gtk_widget_show_all (button);
			return;
		}
		else
		{
			gtk_widget_set_size_request (button, MIN(button_space, full_button_width), -1);
		}

		/* if the number of buttons forces width to less than 3x the icon size, hide the icons
		 * if the number of buttons forces width to less than the icon size, hide the labels too.
		 * This is roughy the same behavior as on x11
		 * To find the icon and label we must iterate through the children of the box we packed
		 * into the button, there are two of them
		 */

			GList* contents = gtk_container_get_children (GTK_CONTAINER (box));
			while (contents != NULL)
			{
				widget = GTK_WIDGET (contents->data);
				/*Show or hide the icon*/
				if (GTK_IS_IMAGE (widget))
				{
					if ((button_space < icon_size * 3) && (button_space > 1))
						gtk_widget_hide (widget);

					else
						gtk_widget_show (widget);

				}

				/*Show or hide the label*/
				if (GTK_IS_LABEL (widget))
				{
					if ((button_space < icon_size) && (button_space > 1))
					{
						gtk_widget_hide (widget);
						/*We can go a little wider for empty buttons*/
						gtk_widget_set_size_request (button, tasklist_width / buttons * 0.9, -1);
						if (task)
							gtk_widget_hide (task->label);

					}
					else
					{
						gtk_widget_show (widget);
					}
				}
				contents = contents->next;
			}
		children = children->next;
	}
	return;
}

static void
window_state_changed (XfwWindow *window, XfwWindowState changed_mask, XfwWindowState new_state, gpointer user_data)
{
	ToplevelTask *task = user_data;
	update_task_state (task);
}

static void
window_name_changed (XfwWindow *window, ToplevelTask *task)
{
	if (task->label)
		gtk_label_set_label (GTK_LABEL (task->label), xfw_window_get_name (window));

	/* keep the group button (showing the active window's title) in sync */
	if (task->group)
		group_task_update_name (task->group);
}

static void
update_task_icon (ToplevelTask *task)
{
	GIcon *icon = NULL;
	XfwApplication *app;

	if (!task || !task->window || !task->icon)
		return;

	app = xfw_window_get_application (task->window);
	if (app != NULL)
		icon = xfw_application_get_gicon (app);
	if (icon == NULL)
		icon = xfw_window_get_gicon (task->window);

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (task->icon), icon, GTK_ICON_SIZE_MENU);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (task->icon), "unknown", GTK_ICON_SIZE_MENU);
}

static void
window_icon_changed (XfwWindow *window, ToplevelTask *task)
{
	update_task_icon (task);
}

static GIcon *
window_gicon (XfwWindow *window)
{
	GIcon *icon = NULL;
	XfwApplication *app;

	if (!window)
		return NULL;

	app = xfw_window_get_application (window);
	if (app != NULL)
		icon = xfw_application_get_gicon (app);
	if (icon == NULL)
		icon = xfw_window_get_gicon (window);

	return icon;
}

static void
group_menu_window_activate (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = user_data;

	if (task && task->window)
		xfw_window_activate (task->window, NULL, 0, NULL);
}

static void
group_task_update_name (GroupTask *group)
{
	XfwWindow *active;
	GList *l;
	const gchar *name = NULL;

	if (!group || !group->label)
		return;

	active = NULL;
	if (group->tasklist && group->tasklist->screen)
		active = xfw_screen_get_active_window (group->tasklist->screen);

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (!task || !task->window)
			continue;

		if (name == NULL)
			name = xfw_window_get_name (task->window);
		if (active != NULL && task->window == active)
		{
			name = xfw_window_get_name (task->window);
			break;
		}
	}

	if (name == NULL)
		name = xfw_application_get_name (group->app);

	gtk_label_set_label (GTK_LABEL (group->label), name ? name : "");
}

static void
group_task_update_name_for (GroupTask *group, ToplevelTask *task)
{
	const gchar *name;

	if (!group || !group->label || !task || !task->window)
		return;

	name = xfw_window_get_name (task->window);
	gtk_label_set_label (GTK_LABEL (group->label), name ? name : "");
}

static void
group_task_update_icon (GroupTask *group)
{
	GIcon *icon;

	if (!group || !group->icon)
		return;

	icon = xfw_application_get_gicon (group->app);
	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (group->icon), icon, GTK_ICON_SIZE_MENU);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (group->icon), "unknown", GTK_ICON_SIZE_MENU);
}

static gboolean
tasklist_should_group (TasklistManager *tasklist, guint n_windows)
{
	if (tasklist->grouping == WAYLAND_TASKLIST_NEVER_GROUP)
		return FALSE;
	if (tasklist->grouping == WAYLAND_TASKLIST_ALWAYS_GROUP)
		return n_windows >= 2;
	return tasklist->auto_grouping_applied && n_windows >= 2;
}

static gboolean
tasklist_buttons_crowded (TasklistManager *tasklist, gint available_width)
{
	GtkRequisition req;
	GList *l;
	gint needed = 0;

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task == NULL || task->button == NULL)
			continue;

		if (gtk_widget_get_visible (task->button))
		{
			gtk_widget_get_preferred_width (task->button, NULL, &req.width);
			task->natural_width = req.width;
		}

		if (task->natural_width <= 0)
			task->natural_width = icon_size + 32;

		needed += task->natural_width;
	}

	return needed > available_width;
}

static gboolean
tasklist_apply_auto_grouping (gpointer data)
{
	TasklistManager *tasklist = data;

	tasklist->auto_grouping_idle = 0;

	if (tasklist->auto_grouping == tasklist->auto_grouping_applied)
		return G_SOURCE_REMOVE;

	tasklist->auto_grouping_applied = tasklist->auto_grouping;
	tasklist_rebuild (tasklist);

	return G_SOURCE_REMOVE;
}

static void
tasklist_list_size_allocate (GtkWidget *widget, GdkRectangle *allocation, TasklistManager *tasklist)
{
	gboolean crowded;

	if (tasklist->grouping != WAYLAND_TASKLIST_AUTO_GROUP)
	{
		tasklist->auto_grouping = FALSE;
		return;
	}

	/* don't evaluate crowd state while we are rebuilding, the buttons are
	 * half-hidden and the measurement would suggest the wrong thing */
	if (tasklist->rebuilding)
		return;

	crowded = tasklist_buttons_crowded (tasklist, allocation->width);
	tasklist->auto_grouping = crowded;

	if (crowded != tasklist->auto_grouping_applied && tasklist->auto_grouping_idle == 0)
		tasklist->auto_grouping_idle = g_idle_add (tasklist_apply_auto_grouping, tasklist);
}

static void
group_task_update_member_buttons (GroupTask *group)
{
	GList *l;
	gboolean grouped;

	if (!group)
		return;

	grouped = tasklist_should_group (group->tasklist, group->n_windows);

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task && task->button)
		{
			if (grouped)
				gtk_widget_hide (task->button);
			else if (!gtk_widget_get_visible (task->button))
				gtk_widget_show (task->button);
		}
	}
}

static void
group_task_update (GroupTask *group)
{
	gboolean grouped;

	if (!group)
		return;

	grouped = tasklist_should_group (group->tasklist, group->n_windows);

	group_task_update_name (group);
	group_task_update_icon (group);

	if (group->counter_label)
	{
		if (grouped)
		{
			gchar *text = g_strdup_printf ("%u", group->n_windows);
			gtk_label_set_label (GTK_LABEL (group->counter_label), text);
			g_free (text);
			gtk_widget_show (group->counter_label);
		}
		else
		{
			gtk_widget_hide (group->counter_label);
		}
	}

	if (grouped)
		gtk_widget_show (group->button);
	else
		gtk_widget_hide (group->button);
}

static void
group_task_child_state_changed (GroupTask *group)
{
	GList *l;
	gboolean active = FALSE;
	gboolean urgent = FALSE;
	gboolean minimized = TRUE;

	if (!group || !group->button)
		return;

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (!task)
			continue;
		if (task->active)
			active = TRUE;
		if (task->urgent)
			urgent = TRUE;
		if (!task->minimized)
			minimized = FALSE;
	}

	group->active = active;
	group->urgent = urgent;
	group->minimized = minimized;

	if (urgent)
		gtk_style_context_add_class (gtk_widget_get_style_context (group->button), "urgent");
	else
		gtk_style_context_remove_class (gtk_widget_get_style_context (group->button), "urgent");

	gtk_button_set_relief (GTK_BUTTON (group->button),
			       active ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE);

	/* the group button shows the active window's title */
	group_task_update_name (group);
}

static void
group_task_icon_changed (XfwApplication *app, GroupTask *group)
{
	group_task_update_icon (group);
}

static void
group_task_name_changed (XfwApplication *app, GParamSpec *pspec, GroupTask *group)
{
	group_task_update_name (group);
}

static void
group_menu_close_all (GtkMenuItem *item, gpointer user_data)
{
	GroupTask *group = user_data;
	GList *l;

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task && task->window)
			xfw_window_close (task->window, 0, NULL);
	}
}

static void
group_task_menu_show (GroupTask *group, GdkEventButton *event, gboolean right_click)
{
	GtkWidget *menu;
	GList *l;

	if (!group || !group->button)
		return;

	(void) right_click;

	menu = gtk_menu_new ();

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;
		GtkWidget *item, *box, *icon, *label;
		GIcon *gicon;

		if (!task || !task->window)
			continue;

		item = gtk_menu_item_new ();
		box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		icon = gtk_image_new ();
		gicon = window_gicon (task->window);
		if (gicon != NULL)
			gtk_image_set_from_gicon (GTK_IMAGE (icon), gicon, GTK_ICON_SIZE_MENU);
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (icon), "unknown", GTK_ICON_SIZE_MENU);

		label = gtk_label_new (xfw_window_get_name (task->window));
		gtk_label_set_xalign (GTK_LABEL (label), 0.0);
		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (label), 24);

		gtk_box_pack_start (GTK_BOX (box), icon, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
		gtk_container_add (GTK_CONTAINER (item), box);

		g_signal_connect (item, "activate",
				  G_CALLBACK (group_menu_window_activate), task);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show_all (item);
	}

	/* a visible group button always represents two or more windows, so
	 * "Close All Windows" is offered whether the menu was opened by left
	 * click (picker) or right click (context menu) */
	{
		GtkWidget *item;

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
		item = gtk_menu_item_new_with_label ("Close All Windows");
		g_signal_connect (item, "activate", G_CALLBACK (group_menu_close_all), group);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show_all (item);
	}

	gtk_widget_show (menu);
	gtk_menu_popup_at_widget (GTK_MENU (menu), group->button,
				  GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST, (GdkEvent *) event);
}

static void
group_task_clicked (GtkButton *button, GroupTask *group)
{
	group_task_menu_show (group, NULL, FALSE);
}

static gboolean
on_group_button_press (GtkWidget *button, GdkEvent *event, GroupTask *group)
{
	if (((GdkEventButton*)event)->button == GDK_BUTTON_MIDDLE &&
	    group->tasklist->middle_click_close)
	{
		GList *l;

		for (l = group->members; l != NULL; l = l->next)
		{
			ToplevelTask *task = l->data;

			if (task && task->window)
				xfw_window_close (task->window, ((GdkEventButton*)event)->time, NULL);
		}
		return TRUE;
	}

	if (((GdkEventButton*)event)->button == GDK_BUTTON_SECONDARY)
	{
		group_task_menu_show (group, (GdkEventButton*)event, TRUE);
		return TRUE;
	}

	return FALSE;
}

static GroupTask *
group_task_new (TasklistManager *tasklist, XfwApplication *app)
{
	GroupTask *group;

	group = g_new0 (GroupTask, 1);
	group->tasklist = tasklist;
	group->app = g_object_ref (app);

	group->button = gtk_button_new ();
	group->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	group->icon = gtk_image_new_from_icon_name ("unknown", icon_size);
	group->label = gtk_label_new ("");
	group->counter_label = gtk_label_new ("");

	gtk_label_set_max_width_chars (GTK_LABEL (group->label), TASKLIST_TEXT_MAX_WIDTH);
	gtk_label_set_ellipsize (GTK_LABEL (group->label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign (GTK_LABEL (group->label), 0.0);

	gtk_box_pack_start (GTK_BOX (group->box), group->icon, FALSE, FALSE, 6);
	gtk_box_pack_start (GTK_BOX (group->box), group->label, TRUE, TRUE, 2);
	gtk_box_pack_start (GTK_BOX (group->box), group->counter_label, FALSE, FALSE, 4);

	gtk_container_add (GTK_CONTAINER (group->button), group->box);
	gtk_widget_set_name (group->button, "tasklist-button");
	gtk_widget_show_all (group->button);

	g_object_set_data (G_OBJECT (group->button), group_task_key, group);

	g_signal_connect (group->button, "clicked",
			  G_CALLBACK (group_task_clicked), group);
	g_signal_connect (group->button, "button-press-event",
			  G_CALLBACK (on_group_button_press), group);
	g_signal_connect (group->button, "scroll-event",
			  G_CALLBACK (tasklist_scroll_event), tasklist);

	g_signal_connect (app, "icon-changed",
			  G_CALLBACK (group_task_icon_changed), group);
	g_signal_connect (app, "notify::name",
			  G_CALLBACK (group_task_name_changed), group);

	group_task_update_icon (group);
	group_task_update_name (group);

	gtk_box_pack_start (GTK_BOX (tasklist->list), group->button, TRUE, TRUE, 0);
	/* hidden until it has more than one window */
	gtk_widget_hide (group->button);

	return group;
}

static void
group_task_free (gpointer data)
{
	GroupTask *group = data;

	if (!group)
		return;

	if (group->app)
	{
		g_signal_handlers_disconnect_by_data (group->app, group);
		g_object_unref (group->app);
	}

	group->button = NULL;

	g_list_free (group->members);
	g_free (group);
}

static void
group_task_add_window (GroupTask *group, ToplevelTask *task)
{
	if (!group || !task || !task->window)
		return;

	if (g_list_find (group->members, task) != NULL)
		return;

	task->group = group;
	group->members = g_list_append (group->members, task);
	group->n_windows++;

	g_hash_table_insert (group->tasklist->window_to_group, task->window, group);

	group_task_update (group);
	group_task_update_member_buttons (group);
	group_task_child_state_changed (group);
	group_task_update_name_for (group, task);
}

static void
group_task_remove_window (GroupTask *group, ToplevelTask *task)
{
	if (!group || !task)
		return;

	group->members = g_list_remove (group->members, task);
	if (task->window &&
	    g_hash_table_lookup (group->tasklist->window_to_group, task->window) == group)
		g_hash_table_remove (group->tasklist->window_to_group, task->window);

	task->group = NULL;
	group->n_windows--;

	if (group->n_windows == 0)
	{
		if (group->button)
		{
			GtkWidget *parent = gtk_widget_get_parent (group->button);
			if (parent)
				gtk_container_remove (GTK_CONTAINER (parent), group->button);
			group->button = NULL;
		}

		g_hash_table_remove (group->tasklist->apps, group->app);
		return;
	}

	group_task_update (group);
	group_task_update_member_buttons (group);
	group_task_child_state_changed (group);
}

static void
tasklist_assign_to_group (TasklistManager *tasklist, ToplevelTask *task)
{
	XfwApplication *app;
	GroupTask *group;

	if (tasklist->grouping == WAYLAND_TASKLIST_NEVER_GROUP)
		return;

	app = xfw_window_get_application (task->window);
	if (app == NULL)
		return;

	group = g_hash_table_lookup (tasklist->apps, app);
	if (group == NULL)
	{
		group = group_task_new (tasklist, app);
		g_hash_table_insert (tasklist->apps, app, group);
	}

	group_task_add_window (group, task);
}

static void
window_application_changed (XfwWindow *window, GParamSpec *pspec, ToplevelTask *task)
{
	TasklistManager *tasklist;

	if (!task)
		return;

	tasklist = task->tasklist;

	if (task->group)
		group_task_remove_window (task->group, task);

	if (tasklist && tasklist->grouping != WAYLAND_TASKLIST_NEVER_GROUP)
		tasklist_assign_to_group (tasklist, task);
}

static void
menu_on_maximize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
		xfw_window_set_maximized (task->window, !task->maximized, NULL);
}

static void
menu_on_minimize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
		xfw_window_set_minimized (task->window, !task->minimized, NULL);
}

static void
menu_on_close (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
		xfw_window_close (task->window, 0, NULL);
}

static void
menu_on_close_all (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	GroupTask *group;
	GList *l;

	if (!task)
		return;

	group = task->group;
	if (!group)
	{
		if (task->window)
			xfw_window_close (task->window, 0, NULL);
		return;
	}

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *member = l->data;

		if (member && member->window)
			xfw_window_close (member->window, 0, NULL);
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
	menu->close_all = gtk_menu_item_new_with_label ("Close All Windows");

	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->maximize);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->minimize);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), gtk_separator_menu_item_new ());
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->on_top);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), gtk_separator_menu_item_new ());
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->close);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu->menu), menu->close_all);

	gtk_widget_show_all (menu->menu);

	g_signal_connect (menu->maximize, "activate", G_CALLBACK (menu_on_maximize), NULL);
	g_signal_connect (menu->minimize, "activate", G_CALLBACK (menu_on_minimize), NULL);
	g_signal_connect (menu->close, "activate", G_CALLBACK (menu_on_close), NULL);
	g_signal_connect (menu->close_all, "activate", G_CALLBACK (menu_on_close_all), NULL);
	gtk_widget_set_sensitive (menu->on_top, FALSE);
	return menu;
}

static gboolean
tasklist_has_window (TasklistManager *tasklist, XfwWindow *window)
{
	GList *l;

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;
		if (task->window == window)
			return TRUE;
	}
	return FALSE;
}

static void
screen_window_opened (XfwScreen *screen, XfwWindow *window, TasklistManager *tasklist)
{
	ToplevelTask *task;

	if (!window || tasklist_has_window (tasklist, window))
		return;

	task = toplevel_task_new (tasklist, window);
	gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);
}

static void
screen_window_closed (XfwScreen *screen, XfwWindow *window, TasklistManager *tasklist)
{
	GList *l;

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;
		if (task->window == window)
		{
			GtkOrientation orient;
			GtkWidget *outer_box, *parent_box;
			int button_space;

			buttons = buttons -1;

			/* remove the window from its group (if any) before destroying the button */
			if (task->group)
				group_task_remove_window (task->group, task);

			outer_box = tasklist->outer_box;
			/* removes the task from tasklist->tasks and destroys the button */
			tasklist->tasks = g_list_remove (tasklist->tasks, task);
			gtk_widget_destroy (task->button);

			if (buttons == 0)
				return;

		/* We don't need to modify button size on a vertical panel*/
			orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (outer_box));
			if (orient == GTK_ORIENTATION_VERTICAL)
				return;

			/*Get the box the tasklist outer box sits in
			 *and leave a little space so the buttons don't push other applets off the panel
			 */
			parent_box = gtk_widget_get_ancestor ((outer_box), GTK_TYPE_BOX);
			tasklist_width = MAX(gtk_widget_get_allocated_width (parent_box), tasklist_width) ;
			button_space = (tasklist_width / buttons) * 0.75;
			button_space = MIN(button_space, full_button_width);
			adjust_buttons (GTK_CONTAINER (tasklist->list), button_space, buttons, NULL);

			return;
		}
	}
}

static void
screen_active_window_changed (XfwScreen *screen, XfwWindow *previous_window, gpointer user_data)
{
	TasklistManager *tasklist = user_data;
	GList *l;

	for (l = tasklist->tasks; l != NULL; l = l->next)
		update_task_state (l->data);
}

static void
tasklist_manager_disconnected_from_widget (TasklistManager *tasklist)
{
	if (tasklist->auto_grouping_idle != 0)
	{
		g_source_remove (tasklist->auto_grouping_idle);
		tasklist->auto_grouping_idle = 0;
	}

	if (tasklist->list)
	{
		GList *children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));
		for (GList *iter = children; iter != NULL; iter = g_list_next (iter))
			gtk_widget_destroy (GTK_WIDGET (iter->data));
		g_list_free (children);
		tasklist->list = NULL;
	}

	/* free any remaining tasks not removed by window-closed;
	 * toplevel_task_disconnected_from_widget (GDestroyNotify) handles freeing
	 * as buttons finalize, so just disconnect signal handlers here for safety */
	{
		GList *t;
		for (t = tasklist->tasks; t != NULL; t = t->next)
		{
			ToplevelTask *task = t->data;
			if (task->window)
			{
				g_signal_handlers_disconnect_by_data (task->window, task);
				g_object_unref (task->window);
			}
		}
	}

	if (tasklist->apps)
	{
		GHashTableIter iter;
		gpointer key, value;

		g_hash_table_iter_init (&iter, tasklist->apps);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			GroupTask *g = value;
			if (g && g->app)
				g_signal_handlers_disconnect_by_data (g->app, g);
		}

		g_hash_table_destroy (tasklist->apps);
		tasklist->apps = NULL;
	}

	if (tasklist->window_to_group)
	{
		g_hash_table_destroy (tasklist->window_to_group);
		tasklist->window_to_group = NULL;
	}

	if (tasklist->outer_box)
		tasklist->outer_box = NULL;

	if (tasklist->screen)
	{
		g_signal_handlers_disconnect_by_data (tasklist->screen, tasklist);
		g_object_unref (tasklist->screen);
		tasklist->screen = NULL;
	}

	if (tasklist->context_menu)
	{
		gtk_widget_destroy (tasklist->context_menu->menu);
		g_free (tasklist->context_menu);
		tasklist->context_menu = NULL;
	}

	g_free (tasklist);
}

static TasklistManager *
tasklist_manager_new (void)
{
	TasklistManager *tasklist;
	GList *windows, *l;
	XfwScreen *screen = xfw_screen_get_default ();

	if (screen == NULL)
		return NULL;

	tasklist = g_new0 (TasklistManager, 1);
	tasklist->screen = screen;
	tasklist->list = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous (GTK_BOX (tasklist->list), TRUE);
	tasklist->outer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (tasklist->outer_box), tasklist->list, FALSE, FALSE, 0);
	gtk_widget_show (tasklist->list);
	g_object_set_data_full (G_OBJECT (tasklist->outer_box),
				tasklist_manager_key,
				tasklist,
				(GDestroyNotify)tasklist_manager_disconnected_from_widget);
	tasklist->context_menu = context_menu_new ();

	tasklist->apps = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
						(GDestroyNotify) group_task_free);
	tasklist->window_to_group = g_hash_table_new (g_direct_hash, g_direct_equal);

	/* mouse scroll to switch windows */
	gtk_widget_add_events (tasklist->list, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	g_signal_connect (tasklist->list, "scroll-event",
			  G_CALLBACK (tasklist_scroll_event), tasklist);
	/* space-based auto-grouping */
	g_signal_connect (tasklist->list, "size-allocate",
			  G_CALLBACK (tasklist_list_size_allocate), tasklist);

	/* add all existing windows on this screen */
	windows = xfw_screen_get_windows (screen);
	for (l = windows; l != NULL; l = l->next)
	{
		XfwWindow *window = l->data;
		ToplevelTask *task = toplevel_task_new (tasklist, window);
		if (task != NULL)
			gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);
	}

	/* monitor window changes */
	g_signal_connect (screen, "window-opened",
			  G_CALLBACK (screen_window_opened), tasklist);
	g_signal_connect (screen, "window-closed",
			  G_CALLBACK (screen_window_closed), tasklist);
	g_signal_connect (screen, "active-window-changed",
			  G_CALLBACK (screen_active_window_changed), tasklist);

	return tasklist;
}

static void
toplevel_task_disconnected_from_widget (ToplevelTask *task)
{
	if (task->window)
	{
		if (task->state_changed_id)
			g_signal_handler_disconnect (task->window, task->state_changed_id);
		if (task->name_changed_id)
			g_signal_handler_disconnect (task->window, task->name_changed_id);
		if (task->icon_changed_id)
			g_signal_handler_disconnect (task->window, task->icon_changed_id);
		if (task->application_changed_id)
			g_signal_handler_disconnect (task->window, task->application_changed_id);
		g_object_unref (task->window);
	}

	if (task->tasklist)
		task->tasklist->tasks = g_list_remove (task->tasklist->tasks, task);

	task->button = NULL;
	task->icon = NULL;
	task->label = NULL;
	task->window = NULL;

	g_free (task);
}

static gboolean
tasklist_horizontal (TasklistManager *tasklist)
{
	return gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->list)) == GTK_ORIENTATION_HORIZONTAL;
}

static void
tasklist_button_drag_data_get (GtkWidget *widget,
			       GdkDragContext *context,
			       GtkSelectionData *selection_data,
			       guint info,
			       guint time,
			       ToplevelTask *task)
{
	gulong wid = wayland_task_window_get_wid (task->window);
	gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
				8, (guchar *) &wid, sizeof (wid));
}

static void
tasklist_button_drag_begin (GtkWidget *widget,
			    GdkDragContext *context,
			    ToplevelTask *task)
{
	GdkPixbuf *icon;
	gint scale;

	if (task->window)
	{
		scale = gtk_widget_get_scale_factor (widget);
		icon = xfw_window_get_icon (task->window, icon_size, scale);
		if (icon != NULL)
			gtk_drag_set_icon_pixbuf (context, icon, 0, 0);
	}
}

static gboolean
tasklist_button_drag_motion (GtkWidget *widget,
			     GdkDragContext *context,
			     gint x, gint y, guint time,
			     TasklistManager *tasklist)
{
	GtkWidget *source_widget;
	ToplevelTask *source, *target;
	GList *source_node, *target_node;
	GtkAllocation allocation;
	gboolean second_half;

	source_widget = gtk_drag_get_source_widget (context);
	if (source_widget == NULL)
		return FALSE;

	/* only reorder among our own tasklist buttons */
	if (gtk_widget_get_parent (source_widget) != GTK_WIDGET (tasklist->list))
		return FALSE;

	source = g_object_get_data (G_OBJECT (source_widget), toplevel_task_key);
	target = g_object_get_data (G_OBJECT (widget), toplevel_task_key);
	if (source == NULL || target == NULL || source == target)
		return FALSE;

	/* drop on the right/bottom half -> insert after the target button */
	gtk_widget_get_allocation (widget, &allocation);
	second_half = tasklist_horizontal (tasklist)
		? x >= allocation.width / 2
		: y >= allocation.height / 2;

	source_node = g_list_find (tasklist->tasks, source);
	target_node = g_list_find (tasklist->tasks, target);
	if (source_node == NULL || target_node == NULL)
		return FALSE;
	if (second_half)
		target_node = g_list_next (target_node);

	/* no change needed if already at the insertion point */
	if (source_node == target_node || g_list_next (source_node) == target_node)
	{
		gdk_drag_status (context, GDK_ACTION_MOVE, time);
		return TRUE;
	}

	/* live-reorder the list and the widgets */
	tasklist->tasks = g_list_remove (tasklist->tasks, source);
	tasklist->tasks = g_list_insert_before (tasklist->tasks, target_node, source);
	gtk_box_reorder_child (GTK_BOX (tasklist->list), source->button,
			       g_list_index (tasklist->tasks, source));
	gtk_widget_queue_resize (tasklist->list);

	gdk_drag_status (context, GDK_ACTION_MOVE, time);
	return TRUE;
}

static void
tasklist_button_drag_data_received (GtkWidget *widget,
				    GdkDragContext *context,
				    gint x, gint y,
				    GtkSelectionData *selection_data,
				    guint info,
				    guint time,
				    TasklistManager *tasklist)
{
	/* the list has already been reordered live by drag-motion, so
	 * nothing left to do here */
}

static void
toplevel_task_handle_clicked (GtkButton *button, ToplevelTask *task)
{
	if (task->window)
	{
		if (task->active)
		{
			xfw_window_set_minimized (task->window, TRUE, NULL);
		}
		else
		{
			xfw_window_activate (task->window, NULL, 0, NULL);
		}
	}
}

static gboolean on_toplevel_button_press (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist)
{
	/* Assume event is a button press */

	if (((GdkEventButton*)event)->button == GDK_BUTTON_MIDDLE &&
	    tasklist->middle_click_close)
	{
		ToplevelTask *task = g_object_get_data (G_OBJECT (button), toplevel_task_key);
		if (task && task->window)
			xfw_window_close (task->window, ((GdkEventButton*)event)->time, NULL);
		return TRUE;
	}

	if (((GdkEventButton*)event)->button == GDK_BUTTON_SECONDARY)
	{
		ContextMenu *menu = tasklist->context_menu;
		ToplevelTask *task = g_object_get_data (G_OBJECT (button), toplevel_task_key);

		g_object_set_data (G_OBJECT (menu->maximize), toplevel_task_key, task);
		g_object_set_data (G_OBJECT (menu->minimize), toplevel_task_key, task);
		g_object_set_data (G_OBJECT (menu->close), toplevel_task_key, task);
		g_object_set_data (G_OBJECT (menu->close_all), toplevel_task_key, task);

		/* offer "Close All Windows" whenever grouping is enabled, so the
		 * option exists in Auto mode even while the windows are shown as
		 * individual buttons (and not just on the group button) */
		gtk_widget_set_visible (menu->close_all,
					tasklist->grouping != WAYLAND_TASKLIST_NEVER_GROUP);

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

static gboolean
tasklist_scroll_event (GtkWidget *widget, GdkEventScroll *event, TasklistManager *tasklist)
{
	ToplevelTask *child;
	GList *li, *lnew = NULL;
	GdkScrollDirection direction;
	gboolean wrap_windows = TRUE;

	if (!tasklist->scroll_enabled)
		return TRUE;

	/* get the current active window button */
	for (li = tasklist->tasks; li != NULL; li = li->next)
	{
		child = li->data;
		if (child->window && !child->group && xfw_window_is_active (child->window))
			break;
	}

	if (li == NULL)
		return TRUE;

	if (event->direction != GDK_SCROLL_SMOOTH)
		direction = event->direction;
	else if (event->delta_y < 0)
		direction = GDK_SCROLL_UP;
	else if (event->delta_y > 0)
		direction = GDK_SCROLL_DOWN;
	else if (event->delta_x < 0)
		direction = GDK_SCROLL_LEFT;
	else if (event->delta_x > 0)
		direction = GDK_SCROLL_RIGHT;
	else
		return TRUE;

	switch (direction)
	{
		case GDK_SCROLL_UP:
			/* find the previous button on the tasklist */
			for (lnew = g_list_previous (li);; lnew = lnew->prev)
			{
				if (lnew == NULL)
				{
					if (wrap_windows)
					{
						lnew = g_list_last (li);
						wrap_windows = FALSE;
						if (lnew == NULL)
							break;
					}
					else
						break;
				}

				child = lnew->data;
				if (child->window != NULL && !child->group)
					break;
			}
			break;

		case GDK_SCROLL_DOWN:
			/* find the next button on the tasklist */
			for (lnew = g_list_next (li);; lnew = lnew->next)
			{
				if (lnew == NULL)
				{
					if (wrap_windows)
					{
						lnew = g_list_first (li);
						wrap_windows = FALSE;
						if (lnew == NULL)
							break;
					}
					else
						break;
				}

				child = lnew->data;
				if (child->window != NULL && !child->group)
					break;
			}
			break;

		case GDK_SCROLL_LEFT:
		case GDK_SCROLL_RIGHT:
		default:
			return TRUE;
	}

	if (lnew != NULL)
	{
		child = lnew->data;
		xfw_window_activate (child->window, NULL, event->time, NULL);
	}

	return TRUE;
}

static ToplevelTask *
toplevel_task_new (TasklistManager *tasklist, XfwWindow *window)
{
	ToplevelTask *task = g_new0 (ToplevelTask, 1);
	GtkOrientation orient;
	GtkWidget *whole_panel_box, *parent_box;
	int button_space, panel_width;

	if (window == NULL)
	{
		g_free (task);
		return NULL;
	}

	buttons = buttons + 1;
	orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box));
	task->window = g_object_ref (window);
	task->tasklist = tasklist;
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

	g_object_set_data_full (G_OBJECT (task->button),
				toplevel_task_key,
				task,
				(GDestroyNotify)toplevel_task_disconnected_from_widget);

	g_signal_connect (task->button, "button-press-event",
			  G_CALLBACK (on_toplevel_button_press),
			  tasklist);

	/* mouse scroll to switch windows */
	gtk_widget_add_events (task->button, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	g_signal_connect (task->button, "scroll-event",
			  G_CALLBACK (tasklist_scroll_event), tasklist);

	/* drag-and-drop to reorder task buttons */
	gtk_drag_source_set (task->button, GDK_BUTTON1_MASK,
			     source_targets, G_N_ELEMENTS (source_targets),
			     GDK_ACTION_MOVE);
	gtk_drag_dest_set (task->button, GTK_DEST_DEFAULT_DROP,
			   source_targets, G_N_ELEMENTS (source_targets),
			   GDK_ACTION_MOVE);
	g_signal_connect (task->button, "drag-data-get",
			  G_CALLBACK (tasklist_button_drag_data_get), task);
	g_signal_connect (task->button, "drag-data-received",
			  G_CALLBACK (tasklist_button_drag_data_received), tasklist);
	g_signal_connect (task->button, "drag-motion",
			  G_CALLBACK (tasklist_button_drag_motion), tasklist);
	g_signal_connect (task->button, "drag-begin",
			  G_CALLBACK (tasklist_button_drag_begin), task);

	/* monitor window changes */
	task->state_changed_id = g_signal_connect (window, "state-changed",
						   G_CALLBACK (window_state_changed), task);
	task->name_changed_id = g_signal_connect (window, "name-changed",
						  G_CALLBACK (window_name_changed), task);
	task->icon_changed_id = g_signal_connect (window, "icon-changed",
						  G_CALLBACK (window_icon_changed), task);
	task->application_changed_id = g_signal_connect (window, "notify::application",
							 G_CALLBACK (window_application_changed), task);

	/* set initial contents */
	gtk_label_set_label (GTK_LABEL (task->label), xfw_window_get_name (window));
	update_task_icon (task);
	update_task_state (task);

	tasklist->tasks = g_list_append (tasklist->tasks, task);

	/* assign to an application group if grouping is enabled */
	tasklist_assign_to_group (tasklist, task);

	/* Buttons on a vertical panel are not affected by how many are needed
	 * GTK handles compressing contents as needed as the window width tells
	 * GTK how much space to allocate the label and icon. Buttons will use
	 * the full width of a vertical panel without any special attention
	 * so break out here instead of breaking the vertical panel case
	 */

	if (orient == GTK_ORIENTATION_VERTICAL)
		return task;

	/* On horizontal panels, GTK does not by default limit the width of the tasklist
	 * as it does not run out of space in the window until the entire panel is used,
	 * leaving buttons at full width until then and overflowing all other applets
	 *
	 * Thus we must get the tasklist's allocated width when extra space remains,
	 * which will be most of the distance between the handle and the next applet
	 * From there, we can expand buttons and/or hide elements as needed
	 * For some reason this function always gets called twice, so use half the value of buttons
	 * but do not attempt to adjust the global value as it would get adjusted twice
	 * Since we are adding a button here the true value cannot be zero
	 */
	whole_panel_box = gtk_widget_get_toplevel(GTK_WIDGET (tasklist->outer_box));
	parent_box = gtk_widget_get_ancestor(GTK_WIDGET (tasklist->outer_box), GTK_TYPE_BOX);
	if (gtk_widget_get_allocated_width (parent_box) > 1)
	{
		tasklist_width = gtk_widget_get_allocated_width (parent_box);
	}
	else
	{
		tasklist_width = MAX(gtk_widget_get_allocated_width (parent_box), tasklist_width);
	}

	panel_width = gtk_widget_get_allocated_width (whole_panel_box);

	/*on startup we get an allocated with of zero, so start with 1/3 the panel width
	 *as a sane default
	 *This may overflow on very crowded panels where the tasklist is less than 1/3ed the
	 *panel witth but will self-correct on opening or closing a few windows
	 */

	if (tasklist_width <= 2)
		tasklist_width = panel_width / 3;

	/*Do not allow buttons to equal zero or the division below is a crasher*/
	buttons = MAX ((buttons), 1);

	/*always allow at least three buttons to fit without adjustment
	 *so short window lists don't overflow
	 */
	if (tasklist_width > 0)
	{
		full_button_width = MIN(max_button_width, tasklist_width / 3);
	}

	/*Leave a little space so the buttons don't push other applets off the panel*/
	button_space = (tasklist_width / buttons) * 0.75;
	button_space = MIN(button_space, full_button_width);

	/* iterate over all the buttons*/
	adjust_buttons (GTK_CONTAINER (tasklist->list), button_space, buttons, task);

	/*Reset the tasklist width after button adjustments*/
	if (gtk_widget_get_allocated_width (parent_box) > 1)
	{
		tasklist_width = gtk_widget_get_allocated_width (parent_box);
	}
	else
	{
		tasklist_width = MAX(gtk_widget_get_allocated_width (parent_box), tasklist_width);
	}
	return task;
}

static gboolean
wayland_tasklist_init_if_needed (void)
{
	GdkDisplay *gdk_display;

	gdk_display = gdk_display_get_default ();
	g_return_val_if_fail (gdk_display, FALSE);
	g_return_val_if_fail (GDK_IS_WAYLAND_DISPLAY (gdk_display), FALSE);

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	return TRUE;
}

GtkWidget*
wayland_tasklist_new ()
{
	if (!wayland_tasklist_init_if_needed ())
		return gtk_label_new ("Shell does not support WLR Foreign Toplevel Control");

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

void
wayland_tasklist_set_middle_click_close (GtkWidget *tasklist_widget, gboolean enabled)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	tasklist->middle_click_close = enabled;
}

void
wayland_tasklist_set_scroll_enabled (GtkWidget *tasklist_widget, gboolean enabled)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	tasklist->scroll_enabled = enabled;
}

static void
tasklist_rebuild (TasklistManager *tasklist)
{
	GHashTableIter iter;
	gpointer key, value;
	GList *t, *l;
	GList *to_destroy = NULL;

	/* stop tracking window changes while rebuilding */
	if (tasklist->screen)
		g_signal_handlers_disconnect_by_data (tasklist->screen, tasklist);

	/* guard against re-entrant rebuilds: the show/hide of buttons during
	 * the rebuild triggers size-allocate, which would otherwise schedule
	 * another rebuild based on a half-updated grouping state */
	tasklist->rebuilding = TRUE;

	/* 1. Disconnect groups from their applications and collect every group
	 *    button so we can destroy them in one pass below. The individual
	 *    task buttons are kept alive to avoid freeing XfwWindow objects
	 *    that libxfce4windowing may not hold a strong reference to. */
	if (tasklist->apps)
	{
		g_hash_table_iter_init (&iter, tasklist->apps);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			GroupTask *group = value;

			if (group == NULL)
				continue;

			if (group->app)
				g_signal_handlers_disconnect_by_data (group->app, group);

			if (group->button)
				to_destroy = g_list_prepend (to_destroy, group->button);

			group->members = NULL;
		}

		for (l = to_destroy; l != NULL; l = l->next)
		{
			GtkWidget *btn = GTK_WIDGET (l->data);
			if (gtk_widget_get_parent (btn))
				gtk_widget_destroy (btn);
		}
		g_list_free (to_destroy);

		g_hash_table_remove_all (tasklist->apps);
	}

	if (tasklist->window_to_group)
		g_hash_table_remove_all (tasklist->window_to_group);

	/* 2. Clear each task's grouping state. */
	for (t = tasklist->tasks; t != NULL; t = t->next)
	{
		ToplevelTask *task = t->data;

		if (task)
			task->group = NULL;
	}

	/* 3. Show every individual task button again and assign it according to
	 *    the current grouping mode. */
	for (t = tasklist->tasks; t != NULL; t = t->next)
	{
		ToplevelTask *task = t->data;

		if (task == NULL || task->button == NULL)
			continue;

		gtk_widget_show (task->button);

		if (gtk_widget_get_parent (task->button) == NULL)
			gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);

		tasklist_assign_to_group (tasklist, task);
	}

	/* 4. Re-enable screen event tracking */
	g_signal_connect (tasklist->screen, "window-opened",
			  G_CALLBACK (screen_window_opened), tasklist);
	g_signal_connect (tasklist->screen, "window-closed",
			  G_CALLBACK (screen_window_closed), tasklist);
	g_signal_connect (tasklist->screen, "active-window-changed",
			  G_CALLBACK (screen_active_window_changed), tasklist);

	tasklist->rebuilding = FALSE;

	if (gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box)) != GTK_ORIENTATION_VERTICAL)
	{
		GList *children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));
		guint visible = g_list_length (children);
		g_list_free (children);

		if (visible > 0)
		{
			GtkWidget *parent_box;
			int button_space;

			parent_box = gtk_widget_get_ancestor (tasklist->outer_box, GTK_TYPE_BOX);
			tasklist_width = MAX (gtk_widget_get_allocated_width (parent_box),
					      tasklist_width);
			button_space = (tasklist_width / visible) * 0.75;
			button_space = MIN (button_space, full_button_width);
			adjust_buttons (GTK_CONTAINER (tasklist->list), button_space, visible, NULL);
		}
	}

	gtk_widget_queue_resize (tasklist->list);
}

void
wayland_tasklist_set_grouping (GtkWidget *tasklist_widget, WaylandTasklistGroupingType grouping)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);

	if (tasklist->grouping == grouping)
		return;

	tasklist->grouping = grouping;

	/* when switching away from auto-grouping, stop the space-based
	 * regrouping so it doesn't fight with the new mode */
	if (grouping != WAYLAND_TASKLIST_AUTO_GROUP)
	{
		if (tasklist->auto_grouping_idle != 0)
		{
			g_source_remove (tasklist->auto_grouping_idle);
			tasklist->auto_grouping_idle = 0;
		}
		tasklist->auto_grouping = FALSE;
		tasklist->auto_grouping_applied = FALSE;
	}

	tasklist_rebuild (tasklist);
}
