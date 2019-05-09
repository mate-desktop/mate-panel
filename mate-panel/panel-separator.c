/*
 * panel-separator.c: panel "Separator" module
 *
 * Copyright (C) 2005 Carlos Garcia Campos <carlosgc@gnome.org>
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
 *      Carlos Garcia Campos <carlosgc@gnome.org>
 */

#include <config.h>

#include "panel-separator.h"
#include "panel-background.h"
#include "panel-profile.h"

#define SEPARATOR_SIZE 10

struct _PanelSeparatorPrivate {
	AppletInfo     *info;
	PanelWidget    *panel;

	GtkOrientation  orientation;
};

G_DEFINE_TYPE_WITH_PRIVATE (PanelSeparator, panel_separator, GTK_TYPE_EVENT_BOX)

static gboolean
panel_separator_draw (GtkWidget *widget, cairo_t *cr)
{
	PanelSeparator  *separator;
	GtkStyleContext *context;
	GtkStateFlags    state;
	GtkBorder        padding;
	int              width;
	int              height;

	separator = PANEL_SEPARATOR (widget);

	if (!gtk_widget_is_drawable(widget))
		return FALSE;

	GTK_WIDGET_CLASS(panel_separator_parent_class)->draw(widget, cr);

	state = gtk_widget_get_state_flags (widget);
	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_padding (context, state, &padding);

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);

	cairo_save (cr);

	if (separator->priv->orientation == GTK_ORIENTATION_HORIZONTAL) {
		int x;

		x = (width - padding.left - padding.right) / 2 + padding.left;
		x = MIN (x, width - padding.right);

		gtk_render_line (context, cr,
				 x, padding.top,
				 x, height - padding.bottom);
	} else {
		int y;

		y = (height - padding.top - padding.bottom) / 2 + padding.top;
		y = MIN (y, height - padding.bottom);

		gtk_render_line (context, cr,
				 padding.left, y,
				 width - padding.right, y);
	}
	cairo_restore (cr);

	gtk_style_context_restore (context);

	return FALSE;
}

static void
panel_separator_get_preferred_width (GtkWidget *widget,
				     gint *minimal_width,
				     gint *natural_width)
{
	PanelSeparator *separator;
	int             size;

	separator = PANEL_SEPARATOR (widget);

	size = panel_toplevel_get_size (separator->priv->panel->toplevel);

	if (separator->priv->orientation == GTK_ORIENTATION_VERTICAL)
		*minimal_width = *natural_width = size;
	else
		*minimal_width = *natural_width = SEPARATOR_SIZE;
}

static void
panel_separator_get_preferred_height (GtkWidget *widget,
				      gint *minimal_height,
				      gint *natural_height)
{
	PanelSeparator *separator;
	int             size;

	separator = PANEL_SEPARATOR (widget);

	size = panel_toplevel_get_size (separator->priv->panel->toplevel);

	if (separator->priv->orientation == GTK_ORIENTATION_VERTICAL)
		*minimal_height = *natural_height = SEPARATOR_SIZE;
	else
		*minimal_height = *natural_height = size;
}

static void
panel_separator_size_allocate (GtkWidget     *widget,
			       GtkAllocation *allocation)
{
	GtkAllocation    old_allocation;
	GtkAllocation    widget_allocation;
	PanelBackground *background;

	gtk_widget_get_allocation (widget, &widget_allocation);

	old_allocation.x      = widget_allocation.x;
	old_allocation.y      = widget_allocation.y;
	old_allocation.width  = widget_allocation.width;
	old_allocation.height = widget_allocation.height;

	GTK_WIDGET_CLASS (panel_separator_parent_class)->size_allocate (widget, allocation);

	if (old_allocation.x      == allocation->x &&
	    old_allocation.y      == allocation->y &&
	    old_allocation.width  == allocation->width &&
	    old_allocation.height == allocation->height)
		return;

	background = &PANEL_SEPARATOR (widget)->priv->panel->toplevel->background;
	if (background->type == PANEL_BACK_NONE ||
	   (background->type == PANEL_BACK_COLOR && !background->has_alpha))
		return;

	panel_separator_change_background (PANEL_SEPARATOR (widget));
}

static void
panel_separator_parent_set (GtkWidget *widget,
			   GtkWidget *previous_parent)
{
	PanelSeparator *separator;
	GtkWidget      *parent;

	separator = PANEL_SEPARATOR (widget);

	parent = gtk_widget_get_parent (widget);
	g_assert (!parent || PANEL_IS_WIDGET (parent));

	separator->priv->panel = (PanelWidget *) parent;
}

static void
panel_separator_class_init (PanelSeparatorClass *klass)
{
	GtkWidgetClass *widget_class  = GTK_WIDGET_CLASS (klass);

	widget_class->draw                 = panel_separator_draw;
	widget_class->get_preferred_width  = panel_separator_get_preferred_width;
	widget_class->get_preferred_height = panel_separator_get_preferred_height;
	widget_class->size_allocate = panel_separator_size_allocate;
	widget_class->parent_set    = panel_separator_parent_set;

	gtk_widget_class_set_css_name (widget_class, "PanelSeparator");
}

static void
panel_separator_init (PanelSeparator *separator)
{
	separator->priv = panel_separator_get_instance_private (separator);

	separator->priv->info  = NULL;
	separator->priv->panel = NULL;
	separator->priv->orientation = GTK_ORIENTATION_HORIZONTAL;
}

void
panel_separator_set_orientation (PanelSeparator   *separator,
				 PanelOrientation  orientation)
{
	GtkOrientation orient = GTK_ORIENTATION_HORIZONTAL;

	g_return_if_fail (PANEL_IS_SEPARATOR (separator));

	switch (orientation) {
	case PANEL_ORIENTATION_TOP:
	case PANEL_ORIENTATION_BOTTOM:
		orient = GTK_ORIENTATION_HORIZONTAL;
		break;
	case PANEL_ORIENTATION_RIGHT:
	case PANEL_ORIENTATION_LEFT:
		orient = GTK_ORIENTATION_VERTICAL;
		break;
	}

	if (orient == separator->priv->orientation)
		return;

	separator->priv->orientation = orient;

	gtk_widget_queue_draw (GTK_WIDGET (separator));
}

void
panel_separator_load_from_gsettings (PanelWidget *panel,
				 gboolean     locked,
				 int          position,
				 const char  *id)
{
	PanelSeparator *separator;

	separator = g_object_new (PANEL_TYPE_SEPARATOR, NULL);

	separator->priv->info = mate_panel_applet_register (GTK_WIDGET (separator),
						       NULL, NULL,
						       panel, locked, position,
						       TRUE,
						       PANEL_OBJECT_SEPARATOR,
						       id);

	if (!separator->priv->info) {
		gtk_widget_destroy (GTK_WIDGET (separator));
		return;
	}

	panel_widget_set_applet_expandable (panel, GTK_WIDGET (separator),
					    FALSE, TRUE);
	panel_widget_set_applet_size_constrained (panel,
						  GTK_WIDGET (separator), TRUE);
}

void
panel_separator_create (PanelToplevel *toplevel,
			int            position)
{
	char *id;

	id = panel_profile_prepare_object (PANEL_OBJECT_SEPARATOR,
					   toplevel, position, FALSE);
	panel_profile_add_to_list (PANEL_GSETTINGS_OBJECTS, id);
	g_free (id);
}

void
panel_separator_change_background (PanelSeparator *separator)
{
	panel_background_apply_css(&separator->priv->panel->toplevel->background, GTK_WIDGET(separator));
}
