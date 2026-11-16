/*
 * wayland-backend.c: Support for running on Wayland compositors
 *
 * Copyright (C) 2019 William Wold
 * Copyright (C) 2019-2026 MATE Developers
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
 *
 * Authors:
 *     William Wold <wm@wmww.sh>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

#include "wayland-backend.h"
#include "panel-multimonitor.h"

static void
wayland_panel_toplevel_apply_anchors (PanelToplevel*   toplevel,
				      PanelOrientation orientation,
				      gboolean         expand)
{
	GtkWindow* window;
	gboolean anchor[GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER];

	window = GTK_WINDOW (toplevel);

	for (int i = 0; i < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; i++)
		anchor[i] = expand;

	switch (orientation) {
	case PANEL_ORIENTATION_LEFT:
		anchor[GTK_LAYER_SHELL_EDGE_LEFT] = TRUE;
		anchor[GTK_LAYER_SHELL_EDGE_RIGHT] = FALSE;
		break;
	case PANEL_ORIENTATION_RIGHT:
		anchor[GTK_LAYER_SHELL_EDGE_RIGHT] = TRUE;
		anchor[GTK_LAYER_SHELL_EDGE_LEFT] = FALSE;
		break;
	case PANEL_ORIENTATION_TOP:
		anchor[GTK_LAYER_SHELL_EDGE_TOP] = TRUE;
		anchor[GTK_LAYER_SHELL_EDGE_BOTTOM] = FALSE;
		break;
	case PANEL_ORIENTATION_BOTTOM:
		anchor[GTK_LAYER_SHELL_EDGE_BOTTOM] = TRUE;
		anchor[GTK_LAYER_SHELL_EDGE_TOP] = FALSE;
		break;
	default:
		g_warning ("Invalid panel orientation %d", orientation);
	}

	for (int i = 0; i < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; i++)
		gtk_layer_set_anchor (window, i, anchor[i]);
}

/* size-allocate handler restoring the real anchors after the size change */
static void
wayland_panel_toplevel_set_anchor (GtkWidget      *widget,
				   GtkAllocation  *alloc,
				   gpointer        data)
{
	PanelToplevel* toplevel = data;

	gulong id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (toplevel),
							 "wayland-set-anchor-id"));
	if (id != 0)
		g_signal_handler_disconnect (G_OBJECT (toplevel), id);
	g_object_set_data (G_OBJECT (toplevel), "wayland-set-anchor-id", NULL);

	wayland_panel_toplevel_apply_anchors (toplevel,
		panel_toplevel_get_orientation (toplevel),
		panel_toplevel_get_expand (toplevel));
}

/* idle handler switching to the two-anchor {top,left} corner state */
static void
wayland_panel_toplevel_set_anchor_default (gpointer data)
{
	PanelToplevel* toplevel = data;
	GtkWindow* window;
	GtkWidget *widget;

	if (g_object_get_data (G_OBJECT (toplevel), "wayland-set-anchor-id") == NULL) {
		window = GTK_WINDOW (toplevel);
		gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
		gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
		gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
		gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);

		widget = GTK_WIDGET (toplevel);
		gulong id = g_signal_connect (widget, "size-allocate",
					      G_CALLBACK (wayland_panel_toplevel_set_anchor),
					      toplevel);
		g_object_set_data (G_OBJECT (toplevel), "wayland-set-anchor-id",
				   GSIZE_TO_POINTER (id));
	}

	g_object_set_data (G_OBJECT (toplevel), "wayland-set-anchor-default-id", NULL);
}

/* schedule the corner state switch; must be called before a size change */
static void
wayland_panel_toplevel_queue_anchor_default (PanelToplevel* toplevel)
{
	guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (toplevel),
							"wayland-set-anchor-default-id"));

	if (id != 0)
		g_source_remove (id);

	id = g_idle_add ((GSourceFunc) wayland_panel_toplevel_set_anchor_default, toplevel);
	g_object_set_data (G_OBJECT (toplevel), "wayland-set-anchor-default-id",
			   GUINT_TO_POINTER (id));
}

static gboolean
wayland_panel_toplevel_placement_changed (PanelToplevel*   toplevel,
					  PanelOrientation orientation,
					  gboolean         expand)
{
	gpointer prev = g_object_get_data (G_OBJECT (toplevel),
					   "wayland-panel-placement");
	PanelOrientation prev_orientation =
		prev != NULL ? (PanelOrientation) GPOINTER_TO_INT (prev) : orientation;
	gboolean prev_expand = g_object_get_data (G_OBJECT (toplevel),
						  "wayland-panel-expand") != NULL;
	gboolean changed = prev != NULL &&
		(prev_orientation != orientation || prev_expand != expand);

	g_object_set_data (G_OBJECT (toplevel), "wayland-panel-placement",
			   GINT_TO_POINTER (orientation));
	g_object_set_data (G_OBJECT (toplevel), "wayland-panel-expand",
			   expand ? GINT_TO_POINTER (1) : NULL);

	return changed;
}

void
wayland_panel_toplevel_update_placement (PanelToplevel* toplevel)
{
	gboolean expand;
	PanelOrientation orientation;
	GdkMonitor *monitor;

	monitor = panel_multimonitor_get_gdk_monitor (panel_toplevel_get_monitor (toplevel));
	if (monitor != gtk_layer_get_monitor (GTK_WINDOW (toplevel)))
		gtk_layer_set_monitor (GTK_WINDOW (toplevel), monitor);

	orientation = panel_toplevel_get_orientation (toplevel);
	expand = panel_toplevel_get_expand (toplevel);

	if (wayland_panel_toplevel_placement_changed (toplevel, orientation, expand)) {
		/* the panel size is going to change: let it resize in the
		 * two-anchor corner state first */
		g_object_set_data (G_OBJECT (toplevel), "wayland-placement-orient",
				   GINT_TO_POINTER (orientation));
		if (expand)
			g_object_set_data (G_OBJECT (toplevel), "wayland-placement-expand",
					   GINT_TO_POINTER (1));
		else
			g_object_set_data (G_OBJECT (toplevel), "wayland-placement-expand", NULL);

		wayland_panel_toplevel_queue_anchor_default (toplevel);
		return;
	}

	wayland_panel_toplevel_apply_anchors (toplevel, orientation, expand);
}

static void
wayland_panel_toplevel_monitor_removed (GdkDisplay    *display,
					GdkMonitor    *monitor,
					PanelToplevel *toplevel)
{
	/* The compositor closes a layer surface whose output goes away, and
	 * a closed surface gets no frame callbacks, so GTK would never lay
	 * the panel out again: remap it at once. */
	if (gtk_layer_get_monitor (GTK_WINDOW (toplevel)) == monitor)
		gtk_layer_set_monitor (GTK_WINDOW (toplevel), NULL);
}

void
wayland_panel_toplevel_init (PanelToplevel* toplevel)
{
	GtkWindow* window;

	window = GTK_WINDOW (toplevel);
	gtk_layer_init_for_window (window);
	gtk_layer_set_layer (window, GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_namespace (window, "panel");
	g_signal_connect_object (gdk_display_get_default (), "monitor-removed",
				 G_CALLBACK (wayland_panel_toplevel_monitor_removed), toplevel, 0);
	wayland_panel_toplevel_update_placement (toplevel);
}

void
wayland_panel_toplevel_move_resize (PanelToplevel           *toplevel,
                                    const GdkRectangle      *geometry,
                                    const GdkRectangle      *monitor_geom)
{
	GtkWindow* window;
	gint margin[GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER];

	window = GTK_WINDOW (toplevel);

	margin[GTK_LAYER_SHELL_EDGE_LEFT] = geometry->x - monitor_geom->x;
	margin[GTK_LAYER_SHELL_EDGE_TOP]  = geometry->y - monitor_geom->y;
	margin[GTK_LAYER_SHELL_EDGE_RIGHT] =
		monitor_geom->x + monitor_geom->width - (geometry->x + geometry->width);
	margin[GTK_LAYER_SHELL_EDGE_BOTTOM] =
		monitor_geom->y + monitor_geom->height - (geometry->y + geometry->height);

	/*
	 * gtk-layer-shell driven layer surfaces never resize the underlying
	 * GDK window themselves; if the widget was reallocated to a different
	 * size (e.g. after an orientation change) the compositor keeps showing
	 * the stale surface footprint. Force the GDK window to the geometry
	 * size and invalidate it, so a correctly sized buffer is committed.
	 */
	{
		GdkWindow *gdk = gtk_widget_get_window (GTK_WIDGET (window));
		if (gdk && (gdk_window_get_width (gdk) != geometry->width ||
			    gdk_window_get_height (gdk) != geometry->height)) {
			gdk_window_resize (gdk, geometry->width, geometry->height);
			gdk_window_invalidate_rect (gdk, NULL, FALSE);
		}
	}

	for (int i = 0; i < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; i++)
		gtk_layer_set_margin (window, i, margin[i]);
}

void
wayland_panel_toplevel_update_exclusive_zone (PanelToplevel* toplevel,
                                              gint           exclusive_zone)
{
	gtk_layer_set_exclusive_zone (GTK_WINDOW (toplevel), exclusive_zone);
}

/* The autohide drop-zone is only an input surface: clear the surface so the
 * underlying panel strip stays visible. */
static gboolean
wayland_panel_toplevel_autohide_window_draw (GtkWidget *widget,
					     cairo_t   *cr)
{
	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);
	return FALSE;
}

GtkWindow *
wayland_panel_toplevel_create_autohide_window (PanelToplevel *toplevel)
{
	GtkWindow *window;
	GdkScreen *screen;

	window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));

	gtk_widget_set_name (GTK_WIDGET (window), "MatePanelWindowHidden");
	gtk_window_set_title (window, "MatePanelWindowHidden");
	gtk_window_set_decorated (window, FALSE);
	gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_skip_taskbar_hint (window, TRUE);
	gtk_window_set_skip_pager_hint (window, TRUE);
	gtk_window_set_accept_focus (window, FALSE);
	gtk_window_set_focus_on_map (window, FALSE);
	gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

	/* grab crossing events on this surface; they drive the autohide
	 * state machine independently of the (animated) panel surface */
	gtk_widget_add_events (GTK_WIDGET (window),
			       GDK_ENTER_NOTIFY_MASK |
			       GDK_LEAVE_NOTIFY_MASK |
			       GDK_POINTER_MOTION_MASK);

	/* visually transparent: this window is only an input area */
	screen = gtk_widget_get_screen (GTK_WIDGET (window));
	if (gdk_screen_get_rgba_visual (screen))
		gtk_widget_set_visual (GTK_WIDGET (window),
				       gdk_screen_get_rgba_visual (screen));

	gtk_layer_init_for_window (window);
	gtk_layer_set_layer (window, GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_namespace (window, "panel");
	gtk_layer_set_exclusive_zone (window, -1);
	gtk_layer_set_keyboard_interactivity (window, FALSE);

	/* place it with the {left, top} corner margins, like the panel */
	gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
	gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);

	g_signal_connect (window, "draw",
			  G_CALLBACK (wayland_panel_toplevel_autohide_window_draw),
			  NULL);

	return window;
}

void
wayland_panel_toplevel_autohide_window_show (GtkWindow          *window,
					     PanelToplevel      *toplevel,
					     const GdkRectangle *geometry,
					     const GdkRectangle *monitor_geom)
{
	GdkDisplay *display;
	int         monitor;
	int         n_monitors;

	display = gdk_display_get_default ();
	n_monitors = gdk_display_get_n_monitors (display);
	monitor = panel_toplevel_get_monitor (toplevel);
	if (monitor < 0 || monitor >= n_monitors)
		monitor = 0;

	gtk_layer_set_monitor (window, gdk_display_get_monitor (display, monitor));

	gtk_widget_set_size_request (GTK_WIDGET (window),
				     geometry->width, geometry->height);
	gtk_window_resize (window, geometry->width, geometry->height);

	gtk_layer_set_margin (window, GTK_LAYER_SHELL_EDGE_LEFT,
			      geometry->x - monitor_geom->x);
	gtk_layer_set_margin (window, GTK_LAYER_SHELL_EDGE_TOP,
			      geometry->y - monitor_geom->y);

	if (!gtk_widget_get_visible (GTK_WIDGET (window)))
		gtk_widget_show (GTK_WIDGET (window));
}

void
wayland_panel_toplevel_autohide_window_hide (GtkWindow *window)
{
	if (gtk_widget_get_visible (GTK_WIDGET (window)))
		gtk_widget_hide (GTK_WIDGET (window));
}
