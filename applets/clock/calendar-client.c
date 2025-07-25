/*
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

#include "calendar-client.h"

#include <libintl.h>
#include <string.h>
#define HANDLE_LIBICAL_MEMORY

#ifdef HAVE_EDS

#include <libecal/libecal.h>
#include "calendar-sources.h"
#endif

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

#ifndef _
#define _(x) gettext(x)
#endif

#ifndef N_
#define N_(x) x
#endif

#ifdef HAVE_EDS

typedef struct _CalendarClientQuery  CalendarClientQuery;
typedef struct _CalendarClientSource CalendarClientSource;

struct _CalendarClientQuery
{
  ECalClientView *view;
  GHashTable     *events;
};

struct _CalendarClientSource
{
  CalendarClient      *client;
  ECalClient          *source;

  CalendarClientQuery  completed_query;
  CalendarClientQuery  in_progress_query;

  guint                changed_signal_id;

  guint                query_completed : 1;
  guint                query_in_progress : 1;
};

struct _CalendarClientPrivate
{
  CalendarSources     *calendar_sources;

  GSList              *appointment_sources;
  GSList              *task_sources;

  ICalTimezone        *zone;

  guint                zone_listener;
  GSettings           *calendar_settings;

  guint                day;
  guint                month;
  guint                year;
};

static void calendar_client_finalize     (GObject             *object);
static void calendar_client_set_property (GObject             *object,
					  guint                prop_id,
					  const GValue        *value,
					  GParamSpec          *pspec);
static void calendar_client_get_property (GObject             *object,
					  guint                prop_id,
					  GValue              *value,
					  GParamSpec          *pspec);

static GSList *calendar_client_update_sources_list         (CalendarClient       *client,
							    GSList               *sources,
							    GList                *esources,
							    guint                 changed_signal_id);
static void    calendar_client_appointment_sources_changed (CalendarClient       *client);
static void    calendar_client_task_sources_changed        (CalendarClient       *client);

static void calendar_client_stop_query  (CalendarClient       *client,
					 CalendarClientSource *source,
					 CalendarClientQuery  *query);
static void calendar_client_start_query (CalendarClient       *client,
					 CalendarClientSource *source,
					 const char           *query);

static void calendar_client_source_finalize (CalendarClientSource *source);
static void calendar_client_query_finalize  (CalendarClientQuery  *query);

static void
calendar_client_update_appointments (CalendarClient *client);
static void
calendar_client_update_tasks (CalendarClient *client);

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

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (CalendarClient, calendar_client, G_TYPE_OBJECT)

static void
calendar_client_class_init (CalendarClientClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = calendar_client_finalize;
  gobject_class->set_property = calendar_client_set_property;
  gobject_class->get_property = calendar_client_get_property;

  g_object_class_install_property (gobject_class,
				   PROP_DAY,
				   g_param_spec_uint ("day",
						      "Day",
						      "The currently monitored day between 1 and 31 (0 denotes unset)",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_MONTH,
				   g_param_spec_uint ("month",
						      "Month",
						      "The currently monitored month between 0 and 11",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_YEAR,
				   g_param_spec_uint ("year",
						      "Year",
						      "The currently monitored year",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  signals [APPOINTMENTS_CHANGED] =
    g_signal_new ("appointments-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarClientClass, tasks_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);

  signals [TASKS_CHANGED] =
    g_signal_new ("tasks-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarClientClass, tasks_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);
}

/* Timezone code adapted from evolution/calendar/gui/calendar-config.c */
/* The current timezone, e.g. "Europe/London". It may be NULL, in which case
   you should assume UTC. */
static gchar *
calendar_client_config_get_timezone (GSettings *calendar_settings)
{
  return g_settings_get_string (calendar_settings, "timezone");
}

static ICalTimezone *
calendar_client_config_get_icaltimezone (CalendarClient *client)
{
  gchar        *location = NULL;
  ICalTimezone *zone = NULL;

  if (client->priv->calendar_settings != NULL)
    location = calendar_client_config_get_timezone (client->priv->calendar_settings);

  if (!location) {
    /* MATE panel doesn't store timezone in GSettings
     * Since libical timezone lookup often fails, just use UTC
     * The display code will handle local time conversion */
    return i_cal_timezone_get_utc_timezone ();
  }

  zone = i_cal_timezone_get_builtin_timezone (location);
  g_free (location);

  return zone;
}

static void
calendar_client_set_timezone (CalendarClient *client)
{
  GList *list, *link;

  client->priv->zone = calendar_client_config_get_icaltimezone (client);

  list = calendar_sources_get_appointment_clients (client->priv->calendar_sources);
  for (link = list; link != NULL; link = g_list_next (link))
    {
      ECalClient *cal = E_CAL_CLIENT (link->data);

      e_cal_client_set_default_timezone (cal, client->priv->zone);
    }
  g_list_free (list);
}

static void
calendar_client_timezone_changed_cb (GSettings      *calendar_settings,
                                     const gchar    *key,
                                     CalendarClient *client)
{
  calendar_client_set_timezone (client);
}

static void
load_calendars (CalendarClient    *client,
                CalendarEventType  type)
{
  GSList *l, *clients;

  switch (type)
    {
      case CALENDAR_EVENT_APPOINTMENT:
        clients = client->priv->appointment_sources;
        break;
      case CALENDAR_EVENT_TASK:
        clients = client->priv->task_sources;
        break;
      case CALENDAR_EVENT_ALL:
      default:
        g_assert_not_reached ();
    }

  for (l = clients; l != NULL; l = l->next)
    {
      if (type == CALENDAR_EVENT_APPOINTMENT)
        calendar_client_update_appointments (client);
      else if (type == CALENDAR_EVENT_TASK)
        calendar_client_update_tasks (client);
    }
}

static void
calendar_client_init (CalendarClient *client)
{
  GList *list;

  client->priv = calendar_client_get_instance_private (client);

  client->priv->calendar_sources = calendar_sources_get ();

  list = calendar_sources_get_appointment_clients (client->priv->calendar_sources);
  client->priv->appointment_sources =
    calendar_client_update_sources_list (client, NULL, list, signals [APPOINTMENTS_CHANGED]);
  g_list_free (list);

  list = calendar_sources_get_task_clients (client->priv->calendar_sources);
  client->priv->task_sources = calendar_client_update_sources_list (client, NULL, list, signals [TASKS_CHANGED]);
  g_list_free (list);

  /* set the timezone before loading the clients */
  calendar_client_set_timezone (client);
  load_calendars (client, CALENDAR_EVENT_APPOINTMENT);
  load_calendars (client, CALENDAR_EVENT_TASK);

  g_signal_connect_swapped (client->priv->calendar_sources,
			    "appointment-sources-changed",
			    G_CALLBACK (calendar_client_appointment_sources_changed),
			    client);
  g_signal_connect_swapped (client->priv->calendar_sources,
			    "task-sources-changed",
			    G_CALLBACK (calendar_client_task_sources_changed),
			    client);

  if (client->priv->calendar_settings != NULL)
    client->priv->zone_listener = g_signal_connect (client->priv->calendar_settings,
						    "changed::timezone",
						    G_CALLBACK (calendar_client_timezone_changed_cb),
						    client);

  client->priv->day = G_MAXUINT;
  client->priv->month = G_MAXUINT;
  client->priv->year = G_MAXUINT;
}

static void
calendar_client_finalize (GObject *object)
{
  CalendarClient *client = CALENDAR_CLIENT (object);
  GSList         *l;

  if (client->priv->zone_listener)
    {
      g_signal_handler_disconnect (client->priv->calendar_settings,
				   client->priv->zone_listener);
      client->priv->zone_listener = 0;
    }

  if (client->priv->calendar_settings)
    g_object_unref (client->priv->calendar_settings);
  client->priv->calendar_settings = NULL;

  for (l = client->priv->appointment_sources; l; l = l->next)
    {
      calendar_client_source_finalize (l->data);
      g_free (l->data);
    }
  g_slist_free (client->priv->appointment_sources);
  client->priv->appointment_sources = NULL;

  for (l = client->priv->task_sources; l; l = l->next)
    {
      calendar_client_source_finalize (l->data);
      g_free (l->data);
    }
  g_slist_free (client->priv->task_sources);
  client->priv->task_sources = NULL;

  if (client->priv->calendar_sources)
    g_object_unref (client->priv->calendar_sources);
  client->priv->calendar_sources = NULL;

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
      calendar_client_select_month (client,
				    g_value_get_uint (value),
				    client->priv->year);
      break;
    case PROP_YEAR:
      calendar_client_select_month (client,
				    client->priv->month,
				    g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
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
    case PROP_DAY:
      g_value_set_uint (value, client->priv->day);
      break;
    case PROP_MONTH:
      g_value_set_uint (value, client->priv->month);
      break;
    case PROP_YEAR:
      g_value_set_uint (value, client->priv->year);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CalendarClient *
calendar_client_new (GSettings *settings)
{
  CalendarClient *client = g_object_new (CALENDAR_TYPE_CLIENT, NULL);

  /* Use the provided MATE panel settings instead of Evolution settings */
  if (settings) {
    client->priv->calendar_settings = g_object_ref (settings);
  } else {
    /* Fallback to Evolution calendar settings if available */
    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    const gchar *evolution_calendar_schema = "org.gnome.evolution.calendar";
    GSettingsSchema *schema = g_settings_schema_source_lookup (schema_source, evolution_calendar_schema, FALSE);
    if (schema) {
      client->priv->calendar_settings = g_settings_new (evolution_calendar_schema);
      g_settings_schema_unref (schema);
    } else {
      /* No Evolution settings available, calendar_settings will remain NULL */
      client->priv->calendar_settings = NULL;
    }
  }

  return client;
}

/* @day and @month can happily be out of range as
 * mktime() will normalize them correctly. From mktime(3):
 *
 * "If structure members are outside their legal interval,
 *  they will be normalized (so that, e.g., 40 October is
 *  changed into 9 November)."
 *
 * "What?", you say, "Something useful in libc?"
 */
static inline time_t
make_time_for_day_begin (int day,
			 int month,
			 int year)
{
  struct tm localtime_tm = { 0, };

  localtime_tm.tm_mday  = day;
  localtime_tm.tm_mon   = month;
  localtime_tm.tm_year  = year - 1900;
  localtime_tm.tm_isdst = -1;

  return mktime (&localtime_tm);
}

static inline char *
make_isodate_for_day_begin (int day,
			    int month,
			    int year)
{
  time_t utctime;

  utctime = make_time_for_day_begin (day, month, year);

  return utctime != -1 ? isodate_from_time_t (utctime) : NULL;
}

static time_t
get_time_from_property (ICalComponent    *icomp,
			ICalPropertyKind  prop_kind,
			ICalTime         * (* get_prop_func) (ICalProperty *prop),
			ICalTimezone     *default_zone)
{
  ICalProperty *prop;
  ICalTime *ical_time;
  ICalParameter *param;
  ICalTimezone *timezone;
  time_t retval;

  prop = i_cal_component_get_first_property (icomp, prop_kind);
  if (!prop)
    return 0;

  param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
  ical_time = get_prop_func (prop);
  g_object_unref (prop);

  if (param)
    {
      const char *tzid;

      tzid = i_cal_parameter_get_tzid (param);
      timezone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
      g_object_unref (param);

      /* If timezone lookup failed, fall back to default zone */
      if (!timezone) {
        timezone = default_zone;
      }
    }
  else if (i_cal_time_is_utc (ical_time))
    {
      timezone = i_cal_timezone_get_utc_timezone ();
    }
  else
    {
      timezone = default_zone;
    }

  retval = i_cal_time_as_timet_with_zone (ical_time, timezone);

  g_object_unref (ical_time);

  return retval;
}

static char *
get_component_uid (ICalComponent *component)
{
  return g_strdup (i_cal_component_get_uid (component));
}

static char *
get_component_rid (ICalComponent *component)
{
  ICalProperty *prop;
  ICalTime *time;
  char *rid;

  prop = i_cal_component_get_first_property (component, I_CAL_RECURRENCEID_PROPERTY);
  if (!prop)
    return NULL;

  time = i_cal_property_get_recurrenceid (prop);
  g_object_unref (prop);

  if (!i_cal_time_is_valid_time (time) || i_cal_time_is_null_time (time))
    {
      g_object_unref (time);
      return NULL;
    }

  rid = g_strdup (i_cal_time_as_ical_string (time));
  g_object_unref (time);

  return rid;
}

static char *
get_component_summary (ICalComponent *component)
{
  ICalProperty *prop;
  char *summary;

  prop = i_cal_component_get_first_property (component, I_CAL_SUMMARY_PROPERTY);
  if (!prop)
    return NULL;

  summary = g_strdup (i_cal_property_get_summary (prop));
  g_object_unref (prop);

  return summary;
}

static char *
get_component_description (ICalComponent *component)
{
  ICalProperty *prop;
  char *description;

  prop = i_cal_component_get_first_property (component, I_CAL_DESCRIPTION_PROPERTY);
  if (!prop)
    return NULL;

  description = g_strdup (i_cal_property_get_description (prop));
  g_object_unref (prop);

  return description;
}

static inline time_t
get_component_start_time (ICalComponent *component,
                          ICalTimezone  *default_zone)
{
  return get_time_from_property (component,
				 I_CAL_DTSTART_PROPERTY,
				 i_cal_property_get_dtstart,
				 default_zone);
}

static inline time_t
get_component_end_time (ICalComponent *component,
                        ICalTimezone  *default_zone)
{
  return get_time_from_property (component,
                                 I_CAL_DTEND_PROPERTY,
                                 i_cal_property_get_dtend,
                                 default_zone);
}

static gboolean
get_component_is_all_day (ICalComponent *component,
                          time_t         start_time,
                          ICalTimezone  *default_zone)
{
  ICalTime *dtstart;
  struct tm *start_tm;
  time_t end_time;
  ICalProperty *prop;
  ICalDuration *duration;
  gboolean is_all_day;

  dtstart = i_cal_component_get_dtstart (component);

  if (dtstart && i_cal_time_is_date (dtstart))
    {
      g_object_unref (dtstart);
      return TRUE;
    }

  g_object_unref (dtstart);

  start_tm = gmtime (&start_time);
  if (start_tm->tm_sec  != 0 ||
      start_tm->tm_min  != 0 ||
      start_tm->tm_hour != 0)
    return FALSE;

  if ((end_time = get_component_end_time (component, default_zone)))
    return (end_time - start_time) % 86400 == 0;

  prop = i_cal_component_get_first_property (component, I_CAL_DURATION_PROPERTY);
  if (!prop)
    return FALSE;

  duration = i_cal_property_get_duration (prop);
  g_object_unref (prop);

  is_all_day = i_cal_duration_as_int (duration) % 86400 == 0;
  g_object_unref (duration);

  return is_all_day;
}

static inline time_t
get_component_due_time (ICalComponent *component,
                        ICalTimezone  *default_zone)
{
  return get_time_from_property (component,
                                 I_CAL_DUE_PROPERTY,
                                 i_cal_property_get_due,
                                 default_zone);
}

static guint
get_component_percent_complete (ICalComponent *component)
{
  ICalPropertyStatus status;
  ICalProperty *prop;
  int percent_complete;

  status = i_cal_component_get_status (component);
  if (status == I_CAL_STATUS_COMPLETED)
    return 100;

  prop = i_cal_component_get_first_property (component, I_CAL_COMPLETED_PROPERTY);

  if (prop)
    {
      g_object_unref (prop);
      return 100;
    }

  prop = i_cal_component_get_first_property (component, I_CAL_PERCENTCOMPLETE_PROPERTY);
  if (!prop)
    return 0;

  percent_complete = i_cal_property_get_percentcomplete (prop);
  g_object_unref (prop);

  return CLAMP (percent_complete, 0, 100);
}

static inline time_t
get_component_completed_time (ICalComponent *component,
                              ICalTimezone  *default_zone)
{
  return get_time_from_property (component,
                                 I_CAL_COMPLETED_PROPERTY,
                                 i_cal_property_get_completed,
                                 default_zone);
}

static int
get_component_priority (ICalComponent *component)
{
  ICalProperty *prop;
  int priority;

  prop = i_cal_component_get_first_property (component, I_CAL_PRIORITY_PROPERTY);
  if (!prop)
    return -1;

  priority = i_cal_property_get_priority (prop);
  g_object_unref (prop);

  return priority;
}

static char *
get_source_color (ECalClient *esource)
{
  ESource *source;
  ECalClientSourceType source_type;
  ESourceSelectable *extension;
  const gchar *extension_name;

  g_return_val_if_fail (E_IS_CAL_CLIENT (esource), NULL);

  source = e_client_get_source (E_CLIENT (esource));
  source_type = e_cal_client_get_source_type (esource);

  switch (source_type)
    {
      case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
        extension_name = E_SOURCE_EXTENSION_CALENDAR;
        break;
      case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
        extension_name = E_SOURCE_EXTENSION_TASK_LIST;
        break;
      case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
      case E_CAL_CLIENT_SOURCE_TYPE_LAST:
      default:
        g_return_val_if_reached (NULL);
    }

  extension = e_source_get_extension (source, extension_name);

  return e_source_selectable_dup_color (extension);
}

static gchar *
get_source_backend_name (ECalClient *esource)
{
  ESource *source;
  ECalClientSourceType source_type;
  ESourceBackend *extension;
  const gchar *extension_name;

  g_return_val_if_fail (E_IS_CAL_CLIENT (esource), NULL);

  source = e_client_get_source (E_CLIENT (esource));
  source_type = e_cal_client_get_source_type (esource);

  switch (source_type)
    {
      case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
        extension_name = E_SOURCE_EXTENSION_CALENDAR;
        break;
      case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
        extension_name = E_SOURCE_EXTENSION_TASK_LIST;
        break;
      case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
      case E_CAL_CLIENT_SOURCE_TYPE_LAST:
      default:
        g_return_val_if_reached (NULL);
    }

  extension = e_source_get_extension (source, extension_name);

  return e_source_backend_dup_backend_name (extension);
}

static inline gboolean
calendar_appointment_equal (CalendarAppointment *a,
			    CalendarAppointment *b)
{
  GSList *la, *lb;

  if (g_slist_length (a->occurrences) != g_slist_length (b->occurrences))
      return FALSE;

  for (la = a->occurrences, lb = b->occurrences; la && lb; la = la->next, lb = lb->next)
    {
      CalendarOccurrence *oa = la->data;
      CalendarOccurrence *ob = lb->data;

      if (oa->start_time != ob->start_time ||
	  oa->end_time   != ob->end_time)
	return FALSE;
    }

  return
    g_strcmp0 (a->uid, b->uid) == 0 &&
    g_strcmp0 (a->backend_name, b->backend_name) == 0 &&
    g_strcmp0 (a->summary, b->summary) == 0 &&
    g_strcmp0 (a->description, b->description) == 0 &&
    g_strcmp0 (a->color_string, b->color_string) == 0 &&
    a->start_time == b->start_time &&
    a->end_time == b->end_time &&
    a->is_all_day == b->is_all_day;
}

static void
calendar_appointment_copy (CalendarAppointment *appointment,
			   CalendarAppointment *appointment_copy)
{
  GSList *l;

  g_assert (appointment != NULL);
  g_assert (appointment_copy != NULL);

  appointment_copy->occurrences = g_slist_copy (appointment->occurrences);
  for (l = appointment_copy->occurrences; l; l = l->next)
    {
      CalendarOccurrence *occurrence = l->data;
      CalendarOccurrence *occurrence_copy;

      occurrence_copy             = g_new0 (CalendarOccurrence, 1);
      occurrence_copy->start_time = occurrence->start_time;
      occurrence_copy->end_time   = occurrence->end_time;

      l->data = occurrence_copy;
    }

  appointment_copy->uid          = g_strdup (appointment->uid);
  appointment_copy->backend_name = g_strdup (appointment->backend_name);
  appointment_copy->summary      = g_strdup (appointment->summary);
  appointment_copy->description  = g_strdup (appointment->description);
  appointment_copy->color_string = g_strdup (appointment->color_string);
  appointment_copy->start_time   = appointment->start_time;
  appointment_copy->end_time     = appointment->end_time;
  appointment_copy->is_all_day   = appointment->is_all_day;
}

static void
calendar_appointment_finalize (CalendarAppointment *appointment)
{
  GSList *l;

  for (l = appointment->occurrences; l; l = l->next)
    g_free (l->data);
  g_slist_free (appointment->occurrences);
  appointment->occurrences = NULL;

  g_free (appointment->uid);
  appointment->uid = NULL;

  g_free (appointment->rid);
  appointment->rid = NULL;

  g_free (appointment->backend_name);
  appointment->backend_name = NULL;

  g_free (appointment->summary);
  appointment->summary = NULL;

  g_free (appointment->description);
  appointment->description = NULL;

  g_free (appointment->color_string);
  appointment->color_string = NULL;

  appointment->start_time = 0;
  appointment->is_all_day = FALSE;
}

static void
calendar_appointment_init (CalendarAppointment  *appointment,
                           ICalComponent        *component,
                           CalendarClientSource *source,
                           ICalTimezone         *default_zone)
{
  appointment->uid = get_component_uid (component);
  appointment->rid = get_component_rid (component);
  appointment->backend_name = get_source_backend_name (source->source);
  appointment->summary = get_component_summary (component);
  appointment->description = get_component_description (component);
  appointment->color_string = get_source_color (source->source);
  appointment->start_time = get_component_start_time (component, default_zone);
  appointment->end_time = get_component_end_time (component, default_zone);
  appointment->is_all_day = get_component_is_all_day (component,
                                                      appointment->start_time,
                                                      default_zone);
}

static ICalTimezone *
resolve_timezone_id (const char    *tzid,
                     gpointer       user_data,
                     GCancellable  *cancellable,
                     GError       **error)
{
  ECalClient *client;
  ICalTimezone *retval;

  client = E_CAL_CLIENT (user_data);
  retval = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);

  if (!retval)
    e_cal_client_get_timezone_sync (client, tzid, &retval, NULL, NULL);

  return retval;
}

static gboolean
calendar_appointment_collect_occurrence (ICalComponent  *component,
                                         ICalTime       *occurrence_start,
                                         ICalTime       *occurrence_end,
                                         gpointer        user_data,
                                         GCancellable   *cancellable,
                                         GError        **error)
{
  time_t start;
  time_t end;
  CalendarOccurrence *occurrence;
  GSList **collect_loc = user_data;

  start = i_cal_time_as_timet (occurrence_start);
  end = i_cal_time_as_timet (occurrence_end);

  occurrence = g_new0 (CalendarOccurrence, 1);
  occurrence->start_time = start;
  occurrence->end_time = end;

  *collect_loc = g_slist_prepend (*collect_loc, occurrence);

  return TRUE;
}

static void
calendar_appointment_generate_ocurrences (CalendarAppointment *appointment,
                                          ICalComponent       *component,
                                          ECalClient          *source,
                                          time_t               start,
                                          time_t               end,
                                          ICalTimezone        *default_zone)
{
  ICalTime *start_time;
  ICalTime *end_time;

  g_assert (appointment->occurrences == NULL);

  start_time = i_cal_time_new_from_timet_with_zone (start, FALSE, NULL);
  end_time = i_cal_time_new_from_timet_with_zone (end, FALSE, NULL);

  e_cal_recur_generate_instances_sync (component,
                                       start_time,
                                       end_time,
                                       calendar_appointment_collect_occurrence,
                                       &appointment->occurrences,
                                       resolve_timezone_id,
                                       source,
                                       default_zone,
                                       NULL,
                                       NULL);

  g_object_unref (start_time);
 g_object_unref (end_time);

  appointment->occurrences = g_slist_reverse (appointment->occurrences);
}

static inline gboolean
calendar_task_equal (CalendarTask *a,
		     CalendarTask *b)
{
  return
    g_strcmp0 (a->uid, b->uid) == 0 &&
    g_strcmp0 (a->summary, b->summary) == 0 &&
    g_strcmp0 (a->description, b->description) == 0 &&
    g_strcmp0 (a->color_string, b->color_string) == 0 &&
    a->start_time == b->start_time &&
    a->due_time == b->due_time &&
    a->percent_complete == b->percent_complete &&
    a->completed_time == b->completed_time &&
    a->priority == b->priority;
}

static void
calendar_task_copy (CalendarTask *task,
		    CalendarTask *task_copy)
{
  g_assert (task != NULL);
  g_assert (task_copy != NULL);

  task_copy->uid              = g_strdup (task->uid);
  task_copy->summary          = g_strdup (task->summary);
  task_copy->description      = g_strdup (task->description);
  task_copy->color_string     = g_strdup (task->color_string);
  task_copy->start_time       = task->start_time;
  task_copy->due_time         = task->due_time;
  task_copy->percent_complete = task->percent_complete;
  task_copy->completed_time   = task->completed_time;
  task_copy->priority         = task->priority;
}

static void
calendar_task_finalize (CalendarTask *task)
{
  g_free (task->uid);
  task->uid = NULL;

  g_free (task->summary);
  task->summary = NULL;

  g_free (task->description);
  task->description = NULL;

  g_free (task->color_string);
  task->color_string = NULL;

  task->percent_complete = 0;
}

static void
calendar_task_init (CalendarTask         *task,
                    ICalComponent        *component,
                    CalendarClientSource *source,
                    ICalTimezone         *default_zone)
{
  task->uid = get_component_uid (component);
  task->summary = get_component_summary (component);
  task->description = get_component_description (component);
  task->color_string = get_source_color (source->source);
  task->start_time = get_component_start_time (component, default_zone);
  task->due_time = get_component_due_time (component, default_zone);
  task->percent_complete = get_component_percent_complete (component);
  task->completed_time = get_component_completed_time (component, default_zone);
  task->priority = get_component_priority (component);
}

void
calendar_event_free (CalendarEvent *event)
{
  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      calendar_appointment_finalize (CALENDAR_APPOINTMENT (event));
      break;
    case CALENDAR_EVENT_TASK:
      calendar_task_finalize (CALENDAR_TASK (event));
      break;
    case CALENDAR_EVENT_ALL:
    default:
      g_assert_not_reached ();
      break;
    }

  g_free (event);
}

static CalendarEvent *
calendar_event_new (ICalComponent        *component,
                    CalendarClientSource *source,
                    ICalTimezone         *default_zone)
{
  CalendarEvent *event;
  ICalComponentKind component_kind;

  event = g_new0 (CalendarEvent, 1);
  component_kind = i_cal_component_isa (component);

  if (component_kind == I_CAL_VEVENT_COMPONENT)
    {
      event->type = CALENDAR_EVENT_APPOINTMENT;
      calendar_appointment_init (CALENDAR_APPOINTMENT (event),
                                 component, source, default_zone);
    }
  else if (component_kind == I_CAL_VTODO_COMPONENT)
    {
      event->type = CALENDAR_EVENT_TASK;
      calendar_task_init (CALENDAR_TASK (event),
                          component, source, default_zone);
    }
  else
    {
      g_warning ("Unknown calendar component type: %d\n", component_kind);
      g_free (event);

      return NULL;
    }

  return event;
}

static CalendarEvent *
calendar_event_copy (CalendarEvent *event)
{
  CalendarEvent *retval;

  if (!event)
    return NULL;

  retval = g_new0 (CalendarEvent, 1);

  retval->type = event->type;

  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      calendar_appointment_copy (CALENDAR_APPOINTMENT (event),
				 CALENDAR_APPOINTMENT (retval));
      break;
    case CALENDAR_EVENT_TASK:
      calendar_task_copy (CALENDAR_TASK (event),
			  CALENDAR_TASK (retval));
      break;
    case CALENDAR_EVENT_ALL:
    default:
      g_assert_not_reached ();
      break;
    }

  return retval;
}

static char *
calendar_event_get_uid (CalendarEvent *event)
{
  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      return g_strdup_printf ("%s%s", CALENDAR_APPOINTMENT (event)->uid, CALENDAR_APPOINTMENT (event)->rid ? CALENDAR_APPOINTMENT (event)->rid : "");
      break;
    case CALENDAR_EVENT_TASK:
      return g_strdup (CALENDAR_TASK (event)->uid);
      break;
    case CALENDAR_EVENT_ALL:
    default:
      g_assert_not_reached ();
      break;
    }

  return NULL;
}

static gboolean
calendar_event_equal (CalendarEvent *a,
		      CalendarEvent *b)
{
  if (!a && !b)
    return TRUE;

  if ((a && !b) || (!a && b))
    return FALSE;

  if (a->type != b->type)
    return FALSE;

  switch (a->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      return calendar_appointment_equal (CALENDAR_APPOINTMENT (a),
					 CALENDAR_APPOINTMENT (b));
    case CALENDAR_EVENT_TASK:
      return calendar_task_equal (CALENDAR_TASK (a),
				  CALENDAR_TASK (b));
    case CALENDAR_EVENT_ALL:
    default:
      break;
    }

  g_assert_not_reached ();

  return FALSE;
}

static void
calendar_event_generate_ocurrences (CalendarEvent *event,
                                    ICalComponent *component,
                                    ECalClient    *source,
                                    time_t         start,
                                    time_t         end,
                                    ICalTimezone  *default_zone)
{
  if (event->type != CALENDAR_EVENT_APPOINTMENT)
    return;

  calendar_appointment_generate_ocurrences (CALENDAR_APPOINTMENT (event),
                                            component,
                                            source,
                                            start,
                                            end,
                                            default_zone);
}

static inline void
calendar_event_debug_dump (CalendarEvent *event)
{
#ifdef CALENDAR_ENABLE_DEBUG
  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      {
	char   *start_str;
	char   *end_str;
	GSList *l;

	start_str = CALENDAR_APPOINTMENT (event)->start_time ?
	                    isodate_from_time_t (CALENDAR_APPOINTMENT (event)->start_time) :
	                    g_strdup ("(undefined)");
	end_str = CALENDAR_APPOINTMENT (event)->end_time ?
	                    isodate_from_time_t (CALENDAR_APPOINTMENT (event)->end_time) :
	                    g_strdup ("(undefined)");

	g_free (start_str);
	g_free (end_str);

	for (l = CALENDAR_APPOINTMENT (event)->occurrences; l; l = l->next)
	  {
	    CalendarOccurrence *occurrence = l->data;

	    start_str = occurrence->start_time ?
	      isodate_from_time_t (occurrence->start_time) :
	      g_strdup ("(undefined)");

	    end_str = occurrence->end_time ?
	      isodate_from_time_t (occurrence->end_time) :
	      g_strdup ("(undefined)");

	    g_free (start_str);
	    g_free (end_str);
	  }
      }
      break;
    case CALENDAR_EVENT_TASK:
      {
	char *start_str;
	char *due_str;
	char *completed_str;

	start_str = CALENDAR_TASK (event)->start_time ?
	                    isodate_from_time_t (CALENDAR_TASK (event)->start_time) :
	                    g_strdup ("(undefined)");
	due_str = CALENDAR_TASK (event)->due_time ?
	                    isodate_from_time_t (CALENDAR_TASK (event)->due_time) :
	                    g_strdup ("(undefined)");
	completed_str = CALENDAR_TASK (event)->completed_time ?
	                    isodate_from_time_t (CALENDAR_TASK (event)->completed_time) :
	                    g_strdup ("(undefined)");

	g_free (completed_str);
      }
      break;
    default:
      g_assert_not_reached ();
      break;
    }
#endif
}

static inline CalendarClientQuery *
goddamn_this_is_crack (CalendarClientSource *source,
                       ECalClientView       *view,
                       gboolean             *emit_signal)
{
  g_assert (view != NULL);

  if (source->completed_query.view == view)
    {
      if (emit_signal)
	*emit_signal = TRUE;
      return &source->completed_query;
    }
  else if (source->in_progress_query.view == view)
    {
      if (emit_signal)
	*emit_signal = FALSE;
      return &source->in_progress_query;
    }

  g_assert_not_reached ();

  return NULL;
}

static void
calendar_client_handle_query_completed (CalendarClientSource *source,
                                        GError               *error,
                                        ECalClientView       *view)
{
  CalendarClientQuery *query;

  query = goddamn_this_is_crack (source, view, NULL);

  if (error != NULL)
    {
      g_warning ("Calendar query failed: %s", error->message);
      calendar_client_stop_query (source->client, source, query);
      return;
    }

  g_assert (source->query_in_progress != FALSE);
  g_assert (query == &source->in_progress_query);

  calendar_client_query_finalize (&source->completed_query);

  source->completed_query = source->in_progress_query;
  source->query_completed = TRUE;

  source->query_in_progress        = FALSE;
  source->in_progress_query.view   = NULL;
  source->in_progress_query.events = NULL;

  g_signal_emit (source->client, source->changed_signal_id, 0);
}

static void
calendar_client_handle_query_result (CalendarClientSource *source,
                                     GList                *objects,
                                     ECalClientView       *view)
{
  CalendarClientQuery *query;
  CalendarClient      *client;
  gboolean             emit_signal;
  gboolean             events_changed;
  GList               *l;
  time_t               month_begin;
  time_t               month_end;

  client = source->client;

  query = goddamn_this_is_crack (source, view, &emit_signal);

  month_begin = make_time_for_day_begin (1,
					 client->priv->month,
					 client->priv->year);

  month_end = make_time_for_day_begin (1,
				       client->priv->month + 1,
				       client->priv->year);

  events_changed = FALSE;
  for (l = objects; l; l = l->next)
    {
      CalendarEvent *event;
      CalendarEvent *old_event;
      ICalComponent *component = l->data;
      char          *uid;

      event = calendar_event_new (component, source, client->priv->zone);
      if (!event)
	      continue;

      calendar_event_generate_ocurrences (event,
                                          component,
                                          source->source,
                                          month_begin,
                                          month_end,
                                          client->priv->zone);

      uid = calendar_event_get_uid (event);

      old_event = g_hash_table_lookup (query->events, uid);

      if (!calendar_event_equal (event, old_event))
	{
	  calendar_event_debug_dump (event);

	  g_hash_table_replace (query->events, uid, event);

	  events_changed = TRUE;
	}
      else
	{
	  calendar_event_free (event);
	  g_free (uid);
	}
    }

  if (emit_signal && events_changed)
    {
      g_signal_emit (source->client, source->changed_signal_id, 0);
    }
}

static gboolean
check_object_remove (gpointer key,
                     gpointer value,
                     gpointer data)
{
  char *uid;
  size_t len;

  uid = data;
  len = strlen (uid);

  if (len <= strlen (key) && strncmp (uid, key, len) == 0)
    {
      calendar_event_debug_dump (value);

      return TRUE;
    }

  return FALSE;
}

static void
calendar_client_handle_objects_removed (CalendarClientSource *source,
                                        GList                *ids,
                                        ECalClientView       *view)
{
  CalendarClientQuery *query;
  gboolean             emit_signal;
  gboolean             events_changed;
  GList               *l;

  query = goddamn_this_is_crack (source, view, &emit_signal);

  events_changed = FALSE;
  for (l = ids; l; l = l->next)
    {
      CalendarEvent *event;
      ECalComponentId *id;
      const char *uid;
      const char *rid;
      char  *key;

      id = l->data;
      uid = e_cal_component_id_get_uid (id);
      rid = e_cal_component_id_get_rid (id);
      key = g_strdup_printf ("%s%s", uid, rid ? rid : "");

      if (!rid || !*rid)
        {
          guint size = g_hash_table_size (query->events);

          g_hash_table_foreach_remove (query->events, check_object_remove, (gpointer) uid);

          if (size != g_hash_table_size (query->events))
            events_changed = TRUE;
        }
      else if ((event = g_hash_table_lookup (query->events, key)))
        {
          calendar_event_debug_dump (event);

          g_assert (g_hash_table_remove (query->events, key));

          events_changed = TRUE;
        }

      g_free (key);
    }

  if (emit_signal && events_changed)
    {
      g_signal_emit (source->client, source->changed_signal_id, 0);
    }
}

static void
calendar_client_query_finalize (CalendarClientQuery *query)
{
  if (query->view)
    g_object_unref (query->view);
  query->view = NULL;

  if (query->events)
    g_hash_table_destroy (query->events);
  query->events = NULL;
}

static void
calendar_client_stop_query (CalendarClient       *client,
			    CalendarClientSource *source,
			    CalendarClientQuery  *query)
{
  if (query == &source->in_progress_query)
    {
      g_assert (source->query_in_progress != FALSE);

      source->query_in_progress = FALSE;
    }
  else if (query == &source->completed_query)
    {
      g_assert (source->query_completed != FALSE);

      source->query_completed = FALSE;
    }
  else
    g_assert_not_reached ();

  calendar_client_query_finalize (query);
}

static void
calendar_client_start_query (CalendarClient       *client,
			     CalendarClientSource *source,
			     const char           *query)
{
  ECalClientView *view = NULL;
  GError *error = NULL;

  if (!e_cal_client_get_view_sync (source->source, query, &view, NULL, &error))
    {
      g_warning ("Error preparing the query: '%s': %s",
                 query, error->message);

      g_error_free (error);
      return;
    }

  g_assert (view != NULL);

  if (source->query_in_progress)
    calendar_client_stop_query (client, source, &source->in_progress_query);

  source->query_in_progress        = TRUE;
  source->in_progress_query.view   = view;
  source->in_progress_query.events = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    g_free,
							    (GDestroyNotify) calendar_event_free);

  g_signal_connect_swapped (view, "objects-added",
			    G_CALLBACK (calendar_client_handle_query_result),
			    source);
  g_signal_connect_swapped (view, "objects-modified",
			    G_CALLBACK (calendar_client_handle_query_result),
			    source);
  g_signal_connect_swapped (view, "objects-removed",
			    G_CALLBACK (calendar_client_handle_objects_removed),
			    source);
  g_signal_connect_swapped (view, "complete",
			    G_CALLBACK (calendar_client_handle_query_completed),
			    source);

  e_cal_client_view_start (view, NULL);
}

static void
calendar_client_update_appointments (CalendarClient *client)
{
  GSList *l;
  char   *query;
  char   *month_begin;
  char   *month_end;

  if (client->priv->month == G_MAXUINT || client->priv->year == G_MAXUINT)
    return;

  month_begin = make_isodate_for_day_begin (1,
					    client->priv->month,
					    client->priv->year);

  month_end = make_isodate_for_day_begin (1,
					  client->priv->month + 1,
					  client->priv->year);

  query = g_strdup_printf ("occur-in-time-range? (make-time \"%s\") "
			                        "(make-time \"%s\")",
			   month_begin, month_end);

  for (l = client->priv->appointment_sources; l; l = l->next)
    {
      CalendarClientSource *cs = l->data;

      calendar_client_start_query (client, cs, query);
    }

  g_free (month_begin);
  g_free (month_end);
  g_free (query);
}

/* FIXME:
 * perhaps we should use evo's "hide_completed_tasks" pref?
 */
static void
calendar_client_update_tasks (CalendarClient *client)
{
  GSList *l;
  char   *query;

#ifdef FIX_BROKEN_TASKS_QUERY
  /* FIXME: this doesn't work for tasks without a start or
   *        due date
   *        Look at filter_task() to see the behaviour we
   *        want.
   */

  char   *day_begin;
  char   *day_end;

  if (client->priv->day == G_MAXUINT ||
      client->priv->month == G_MAXUINT ||
      client->priv->year == G_MAXUINT)
    return;

  day_begin = make_isodate_for_day_begin (client->priv->day,
					  client->priv->month,
					  client->priv->year);

  day_end = make_isodate_for_day_begin (client->priv->day + 1,
					client->priv->month,
					client->priv->year);
  if (!day_begin || !day_end)
    {
      g_warning ("Cannot run query with invalid date: %dd %dy %dm\n",
		 client->priv->day,
		 client->priv->month,
		 client->priv->year);
      g_free (day_begin);
      g_free (day_end);
      return;
    }

  query = g_strdup_printf ("(and (occur-in-time-range? (make-time \"%s\") "
                                                      "(make-time \"%s\")) "
                             "(or (not is-completed?) "
                               "(and (is-completed?) "
                                    "(not (completed-before? (make-time \"%s\"))))))",
			   day_begin, day_end, day_begin);
#else
  query = g_strdup ("#t");
#endif /* FIX_BROKEN_TASKS_QUERY */

  for (l = client->priv->task_sources; l; l = l->next)
    {
      CalendarClientSource *cs = l->data;

      calendar_client_start_query (client, cs, query);
    }

#ifdef FIX_BROKEN_TASKS_QUERY
  g_free (day_begin);
  g_free (day_end);
#endif
  g_free (query);
}

static void
calendar_client_source_finalize (CalendarClientSource *source)
{
  source->client = NULL;

  if (source->source) {
    g_object_unref (source->source);
  }
  source->source = NULL;

  calendar_client_query_finalize (&source->completed_query);
  calendar_client_query_finalize (&source->in_progress_query);

  source->query_completed   = FALSE;
  source->query_in_progress = FALSE;
}

static int
compare_calendar_sources (CalendarClientSource *s1,
			  CalendarClientSource *s2)
{
  return (s1->source == s2->source) ? 0 : 1;
}

static GSList *
calendar_client_update_sources_list (CalendarClient *client,
				     GSList         *sources,
				     GList          *esources,
				     guint           changed_signal_id)
{
  GList *link;
  GSList *retval, *l;

  retval = NULL;

  for (link = esources; link != NULL; link = g_list_next (link))
    {
      CalendarClientSource  dummy_source;
      CalendarClientSource *new_source;
      GSList               *s;
      ECalClient           *esource = link->data;

      dummy_source.source = esource;

      if ((s = g_slist_find_custom (sources,
				    &dummy_source,
				    (GCompareFunc) compare_calendar_sources)))
	{
	  new_source = s->data;
	  sources = g_slist_delete_link (sources, s);
	}
      else
	{
	  new_source                    = g_new0 (CalendarClientSource, 1);
	  new_source->client            = client;
	  new_source->source            = g_object_ref (esource);
	  new_source->changed_signal_id = changed_signal_id;
	}

      retval = g_slist_prepend (retval, new_source);
    }

  for (l = sources; l; l = l->next)
    {
      CalendarClientSource *source = l->data;

      calendar_client_source_finalize (source);
      g_free (source);
    }
  g_slist_free (sources);

  return retval;
}

static void
calendar_client_appointment_sources_changed (CalendarClient  *client)
{
  GList *list;

  list = calendar_sources_get_appointment_clients (client->priv->calendar_sources);

  client->priv->appointment_sources = calendar_client_update_sources_list (client,
									   client->priv->appointment_sources,
									   list,
									   signals [APPOINTMENTS_CHANGED]);

  load_calendars (client, CALENDAR_EVENT_APPOINTMENT);
  calendar_client_update_appointments (client);

  g_list_free (list);
}

static void
calendar_client_task_sources_changed (CalendarClient  *client)
{
  GList *list;

  list = calendar_sources_get_task_clients (client->priv->calendar_sources);

  client->priv->task_sources = calendar_client_update_sources_list (client,
								    client->priv->task_sources,
								    list,
								    signals [TASKS_CHANGED]);

  load_calendars (client, CALENDAR_EVENT_TASK);
  calendar_client_update_tasks (client);

  g_list_free (list);
}

void
calendar_client_get_date (CalendarClient *client,
                          guint          *year,
                          guint          *month,
                          guint          *day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));

  if (year)
    *year = client->priv->year;

  if (month)
    *month = client->priv->month;

  if (day)
    *day = client->priv->day;
}

void
calendar_client_select_month (CalendarClient *client,
			      guint           month,
			      guint           year)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (month <= 11);

  if (client->priv->year != year || client->priv->month != month)
    {
      client->priv->month = month;
      client->priv->year  = year;

      calendar_client_update_appointments (client);
      calendar_client_update_tasks (client);

      g_object_freeze_notify (G_OBJECT (client));
      g_object_notify (G_OBJECT (client), "month");
      g_object_notify (G_OBJECT (client), "year");
      g_object_thaw_notify (G_OBJECT (client));
    }
}

void
calendar_client_select_day (CalendarClient *client,
			    guint           day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (day <= 31);

  if (client->priv->day != day)
    {
      client->priv->day = day;

      /* don't need to update appointments unless
       * the selected month changes
       */
#ifdef FIX_BROKEN_TASKS_QUERY
      calendar_client_update_tasks (client);
#endif

      g_object_notify (G_OBJECT (client), "day");
    }
}

typedef struct
{
  CalendarClient *client;
  GSList         *events;
  time_t          start_time;
  time_t          end_time;
} FilterData;

typedef void (* CalendarEventFilterFunc) (const char    *uid,
					  CalendarEvent *event,
					  FilterData    *filter_data);

static void
filter_appointment (const char    *uid,
		    CalendarEvent *event,
		    FilterData    *filter_data)
{
  GSList *occurrences, *l;

  if (event->type != CALENDAR_EVENT_APPOINTMENT)
    return;

  occurrences = CALENDAR_APPOINTMENT (event)->occurrences;
  CALENDAR_APPOINTMENT (event)->occurrences = NULL;

  for (l = occurrences; l; l = l->next)
    {
      CalendarOccurrence *occurrence = l->data;
      time_t start_time = occurrence->start_time;
      time_t end_time   = occurrence->end_time;

      if ((start_time >= filter_data->start_time &&
           start_time < filter_data->end_time) ||
          (start_time <= filter_data->start_time &&
           (end_time - 1) > filter_data->start_time))
	{
	  CalendarEvent *new_event;

	  new_event = calendar_event_copy (event);

	  CALENDAR_APPOINTMENT (new_event)->start_time = occurrence->start_time;
	  CALENDAR_APPOINTMENT (new_event)->end_time   = occurrence->end_time;

	  filter_data->events = g_slist_prepend (filter_data->events, new_event);
	}
    }

  CALENDAR_APPOINTMENT (event)->occurrences = occurrences;
}

static void
filter_task (const char    *uid,
	     CalendarEvent *event,
	     FilterData    *filter_data)
{
#ifdef FIX_BROKEN_TASKS_QUERY
  CalendarTask *task;
#endif

  if (event->type != CALENDAR_EVENT_TASK)
    return;

#ifdef FIX_BROKEN_TASKS_QUERY
  task = CALENDAR_TASK (event);

  if (task->start_time && task->start_time > filter_data->start_time)
    return;

  if (task->completed_time &&
      (task->completed_time < filter_data->start_time ||
       task->completed_time > filter_data->end_time))
    return;
#endif /* FIX_BROKEN_TASKS_QUERY */

  filter_data->events = g_slist_prepend (filter_data->events,
					 calendar_event_copy (event));
}

static GSList *
calendar_client_filter_events (CalendarClient          *client,
			       GSList                  *sources,
			       CalendarEventFilterFunc  filter_func,
			       time_t                   start_time,
			       time_t                   end_time)
{
  FilterData  filter_data;
  GSList     *l;
  GSList     *retval;

  if (!sources)
    return NULL;

  filter_data.client     = client;
  filter_data.events     = NULL;
  filter_data.start_time = start_time;
  filter_data.end_time   = end_time;

  retval = NULL;
  for (l = sources; l; l = l->next)
    {
      CalendarClientSource *source = l->data;

      if (source->query_completed)
	{
	  filter_data.events = NULL;
	  g_hash_table_foreach (source->completed_query.events,
				(GHFunc) filter_func,
				&filter_data);

	  filter_data.events = g_slist_reverse (filter_data.events);

	  retval = g_slist_concat (retval, filter_data.events);
	}
    }

  return retval;
}

GSList *
calendar_client_get_events (CalendarClient    *client,
			    CalendarEventType  event_mask)
{
  GSList *appointments;
  GSList *tasks;
  time_t  day_begin;
  time_t  day_end;

  g_return_val_if_fail (CALENDAR_IS_CLIENT (client), NULL);
  g_return_val_if_fail (client->priv->day != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->month != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->year != G_MAXUINT, NULL);

  day_begin = make_time_for_day_begin (client->priv->day,
				       client->priv->month,
				       client->priv->year);
  day_end   = make_time_for_day_begin (client->priv->day + 1,
				       client->priv->month,
				       client->priv->year);

  appointments = NULL;
  if (event_mask & CALENDAR_EVENT_APPOINTMENT)
    {
      appointments = calendar_client_filter_events (client,
						    client->priv->appointment_sources,
						    filter_appointment,
						    day_begin,
						    day_end);
    }

  tasks = NULL;
  if (event_mask & CALENDAR_EVENT_TASK)
    {
      tasks = calendar_client_filter_events (client,
					     client->priv->task_sources,
					     filter_task,
					     day_begin,
					     day_end);
    }

  return g_slist_concat (appointments, tasks);
}

static inline int
day_from_time_t (time_t t)
{
  struct tm *tm = localtime (&t);

  g_assert (tm == NULL || (tm->tm_mday >=1 && tm->tm_mday <= 31));

  return tm ? tm->tm_mday : 0;
}

void
calendar_client_foreach_appointment_day (CalendarClient  *client,
					 CalendarDayIter  iter_func,
					 gpointer         user_data)
{
  GSList   *appointments, *l;
  gboolean  marked_days [32] = { FALSE, };
  time_t    month_begin;
  time_t    month_end;
  int       i;

  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (iter_func != NULL);
  g_return_if_fail (client->priv->month != G_MAXUINT);
  g_return_if_fail (client->priv->year != G_MAXUINT);

  month_begin = make_time_for_day_begin (1,
					 client->priv->month,
					 client->priv->year);
  month_end   = make_time_for_day_begin (1,
					 client->priv->month + 1,
					 client->priv->year);

  appointments = calendar_client_filter_events (client,
						client->priv->appointment_sources,
						filter_appointment,
						month_begin,
						month_end);
  for (l = appointments; l; l = l->next)
    {
      CalendarAppointment *appointment = l->data;

      if (appointment->start_time)
        {
          time_t day_time = appointment->start_time;

          if (day_time >= month_begin)
            marked_days [day_from_time_t (day_time)] = TRUE;

          if (appointment->end_time)
            {
              int day_offset;
              int duration = appointment->end_time - appointment->start_time;
	      /* mark the days for the appointment, no need to add an extra one when duration is a multiple of 86400 */
              for (day_offset = 1; day_offset <= duration / 86400 && duration != day_offset * 86400; day_offset++)
                {
                  time_t day_tm = appointment->start_time + day_offset * 86400;

                  if (day_tm > month_end)
                    break;
                  if (day_tm >= month_begin)
                    marked_days [day_from_time_t (day_tm)] = TRUE;
                }
            }
        }
      calendar_event_free (CALENDAR_EVENT (appointment));
    }

  g_slist_free (appointments);

  for (i = 1; i < 32; i++)
    {
      if (marked_days [i])
	iter_func (client, i, user_data);
    }
}

void
calendar_client_set_task_completed (CalendarClient *client,
				    char           *task_uid,
				    gboolean        task_completed,
				    guint           percent_complete)
{
  GSList *l;
  ECalClient *esource;
  ICalComponent *component;
  ICalProperty *prop;
  ICalPropertyStatus status;

  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (task_uid != NULL);
  g_return_if_fail (task_completed == FALSE || percent_complete == 100);

  component = NULL;
  esource = NULL;
  for (l = client->priv->task_sources; l; l = l->next)
    {
      CalendarClientSource *source = l->data;

      esource = source->source;
      e_cal_client_get_object_sync (esource, task_uid, NULL, &component, NULL, NULL);
      if (component)
        break;
    }

  if (!component)
    {
      g_warning ("Cannot locate task with uid = '%s'\n", task_uid);
      return;
    }

  g_assert (esource != NULL);

  /* Completed time */
  prop = i_cal_component_get_first_property (component, I_CAL_COMPLETED_PROPERTY);
  if (task_completed)
    {
      ICalTime *completed_time;

      completed_time = i_cal_time_new_current_with_zone (client->priv->zone);
      if (!prop)
        {
          i_cal_component_take_property (component,
                                         i_cal_property_new_completed (completed_time));
        }
      else
        {
          i_cal_property_set_completed (prop, completed_time);
        }
    }
  else if (prop)
    {
      i_cal_component_remove_property (component, prop);
    }
  g_clear_object (&prop);

  /* Percent complete */
  prop = i_cal_component_get_first_property (component, I_CAL_PERCENTCOMPLETE_PROPERTY);
  if (!prop)
    {
      i_cal_component_take_property (component,
                                    i_cal_property_new_percentcomplete (percent_complete));
    }
  else
    {
      i_cal_property_set_percentcomplete (prop, percent_complete);
    }
  g_clear_object (&prop);

  /* Status */
  status = task_completed ? I_CAL_STATUS_COMPLETED : I_CAL_STATUS_NEEDSACTION;
  prop = i_cal_component_get_first_property (component, I_CAL_STATUS_PROPERTY);
  if (prop)
    {
      i_cal_property_set_status (prop, status);
    }
  else
    {
      i_cal_component_take_property (component, i_cal_property_new_status (status));
    }
  g_clear_object (&prop);

  e_cal_client_modify_object_sync (esource,
                                   component,
                                   E_CAL_OBJ_MOD_ALL,
                                   0,
                                   NULL,
                                   NULL);
}

gboolean
calendar_client_create_task (CalendarClient *client,
                            const char     *summary)
{
  GSList *l;
  ECalClient *task_client = NULL;
  ICalComponent *vtodo_component;
  gchar *uid;
  GError *error = NULL;
  gboolean success = FALSE;

  g_return_val_if_fail (CALENDAR_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (summary != NULL && *summary != '\0', FALSE);

  /* Use the first available task source (like the existing code does) */
  for (l = client->priv->task_sources; l; l = l->next)
    {
      CalendarClientSource *source = l->data;
      task_client = source->source;
      if (task_client)
        break;
    }

  if (!task_client)
    {
      g_warning ("No task client available for task creation");
      return FALSE;
    }

  /* Create a simple VTODO component */
  vtodo_component = i_cal_component_new (I_CAL_VTODO_COMPONENT);

  /* Generate UID */
  uid = e_util_generate_uid ();
  i_cal_component_set_uid (vtodo_component, uid);
  g_free (uid);

  /* Set summary */
  i_cal_component_set_summary (vtodo_component, summary);

  /* Set created time */
  ICalTime *now = i_cal_time_new_current_with_zone (client->priv->zone);
  i_cal_component_set_dtstamp (vtodo_component, now);
  g_object_unref (now);

  /* Create the task */
  success = e_cal_client_create_object_sync (task_client,
                                            vtodo_component,
                                            E_CAL_OPERATION_FLAG_NONE,
                                            NULL, /* out uid */
                                            NULL, /* cancellable */
                                            &error);

  if (error)
    {
      g_warning ("Failed to create task: %s", error->message);
      g_error_free (error);
      success = FALSE;
    }

  /* Cleanup */
  g_object_unref (vtodo_component);

  return success;
}

#endif /* HAVE_EDS */
