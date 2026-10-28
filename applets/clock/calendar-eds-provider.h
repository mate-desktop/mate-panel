/*
 * calendar-eds-provider.h: Evolution Data Server calendar source
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

#ifndef __CALENDAR_EDS_PROVIDER_H__
#define __CALENDAR_EDS_PROVIDER_H__

#include "calendar-provider.h"

G_BEGIN_DECLS

#define CALENDAR_TYPE_EDS_PROVIDER         (calendar_eds_provider_get_type ())
#define CALENDAR_EDS_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CALENDAR_TYPE_EDS_PROVIDER, CalendarEDSProvider))
#define CALENDAR_EDS_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), CALENDAR_TYPE_EDS_PROVIDER, CalendarEDSProviderClass))
#define CALENDAR_IS_EDS_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CALENDAR_TYPE_EDS_PROVIDER))
#define CALENDAR_IS_EDS_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CALENDAR_TYPE_EDS_PROVIDER))
#define CALENDAR_EDS_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CALENDAR_TYPE_EDS_PROVIDER, CalendarEDSProviderClass))

typedef struct _CalendarEDSProvider        CalendarEDSProvider;
typedef struct _CalendarEDSProviderClass   CalendarEDSProviderClass;
typedef struct _CalendarEDSProviderPrivate CalendarEDSProviderPrivate;

struct _CalendarEDSProvider
{
  CalendarProvider            parent;
  CalendarEDSProviderPrivate *priv;
};

struct _CalendarEDSProviderClass
{
  CalendarProviderClass parent_class;
};

GType             calendar_eds_provider_get_type (void) G_GNUC_CONST;

/* @settings may be NULL; if so the provider checks for Evolution's own
 * GSettings schema and falls back to UTC if unavailable. */
CalendarProvider *calendar_eds_provider_new      (GSettings *settings);

G_END_DECLS

#endif /* __CALENDAR_EDS_PROVIDER_H__ */
