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

#ifndef SN_IMAGE_MENU_ITEM_H
#define SN_IMAGE_MENU_ITEM_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_IMAGE_MENU_ITEM sn_image_menu_item_get_type ()
G_DECLARE_FINAL_TYPE (SnImageMenuItem, sn_image_menu_item,
                      SN, IMAGE_MENU_ITEM, GtkMenuItem)

GtkWidget *sn_image_menu_item_new                        (void);

void       sn_image_menu_item_set_image_from_icon_name   (SnImageMenuItem *item,
                                                          const gchar     *icon_name);

void       sn_image_menu_item_set_image_from_icon_pixbuf (SnImageMenuItem *item,
                                                          GdkPixbuf       *pixbuf);

void       sn_image_menu_item_unset_image                (SnImageMenuItem *item);

G_END_DECLS

#endif
