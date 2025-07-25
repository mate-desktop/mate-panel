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

#include "calendar-sources.h"

#include <libintl.h>
#include <string.h>
#define HANDLE_LIBICAL_MEMORY


#include <libecal/libecal.h>

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

#ifndef _
#define _(x) gettext(x)
#endif

#ifndef N_
#define N_(x) x
#endif

typedef struct _ClientData ClientData;
typedef struct _CalendarSourceData CalendarSourceData;

struct _ClientData
{
  ECalClient *client;
  gulong      backend_died_id;
};

struct _CalendarSourceData
{
  ECalClientSourceType  source_type;
  CalendarSources      *sources;
  guint                 changed_signal;

  /* ESource -> EClient */
  GHashTable           *clients;

  guint                 timeout_id;

  guint                 loaded : 1;
};

struct _CalendarSourcesPrivate
{
  ESourceRegistry    *registry;
  gulong              source_added_id;
  gulong              source_changed_id;
  gulong              source_removed_id;

  CalendarSourceData  appointment_sources;
  CalendarSourceData  task_sources;
};

static void calendar_sources_finalize   (GObject             *object);

static void backend_died_cb (EClient *client, CalendarSourceData *source_data);
static void calendar_sources_registry_source_changed_cb (ESourceRegistry *registry,
                                                         ESource         *source,
                                                         CalendarSources *sources);
static void calendar_sources_registry_source_removed_cb (ESourceRegistry *registry,
                                                         ESource         *source,
                                                         CalendarSources *sources);

enum
{
  APPOINTMENT_SOURCES_CHANGED,
  TASK_SOURCES_CHANGED,
  LAST_SIGNAL
};
static guint signals [LAST_SIGNAL] = { 0, };

static CalendarSources *calendar_sources_singleton = NULL;

static void
client_data_free (ClientData *data)
{
  g_signal_handler_disconnect (data->client, data->backend_died_id);
  g_object_unref (data->client);
  g_free (data);
}

G_DEFINE_TYPE_WITH_PRIVATE (CalendarSources, calendar_sources, G_TYPE_OBJECT)

static void
calendar_sources_class_init (CalendarSourcesClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = calendar_sources_finalize;

  signals [APPOINTMENT_SOURCES_CHANGED] =
    g_signal_new ("appointment-sources-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarSourcesClass,
				   appointment_sources_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);

  signals [TASK_SOURCES_CHANGED] =
    g_signal_new ("task-sources-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarSourcesClass,
				   task_sources_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);
}

static void
calendar_sources_init (CalendarSources *sources)
{
  GError *error = NULL;

  sources->priv = calendar_sources_get_instance_private (sources);

  /* XXX Not sure what to do if this fails.
   *     Should this class implement GInitable or pass the
   *     registry in as a G_PARAM_CONSTRUCT_ONLY property? */
  sources->priv->registry = e_source_registry_new_sync (NULL, &error);
  if (error != NULL) {
    g_critical ("%s: %s", G_STRFUNC, error->message);
    g_error_free (error);
  }

  sources->priv->source_added_id   = g_signal_connect (sources->priv->registry,
                                                       "source-added",
                                                       G_CALLBACK (calendar_sources_registry_source_changed_cb),
                                                       sources);
  sources->priv->source_changed_id = g_signal_connect (sources->priv->registry,
                                                       "source-changed",
                                                       G_CALLBACK (calendar_sources_registry_source_changed_cb),
                                                       sources);
  sources->priv->source_removed_id = g_signal_connect (sources->priv->registry,
                                                       "source-removed",
                                                       G_CALLBACK (calendar_sources_registry_source_removed_cb),
                                                       sources);

  sources->priv->appointment_sources.source_type    = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
  sources->priv->appointment_sources.sources        = sources;
  sources->priv->appointment_sources.changed_signal = signals [APPOINTMENT_SOURCES_CHANGED];
  sources->priv->appointment_sources.clients        = g_hash_table_new_full ((GHashFunc) e_source_hash,
                                                                             (GEqualFunc) e_source_equal,
                                                                             (GDestroyNotify) g_object_unref,
                                                                             (GDestroyNotify) client_data_free);
  sources->priv->appointment_sources.timeout_id     = 0;

  sources->priv->task_sources.source_type    = E_CAL_CLIENT_SOURCE_TYPE_TASKS;
  sources->priv->task_sources.sources        = sources;
  sources->priv->task_sources.changed_signal = signals [TASK_SOURCES_CHANGED];
  sources->priv->task_sources.clients        = g_hash_table_new_full ((GHashFunc) e_source_hash,
                                                                      (GEqualFunc) e_source_equal,
                                                                      (GDestroyNotify) g_object_unref,
                                                                      (GDestroyNotify) client_data_free);
  sources->priv->task_sources.timeout_id     = 0;
}

static void
calendar_sources_finalize_source_data (CalendarSources    *sources,
				       CalendarSourceData *source_data)
{
  if (source_data->loaded)
    {
      g_hash_table_destroy (source_data->clients);
      source_data->clients = NULL;

      if (source_data->timeout_id != 0)
        {
          g_source_remove (source_data->timeout_id);
          source_data->timeout_id = 0;
        }

      source_data->loaded = FALSE;
    }
}

static void
calendar_sources_finalize (GObject *object)
{
  CalendarSources *sources = CALENDAR_SOURCES (object);

  if (sources->priv->registry)
    {
      g_signal_handler_disconnect (sources->priv->registry,
                                   sources->priv->source_added_id);
      g_signal_handler_disconnect (sources->priv->registry,
                                   sources->priv->source_changed_id);
      g_signal_handler_disconnect (sources->priv->registry,
                                   sources->priv->source_removed_id);
      g_object_unref (sources->priv->registry);
    }
  sources->priv->registry = NULL;

  calendar_sources_finalize_source_data (sources, &sources->priv->appointment_sources);
  calendar_sources_finalize_source_data (sources, &sources->priv->task_sources);

  G_OBJECT_CLASS (calendar_sources_parent_class)->finalize (object);
}

CalendarSources *
calendar_sources_get (void)
{
  gpointer singleton_location = &calendar_sources_singleton;

  if (calendar_sources_singleton)
    return g_object_ref (calendar_sources_singleton);

  calendar_sources_singleton = g_object_new (CALENDAR_TYPE_SOURCES, NULL);
  g_object_add_weak_pointer (G_OBJECT (calendar_sources_singleton),
			     singleton_location);

  return calendar_sources_singleton;
}

/* The clients are just created here but not loaded */
static void
create_client_for_source (ESource              *source,
                          ECalClientSourceType  source_type,
                          CalendarSourceData   *source_data)
{
  ClientData *data;
  GError *error;
  EClient *client;

  client = g_hash_table_lookup (source_data->clients, source);
  g_return_if_fail (client == NULL);

  error = NULL;
  client = e_cal_client_connect_sync (source, source_type, -1, NULL, &error);

  if (!client)
    {
      g_warning ("Could not load source '%s': %s",
                 e_source_get_uid (source),
                 error->message);

      g_clear_error (&error);
      return;
    }

  data = g_new0 (ClientData, 1);
  data->client = E_CAL_CLIENT (client); /* takes ownership */
  data->backend_died_id = g_signal_connect (client,
                                            "backend-died",
                                            G_CALLBACK (backend_died_cb),
                                            source_data);

  g_hash_table_insert (source_data->clients, g_object_ref (source), data);
}

static inline void
debug_dump_ecal_list (GHashTable *clients)
{
#ifdef CALENDAR_ENABLE_DEBUG
  GList *list, *link;

  dprintf ("Loaded clients:\n");
  list = g_hash_table_get_keys (clients);
  for (link = list; link != NULL; link = g_list_next (link))
    {
      ESource *source = E_SOURCE (link->data);

      dprintf ("  %s %s\n",
	       e_source_get_uid (source),
	       e_source_get_display_name (source));
    }
#endif
}

static void
calendar_sources_load_esource_list (ESourceRegistry *registry,
                                    CalendarSourceData *source_data);

static gboolean
backend_restart (gpointer data)
{
  CalendarSourceData *source_data = data;
  ESourceRegistry *registry;

  registry = source_data->sources->priv->registry;
  calendar_sources_load_esource_list (registry, source_data);
  g_signal_emit (source_data->sources, source_data->changed_signal, 0);

  source_data->timeout_id = 0;

  return FALSE;
}

static void
backend_died_cb (EClient *client, CalendarSourceData *source_data)
{
  ESource *source;
  const char *display_name;

  source = e_client_get_source (client);
  display_name = e_source_get_display_name (source);
  g_warning ("The calendar backend for '%s' has crashed.", display_name);
  g_hash_table_remove (source_data->clients, source);

  if (source_data->timeout_id != 0)
    {
      g_source_remove (source_data->timeout_id);
      source_data->timeout_id = 0;
    }

  source_data->timeout_id = g_timeout_add_seconds (2, backend_restart,
                                                   source_data);
}

static void
calendar_sources_load_esource_list (ESourceRegistry *registry,
                                    CalendarSourceData *source_data)
{
  GList *list, *link;
  const gchar *extension_name;

  switch (source_data->source_type)
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
        g_return_if_reached ();
    }

  list = e_source_registry_list_sources (registry, extension_name);

  for (link = list; link != NULL; link = g_list_next (link))
    {
      ESource *source = E_SOURCE (link->data);
      ESourceSelectable *extension;
      gboolean show_source;

      extension = e_source_get_extension (source, extension_name);
      show_source = e_source_get_enabled (source) && e_source_selectable_get_selected (extension);

      if (show_source)
        create_client_for_source (source, source_data->source_type, source_data);
    }

  debug_dump_ecal_list (source_data->clients);

  g_list_free_full (list, g_object_unref);
}

static void
calendar_sources_registry_source_changed_cb (ESourceRegistry *registry,
                                             ESource         *source,
                                             CalendarSources *sources)
{
  if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
    {
      CalendarSourceData *source_data;
      ESourceSelectable *extension;
      gboolean have_client;
      gboolean show_source;

      source_data = &sources->priv->appointment_sources;
      extension = e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
      have_client = (g_hash_table_lookup (source_data->clients, source) != NULL);
      show_source = e_source_get_enabled (source) && e_source_selectable_get_selected (extension);

      if (!show_source && have_client)
        {
          g_hash_table_remove (source_data->clients, source);
          g_signal_emit (sources, source_data->changed_signal, 0);
        }
      if (show_source && !have_client)
        {
          create_client_for_source (source, source_data->source_type, source_data);
          g_signal_emit (sources, source_data->changed_signal, 0);
        }
    }

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
    {
      CalendarSourceData *source_data;
      ESourceSelectable *extension;
      gboolean have_client;
      gboolean show_source;

      source_data = &sources->priv->task_sources;
      extension = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
      have_client = (g_hash_table_lookup (source_data->clients, source) != NULL);
      show_source = e_source_get_enabled (source) && e_source_selectable_get_selected (extension);

      if (!show_source && have_client)
        {
          g_hash_table_remove (source_data->clients, source);
          g_signal_emit (sources, source_data->changed_signal, 0);
        }
      if (show_source && !have_client)
        {
          create_client_for_source (source, source_data->source_type, source_data);
          g_signal_emit (sources, source_data->changed_signal, 0);
        }
    }
}

static void
calendar_sources_registry_source_removed_cb (ESourceRegistry *registry,
                                             ESource         *source,
                                             CalendarSources *sources)
{
  if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
    {
      CalendarSourceData *source_data;

      source_data = &sources->priv->appointment_sources;
      g_hash_table_remove (source_data->clients, source);
      g_signal_emit (sources, source_data->changed_signal, 0);
    }

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
    {
      CalendarSourceData *source_data;

      source_data = &sources->priv->task_sources;
      g_hash_table_remove (source_data->clients, source);
      g_signal_emit (sources, source_data->changed_signal, 0);
    }
}

GList *
calendar_sources_get_appointment_clients (CalendarSources *sources)
{
  GList *list, *link;

  g_return_val_if_fail (CALENDAR_IS_SOURCES (sources), NULL);

  if (!sources->priv->appointment_sources.loaded)
    {
      calendar_sources_load_esource_list (sources->priv->registry,
                                          &sources->priv->appointment_sources);
      sources->priv->appointment_sources.loaded = TRUE;
    }

  list = g_hash_table_get_values (sources->priv->appointment_sources.clients);

  for (link = list; link != NULL; link = g_list_next (link))
    link->data = ((ClientData *) link->data)->client;

  return list;
}

GList *
calendar_sources_get_task_clients (CalendarSources *sources)
{
  GList *list, *link;

  g_return_val_if_fail (CALENDAR_IS_SOURCES (sources), NULL);

  if (!sources->priv->task_sources.loaded)
    {
      calendar_sources_load_esource_list (sources->priv->registry,
                                          &sources->priv->task_sources);
      sources->priv->task_sources.loaded = TRUE;
    }

  list = g_hash_table_get_values (sources->priv->task_sources.clients);

  for (link = list; link != NULL; link = g_list_next (link))
    link->data = ((ClientData *) link->data)->client;

  return list;
}
