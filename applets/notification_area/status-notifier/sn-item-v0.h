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

#ifndef SN_ITEM_V0_H
#define SN_ITEM_V0_H

#include "sn-item.h"

G_BEGIN_DECLS

#define SN_TYPE_ITEM_V0 sn_item_v0_get_type ()
G_DECLARE_FINAL_TYPE (SnItemV0, sn_item_v0, SN, ITEM_V0, SnItem)

SnItem *sn_item_v0_new (const gchar *bus_name,
                        const gchar *object_path);

gint sn_item_v0_get_icon_size (SnItemV0 *v0);
void sn_item_v0_set_icon_size (SnItemV0 *v0,
                               gint size);

G_END_DECLS

#endif
