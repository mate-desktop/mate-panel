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

#include "xfw-pager.h"

#define PAGER_WORKSPACE_SIZE 16
#define PAGER_MIN_WINDOW_SIZE 3

typedef struct _GroupSignal GroupSignal;

struct _GroupSignal {
	XfwWorkspaceGroup *group;
	gulong             handlers[3]; /* active-changed, added, removed */
};

typedef struct _XfwPagerData XfwPagerData;

struct _XfwPagerData {
	GtkWidget *grid;
	GList *cells; /* list of XfwPagerCell* backing the workspace buttons */

	XfwScreen *screen;
	XfwWorkspaceManager *workspace_manager;
	GList *group_signals; /* list of GroupSignal* */
	GList *window_signals; /* list of tracked XfwWindow* */

	GtkOrientation orientation;
	int n_rows;
	gboolean display_all;
	gboolean display_names;
	gboolean ignore_toggle;
};

typedef struct _XfwPagerCell XfwPagerCell;
typedef struct _XfwPagerCellClass XfwPagerCellClass;

struct _XfwPagerCell {
	GtkDrawingArea parent;
	XfwWorkspace *ws;
	XfwPagerData *data;
};

struct _XfwPagerCellClass {
	GtkDrawingAreaClass parent_class;
};

typedef struct _XfwPagerGrid XfwPagerGrid;
typedef struct _XfwPagerGridClass XfwPagerGridClass;

struct _XfwPagerGrid {
	GtkGrid parent;
};

struct _XfwPagerGridClass {
	GtkGridClass parent_class;
};

static void rebuild_ui (XfwPagerData *data);

static XfwPagerData *pager_widget_get_data (GtkWidget *pager_widget);

GType xfw_pager_cell_get_type (void);
GType xfw_pager_grid_get_type (void);

/* ---- Helpers ---- */

static int
count_visible_workspaces (XfwPagerData *data)
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
get_nth_workspace (XfwPagerData *data, int index)
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

static XfwWorkspace *
pager_get_active_workspace (XfwPagerData *data)
{
	GList *workspaces, *l;

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);

	for (l = workspaces; l != NULL; l = l->next)
	{
		XfwWorkspace *ws = l->data;
		if (xfw_workspace_get_state (ws) & XFW_WORKSPACE_STATE_ACTIVE)
			return ws;
	}

	return NULL;
}

/* libwnck only ever drew normal, non-minimized, non-pager-skipping windows
 * on its pager buttons.  Desktop and dock windows are filter themselves out
 * via the skip-pager state, so no explicit type check is needed. */
static gboolean
pager_window_relevant (XfwWindow *window)
{
	XfwWindowState state = xfw_window_get_state (window);

	return (state & (XFW_WINDOW_STATE_MINIMIZED |
			 XFW_WINDOW_STATE_SKIP_PAGER)) == 0;
}

/* Resolve the workspace a window should be drawn on.  Pinned windows with no
 * workspace of their own are shown on the active workspace, matching libwnck. */
static XfwWorkspace *
window_get_pager_workspace (XfwPagerData *data, XfwWindow *window)
{
	XfwWorkspace *ws = xfw_window_get_workspace (window);

	if (ws == NULL && xfw_window_is_pinned (window))
		ws = pager_get_active_workspace (data);

	return ws;
}

static double
pager_workspace_aspect (XfwPagerData *data)
{
	XfwWorkspace *ws = get_nth_workspace (data, 0);

	if (ws != NULL)
	{
		GdkRectangle *geo = xfw_workspace_get_geometry (ws);

		if (geo->width > 0 && geo->height > 0)
			return (double) geo->width / (double) geo->height;
	}

	return 1.0;
}

static void
pager_get_layout (XfwPagerData *data, int *rows, int *cols)
{
	int n_spaces = count_visible_workspaces (data);

	if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		*rows = data->n_rows;
		*cols = (n_spaces + *rows - 1) / *rows;
	}
	else
	{
		*cols = data->n_rows;
		*rows = (n_spaces + *cols - 1) / *cols;
	}
}

/* ---- Mini-window drawing ---- */

static void
pager_draw_window_icon (cairo_t *cr, XfwWindow *window, const GdkRectangle *winrect)
{
	GIcon *gicon;
	GtkIconTheme *theme;
	GtkIconInfo *info;
	GdkPixbuf *pixbuf;
	int max_size, icon_w, icon_h, icon_x, icon_y;

	gicon = xfw_window_get_gicon (window);
	if (gicon == NULL)
		return;

	max_size = MIN (winrect->width - 2, winrect->height - 2);
	if (max_size <= 0)
		return;

	theme = gtk_icon_theme_get_default ();
	info = gtk_icon_theme_lookup_by_gicon (theme, gicon, max_size,
					       GTK_ICON_LOOKUP_FORCE_SIZE);
	if (info == NULL)
		return;

	pixbuf = gtk_icon_info_load_icon (info, NULL);
	g_object_unref (info);
	if (pixbuf == NULL)
		return;

	icon_w = gdk_pixbuf_get_width (pixbuf);
	icon_h = gdk_pixbuf_get_height (pixbuf);

	if (icon_w > winrect->width - 2 || icon_h > winrect->height - 2)
	{
		g_object_unref (pixbuf);
		return;
	}

	icon_x = winrect->x + (winrect->width - icon_w) / 2;
	icon_y = winrect->y + (winrect->height - icon_h) / 2;

	gdk_cairo_set_source_pixbuf (cr, pixbuf, icon_x, icon_y);
	cairo_paint (cr);

	g_object_unref (pixbuf);
}

static void
pager_draw_window (cairo_t *cr, GtkStyleContext *context, XfwWindow *window,
		   const GdkRectangle *winrect, gboolean workspace_active)
{
	GtkStateFlags state = workspace_active ? GTK_STATE_FLAG_SELECTED
					       : GTK_STATE_FLAG_NORMAL;
	GdkRGBA fg;
	int x = winrect->x, y = winrect->y;
	int w = MAX (0, winrect->width - 2);
	int h = MAX (0, winrect->height - 2);

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);

	gtk_render_background (context, cr, x + 1, y + 1, w, h);

	if (xfw_window_get_state (window) & XFW_WINDOW_STATE_ACTIVE)
	{
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.3);
		cairo_rectangle (cr, x + 1, y + 1, w, h);
		cairo_fill (cr);
	}

	pager_draw_window_icon (cr, window, winrect);

	gtk_render_frame (context, cr, x + 0.5, y + 0.5,
			  MAX (0, winrect->width - 1),
			  MAX (0, winrect->height - 1));

	gtk_style_context_get_color (context, state, &fg);
	gdk_cairo_set_source_rgba (cr, &fg);
	cairo_set_line_width (cr, 1.0);
	cairo_rectangle (cr, x + 0.5, y + 0.5,
			 MAX (0, winrect->width - 1),
			 MAX (0, winrect->height - 1));
	cairo_stroke (cr);

	gtk_style_context_restore (context);
}

static gboolean
xfw_pager_cell_draw (GtkWidget *widget, cairo_t *cr)
{
	XfwPagerCell *cell = (XfwPagerCell *) widget;
	XfwPagerData *data = cell->data;
	GtkStyleContext *context;
	GdkRectangle bounds, clipped;
	GdkRectangle *ws_geo;
	double wratio, hratio;
	gboolean is_active;
	GList *windows, *l;
	int width, height;

	if (data == NULL || cell->ws == NULL)
		return FALSE;

	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);
	if (width <= 0 || height <= 0)
		return FALSE;

	ws_geo = xfw_workspace_get_geometry (cell->ws);
	if (ws_geo->width <= 0 || ws_geo->height <= 0)
		return FALSE;

	wratio = (double) width / (double) ws_geo->width;
	hratio = (double) height / (double) ws_geo->height;

	is_active = (xfw_workspace_get_state (cell->ws) &
		     XFW_WORKSPACE_STATE_ACTIVE) != 0;
	context = gtk_widget_get_style_context (widget);

	bounds.x = 0;
	bounds.y = 0;
	bounds.width = width;
	bounds.height = height;

	/* Bottom to top, matching the stacked window order. */
	windows = xfw_screen_get_windows_stacked (data->screen);
	for (l = windows; l != NULL; l = l->next)
	{
		XfwWindow *window = l->data;
		GdkRectangle *win_geo;
		GdkRectangle winrect;

		if (!pager_window_relevant (window))
			continue;

		if (window_get_pager_workspace (data, window) != cell->ws)
			continue;

		win_geo = xfw_window_get_geometry (window);
		if (win_geo->width <= 0 || win_geo->height <= 0)
			continue;

		winrect.x = (int) ((win_geo->x - ws_geo->x) * wratio + 0.5);
		winrect.y = (int) ((win_geo->y - ws_geo->y) * hratio + 0.5);
		winrect.width = (int) (win_geo->width * wratio + 0.5);
		winrect.height = (int) (win_geo->height * hratio + 0.5);

		if (winrect.width < PAGER_MIN_WINDOW_SIZE)
			winrect.width = PAGER_MIN_WINDOW_SIZE;
		if (winrect.height < PAGER_MIN_WINDOW_SIZE)
			winrect.height = PAGER_MIN_WINDOW_SIZE;

		if (!gdk_rectangle_intersect (&bounds, &winrect, &clipped))
			continue;

		pager_draw_window (cr, context, window, &clipped, is_active);
	}

	return FALSE;
}

G_DEFINE_TYPE (XfwPagerCell, xfw_pager_cell, GTK_TYPE_DRAWING_AREA)

static void
xfw_pager_cell_class_init (XfwPagerCellClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->draw = xfw_pager_cell_draw;
}

static void
xfw_pager_cell_init (XfwPagerCell *cell)
{
	cell->ws = NULL;
	cell->data = NULL;
}

static GtkWidget *
xfw_pager_cell_new (XfwWorkspace *ws, XfwPagerData *data)
{
	XfwPagerCell *cell = g_object_new (xfw_pager_cell_get_type (), NULL);

	cell->ws = ws;
	cell->data = data;

	gtk_widget_set_hexpand (GTK_WIDGET (cell), TRUE);
	gtk_widget_set_vexpand (GTK_WIDGET (cell), TRUE);

	return GTK_WIDGET (cell);
}

/* ---- Aspect-correct pager grid ---- */

G_DEFINE_TYPE (XfwPagerGrid, xfw_pager_grid, GTK_TYPE_GRID)

/* The grid keeps each workspace button at the aspect ratio of the workspace
 * itself, following libwnck's WnckPager.  The grid's preferred size is
 * derived from the workspace geometry rather than the buttons, so the panel
 * can give the pager a size that preserves the aspect ratio. */
static void
xfw_pager_grid_get_preferred_width (GtkWidget *widget, int *minimum, int *natural)
{
	XfwPagerData *data = pager_widget_get_data (widget);
	int rows, cols, cell_w;

	if (data == NULL || data->display_names)
	{
		GTK_WIDGET_CLASS (xfw_pager_grid_parent_class)->get_preferred_width (widget, minimum, natural);
		return;
	}

	pager_get_layout (data, &rows, &cols);
	(void) rows;
	cell_w = MAX (1, (int) (pager_workspace_aspect (data) * PAGER_WORKSPACE_SIZE + 0.5));
	*minimum = *natural = cell_w * cols;
}

static void
xfw_pager_grid_get_preferred_height (GtkWidget *widget, int *minimum, int *natural)
{
	XfwPagerData *data = pager_widget_get_data (widget);
	int rows, cols, cell_h;
	double aspect;

	if (data == NULL || data->display_names)
	{
		GTK_WIDGET_CLASS (xfw_pager_grid_parent_class)->get_preferred_height (widget, minimum, natural);
		return;
	}

	pager_get_layout (data, &rows, &cols);
	(void) cols;
	aspect = pager_workspace_aspect (data);
	cell_h = MAX (1, (int) (PAGER_WORKSPACE_SIZE / aspect + 0.5));
	*minimum = *natural = cell_h * rows;
}

static void
xfw_pager_grid_get_preferred_width_for_height (GtkWidget *widget, int height,
					       int *minimum, int *natural)
{
	XfwPagerData *data = pager_widget_get_data (widget);
	int rows, cols, cell_h, cell_w;

	if (data == NULL || data->display_names)
	{
		GTK_WIDGET_CLASS (xfw_pager_grid_parent_class)->get_preferred_width_for_height (widget, height, minimum, natural);
		return;
	}

	pager_get_layout (data, &rows, &cols);
	cell_h = MAX (1, height / rows);
	cell_w = MAX (1, (int) (pager_workspace_aspect (data) * cell_h + 0.5));
	*minimum = *natural = cell_w * cols;
}

static void
xfw_pager_grid_get_preferred_height_for_width (GtkWidget *widget, int width,
					       int *minimum, int *natural)
{
	XfwPagerData *data = pager_widget_get_data (widget);
	int rows, cols, cell_w, cell_h;
	double aspect;

	if (data == NULL || data->display_names)
	{
		GTK_WIDGET_CLASS (xfw_pager_grid_parent_class)->get_preferred_height_for_width (widget, width, minimum, natural);
		return;
	}

	pager_get_layout (data, &rows, &cols);
	aspect = pager_workspace_aspect (data);
	cell_w = MAX (1, width / cols);
	cell_h = MAX (1, (int) (cell_w / aspect + 0.5));
	*minimum = *natural = cell_h * rows;
}

static GtkSizeRequestMode
xfw_pager_grid_get_request_mode (GtkWidget *widget)
{
	XfwPagerData *data = pager_widget_get_data (widget);

	if (data == NULL || data->display_names)
		return GTK_WIDGET_CLASS (xfw_pager_grid_parent_class)->get_request_mode (widget);

	if (data->orientation == GTK_ORIENTATION_VERTICAL)
		return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
	else
		return GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT;
}

static void
xfw_pager_grid_class_init (XfwPagerGridClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->get_request_mode = xfw_pager_grid_get_request_mode;
	widget_class->get_preferred_width = xfw_pager_grid_get_preferred_width;
	widget_class->get_preferred_height = xfw_pager_grid_get_preferred_height;
	widget_class->get_preferred_width_for_height = xfw_pager_grid_get_preferred_width_for_height;
	widget_class->get_preferred_height_for_width = xfw_pager_grid_get_preferred_height_for_width;
}

static void
xfw_pager_grid_init (XfwPagerGrid *grid)
{
}

static GtkWidget *
xfw_pager_grid_new (void)
{
	return g_object_new (xfw_pager_grid_get_type (), NULL);
}

/* ---- Build / rebuild the button grid ---- */

static void
on_button_toggled (GtkToggleButton *button, XfwPagerData *data)
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
rebuild_ui (XfwPagerData *data)
{
	GList *children, *l;
	int n_spaces, i, col, row;
	int rows, cols;

	/* Destroy old buttons */
	children = gtk_container_get_children (GTK_CONTAINER (data->grid));
	for (l = children; l != NULL; l = l->next)
		gtk_widget_destroy (GTK_WIDGET (l->data));
	g_list_free (children);

	g_list_free (data->cells);
	data->cells = NULL;

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
		GtkWidget *button;
		XfwWorkspaceState ws_state;

		ws = get_nth_workspace (data, i);

		button = gtk_toggle_button_new ();

		if (data->display_names)
		{
			const char *name;

			if (ws)
			{
				name = xfw_workspace_get_name (ws);
				if (!name || name[0] == '\0')
					name = xfw_workspace_get_id (ws);
				if (!name)
					name = "";
			}
			else
			{
				name = "";
			}

			gtk_container_add (GTK_CONTAINER (button), gtk_label_new (name));
		}
		else
		{
			GtkWidget *cell = xfw_pager_cell_new (ws, data);

			gtk_container_add (GTK_CONTAINER (button), cell);
			data->cells = g_list_append (data->cells, cell);
		}

		gtk_widget_set_hexpand (button, TRUE);
		gtk_widget_set_vexpand (button, TRUE);
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
			     XfwPagerData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_added (XfwWorkspaceGroup *group,
		    XfwWorkspace      *workspace,
		    XfwPagerData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_removed (XfwWorkspaceGroup *group,
		      XfwWorkspace      *workspace,
		      XfwPagerData *data)
{
	rebuild_ui (data);
}

/* ---- Group signal management ---- */

static void
disconnect_group_signals (XfwPagerData *data)
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
connect_group_signals (XfwPagerData *data)
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
			    XfwPagerData *data)
{
	connect_group_signals (data);
	rebuild_ui (data);
}

static void
on_workspace_group_destroyed (XfwWorkspaceManager *manager,
			      XfwWorkspaceGroup   *group,
			      XfwPagerData *data)
{
	connect_group_signals (data);
	rebuild_ui (data);
}

static void
on_workspace_created (XfwWorkspaceManager *manager,
		      XfwWorkspace        *workspace,
		      XfwPagerData *data)
{
	rebuild_ui (data);
}

static void
on_workspace_destroyed (XfwWorkspaceManager *manager,
			XfwWorkspace        *workspace,
			XfwPagerData *data)
{
	rebuild_ui (data);
}

/* ---- Window signal management ---- */

static void
pager_queue_redraw (XfwPagerData *data)
{
	for (GList *l = data->cells; l != NULL; l = l->next)
		gtk_widget_queue_draw (GTK_WIDGET (l->data));
}

static void
window_refresh (XfwWindow *window, XfwPagerData *data)
{
	pager_queue_redraw (data);
}

static void
window_state_changed (XfwWindow     *window,
		      XfwWindowState changed_mask,
		      XfwWindowState new_state,
		      XfwPagerData  *data)
{
	pager_queue_redraw (data);
}

static void
connect_window_signals (XfwPagerData *data, XfwWindow *window)
{
	g_signal_connect (window, "state-changed",
			  G_CALLBACK (window_state_changed), data);
	g_signal_connect (window, "geometry-changed",
			  G_CALLBACK (window_refresh), data);
	g_signal_connect (window, "icon-changed",
			  G_CALLBACK (window_refresh), data);

	data->window_signals = g_list_prepend (data->window_signals, window);
}

static void
disconnect_window_signals (XfwPagerData *data)
{
	for (GList *l = data->window_signals; l != NULL; l = l->next)
	{
		XfwWindow *window = l->data;

		g_signal_handlers_disconnect_by_func (window,
						       G_CALLBACK (window_state_changed),
						       data);
		g_signal_handlers_disconnect_by_func (window,
						       G_CALLBACK (window_refresh),
						       data);
	}

	g_list_free (data->window_signals);
	data->window_signals = NULL;
}

static void
on_screen_window_opened (XfwScreen *screen,
			 XfwWindow  *window,
			 XfwPagerData *data)
{
	if (pager_window_relevant (window))
		connect_window_signals (data, window);

	pager_queue_redraw (data);
}

static void
on_screen_window_closed (XfwScreen *screen,
			 XfwWindow  *window,
			 XfwPagerData *data)
{
	data->window_signals = g_list_remove (data->window_signals, window);
	pager_queue_redraw (data);
}

static void
on_screen_active_window_changed (XfwScreen *screen,
				 XfwWindow *previous,
				 XfwPagerData *data)
{
	pager_queue_redraw (data);
}

/* ---- Cleanup ---- */

static void
disconnect_screen_signals (XfwPagerData *data)
{
	if (data->workspace_manager)
		g_signal_handlers_disconnect_by_data (data->workspace_manager, data);
	if (data->screen)
		g_signal_handlers_disconnect_by_data (data->screen, data);
}

static void
xfw_pager_data_free (XfwPagerData *data)
{
	disconnect_screen_signals (data);
	disconnect_group_signals (data);
	disconnect_window_signals (data);
	g_list_free (data->cells);
	g_free (data);
}

/* ---- Public API ---- */

GtkWidget *
xfw_pager_new (void)
{
	XfwPagerData *data;

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	data = g_new0 (XfwPagerData, 1);
	data->orientation = GTK_ORIENTATION_HORIZONTAL;
	data->n_rows = 1;
	data->display_all = TRUE;
	data->display_names = FALSE;
	data->ignore_toggle = FALSE;

	data->grid = xfw_pager_grid_new ();

	GtkStyleContext *context = gtk_widget_get_style_context (data->grid);
	gtk_style_context_add_class (context, "wnck-pager");

	g_object_set_data_full (G_OBJECT (data->grid),
				"xfw_pager_data",
				data,
				(GDestroyNotify) xfw_pager_data_free);

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

	g_signal_connect (data->screen, "window-opened",
			  G_CALLBACK (on_screen_window_opened), data);
	g_signal_connect (data->screen, "window-closed",
			  G_CALLBACK (on_screen_window_closed), data);
	g_signal_connect (data->screen, "active-window-changed",
			  G_CALLBACK (on_screen_active_window_changed), data);

	for (GList *l = xfw_screen_get_windows (data->screen);
	     l != NULL; l = l->next)
	{
		if (pager_window_relevant (l->data))
			connect_window_signals (data, l->data);
	}

	connect_group_signals (data);
	rebuild_ui (data);

	return data->grid;
}

static XfwPagerData *
pager_widget_get_data (GtkWidget *pager_widget)
{
	return g_object_get_data (G_OBJECT (pager_widget), "xfw_pager_data");
}

void
xfw_pager_set_orientation (GtkWidget *pager_widget, GtkOrientation orientation)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->orientation == orientation)
		return;

	data->orientation = orientation;
	rebuild_ui (data);
}

void
xfw_pager_set_rows (GtkWidget *pager_widget, int n_rows)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	n_rows = CLAMP (n_rows, 1, 16);

	if (data->n_rows == n_rows)
		return;

	data->n_rows = n_rows;
	rebuild_ui (data);
}

void
xfw_pager_set_show_all (GtkWidget *pager_widget, gboolean show_all)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->display_all == show_all)
		return;

	data->display_all = show_all;
	rebuild_ui (data);
}

void
xfw_pager_set_show_names (GtkWidget *pager_widget, gboolean show_names)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
	g_return_if_fail (data);

	if (data->display_names == show_names)
		return;

	data->display_names = show_names;
	rebuild_ui (data);
}

int
xfw_pager_get_count (GtkWidget *pager_widget)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);

	g_return_val_if_fail (data, 0);

	return g_list_length (xfw_workspace_manager_list_workspaces (data->workspace_manager));
}

/* Names are indexed against the full workspace list, like
 * xfw_pager_get_count(), rather than the subset of workspaces the pager
 * buttons are built from.  This is what a workspace inventory needs when the
 * "show all workspaces" option is off and some workspaces are hidden. */
const char *
xfw_pager_get_name (GtkWidget *pager_widget, int index)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
	XfwWorkspace *ws;
	const char *name;
	GList *workspaces;
	int i = 0;

	g_return_val_if_fail (data, "workspace");

	workspaces = xfw_workspace_manager_list_workspaces (data->workspace_manager);

	for (; workspaces != NULL; workspaces = workspaces->next, i++)
	{
		if (i != index)
			continue;

		ws = workspaces->data;
		name = xfw_workspace_get_name (ws);
		if (name && name[0] != '\0')
			return name;

		return xfw_workspace_get_id (ws);
	}

	return "workspace";
}

int
xfw_pager_get_active_index (GtkWidget *pager_widget)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
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
xfw_pager_activate_nth (GtkWidget *pager_widget, int index)
{
	XfwPagerData *data = pager_widget_get_data (pager_widget);
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
