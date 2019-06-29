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

#include "sn-dbus-menu.h"
#include "sn-dbus-menu-gen.h"
#include "sn-dbus-menu-item.h"

struct _SnDBusMenu
{
  GtkMenu        parent;

  GHashTable    *items;

  GCancellable  *cancellable;

  gchar         *bus_name;
  gchar         *object_path;
  guint          name_id;

  SnDBusMenuGen *proxy;
};

enum
{
  PROP_0,

  PROP_BUS_NAME,
  PROP_OBJECT_PATH,

  LAST_PROP
};

static GParamSpec *properties[LAST_PROP] = { NULL };

static const gchar *property_names[] =
{
  "accessible-desc",
  "children-display",
  "disposition",
  "enabled",
  "icon-data",
  "icon-name",
  "label",
  "shortcut",
  "toggle-type",
  "toggle-state",
  "type",
  "visible",
  NULL
};

G_DEFINE_TYPE (SnDBusMenu, sn_dbus_menu, GTK_TYPE_MENU)

static void
activate_cb (GtkWidget  *widget,
             SnDBusMenu *menu)
{
  guint id;

  if (gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget)) != NULL)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "item-id"));
  sn_dbus_menu_gen_call_event_sync (menu->proxy, id, "clicked",
                                    g_variant_new ("v", g_variant_new_int32 (0)),
                                    gtk_get_current_event_time (), NULL, NULL);
}

static GtkMenu *
layout_update_item (SnDBusMenu *menu,
                    GtkMenu    *gtk_menu,
                    guint       id,
                    GVariant   *props)
{
  SnDBusMenuItem *item;

  if (id == 0)
    return gtk_menu;

  item = g_hash_table_lookup (menu->items, GUINT_TO_POINTER (id));

  if (item == NULL)
    {
      item = sn_dbus_menu_item_new (props);

      g_object_set_data (G_OBJECT (item->item), "item-id", GUINT_TO_POINTER (id));
      gtk_menu_shell_append (GTK_MENU_SHELL (gtk_menu), item->item);

      item->activate_id = g_signal_connect (item->item, "activate",
                                            G_CALLBACK (activate_cb), menu);

      g_hash_table_replace (menu->items, GUINT_TO_POINTER (id), item);
    }
  else
    {
      sn_dbus_menu_item_update_props (item, props);
    }
  return item->submenu;
}

static void
layout_parse (SnDBusMenu *menu,
              GVariant   *layout,
              GtkMenu    *gtk_menu)
{
  guint id;
  GVariant *props;
  GVariant *items;
  GtkMenu *submenu;
  GVariantIter iter;
  GVariant *child;

  if (!g_variant_is_of_type (layout, G_VARIANT_TYPE ("(ia{sv}av)")))
    {
      g_warning ("Type of return value for 'layout' property in "
                 "'GetLayout' call should be '(ia{sv}av)' but got '%s'",
                 g_variant_get_type_string (layout));

      return;
    }

  g_variant_get (layout, "(i@a{sv}@av)", &id, &props, &items);

  submenu = layout_update_item (menu, gtk_menu, id, props);
  g_variant_unref (props);

  g_variant_iter_init (&iter, items);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      GVariant *value;

      value = g_variant_get_variant (child);

      layout_parse (menu, value, submenu);
      g_variant_unref (value);

      g_variant_unref (child);
    }

  g_variant_unref (items);
}

static void
get_layout_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
  GVariant *layout;
  guint revision;
  GError *error;
  SnDBusMenu *menu;

  error = NULL;
  sn_dbus_menu_gen_call_get_layout_finish (SN_DBUS_MENU_GEN (source_object),
                                           &revision, &layout, res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  menu = SN_DBUS_MENU (user_data);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  g_hash_table_remove_all (menu->items);
  layout_parse (menu, layout, GTK_MENU (menu));

  /* Reposition menu to accomodate any size changes   */
  /* Menu size never changes with GTK 3.20 or earlier */
  gtk_menu_reposition(GTK_MENU(menu));

  g_variant_unref (layout);
}

static void
update_layout (SnDBusMenu *menu,
               gint        parent)
{
  gint depth;

  parent = 0;
  depth = -1;

  sn_dbus_menu_gen_call_get_layout (menu->proxy, parent, depth,
                                    property_names, menu->cancellable,
                                    get_layout_cb, menu);
}

static void
items_properties_updated_cb (SnDBusMenuGen *proxy,
                             GVariant      *updated_props,
                             GVariant      *removed_props,
                             SnDBusMenu    *menu)
{
  GVariantIter iter;
  guint id;
  GVariant *props;
  SnDBusMenuItem *item;

  g_variant_iter_init (&iter, updated_props);
  while (g_variant_iter_next (&iter, "(i@a{sv})", &id, &props))
    {
      item = g_hash_table_lookup (menu->items, GUINT_TO_POINTER (id));

      if (item != NULL)
        sn_dbus_menu_item_update_props (item, props);

      g_variant_unref (props);
    }

  g_variant_iter_init (&iter, removed_props);
  while (g_variant_iter_next (&iter, "(i@as)", &id, &props))
    {
      item = g_hash_table_lookup (menu->items, GUINT_TO_POINTER (id));

      if (item != NULL)
        sn_dbus_menu_item_remove_props (item, props);

      g_variant_unref (props);
    }
}

static void
layout_updated_cb (SnDBusMenuGen *proxy,
                   guint          revision,
                   gint           parent,
                   SnDBusMenu    *menu)
{
  update_layout (menu, parent);
}

static void
item_activation_requested_cb (SnDBusMenuGen *proxy,
                              gint           id,
                              guint          timestamp,
                              SnDBusMenu    *menu)
{
  g_debug ("activation requested: id - %d, timestamp - %d", id, timestamp);
}

static void
map_cb (GtkWidget  *widget,
        SnDBusMenu *menu)
{
  gboolean need_update;

  sn_dbus_menu_gen_call_event_sync (menu->proxy, 0, "opened",
                                    g_variant_new ("v", g_variant_new_int32 (0)),
                                    gtk_get_current_event_time (),
                                    NULL, NULL);

  sn_dbus_menu_gen_call_about_to_show_sync (menu->proxy, 0, &need_update,
                                            NULL, NULL);

  if (need_update)
    {
      update_layout (menu, 0);
    }
}

static void
unmap_cb (GtkWidget  *widget,
          SnDBusMenu *menu)
{
  sn_dbus_menu_gen_call_event_sync (menu->proxy, 0, "closed",
                                    g_variant_new ("v", g_variant_new_int32 (0)),
                                    gtk_get_current_event_time (),
                                    NULL, NULL);
}

static void
proxy_ready_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  SnDBusMenuGen *proxy;
  GError *error;
  SnDBusMenu *menu;

  error = NULL;
  proxy = sn_dbus_menu_gen_proxy_new_finish (res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  menu = SN_DBUS_MENU (user_data);
  menu->proxy = proxy;

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (proxy, "items-properties-updated",
                    G_CALLBACK (items_properties_updated_cb), menu);

  g_signal_connect (proxy, "layout-updated",
                    G_CALLBACK (layout_updated_cb), menu);

  g_signal_connect (proxy, "item-activation-requested",
                    G_CALLBACK (item_activation_requested_cb), menu);

  g_signal_connect (menu, "map", G_CALLBACK (map_cb), menu);
  g_signal_connect (menu, "unmap", G_CALLBACK (unmap_cb), menu);

  update_layout (menu, 0);
}

static void
name_appeared_cb (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
  SnDBusMenu *menu;

  menu = SN_DBUS_MENU (user_data);

  sn_dbus_menu_gen_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE,
                              menu->bus_name, menu->object_path,
                              menu->cancellable, proxy_ready_cb, menu);
}

static void
name_vanished_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  SnDBusMenu *menu;

  menu = SN_DBUS_MENU (user_data);

  g_clear_object (&menu->proxy);
}

static void
sn_dbus_menu_constructed (GObject *object)
{
  SnDBusMenu *menu;
  GtkWidget *toplevel;
  GdkScreen *screen;
  GdkVisual *visual;
  GtkStyleContext *context;

  G_OBJECT_CLASS (sn_dbus_menu_parent_class)->constructed (object);
  menu = SN_DBUS_MENU (object);

  /*Set up theme and transparency support*/
  toplevel = gtk_widget_get_toplevel(GTK_WIDGET(menu));
  /* Fix any failures of compiz/other wm's to communicate with gtk for transparency */
  screen = gtk_widget_get_screen(GTK_WIDGET(toplevel));
  visual = gdk_screen_get_rgba_visual(screen);
  gtk_widget_set_visual(GTK_WIDGET(toplevel), visual);
  /* Set menu and it's toplevel window to follow panel theme */
  context = gtk_widget_get_style_context (GTK_WIDGET(toplevel));
  gtk_style_context_add_class(context,"gnome-panel-menu-bar");
  gtk_style_context_add_class(context,"mate-panel-menu-bar");

  menu->name_id = g_bus_watch_name (G_BUS_TYPE_SESSION, menu->bus_name,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    name_appeared_cb, name_vanished_cb,
                                    menu, NULL);
}

static void
sn_dbus_menu_dispose (GObject *object)
{
  SnDBusMenu *menu;

  menu = SN_DBUS_MENU (object);

  if (menu->name_id > 0)
    {
      g_bus_unwatch_name (menu->name_id);
      menu->name_id = 0;
    }

  g_clear_pointer (&menu->items, g_hash_table_destroy);

  g_cancellable_cancel (menu->cancellable);
  g_clear_object (&menu->cancellable);
  g_clear_object (&menu->proxy);

  G_OBJECT_CLASS (sn_dbus_menu_parent_class)->dispose (object);
}

static void
sn_dbus_menu_finalize (GObject *object)
{
  SnDBusMenu *menu;

  menu = SN_DBUS_MENU (object);

  g_free (menu->bus_name);
  g_free (menu->object_path);

  G_OBJECT_CLASS (sn_dbus_menu_parent_class)->finalize (object);
}

static void
sn_dbus_menu_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  SnDBusMenu *menu;

  menu = SN_DBUS_MENU (object);

  switch (property_id)
    {
      case PROP_BUS_NAME:
        menu->bus_name = g_value_dup_string (value);
        break;

      case PROP_OBJECT_PATH:
        menu->object_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
install_properties (GObjectClass *object_class)
{
  properties[PROP_BUS_NAME] =
    g_param_spec_string ("bus-name", "bus-name", "bus-name", NULL,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path", "object-path", "object-path", NULL,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
sn_dbus_menu_class_init (SnDBusMenuClass *menu_class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (menu_class);

  object_class->constructed = sn_dbus_menu_constructed;
  object_class->dispose = sn_dbus_menu_dispose;
  object_class->finalize = sn_dbus_menu_finalize;
  object_class->set_property = sn_dbus_menu_set_property;

  install_properties (object_class);
}

static void
sn_dbus_menu_init (SnDBusMenu *menu)
{
  menu->items = g_hash_table_new_full (NULL, NULL, NULL, sn_dubs_menu_item_free);
  menu->cancellable = g_cancellable_new ();
}

GtkMenu *
sn_dbus_menu_new (const gchar *bus_name,
                  const gchar *object_path)
{
  return g_object_new (SN_TYPE_DBUS_MENU,
                       "bus-name", bus_name,
                       "object-path", object_path,
                       NULL);
}
