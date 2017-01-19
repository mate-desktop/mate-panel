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

#ifndef SN_DBUS_MENU_ITEM_H
#define SN_DUBS_MENU_ITEM_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct
{
  guint           key;
  GdkModifierType mask;
} SnShortcut;

typedef struct
{
  gchar       *accessible_desc;
  gchar       *children_display;
  gchar       *disposition;
  gboolean     enabled;
  gchar       *icon_name;
  GdkPixbuf   *icon_data;
  gchar       *label;
  SnShortcut **shortcuts;
  gchar       *toggle_type;
  gint32       toggle_state;
  gchar       *type;
  gboolean     visible;

  GtkWidget   *item;
  GtkMenu     *submenu;

  gulong       activate_id;
} SnDBusMenuItem;

SnDBusMenuItem *sn_dbus_menu_item_new          (GVariant       *props);

void            sn_dubs_menu_item_free         (gpointer        data);

void            sn_dbus_menu_item_update_props (SnDBusMenuItem *item,
                                                GVariant       *props);

void            sn_dbus_menu_item_remove_props (SnDBusMenuItem *item,
                                                GVariant       *props);

G_END_DECLS

#endif
