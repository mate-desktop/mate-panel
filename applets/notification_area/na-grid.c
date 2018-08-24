/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2007 Christian Persch
 * Copyright (C) 2017 Colomban Wendling <cwendling@hypra.fr>
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

/* Well, actuall'y is the Tray itself, the container for the items.  But
 * NaTray is already taken for the XEMBED part, so for now it's called NaGrid,
 * don't make a big deal out of it. */

#include "config.h"
//#include <syslog.h> if we need Debug syslog output to know what's happenning in the grid sorting mechanism
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "na-grid.h"

#include "system-tray/na-tray.h"
#include "status-notifier/sn-host-v0.h"

#define MIN_ICON_SIZE_DEFAULT 26
#define USE_ONLY_ONE_LINE_DEFAULT TRUE

typedef struct
{
	gint cols;
	gint rows;
	gint length;
} GridProps;

typedef struct
{
	GtkOrientation orientation;
	guint index;
	NaGrid *na_grid;
} PackData;

struct _NaGrid
{
	GtkGrid parent;

	gint icon_padding;
	gint icon_size;
	gint min_icon_size;
	gboolean use_only_one_line;

	GSList *hosts;
	GSList *items;
	GridProps *grid_props;
};

enum
{
	PROP_0,
	PROP_ICON_PADDING,
	PROP_ICON_SIZE
};

G_DEFINE_TYPE (NaGrid, na_grid, GTK_TYPE_GRID)

static gint
compare_items (gconstpointer a,
	       gconstpointer b)
{
	NaItem *item1;
	NaItem *item2;
	NaItemCategory c1;
	NaItemCategory c2;
	const gchar *id1;
	const gchar *id2;

	item1 = (NaItem *) a;
	item2 = (NaItem *) b;

	c1 = na_item_get_category (item1);
	c2 = na_item_get_category (item2);

	if (c1 < c2)
		return -1;
	else if (c1 > c2)
		return 1;

	id1 = na_item_get_id (item1);
	id2 = na_item_get_id (item2);

	return g_strcmp0 (id1, id2);
}

static void reorder_items_with_data(GtkWidget *item,
				    PackData  *data)
{
	guint row, col;
	guint left, right, top, bottom;

	/* row / col number depends on whether we are horizontal or vertical */
	if (data->orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		col = data->index / data->na_grid->grid_props->rows;
		row = data->index % data->na_grid->grid_props->rows;
	} else {
		row = data->index / data->na_grid->grid_props->cols;
		col = data->index % data->na_grid->grid_props->cols;
	}
	/* only update item position if has changed from current */
	gtk_container_child_get (GTK_CONTAINER (data->na_grid),
				 item,
				 "left-attach", &left,
				 "width", &right,
				 "top-attach", &top,
				 "height", &bottom,
				 NULL);
	if (left != col || right != 1 || top != row || bottom != 1)
	{
		gtk_container_child_set (GTK_CONTAINER (data->na_grid),
					 item,
					 "left-attach", col,
					 "width", 1,
					 "top-attach", row,
					 "height", 1,
					 NULL);
	}
	/* increment to index of next item */
	data->index++;
}

static void
determine_grid_properties(NaGrid *self)
{
	gint rows, cols, length;
	GtkOrientation orientation;
	GtkAllocation allocation;
	
	orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
	gtk_widget_get_allocation (GTK_GRID (self), &allocation);
	length = g_slist_length (self->items);
	
	if (self->use_only_one_line)
	{

		if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
			rows = 1;
			cols = length;
		} else {
			cols = 1;
			rows = length;
		}
	
	} else {

		if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
			rows = MAX (1, allocation.height / self->min_icon_size);
			cols = MAX (1, length / rows);
			if (length % rows)
				cols++;
		} else {
			cols = MAX (1, allocation.width / self->min_icon_size);
			rows = MAX (1, length / cols);
			if (length % cols)
				rows++;
		}
	
	}
	
	self->grid_props->cols = cols;
	self->grid_props->rows = rows;
	self->grid_props->length = length;

	// Debug to see if it's calculating correctly...
	//syslog (LOG_MAKEPRI(LOG_LOCAL1, LOG_INFO), "Cols %d. Rows %d. length %d.", cols, rows, length);
}

void
refresh_grid(NaGrid *self)
{
	PackData pack_data;
	GSList *item;
	GtkOrientation orientation;
	gint row, col = 0;
	
	g_return_if_fail (NA_IS_GRID (self));
	
	orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
	determine_grid_properties (self);

	for (item = self->items; item; item = item->next)
	{
		if (GTK_IS_WIDGET (item))
			gtk_container_remove (GTK_CONTAINER (self), GTK_WIDGET(item));
	}

	for (item = self->items; item; item = item->next)
	{	
		if (GTK_IS_WIDGET (item))
		{
			gtk_grid_attach (GTK_GRID (self),
					 GTK_WIDGET(item),
					 col, row,
					 1, 1);

			gtk_container_child_set (GTK_CONTAINER (self),
						 item,
						 "left-attach", col,
						 "width", 1,
						 "top-attach", row,
						 "height", 1,
						 NULL);

			if (self->use_only_one_line) {
				if (orientation == GTK_ORIENTATION_HORIZONTAL)
					col++;
				else
					row++;
			} else {
				if (orientation == GTK_ORIENTATION_HORIZONTAL) {
					if (col < self->grid_props->cols) col++;
					else {
						col = 0;
						row++;
					}
				} else {
					if (row < self->grid_props->rows) row++;
					else {
						row = 0;
						col++;
					}
				} 
			} 
		}
	}

	pack_data.orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
	pack_data.index = 0;
	pack_data.na_grid = self;

	g_list_foreach (self->items,
			(GHFunc)reorder_items_with_data,
			&pack_data);

	na_grid_force_redraw (GTK_GRID (self));
}

void set_grid_display_mode(NaGrid   *grid,
			   gboolean  use_only_one_line,
			   gint      min_icon_size)
{
	grid->use_only_one_line = use_only_one_line;
	grid->min_icon_size = min_icon_size;
	refresh_grid (grid);
}

static void
item_added_cb (NaHost *host,
	       NaItem *item,
	       NaGrid *self)
{
	g_return_if_fail (NA_IS_HOST (host));
	g_return_if_fail (NA_IS_ITEM (item));
	g_return_if_fail (NA_IS_GRID (self));

	g_object_bind_property (self, "orientation",
				item, "orientation",
				G_BINDING_SYNC_CREATE);

	self->items = g_slist_prepend (self->items, item);

	determine_grid_properties (self);

	gtk_widget_set_hexpand (GTK_WIDGET(item), TRUE);
	gtk_widget_set_vexpand (GTK_WIDGET(item), TRUE);
	gtk_grid_attach (GTK_GRID (self),
			 item,
			 self->grid_props->cols - 1,
			 self->grid_props->rows - 1,
			 1, 1);

	self->items = g_slist_sort (self->items, compare_items);
	refresh_grid (self);
}

static void
item_removed_cb (NaHost *host,
		 NaItem *item,
		 NaGrid *self)
{
	g_return_if_fail (NA_IS_HOST (host));
	g_return_if_fail (NA_IS_ITEM (item));
	g_return_if_fail (NA_IS_GRID (self));

	gtk_container_remove (GTK_CONTAINER (self), GTK_WIDGET (item));
	
	self->items = g_slist_remove (self->items, item);
	refresh_grid (self);
}

static void
refresh_notify (GObject    *object,
		GParamSpec *pspec,
		gpointer    data)
{
	refresh_grid (NA_GRID (object));
}

static void
na_grid_init (NaGrid *self)
{
	self->icon_padding = 0;
	self->icon_size = 0;

	self->hosts = NULL;
	self->items = NULL;
	self->grid_props = g_new0 (GridProps, 1);
	self->min_icon_size = MIN_ICON_SIZE_DEFAULT;
	self->use_only_one_line = USE_ONLY_ONE_LINE_DEFAULT;
	
	gtk_grid_set_row_homogeneous (GTK_GRID(self), TRUE);
	gtk_grid_set_column_homogeneous (GTK_GRID(self), TRUE);

	g_signal_connect (self, "notify::orientation", G_CALLBACK (refresh_notify), NULL);
	g_signal_connect (self, "notify::size-changed", G_CALLBACK (refresh_notify), NULL);
}

static void
add_host (NaGrid *self,
	  NaHost *host)
{
	self->hosts = g_slist_prepend (self->hosts, host);

	g_object_bind_property (self, "icon-padding", host, "icon-padding",
				G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
	g_object_bind_property (self, "icon-size", host, "icon-size",
				G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

	g_signal_connect_object (host, "item-added",
				 G_CALLBACK (item_added_cb), self, 0);
	g_signal_connect_object (host, "item-removed",
				 G_CALLBACK (item_removed_cb), self, 0);
}

static void
na_grid_style_updated (GtkWidget *widget)
{
	NaGrid *self = NA_GRID (widget);
	GtkStyleContext *context;
	GSList *node;

	if (GTK_WIDGET_CLASS (na_grid_parent_class)->style_updated)
		GTK_WIDGET_CLASS (na_grid_parent_class)->style_updated (widget);

	context = gtk_widget_get_style_context (widget);

	for (node = self->hosts; node; node = node->next)
	{
		gtk_style_context_save (context);
		na_host_style_updated (node->data, context);
		gtk_style_context_restore (context);
	}
}

/* Custom drawing because system-tray items need weird stuff. */
static gboolean
na_grid_draw (GtkWidget *grid,
	      cairo_t   *cr)
{
	GList *child;
	GList *children = gtk_container_get_children (GTK_CONTAINER (grid));

	for (child = children; child; child = child->next)
	{
		if (! NA_IS_ITEM (child->data) || !na_item_draw_on_parent (child->data, grid, cr))
		{
			if (gtk_widget_is_drawable (child->data) &&
			    gtk_cairo_should_draw_window (cr, gtk_widget_get_window (child->data)))
				gtk_container_propagate_draw (GTK_CONTAINER (grid), child->data, cr);
		}
	}

	g_list_free (children);

	return TRUE;
}

static void
na_grid_realize (GtkWidget *widget)
{
	NaGrid *self = NA_GRID (widget);
	GdkScreen *screen;
	GtkOrientation orientation;
	NaHost *tray_host;

	GTK_WIDGET_CLASS (na_grid_parent_class)->realize (widget);

	/* Instantiate the hosts now we have a screen */
	screen = gtk_widget_get_screen (GTK_WIDGET (self));
	orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
	tray_host = na_tray_new_for_screen (screen, orientation);
	g_object_bind_property (self, "orientation",
				tray_host, "orientation",
				G_BINDING_DEFAULT);

	add_host (self, tray_host);
	add_host (self, sn_host_v0_new ());
}

static void
na_grid_unrealize (GtkWidget *widget)
{
	NaGrid *self = NA_GRID (widget);

	if (self->hosts != NULL)
	{
		g_slist_free_full (self->hosts, g_object_unref);
		self->hosts = NULL;
	}

	g_clear_pointer (&self->items, g_slist_free);

	GTK_WIDGET_CLASS (na_grid_parent_class)->unrealize (widget);
}

static void
na_grid_get_property (GObject    *object,
		      guint       property_id,
		      GValue     *value,
		      GParamSpec *pspec)
{
	NaGrid *self = NA_GRID (object);

	switch (property_id)
	{
		case PROP_ICON_PADDING:
			g_value_set_int (value, self->icon_padding);
			break;
		case PROP_ICON_SIZE:
			g_value_set_int (value, self->icon_size);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
na_grid_set_property (GObject      *object,
		      guint	   property_id,
		      const GValue *value,
		      GParamSpec   *pspec)
{
	NaGrid *self = NA_GRID (object);

	switch (property_id)
	{
		case PROP_ICON_PADDING:
			self->icon_padding = g_value_get_int (value);
			break;
		case PROP_ICON_SIZE:
			self->icon_size = g_value_get_int (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
na_grid_class_init (NaGridClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gobject_class->get_property = na_grid_get_property;
	gobject_class->set_property = na_grid_set_property;

	widget_class->draw = na_grid_draw;
	widget_class->realize = na_grid_realize;
	widget_class->unrealize = na_grid_unrealize;
	widget_class->style_updated = na_grid_style_updated;

	g_object_class_install_property (gobject_class, PROP_ICON_PADDING,
		g_param_spec_int ("icon-padding",
				  "Padding around icons",
				  "Padding that should be put around icons, in pixels",
				  0, G_MAXINT, 0,
				  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_ICON_SIZE,
		g_param_spec_int ("icon-size",
				  "Icon size",
				  "If non-zero, hardcodes the size of the icons in pixels",
				  0, G_MAXINT, 0,
				  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

GtkWidget *
na_grid_new (GtkOrientation  orientation)
{
	return g_object_new (NA_TYPE_GRID,
			     "orientation", orientation,
			     NULL);
}

void
na_grid_force_redraw (NaGrid *grid)
{
	GSList *node;
	
	g_return_if_fail (NA_IS_GRID (grid));

	for (node = grid->hosts; node; node = node->next)
		na_host_force_redraw (node->data);
}
