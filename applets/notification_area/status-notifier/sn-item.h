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

#ifndef SN_ITEM_H
#define SN_ITEM_H

#include <gtk/gtk.h>

#include "sn-flat-button.h"

G_BEGIN_DECLS

#define SN_TYPE_ITEM            (sn_item_get_type ())
#define SN_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SN_TYPE_ITEM, SnItem))
#define SN_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SN_TYPE_ITEM, SnItemClass))
#define SN_IS_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SN_TYPE_ITEM))
#define SN_IS_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SN_TYPE_ITEM))
#define SN_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SN_TYPE_ITEM, SnItemClass))

typedef enum
{
  SN_ITEM_ORIENTATION_HORIZONTAL,
  SN_ITEM_ORIENTATION_VERTICAL
} SnItemOrientation;

typedef struct _SnItem        SnItem;
typedef struct _SnItemPrivate SnItemPrivate;
typedef struct _SnItemClass   SnItemClass;

struct _SnItem
{
  SnFlatButtonClass parent_instance;

  SnItemPrivate *priv;
};

struct _SnItemClass
{
  SnFlatButtonClass parent_class;

  void          (* ready)              (SnItem            *item);

  const gchar * (* get_id)             (SnItem            *item);

  const gchar * (* get_category)       (SnItem            *item);

  const gchar * (* get_menu)           (SnItem            *item);

  void          (* context_menu)       (SnItem            *item,
                                        gint               x,
                                        gint               y);

  void          (* activate)           (SnItem            *item,
                                        gint               x,
                                        gint               y);

  void          (* secondary_activate) (SnItem            *item,
                                        gint               x,
                                        gint               y);

  void          (* scroll)             (SnItem            *item,
                                        gint               delta,
                                        SnItemOrientation  orientation);
};

GType           sn_item_get_type        (void);
const gchar    *sn_item_get_bus_name    (SnItem *item);
const gchar    *sn_item_get_object_path (SnItem *item);

void            sn_item_emit_ready      (SnItem *item);

G_END_DECLS

#endif
