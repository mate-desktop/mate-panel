/*
 * Copyright (C) 2016 Alberts Muktupāvels
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#include "sn-host-v0.h"
#include "sn-item-v0.h"
#include "sn-watcher-v0-gen.h"

#define SN_HOST_BUS_NAME "org.kde.StatusNotifierHost"
#define SN_HOST_OBJECT_PATH "/StatusNotifierHost"
#define SN_ITEM_OBJECT_PATH "/StatusNotifierItem"
#define SN_WATCHER_BUS_NAME "org.kde.StatusNotifierWatcher"
#define SN_WATCHER_OBJECT_PATH "/StatusNotifierWatcher"

struct _SnHostV0
{
  SnHostV0GenSkeleton  parent;

  gchar               *bus_name;
  gchar               *object_path;
  guint                bus_name_id;

  GCancellable        *cancellable;

  guint                watcher_id;
  SnWatcherV0Gen      *watcher;

  GSList              *items;

  gint                 icon_padding;
  gint                 icon_size;
};

enum
{
  PROP_0,

  PROP_ICON_PADDING,
  PROP_ICON_SIZE,

  LAST_PROP
};

static void sn_host_v0_gen_init (SnHostV0GenIface *iface);
static void na_host_init        (NaHostInterface  *iface);

G_DEFINE_TYPE_WITH_CODE (SnHostV0, sn_host_v0, SN_TYPE_HOST_V0_GEN_SKELETON,
                         G_IMPLEMENT_INTERFACE (SN_TYPE_HOST_V0_GEN, sn_host_v0_gen_init)
                         G_IMPLEMENT_INTERFACE (NA_TYPE_HOST, na_host_init))

static void
sn_host_v0_gen_init (SnHostV0GenIface *iface)
{
}

static void
na_host_init (NaHostInterface *iface)
{
}

static void
get_bus_name_and_object_path (const gchar  *service,
                              gchar       **bus_name,
                              gchar       **object_path)
{
  gchar *tmp;

  g_assert (*bus_name == NULL);
  g_assert (*object_path == NULL);

  tmp = g_strstr_len (service, -1, "/");
  if (tmp != NULL)
    {
      gchar **strings;

      strings = g_strsplit (service, "/", 2);

      *bus_name = g_strdup (strings[0]);
      *object_path = g_strdup (tmp);

      g_strfreev (strings);
    }
  else
    {
      *bus_name = g_strdup (service);
      *object_path = g_strdup (SN_ITEM_OBJECT_PATH);
    }
}

static void
ready_cb (SnItem   *item,
          SnHostV0 *v0)
{
  na_host_emit_item_added (NA_HOST (v0), NA_ITEM (item));
}

static void
add_registered_item (SnHostV0    *v0,
                     const gchar *service)
{
  gchar *bus_name;
  gchar *object_path;
  SnItem *item;

  bus_name = NULL;
  object_path = NULL;

  get_bus_name_and_object_path (service, &bus_name, &object_path);

  item = sn_item_v0_new (bus_name, object_path);
  g_object_ref_sink (item);

  g_object_bind_property (v0, "icon-padding", item, "icon-padding",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
  g_object_bind_property (v0, "icon-size", item, "icon-size",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  v0->items = g_slist_prepend (v0->items, item);
  g_signal_connect (item, "ready", G_CALLBACK (ready_cb), v0);

  g_free (bus_name);
  g_free (object_path);
}

static void
item_registered_cb (SnWatcherV0Gen *watcher,
                    const gchar    *service,
                    SnHostV0       *v0)
{
  add_registered_item (v0, service);
}

static void
item_unregistered_cb (SnWatcherV0Gen *watcher,
                      const gchar    *service,
                      SnHostV0       *v0)
{
  GSList *l;

  for (l = v0->items; l != NULL; l = g_slist_next (l))
    {
      SnItem *item;
      gboolean found;
      gchar *bus_name;
      gchar *object_path;

      item = SN_ITEM (l->data);

      found = FALSE;
      bus_name = NULL;
      object_path = NULL;

      get_bus_name_and_object_path (service, &bus_name, &object_path);

      if (g_strcmp0 (sn_item_get_bus_name (item), bus_name) == 0 &&
          g_strcmp0 (sn_item_get_object_path (item), object_path) == 0)
        {
          v0->items = g_slist_remove (v0->items, item);
          na_host_emit_item_removed (NA_HOST (v0), NA_ITEM (item));
          g_object_unref (item);

          found = TRUE;
        }

      g_free (bus_name);
      g_free (object_path);

      if (found)
        break;
    }
}

static void
register_host_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  GError *error;
  SnHostV0 *v0;
  gchar **items;
  gint i;

  error = NULL;
  sn_watcher_v0_gen_call_register_host_finish (SN_WATCHER_V0_GEN (source_object),
                                               res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  v0 = SN_HOST_V0 (user_data);

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);

      return;
    }

  g_signal_connect (v0->watcher, "item-registered",
                    G_CALLBACK (item_registered_cb), v0);

  g_signal_connect (v0->watcher, "item-unregistered",
                    G_CALLBACK (item_unregistered_cb), v0);

  items = sn_watcher_v0_gen_dup_registered_items (v0->watcher);

  if (items) {
    for (i = 0; items[i] != NULL; i++)
      add_registered_item (v0, items[i]);
  }

  g_strfreev (items);
}

static void
proxy_ready_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  GError *error;
  SnWatcherV0Gen *watcher;
  SnHostV0 *v0;

  error = NULL;
  watcher = sn_watcher_v0_gen_proxy_new_finish (res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  v0 = SN_HOST_V0 (user_data);
  v0->watcher = watcher;

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);

      return;
    }

  sn_watcher_v0_gen_call_register_host (v0->watcher, v0->object_path,
                                        v0->cancellable, register_host_cb, v0);
}

static void
name_appeared_cb (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (user_data);

  g_assert (v0->cancellable == NULL);
  v0->cancellable = g_cancellable_new ();

  sn_watcher_v0_gen_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE,
                               SN_WATCHER_BUS_NAME, SN_WATCHER_OBJECT_PATH,
                               v0->cancellable, proxy_ready_cb, user_data);
}

static void
emit_item_removed_signal (gpointer data,
                          gpointer user_data)
{
  na_host_emit_item_removed (NA_HOST (user_data), NA_ITEM (data));
}

static void
name_vanished_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (user_data);

  g_cancellable_cancel (v0->cancellable);
  g_clear_object (&v0->cancellable);

  g_clear_object (&v0->watcher);

  if (v0->items)
    {
      g_slist_foreach (v0->items, emit_item_removed_signal, v0);
      g_slist_free_full (v0->items, g_object_unref);
      v0->items = NULL;
    }
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  SnHostV0 *v0;
  GDBusInterfaceSkeleton *skeleton;
  GError *error;

  v0 = SN_HOST_V0 (user_data);
  skeleton = G_DBUS_INTERFACE_SKELETON (v0);

  error = NULL;
  g_dbus_interface_skeleton_export (skeleton, connection,
                                    v0->object_path, &error);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);

      return;
    }

  v0->watcher_id = g_bus_watch_name (G_BUS_TYPE_SESSION, SN_WATCHER_BUS_NAME,
                                     G_BUS_NAME_WATCHER_FLAGS_NONE,
                                     name_appeared_cb, name_vanished_cb,
                                     v0, NULL);
}

static void
sn_host_v0_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (object);

  switch (property_id)
    {
      case PROP_ICON_PADDING:
        g_value_set_int (value, v0->icon_padding);
        break;

      case PROP_ICON_SIZE:
        g_value_set_int (value, v0->icon_size);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
sn_host_v0_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (object);

  switch (property_id)
    {
      case PROP_ICON_PADDING:
        v0->icon_padding = g_value_get_int (value);
        break;

      case PROP_ICON_SIZE:
        v0->icon_size = g_value_get_int (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
install_properties (GObjectClass *object_class)
{
  /* NaHost properties */
  g_object_class_override_property (object_class, PROP_ICON_PADDING, "icon-padding");
  g_object_class_override_property (object_class, PROP_ICON_SIZE, "icon-size");
}

static void
sn_host_v0_dispose (GObject *object)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (object);

  if (v0->bus_name_id > 0)
    {
      g_bus_unown_name (v0->bus_name_id);
      v0->bus_name_id = 0;
    }

  if (v0->watcher_id > 0)
    {
      g_bus_unwatch_name (v0->watcher_id);
      v0->watcher_id = 0;
    }

  g_cancellable_cancel (v0->cancellable);
  g_clear_object (&v0->cancellable);

  g_clear_object (&v0->watcher);

  if (v0->items)
    {
      g_slist_foreach (v0->items, emit_item_removed_signal, v0);
      g_slist_free_full (v0->items, g_object_unref);
      v0->items = NULL;
    }

  G_OBJECT_CLASS (sn_host_v0_parent_class)->dispose (object);
}

static void
sn_host_v0_finalize (GObject *object)
{
  SnHostV0 *v0;

  v0 = SN_HOST_V0 (object);

  g_clear_pointer (&v0->bus_name, g_free);
  g_clear_pointer (&v0->object_path, g_free);

  G_OBJECT_CLASS (sn_host_v0_parent_class)->finalize (object);
}

static void
sn_host_v0_class_init (SnHostV0Class *v0_class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (v0_class);

  object_class->dispose = sn_host_v0_dispose;
  object_class->finalize = sn_host_v0_finalize;
  object_class->get_property = sn_host_v0_get_property;
  object_class->set_property = sn_host_v0_set_property;

  install_properties (object_class);
}

static void
sn_host_v0_init (SnHostV0 *v0)
{
  GBusNameOwnerFlags flags;
  static guint id;

  flags = G_BUS_NAME_OWNER_FLAGS_NONE;
  id++;

  v0->bus_name = g_strdup_printf ("%s-%d-%d", SN_HOST_BUS_NAME, getpid (), id);
  v0->object_path = g_strdup_printf ("%s/%d", SN_HOST_OBJECT_PATH,id);

  v0->bus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION, v0->bus_name, flags,
                                    bus_acquired_cb, NULL, NULL, v0, NULL);

  v0->icon_size = 16;
  v0->icon_padding = 0;
}

NaHost *
sn_host_v0_new (void)
{
  return g_object_new (SN_TYPE_HOST_V0, NULL);
}
