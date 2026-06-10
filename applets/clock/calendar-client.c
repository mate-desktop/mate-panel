/*
 * calendar-client.c: aggregator that merges events from multiple providers
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
 *
 * Authors:
 *     Mark McLoughlin  <mark@skynet.ie>
 *     William Jon McCann  <mccann@jhu.edu>
 *     Martin Grimme  <martin@pycage.de>
 *     Christian Kellner  <gicmo@xatom.net>
 */

#include <config.h>

#include <string.h>
#include <time.h>

#include <glib.h>
#include <gio/gio.h>

#include "calendar-client.h"
#include "calendar-provider.h"

#ifdef HAVE_LIBICAL
#  include "calendar-vdir-provider.h"
#endif

#ifdef HAVE_EDS
#  include "calendar-eds-provider.h"
#endif

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

/* GSettings key for extra vdir calendar paths */
#define KEY_VDIR_CALENDAR_PATHS "vdir-calendar-paths"

/* Default vdirsyncer storage base directory (relative to XDG data home) */
#define VDIRSYNCER_DEFAULT_SUBDIR "vdirsyncer"

struct _CalendarClientPrivate
{
  GSList *providers;   /* list of CalendarProvider * (owned, refcounted) */

  guint   day;
  guint   month;
  guint   year;
};

static void calendar_client_finalize     (GObject      *object);
static void calendar_client_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec);
static void calendar_client_get_property (GObject      *object,
                                          guint         prop_id,
                                          GValue       *value,
                                          GParamSpec   *pspec);

enum
{
  PROP_O,
  PROP_DAY,
  PROP_MONTH,
  PROP_YEAR
};

enum
{
  APPOINTMENTS_CHANGED,
  TASKS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (CalendarClient, calendar_client, G_TYPE_OBJECT)

/* =========================================================================
 * CalendarEvent copy / free  (the structs live in calendar-client.h, but the
 * copy/free helpers are here since they need access to the private type layout)
 * ========================================================================= */

static void
calendar_appointment_copy (CalendarAppointment *src, CalendarAppointment *dst)
{
  dst->occurrences = g_slist_copy (src->occurrences);
  for (GSList *l = dst->occurrences; l != NULL; l = l->next)
    {
      CalendarOccurrence *o     = l->data;
      CalendarOccurrence *copy  = g_new0 (CalendarOccurrence, 1);
      copy->start_time = o->start_time;
      copy->end_time   = o->end_time;
      l->data          = copy;
    }

  dst->uid          = g_strdup (src->uid);
  dst->rid          = g_strdup (src->rid);
  dst->backend_name = g_strdup (src->backend_name);
  dst->summary      = g_strdup (src->summary);
  dst->description  = g_strdup (src->description);
  dst->color_string = g_strdup (src->color_string);
  dst->start_time   = src->start_time;
  dst->end_time     = src->end_time;
  dst->is_all_day   = src->is_all_day;
}

static void
calendar_appointment_finalize (CalendarAppointment *appt)
{
  for (GSList *l = appt->occurrences; l != NULL; l = l->next)
    g_free (l->data);
  g_slist_free (appt->occurrences);
  appt->occurrences = NULL;

  g_free (appt->uid);
  g_free (appt->rid);
  g_free (appt->backend_name);
  g_free (appt->summary);
  g_free (appt->description);
  g_free (appt->color_string);
}

static void
calendar_task_copy (CalendarTask *src, CalendarTask *dst)
{
  dst->uid              = g_strdup (src->uid);
  dst->summary          = g_strdup (src->summary);
  dst->description      = g_strdup (src->description);
  dst->color_string     = g_strdup (src->color_string);
  dst->url              = g_strdup (src->url);
  dst->start_time       = src->start_time;
  dst->due_time         = src->due_time;
  dst->percent_complete = src->percent_complete;
  dst->completed_time   = src->completed_time;
  dst->priority         = src->priority;
}

static void
calendar_task_finalize (CalendarTask *task)
{
  g_free (task->uid);
  g_free (task->summary);
  g_free (task->description);
  g_free (task->color_string);
  g_free (task->url);
}

CalendarEvent *
calendar_event_copy (CalendarEvent *event)
{
  if (!event)
    return NULL;

  CalendarEvent *copy = g_new0 (CalendarEvent, 1);
  copy->type = event->type;

  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      calendar_appointment_copy (CALENDAR_APPOINTMENT (event),
                                  CALENDAR_APPOINTMENT (copy));
      break;
    case CALENDAR_EVENT_TASK:
      calendar_task_copy (CALENDAR_TASK (event), CALENDAR_TASK (copy));
      break;
    default:
      g_assert_not_reached ();
    }

  return copy;
}

gboolean
calendar_event_equal (CalendarEvent *a, CalendarEvent *b)
{
  if (!a && !b) return TRUE;
  if (!a || !b) return FALSE;
  if (a->type != b->type) return FALSE;

  if (a->type == CALENDAR_EVENT_APPOINTMENT)
    {
      CalendarAppointment *aa = CALENDAR_APPOINTMENT (a);
      CalendarAppointment *ab = CALENDAR_APPOINTMENT (b);

      if (g_slist_length (aa->occurrences) != g_slist_length (ab->occurrences))
        return FALSE;
      for (GSList *la = aa->occurrences, *lb = ab->occurrences;
           la && lb; la = la->next, lb = lb->next)
        {
          CalendarOccurrence *oa = la->data;
          CalendarOccurrence *ob = lb->data;
          if (oa->start_time != ob->start_time || oa->end_time != ob->end_time)
            return FALSE;
        }
      return g_strcmp0 (aa->uid,          ab->uid)          == 0 &&
             g_strcmp0 (aa->backend_name, ab->backend_name) == 0 &&
             g_strcmp0 (aa->summary,      ab->summary)      == 0 &&
             g_strcmp0 (aa->description,  ab->description)  == 0 &&
             g_strcmp0 (aa->color_string, ab->color_string) == 0 &&
             aa->start_time == ab->start_time                    &&
             aa->end_time   == ab->end_time                      &&
             aa->is_all_day == ab->is_all_day;
    }

  if (a->type == CALENDAR_EVENT_TASK)
    {
      CalendarTask *ta = CALENDAR_TASK (a);
      CalendarTask *tb = CALENDAR_TASK (b);
      return g_strcmp0 (ta->uid,          tb->uid)          == 0 &&
             g_strcmp0 (ta->summary,      tb->summary)      == 0 &&
             g_strcmp0 (ta->description,  tb->description)  == 0 &&
             g_strcmp0 (ta->color_string, tb->color_string) == 0 &&
             ta->start_time       == tb->start_time               &&
             ta->due_time         == tb->due_time                 &&
             ta->percent_complete == tb->percent_complete         &&
             ta->completed_time   == tb->completed_time           &&
             ta->priority         == tb->priority;
    }

  g_assert_not_reached ();
  return FALSE;
}

void
calendar_event_free (CalendarEvent *event)
{
  if (!event) return;

  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      calendar_appointment_finalize (CALENDAR_APPOINTMENT (event));
      break;
    case CALENDAR_EVENT_TASK:
      calendar_task_finalize (CALENDAR_TASK (event));
      break;
    default:
      g_assert_not_reached ();
    }

  g_free (event);
}

/* =========================================================================
 * GObject boilerplate
 * ========================================================================= */

static void
calendar_client_class_init (CalendarClientClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize     = calendar_client_finalize;
  gobject_class->set_property = calendar_client_set_property;
  gobject_class->get_property = calendar_client_get_property;

  g_object_class_install_property (gobject_class, PROP_DAY,
    g_param_spec_uint ("day", "Day",
                       "Currently monitored day (1–31; 0 = unset)",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MONTH,
    g_param_spec_uint ("month", "Month",
                       "Currently monitored month (0–11)",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_YEAR,
    g_param_spec_uint ("year", "Year",
                       "Currently monitored year",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));

  signals[APPOINTMENTS_CHANGED] =
    g_signal_new ("appointments-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CalendarClientClass, appointments_changed),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[TASKS_CHANGED] =
    g_signal_new ("tasks-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CalendarClientClass, tasks_changed),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
calendar_client_init (CalendarClient *client)
{
  client->priv = calendar_client_get_instance_private (client);
  client->priv->day   = G_MAXUINT;
  client->priv->month = G_MAXUINT;
  client->priv->year  = G_MAXUINT;
}

static void
calendar_client_finalize (GObject *object)
{
  CalendarClient *client = CALENDAR_CLIENT (object);

  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    g_object_unref (l->data);
  g_slist_free (client->priv->providers);
  client->priv->providers = NULL;

  G_OBJECT_CLASS (calendar_client_parent_class)->finalize (object);
}

static void
calendar_client_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  CalendarClient *client = CALENDAR_CLIENT (object);
  switch (prop_id)
    {
    case PROP_DAY:
      calendar_client_select_day (client, g_value_get_uint (value));
      break;
    case PROP_MONTH:
      calendar_client_select_month (client, g_value_get_uint (value),
                                    client->priv->year);
      break;
    case PROP_YEAR:
      calendar_client_select_month (client, client->priv->month,
                                    g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
calendar_client_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  CalendarClient *client = CALENDAR_CLIENT (object);
  switch (prop_id)
    {
    case PROP_DAY:   g_value_set_uint (value, client->priv->day);   break;
    case PROP_MONTH: g_value_set_uint (value, client->priv->month); break;
    case PROP_YEAR:  g_value_set_uint (value, client->priv->year);  break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* =========================================================================
 * Provider management (internal)
 * ========================================================================= */

static void
on_provider_appointments_changed (CalendarProvider *provider,
                                   CalendarClient   *client)
{
  (void) provider;
  g_signal_emit (client, signals[APPOINTMENTS_CHANGED], 0);
}

static void
on_provider_tasks_changed (CalendarProvider *provider,
                            CalendarClient   *client)
{
  (void) provider;
  g_signal_emit (client, signals[TASKS_CHANGED], 0);
}

static void
calendar_client_add_provider (CalendarClient   *client,
                               CalendarProvider *provider)
{
  g_return_if_fail (CALENDAR_IS_PROVIDER (provider));

  client->priv->providers =
    g_slist_prepend (client->priv->providers, g_object_ref (provider));

  g_signal_connect (provider, "appointments-changed",
                    G_CALLBACK (on_provider_appointments_changed), client);
  g_signal_connect (provider, "tasks-changed",
                    G_CALLBACK (on_provider_tasks_changed), client);
}

/* =========================================================================
 * Auto-discovery helpers
 * ========================================================================= */

#ifdef HAVE_LIBICAL
static void
add_vdir_providers_from_path (CalendarClient *client, const char *path)
{
  GSList *providers = calendar_vdir_discover (path);
  for (GSList *l = providers; l != NULL; l = l->next)
    {
      calendar_client_add_provider (client, CALENDAR_PROVIDER (l->data));
      g_object_unref (l->data);
    }
  g_slist_free (providers);
}
#endif /* HAVE_LIBICAL */

/* =========================================================================
 * Public constructor
 * ========================================================================= */

CalendarClient *
calendar_client_new (GSettings *settings)
{
  CalendarClient *client = g_object_new (CALENDAR_TYPE_CLIENT, NULL);

#ifdef HAVE_EDS
  {
    CalendarProvider *eds = calendar_eds_provider_new (settings);
    if (eds != NULL)
      {
        calendar_client_add_provider (client, eds);
        g_object_unref (eds);
      }
  }
#endif /* HAVE_EDS */

#ifdef HAVE_LIBICAL
  {
    /* Auto-discover vdirsyncer's default storage path */
    const char *data_home = g_get_user_data_dir ();
    char *vdir_base = g_build_filename (data_home, VDIRSYNCER_DEFAULT_SUBDIR, NULL);
    add_vdir_providers_from_path (client, vdir_base);
    g_free (vdir_base);

    /* Additional paths from GSettings (if the key exists in @settings) */
    if (settings != NULL)
      {
        /* Check whether this settings instance has the vdir key before
         * calling get_strv (avoids a GLib critical if key is absent). */
        gchar **all_keys  = g_settings_list_keys (settings);
        gboolean has_key  = FALSE;
        for (int ki = 0; all_keys[ki] != NULL; ki++)
          if (g_strcmp0 (all_keys[ki], KEY_VDIR_CALENDAR_PATHS) == 0)
            { has_key = TRUE; break; }
        g_strfreev (all_keys);

        if (has_key)
          {
            gchar **paths = g_settings_get_strv (settings, KEY_VDIR_CALENDAR_PATHS);
            for (gint i = 0; paths[i] != NULL; i++)
              {
                CalendarProvider *p = calendar_vdir_provider_new (paths[i]);
                if (p != NULL)
                  {
                    calendar_client_add_provider (client, p);
                    g_object_unref (p);
                  }
              }
            g_strfreev (paths);
          } /* if (has_key) */
      } /* if (settings != NULL) */
  } /* HAVE_LIBICAL block */
#endif /* HAVE_LIBICAL */

  (void) settings; /* suppress warning when both EDS and libical are absent */
  return client;
}

/* =========================================================================
 * Date selection
 * ========================================================================= */

void
calendar_client_get_date (CalendarClient *client,
                           guint          *year,
                           guint          *month,
                           guint          *day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));

  if (year)  *year  = client->priv->year;
  if (month) *month = client->priv->month;
  if (day)   *day   = client->priv->day;
}

void
calendar_client_select_month (CalendarClient *client,
                               guint           month,
                               guint           year)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (month <= 11);

  if (client->priv->month == month && client->priv->year == year)
    return;

  client->priv->month = month;
  client->priv->year  = year;

  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    calendar_provider_select_month (CALENDAR_PROVIDER (l->data), month, year);

  g_object_freeze_notify (G_OBJECT (client));
  g_object_notify (G_OBJECT (client), "month");
  g_object_notify (G_OBJECT (client), "year");
  g_object_thaw_notify (G_OBJECT (client));
}

void
calendar_client_select_day (CalendarClient *client, guint day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (day <= 31);

  if (client->priv->day == day)
    return;

  client->priv->day = day;

  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    calendar_provider_select_day (CALENDAR_PROVIDER (l->data), day);

  g_object_notify (G_OBJECT (client), "day");
}

/* =========================================================================
 * Time range helpers
 * ========================================================================= */

static inline time_t
make_time_for_day_begin (int day, int month, int year)
{
  struct tm tm = { 0, };
  tm.tm_mday  = day;
  tm.tm_mon   = month;
  tm.tm_year  = year - 1900;
  tm.tm_isdst = -1;
  return mktime (&tm);
}

/* =========================================================================
 * Event retrieval
 * ========================================================================= */

/* Explicit update triggers (kept for backward compatibility with
 * calendar-window.c which calls them after certain state changes) */
void
calendar_client_update_appointments (CalendarClient *client)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));

  if (client->priv->month == G_MAXUINT || client->priv->year == G_MAXUINT)
    return;

  /* Re-issue select_month so providers re-run their queries */
  guint month = client->priv->month;
  guint year  = client->priv->year;
  client->priv->month = G_MAXUINT; /* force change detection */
  calendar_client_select_month (client, month, year);
}

void
calendar_client_update_tasks (CalendarClient *client)
{
  calendar_client_update_appointments (client); /* providers refresh both */
}

GSList *
calendar_client_get_events (CalendarClient    *client,
                             CalendarEventType  event_mask)
{
  g_return_val_if_fail (CALENDAR_IS_CLIENT (client), NULL);
  g_return_val_if_fail (client->priv->day   != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->month != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->year  != G_MAXUINT, NULL);

  time_t day_begin = make_time_for_day_begin ((int) client->priv->day,
                                               (int) client->priv->month,
                                               (int) client->priv->year);
  time_t day_end   = make_time_for_day_begin ((int) client->priv->day + 1,
                                               (int) client->priv->month,
                                               (int) client->priv->year);

  GSList *result = NULL;
  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    {
      GSList *events =
        calendar_provider_get_events (CALENDAR_PROVIDER (l->data),
                                       event_mask, day_begin, day_end);
      result = g_slist_concat (result, events);
    }

  return result;
}

static inline int
day_from_time_t (time_t t)
{
  struct tm *tm = localtime (&t);
  return (tm && tm->tm_mday >= 1 && tm->tm_mday <= 31) ? tm->tm_mday : 0;
}

void
calendar_client_foreach_appointment_day (CalendarClient  *client,
                                          CalendarDayIter  iter_func,
                                          gpointer         user_data)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (iter_func != NULL);
  g_return_if_fail (client->priv->month != G_MAXUINT);
  g_return_if_fail (client->priv->year  != G_MAXUINT);

  time_t month_begin = make_time_for_day_begin (1,
                                                 (int) client->priv->month,
                                                 (int) client->priv->year);
  time_t month_end   = make_time_for_day_begin (1,
                                                 (int) client->priv->month + 1,
                                                 (int) client->priv->year);

  /* Collect appointments for the entire month from all providers */
  GSList *all_appts = NULL;
  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    {
      GSList *events =
        calendar_provider_get_events (CALENDAR_PROVIDER (l->data),
                                       CALENDAR_EVENT_APPOINTMENT,
                                       month_begin, month_end);
      all_appts = g_slist_concat (all_appts, events);
    }

  /* Mark days */
  gboolean marked_days[32] = { FALSE, };

  for (GSList *l = all_appts; l != NULL; l = l->next)
    {
      CalendarAppointment *appt = CALENDAR_APPOINTMENT (l->data);

      if (!appt->start_time)
        continue;

      time_t day_time = appt->start_time;
      if (day_time >= month_begin)
        marked_days[day_from_time_t (day_time)] = TRUE;

      if (appt->end_time)
        {
          int duration = (int)(appt->end_time - appt->start_time);
          for (int offset = 1;
               offset <= duration / 86400 && duration != offset * 86400;
               offset++)
            {
              time_t day_tm = appt->start_time + offset * 86400;
              if (day_tm > month_end)
                break;
              if (day_tm >= month_begin)
                marked_days[day_from_time_t (day_tm)] = TRUE;
            }
        }

      calendar_event_free (CALENDAR_EVENT (l->data));
    }
  g_slist_free (all_appts);

  for (int i = 1; i < 32; i++)
    if (marked_days[i])
      iter_func (client, (guint) i, user_data);
}

/* =========================================================================
 * Task mutation (delegated to the first provider that supports it)
 * ========================================================================= */

void
calendar_client_set_task_completed (CalendarClient *client,
                                     char           *task_uid,
                                     gboolean        task_completed,
                                     guint           percent_complete)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (task_uid != NULL);

  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    {
      CalendarProviderClass *klass =
        CALENDAR_PROVIDER_GET_CLASS (CALENDAR_PROVIDER (l->data));
      if (klass->set_task_completed != NULL)
        {
          calendar_provider_set_task_completed (CALENDAR_PROVIDER (l->data),
                                                 task_uid,
                                                 task_completed,
                                                 percent_complete);
          return;
        }
    }
}

gboolean
calendar_client_create_task (CalendarClient *client,
                              const char     *summary)
{
  g_return_val_if_fail (CALENDAR_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (summary != NULL && *summary != '\0', FALSE);

  for (GSList *l = client->priv->providers; l != NULL; l = l->next)
    {
      CalendarProviderClass *klass =
        CALENDAR_PROVIDER_GET_CLASS (CALENDAR_PROVIDER (l->data));
      if (klass->create_task != NULL)
        return calendar_provider_create_task (CALENDAR_PROVIDER (l->data), summary);
    }

  return FALSE;
}
