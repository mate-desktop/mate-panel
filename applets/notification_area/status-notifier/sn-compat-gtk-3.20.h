/*
 * Copyright (C) 2016 Colomban Wendling <cwendling@hypra.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef SN_COMPAT_GTK_3_20_H
#define SN_COMPAT_GTK_3_20_H

#include <gtk/gtk.h>

#if ! GTK_CHECK_VERSION (3, 20, 0)

G_BEGIN_DECLS


/* WARNING: only meant for typical grabbing management */

#define GdkSeat GdkDisplay

#define gdk_display_get_default_seat(display) \
  (display)

static inline void
sn_compat_gdk_seat_ungrab (GdkSeat *seat)
{
#if GTK_CHECK_VERSION (3, 0, 0)
  GdkDeviceManager *manager;
  GdkDevice *device;

  manager = gdk_display_get_device_manager (seat);
  device = gdk_device_manager_get_client_pointer (manager);

  gdk_device_ungrab (device, GDK_CURRENT_TIME);
#else
  gdk_display_pointer_ungrab (seat, GDK_CURRENT_TIME);
#endif
}
#define gdk_seat_ungrab sn_compat_gdk_seat_ungrab


G_END_DECLS

#endif /* GTK version */
#endif
