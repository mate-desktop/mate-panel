/*
 * calendar-eds-provider.c: Evolution Data Server calendar source
 *
 * Absorbs the functionality previously split between calendar-sources.c
 * (ESourceRegistry management) and the EDS-specific internals of
 * calendar-client.c (query dispatch, instance generation, filtering).
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

#include <string.h>
#include <libintl.h>
#define HANDLE_LIBICAL_MEMORY

#include <libecal/libecal.h>

#include "calendar-eds-provider.h"
#include "system-timezone.h"

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

#ifndef _
#define _(x) gettext(x)
#endif

/* =========================================================================
 * Internal types
 * =========================================================================
 *
 * One CalendarEDSQuery holds the event hash-table for a single query pass.
 * One CalendarEDSSource wraps a single ECalClient and owns two queries
 * (completed = current results, in_progress = next results being built).
 * Both queries are used only while calendar_eds_source_run_query() executes
 * — after it returns, in_progress is swapped into completed synchronously.
 */

typedef struct
{
  GHashTable *events;   /* uid_string → CalendarEvent *; owned */
} CalendarEDSQuery;

typedef struct
{
  CalendarEDSProvider *provider;
  ECalClient          *client;
  gulong               backend_died_id;

  CalendarEDSQuery     completed_query;
  CalendarEDSQuery     in_progress_query;

  guint                query_completed  : 1;
  guint                query_in_progress: 1;

  /* Which provider signal to emit when events change */
  gboolean             is_appointments; /* TRUE=appointments, FALSE=tasks */
} CalendarEDSSource;

struct _CalendarEDSProviderPrivate
{
  ESourceRegistry *registry;
  gulong           source_added_id;
  gulong           source_changed_id;
  gulong           source_removed_id;

  /* ESource* → CalendarEDSSource* */
  GHashTable      *appointment_clients;
  GHashTable      *task_clients;

  ICalTimezone    *zone;
  GSettings       *calendar_settings;
  gulong           zone_listener;

  guint            month;  /* 0–11; G_MAXUINT = unset */
  guint            year;   /* e.g. 2025; G_MAXUINT = unset */
};

G_DEFINE_TYPE_WITH_PRIVATE (CalendarEDSProvider,
                             calendar_eds_provider,
                             CALENDAR_TYPE_PROVIDER)

/* Forward declarations */
static void eds_load_source_list (CalendarEDSProvider *self, gboolean is_appointments);
static void eds_run_query        (CalendarEDSProvider *self, gboolean is_appointments);
static void backend_died_cb      (EClient *client, CalendarEDSSource *src);

/* =========================================================================
 * CalendarEDSQuery helpers
 * ========================================================================= */

static void
eds_query_init (CalendarEDSQuery *q)
{
  q->events = g_hash_table_new_full (g_str_hash, g_str_equal,
                                      g_free,
                                      (GDestroyNotify) calendar_event_free);
}

static void
eds_query_clear (CalendarEDSQuery *q)
{
  if (q->events)
    {
      g_hash_table_destroy (q->events);
      q->events = NULL;
    }
}

/* =========================================================================
 * CalendarEDSSource helpers
 * ========================================================================= */

static CalendarEDSSource *
eds_source_new (CalendarEDSProvider *provider,
                ECalClient          *client,
                gboolean             is_appointments)
{
  CalendarEDSSource *src = g_new0 (CalendarEDSSource, 1);
  src->provider        = provider;
  src->client          = g_object_ref (client);
  src->is_appointments = is_appointments;
  src->backend_died_id =
    g_signal_connect (client, "backend-died",
                      G_CALLBACK (backend_died_cb), src);
  return src;
}

static void
eds_source_free (CalendarEDSSource *src)
{
  if (src->backend_died_id)
    g_signal_handler_disconnect (src->client, src->backend_died_id);

  eds_query_clear (&src->completed_query);
  eds_query_clear (&src->in_progress_query);

  g_object_unref (src->client);
  g_free (src);
}

/* =========================================================================
 * ICalComponent → CalendarEvent (private helpers)
 * ========================================================================= */

static time_t
get_time_from_property (ICalComponent    *icomp,
                         ICalPropertyKind  prop_kind,
                         ICalTime        *(*get_prop_func)(ICalProperty *),
                         ICalTimezone     *default_zone)
{
  ICalProperty *prop =
    i_cal_component_get_first_property (icomp, prop_kind);
  if (!prop)
    return 0;

  ICalParameter *param =
    i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
  ICalTime *ical_time = get_prop_func (prop);
  g_object_unref (prop);

  ICalTimezone *timezone = NULL;
  if (param)
    {
      const char *tzid = i_cal_parameter_get_tzid (param);
      timezone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
      g_object_unref (param);
      if (!timezone)
        timezone = default_zone;
    }
  else if (i_cal_time_is_utc (ical_time))
    {
      timezone = i_cal_timezone_get_utc_timezone ();
    }
  else
    {
      timezone = default_zone;
    }

  time_t retval = i_cal_time_as_timet_with_zone (ical_time, timezone);
  g_object_unref (ical_time);
  return retval;
}

static char *
get_source_color (ECalClient *ecal)
{
  ESource           *source     = e_client_get_source (E_CLIENT (ecal));
  ECalClientSourceType stype    = e_cal_client_get_source_type (ecal);
  const char        *ext_name   =
    (stype == E_CAL_CLIENT_SOURCE_TYPE_EVENTS)
      ? E_SOURCE_EXTENSION_CALENDAR
      : E_SOURCE_EXTENSION_TASK_LIST;
  ESourceSelectable *ext        = e_source_get_extension (source, ext_name);
  return e_source_selectable_dup_color (ext);
}

static char *
get_source_backend_name (ECalClient *ecal)
{
  ESource           *source   = e_client_get_source (E_CLIENT (ecal));
  ECalClientSourceType stype  = e_cal_client_get_source_type (ecal);
  const char        *ext_name =
    (stype == E_CAL_CLIENT_SOURCE_TYPE_EVENTS)
      ? E_SOURCE_EXTENSION_CALENDAR
      : E_SOURCE_EXTENSION_TASK_LIST;
  ESourceBackend    *ext      = e_source_get_extension (source, ext_name);
  return e_source_backend_dup_backend_name (ext);
}

static gboolean
get_component_is_all_day (ICalComponent *comp,
                           time_t         start_time,
                           ICalTimezone  *default_zone)
{
  ICalTime *dtstart = i_cal_component_get_dtstart (comp);
  if (dtstart && i_cal_time_is_date (dtstart))
    {
      g_object_unref (dtstart);
      return TRUE;
    }
  if (dtstart) g_object_unref (dtstart);

  struct tm *start_tm = gmtime (&start_time);
  if (start_tm->tm_sec != 0 || start_tm->tm_min != 0 || start_tm->tm_hour != 0)
    return FALSE;

  time_t end_time =
    get_time_from_property (comp, I_CAL_DTEND_PROPERTY,
                             i_cal_property_get_dtend, default_zone);
  if (end_time)
    return (end_time - start_time) % 86400 == 0;

  ICalProperty *prop =
    i_cal_component_get_first_property (comp, I_CAL_DURATION_PROPERTY);
  if (!prop) return FALSE;

  ICalDuration *dur   = i_cal_property_get_duration (prop);
  gboolean      all_d = i_cal_duration_as_int (dur) % 86400 == 0;
  g_object_unref (dur);
  g_object_unref (prop);
  return all_d;
}

static void
appointment_init (CalendarAppointment *appt,
                   ICalComponent        *comp,
                   CalendarEDSSource    *src,
                   ICalTimezone         *zone)
{
  ICalProperty *prop;

  prop = i_cal_component_get_first_property (comp, I_CAL_UID_PROPERTY);
  appt->uid = prop ? g_strdup (i_cal_property_get_uid (prop)) : NULL;
  if (prop) g_object_unref (prop);

  prop = i_cal_component_get_first_property (comp, I_CAL_RECURRENCEID_PROPERTY);
  if (prop)
    {
      ICalTime *rid_time = i_cal_property_get_recurrenceid (prop);
      g_object_unref (prop);
      if (rid_time && i_cal_time_is_valid_time (rid_time) &&
          !i_cal_time_is_null_time (rid_time))
        appt->rid = g_strdup (i_cal_time_as_ical_string (rid_time));
      if (rid_time) g_object_unref (rid_time);
    }

  appt->backend_name = get_source_backend_name (src->client);
  appt->color_string = get_source_color (src->client);

  prop = i_cal_component_get_first_property (comp, I_CAL_SUMMARY_PROPERTY);
  appt->summary = prop ? g_strdup (i_cal_property_get_summary (prop)) : NULL;
  if (prop) g_object_unref (prop);

  prop = i_cal_component_get_first_property (comp, I_CAL_DESCRIPTION_PROPERTY);
  appt->description = prop ? g_strdup (i_cal_property_get_description (prop)) : NULL;
  if (prop) g_object_unref (prop);

  appt->start_time =
    get_time_from_property (comp, I_CAL_DTSTART_PROPERTY,
                             i_cal_property_get_dtstart, zone);
  appt->end_time =
    get_time_from_property (comp, I_CAL_DTEND_PROPERTY,
                             i_cal_property_get_dtend, zone);
  appt->is_all_day = get_component_is_all_day (comp, appt->start_time, zone);
}

static void
task_init (CalendarTask      *task,
            ICalComponent     *comp,
            CalendarEDSSource *src,
            ICalTimezone      *zone)
{
  ICalProperty *prop;

  prop = i_cal_component_get_first_property (comp, I_CAL_UID_PROPERTY);
  task->uid = prop ? g_strdup (i_cal_property_get_uid (prop)) : NULL;
  if (prop) g_object_unref (prop);

  prop = i_cal_component_get_first_property (comp, I_CAL_SUMMARY_PROPERTY);
  task->summary = prop ? g_strdup (i_cal_property_get_summary (prop)) : NULL;
  if (prop) g_object_unref (prop);

  prop = i_cal_component_get_first_property (comp, I_CAL_DESCRIPTION_PROPERTY);
  task->description = prop ? g_strdup (i_cal_property_get_description (prop)) : NULL;
  if (prop) g_object_unref (prop);

  prop = i_cal_component_get_first_property (comp, I_CAL_URL_PROPERTY);
  task->url = prop ? g_strdup (i_cal_property_get_url (prop)) : NULL;
  if (prop) g_object_unref (prop);

  task->color_string = get_source_color (src->client);
  task->start_time   =
    get_time_from_property (comp, I_CAL_DTSTART_PROPERTY,
                             i_cal_property_get_dtstart, zone);
  task->due_time =
    get_time_from_property (comp, I_CAL_DUE_PROPERTY,
                             i_cal_property_get_due, zone);
  task->completed_time =
    get_time_from_property (comp, I_CAL_COMPLETED_PROPERTY,
                             i_cal_property_get_completed, zone);

  ICalPropertyStatus status = i_cal_component_get_status (comp);
  if (status == I_CAL_STATUS_COMPLETED)
    {
      task->percent_complete = 100;
    }
  else
    {
      prop =
        i_cal_component_get_first_property (comp, I_CAL_PERCENTCOMPLETE_PROPERTY);
      if (prop)
        {
          task->percent_complete =
            CLAMP (i_cal_property_get_percentcomplete (prop), 0, 100);
          g_object_unref (prop);
        }
    }

  prop = i_cal_component_get_first_property (comp, I_CAL_PRIORITY_PROPERTY);
  task->priority = prop ? i_cal_property_get_priority (prop) : -1;
  if (prop) g_object_unref (prop);
}

/* =========================================================================
 * Instance generation callback (called by e_cal_client_generate_instances_*)
 * ========================================================================= */

typedef struct
{
  CalendarEDSSource *source;
  ICalTimezone      *system_timezone;
  gboolean           events_changed;
} InstanceCbData;

static gboolean
instance_generated_cb (ICalComponent *icomp,
                        ICalTime      *instance_start,
                        ICalTime      *instance_end,
                        gpointer       user_data,
                        GCancellable  *cancellable,
                        GError       **error)
{
  InstanceCbData    *data = user_data;
  CalendarEDSSource *src  = data->source;
  CalendarEDSProvider *provider = src->provider;
  CalendarEDSProviderPrivate *priv = provider->priv;

  /* Convert instance times to local timezone */
  ICalTimezone *event_tz = i_cal_time_get_timezone (instance_start);
  ICalTime *local_start  = i_cal_time_clone (instance_start);
  ICalTime *local_end    = i_cal_time_clone (instance_end);

  if (event_tz && data->system_timezone && event_tz != data->system_timezone)
    {
      i_cal_time_convert_timezone (local_start, event_tz, data->system_timezone);
      i_cal_time_convert_timezone (local_end,   event_tz, data->system_timezone);
    }

  time_t start_t = i_cal_time_as_timet (local_start);
  time_t end_t   = i_cal_time_as_timet (local_end);

  g_object_unref (local_start);
  g_object_unref (local_end);

  /* Build a CalendarEvent from the component */
  ICalComponentKind kind = i_cal_component_isa (icomp);
  CalendarEvent *event   = g_new0 (CalendarEvent, 1);

  if (kind == I_CAL_VEVENT_COMPONENT)
    {
      event->type = CALENDAR_EVENT_APPOINTMENT;
      appointment_init (CALENDAR_APPOINTMENT (event), icomp, src, priv->zone);
      CALENDAR_APPOINTMENT (event)->start_time = start_t;
      CALENDAR_APPOINTMENT (event)->end_time   = end_t;

      CalendarOccurrence *occ = g_new0 (CalendarOccurrence, 1);
      occ->start_time = start_t;
      occ->end_time   = end_t;
      CALENDAR_APPOINTMENT (event)->occurrences = g_slist_prepend (NULL, occ);
    }
  else if (kind == I_CAL_VTODO_COMPONENT)
    {
      event->type = CALENDAR_EVENT_TASK;
      task_init (CALENDAR_TASK (event), icomp, src, priv->zone);
    }
  else
    {
      g_free (event);
      return TRUE;
    }

  /* Build a uid key: uid + optional rid */
  char *uid_key;
  if (event->type == CALENDAR_EVENT_APPOINTMENT)
    {
      const char *uid = CALENDAR_APPOINTMENT (event)->uid;
      const char *rid = CALENDAR_APPOINTMENT (event)->rid;
      uid_key = (rid != NULL)
        ? g_strdup_printf ("%s%s", uid ? uid : "", rid)
        : g_strdup (uid ? uid : "");
    }
  else
    {
      uid_key = g_strdup (CALENDAR_TASK (event)->uid ? CALENDAR_TASK (event)->uid : "");
    }

  CalendarEvent *old =
    g_hash_table_lookup (src->in_progress_query.events, uid_key);

  if (!calendar_event_equal (event, old))
    {
      g_hash_table_replace (src->in_progress_query.events, uid_key, event);
      data->events_changed = TRUE;
    }
  else
    {
      calendar_event_free (event);
      g_free (uid_key);
    }

  return TRUE;
}

/* =========================================================================
 * Query runner
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

static inline char *
make_isodate_for_day_begin (int day, int month, int year)
{
  time_t t = make_time_for_day_begin (day, month, year);
  return (t != -1) ? isodate_from_time_t (t) : NULL;
}

static void
eds_run_query_on_source (CalendarEDSProvider *self,
                          CalendarEDSSource   *src,
                          const char          *query)
{
  CalendarEDSProviderPrivate *priv = self->priv;

  if (src->query_in_progress)
    {
      eds_query_clear (&src->in_progress_query);
      src->query_in_progress = FALSE;
    }

  src->query_in_progress = TRUE;
  eds_query_init (&src->in_progress_query);

  GSList *objects = NULL;
  GError *error   = NULL;

  if (!e_cal_client_get_object_list_sync (src->client, query,
                                           &objects, NULL, &error))
    {
      g_warning ("CalendarEDSProvider: query failed: %s", error->message);
      g_error_free (error);
      eds_query_clear (&src->in_progress_query);
      src->query_in_progress = FALSE;
      return;
    }

  /* Determine the system timezone once for all instances */
  SystemTimezone *systz    = system_timezone_new ();
  const char     *tz_name  = system_timezone_get (systz);
  ICalTimezone   *sys_zone = i_cal_timezone_get_builtin_timezone (tz_name);
  g_object_unref (systz);

  time_t month_begin =
    make_time_for_day_begin (1, (int) priv->month, (int) priv->year);
  time_t month_end;
  if (priv->month == 11)
    month_end = make_time_for_day_begin (1, 0, (int) priv->year + 1);
  else
    month_end = make_time_for_day_begin (1, (int) priv->month + 1, (int) priv->year);

  InstanceCbData cb_data = {
    .source           = src,
    .system_timezone  = sys_zone,
    .events_changed   = FALSE,
  };

  for (GSList *l = objects; l != NULL; l = l->next)
    {
      ICalComponent *comp = l->data;
      e_cal_client_generate_instances_for_object_sync (src->client,
                                                        comp,
                                                        month_begin,
                                                        month_end,
                                                        NULL,
                                                        instance_generated_cb,
                                                        &cb_data);
    }

  g_slist_free_full (objects, g_object_unref);

  /* Swap in_progress → completed */
  eds_query_clear (&src->completed_query);
  src->completed_query   = src->in_progress_query;
  src->query_completed   = TRUE;
  src->query_in_progress = FALSE;
  src->in_progress_query.events = NULL;

  if (cb_data.events_changed)
    {
      if (src->is_appointments)
        calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
      else
        calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));
    }
}

static void
eds_run_query (CalendarEDSProvider *self, gboolean is_appointments)
{
  CalendarEDSProviderPrivate *priv = self->priv;

  if (priv->month == G_MAXUINT || priv->year == G_MAXUINT)
    return;

  GHashTable *clients =
    is_appointments ? priv->appointment_clients : priv->task_clients;

  if (is_appointments)
    {
      char *month_begin =
        make_isodate_for_day_begin (1, (int) priv->month, (int) priv->year);
      char *month_end =
        make_isodate_for_day_begin (1, (int) priv->month + 1, (int) priv->year);
      char *query =
        g_strdup_printf ("occur-in-time-range? (make-time \"%s\") "
                         "(make-time \"%s\")",
                         month_begin, month_end);

      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init (&iter, clients);
      while (g_hash_table_iter_next (&iter, &key, &value))
        eds_run_query_on_source (self, (CalendarEDSSource *) value, query);

      g_free (month_begin);
      g_free (month_end);
      g_free (query);
    }
  else
    {
      /* Tasks: fetch all and let the UI filter */
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init (&iter, clients);
      while (g_hash_table_iter_next (&iter, &key, &value))
        eds_run_query_on_source (self, (CalendarEDSSource *) value, "#t");
    }
}

/* =========================================================================
 * Backend-died recovery
 * ========================================================================= */

static gboolean
backend_restart_cb (gpointer user_data)
{
  CalendarEDSSource   *src  = user_data;
  CalendarEDSProvider *self = src->provider;

  eds_load_source_list (self, src->is_appointments);
  eds_run_query (self, src->is_appointments);

  return G_SOURCE_REMOVE;
}

static void
backend_died_cb (EClient *client, CalendarEDSSource *src)
{
  ESource    *source       = e_client_get_source (client);
  const char *display_name = e_source_get_display_name (source);

  g_warning ("Calendar backend for '%s' has crashed.", display_name);

  CalendarEDSProviderPrivate *priv = src->provider->priv;
  GHashTable *clients = src->is_appointments
    ? priv->appointment_clients
    : priv->task_clients;

  g_hash_table_remove (clients, source);
  g_timeout_add_seconds (2, backend_restart_cb, src);
}

/* =========================================================================
 * ESourceRegistry management
 * ========================================================================= */

static void
create_client_for_source (CalendarEDSProvider     *self,
                            ESource                 *source,
                            ECalClientSourceType     source_type,
                            GHashTable              *clients,
                            gboolean                 is_appointments)
{
  if (g_hash_table_lookup (clients, source) != NULL)
    return;

  GError  *error  = NULL;
  EClient *client =
    e_cal_client_connect_sync (source, source_type, -1, NULL, &error);

  if (client == NULL)
    {
      g_warning ("CalendarEDSProvider: cannot load source '%s': %s",
                 e_source_get_uid (source), error->message);
      g_clear_error (&error);
      return;
    }

  CalendarEDSSource *src =
    eds_source_new (self, E_CAL_CLIENT (client), is_appointments);
  g_object_unref (client);

  g_hash_table_insert (clients, g_object_ref (source), src);
}

static void
eds_load_source_list (CalendarEDSProvider *self, gboolean is_appointments)
{
  CalendarEDSProviderPrivate *priv = self->priv;

  ECalClientSourceType source_type;
  const char          *extension_name;
  GHashTable          *clients;

  if (is_appointments)
    {
      source_type    = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
      extension_name = E_SOURCE_EXTENSION_CALENDAR;
      clients        = priv->appointment_clients;
    }
  else
    {
      source_type    = E_CAL_CLIENT_SOURCE_TYPE_TASKS;
      extension_name = E_SOURCE_EXTENSION_TASK_LIST;
      clients        = priv->task_clients;
    }

  GList *list = e_source_registry_list_sources (priv->registry, extension_name);
  for (GList *l = list; l != NULL; l = l->next)
    {
      ESource           *source = E_SOURCE (l->data);
      ESourceSelectable *ext    = e_source_get_extension (source, extension_name);
      gboolean           show   =
        e_source_get_enabled (source) && e_source_selectable_get_selected (ext);

      if (show)
        create_client_for_source (self, source, source_type,
                                   clients, is_appointments);
    }

  g_list_free_full (list, g_object_unref);
}

static void
registry_source_changed_cb (ESourceRegistry     *registry,
                              ESource             *source,
                              CalendarEDSProvider *self)
{
  CalendarEDSProviderPrivate *priv = self->priv;
  (void) registry;

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
    {
      ESourceSelectable *ext =
        e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
      gboolean have   = g_hash_table_lookup (priv->appointment_clients, source) != NULL;
      gboolean show   = e_source_get_enabled (source) &&
                        e_source_selectable_get_selected (ext);

      if (!show && have)
        {
          g_hash_table_remove (priv->appointment_clients, source);
          calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
        }
      else if (show && !have)
        {
          create_client_for_source (self, source,
                                     E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
                                     priv->appointment_clients, TRUE);
          eds_run_query (self, TRUE);
          calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
        }
    }

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
    {
      ESourceSelectable *ext =
        e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
      gboolean have = g_hash_table_lookup (priv->task_clients, source) != NULL;
      gboolean show = e_source_get_enabled (source) &&
                      e_source_selectable_get_selected (ext);

      if (!show && have)
        {
          g_hash_table_remove (priv->task_clients, source);
          calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));
        }
      else if (show && !have)
        {
          create_client_for_source (self, source,
                                     E_CAL_CLIENT_SOURCE_TYPE_TASKS,
                                     priv->task_clients, FALSE);
          eds_run_query (self, FALSE);
          calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));
        }
    }
}

static void
registry_source_removed_cb (ESourceRegistry     *registry,
                              ESource             *source,
                              CalendarEDSProvider *self)
{
  CalendarEDSProviderPrivate *priv = self->priv;
  (void) registry;

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
    {
      g_hash_table_remove (priv->appointment_clients, source);
      calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
    }
  if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
    {
      g_hash_table_remove (priv->task_clients, source);
      calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));
    }
}

/* =========================================================================
 * Timezone management
 * ========================================================================= */

static void
eds_set_timezone (CalendarEDSProvider *self)
{
  CalendarEDSProviderPrivate *priv = self->priv;

  /* Try GSettings timezone key (Evolution or panel settings) */
  if (priv->calendar_settings != NULL)
    {
      gchar **keys = g_settings_list_keys (priv->calendar_settings);
      for (gint i = 0; keys[i] != NULL; i++)
        {
          if (g_strcmp0 (keys[i], "timezone") == 0)
            {
              gchar *loc = g_settings_get_string (priv->calendar_settings, "timezone");
              if (loc && *loc)
                {
                  ICalTimezone *tz = i_cal_timezone_get_builtin_timezone (loc);
                  if (tz) priv->zone = tz;
                }
              g_free (loc);
              break;
            }
        }
      g_strfreev (keys);
    }

  if (priv->zone == NULL)
    priv->zone = i_cal_timezone_get_utc_timezone ();

  /* Push timezone into all connected ECalClients */
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, priv->appointment_clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      CalendarEDSSource *src = value;
      e_cal_client_set_default_timezone (src->client, priv->zone);
    }
}

static void
timezone_changed_cb (GSettings           *settings,
                      const char          *key,
                      CalendarEDSProvider *self)
{
  (void) settings;
  (void) key;
  eds_set_timezone (self);
}

/* =========================================================================
 * CalendarProvider vtable
 * ========================================================================= */

static void
eds_select_month (CalendarProvider *provider, guint month, guint year)
{
  CalendarEDSProvider        *self = CALENDAR_EDS_PROVIDER (provider);
  CalendarEDSProviderPrivate *priv = self->priv;

  if (priv->month == month && priv->year == year)
    return;

  priv->month = month;
  priv->year  = year;

  eds_run_query (self, TRUE);
  eds_run_query (self, FALSE);
}

static void
eds_select_day (CalendarProvider *provider, guint day)
{
  (void) provider;
  (void) day;
}

static GSList *
eds_get_events (CalendarProvider  *provider,
                CalendarEventType  event_mask,
                time_t             start_time,
                time_t             end_time)
{
  CalendarEDSProvider        *self    = CALENDAR_EDS_PROVIDER (provider);
  CalendarEDSProviderPrivate *priv    = self->priv;
  GSList                     *result  = NULL;

  if (event_mask & CALENDAR_EVENT_APPOINTMENT)
    {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init (&iter, priv->appointment_clients);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          CalendarEDSSource *src = value;
          if (!src->query_completed) continue;

          GSList *events =
            calendar_provider_filter_events_by_range (src->completed_query.events,
                                                       CALENDAR_EVENT_APPOINTMENT,
                                                       start_time, end_time);
          result = g_slist_concat (result, events);
        }
    }

  if (event_mask & CALENDAR_EVENT_TASK)
    {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init (&iter, priv->task_clients);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          CalendarEDSSource *src = value;
          if (!src->query_completed) continue;

          GSList *events =
            calendar_provider_filter_events_by_range (src->completed_query.events,
                                                       CALENDAR_EVENT_TASK,
                                                       start_time, end_time);
          result = g_slist_concat (result, events);
        }
    }

  return result;
}

static void
eds_set_task_completed (CalendarProvider *provider,
                         const char       *task_uid,
                         gboolean          task_completed,
                         guint             percent_complete)
{
  CalendarEDSProvider        *self = CALENDAR_EDS_PROVIDER (provider);
  CalendarEDSProviderPrivate *priv = self->priv;

  GHashTableIter iter;
  gpointer key, value;
  ECalClient     *found_client = NULL;
  ICalComponent  *found_comp   = NULL;

  g_hash_table_iter_init (&iter, priv->task_clients);
  while (g_hash_table_iter_next (&iter, &key, &value) && found_comp == NULL)
    {
      CalendarEDSSource *src = value;
      if (!src->query_completed) continue;

      CalendarEvent *ev =
        g_hash_table_lookup (src->completed_query.events, task_uid);
      if (ev == NULL) continue;

      GError *error = NULL;
      if (e_cal_client_get_object_sync (src->client, task_uid, NULL,
                                         &found_comp, NULL, &error))
        {
          found_client = src->client;
        }
      else
        {
          g_clear_error (&error);
        }
    }

  if (found_comp == NULL || found_client == NULL)
    return;

  /* Update status */
  ICalPropertyStatus status = task_completed
    ? I_CAL_STATUS_COMPLETED
    : I_CAL_STATUS_NEEDSACTION;

  ICalProperty *prop =
    i_cal_component_get_first_property (found_comp, I_CAL_STATUS_PROPERTY);
  if (prop)
    {
      i_cal_property_set_status (prop, status);
      g_object_unref (prop);
    }
  else
    {
      i_cal_component_take_property (found_comp,
                                      i_cal_property_new_status (status));
    }

  /* Update percent-complete */
  prop = i_cal_component_get_first_property (found_comp,
                                               I_CAL_PERCENTCOMPLETE_PROPERTY);
  if (prop)
    {
      i_cal_property_set_percentcomplete (prop, (int) percent_complete);
      g_object_unref (prop);
    }
  else
    {
      i_cal_component_take_property (
        found_comp, i_cal_property_new_percentcomplete ((int) percent_complete));
    }

  e_cal_client_modify_object_sync (found_client, found_comp,
                                    E_CAL_OBJ_MOD_ALL, 0, NULL, NULL);
  g_object_unref (found_comp);
}

static gboolean
eds_create_task (CalendarProvider *provider, const char *summary)
{
  CalendarEDSProvider        *self = CALENDAR_EDS_PROVIDER (provider);
  CalendarEDSProviderPrivate *priv = self->priv;

  /* Use the first available task client */
  ECalClient *task_client = NULL;
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init (&iter, priv->task_clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      task_client = ((CalendarEDSSource *) value)->client;
      break;
    }

  if (task_client == NULL)
    return FALSE;

  ICalComponent *vtodo = i_cal_component_new_vtodo ();
  i_cal_component_take_property (vtodo, i_cal_property_new_summary (summary));

  char   *uid   = NULL;
  GError *error = NULL;
  gboolean ok = e_cal_client_create_object_sync (task_client, vtodo,
                                                  E_CAL_OPERATION_FLAG_NONE,
                                                  &uid, NULL, &error);
  g_object_unref (vtodo);
  g_free (uid);
  if (error) g_error_free (error);

  return ok;
}

/* =========================================================================
 * GObject lifecycle
 * ========================================================================= */

static void
calendar_eds_provider_finalize (GObject *object)
{
  CalendarEDSProvider        *self = CALENDAR_EDS_PROVIDER (object);
  CalendarEDSProviderPrivate *priv = self->priv;

  if (priv->registry)
    {
      g_signal_handler_disconnect (priv->registry, priv->source_added_id);
      g_signal_handler_disconnect (priv->registry, priv->source_changed_id);
      g_signal_handler_disconnect (priv->registry, priv->source_removed_id);
      g_clear_object (&priv->registry);
    }

  if (priv->zone_listener && priv->calendar_settings)
    g_signal_handler_disconnect (priv->calendar_settings, priv->zone_listener);

  g_clear_object (&priv->calendar_settings);

  g_hash_table_destroy (priv->appointment_clients);
  g_hash_table_destroy (priv->task_clients);

  G_OBJECT_CLASS (calendar_eds_provider_parent_class)->finalize (object);
}

static void
calendar_eds_provider_class_init (CalendarEDSProviderClass *klass)
{
  GObjectClass          *gobject_class  = G_OBJECT_CLASS (klass);
  CalendarProviderClass *provider_class = CALENDAR_PROVIDER_CLASS (klass);

  gobject_class->finalize          = calendar_eds_provider_finalize;

  provider_class->select_month     = eds_select_month;
  provider_class->select_day       = eds_select_day;
  provider_class->get_events       = eds_get_events;
  provider_class->set_task_completed = eds_set_task_completed;
  provider_class->create_task      = eds_create_task;
}

static void
calendar_eds_provider_init (CalendarEDSProvider *self)
{
  self->priv = calendar_eds_provider_get_instance_private (self);
  CalendarEDSProviderPrivate *priv = self->priv;

  priv->month = G_MAXUINT;
  priv->year  = G_MAXUINT;
  priv->zone  = NULL;

  priv->appointment_clients =
    g_hash_table_new_full ((GHashFunc)  e_source_hash,
                            (GEqualFunc) e_source_equal,
                            (GDestroyNotify) g_object_unref,
                            (GDestroyNotify) eds_source_free);
  priv->task_clients =
    g_hash_table_new_full ((GHashFunc)  e_source_hash,
                            (GEqualFunc) e_source_equal,
                            (GDestroyNotify) g_object_unref,
                            (GDestroyNotify) eds_source_free);
}

/* =========================================================================
 * Public constructor
 * ========================================================================= */

CalendarProvider *
calendar_eds_provider_new (GSettings *settings)
{
  CalendarEDSProvider        *self  = g_object_new (CALENDAR_TYPE_EDS_PROVIDER, NULL);
  CalendarEDSProviderPrivate *priv  = self->priv;
  GError                     *error = NULL;

  /* GSettings: prefer the passed-in settings (MATE panel's own schema), then
   * fall back to Evolution's schema if available. */
  if (settings != NULL)
    {
      priv->calendar_settings = g_object_ref (settings);
    }
  else
    {
      GSettingsSchemaSource *src =
        g_settings_schema_source_get_default ();
      GSettingsSchema *schema =
        g_settings_schema_source_lookup (src, "org.gnome.evolution.calendar", FALSE);
      if (schema)
        {
          priv->calendar_settings = g_settings_new ("org.gnome.evolution.calendar");
          g_settings_schema_unref (schema);
        }
    }

  eds_set_timezone (self);

  if (priv->calendar_settings)
    priv->zone_listener =
      g_signal_connect (priv->calendar_settings, "changed::timezone",
                        G_CALLBACK (timezone_changed_cb), self);

  priv->registry = e_source_registry_new_sync (NULL, &error);
  if (error)
    {
      g_critical ("CalendarEDSProvider: cannot create ESourceRegistry: %s",
                  error->message);
      g_error_free (error);
      g_object_unref (self);
      return NULL;
    }

  priv->source_added_id =
    g_signal_connect (priv->registry, "source-added",
                      G_CALLBACK (registry_source_changed_cb), self);
  priv->source_changed_id =
    g_signal_connect (priv->registry, "source-changed",
                      G_CALLBACK (registry_source_changed_cb), self);
  priv->source_removed_id =
    g_signal_connect (priv->registry, "source-removed",
                      G_CALLBACK (registry_source_removed_cb), self);

  /* Eagerly load the source lists so clients are ready before first query */
  eds_load_source_list (self, TRUE);
  eds_load_source_list (self, FALSE);

  return CALENDAR_PROVIDER (self);
}
