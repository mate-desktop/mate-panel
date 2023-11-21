/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2022 Alberts Muktupāvels
 * Copyright (C) 2023 Colomban Wendling
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* See:
 * https://github.com/mate-desktop/mate-panel/issues/1230#issuecomment-1046235088
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pager-container.h"
#include <gtk/gtk.h>


G_DEFINE_TYPE (PagerContainer, pager_container, GTK_TYPE_BIN)

static gboolean
queue_resize_idle_cb (gpointer user_data)
{
	gtk_widget_queue_resize (GTK_WIDGET (user_data));
	return G_SOURCE_REMOVE;
}

static void
pager_container_get_preferred_width (GtkWidget *widget,
                                     int       *minimum_width,
                                     int       *natural_width)
{
	PagerContainer *self;

	self = PAGER_CONTAINER (widget);

	if (self->orientation == GTK_ORIENTATION_VERTICAL)
	{
		/* self->size is panel width */
		*minimum_width = *natural_width = self->size;
	}
	else
	{
		/* self->size is panel size/height, that will get allocated to pager, request width for this size */
		gtk_widget_get_preferred_width_for_height (gtk_bin_get_child (GTK_BIN (self)),
		                                           self->size,
		                                           minimum_width,
		                                           natural_width);
	}
}

static void
pager_container_get_preferred_height (GtkWidget *widget,
                                      int       *minimum_height,
                                      int       *natural_height)
{
	PagerContainer *self;

	self = PAGER_CONTAINER (widget);

	if (self->orientation == GTK_ORIENTATION_VERTICAL)
	{
		/* self->size is panel size/width that will get allocated to pager, request height for this size */
		gtk_widget_get_preferred_height_for_width (gtk_bin_get_child (GTK_BIN (self)),
		                                           self->size,
		                                           minimum_height,
		                                           natural_height);
	}
	else
	{
		/* self->size is panel height */
		*minimum_height = *natural_height = self->size;
	}
}

static void
pager_container_size_allocate (GtkWidget     *widget,
                               GtkAllocation *allocation)
{
	PagerContainer *self;
	int size;

	self = PAGER_CONTAINER (widget);

	if (self->orientation == GTK_ORIENTATION_VERTICAL)
		size = allocation->width;
	else
		size = allocation->height;

	size = MAX (size, 1);

	if (self->size != size)
	{
		self->size = size;
		g_idle_add (queue_resize_idle_cb, self);
		return;
	}

	GTK_WIDGET_CLASS (pager_container_parent_class)->size_allocate (widget,
	                                                                allocation);
}

static void
pager_container_class_init (PagerContainerClass *self_class)
{
	GtkWidgetClass *widget_class;

	widget_class = GTK_WIDGET_CLASS (self_class);

	widget_class->get_preferred_width = pager_container_get_preferred_width;
	widget_class->get_preferred_height = pager_container_get_preferred_height;
	widget_class->size_allocate = pager_container_size_allocate;
}

static void
pager_container_init (PagerContainer *self)
{
}

GtkWidget *
pager_container_new (GtkWidget     *child,
                     GtkOrientation orientation)
{
	PagerContainer *self;

	self = g_object_new (PAGER_CONTAINER_TYPE, "child", child, NULL);

	self->orientation = orientation;

	return GTK_WIDGET (self);
}

void
pager_container_set_orientation (PagerContainer *self,
                                 GtkOrientation  orientation)
{
	if (self->orientation == orientation)
		return;

	self->orientation = orientation;

	gtk_widget_queue_resize (GTK_WIDGET (self));
}
