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
#include <libpanel-util/panel-color.h>

#include "panel-frame.h"
#include "panel-typebuiltins.h"

G_DEFINE_TYPE (PanelFrame, panel_frame, GTK_TYPE_BIN)

enum {
	PROP_0,
	PROP_EDGES
};

static void
panel_frame_get_preferred_width (GtkWidget *widget,
				 gint *minimal_width,
				 gint *natural_width)
{
	PanelFrame      *frame = (PanelFrame *) widget;
	GtkBin          *bin = (GtkBin *) widget;
	GtkStyleContext *context;
	GtkWidget       *child;
	GtkBorder        padding;
	int              border_width;

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_padding (context, gtk_widget_get_state_flags (widget), &padding);
	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	*minimal_width = 1;
	*natural_width = 1;

	child = gtk_bin_get_child (bin);
	if (child && gtk_widget_get_visible (child))
		gtk_widget_get_preferred_width (child, minimal_width, natural_width);

	*minimal_width += border_width;
	*natural_width += border_width;

	if (frame->edges & PANEL_EDGE_LEFT) {
		*minimal_width += padding.left;
		*natural_width += padding.left;
	}

	if (frame->edges & PANEL_EDGE_RIGHT) {
		*minimal_width += padding.right;
		*natural_width += padding.right;
	}
}

static void
panel_frame_get_preferred_height (GtkWidget *widget,
				  gint *minimal_height,
				  gint *natural_height)
{
	PanelFrame      *frame = (PanelFrame *) widget;
	GtkBin          *bin   = (GtkBin *) widget;
	GtkStyleContext *context;
	GtkWidget       *child;
	GtkBorder        padding;
	int              border_width;

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_padding (context, gtk_widget_get_state_flags (widget), &padding);	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	*minimal_height = 1;
	*natural_height = 1;

	child = gtk_bin_get_child (bin);
	if (child && gtk_widget_get_visible (child))
		gtk_widget_get_preferred_height (child, minimal_height, natural_height);

	*minimal_height += border_width;
	*natural_height += border_width;

	if (frame->edges & PANEL_EDGE_TOP) {
		*minimal_height += padding.top;
		*natural_height += padding.top;
	}

	if (frame->edges & PANEL_EDGE_BOTTOM) {
		*minimal_height += padding.bottom;
		*natural_height += padding.bottom;
	}
}

static void
panel_frame_size_allocate (GtkWidget     *widget,
			   GtkAllocation *allocation)
{
	PanelFrame      *frame = (PanelFrame *) widget;
	GtkBin          *bin   = (GtkBin *) widget;
	GtkStyleContext *context;
	GtkBorder        padding;
	GtkAllocation    child_allocation;
	GtkAllocation    child_allocation_current;
	GtkWidget       *child;
	int              border_width;

	gtk_widget_set_allocation (widget, allocation);

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_padding (context, gtk_widget_get_state_flags (widget), &padding);
	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	child_allocation.x      = allocation->x + border_width;
	child_allocation.y      = allocation->y + border_width;
	child_allocation.width  = allocation->width  - 2 * border_width;
	child_allocation.height = allocation->height - 2 * border_width;

	if (frame->edges & PANEL_EDGE_LEFT) {
		child_allocation.x     += padding.left;
		child_allocation.width -= padding.left;
	}

	if (frame->edges & PANEL_EDGE_TOP) {
		child_allocation.y      += padding.top;
		child_allocation.height -= padding.top;
	}

	if (frame->edges & PANEL_EDGE_RIGHT)
		child_allocation.width -= padding.left;

	if (frame->edges & PANEL_EDGE_BOTTOM)
		child_allocation.height -= padding.top;

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
		  cairo_t *cr,
		  PanelFrameEdge  edges)
{
	PanelFrame       *frame = (PanelFrame *) widget;
	GtkStyleContext  *context;
	GtkStateFlags     state;
	GdkRGBA          *bg;
	GdkRGBA           dark, light;
	GtkBorder         padding;
	int               x, y, width, height;


	if (edges == PANEL_EDGE_NONE)
		return;

	context = gtk_widget_get_style_context (widget);
	state = gtk_widget_get_state_flags (widget);

	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);

	gtk_style_context_get (context, state,
	                       "background-color", &bg,
	                       NULL);

	gtk_style_shade (bg, &dark, 0.7);
	gtk_style_shade (bg, &light, 1.3);

	gtk_style_context_get_padding (context, state, &padding);

	/* Copied from gtk_default_draw_shadow() */

	x = y = 0;

	cairo_set_line_width (cr, 1);

	if (frame->edges & PANEL_EDGE_BOTTOM && padding.bottom > 0) {
		if (padding.bottom > 1) {
			gdk_cairo_set_source_rgba (cr, &dark);
			cairo_move_to (cr, x + .5, y + height - 2 + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 2 + .5);
			cairo_stroke (cr);

			cairo_set_source_rgb (cr, 0., 0., 0.);
			cairo_move_to (cr, x + .5, y + height - 1 - .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		} else {
			gdk_cairo_set_source_rgba (cr, &dark);
			cairo_move_to (cr, x + .5, y + height - 1 - .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}

	if (frame->edges & PANEL_EDGE_RIGHT && padding.right > 0) {
		if (padding.right > 1) {
			gdk_cairo_set_source_rgba (cr, &dark);
			cairo_move_to (cr, x + width - 2 - .5, y + .5);
			cairo_line_to (cr, x + width - 2 - .5, y + height - 1 - .5);
			cairo_stroke (cr);

			cairo_set_source_rgb (cr, 0., 0., 0.);
			cairo_move_to (cr, x + width - 1 - .5, y + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		} else {
			gdk_cairo_set_source_rgba (cr, &dark);
			cairo_move_to (cr, x + width - 1 - .5, y + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}

	if (frame->edges & PANEL_EDGE_TOP && padding.top > 0) {
		gdk_cairo_set_source_rgba (cr, &light);
		cairo_move_to (cr, x + .5, y + .5);
		cairo_line_to (cr, x + width - 1 - .5, y + .5);
		cairo_stroke (cr);

		if (padding.top > 1) {
			gdk_cairo_set_source_rgba (cr, bg);
			cairo_move_to (cr, x + .5, y + 1 + .5);
			cairo_line_to (cr, x + width - 1 - .5, y + 1 + .5);
			cairo_stroke (cr);
		}
	}

	if (frame->edges & PANEL_EDGE_LEFT && padding.left > 0) {
		gdk_cairo_set_source_rgba (cr, &light);
		cairo_move_to (cr, x + .5, y + .5);
		cairo_line_to (cr, x + .5, y + height - 1 - .5);
		cairo_stroke (cr);

		if (padding.left > 1) {
			gdk_cairo_set_source_rgba (cr, bg);
			cairo_move_to (cr, x + 1 + .5, y + .5);
			cairo_line_to (cr, x + 1 + .5, y + height - 1 - .5);
			cairo_stroke (cr);
		}
	}

	gdk_rgba_free (bg);
}

static gboolean panel_frame_expose(GtkWidget* widget, cairo_t* cr)
{
	PanelFrame *frame = (PanelFrame *) widget;
	gboolean    retval = FALSE;

	if (!gtk_widget_is_drawable (widget))
		return retval;

	if (GTK_WIDGET_CLASS (panel_frame_parent_class)->draw)
		retval = GTK_WIDGET_CLASS (panel_frame_parent_class)->draw (widget, cr);

	panel_frame_draw (widget, cr, frame->edges);

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
	widget_class->get_preferred_width  = panel_frame_get_preferred_width;
	widget_class->get_preferred_height = panel_frame_get_preferred_height;
	widget_class->draw                 = panel_frame_expose;

	gtk_widget_class_set_css_name (widget_class, "PanelFrame");

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
