/*
 * calendar-provider.h: abstract base class for calendar event sources
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

#ifndef __CALENDAR_PROVIDER_H__
#define __CALENDAR_PROVIDER_H__

#include <glib-object.h>
#include "calendar-client.h"

G_BEGIN_DECLS

#define CALENDAR_TYPE_PROVIDER          (calendar_provider_get_type ())
#define CALENDAR_PROVIDER(o)            (G_TYPE_CHECK_INSTANCE_CAST ((o), CALENDAR_TYPE_PROVIDER, CalendarProvider))
#define CALENDAR_PROVIDER_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST ((k), CALENDAR_TYPE_PROVIDER, CalendarProviderClass))
#define CALENDAR_IS_PROVIDER(o)         (G_TYPE_CHECK_INSTANCE_TYPE ((o), CALENDAR_TYPE_PROVIDER))
#define CALENDAR_IS_PROVIDER_CLASS(k)   (G_TYPE_CHECK_CLASS_TYPE ((k), CALENDAR_TYPE_PROVIDER))
#define CALENDAR_PROVIDER_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), CALENDAR_TYPE_PROVIDER, CalendarProviderClass))

typedef struct _CalendarProvider      CalendarProvider;
typedef struct _CalendarProviderClass CalendarProviderClass;

struct _CalendarProvider
{
  GObject parent;
};

struct _CalendarProviderClass
{
  GObjectClass parent_class;

  /* Called when the user navigates to a different month.
   * Subclasses should refresh their event cache for this month/year
   * and emit appointments-changed / tasks-changed when ready. */
  void      (*select_month)       (CalendarProvider *provider,
                                   guint             month,
                                   guint             year);

  /* Called when the user selects a specific day.  Subclasses that
   * cache by month (the common case) can ignore this. */
  void      (*select_day)         (CalendarProvider *provider,
                                   guint             day);

  /* Return a GSList of newly-allocated CalendarEvent* copies whose
   * occurrences overlap the half-open interval [start_time, end_time).
   * event_mask is a bitmask of CalendarEventType values. */
  GSList   *(*get_events)         (CalendarProvider *provider,
                                   CalendarEventType event_mask,
                                   time_t            start_time,
                                   time_t            end_time);

  /* Optional write-back operations.  Providers that do not support
   * mutation should leave these NULL. */
  void      (*set_task_completed) (CalendarProvider *provider,
                                   const char       *task_uid,
                                   gboolean          task_completed,
                                   guint             percent_complete);
  gboolean  (*create_task)        (CalendarProvider *provider,
                                   const char       *summary);

  /* Signals */
  void      (*appointments_changed) (CalendarProvider *provider);
  void      (*tasks_changed)        (CalendarProvider *provider);
};

GType    calendar_provider_get_type            (void) G_GNUC_CONST;

/* Dispatch helpers — call the corresponding virtual method */
void     calendar_provider_select_month        (CalendarProvider *provider,
                                                guint             month,
                                                guint             year);
void     calendar_provider_select_day          (CalendarProvider *provider,
                                                guint             day);
GSList  *calendar_provider_get_events          (CalendarProvider *provider,
                                                CalendarEventType event_mask,
                                                time_t            start_time,
                                                time_t            end_time);
void     calendar_provider_set_task_completed  (CalendarProvider *provider,
                                                const char       *task_uid,
                                                gboolean          task_completed,
                                                guint             percent_complete);
gboolean calendar_provider_create_task         (CalendarProvider *provider,
                                                const char       *summary);

/* For use by subclass implementations to fire signals */
void     calendar_provider_emit_appointments_changed (CalendarProvider *provider);
void     calendar_provider_emit_tasks_changed        (CalendarProvider *provider);

/* Shared filter utility: given a hash table (uid → CalendarEvent*) containing
 * events cached for a whole month, return copies of those that overlap
 * [start_time, end_time).  Callers own the returned list and its elements. */
GSList  *calendar_provider_filter_events_by_range (GHashTable        *events,
                                                   CalendarEventType  event_mask,
                                                   time_t             start_time,
                                                   time_t             end_time);

G_END_DECLS

#endif /* __CALENDAR_PROVIDER_H__ */
