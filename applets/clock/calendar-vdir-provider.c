/*
 * calendar-vdir-provider.c: reads calendar events from a vdir collection
 *
 * A vdir collection is a directory where each .ics file contains exactly
 * one VEVENT or VTODO component (as produced by vdirsyncer).  Optional
 * plain-text metadata files 'color' (#RRGGBB) and 'displayname' live in
 * the same directory.
 *
 * Reference: https://vdirsyncer.readthedocs.io/en/stable/vdir.html
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
#include <time.h>

#include <glib.h>
#include <gio/gio.h>
#include <libical-glib/libical-glib.h>

#include "calendar-vdir-provider.h"

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

/* i_cal_duration_as_int was removed in newer libical-glib; compute manually. */
static time_t
ical_duration_as_secs (ICalDuration *dur)
{
  if (!dur) return 0;
  time_t secs = (time_t) i_cal_duration_get_weeks   (dur) * 7 * 86400
              + (time_t) i_cal_duration_get_days    (dur)     * 86400
              + (time_t) i_cal_duration_get_hours   (dur)     *  3600
              + (time_t) i_cal_duration_get_minutes (dur)     *    60
              + (time_t) i_cal_duration_get_seconds (dur);
  return i_cal_duration_is_neg (dur) ? -secs : secs;
}

/* In newer libical-glib the property getter functions take 'const ICalProperty *'
 * while older versions use 'ICalProperty *'.  These thin wrappers always accept
 * a non-const pointer so they can be used as function-pointer arguments on both
 * old and new versions without triggering -Wincompatible-function-pointer-types. */
static ICalTime *prop_get_dtstart   (ICalProperty *p) { return i_cal_property_get_dtstart   (p); }
static ICalTime *prop_get_dtend     (ICalProperty *p) { return i_cal_property_get_dtend     (p); }
static ICalTime *prop_get_due       (ICalProperty *p) { return i_cal_property_get_due       (p); }
static ICalTime *prop_get_completed (ICalProperty *p) { return i_cal_property_get_completed (p); }

struct _CalendarVdirProviderPrivate
{
  char         *directory;
  char         *display_name;  /* from 'displayname' file, or NULL */
  char         *color_string;  /* from 'color' file (#RRGGBB), or NULL */

  ICalTimezone *zone;          /* local timezone for time conversions */

  /* filename (basename) → ICalComponent * (parsed VCALENDAR wrapper) */
  GHashTable   *components;

  /* uid_string → CalendarEvent * (expanded for current month)          */
  GHashTable   *appointment_events;
  GHashTable   *task_events;

  guint         month;         /* 0-11; G_MAXUINT = unset */
  guint         year;          /* e.g. 2025; G_MAXUINT = unset */
  gboolean      loaded;        /* TRUE once the directory has been scanned */

  GFileMonitor *dir_monitor;
};

G_DEFINE_TYPE_WITH_PRIVATE (CalendarVdirProvider,
                             calendar_vdir_provider,
                             CALENDAR_TYPE_PROVIDER)

/* =========================================================================
 * Small utilities
 * ========================================================================= */

/* Read a plain-text metadata file from a vdir directory, strip trailing
 * whitespace, and return it as a newly-allocated string.  Returns NULL if
 * the file doesn't exist or is empty. */
static char *
read_metadata_file (const char *directory, const char *filename)
{
  char *path     = g_build_filename (directory, filename, NULL);
  char *contents = NULL;

  if (g_file_get_contents (path, &contents, NULL, NULL) && contents != NULL)
    {
      g_strchomp (contents);
      if (*contents == '\0')
        {
          g_free (contents);
          contents = NULL;
        }
    }

  g_free (path);
  return contents;
}

static inline time_t
make_month_begin (guint month, guint year)
{
  struct tm tm = { 0, };
  tm.tm_mday  = 1;
  tm.tm_mon   = (int) month;
  tm.tm_year  = (int) year - 1900;
  tm.tm_isdst = -1;
  return mktime (&tm);
}

static inline time_t
make_month_end (guint month, guint year)
{
  if (month == 11)
    return make_month_begin (0, year + 1);
  return make_month_begin (month + 1, year);
}

/* =========================================================================
 * Recurrence expansion
 * =========================================================================
 *
 * Given an ICalComponent (VEVENT or VTODO) and a half-open time range
 * [range_start, range_end), return a list of CalendarOccurrence* that fall
 * within that range.  Handles RRULE, RDATE, and EXDATE per RFC 5545.
 */
static GSList *
expand_recurrences (ICalComponent *comp,
                    ICalTimezone  *zone,
                    time_t         range_start,
                    time_t         range_end)
{
  GSList       *result   = NULL;
  ICalTime     *dtstart  = NULL;
  ICalTime     *dtend    = NULL;
  ICalProperty *prop     = NULL;
  time_t        duration = 0;

  dtstart = i_cal_component_get_dtstart (comp);
  if (!dtstart || i_cal_time_is_null_time (dtstart))
    {
      if (dtstart) g_object_unref (dtstart);
      return NULL;
    }

  /* ------------------------------------------------------------------
   * Compute the duration of a single occurrence
   * ------------------------------------------------------------------ */
  dtend = i_cal_component_get_dtend (comp);
  if (dtend != NULL && !i_cal_time_is_null_time (dtend))
    {
      duration = i_cal_time_as_timet (dtend) - i_cal_time_as_timet (dtstart);
    }
  else
    {
      prop = i_cal_component_get_first_property (comp, I_CAL_DURATION_PROPERTY);
      if (prop != NULL)
        {
          ICalDuration *dur = i_cal_property_get_duration (prop);
          duration = ical_duration_as_secs (dur);
          g_object_unref (dur);
          g_object_unref (prop);
          prop = NULL;
        }
    }
  if (dtend) g_object_unref (dtend);
  if (duration < 0) duration = 0;

  /* ------------------------------------------------------------------
   * Collect EXDATE values for exclusion checking
   * ------------------------------------------------------------------ */
  GSList *exdates = NULL;
  for (prop = i_cal_component_get_first_property (comp, I_CAL_EXDATE_PROPERTY);
       prop != NULL;
       prop = i_cal_component_get_next_property (comp, I_CAL_EXDATE_PROPERTY))
    {
      ICalTime *extime = i_cal_property_get_exdate (prop);
      if (extime != NULL && !i_cal_time_is_null_time (extime))
        {
          time_t *t = g_new (time_t, 1);
          *t = i_cal_time_as_timet_with_zone (extime, zone);
          exdates = g_slist_prepend (exdates, t);
        }
      if (extime) g_object_unref (extime);
      g_object_unref (prop);
    }
  prop = NULL;

  /* Helper: add an occurrence if it overlaps range and isn't excluded. */
#define MAYBE_ADD_OCC(occ_start)                                          \
  do {                                                                    \
    time_t _s = (occ_start);                                              \
    time_t _e = _s + duration;                                            \
    if (_s < range_end && _e > range_start)                               \
      {                                                                   \
        gboolean _excl = FALSE;                                           \
        for (GSList *_el = exdates; _el; _el = _el->next)                 \
          if (*(time_t *)_el->data == _s) { _excl = TRUE; break; }       \
        if (!_excl)                                                       \
          {                                                               \
            CalendarOccurrence *_o = g_new0 (CalendarOccurrence, 1);     \
            _o->start_time = _s;                                          \
            _o->end_time   = _e;                                          \
            result = g_slist_prepend (result, _o);                       \
          }                                                               \
      }                                                                   \
  } while (0)

  /* ------------------------------------------------------------------
   * RRULE expansion
   * ------------------------------------------------------------------ */
  prop = i_cal_component_get_first_property (comp, I_CAL_RRULE_PROPERTY);
  if (prop != NULL)
    {
      ICalRecurrence   *rrule = i_cal_property_get_rrule (prop);
      ICalRecurIterator *iter  = i_cal_recur_iterator_new (rrule, dtstart);

      ICalTime *next;
      while (TRUE)
        {
          next = i_cal_recur_iterator_next (iter);
          if (next == NULL || i_cal_time_is_null_time (next))
            {
              if (next) g_object_unref (next);
              break;
            }

          time_t occ_start = i_cal_time_as_timet_with_zone (next, zone);
          g_object_unref (next);

          /* RRULE iterates in ascending order; stop once past range */
          if (occ_start >= range_end)
            break;

          MAYBE_ADD_OCC (occ_start);
        }

      g_object_unref (iter);
      g_object_unref (rrule);
      g_object_unref (prop);
      prop = NULL;
    }
  else
    {
      /* No RRULE: single occurrence at DTSTART */
      time_t occ_start = i_cal_time_as_timet_with_zone (dtstart, zone);
      MAYBE_ADD_OCC (occ_start);

      /* Plus any RDATE entries */
      for (prop = i_cal_component_get_first_property (comp, I_CAL_RDATE_PROPERTY);
           prop != NULL;
           prop = i_cal_component_get_next_property (comp, I_CAL_RDATE_PROPERTY))
        {
          ICalDatetimeperiod *rdtp = i_cal_property_get_rdate (prop);
          if (rdtp != NULL)
            {
              ICalTime *rdt = i_cal_datetimeperiod_get_time (rdtp);
              if (rdt != NULL && !i_cal_time_is_null_time (rdt))
                {
                  time_t rd_start = i_cal_time_as_timet_with_zone (rdt, zone);
                  MAYBE_ADD_OCC (rd_start);
                }
              if (rdt) g_object_unref (rdt);
              g_object_unref (rdtp);
            }
          g_object_unref (prop);
        }
      prop = NULL;
    }

#undef MAYBE_ADD_OCC

  g_slist_free_full (exdates, g_free);
  g_object_unref (dtstart);

  return result;
}

/* =========================================================================
 * Component → CalendarEvent conversion
 * ========================================================================= */

/* Generic helper: get a string property value (caller owns return value). */
static char *
get_str_prop (ICalComponent *comp, ICalPropertyKind kind,
              const char *(*getter)(ICalProperty *))
{
  ICalProperty *prop = i_cal_component_get_first_property (comp, kind);
  if (!prop)
    return NULL;
  char *val = g_strdup (getter (prop));
  g_object_unref (prop);
  return val;
}

/* Generic helper: get a time_t from a datetime property. */
static time_t
get_time_prop (ICalComponent *comp,
               ICalPropertyKind kind,
               ICalTime *(*getter)(ICalProperty *),
               ICalTimezone *zone)
{
  ICalProperty *prop = i_cal_component_get_first_property (comp, kind);
  if (!prop)
    return 0;
  ICalTime *t = getter (prop);
  g_object_unref (prop);
  if (!t || i_cal_time_is_null_time (t))
    {
      if (t) g_object_unref (t);
      return 0;
    }
  time_t result = i_cal_time_as_timet_with_zone (t, zone);
  g_object_unref (t);
  return result;
}

/*
 * Build a CalendarAppointment (wrapped in CalendarEvent) from a VEVENT
 * component.  Occurrences that fall within [range_start, range_end) are
 * pre-expanded and stored in appointment->occurrences.
 * Returns NULL if there are no occurrences in the range.
 */
static CalendarEvent *
vevent_to_appointment (ICalComponent *comp,
                       const char    *color_string,
                       const char    *backend_name,
                       ICalTimezone  *zone,
                       time_t         range_start,
                       time_t         range_end)
{
  GSList *occurrences = expand_recurrences (comp, zone, range_start, range_end);
  if (occurrences == NULL)
    return NULL;

  CalendarEvent *event = g_new0 (CalendarEvent, 1);
  event->type = CALENDAR_EVENT_APPOINTMENT;

  CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
  appt->uid          = get_str_prop (comp, I_CAL_UID_PROPERTY,
                                      (const char *(*)(ICalProperty *)) i_cal_property_get_uid);
  appt->rid          = NULL;
  appt->backend_name = g_strdup (backend_name);
  appt->summary      = get_str_prop (comp, I_CAL_SUMMARY_PROPERTY,
                                      (const char *(*)(ICalProperty *)) i_cal_property_get_summary);
  appt->description  = get_str_prop (comp, I_CAL_DESCRIPTION_PROPERTY,
                                      (const char *(*)(ICalProperty *)) i_cal_property_get_description);
  appt->color_string = g_strdup (color_string);
  appt->occurrences  = occurrences;

  /* Set primary start/end from the first occurrence (they are overridden
   * per-occurrence during filtering in get_events()). */
  CalendarOccurrence *first = (CalendarOccurrence *) occurrences->data;
  appt->start_time = first->start_time;
  appt->end_time   = first->end_time;
  appt->is_all_day = (first->end_time - first->start_time) % 86400 == 0;

  return event;
}

/*
 * Build a CalendarTask (wrapped in CalendarEvent) from a VTODO component.
 */
static CalendarEvent *
vtodo_to_task (ICalComponent *comp,
               const char    *color_string,
               ICalTimezone  *zone)
{
  CalendarEvent *event = g_new0 (CalendarEvent, 1);
  event->type = CALENDAR_EVENT_TASK;

  CalendarTask *task = CALENDAR_TASK (event);
  task->uid         = get_str_prop (comp, I_CAL_UID_PROPERTY,
                                     (const char *(*)(ICalProperty *)) i_cal_property_get_uid);
  task->summary     = get_str_prop (comp, I_CAL_SUMMARY_PROPERTY,
                                     (const char *(*)(ICalProperty *)) i_cal_property_get_summary);
  task->description = get_str_prop (comp, I_CAL_DESCRIPTION_PROPERTY,
                                     (const char *(*)(ICalProperty *)) i_cal_property_get_description);
  task->color_string = g_strdup (color_string);
  task->url         = get_str_prop (comp, I_CAL_URL_PROPERTY,
                                     (const char *(*)(ICalProperty *)) i_cal_property_get_url);

  task->start_time     = get_time_prop (comp, I_CAL_DTSTART_PROPERTY,
                                         prop_get_dtstart, zone);
  task->due_time       = get_time_prop (comp, I_CAL_DUE_PROPERTY,
                                         prop_get_due, zone);
  task->completed_time = get_time_prop (comp, I_CAL_COMPLETED_PROPERTY,
                                         prop_get_completed, zone);

  ICalPropertyStatus status = i_cal_component_get_status (comp);
  if (status == I_CAL_STATUS_COMPLETED)
    {
      task->percent_complete = 100;
    }
  else
    {
      ICalProperty *prop =
        i_cal_component_get_first_property (comp, I_CAL_PERCENTCOMPLETE_PROPERTY);
      if (prop)
        {
          task->percent_complete = CLAMP (i_cal_property_get_percentcomplete (prop), 0, 100);
          g_object_unref (prop);
        }
    }

  ICalProperty *prio_prop =
    i_cal_component_get_first_property (comp, I_CAL_PRIORITY_PROPERTY);
  if (prio_prop)
    {
      task->priority = i_cal_property_get_priority (prio_prop);
      g_object_unref (prio_prop);
    }
  else
    {
      task->priority = -1;
    }

  return event;
}

/* =========================================================================
 * Loading and caching
 * ========================================================================= */

/* Re-build appointment_events and task_events from the loaded components
 * for the current month/year window. */
static void
vdir_rebuild_month_cache (CalendarVdirProvider *self)
{
  CalendarVdirProviderPrivate *priv = self->priv;

  if (priv->month == G_MAXUINT || priv->year == G_MAXUINT)
    return;

  time_t range_start = make_month_begin (priv->month, priv->year);
  time_t range_end   = make_month_end   (priv->month, priv->year);

  g_hash_table_remove_all (priv->appointment_events);
  g_hash_table_remove_all (priv->task_events);

  const char *display_name =
    (priv->display_name != NULL) ? priv->display_name : priv->directory;

  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, priv->components);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      ICalComponent *vcal  = I_CAL_COMPONENT (value);
      /* In a vdir file the outer component is VCALENDAR; the single inner
       * component is the VEVENT or VTODO. */
      ICalComponent *inner = i_cal_component_get_inner (vcal);
      if (inner == NULL)
        continue;

      ICalComponentKind kind = i_cal_component_isa (inner);

      if (kind == I_CAL_VEVENT_COMPONENT)
        {
          CalendarEvent *ev = vevent_to_appointment (inner,
                                                      priv->color_string,
                                                      display_name,
                                                      priv->zone,
                                                      range_start,
                                                      range_end);
          if (ev != NULL)
            {
              const char *uid = CALENDAR_APPOINTMENT (ev)->uid;
              /* Fall back to the filename as key if the component has no UID */
              char *uid_key = g_strdup (uid ? uid : (const char *) key);
              g_hash_table_replace (priv->appointment_events, uid_key, ev);
            }
        }
      else if (kind == I_CAL_VTODO_COMPONENT)
        {
          CalendarEvent *ev = vtodo_to_task (inner, priv->color_string, priv->zone);
          if (ev != NULL)
            {
              const char *uid = CALENDAR_TASK (ev)->uid;
              char *uid_key = g_strdup (uid ? uid : (const char *) key);
              g_hash_table_replace (priv->task_events, uid_key, ev);
            }
        }

      g_object_unref (inner);
    }
}

/* Parse a single .ics file and insert / replace its component in the
 * components hash table. */
static void
vdir_load_file (CalendarVdirProvider *self, const char *basename)
{
  CalendarVdirProviderPrivate *priv = self->priv;
  char  *path     = g_build_filename (priv->directory, basename, NULL);
  char  *contents = NULL;

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    {
      g_free (path);
      return;
    }

  ICalComponent *comp = i_cal_parser_parse_string (contents);
  if (comp != NULL)
    g_hash_table_replace (priv->components, g_strdup (basename), comp);

  g_free (contents);
  g_free (path);
}

/* Scan the vdir directory and (re)load all .ics files. */
static void
vdir_load_all_files (CalendarVdirProvider *self)
{
  CalendarVdirProviderPrivate *priv  = self->priv;
  GError                      *error = NULL;

  g_hash_table_remove_all (priv->components);

  GDir *dir = g_dir_open (priv->directory, 0, &error);
  if (dir == NULL)
    {
      g_warning ("CalendarVdirProvider: cannot open '%s': %s",
                 priv->directory, error->message);
      g_error_free (error);
      return;
    }

  const char *name;
  while ((name = g_dir_read_name (dir)) != NULL)
    {
      /* Per vdir spec: ignore .tmp files and files without an extension */
      if (g_str_has_suffix (name, ".tmp") || strchr (name, '.') == NULL)
        continue;
      if (!g_str_has_suffix (name, ".ics"))
        continue;

      vdir_load_file (self, name);
    }

  g_dir_close (dir);
}

/* =========================================================================
 * GFileMonitor callback
 * ========================================================================= */

static void
on_vdir_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 gpointer           user_data)
{
  CalendarVdirProvider        *self = CALENDAR_VDIR_PROVIDER (user_data);
  CalendarVdirProviderPrivate *priv = self->priv;

  (void) monitor;
  (void) other_file;

  char    *basename = g_file_get_basename (file);
  gboolean is_ics   = g_str_has_suffix (basename, ".ics") &&
                      !g_str_has_suffix (basename, ".tmp");
  gboolean is_meta  = (g_strcmp0 (basename, "displayname") == 0 ||
                       g_strcmp0 (basename, "color") == 0);

  if (!is_ics && !is_meta)
    {
      g_free (basename);
      return;
    }

  if (is_meta)
    {
      g_free (priv->display_name);
      g_free (priv->color_string);
      priv->display_name = read_metadata_file (priv->directory, "displayname");
      priv->color_string = read_metadata_file (priv->directory, "color");
      vdir_rebuild_month_cache (self);
      calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
      calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));
      g_free (basename);
      return;
    }

  /* .ics file event */
  switch (event_type)
    {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
      vdir_load_file (self, basename);
      break;
    case G_FILE_MONITOR_EVENT_DELETED:
      g_hash_table_remove (priv->components, basename);
      break;
    default:
      g_free (basename);
      return;
    }

  /* Figure out whether the affected component is an appointment or task so
   * we can emit the narrower signal. */
  gboolean is_appointment = TRUE;
  ICalComponent *vcal = g_hash_table_lookup (priv->components, basename);
  if (vcal != NULL)
    {
      ICalComponent *inner = i_cal_component_get_inner (vcal);
      if (inner != NULL)
        {
          is_appointment = (i_cal_component_isa (inner) != I_CAL_VTODO_COMPONENT);
          g_object_unref (inner);
        }
    }

  vdir_rebuild_month_cache (self);

  if (is_appointment)
    calendar_provider_emit_appointments_changed (CALENDAR_PROVIDER (self));
  else
    calendar_provider_emit_tasks_changed (CALENDAR_PROVIDER (self));

  g_free (basename);
}

/* =========================================================================
 * CalendarProvider vtable
 * ========================================================================= */

static void
vdir_select_month (CalendarProvider *provider, guint month, guint year)
{
  CalendarVdirProvider        *self = CALENDAR_VDIR_PROVIDER (provider);
  CalendarVdirProviderPrivate *priv = self->priv;

  if (priv->month == month && priv->year == year && priv->loaded)
    return;

  priv->month = month;
  priv->year  = year;

  if (!priv->loaded)
    {
      vdir_load_all_files (self);
      priv->loaded = TRUE;
    }

  vdir_rebuild_month_cache (self);
}

static void
vdir_select_day (CalendarProvider *provider, guint day)
{
  /* Day-level filtering is handled in get_events(); nothing to cache. */
  (void) provider;
  (void) day;
}

static GSList *
vdir_get_events (CalendarProvider  *provider,
                 CalendarEventType  event_mask,
                 time_t             start_time,
                 time_t             end_time)
{
  CalendarVdirProvider        *self = CALENDAR_VDIR_PROVIDER (provider);
  CalendarVdirProviderPrivate *priv = self->priv;
  GSList *result = NULL;

  if (event_mask & CALENDAR_EVENT_APPOINTMENT)
    {
      GSList *appts = calendar_provider_filter_events_by_range (priv->appointment_events,
                                                                 CALENDAR_EVENT_APPOINTMENT,
                                                                 start_time, end_time);
      result = g_slist_concat (result, appts);
    }

  if (event_mask & CALENDAR_EVENT_TASK)
    {
      GSList *tasks = calendar_provider_filter_events_by_range (priv->task_events,
                                                                 CALENDAR_EVENT_TASK,
                                                                 start_time, end_time);
      result = g_slist_concat (result, tasks);
    }

  return result;
}

/* =========================================================================
 * GObject lifecycle
 * ========================================================================= */

static void
calendar_vdir_provider_finalize (GObject *object)
{
  CalendarVdirProvider        *self = CALENDAR_VDIR_PROVIDER (object);
  CalendarVdirProviderPrivate *priv = self->priv;

  if (priv->dir_monitor != NULL)
    {
      g_file_monitor_cancel (priv->dir_monitor);
      g_clear_object (&priv->dir_monitor);
    }

  g_hash_table_destroy (priv->components);
  g_hash_table_destroy (priv->appointment_events);
  g_hash_table_destroy (priv->task_events);

  g_free (priv->directory);
  g_free (priv->display_name);
  g_free (priv->color_string);

  G_OBJECT_CLASS (calendar_vdir_provider_parent_class)->finalize (object);
}

static void
calendar_vdir_provider_class_init (CalendarVdirProviderClass *klass)
{
  GObjectClass          *gobject_class  = G_OBJECT_CLASS (klass);
  CalendarProviderClass *provider_class = CALENDAR_PROVIDER_CLASS (klass);

  gobject_class->finalize       = calendar_vdir_provider_finalize;

  provider_class->select_month  = vdir_select_month;
  provider_class->select_day    = vdir_select_day;
  provider_class->get_events    = vdir_get_events;
  /* set_task_completed and create_task intentionally left NULL:
   * vdir collections are read-only from the applet's perspective. */
}

static void
calendar_vdir_provider_init (CalendarVdirProvider *self)
{
  self->priv = calendar_vdir_provider_get_instance_private (self);
  CalendarVdirProviderPrivate *priv = self->priv;

  priv->month  = G_MAXUINT;
  priv->year   = G_MAXUINT;
  priv->loaded = FALSE;

  /* Default timezone: attempt to determine the local zone from the system */
  priv->zone = i_cal_timezone_get_utc_timezone ();

  priv->components = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free,
                                             (GDestroyNotify) g_object_unref);
  priv->appointment_events =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                            g_free,
                            (GDestroyNotify) calendar_event_free);
  priv->task_events =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                            g_free,
                            (GDestroyNotify) calendar_event_free);
}

/* =========================================================================
 * Public constructor
 * ========================================================================= */

CalendarProvider *
calendar_vdir_provider_new (const char *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);

  if (!g_file_test (directory, G_FILE_TEST_IS_DIR))
    return NULL;

  CalendarVdirProvider        *self = g_object_new (CALENDAR_TYPE_VDIR_PROVIDER, NULL);
  CalendarVdirProviderPrivate *priv = self->priv;

  priv->directory    = g_strdup (directory);
  priv->display_name = read_metadata_file (directory, "displayname");
  priv->color_string = read_metadata_file (directory, "color");

  /* Resolve the local timezone via the system TZ name */
  {
    time_t    now   = time (NULL);
    struct tm local = { 0, };
    localtime_r (&now, &local);
    char tz_name[64];
    if (strftime (tz_name, sizeof (tz_name), "%Z", &local) > 0)
      {
        ICalTimezone *tz = i_cal_timezone_get_builtin_timezone (tz_name);
        if (tz != NULL)
          priv->zone = tz;
      }
  }

  /* Set up a directory monitor for live updates */
  GFile  *dir_file = g_file_new_for_path (directory);
  GError *error    = NULL;
  priv->dir_monitor =
    g_file_monitor_directory (dir_file, G_FILE_MONITOR_NONE, NULL, &error);
  g_object_unref (dir_file);

  if (priv->dir_monitor != NULL)
    {
      g_signal_connect (priv->dir_monitor, "changed",
                        G_CALLBACK (on_vdir_changed), self);
    }
  else
    {
      g_warning ("CalendarVdirProvider: cannot monitor '%s': %s",
                 directory, error != NULL ? error->message : "unknown error");
      g_clear_error (&error);
    }

  return CALENDAR_PROVIDER (self);
}

/* =========================================================================
 * Auto-discovery
 * =========================================================================
 *
 * Scan @base_path for subdirectories that look like vdir collections.
 * A directory qualifies if it contains at least one .ics file, or has a
 * 'displayname' or 'color' metadata file (an empty-but-configured calendar).
 */
GSList *
calendar_vdir_discover (const char *base_path)
{
  GSList *providers = NULL;
  GError *error     = NULL;

  if (base_path == NULL || !g_file_test (base_path, G_FILE_TEST_IS_DIR))
    return NULL;

  GDir *dir = g_dir_open (base_path, 0, &error);
  if (dir == NULL)
    {
      g_debug ("CalendarVdirProvider: cannot scan '%s': %s", base_path, error->message);
      g_error_free (error);
      return NULL;
    }

  const char *entry;
  while ((entry = g_dir_read_name (dir)) != NULL)
    {
      char *subdir = g_build_filename (base_path, entry, NULL);

      if (!g_file_test (subdir, G_FILE_TEST_IS_DIR))
        {
          g_free (subdir);
          continue;
        }

      /* Check whether this looks like a vdir collection */
      gboolean looks_like_vdir = FALSE;

      /* Quick check: metadata files */
      char *meta_path = g_build_filename (subdir, "displayname", NULL);
      if (g_file_test (meta_path, G_FILE_TEST_IS_REGULAR))
        looks_like_vdir = TRUE;
      g_free (meta_path);

      if (!looks_like_vdir)
        {
          meta_path = g_build_filename (subdir, "color", NULL);
          if (g_file_test (meta_path, G_FILE_TEST_IS_REGULAR))
            looks_like_vdir = TRUE;
          g_free (meta_path);
        }

      /* Deeper check: any .ics file */
      if (!looks_like_vdir)
        {
          GDir *sub = g_dir_open (subdir, 0, NULL);
          if (sub != NULL)
            {
              const char *f;
              while ((f = g_dir_read_name (sub)) != NULL)
                {
                  if (g_str_has_suffix (f, ".ics") &&
                      !g_str_has_suffix (f, ".tmp"))
                    {
                      looks_like_vdir = TRUE;
                      break;
                    }
                }
              g_dir_close (sub);
            }
        }

      if (looks_like_vdir)
        {
          CalendarProvider *p = calendar_vdir_provider_new (subdir);
          if (p != NULL)
            providers = g_slist_prepend (providers, p);
        }
      else
        {
          /* Not a collection itself — recurse one level (vdirsyncer stores
           * collections as <base>/<pair-name>/<collection-name>/) */
          GSList *nested = calendar_vdir_discover (subdir);
          providers = g_slist_concat (providers, nested);
        }

      g_free (subdir);
    }

  g_dir_close (dir);
  return providers;
}
