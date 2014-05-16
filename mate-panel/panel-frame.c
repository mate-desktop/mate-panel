/*
 * panel-frame.c: A frame which only draws certain edges.
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "panel-frame.h"

#include "panel-typebuiltins.h"

G_DEFINE_TYPE (PanelFrame, panel_frame, GTK_TYPE_BIN)

enum {
	PROP_0,
	PROP_EDGES
};

static void
panel_frame_size_request (GtkWidget      *widget,
			  GtkRequisition *requisition)
{
	PanelFrame *frame = (PanelFrame *) widget;
	GtkBin     *bin   = (GtkBin *) widget;
	GtkStyle   *style;
	GtkWidget  *child;
	int         border_width;

	style = gtk_widget_get_style (widget);
	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	requisition->width = 1;
	requisition->height = 1;

	child = gtk_bin_get_child (bin);
	if (child && gtk_widget_get_visible (child))
#if GTK_CHECK_VERSION (3, 0, 0)
		gtk_widget_get_preferred_size (child, requisition, NULL);
#else
		gtk_widget_size_request (child, requisition);
#endif

	requisition->width  += border_width;
	requisition->height += border_width;

	if (frame->edges & PANEL_EDGE_TOP)
		requisition->height += style->xthickness;
	if (frame->edges & PANEL_EDGE_BOTTOM)
		requisition->height += style->xthickness;
	if (frame->edges & PANEL_EDGE_LEFT)
		requisition->width += style->ythickness;
	if (frame->edges & PANEL_EDGE_RIGHT)
		requisition->width += style->ythickness;
}

#if GTK_CHECK_VERSION (3, 0, 0)
static void
panel_frame_get_preferred_width (GtkWidget *widget,
								    gint *minimum_width,
								    gint *natural_width)
{
	GtkRequisition req;
	panel_frame_size_request (widget, &req);
	*minimum_width = *natural_width = req.width;
}

static void
panel_frame_get_preferred_height (GtkWidget *widget,
									 gint *minimum_height,
									 gint *natural_height)
{
	GtkRequisition req;
	panel_frame_size_request (widget, &req);
	*minimum_height = *natural_height = req.height;
}
#endif

static void
panel_frame_size_allocate (GtkWidget     *widget,
			   GtkAllocation *allocation)
{
	PanelFrame    *frame = (PanelFrame *) widget;
	GtkBin        *bin   = (GtkBin *) widget;
	GtkStyle      *style;
	GtkAllocation  child_allocation;
	GtkAllocation  child_allocation_current;
	GtkWidget     *child;
	int            border_width;

	gtk_widget_set_allocation (widget, allocation);

	style = gtk_widget_get_style (widget);
	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	child_allocation.x      = allocation->x + border_width;
	child_allocation.y      = allocation->y + border_width;
	child_allocation.width  = allocation->width  - 2 * border_width;
	child_allocation.height = allocation->height - 2 * border_width;

	if (frame->edges & PANEL_EDGE_LEFT) {
		child_allocation.x     += style->xthickness;
		child_allocation.width -= style->xthickness;
	}

	if (frame->edges & PANEL_EDGE_TOP) {
		child_allocation.y      += style->ythickness;
		child_allocation.height -= style->ythickness;
	}

	if (frame->edges & PANEL_EDGE_RIGHT)
		child_allocation.width -= style->xthickness;

	if (frame->edges & PANEL_EDGE_BOTTOM)
		child_allocation.height -= style->ythickness;

	child = gtk_bin_get_child (bin);
	gtk_widget_get_allocation (child, &child_allocation_current);

	if (gtk_widget_get_mapped (widget) &&
	    (child_allocation.x != child_allocation_current.x ||
	     child_allocation.y != child_allocation_current.y ||
	     child_allocation.width  != child_allocation_current.width ||
	     child_allocation.height != child_allocation_current.height))
		gdk_window_invalidate_rect (gtk_widget_get_window (widget), allocation, FALSE);

	if (child && gtk_widget_get_visible (child))
		gtk_widget_size_allocate (child, &child_allocation);
}

void
panel_frame_draw (GtkWidget      *widget,
#if GTK_CHECK_VERSION (3, 0, 0)
		  cairo_t *cr,
#endif
		  PanelFrameEdge  edges)
{
#if !GTK_CHECK_VERSION (3, 0, 0)
	GdkWindow     *window;
#endif
	GtkStyle      *style;
	GtkStateType   state;
#if GTK_CHECK_VERSION (3, 0, 0)
	GdkColor      *dark, *light, *black;
#else
	GtkAllocation  allocation;
	GdkGC         *dark, *light, *black;
#endif
	int            x, y, width, height;
	int            xthickness, ythickness;

	if (edges == PANEL_EDGE_NONE)
		return;

#if !GTK_CHECK_VERSION (3, 0, 0)
	window = gtk_widget_get_window (widget);
#endif
	style = gtk_widget_get_style (widget);
	state = gtk_widget_get_state (widget);
#if GTK_CHECK_VERSION (3, 0, 0)
	x = 0;
	y = 0;
	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);
#else
	gtk_widget_get_allocation (widget, &allocation);
	x      = allocation.x;
	y      = allocation.y;
	width  = allocation.width;
	height = allocation.height;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
	dark = &style->dark [state];
	light = &style->light [state];
	black = &style->black;
#else
	dark  = style->dark_gc [state];
	light = style->light_gc [state];
	black = style->black_gc;
#endif

	xthickness = style->xthickness;
	ythickness = style->ythickness;

	/* Copied from gtk_default_draw_shadow() */

#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_set_line_width (cr, 1);

	if (edges & PANEL_EDGE_BOTTOM && ythickness > 0) {
		if (ythickness > 1) {
			gdk_cairo_set_source_color (cr, dark);
			cairo_move_to (cr, x + .5, y + height - 2 + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 2 + .5);
			cairo_stroke (cr);

			gdk_cairo_set_source_color (cr, black);
			cairo_move_to (cr, x + .5, y + height - 1 - .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		} else {
			gdk_cairo_set_source_color (cr, dark);
			cairo_move_to (cr, x + .5, y + height - 1 - .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}

	if (edges & PANEL_EDGE_RIGHT && xthickness > 0) {
		if (xthickness > 1) {
			gdk_cairo_set_source_color (cr, dark);
			cairo_move_to (cr, x + width - 2 - .5, y + .5);
			cairo_line_to (cr, x + width - 2 - .5, y + height - 1 - .5);
			cairo_stroke (cr);

			gdk_cairo_set_source_color (cr, black);
			cairo_move_to (cr, x + width - 1 - .5, y + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		} else {
			gdk_cairo_set_source_color (cr, dark);
			cairo_move_to (cr, x + width - 1 - .5, y + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}

	if (edges & PANEL_EDGE_TOP && ythickness > 0) {
		gdk_cairo_set_source_color (cr, light);
		cairo_move_to (cr, x + .5, y + .5);
		cairo_line_to (cr, x + width - 1 - .5, y + .5);
		cairo_stroke (cr);

		if (ythickness > 1) {
			gdk_cairo_set_source_color (cr, &style->bg [state]);
			cairo_move_to (cr, x + .5, y + 1 + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + 1 + .5);
			cairo_stroke (cr);
		}
	}

	if (edges & PANEL_EDGE_LEFT && xthickness > 0) {
		gdk_cairo_set_source_color (cr, light);
		cairo_move_to (cr, x + .5, y + .5);
		cairo_line_to (cr, x + .5, y + height - 1 - .5);
		cairo_stroke (cr);

		if (xthickness > 1) {
			gdk_cairo_set_source_color (cr, &style->bg [state]);
			cairo_move_to (cr, x + 1 + .5, y + .5);
			cairo_line_to (cr, x + 1 + .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}
#else
	if (edges & PANEL_EDGE_BOTTOM && ythickness > 0) {
		if (ythickness > 1) {
			gdk_draw_line (window, dark,
				       x, y + height - 2,
				       x + width - 1, y + height - 2);
			gdk_draw_line (window, black,
				       x, y + height - 1,
				       x + width - 1, y + height - 1);
		} else
			gdk_draw_line (window, dark,
				       x, y + height - 1,
				       x + width - 1, y + height - 1);
	}

	if (edges & PANEL_EDGE_RIGHT && xthickness > 0) {
		if (xthickness > 1) {
			gdk_draw_line (window, dark,
				       x + width - 2, y,
				       x + width - 2, y + height - 1);

			gdk_draw_line (window, black,
				       x + width - 1, y,
				       x + width - 1, y + height - 1);
		} else
			gdk_draw_line (window, dark,
				       x + width - 1, y,
				       x + width - 1, y + height - 1);
	}

	if (edges & PANEL_EDGE_TOP && ythickness > 0) {
		gdk_draw_line (window, light,
			       x, y, x + width - 1, y);

		if (ythickness > 1)
			gdk_draw_line (window,
				       style->bg_gc [state],
				       x, y + 1, x + width - 1, y + 1);
	}

	if (edges & PANEL_EDGE_LEFT && xthickness > 0) {
		gdk_draw_line (window, light,
			       x, y, x, y + height - 1);

		if (xthickness > 1)
			gdk_draw_line (window,
				       style->bg_gc [state],
				       x + 1, y, x + 1, y + height - 1);
	}
#endif
}

#if GTK_CHECK_VERSION (3, 0, 0)
static gboolean panel_frame_expose(GtkWidget* widget, cairo_t* cr)
#else
static gboolean panel_frame_expose(GtkWidget* widget, GdkEventExpose* event)
#endif
{
	PanelFrame *frame = (PanelFrame *) widget;
	gboolean    retval = FALSE;

	if (!gtk_widget_is_drawable (widget))
		return retval;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (GTK_WIDGET_CLASS (panel_frame_parent_class)->draw)
		retval = GTK_WIDGET_CLASS (panel_frame_parent_class)->draw (widget, cr);
#else
	if (GTK_WIDGET_CLASS (panel_frame_parent_class)->expose_event)
		retval = GTK_WIDGET_CLASS (panel_frame_parent_class)->expose_event (widget, event);
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
	panel_frame_draw (widget, cr, frame->edges);
#else
	panel_frame_draw (widget, frame->edges);
#endif

	return retval;
}

static void
panel_frame_set_property (GObject      *object,
			  guint         prop_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	PanelFrame *frame;

	g_return_if_fail (PANEL_IS_FRAME (object));

	frame = PANEL_FRAME (object);

	switch (prop_id) {
	case PROP_EDGES:
		panel_frame_set_edges (frame, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_frame_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	PanelFrame *frame;

	g_return_if_fail (PANEL_IS_FRAME (object));

	frame = PANEL_FRAME (object);

	switch (prop_id) {
	case PROP_EDGES:
		g_value_set_enum (value, frame->edges);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_frame_class_init (PanelFrameClass *klass)
{
	GObjectClass   *gobject_class   = (GObjectClass    *) klass;
	GtkWidgetClass *widget_class    = (GtkWidgetClass  *) klass;

	gobject_class->set_property = panel_frame_set_property;
        gobject_class->get_property = panel_frame_get_property;

	widget_class->size_allocate = panel_frame_size_allocate;
#if GTK_CHECK_VERSION (3, 0, 0)
	widget_class->get_preferred_width  = panel_frame_get_preferred_width;
	widget_class->get_preferred_height = panel_frame_get_preferred_height;
	widget_class->draw                 = panel_frame_expose;
#else
	widget_class->size_request  = panel_frame_size_request;
	widget_class->expose_event  = panel_frame_expose;
#endif

	g_object_class_install_property (
		gobject_class,
		PROP_EDGES,
		g_param_spec_enum (
			"edges",
			"Edges",
			"Which edges to draw",
			PANEL_TYPE_FRAME_EDGE,
			PANEL_EDGE_NONE,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
panel_frame_init (PanelFrame *frame)
{
	frame->edges = PANEL_EDGE_NONE;
}

GtkWidget *
panel_frame_new (PanelFrameEdge edges)
{
	return g_object_new (PANEL_TYPE_FRAME, "edges", edges, NULL);
}

void
panel_frame_set_edges (PanelFrame     *frame,
		       PanelFrameEdge  edges)
{
	g_return_if_fail (PANEL_IS_FRAME (frame));

	if (frame->edges == edges)
		return;

	frame->edges = edges;

	gtk_widget_queue_resize (GTK_WIDGET (frame));

	g_object_notify (G_OBJECT (frame), "edges");
}

PanelFrameEdge
panel_frame_get_edges (PanelFrame *frame)
{
	g_return_val_if_fail (PANEL_IS_FRAME (frame), 0);

	return frame->edges;
}
