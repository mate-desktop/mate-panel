/*
 * Copyright (C) 2016 Alberts Muktupāvels
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "config.h"

#include "gf-sn-watcher-v0.h"

struct _GfSnWatcherV0
{
  GfSnWatcherV0GenSkeleton  parent;

  guint                     bus_name_id;

  GSList                   *hosts;
  GSList                   *items;
};

typedef enum
{
  GF_WATCH_TYPE_HOST,
  GF_WATCH_TYPE_ITEM
} GfWatchType;

typedef struct
{
  GfSnWatcherV0 *v0;
  GfWatchType    type;

  gchar         *service;
  gchar         *bus_name;
  gchar         *object_path;
  guint          watch_id;
} GfWatch;

static void gf_sn_watcher_v0_gen_init (GfSnWatcherV0GenIface *iface);

G_DEFINE_TYPE_WITH_CODE (GfSnWatcherV0, gf_sn_watcher_v0, GF_TYPE_SN_WATCHER_V0_GEN_SKELETON,
                         G_IMPLEMENT_INTERFACE (GF_TYPE_SN_WATCHER_V0_GEN, gf_sn_watcher_v0_gen_init))

static void
update_registered_items (GfSnWatcherV0 *v0)
{
  GVariantBuilder builder;
  GSList *l;
  GVariant *variant;
  const gchar **items;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

  for (l = v0->items; l != NULL; l = g_slist_next (l))
    {
      GfWatch *watch;
      gchar *item;

      watch = (GfWatch *) l->data;

      item = g_strdup_printf ("%s%s", watch->bus_name, watch->object_path);
      g_variant_builder_add (&builder, "s", item);
      g_free (item);
    }

  variant = g_variant_builder_end (&builder);
  items = g_variant_get_strv (variant, NULL);

  gf_sn_watcher_v0_gen_set_registered_items (GF_SN_WATCHER_V0_GEN (v0), items);
  g_variant_unref (variant);
  g_free (items);
}

static void
gf_watch_free (gpointer data)
{
  GfWatch *watch;

  watch = (GfWatch *) data;

  if (watch->watch_id > 0)
    g_bus_unwatch_name (watch->watch_id);

  g_free (watch->service);
  g_free (watch->bus_name);
  g_free (watch->object_path);

  g_free (watch);
}

static void
name_vanished_cb (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  GfWatch *watch;
  GfSnWatcherV0 *v0;
  GfSnWatcherV0Gen *gen;

  watch = (GfWatch *) user_data;
  v0 = watch->v0;
  gen = GF_SN_WATCHER_V0_GEN (v0);

  if (watch->type == GF_WATCH_TYPE_HOST)
    {
      v0->hosts = g_slist_remove (v0->hosts, watch);

      if (v0->hosts == NULL)
        {
          gf_sn_watcher_v0_gen_set_is_host_registered (gen, FALSE);
          gf_sn_watcher_v0_gen_emit_host_registered (gen);
        }
    }
  else if (watch->type == GF_WATCH_TYPE_ITEM)
    {
      gchar *tmp;

      v0->items = g_slist_remove (v0->items, watch);

      update_registered_items (v0);

      tmp = g_strdup_printf ("%s%s", watch->bus_name, watch->object_path);
      gf_sn_watcher_v0_gen_emit_item_unregistered (gen, tmp);
      g_free (tmp);
    }
  else
    {
      g_assert_not_reached ();
    }

  gf_watch_free (watch);
}

static GfWatch *
gf_watch_new (GfSnWatcherV0 *v0,
              GfWatchType    type,
              const gchar   *service,
              const gchar   *bus_name,
              const gchar   *object_path)
{
  GfWatch *watch;

  watch = g_new0 (GfWatch, 1);

  watch->v0 = v0;
  watch->type = type;

  watch->service = g_strdup (service);
  watch->bus_name = g_strdup (bus_name);
  watch->object_path = g_strdup (object_path);
  watch->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION, bus_name,
                                      G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
                                      name_vanished_cb, watch, NULL);

  return watch;
}

static GfWatch *
gf_watch_find (GSList      *list,
               const gchar *bus_name,
               const gchar *object_path)
{
  GSList *l;

  for (l = list; l != NULL; l = g_slist_next (l))
    {
      GfWatch *watch;

      watch = (GfWatch *) l->data;

      if (g_strcmp0 (watch->bus_name, bus_name) == 0 &&
          g_strcmp0 (watch->object_path, object_path) == 0)
        {
          return watch;
        }
    }

  return NULL;
}

static gboolean
gf_sn_watcher_v0_handle_register_host (GfSnWatcherV0Gen      *object,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *service)
{
  GfSnWatcherV0 *v0;
  const gchar *bus_name;
  const gchar *object_path;
  GfWatch *watch;

  v0 = GF_SN_WATCHER_V0 (object);

  if (*service == '/')
    {
      bus_name = g_dbus_method_invocation_get_sender (invocation);
      object_path = service;
    }
  else
    {
      bus_name = service;
      object_path = "/StatusNotifierHost";
    }

  if (g_dbus_is_name (bus_name) == FALSE)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "D-Bus bus name '%s' is not valid",
                                             bus_name);

      return TRUE;
    }

  watch = gf_watch_find (v0->hosts, bus_name, object_path);

  if (watch != NULL)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Status Notifier Host with bus name '%s' and object path '%s' is already registered",
                                             bus_name, object_path);

      return TRUE;
    }

  watch = gf_watch_new (v0, GF_WATCH_TYPE_HOST, service, bus_name, object_path);
  v0->hosts = g_slist_prepend (v0->hosts, watch);

  if (!gf_sn_watcher_v0_gen_get_is_host_registered (object))
    {
      gf_sn_watcher_v0_gen_set_is_host_registered (object, TRUE);
      gf_sn_watcher_v0_gen_emit_host_registered (object);
    }

  gf_sn_watcher_v0_gen_complete_register_host (object, invocation);

  return TRUE;
}

static gboolean
gf_sn_watcher_v0_handle_register_item (GfSnWatcherV0Gen      *object,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *service)
{
  GfSnWatcherV0 *v0;
  const gchar *bus_name;
  const gchar *object_path;
  GfWatch *watch;
  gchar *tmp;

  v0 = GF_SN_WATCHER_V0 (object);

  if (*service == '/')
    {
      bus_name = g_dbus_method_invocation_get_sender (invocation);
      object_path = service;
    }
  else
    {
      bus_name = service;
      object_path = "/StatusNotifierItem";
    }

  if (g_dbus_is_name (bus_name) == FALSE)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "D-Bus bus name '%s' is not valid",
                                             bus_name);

      return TRUE;
    }

  watch = gf_watch_find (v0->items, bus_name, object_path);

  if (watch != NULL)
    {
      /* the specification doesn't explicitly state what should happen when
       * trying to register the same item again, so it would make sense to
       * forbid it.  Unfortunately libappindicator tries re-registering pretty
       * often, and even falls back to System Tray if it fails.
       * So in practice we need to be forgiving and pretend it's OK. */
#if 0
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Status Notifier Item with bus name '%s' and object path '%s' is already registered",
                                             bus_name, object_path);
#else
      g_warning ("Status Notifier Item with bus name '%s' and object path '%s' is already registered",
                 bus_name, object_path);
      /* FIXME: is it OK to simply ignore the request instead of removing the
       *        old one and adding the new one?  I don't see the problem as
       *        they are identical, but...? */
      gf_sn_watcher_v0_gen_complete_register_item (object, invocation);
#endif

      return TRUE;
    }

  watch = gf_watch_new (v0, GF_WATCH_TYPE_ITEM, service, bus_name, object_path);
  v0->items = g_slist_prepend (v0->items, watch);

  update_registered_items (v0);

  tmp = g_strdup_printf ("%s%s", bus_name, object_path);
  gf_sn_watcher_v0_gen_emit_item_registered (object, tmp);
  g_free (tmp);

  gf_sn_watcher_v0_gen_complete_register_item (object, invocation);

  return TRUE;
}

static void
gf_sn_watcher_v0_gen_init (GfSnWatcherV0GenIface *iface)
{
  iface->handle_register_host = gf_sn_watcher_v0_handle_register_host;
  iface->handle_register_item = gf_sn_watcher_v0_handle_register_item;
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  GfSnWatcherV0 *v0;
  GDBusInterfaceSkeleton *skeleton;
  GError *error;

  v0 = GF_SN_WATCHER_V0 (user_data);
  skeleton = G_DBUS_INTERFACE_SKELETON (v0);

  error = NULL;
  g_dbus_interface_skeleton_export (skeleton, connection,
                                    "/StatusNotifierWatcher", &error);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }
}

static void
gf_sn_watcher_v0_dispose (GObject *object)
{
  GfSnWatcherV0 *v0;

  v0 = GF_SN_WATCHER_V0 (object);

  if (v0->bus_name_id > 0)
    {
      g_bus_unown_name (v0->bus_name_id);
      v0->bus_name_id = 0;
    }

  if (v0->hosts != NULL)
    {
      g_slist_free_full (v0->hosts, gf_watch_free);
      v0->hosts = NULL;
    }

  if (v0->items != NULL)
    {
      g_slist_free_full (v0->items, gf_watch_free);
      v0->items = NULL;
    }

  G_OBJECT_CLASS (gf_sn_watcher_v0_parent_class)->dispose (object);
}

static void
gf_sn_watcher_v0_class_init (GfSnWatcherV0Class *v0_class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (v0_class);

  object_class->dispose = gf_sn_watcher_v0_dispose;
}

static void
gf_sn_watcher_v0_init (GfSnWatcherV0 *v0)
{
  GBusNameOwnerFlags flags;

  flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
          G_BUS_NAME_OWNER_FLAGS_REPLACE;

  v0->bus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                    "org.kde.StatusNotifierWatcher", flags,
                                    bus_acquired_cb, NULL, NULL, v0, NULL);
}

GfSnWatcherV0 *
gf_sn_watcher_v0_new (void)
{
  return g_object_new (GF_TYPE_SN_WATCHER_V0, NULL);
}
