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

#ifndef SN_COMPAT_GTK_3_16_H
#define SN_COMPAT_GTK_3_16_H

#include <gtk/gtk.h>

#if ! GTK_CHECK_VERSION (3, 16, 0)

G_BEGIN_DECLS


static inline void
sn_compat_gtk_label_set_xalign (GtkLabel *label,
                                gfloat    xalign)
{
  g_object_set (label, "xalign", xalign, NULL);
}
#define gtk_label_set_xalign sn_compat_gtk_label_set_xalign


G_END_DECLS

#endif /* GTK version */
#endif
