/*
 * calendar-vdir-provider.h: vdir storage calendar source
 *
 * Copyright (C) 2004 Free Software Foundation, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CALENDAR_VDIR_PROVIDER_H__
#define __CALENDAR_VDIR_PROVIDER_H__

#include "calendar-provider.h"

G_BEGIN_DECLS

#define CALENDAR_TYPE_VDIR_PROVIDER         (calendar_vdir_provider_get_type ())
#define CALENDAR_VDIR_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CALENDAR_TYPE_VDIR_PROVIDER, CalendarVdirProvider))
#define CALENDAR_VDIR_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), CALENDAR_TYPE_VDIR_PROVIDER, CalendarVdirProviderClass))
#define CALENDAR_IS_VDIR_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CALENDAR_TYPE_VDIR_PROVIDER))
#define CALENDAR_IS_VDIR_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CALENDAR_TYPE_VDIR_PROVIDER))
#define CALENDAR_VDIR_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CALENDAR_TYPE_VDIR_PROVIDER, CalendarVdirProviderClass))

typedef struct _CalendarVdirProvider        CalendarVdirProvider;
typedef struct _CalendarVdirProviderClass   CalendarVdirProviderClass;
typedef struct _CalendarVdirProviderPrivate CalendarVdirProviderPrivate;

struct _CalendarVdirProvider
{
  CalendarProvider             parent;
  CalendarVdirProviderPrivate *priv;
};

struct _CalendarVdirProviderClass
{
  CalendarProviderClass parent_class;
};

GType             calendar_vdir_provider_get_type (void) G_GNUC_CONST;

/* Create a provider for a single vdir collection directory.
 * Reads the optional 'displayname' and 'color' metadata files from the
 * directory.  Returns NULL if @directory does not exist. */
CalendarProvider *calendar_vdir_provider_new      (const char *directory);

/* Scan @base_path for vdir collections (subdirectories containing .ics
 * files or a displayname/color file) and return a GSList of newly-created
 * CalendarProvider* objects.  The caller owns the list and its elements. */
GSList           *calendar_vdir_discover          (const char *base_path);

G_END_DECLS

#endif /* __CALENDAR_VDIR_PROVIDER_H__ */
