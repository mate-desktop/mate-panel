/*
 * Copyright (C) 2016 Alberts Muktupāvels
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

#include "config.h"

#include "sn-image-menu-item.h"

#define SPACING 6

struct _SnImageMenuItem
{
  GtkMenuItem  parent;

  GtkWidget   *box;
  GtkWidget   *image;
  GtkWidget   *accel_label;

  gchar       *label;
};

G_DEFINE_TYPE (SnImageMenuItem, sn_image_menu_item, GTK_TYPE_MENU_ITEM)

static void
sn_image_menu_item_get_preferred_width (GtkWidget *widget,
                                        gint      *minimum,
                                        gint      *natural)
{
  GtkWidgetClass *widget_class;
  SnImageMenuItem *item;
  GtkRequisition image_requisition;

  widget_class = GTK_WIDGET_CLASS (sn_image_menu_item_parent_class);
  item = SN_IMAGE_MENU_ITEM (widget);

  widget_class->get_preferred_width (widget, minimum, natural);

  if (!gtk_widget_get_visible (item->image))
    return;

  gtk_widget_get_preferred_size (item->image, &image_requisition, NULL);

  if (image_requisition.width > 0)
    {
      *minimum -= image_requisition.width + SPACING;
      *natural -= image_requisition.width + SPACING;
    }
}

static void
sn_image_menu_item_size_allocate (GtkWidget     *widget,
                                  GtkAllocation *allocation)
{
  GtkWidgetClass *widget_class;
  SnImageMenuItem *item;
  GtkRequisition image_requisition;
  GtkAllocation box_allocation;

  widget_class = GTK_WIDGET_CLASS (sn_image_menu_item_parent_class);
  item = SN_IMAGE_MENU_ITEM (widget);

  widget_class->size_allocate (widget, allocation);

  if (!gtk_widget_get_visible (item->image))
    return;

  gtk_widget_get_preferred_size (item->image, &image_requisition, NULL);
  gtk_widget_get_allocation (item->box, &box_allocation);

  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    {
      if (image_requisition.width > 0)
        box_allocation.x -= image_requisition.width + SPACING;
    }
  else
    {
      if (image_requisition.width > 0)
        box_allocation.x += image_requisition.width + SPACING;
    }

  gtk_widget_size_allocate (item->box, &box_allocation);
}

static const gchar *
sn_image_menu_item_get_label (GtkMenuItem *menu_item)
{
  SnImageMenuItem *item;

  item = SN_IMAGE_MENU_ITEM (menu_item);

  return item->label;
}

static void
sn_image_menu_item_toggle_size_request (GtkMenuItem *menu_item,
                                        gint        *requisition)
{
  SnImageMenuItem *item;
  GtkRequisition image_requisition;

  item = SN_IMAGE_MENU_ITEM (menu_item);

  *requisition = 0;

  if (!gtk_widget_get_visible (item->image))
    return;

  gtk_widget_get_preferred_size (item->image, &image_requisition, NULL);

  if (image_requisition.width > 0)
    *requisition = image_requisition.width + SPACING;
}

static void
sn_image_menu_item_set_label (GtkMenuItem *menu_item,
                              const gchar *label)
{
  SnImageMenuItem *item;

  item = SN_IMAGE_MENU_ITEM (menu_item);

  if (g_strcmp0 (item->label, label) != 0)
    {
      g_free (item->label);
      item->label = g_strdup (label);

      gtk_label_set_text_with_mnemonic (GTK_LABEL (item->accel_label), label);
      g_object_notify (G_OBJECT (menu_item), "label");
    }
}

static void
sn_image_menu_item_class_init (SnImageMenuItemClass *item_class)
{
  GtkWidgetClass *widget_class;
  GtkMenuItemClass *menu_item_class;

  widget_class = GTK_WIDGET_CLASS (item_class);
  menu_item_class = GTK_MENU_ITEM_CLASS (item_class);

  widget_class->get_preferred_width = sn_image_menu_item_get_preferred_width;
  widget_class->size_allocate = sn_image_menu_item_size_allocate;

  menu_item_class->get_label = sn_image_menu_item_get_label;
  menu_item_class->toggle_size_request = sn_image_menu_item_toggle_size_request;
  menu_item_class->set_label = sn_image_menu_item_set_label;
}

static void
sn_image_menu_item_init (SnImageMenuItem *item)
{
  GtkAccelLabel *accel_label;

  item->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, SPACING);
  gtk_container_add (GTK_CONTAINER (item), item->box);
  gtk_widget_show (item->box);

  item->image = gtk_image_new ();
  gtk_box_pack_start (GTK_BOX (item->box), item->image, FALSE, FALSE, 0);

  item->accel_label = gtk_accel_label_new ("");
  gtk_box_pack_end (GTK_BOX (item->box), item->accel_label, TRUE, TRUE, 0);
  gtk_label_set_xalign (GTK_LABEL (item->accel_label), 0.0);
  gtk_widget_show (item->accel_label);

  accel_label = GTK_ACCEL_LABEL (item->accel_label);
  gtk_accel_label_set_accel_widget (accel_label, GTK_WIDGET (item));
  gtk_label_set_use_underline (GTK_LABEL (accel_label), TRUE);
}

GtkWidget *
sn_image_menu_item_new (void)
{
  return g_object_new (SN_TYPE_IMAGE_MENU_ITEM, NULL);
}

void
sn_image_menu_item_set_image_from_icon_name (SnImageMenuItem *item,
                                             const gchar     *icon_name)
{
  GtkImage *image;

  image = GTK_IMAGE (item->image);

  gtk_image_set_from_icon_name (image, icon_name, GTK_ICON_SIZE_MENU);
  gtk_image_set_pixel_size (image, 16);
  gtk_widget_show (item->image);
}

void
sn_image_menu_item_set_image_from_icon_pixbuf (SnImageMenuItem *item,
                                               GdkPixbuf       *pixbuf)
{
  gtk_image_set_from_pixbuf (GTK_IMAGE (item->image), pixbuf);
  gtk_widget_show (item->image);
}

void
sn_image_menu_item_unset_image (SnImageMenuItem *item)
{
  gtk_image_clear (GTK_IMAGE (item->image));
  gtk_widget_hide (item->image);
}
