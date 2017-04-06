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
 * NaTray is already taken for the XEMBED part, so for now it's called NaBox,
 * don't make a big deal out of it. */

#include "config.h"

#include <gtk/gtk.h>

#include "na-box.h"

#include "system-tray/na-tray.h"
#include "status-notifier/sn-host-v0.h"

#define ICON_SPACING 1
#define MIN_BOX_SIZE 3

struct _NaBox
{
  GtkBox parent;

  gint icon_padding;
  gint icon_size;

  GSList *hosts;
  GSList *items;
};

enum
{
  PROP_0,
  PROP_ICON_PADDING,
  PROP_ICON_SIZE
};

G_DEFINE_TYPE (NaBox, na_box, GTK_TYPE_BOX)

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

static void
reorder_items (GtkWidget *widget,
               gpointer   user_data)
{
  NaBox *nb;
  gint position;

  nb = NA_BOX (user_data);

  position = g_slist_index (nb->items, widget);
  gtk_box_reorder_child (GTK_BOX (nb), widget, position);
}

static void
item_added_cb (NaHost *host,
               NaItem *item,
               NaBox  *self)
{
  g_return_if_fail (NA_IS_HOST (host));
  g_return_if_fail (NA_IS_ITEM (item));
  g_return_if_fail (NA_IS_BOX (self));

  g_object_bind_property (self, "orientation",
                          item, "orientation",
                          G_BINDING_SYNC_CREATE);

  self->items = g_slist_prepend (self->items, item);
  gtk_box_pack_start (GTK_BOX (self), GTK_WIDGET (item), FALSE, FALSE, 0);

  self->items = g_slist_sort (self->items, compare_items);
  gtk_container_foreach (GTK_CONTAINER (self), reorder_items, self);
}

static void
item_removed_cb (NaHost *host,
                 NaItem *item,
                 NaBox  *self)
{
  g_return_if_fail (NA_IS_HOST (host));
  g_return_if_fail (NA_IS_ITEM (item));
  g_return_if_fail (NA_IS_BOX (self));

  gtk_container_remove (GTK_CONTAINER (self), GTK_WIDGET (item));
  self->items = g_slist_remove (self->items, item);
}

static void
update_size_and_orientation (NaBox          *self,
                             GtkOrientation  orientation)
{
  /* FIXME: do we really need that?  comes from NaTray */
  /* FIXME: if we do, do that in overridden preferred size handlers */

  /* note, you want this larger if the frame has non-NONE relief by default. */
  switch (orientation)
    {
    case GTK_ORIENTATION_VERTICAL:
      /* Give box a min size so the frame doesn't look dumb */
      gtk_widget_set_size_request (GTK_WIDGET (self), MIN_BOX_SIZE, -1);
      break;
    case GTK_ORIENTATION_HORIZONTAL:
      gtk_widget_set_size_request (GTK_WIDGET (self), -1, MIN_BOX_SIZE);
      break;
    }
}

static void
orientation_notify (GObject    *object,
                    GParamSpec *pspec,
                    gpointer    data)
{
  update_size_and_orientation (NA_BOX (object),
                               gtk_orientable_get_orientation (GTK_ORIENTABLE (object)));
}

static void
na_box_init (NaBox *self)
{
  GtkOrientation orientation;

  self->icon_padding = 0;
  self->icon_size = 0;

  self->hosts = NULL;
  self->items = NULL;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
  update_size_and_orientation (self, orientation);

  g_signal_connect (self, "notify::orientation", G_CALLBACK (orientation_notify), NULL);
}

static void
add_host (NaBox  *self,
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
na_box_style_updated (GtkWidget *widget)
{
  NaBox           *self = NA_BOX (widget);
  GtkStyleContext *context;
  GSList          *node;

  if (GTK_WIDGET_CLASS (na_box_parent_class)->style_updated)
    GTK_WIDGET_CLASS (na_box_parent_class)->style_updated (widget);

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
na_box_draw (GtkWidget *box,
             cairo_t   *cr)
{
  GList *child;
  GList *children = gtk_container_get_children (GTK_CONTAINER (box));

  for (child = children; child; child = child->next)
    {
      if (! NA_IS_ITEM (child->data) ||
          ! na_item_draw_on_parent (child->data, box, cr))
	{
	  if (gtk_widget_is_drawable (child->data) &&
	      gtk_cairo_should_draw_window (cr, gtk_widget_get_window (child->data)))
	    gtk_container_propagate_draw (GTK_CONTAINER (box), child->data, cr);
	}
    }

  g_list_free (children);

  return TRUE;
}

static void
na_box_realize (GtkWidget *widget)
{
  NaBox *self = NA_BOX (widget);
  GdkScreen *screen;
  GtkOrientation orientation;
  NaHost *tray_host;

  GTK_WIDGET_CLASS (na_box_parent_class)->realize (widget);

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
na_box_unrealize (GtkWidget *widget)
{
  NaBox *self = NA_BOX (widget);

  if (self->hosts != NULL)
    {
      g_slist_free_full (self->hosts, g_object_unref);
      self->hosts = NULL;
    }

  g_clear_pointer (&self->items, g_slist_free);

  GTK_WIDGET_CLASS (na_box_parent_class)->unrealize (widget);
}

static void
na_box_get_property (GObject    *object,
                     guint       property_id,
                     GValue     *value,
                     GParamSpec *pspec)
{
  NaBox *self = NA_BOX (object);

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
na_box_set_property (GObject      *object,
                     guint         property_id,
                     const GValue *value,
                     GParamSpec   *pspec)
{
  NaBox *self = NA_BOX (object);

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
na_box_class_init (NaBoxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->get_property = na_box_get_property;
  gobject_class->set_property = na_box_set_property;

  widget_class->draw = na_box_draw;
  widget_class->realize = na_box_realize;
  widget_class->unrealize = na_box_unrealize;
  widget_class->style_updated = na_box_style_updated;

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
na_box_new (GtkOrientation  orientation)
{
  return g_object_new (NA_TYPE_BOX,
                       "orientation", orientation,
                       "spacing", ICON_SPACING,
                       NULL);
}

void
na_box_force_redraw (NaBox *box)
{
  GSList *node;

  for (node = box->hosts; node; node = node->next)
    na_host_force_redraw (node->data);
}
