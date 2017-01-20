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

#ifndef SN_DBUS_MENU_H
#define SN_DBUS_MENU_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_DBUS_MENU     (sn_dbus_menu_get_type ())
#define SN_DBUS_MENU(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SN_TYPE_DBUS_MENU, SnDBusMenu))
#define SN_IS_DBUS_MENU(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SN_TYPE_DBUS_MENU))

typedef struct _SnDBusMenu      SnDBusMenu;
typedef struct _SnDBusMenuClass SnDBusMenuClass;

struct _SnDBusMenuClass
{
  GtkMenuClass parent_class;
};

GType sn_dbus_menu_get_type (void);

GtkMenu *sn_dbus_menu_new (const gchar *bus_name,
                           const gchar *object_path);

G_END_DECLS

#endif
