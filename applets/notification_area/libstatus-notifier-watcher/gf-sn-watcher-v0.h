/*
 * Copyright (C) 2016 Alberts Muktupāvels
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

#ifndef GF_SN_WATCHER_V0_H
#define GF_SN_WATCHER_V0_H

#include "gf-sn-watcher-v0-gen.h"

G_BEGIN_DECLS

#define GF_TYPE_SN_WATCHER_V0     (gf_sn_watcher_v0_get_type ())
#define GF_SN_WATCHER_V0(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GF_TYPE_SN_WATCHER_V0, GfSnWatcherV0))
#define GF_IS_SN_WATCHER_V0(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GF_TYPE_SN_WATCHER_V0))

typedef struct _GfSnWatcherV0       GfSnWatcherV0;
typedef struct _GfSnWatcherV0Class  GfSnWatcherV0Class;

struct _GfSnWatcherV0Class
{
  GfSnWatcherV0GenSkeletonClass parent_class;
};

GType gf_sn_watcher_v0_get_type (void);
GfSnWatcherV0 *gf_sn_watcher_v0_new (void);

G_END_DECLS

#endif
