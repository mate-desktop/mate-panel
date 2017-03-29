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

#define SN_TYPE_ITEM_V0     (sn_item_v0_get_type ())
#define SN_ITEM_V0(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SN_TYPE_ITEM_V0, SnItemV0))
#define SN_IS_ITEM_V0(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SN_TYPE_ITEM_V0))

typedef struct _SnItemV0      SnItemV0;
typedef struct _SnItemV0Class SnItemV0Class;

struct _SnItemV0Class
{
  SnItemClass parent_class;
};

GType sn_item_v0_get_type (void);

SnItem *sn_item_v0_new (const gchar *bus_name,
                        const gchar *object_path);

gint sn_item_v0_get_icon_padding (SnItemV0 *v0);
void sn_item_v0_set_icon_padding (SnItemV0 *v0,
                                  gint padding);
gint sn_item_v0_get_icon_size (SnItemV0 *v0);
void sn_item_v0_set_icon_size (SnItemV0 *v0,
                               gint size);

G_END_DECLS

#endif
