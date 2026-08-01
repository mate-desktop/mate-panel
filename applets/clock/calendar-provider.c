/*
 * calendar-provider.c: abstract base class for calendar event sources
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

#include <config.h>
#include "calendar-provider.h"

enum
{
  APPOINTMENTS_CHANGED,
  TASKS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_ABSTRACT_TYPE (CalendarProvider, calendar_provider, G_TYPE_OBJECT)

static void
calendar_provider_class_init (CalendarProviderClass *klass)
{
  signals[APPOINTMENTS_CHANGED] =
    g_signal_new ("appointments-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CalendarProviderClass, appointments_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[TASKS_CHANGED] =
    g_signal_new ("tasks-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CalendarProviderClass, tasks_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
calendar_provider_init (CalendarProvider *provider)
{
  (void) provider;
}

/* --------------------------------------------------------------------------
 * Dispatch helpers
 * -------------------------------------------------------------------------- */

void
calendar_provider_select_month (CalendarProvider *provider,
                                guint             month,
                                guint             year)
{
  g_return_if_fail (CALENDAR_IS_PROVIDER (provider));

  CalendarProviderClass *klass = CALENDAR_PROVIDER_GET_CLASS (provider);
  if (klass->select_month)
    klass->select_month (provider, month, year);
}

void
calendar_provider_select_day (CalendarProvider *provider,
                              guint             day)
{
  g_return_if_fail (CALENDAR_IS_PROVIDER (provider));

  CalendarProviderClass *klass = CALENDAR_PROVIDER_GET_CLASS (provider);
  if (klass->select_day)
    klass->select_day (provider, day);
}

GSList *
calendar_provider_get_events (CalendarProvider *provider,
                              CalendarEventType event_mask,
                              time_t            start_time,
                              time_t            end_time)
{
  g_return_val_if_fail (CALENDAR_IS_PROVIDER (provider), NULL);

  CalendarProviderClass *klass = CALENDAR_PROVIDER_GET_CLASS (provider);
  if (klass->get_events)
    return klass->get_events (provider, event_mask, start_time, end_time);
  return NULL;
}

void
calendar_provider_set_task_completed (CalendarProvider *provider,
                                      const char       *task_uid,
                                      gboolean          task_completed,
                                      guint             percent_complete)
{
  g_return_if_fail (CALENDAR_IS_PROVIDER (provider));

  CalendarProviderClass *klass = CALENDAR_PROVIDER_GET_CLASS (provider);
  if (klass->set_task_completed)
    klass->set_task_completed (provider, task_uid, task_completed, percent_complete);
}

gboolean
calendar_provider_create_task (CalendarProvider *provider,
                               const char       *summary)
{
  g_return_val_if_fail (CALENDAR_IS_PROVIDER (provider), FALSE);

  CalendarProviderClass *klass = CALENDAR_PROVIDER_GET_CLASS (provider);
  if (klass->create_task)
    return klass->create_task (provider, summary);
  return FALSE;
}

void
calendar_provider_emit_appointments_changed (CalendarProvider *provider)
{
  g_signal_emit (provider, signals[APPOINTMENTS_CHANGED], 0);
}

void
calendar_provider_emit_tasks_changed (CalendarProvider *provider)
{
  g_signal_emit (provider, signals[TASKS_CHANGED], 0);
}

/* --------------------------------------------------------------------------
 * Shared filter utility
 * --------------------------------------------------------------------------
 *
 * Given a hash-table mapping uid strings to CalendarEvent* objects (cached
 * for a whole month), return individually-copied events whose occurrences
 * overlap the half-open interval [start_time, end_time).
 *
 * For CALENDAR_EVENT_APPOINTMENT the event is expanded: one copy is emitted
 * per overlapping occurrence, with start_time/end_time set to that specific
 * occurrence.  For CALENDAR_EVENT_TASK the event is returned as-is (one copy).
 */
GSList *
calendar_provider_filter_events_by_range (GHashTable        *events,
                                          CalendarEventType  event_mask,
                                          time_t             start_time,
                                          time_t             end_time)
{
  GSList *result = NULL;
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (events != NULL, NULL);

  g_hash_table_iter_init (&iter, events);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      CalendarEvent *event = (CalendarEvent *) value;

      if (event->type == CALENDAR_EVENT_APPOINTMENT &&
          (event_mask & CALENDAR_EVENT_APPOINTMENT))
        {
          CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
          /* Temporarily detach the occurrence list so calendar_event_copy()
           * doesn't deep-copy it; we set per-occurrence times manually. */
          GSList *occurrences    = appt->occurrences;
          appt->occurrences      = NULL;

          for (GSList *l = occurrences; l != NULL; l = l->next)
            {
              CalendarOccurrence *occ = l->data;

              if ((occ->start_time >= start_time && occ->start_time < end_time) ||
                  (occ->start_time <= start_time && (occ->end_time - 1) > start_time))
                {
                  CalendarEvent *copy = calendar_event_copy (event);
                  CALENDAR_APPOINTMENT (copy)->start_time = occ->start_time;
                  CALENDAR_APPOINTMENT (copy)->end_time   = occ->end_time;
                  result = g_slist_prepend (result, copy);
                }
            }

          appt->occurrences = occurrences;
        }
      else if (event->type == CALENDAR_EVENT_TASK &&
               (event_mask & CALENDAR_EVENT_TASK))
        {
          result = g_slist_prepend (result, calendar_event_copy (event));
        }
    }

  return result;
}
