/* Wncklet applet tasklist core, independent of the windowing backend */

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

#include <gtk/gtk.h>

#include "tasklist-backend.h"

/*shorter than wnck-tasklist due to common use of larger fonts*/
#define TASKLIST_TEXT_MAX_WIDTH 16

/*In the future this could be changable from the panel-prefs dialog*/
static const int max_button_width = 180;

/* GTK hands most panel button themes back a zero button padding, which makes
 * the icon size picker choose one size too large for the panel. Enforce a
 * floor so the icons keep a little breathing room at the panel edges. */
static const int min_button_padding = 2;

/* Fallback icon size, used until the panel tells us how much room it has.
 * After that TasklistManager.icon_size_enum/icon_px take over. */
static const int icon_size = 16;

/* The width a button shrinks to before it drops to just its icon. This
 * matches libwnck's DEFAULT_GROUPING_LIMIT so that size hints computed by
 * the core line up with the ones a WnckTasklist used to produce. */
static const int default_grouping_limit = 80;

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
	const TasklistBackend *backend;
	gpointer screen;
	GList *tasks;
	gboolean scroll_enabled;
	gboolean middle_click_close;
	TasklistGroupingType grouping;
	gboolean include_all_workspaces;
	gboolean switch_workspace_on_unminimize;
	gboolean auto_grouping;
	gboolean auto_grouping_applied;
	guint auto_grouping_idle;
	gboolean rebuilding;
	GHashTable *apps;
	GHashTable *window_to_group;

	/* Icon-size refresh scheduled on a theme change, so the fit can be
	 * recalculated off the new button padding. */
	guint icon_refresh_idle;
	gulong theme_changed_signal;

	/* Button sizing, kept per tasklist rather than in file statics so that
	 * several tasklists cannot interfere with each other. */
	guint buttons;
	gint tasklist_width;
	gint full_button_width;
	gint grouping_limit;
	gint last_button_space;

	/* Standard icon size the buttons are rendering at, picked to fit the
	 * panel the applet was given. icon_px is its resolved size in logical
	 * pixels, cached because the show/hide thresholds need it on every
	 * pass. panel_thickness is the panel size itself, height on a
	 * horizontal panel and width on a vertical one, 0 until the applet
	 * reports it. */
	GtkIconSize icon_size_enum;
	gint icon_px;
	gint panel_thickness;

	/* Descending (max, min) width staircase handed to the panel, which walks
	 * it to decide how wide the tasklist may be as space runs short. */
	gint *size_hints;
	guint size_hints_len;
} TasklistManager;

typedef struct
{
	GtkWidget *button;
	GtkWidget *icon;
	GtkWidget *label;
	gpointer window;
	gboolean active;
	gboolean maximized;
	gboolean minimized;
	gboolean fullscreen;
	gboolean urgent;
	TasklistManager *tasklist;
	GroupTask *group;
	gint natural_width;
} ToplevelTask;

struct _GroupTask
{
	TasklistManager *tasklist;
	gpointer app;
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

/* The outer box carries the hover notifications, so it needs a type of its
 * own to hang signals on. It adds no state of its own: the manager is still
 * reached through the tasklist_manager_key object data. */
typedef struct
{
	GtkBox parent_instance;
} TasklistBox;

typedef struct
{
	GtkBoxClass parent_class;
} TasklistBoxClass;

#define TASKLIST_TYPE_BOX (tasklist_box_get_type ())
GType tasklist_box_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (TasklistBox, tasklist_box, GTK_TYPE_BOX)

enum
{
	TASK_ENTER_NOTIFY,
	TASK_LEAVE_NOTIFY,
	TASKLIST_BOX_LAST_SIGNAL
};

static guint tasklist_box_signals[TASKLIST_BOX_LAST_SIGNAL];

static void
tasklist_box_class_init (TasklistBoxClass *klass)
{
	tasklist_box_signals[TASK_ENTER_NOTIFY] =
		g_signal_new ("task-enter-notify", TASKLIST_TYPE_BOX,
			      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	tasklist_box_signals[TASK_LEAVE_NOTIFY] =
		g_signal_new ("task-leave-notify", TASKLIST_TYPE_BOX,
			      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
tasklist_box_init (TasklistBox *box)
{
}

static GtkTargetEntry source_targets[] =
{
	{ "application/x-wnck-window-id", 0, 0 }
};

static ToplevelTask *toplevel_task_new (TasklistManager *tasklist, gpointer window);
static gboolean tasklist_scroll_event (GtkWidget *widget, GdkEventScroll *event, TasklistManager *tasklist);
static void group_task_update (GroupTask *group);
static void group_task_update_name (GroupTask *group);
static void group_task_update_name_for (GroupTask *group, ToplevelTask *task);
static void group_task_add_window (GroupTask *group, ToplevelTask *task);
static void group_task_remove_window (GroupTask *group, ToplevelTask *task);
static void tasklist_assign_to_group (TasklistManager *tasklist, ToplevelTask *task);
static void group_task_child_state_changed (GroupTask *group);
static void tasklist_rebuild (TasklistManager *tasklist);
static void adjust_buttons (TasklistManager *tasklist, int button_space, int buttons, ToplevelTask *task);
static void tasklist_update_size_hints (TasklistManager *tasklist, gint natural_width);
static void tasklist_refresh_icon_size (TasklistManager *tasklist);
static void tasklist_theme_changed (GtkSettings *settings, GParamSpec *pspec, TasklistManager *tasklist);
static gint tasklist_count_visible_buttons (TasklistManager *tasklist);
static void tasklist_refresh_visibility (TasklistManager *tasklist);
static void tasklist_unminimize (TasklistManager *tasklist, gpointer window);
static gboolean on_toplevel_button_enter_notify (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist);
static gboolean on_toplevel_button_leave_notify (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist);

static gboolean
window_is_active (TasklistManager *tasklist, gpointer window)
{
	if (window == NULL)
		return FALSE;

	return (tasklist->backend->get_window_state (window) & TASKLIST_STATE_ACTIVE) != 0;
}

/* A window only gets a visible task button when the tasklist shows every
 * workspace, or when the window is on the one that is active. */
static gboolean
task_should_be_shown (TasklistManager *tasklist, gpointer window)
{
	const TasklistBackend *backend = tasklist->backend;

	if (tasklist->include_all_workspaces)
		return TRUE;

	return backend->window_is_on_active_workspace (window);
}

static void
update_task_visibility (ToplevelTask *task)
{
	if (task == NULL || task->button == NULL)
		return;

	gtk_widget_set_visible (task->button,
				task_should_be_shown (task->tasklist, task->window));
}

static void
update_task_state (ToplevelTask *task)
{
	TasklistWindowState state;

	if (!task || !task->window)
		return;

	state = task->tasklist->backend->get_window_state (task->window);
	task->active = (state & TASKLIST_STATE_ACTIVE) != 0;
	task->maximized = (state & TASKLIST_STATE_MAXIMIZED) != 0;
	task->minimized = (state & TASKLIST_STATE_MINIMIZED) != 0;
	task->fullscreen = (state & TASKLIST_STATE_FULLSCREEN) != 0;
	task->urgent = (state & TASKLIST_STATE_URGENT) != 0;

	if (task->button)
	{
		if (task->urgent)
			gtk_style_context_add_class (gtk_widget_get_style_context (task->button), "urgent");
		else
			gtk_style_context_remove_class (gtk_widget_get_style_context (task->button), "urgent");

		gtk_button_set_relief (GTK_BUTTON (task->button),
				       task->active ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE);
	}

	update_task_visibility (task);

	if (task->group)
		group_task_child_state_changed (task->group);
}

static void
adjust_buttons (TasklistManager *tasklist, int button_space, int buttons, ToplevelTask *task)
{
	GtkWidget *widget, *button, *box;

	/*catch the case of an added button that can be missed
	 *Note that button space can come up zero on a first button
	 */
	if (buttons < 2)
	{
		if(task)
		{
			gtk_widget_set_size_request (task->button, tasklist->full_button_width, -1);
		}
	}

	if ((task) && (button_space > 0) && (button_space < tasklist->icon_px * 3))
	{
		gtk_widget_hide (task->icon);
	}
	else if (task)
	{
		gtk_widget_show (task->icon);
	}

	if ((task) && (button_space > 0) && (button_space < tasklist->icon_px))
	{
		gtk_widget_hide (task->label);
	}
	else if (task)
	{
		gtk_widget_show (task->label);
	}

	GList* children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));

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

		if ((buttons < 2) || (buttons * tasklist->full_button_width < tasklist->tasklist_width * 0.75))
		{
			gtk_widget_set_size_request (button, tasklist->full_button_width, -1);
			gtk_widget_show_all (button);
			children = children->next;
			continue;
		}
		else
		{
			gtk_widget_set_size_request (button, MIN(button_space, tasklist->full_button_width), -1);
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
					if ((button_space < tasklist->icon_px * 3) && (button_space > 1))
						gtk_widget_hide (widget);

					else
						gtk_widget_show (widget);

				}

				/*Show or hide the label*/
				if (GTK_IS_LABEL (widget))
				{
					if ((button_space < tasklist->icon_px) && (button_space > 1))
					{
						gtk_widget_hide (widget);
						/*We can go a little wider for empty buttons*/
						gtk_widget_set_size_request (button, tasklist->tasklist_width / buttons * 0.9, -1);
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
update_task_icon (ToplevelTask *task)
{
	const TasklistBackend *backend;
	GIcon *icon = NULL;
	gpointer app;
	gint width = 0;

	if (!task || !task->window || !task->icon)
		return;

	backend = task->tasklist->backend;

	app = backend->get_window_application (task->window);
	if (app != NULL)
		icon = backend->get_app_gicon (app);
	if (icon == NULL)
		icon = backend->get_window_gicon (task->window);

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (task->icon), icon,
					  task->tasklist->icon_size_enum);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (task->icon),
					      backend->get_fallback_icon_name (),
					      task->tasklist->icon_size_enum);

	/* Force the rendered size, not just the size asked for above. The icon
	 * theme is free to hand back whatever pixel size it likes, so an
	 * application that only ships a 128x128 icon would otherwise drag the
	 * button's minimum width up with it and push the tasklist off the
	 * panel. With a pixel size set, GTK asks the theme for exactly this
	 * size and scales there, which also keeps the result sharp. */
	gtk_icon_size_lookup (task->tasklist->icon_size_enum, &width, NULL);
	gtk_image_set_pixel_size (GTK_IMAGE (task->icon), width);
}

static GIcon *
window_gicon (TasklistManager *tasklist, gpointer window)
{
	const TasklistBackend *backend;
	GIcon *icon = NULL;
	gpointer app;

	if (!window)
		return NULL;

	backend = tasklist->backend;

	app = backend->get_window_application (window);
	if (app != NULL)
		icon = backend->get_app_gicon (app);
	if (icon == NULL)
		icon = backend->get_window_gicon (window);

	return icon;
}

static void
group_menu_window_activate (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = user_data;

	if (task && task->window)
		task->tasklist->backend->activate_window (task->window, 0);
}

static void
group_task_update_name (GroupTask *group)
{
	const TasklistBackend *backend = group->tasklist->backend;
	gpointer active;
	GList *l;
	const gchar *name = NULL;

	if (!group || !group->label)
		return;

	active = NULL;
	if (group->tasklist && group->tasklist->screen)
		active = backend->get_active_window (group->tasklist->screen);

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (!task || !task->window)
			continue;

		if (name == NULL)
			name = backend->get_window_name (task->window);
		if (active != NULL && task->window == active)
		{
			name = backend->get_window_name (task->window);
			break;
		}
	}

	if (name == NULL)
		name = backend->get_app_name (group->app);

	gtk_label_set_label (GTK_LABEL (group->label), name ? name : "");
}

static void
group_task_update_name_for (GroupTask *group, ToplevelTask *task)
{
	const gchar *name;

	if (!group || !group->label || !task || !task->window)
		return;

	name = group->tasklist->backend->get_window_name (task->window);
	gtk_label_set_label (GTK_LABEL (group->label), name ? name : "");
}

static void
group_task_update_icon (GroupTask *group)
{
	GIcon *icon;
	gint width = 0;

	if (!group || !group->icon)
		return;

	icon = group->tasklist->backend->get_app_gicon (group->app);

	if (icon != NULL)
		gtk_image_set_from_gicon (GTK_IMAGE (group->icon), icon,
					  group->tasklist->icon_size_enum);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (group->icon),
					      group->tasklist->backend->get_fallback_icon_name (),
					      group->tasklist->icon_size_enum);

	/* Same reason as update_task_icon(): the group button must not inherit
	 * the minimum width of whatever the icon theme decides to hand back. */
	gtk_icon_size_lookup (group->tasklist->icon_size_enum, &width, NULL);
	gtk_image_set_pixel_size (GTK_IMAGE (group->icon), width);
}

static gboolean
tasklist_should_group (TasklistManager *tasklist, guint n_windows)
{
	if (tasklist->grouping == TASKLIST_NEVER_GROUP)
		return FALSE;
	if (tasklist->grouping == TASKLIST_ALWAYS_GROUP)
		return n_windows >= 2;
	return tasklist->auto_grouping_applied && n_windows >= 2;
}

static gint
tasklist_count_visible_buttons (TasklistManager *tasklist)
{
	GList *children, *l;
	gint visible = 0;

	children = gtk_container_get_children (GTK_CONTAINER (tasklist->list));

	for (l = children; l != NULL; l = l->next)
	{
		if (gtk_widget_get_visible (GTK_WIDGET (l->data)))
			visible++;
	}

	g_list_free (children);

	return visible;
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
			task->natural_width = tasklist->icon_px + 32;

		needed += task->natural_width;
	}

	tasklist_update_size_hints (tasklist, needed);

	return needed > available_width;
}

/* The standard icon sizes the tasklist may render at, largest first. Only
 * these are offered because GtkImage wants a GtkIconSize to look the icon up
 * with and a pixel size to pin the result to; both have to be set, or the
 * icon theme is free to hand back whatever size it likes. */
static const GtkIconSize tasklist_icon_sizes[] = {
	GTK_ICON_SIZE_DIALOG,
	GTK_ICON_SIZE_DND,
	GTK_ICON_SIZE_LARGE_TOOLBAR,
	GTK_ICON_SIZE_MENU,
};

/* What the panel has left over for an icon once the button's own style padding
 * is taken off, matching what showdesktop.c does. The panel size the applet
 * reports and the sizes gtk_icon_size_lookup() returns are both logical
 * pixels, so unlike showdesktop there is no scale factor to apply here: it
 * loads the surface itself, we only tell GTK what to load. */
static gint
tasklist_icon_available_space (TasklistManager *tasklist)
{
	GtkWidget *probe = NULL;
	GtkStyleContext *context;
	GtkStateFlags state;
	GtkBorder padding;
	const gboolean horizontal =
		gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box)) ==
		GTK_ORIENTATION_HORIZONTAL;

	if (tasklist->panel_thickness <= 0)
		return 0;

	/* Probing a real button is the accurate case: set_panel_size() runs
	 * after tasklist_manager_new() has already added the existing
	 * windows. Fall back to the box so a tasklist with no windows still
	 * gets a sane answer. */
	if (tasklist->tasks != NULL && tasklist->tasks->data != NULL)
		probe = ((ToplevelTask *) tasklist->tasks->data)->button;

	if (probe == NULL)
		probe = tasklist->list;

	context = gtk_widget_get_style_context (probe);
	state = gtk_widget_get_state_flags (probe);
	gtk_style_context_get_padding (context, state, &padding);

	padding.top = MAX (padding.top, min_button_padding);
	padding.bottom = MAX (padding.bottom, min_button_padding);
	padding.left = MAX (padding.left, min_button_padding);
	padding.right = MAX (padding.right, min_button_padding);

	if (horizontal)
		return tasklist->panel_thickness - padding.top - padding.bottom;

	return tasklist->panel_thickness - padding.left - padding.right;
}

/* Largest standard icon size that still fits the space available. */
static GtkIconSize
tasklist_choose_icon_size (gint available_px)
{
	guint i;
	gint width = 0, height = 0;

	for (i = 0; i < G_N_ELEMENTS (tasklist_icon_sizes); i++)
	{
		if (!gtk_icon_size_lookup (tasklist_icon_sizes[i], &width, &height))
			continue;

		if (MAX (width, height) <= available_px)
			return tasklist_icon_sizes[i];
	}

	/* Nothing fits, so keep the smallest, which is what the tasklist has
	 * always used. It is the one size that is never worse than before. */
	return GTK_ICON_SIZE_MENU;
}

/* Push the current icon size at every button. The buttons' minimum widths
 * follow the icon, so the hints the panel is holding go stale with it: clear
 * the button_space cache, ask for a relayout, and re-measure now rather than
 * waiting for the next size-allocate, which may not come if only the panel's
 * thickness changed. */
static void
tasklist_apply_icon_size (TasklistManager *tasklist)
{
	GHashTableIter iter;
	gpointer key, value;
	gint width = 0, height = 0;
	GList *l;

	for (l = tasklist->tasks; l != NULL; l = l->next)
		update_task_icon (l->data);

	if (tasklist->apps)
	{
		g_hash_table_iter_init (&iter, tasklist->apps);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			GroupTask *group = value;

			if (group == NULL)
				continue;

			group_task_update_icon (group);
		}
	}

	/* Keep a usable floor even if the theme refuses to answer for this
	 * size: icon_px drives the show/hide thresholds, and 0 would make
	 * every button drop its icon. */
	if (gtk_icon_size_lookup (tasklist->icon_size_enum, &width, &height))
		tasklist->icon_px = MAX (width, height);
	else
		tasklist->icon_px = icon_size;

	tasklist->last_button_space = -1;
	gtk_widget_queue_resize (tasklist->outer_box);

	if (tasklist->tasklist_width > 0)
		tasklist_buttons_crowded (tasklist, tasklist->tasklist_width);
}

/* Build the width staircase the panel walks when it has to squeeze the
 * tasklist. The array is a descending series of (max, min) pixel pairs: give
 * me this much room and the buttons fit at their natural width, and at least
 * this much room or they collapse. The last entry is always 0 so the tasklist
 * can be squeezed down to nothing rather than pushed off the panel.
 *
 * We only describe the ungrouped layout. Collapsing windows into groups frees
 * space too, but that happens on its own via the auto-grouping check above, so
 * omitting those steps only makes the hint list slightly less precise.
 */
static void
tasklist_update_size_hints (TasklistManager *tasklist, gint natural_width)
{
	const gboolean horizontal =
		gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box)) ==
		GTK_ORIENTATION_HORIZONTAL;
	const guint n_buttons = tasklist->buttons;
	gint squeezed;
	GArray *hints;

	if (n_buttons == 0)
	{
		g_clear_pointer (&tasklist->size_hints, g_free);
		tasklist->size_hints_len = 0;
		return;
	}

	/* A vertical panel is sized by height, and the core does not compress
	 * vertical buttons at all, so there is nothing to describe. */
	if (!horizontal)
		return;

	squeezed = MIN (tasklist->grouping_limit, max_button_width) * (gint)n_buttons;

	hints = g_array_new (FALSE, FALSE, sizeof (gint));
	g_array_append_val (hints, natural_width);
	g_array_append_val (hints, squeezed);

	/* Always allow going down to a zero size. */
	((gint *)hints->data)[hints->len - 1] = 0;

	g_free (tasklist->size_hints);
	tasklist->size_hints_len = hints->len;
	tasklist->size_hints = (gint *)g_array_free (hints, FALSE);
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
	gboolean auto_group = (tasklist->grouping == TASKLIST_AUTO_GROUP);
	gint visible, button_space;

	if (!auto_group)
		tasklist->auto_grouping = FALSE;

	/* don't evaluate crowd state while we are rebuilding, the buttons are
	 * half-hidden and the measurement would suggest the wrong thing */
	if (tasklist->rebuilding)
		return;

	if (gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box)) == GTK_ORIENTATION_HORIZONTAL
	    && allocation->width > 1)
	{
		visible = tasklist_count_visible_buttons (tasklist);

		if (visible > 0)
		{
			tasklist->tasklist_width = allocation->width;
			tasklist->full_button_width = MIN (max_button_width, allocation->width / 3);
			button_space = MIN ((allocation->width / visible) * 0.75,
					    tasklist->full_button_width);

			if (button_space != tasklist->last_button_space)
			{
				tasklist->last_button_space = button_space;
				adjust_buttons (tasklist, button_space, visible, NULL);
			}
		}
	}

	crowded = tasklist_buttons_crowded (tasklist, allocation->width);

	if (!auto_group)
		return;

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
group_menu_close_all (GtkMenuItem *item, gpointer user_data)
{
	GroupTask *group = user_data;
	GList *l;

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task && task->window)
			task->tasklist->backend->close_window (task->window, 0);
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
		const TasklistBackend *backend = group->tasklist->backend;
		GtkWidget *item, *box, *icon, *label;
		GIcon *gicon;
		gint width = 0;

		if (!task || !task->window)
			continue;

		item = gtk_menu_item_new ();
		box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		icon = gtk_image_new ();
		gicon = window_gicon (group->tasklist, task->window);

		if (gicon != NULL)
			gtk_image_set_from_gicon (GTK_IMAGE (icon), gicon,
						  group->tasklist->icon_size_enum);
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (icon),
						      backend->get_fallback_icon_name (),
						      group->tasklist->icon_size_enum);

		/* A menu row would otherwise be stretched to the icon's own
		 * pixel size, which is not fixed at GTK_ICON_SIZE_MENU. */
		gtk_icon_size_lookup (group->tasklist->icon_size_enum, &width, NULL);
		gtk_image_set_pixel_size (GTK_IMAGE (icon), width);

		label = gtk_label_new (backend->get_window_name (task->window));
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
				task->tasklist->backend->close_window (task->window,
									     ((GdkEventButton*)event)->time);
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
group_task_new (TasklistManager *tasklist, gpointer app)
{
	GroupTask *group;

	group = g_new0 (GroupTask, 1);
	group->tasklist = tasklist;
	group->app = app;

	group->button = gtk_button_new ();
	group->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	group->icon = gtk_image_new_from_icon_name (tasklist->backend->get_fallback_icon_name (),
						    GTK_ICON_SIZE_MENU);
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
	g_signal_connect (group->button, "enter-notify-event",
			  G_CALLBACK (on_toplevel_button_enter_notify), tasklist);
	g_signal_connect (group->button, "leave-notify-event",
			  G_CALLBACK (on_toplevel_button_leave_notify), tasklist);

	tasklist->backend->set_app_tracking (GTK_WIDGET (tasklist->outer_box),
					     app, TRUE);

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

	/* A group can be freed while the application is still being tracked,
	 * either when its last window goes away or when the whole tasklist is
	 * torn down, so stop tracking here rather than only in tasklist_rebuild. */
	if (group->app && group->tasklist)
		group->tasklist->backend->set_app_tracking (GTK_WIDGET (group->tasklist->outer_box),
							    group->app, FALSE);

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
	const TasklistBackend *backend = tasklist->backend;
	gpointer app;
	GroupTask *group;

	if (tasklist->grouping == TASKLIST_NEVER_GROUP)
		return;

	app = backend->get_window_application (task->window);
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
menu_on_maximize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
		task->tasklist->backend->set_window_maximized (task->window, !task->maximized);
}

static void
menu_on_minimize (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
	{
		if (task->minimized)
			tasklist_unminimize (task->tasklist, task->window);
		else
			task->tasklist->backend->set_window_minimized (task->window, TRUE);
	}
}

static void
menu_on_close (GtkMenuItem *item, gpointer user_data)
{
	ToplevelTask *task = g_object_get_data (G_OBJECT (item), toplevel_task_key);
	if (task && task->window)
		task->tasklist->backend->close_window (task->window, 0);
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
			task->tasklist->backend->close_window (task->window, 0);
		return;
	}

	for (l = group->members; l != NULL; l = l->next)
	{
		ToplevelTask *member = l->data;

		if (member && member->window)
			member->tasklist->backend->close_window (member->window, 0);
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
tasklist_has_window (TasklistManager *tasklist, gpointer window)
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
tasklist_remove_window (TasklistManager *tasklist, gpointer window)
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

			tasklist->buttons = tasklist->buttons - 1;

			/* remove the window from its group (if any) before destroying the button */
			if (task->group)
				group_task_remove_window (task->group, task);

			outer_box = tasklist->outer_box;
			/* removes the task from tasklist->tasks and destroys the button */
			tasklist->tasks = g_list_remove (tasklist->tasks, task);
			gtk_widget_destroy (task->button);

			if (tasklist->buttons == 0)
				return;

		/* We don't need to modify button size on a vertical panel*/
			orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (outer_box));
			if (orient == GTK_ORIENTATION_VERTICAL)
				return;

			/*Get the box the tasklist outer box sits in
			 *and leave a little space so the buttons don't push other applets off the panel
			 */
			parent_box = gtk_widget_get_ancestor ((outer_box), GTK_TYPE_BOX);
			tasklist->tasklist_width = MAX (gtk_widget_get_allocated_width (parent_box),
							tasklist->tasklist_width);
			button_space = (tasklist->tasklist_width / tasklist->buttons) * 0.75;
			button_space = MIN (button_space, tasklist->full_button_width);
			adjust_buttons (tasklist, button_space, tasklist->buttons, NULL);

			return;
		}
	}
}

static void
tasklist_manager_disconnected_from_widget (TasklistManager *tasklist)
{
	if (tasklist->auto_grouping_idle != 0)
	{
		g_source_remove (tasklist->auto_grouping_idle);
		tasklist->auto_grouping_idle = 0;
	}

	if (tasklist->icon_refresh_idle != 0)
	{
		g_source_remove (tasklist->icon_refresh_idle);
		tasklist->icon_refresh_idle = 0;
	}

	if (tasklist->theme_changed_signal != 0)
	{
		g_signal_handler_disconnect (gtk_settings_get_default (),
					     tasklist->theme_changed_signal);
		tasklist->theme_changed_signal = 0;
	}

	g_clear_pointer (&tasklist->size_hints, g_free);
	tasklist->size_hints_len = 0;

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
	 * as buttons finalize, so just stop tracking the windows here. */
	{
		GList *t;
		for (t = tasklist->tasks; t != NULL; t = t->next)
		{
			ToplevelTask *task = t->data;
			if (task->window)
				tasklist->backend->set_window_tracking (GTK_WIDGET (tasklist->outer_box),
									task->window, FALSE);
		}
	}

	if (tasklist->apps)
	{
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
		tasklist->backend->set_screen_tracking (GTK_WIDGET (tasklist->outer_box),
							tasklist->screen, FALSE);
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
tasklist_manager_new (const TasklistBackend *backend)
{
	TasklistManager *tasklist;
	GList *windows, *l;
	gpointer screen = backend->get_screen ();

	if (screen == NULL)
		return NULL;

	tasklist = g_new0 (TasklistManager, 1);
	tasklist->backend = backend;
	tasklist->screen = screen;
	tasklist->grouping_limit = default_grouping_limit;
	tasklist->last_button_space = -1;
	tasklist->icon_size_enum = GTK_ICON_SIZE_MENU;
	tasklist->icon_px = icon_size;
	tasklist->list = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous (GTK_BOX (tasklist->list), TRUE);
	tasklist->outer_box = g_object_new (TASKLIST_TYPE_BOX, NULL);
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

	tasklist->theme_changed_signal = g_signal_connect (gtk_settings_get_default (),
							  "notify::gtk-theme-name",
							  G_CALLBACK (tasklist_theme_changed), tasklist);

	/* add all existing windows on this screen */
	windows = backend->list_windows (screen);
	for (l = windows; l != NULL; l = l->next)
	{
		gpointer window = l->data;
		ToplevelTask *task = toplevel_task_new (tasklist, window);
		if (task != NULL)
			gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);
	}

	/* monitor window changes */
	backend->set_screen_tracking (GTK_WIDGET (tasklist->outer_box), screen, TRUE);

	return tasklist;
}

static void
toplevel_task_disconnected_from_widget (ToplevelTask *task)
{
	if (task->window)
	{
		task->tasklist->backend->set_window_tracking (GTK_WIDGET (task->tasklist->outer_box),
							      task->window, FALSE);
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
	gulong wid = task->tasklist->backend->get_window_id (task->window);
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
		icon = task->tasklist->backend->get_window_pixbuf (task->window,
								  task->tasklist->icon_px,
								  scale);
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
			task->tasklist->backend->set_window_minimized (task->window, TRUE);
		}
		else
		{
			/* A minimized window is normally activated in place, but
			 * that is invisible to the user if it lives on another
			 * workspace, so follow it there first. */
			if (task->minimized)
				tasklist_unminimize (task->tasklist, task->window);
			else
				task->tasklist->backend->activate_window (task->window, 0);
		}
	}
}

/* Hover notifications. The windows are gathered the way libwnck's
 * wnck_task_extract_windows() did: a plain task reports its own window, a
 * group reports every window it holds. The list is only valid for the
 * duration of the emission. */
static void
tasklist_emit_hover_notify (GtkWidget *button, TasklistManager *tasklist, guint signal_id)
{
	GroupTask *group;
	ToplevelTask *task;
	GList *windows = NULL;

	group = g_object_get_data (G_OBJECT (button), group_task_key);
	task = g_object_get_data (G_OBJECT (button), toplevel_task_key);

	if (group != NULL)
	{
		GList *l;

		for (l = group->members; l != NULL; l = l->next)
		{
			ToplevelTask *member = l->data;

			if (member != NULL && member->window != NULL)
				windows = g_list_prepend (windows, member->window);
		}
	}
	else if (task != NULL && task->window != NULL)
	{
		windows = g_list_prepend (windows, task->window);
	}

	windows = g_list_reverse (windows);

	/* The signals belong to the outer box, which is the widget the applet
	 * shell holds on to. */
	g_signal_emit (tasklist->outer_box, signal_id, 0, windows);

	g_list_free (windows);
}

static gboolean
on_toplevel_button_enter_notify (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist)
{
	tasklist_emit_hover_notify (button, tasklist, tasklist_box_signals[TASK_ENTER_NOTIFY]);

	return FALSE;
}

static gboolean
on_toplevel_button_leave_notify (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist)
{
	tasklist_emit_hover_notify (button, tasklist, tasklist_box_signals[TASK_LEAVE_NOTIFY]);

	return FALSE;
}

static gboolean on_toplevel_button_press (GtkWidget *button, GdkEvent *event, TasklistManager *tasklist)
{
	/* Assume event is a button press */

	if (((GdkEventButton*)event)->button == GDK_BUTTON_MIDDLE &&
	    tasklist->middle_click_close)
	{
		ToplevelTask *task = g_object_get_data (G_OBJECT (button), toplevel_task_key);
		if (task && task->window)
			task->tasklist->backend->close_window (task->window, ((GdkEventButton*)event)->time);
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
					tasklist->grouping != TASKLIST_NEVER_GROUP);

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
		if (child->window && !child->group && window_is_active (tasklist, child->window))
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
		tasklist->backend->activate_window (child->window, event->time);
	}

	return TRUE;
}

static ToplevelTask *
toplevel_task_new (TasklistManager *tasklist, gpointer window)
{
	const TasklistBackend *backend = tasklist->backend;
	ToplevelTask *task = g_new0 (ToplevelTask, 1);
	GtkOrientation orient;
	GtkWidget *whole_panel_box, *parent_box;
	int button_space, panel_width;

	if (window == NULL)
	{
		g_free (task);
		return NULL;
	}

	tasklist->buttons = tasklist->buttons + 1;
	orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (tasklist->outer_box));
	task->window = window;
	task->tasklist = tasklist;
	task->button = gtk_button_new ();
	g_signal_connect (task->button, "clicked", G_CALLBACK (toplevel_task_handle_clicked), task);

	task->icon = gtk_image_new_from_icon_name (backend->get_fallback_icon_name (),
						   GTK_ICON_SIZE_MENU);

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

	g_signal_connect (task->button, "enter-notify-event",
			  G_CALLBACK (on_toplevel_button_enter_notify),
			  tasklist);
	g_signal_connect (task->button, "leave-notify-event",
			  G_CALLBACK (on_toplevel_button_leave_notify),
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
	backend->set_window_tracking (GTK_WIDGET (tasklist->outer_box), window, TRUE);

	/* set initial contents */
	gtk_label_set_label (GTK_LABEL (task->label), backend->get_window_name (window));
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
		tasklist->tasklist_width = gtk_widget_get_allocated_width (parent_box);
	}
	else
	{
		tasklist->tasklist_width = MAX (gtk_widget_get_allocated_width (parent_box),
						tasklist->tasklist_width);
	}

	panel_width = gtk_widget_get_allocated_width (whole_panel_box);

	/*on startup we get an allocated with of zero, so start with 1/3 the panel width
	 *as a sane default
	 *This may overflow on very crowded panels where the tasklist is less than 1/3ed the
	 *panel witth but will self-correct on opening or closing a few windows
	 */

	if (tasklist->tasklist_width <= 2)
		tasklist->tasklist_width = panel_width / 3;

	/*Do not allow buttons to equal zero or the division below is a crasher*/
	tasklist->buttons = MAX (tasklist->buttons, 1);

	/*always allow at least three buttons to fit without adjustment
	 *so short window lists don't overflow
	 */
	if (tasklist->tasklist_width > 0)
	{
		tasklist->full_button_width = MIN (max_button_width, tasklist->tasklist_width / 3);
	}

	/*Leave a little space so the buttons don't push other applets off the panel*/
	button_space = (tasklist->tasklist_width / tasklist->buttons) * 0.75;
	button_space = MIN (button_space, tasklist->full_button_width);

	/* iterate over all the buttons*/
	adjust_buttons (tasklist, button_space, tasklist->buttons, task);

	/*Reset the tasklist width after button adjustments*/
	if (gtk_widget_get_allocated_width (parent_box) > 1)
	{
		tasklist->tasklist_width = gtk_widget_get_allocated_width (parent_box);
	}
	else
	{
		tasklist->tasklist_width = MAX (gtk_widget_get_allocated_width (parent_box),
						tasklist->tasklist_width);
	}
	return task;
}

GtkWidget *
tasklist_core_new (const TasklistBackend *backend)
{
	TasklistManager *tasklist;

	g_return_val_if_fail (backend != NULL, NULL);

	tasklist = tasklist_manager_new (backend);

	if (!tasklist)
		return NULL;

	return tasklist->outer_box;
}

static TasklistManager *
tasklist_widget_get_tasklist (GtkWidget *tasklist_widget)
{
	return g_object_get_data (G_OBJECT (tasklist_widget), tasklist_manager_key);
}

void
tasklist_core_set_orientation (GtkWidget *tasklist_widget, GtkOrientation orient)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (tasklist->list), orient);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (tasklist->outer_box), orient);
}

void
tasklist_core_set_middle_click_close (GtkWidget *tasklist_widget, gboolean enabled)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	tasklist->middle_click_close = enabled;
}

void
tasklist_core_set_scroll_enabled (GtkWidget *tasklist_widget, gboolean enabled)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	tasklist->scroll_enabled = enabled;
}

void
tasklist_core_set_include_all_workspaces (GtkWidget *tasklist_widget, gboolean include)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);

	if (tasklist->include_all_workspaces == include)
		return;

	tasklist->include_all_workspaces = include;
	tasklist_refresh_visibility (tasklist);
}

void
tasklist_core_set_switch_workspace_on_unminimize (GtkWidget *tasklist_widget, gboolean switch_ws)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);
	tasklist->switch_workspace_on_unminimize = switch_ws;
}

static void
tasklist_refresh_visibility (TasklistManager *tasklist)
{
	GList *l;

	for (l = tasklist->tasks; l != NULL; l = l->next)
		update_task_visibility (l->data);
}

/* Bring the user to the window's workspace before unminimizing it, so that
 * the window does not appear to vanish off-screen. */
static void
tasklist_unminimize (TasklistManager *tasklist, gpointer window)
{
	if (tasklist->switch_workspace_on_unminimize &&
	    !tasklist->backend->window_is_on_active_workspace (window))
		tasklist->backend->window_move_to_workspace (window);

	tasklist->backend->set_window_minimized (window, FALSE);
}

/* Returns a borrowed (max, min) staircase of widths, terminated by a 0 so
 * that the tasklist may always be squeezed down to nothing. The caller must
 * not modify or free it. *n_elements receives the number of integers, which
 * is always even while there is anything to show. */
const int *
tasklist_core_get_size_hint_list (GtkWidget *tasklist_widget, int *n_elements)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);

	g_return_val_if_fail (tasklist, NULL);
	g_return_val_if_fail (n_elements != NULL, NULL);

	*n_elements = tasklist->size_hints_len;

	return tasklist->size_hints;
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
		tasklist->backend->set_screen_tracking (GTK_WIDGET (tasklist->outer_box),
							tasklist->screen, FALSE);

	/* guard against re-entrant rebuilds: the show/hide of buttons during
	 * the rebuild triggers size-allocate, which would otherwise schedule
	 * another rebuild based on a half-updated grouping state */
	tasklist->rebuilding = TRUE;

	/* 1. Collect every group button so we can destroy them in one pass
	 *    below. The individual task buttons are kept alive: they are the
	 *    owners of the window handles the core still has to be able to
	 *    ask the backend about after the regroup. */
	if (tasklist->apps)
	{
		g_hash_table_iter_init (&iter, tasklist->apps);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			GroupTask *group = value;

			if (group == NULL)
				continue;

			if (group->app)
				tasklist->backend->set_app_tracking (GTK_WIDGET (tasklist->outer_box),
								    group->app, FALSE);

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
	tasklist->backend->set_screen_tracking (GTK_WIDGET (tasklist->outer_box),
						tasklist->screen, TRUE);

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
			tasklist->tasklist_width = MAX (gtk_widget_get_allocated_width (parent_box),
							tasklist->tasklist_width);
			button_space = (tasklist->tasklist_width / visible) * 0.75;
			button_space = MIN (button_space, tasklist->full_button_width);
			adjust_buttons (tasklist, button_space, visible, NULL);
		}
	}

	gtk_widget_queue_resize (tasklist->list);
}

static void
tasklist_refresh_icon_size (TasklistManager *tasklist)
{
	GtkIconSize chosen;
	gint available;

	if (!tasklist)
		return;

	/* Deliberately no early return on a non-positive available: the
	 * chooser treats that as "nothing fits" and drops to the smallest
	 * size, which is the right answer for a very thin panel. */
	available = tasklist_icon_available_space (tasklist);
	chosen = tasklist_choose_icon_size (available);

	if (chosen == tasklist->icon_size_enum)
		return;

	tasklist->icon_size_enum = chosen;
	tasklist_apply_icon_size (tasklist);
}

static gboolean
tasklist_theme_change_idle (gpointer data)
{
	TasklistManager *tasklist = data;

	tasklist->icon_refresh_idle = 0;
	tasklist_refresh_icon_size (tasklist);

	return G_SOURCE_REMOVE;
}

static void
tasklist_theme_changed (GtkSettings *settings, GParamSpec *pspec, TasklistManager *tasklist)
{
	if (!tasklist || tasklist->icon_refresh_idle != 0)
		return;

	tasklist->icon_refresh_idle = g_idle_add (tasklist_theme_change_idle, tasklist);
}

void
tasklist_core_set_panel_size (GtkWidget *tasklist_widget, gint size)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);

	g_return_if_fail (tasklist);

	if (size <= 0 || size == tasklist->panel_thickness)
		return;

	tasklist->panel_thickness = size;

	tasklist_refresh_icon_size (tasklist);
}

void
tasklist_core_set_grouping (GtkWidget *tasklist_widget, TasklistGroupingType grouping)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	g_return_if_fail (tasklist);

	if (tasklist->grouping == grouping)
		return;

	tasklist->grouping = grouping;

	/* when switching away from auto-grouping, stop the space-based
	 * regrouping so it doesn't fight with the new mode */
	if (grouping != TASKLIST_AUTO_GROUP)
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

/* Callbacks a backend makes when its own signals fire. */

void
tasklist_core_window_added (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	ToplevelTask *task;

	g_return_if_fail (tasklist);

	if (!window || tasklist_has_window (tasklist, window))
		return;

	task = toplevel_task_new (tasklist, window);
	if (task != NULL)
		gtk_box_pack_start (GTK_BOX (tasklist->list), task->button, TRUE, TRUE, 0);
}

void
tasklist_core_window_removed (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);

	g_return_if_fail (tasklist);

	if (window)
		tasklist_remove_window (tasklist, window);
}

void
tasklist_core_window_state_changed (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	GList *l;

	g_return_if_fail (tasklist);

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task != NULL && task->window == window)
		{
			update_task_state (task);
			return;
		}
	}
}

void
tasklist_core_window_name_changed (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	GList *l;

	g_return_if_fail (tasklist);

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task == NULL || task->window != window)
			continue;

		if (task->label)
			gtk_label_set_label (GTK_LABEL (task->label),
					     tasklist->backend->get_window_name (task->window));

		/* keep the group button (showing the active window's title) in sync */
		if (task->group)
			group_task_update_name (task->group);

		return;
	}
}

void
tasklist_core_window_icon_changed (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	GList *l;

	g_return_if_fail (tasklist);

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task != NULL && task->window == window)
		{
			update_task_icon (task);
			return;
		}
	}
}

void
tasklist_core_window_application_changed (GtkWidget *tasklist_widget, gpointer window)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	GList *l;

	g_return_if_fail (tasklist);

	for (l = tasklist->tasks; l != NULL; l = l->next)
	{
		ToplevelTask *task = l->data;

		if (task == NULL || task->window != window)
			continue;

		if (task->group)
			group_task_remove_window (task->group, task);

		if (tasklist->grouping != TASKLIST_NEVER_GROUP)
			tasklist_assign_to_group (tasklist, task);

		return;
	}
}

void
tasklist_core_app_icon_changed (GtkWidget *tasklist_widget, gpointer app)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);

	g_return_if_fail (tasklist);

	group_task_update_icon (tasklist->apps ? g_hash_table_lookup (tasklist->apps, app) : NULL);
}

void
tasklist_core_app_name_changed (GtkWidget *tasklist_widget, gpointer app)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);

	g_return_if_fail (tasklist);

	group_task_update_name (tasklist->apps ? g_hash_table_lookup (tasklist->apps, app) : NULL);
}

void
tasklist_core_active_window_changed (GtkWidget *tasklist_widget)
{
	TasklistManager *tasklist = tasklist_widget_get_tasklist (tasklist_widget);
	GList *l;

	g_return_if_fail (tasklist);

	for (l = tasklist->tasks; l != NULL; l = l->next)
		update_task_state (l->data);
}
