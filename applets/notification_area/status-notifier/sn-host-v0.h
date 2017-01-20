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

#ifndef SN_HOST_V0_H
#define SN_HOST_V0_H

#include "na-host.h"
#include "sn-host-v0-gen.h"

G_BEGIN_DECLS

#define SN_TYPE_HOST_V0     (sn_host_v0_get_type ())
#define SN_HOST_V0(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SN_TYPE_HOST_V0, SnHostV0))
#define SN_IS_HOST_V0(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SN_TYPE_HOST_V0))

typedef struct _SnHostV0      SnHostV0;
typedef struct _SnHostV0Class SnHostV0Class;

struct _SnHostV0Class
{
  SnHostV0GenSkeletonClass parent_class;
};

GType sn_host_v0_get_type (void);
NaHost *sn_host_v0_new (void);

G_END_DECLS

#endif
