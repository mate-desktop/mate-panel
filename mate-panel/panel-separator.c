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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Carlos Garcia Campos <carlosgc@gnome.org>
 */

#include <config.h>

#include "panel-separator.h"
#include "panel-background.h"
#include "panel-profile.h"

#define SEPARATOR_SIZE 10

#define PANEL_SEPARATOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PANEL_TYPE_SEPARATOR, PanelSeparatorPrivate))

struct _PanelSeparatorPrivate {
	AppletInfo     *info;
	PanelWidget    *panel;

	GtkOrientation  orientation;
};

G_DEFINE_TYPE (PanelSeparator, panel_separator, GTK_TYPE_EVENT_BOX)

static void
panel_separator_paint (GtkWidget    *widget,
		       GdkRectangle *area)
{
	PanelSeparator *separator;
	GdkWindow      *window;
	GtkStyle       *style;
	GtkAllocation   allocation;

	separator = PANEL_SEPARATOR (widget);

	window = gtk_widget_get_window (widget);
	style = gtk_widget_get_style (widget);
	gtk_widget_get_allocation (widget, &allocation);

	if (separator->priv->orientation == GTK_ORIENTATION_HORIZONTAL) {
		gtk_paint_vline (style, window,
				 gtk_widget_get_state (widget),
				 area, widget, "separator",
				 style->xthickness,
				 allocation.height - style->xthickness,
				 (allocation.width - style->xthickness) / 2);
	} else {
		gtk_paint_hline (style, window,
				 gtk_widget_get_state (widget),
				 area, widget, "separator",
				 style->ythickness,
				 allocation.width - style->ythickness,
				 (allocation.height  - style->ythickness) / 2);
	}
}

static gboolean panel_separator_expose_event(GtkWidget* widget, GdkEventExpose* event)
{
	if (gtk_widget_is_drawable(widget))
	{
		GTK_WIDGET_CLASS(panel_separator_parent_class)->expose_event(widget, event);

		panel_separator_paint(widget, &event->area);
	}

	return FALSE;
}

static void
panel_separator_size_request (GtkWidget      *widget,
			      GtkRequisition *requisition)
{
	PanelSeparator *separator;
	int             size;

	separator = PANEL_SEPARATOR (widget);

	size = panel_toplevel_get_size (separator->priv->panel->toplevel);

	if (separator->priv->orientation == GTK_ORIENTATION_VERTICAL) {
		requisition->width = size;
		requisition->height = SEPARATOR_SIZE;
	} else {
		requisition->width = SEPARATOR_SIZE;
		requisition->height = size;
	}
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

	background = &PANEL_SEPARATOR (widget)->priv->panel->background;

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

	widget_class->expose_event  = panel_separator_expose_event;
	widget_class->size_request  = panel_separator_size_request;
	widget_class->size_allocate = panel_separator_size_allocate;
	widget_class->parent_set    = panel_separator_parent_set;

	g_type_class_add_private (klass, sizeof (PanelSeparatorPrivate));
}

static void
panel_separator_init (PanelSeparator *separator)
{
	separator->priv = PANEL_SEPARATOR_GET_PRIVATE (separator);

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
panel_separator_load_from_mateconf (PanelWidget *panel,
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
	panel_profile_add_to_list (PANEL_MATECONF_OBJECTS, id);
	g_free (id);
}

void
panel_separator_change_background (PanelSeparator *separator)
{
	panel_background_change_background_on_widget (&separator->priv->panel->background,
						      GTK_WIDGET (separator));
}
