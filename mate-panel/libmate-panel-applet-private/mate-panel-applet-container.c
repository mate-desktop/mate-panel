/*
 * mate-panel-applet-container.c: a container for applets.
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <string.h>
#include "mate-panel-applet-container.h"
#include "panel-marshal.h"

struct _MatePanelAppletContainerPrivate {
	GDBusProxy *applet_proxy;

	guint       name_watcher_id;
	gchar      *bus_name;

	guint32     xid;
	GtkWidget  *socket;

	GHashTable *pending_ops;
};

enum {
	APPLET_BROKEN,
	APPLET_MOVE,
	APPLET_REMOVE,
	APPLET_LOCK,
	CHILD_PROPERTY_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct {
	const gchar *name;
	const gchar *dbus_name;
} AppletPropertyInfo;

static const AppletPropertyInfo applet_properties [] = {
	{ "prefs-key",   "PrefsKey" },
	{ "orient",      "Orient" },
	{ "size",        "Size" },
	{ "size-hints",  "SizeHints" },
	{ "background",  "Background" },
	{ "flags",       "Flags" },
	{ "locked",      "Locked" },
	{ "locked-down", "LockedDown" }
};

#define MATE_PANEL_APPLET_CONTAINER_GET_PRIVATE(o) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), PANEL_TYPE_APPLET_CONTAINER, MatePanelAppletContainerPrivate))

#define MATE_PANEL_APPLET_BUS_NAME            "org.mate.panel.applet.%s"
#define MATE_PANEL_APPLET_FACTORY_INTERFACE   "org.mate.panel.applet.AppletFactory"
#define MATE_PANEL_APPLET_FACTORY_OBJECT_PATH "/org/mate/panel/applet/%s"
#define MATE_PANEL_APPLET_INTERFACE           "org.mate.panel.applet.Applet"

static gboolean mate_panel_applet_container_plug_removed (MatePanelAppletContainer *container);

G_DEFINE_TYPE (MatePanelAppletContainer, mate_panel_applet_container, GTK_TYPE_EVENT_BOX);

GQuark mate_panel_applet_container_error_quark (void)
{
	return g_quark_from_static_string ("mate-panel-applet-container-error-quark");
}

static void mate_panel_applet_container_init(MatePanelAppletContainer* container)
{
	container->priv = MATE_PANEL_APPLET_CONTAINER_GET_PRIVATE (container);

	container->priv->socket = gtk_socket_new ();
	g_signal_connect_swapped (container->priv->socket, "plug-removed", G_CALLBACK (mate_panel_applet_container_plug_removed), container);

	container->priv->pending_ops = g_hash_table_new_full (g_direct_hash,
							      g_direct_equal,
							      NULL,
							      (GDestroyNotify) g_object_unref);

	gtk_container_add (GTK_CONTAINER (container),
			   container->priv->socket);
	gtk_widget_show (container->priv->socket);
}

static void
mate_panel_applet_container_cancel_pending_operations (MatePanelAppletContainer *container)
{
	GList *keys, *l;

	if (!container->priv->pending_ops)
		return;

	keys = g_hash_table_get_keys (container->priv->pending_ops);
	for (l = keys; l; l = g_list_next (l)) {
		GCancellable *cancellable;

		cancellable = G_CANCELLABLE (g_hash_table_lookup (container->priv->pending_ops, l->data));
		g_cancellable_cancel (cancellable);
	}
	g_list_free (keys);
}

static void
mate_panel_applet_container_dispose (GObject *object)
{
	MatePanelAppletContainer *container = MATE_PANEL_APPLET_CONTAINER (object);

	if (container->priv->pending_ops) {
		mate_panel_applet_container_cancel_pending_operations (container);
		g_hash_table_destroy (container->priv->pending_ops);
		container->priv->pending_ops = NULL;
	}

	if (container->priv->bus_name) {
		g_free (container->priv->bus_name);
		container->priv->bus_name = NULL;
	}

	if (container->priv->name_watcher_id > 0) {
		g_bus_unwatch_name (container->priv->name_watcher_id);
		container->priv->name_watcher_id = 0;
	}

	if (container->priv->applet_proxy) {
		g_object_unref (container->priv->applet_proxy);
		container->priv->applet_proxy = NULL;
	}

	G_OBJECT_CLASS (mate_panel_applet_container_parent_class)->dispose (object);
}

static void
mate_panel_applet_container_class_init (MatePanelAppletContainerClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (MatePanelAppletContainerPrivate));

	gobject_class->dispose = mate_panel_applet_container_dispose;

	signals[APPLET_BROKEN] =
		g_signal_new ("applet-broken",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MatePanelAppletContainerClass, applet_broken),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[APPLET_MOVE] =
		g_signal_new ("applet-move",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MatePanelAppletContainerClass, applet_move),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[APPLET_REMOVE] =
		g_signal_new ("applet-remove",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MatePanelAppletContainerClass, applet_remove),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[APPLET_LOCK] =
		g_signal_new ("applet-lock",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MatePanelAppletContainerClass, applet_lock),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1, G_TYPE_BOOLEAN);
	signals[CHILD_PROPERTY_CHANGED] =
		g_signal_new ("child-property-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE |
		              G_SIGNAL_DETAILED | G_SIGNAL_NO_HOOKS,
			      G_STRUCT_OFFSET (MatePanelAppletContainerClass, child_property_changed),
			      NULL,
			      NULL,
			      panel_marshal_VOID__STRING_POINTER,
			      G_TYPE_NONE, 2,
			      G_TYPE_STRING,
			      G_TYPE_POINTER);
}

static const AppletPropertyInfo *
mate_panel_applet_container_child_property_get_info (const gchar *property_name)
{
	gint i;

	g_assert (property_name != NULL);

	for (i = 0; i < G_N_ELEMENTS (applet_properties); i++) {
		if (g_ascii_strcasecmp (applet_properties[i].name, property_name) == 0)
			return &applet_properties[i];
	}

	return NULL;
}

GtkWidget *
mate_panel_applet_container_new (void)
{
	GtkWidget *container;

	container = GTK_WIDGET (g_object_new (PANEL_TYPE_APPLET_CONTAINER, NULL));

	return container;
}

static gboolean
mate_panel_applet_container_plug_removed (MatePanelAppletContainer *container)
{
	if (!container->priv->applet_proxy)
		return FALSE;

	mate_panel_applet_container_cancel_pending_operations (container);

	if (container->priv->name_watcher_id > 0) {
		g_bus_unwatch_name (container->priv->name_watcher_id);
		container->priv->name_watcher_id = 0;
	}

	g_object_unref (container->priv->applet_proxy);
	container->priv->applet_proxy = NULL;

	g_signal_emit (container, signals[APPLET_BROKEN], 0);

	/* Continue destroying, in case of reloading
	 * a new frame widget is created
	 */
	return FALSE;
}

static void
mate_panel_applet_container_child_signal (GDBusProxy           *proxy,
				     gchar                *sender_name,
				     gchar                *signal_name,
				     GVariant             *parameters,
				     MatePanelAppletContainer *container)
{
	if (g_strcmp0 (signal_name, "Move") == 0) {
		g_signal_emit (container, signals[APPLET_MOVE], 0);
	} else if (g_strcmp0 (signal_name, "RemoveFromPanel") == 0) {
		g_signal_emit (container, signals[APPLET_REMOVE], 0);
	} else if (g_strcmp0 (signal_name, "Lock") == 0) {
		g_signal_emit (container, signals[APPLET_LOCK], 0, TRUE);
	} else if (g_strcmp0 (signal_name, "Unlock") == 0) {
		g_signal_emit (container, signals[APPLET_LOCK], 0, FALSE);
	}
}

static void
on_property_changed (GDBusConnection      *connection,
		     const gchar          *sender_name,
		     const gchar          *object_path,
		     const gchar          *interface_name,
		     const gchar          *signal_name,
		     GVariant             *parameters,
		     MatePanelAppletContainer *container)
{
	GVariant    *props;
	GVariantIter iter;
	GVariant    *value;
	gchar       *key;

	g_variant_get (parameters, "(s@a{sv}*)", NULL, &props, NULL);

	g_variant_iter_init (&iter, props);
	while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Flags") == 0) {
			g_signal_emit (container, signals[CHILD_PROPERTY_CHANGED],
				       g_quark_from_string ("flags"),
				       "flags", value);
		} else if (g_strcmp0 (key, "SizeHints") == 0) {
			g_signal_emit (container, signals[CHILD_PROPERTY_CHANGED],
				       g_quark_from_string ("size-hints"),
				       "size-hints", value);
		}
	}

	g_variant_unref (props);
}

static void
on_proxy_appeared (GObject      *source_object,
		   GAsyncResult *res,
		   gpointer      user_data)
{
	GSimpleAsyncResult   *result = G_SIMPLE_ASYNC_RESULT (user_data);
	MatePanelAppletContainer *container;
	GDBusProxy           *proxy;
	GError               *error = NULL;

	proxy = g_dbus_proxy_new_finish (res, &error);
	if (!proxy) {
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
		g_simple_async_result_complete (result);
		g_object_unref (result);

		return;
	}

	container = MATE_PANEL_APPLET_CONTAINER (g_async_result_get_source_object (G_ASYNC_RESULT (result)));

	container->priv->applet_proxy = proxy;
	g_signal_connect (container->priv->applet_proxy, "g-signal",
			  G_CALLBACK (mate_panel_applet_container_child_signal),
			  container);
	g_dbus_connection_signal_subscribe (g_dbus_proxy_get_connection (proxy),
					    g_dbus_proxy_get_name (proxy),
					    "org.freedesktop.DBus.Properties",
					    "PropertiesChanged",
					    g_dbus_proxy_get_object_path (proxy),
					    MATE_PANEL_APPLET_INTERFACE,
					    G_DBUS_SIGNAL_FLAGS_NONE,
					    (GDBusSignalCallback) on_property_changed,
					    container, NULL);

	g_simple_async_result_complete (result);
	g_object_unref (result);

	if (container->priv->xid > 0) {
		gtk_socket_add_id (GTK_SOCKET (container->priv->socket),
				   container->priv->xid);
	}

	/* g_async_result_get_source_object returns new ref */
	g_object_unref (container);
}

static void
get_applet_cb (GObject      *source_object,
	       GAsyncResult *res,
	       gpointer      user_data)
{
	GDBusConnection      *connection = G_DBUS_CONNECTION (source_object);
	GSimpleAsyncResult   *result = G_SIMPLE_ASYNC_RESULT (user_data);
	MatePanelAppletContainer *container;
	GVariant             *retvals;
	const gchar          *applet_path;
	GError               *error = NULL;

	retvals = g_dbus_connection_call_finish (connection, res, &error);
	if (!retvals) {
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
		g_simple_async_result_complete (result);
		g_object_unref (result);

		return;
	}

	container = MATE_PANEL_APPLET_CONTAINER (g_async_result_get_source_object (G_ASYNC_RESULT (result)));
	g_variant_get (retvals, "(&ou)", &applet_path, &container->priv->xid);

	g_dbus_proxy_new (connection,
			  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			  NULL,
			  container->priv->bus_name,
			  applet_path,
			  MATE_PANEL_APPLET_INTERFACE,
			  NULL,
			  (GAsyncReadyCallback) on_proxy_appeared,
			  result);

	g_variant_unref (retvals);

	/* g_async_result_get_source_object returns new ref */
	g_object_unref (container);
}

typedef struct {
	GSimpleAsyncResult *result;
	gchar              *factory_id;
	GVariant           *parameters;
	GCancellable       *cancellable;
} AppletFactoryData;

static void
applet_factory_data_free (AppletFactoryData *data)
{
	g_free (data->factory_id);
	if (data->cancellable)
		g_object_unref (data->cancellable);

	g_free (data);
}

static void
on_factory_appeared (GDBusConnection   *connection,
		     const gchar       *name,
		     const gchar       *name_owner,
		     AppletFactoryData *data)
{
	MatePanelAppletContainer *container;
	gchar                *object_path;

	container = MATE_PANEL_APPLET_CONTAINER (g_async_result_get_source_object (G_ASYNC_RESULT (data->result)));
	container->priv->bus_name = g_strdup (name_owner);
	object_path = g_strdup_printf (MATE_PANEL_APPLET_FACTORY_OBJECT_PATH, data->factory_id);
	g_dbus_connection_call (connection,
				name_owner,
				object_path,
				MATE_PANEL_APPLET_FACTORY_INTERFACE,
				"GetApplet",
				data->parameters,
				G_VARIANT_TYPE ("(ou)"),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				data->cancellable,
				get_applet_cb,
				data->result);
	g_free (object_path);
}

static void
mate_panel_applet_container_get_applet (MatePanelAppletContainer *container,
				   GdkScreen            *screen,
				   const gchar          *iid,
				   GVariant             *props,
				   GCancellable         *cancellable,
				   GAsyncReadyCallback   callback,
				   gpointer              user_data)
{
	GSimpleAsyncResult *result;
	AppletFactoryData  *data;
	gint                screen_number;
	gchar              *bus_name;
	gchar              *factory_id;
	gchar              *applet_id;

	result = g_simple_async_result_new (G_OBJECT (container),
					    callback,
					    user_data,
					    mate_panel_applet_container_get_applet);

	applet_id = g_strrstr (iid, "::");
	if (!applet_id) {
		g_simple_async_result_set_error (result,
						 MATE_PANEL_APPLET_CONTAINER_ERROR,
						 MATE_PANEL_APPLET_CONTAINER_INVALID_APPLET,
						 "Invalid applet iid: %s", iid);
		g_simple_async_result_complete (result);
		g_object_unref (result);

		return;
	}

	factory_id = g_strndup (iid, strlen (iid) - strlen (applet_id));
	applet_id += 2;

	/* we can't use the screen of the container widget since it's not in a
	 * widget hierarchy yet */
	screen_number = gdk_screen_get_number (screen);

	data = g_new (AppletFactoryData, 1);
	data->result = result;
	data->factory_id = factory_id;
	data->parameters = g_variant_new ("(si*)", applet_id, screen_number, props);
	data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

	bus_name = g_strdup_printf (MATE_PANEL_APPLET_BUS_NAME, factory_id);

	container->priv->name_watcher_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION,
				  bus_name,
				  G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
				  (GBusNameAppearedCallback) on_factory_appeared,
				  NULL,
				  data,
				  (GDestroyNotify) applet_factory_data_free);

	g_free (bus_name);
}

void
mate_panel_applet_container_add (MatePanelAppletContainer *container,
			    GdkScreen            *screen,
			    const gchar          *iid,
			    GCancellable         *cancellable,
			    GAsyncReadyCallback   callback,
			    gpointer              user_data,
			    GVariant             *properties)
{
	g_return_if_fail (PANEL_IS_APPLET_CONTAINER (container));
	g_return_if_fail (iid != NULL);

	mate_panel_applet_container_cancel_pending_operations (container);

	mate_panel_applet_container_get_applet (container, screen, iid, properties,
					   cancellable, callback, user_data);
}

gboolean
mate_panel_applet_container_add_finish (MatePanelAppletContainer *container,
				   GAsyncResult         *result,
				   GError              **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == mate_panel_applet_container_get_applet);

	return !g_simple_async_result_propagate_error (simple, error);
}

/* Child Properties */
static void
set_applet_property_cb (GObject      *source_object,
			GAsyncResult *res,
			gpointer      user_data)
{
	GDBusConnection      *connection = G_DBUS_CONNECTION (source_object);
	GSimpleAsyncResult   *result = G_SIMPLE_ASYNC_RESULT (user_data);
	MatePanelAppletContainer *container;
	GVariant             *retvals;
	GError               *error = NULL;

	retvals = g_dbus_connection_call_finish (connection, res, &error);
	if (!retvals) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Error setting property: %s\n", error->message);
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
	} else {
		g_variant_unref (retvals);
	}

	container = MATE_PANEL_APPLET_CONTAINER (g_async_result_get_source_object (G_ASYNC_RESULT (result)));
	g_hash_table_remove (container->priv->pending_ops, result);
	g_simple_async_result_complete (result);
	g_object_unref (result);

	/* g_async_result_get_source_object returns new ref */
	g_object_unref (container);
}

void
mate_panel_applet_container_child_set (MatePanelAppletContainer *container,
				  const gchar          *property_name,
				  const GVariant       *value,
				  GCancellable         *cancellable,
				  GAsyncReadyCallback   callback,
				  gpointer              user_data)
{
	GDBusProxy               *proxy = container->priv->applet_proxy;
	const AppletPropertyInfo *info;
	GSimpleAsyncResult       *result;

	if (!proxy)
		return;

	info = mate_panel_applet_container_child_property_get_info (property_name);
	if (!info) {
		g_simple_async_report_error_in_idle (G_OBJECT (container),
						     callback, user_data,
						     MATE_PANEL_APPLET_CONTAINER_ERROR,
						     MATE_PANEL_APPLET_CONTAINER_INVALID_CHILD_PROPERTY,
						     "%s: Applet has no child property named `%s'",
						     G_STRLOC, property_name);
		return;
	}

	result = g_simple_async_result_new (G_OBJECT (container),
					    callback,
					    user_data,
					    mate_panel_applet_container_child_set);

	if (cancellable)
		g_object_ref (cancellable);
	else
		cancellable = g_cancellable_new ();
	g_hash_table_insert (container->priv->pending_ops, result, cancellable);

	g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
				g_dbus_proxy_get_name (proxy),
				g_dbus_proxy_get_object_path (proxy),
				"org.freedesktop.DBus.Properties",
				"Set",
				g_variant_new ("(ssv)",
					       g_dbus_proxy_get_interface_name (proxy),
					       info->dbus_name,
					       value),
				NULL,
				G_DBUS_CALL_FLAGS_NO_AUTO_START,
				-1, cancellable,
				set_applet_property_cb,
				result);
}

gboolean
mate_panel_applet_container_child_set_finish (MatePanelAppletContainer *container,
					 GAsyncResult         *result,
					 GError              **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == mate_panel_applet_container_child_set);

	return !g_simple_async_result_propagate_error (simple, error);
}

static void
get_applet_property_cb (GObject      *source_object,
			GAsyncResult *res,
			gpointer      user_data)
{
	GDBusConnection      *connection = G_DBUS_CONNECTION (source_object);
	GSimpleAsyncResult   *result = G_SIMPLE_ASYNC_RESULT (user_data);
	MatePanelAppletContainer *container;
	GVariant             *retvals;
	GError               *error = NULL;

	retvals = g_dbus_connection_call_finish (connection, res, &error);
	if (!retvals) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Error getting property: %s\n", error->message);
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
	} else {
		GVariant *value, *item;

		item = g_variant_get_child_value (retvals, 0);
		value = g_variant_get_variant (item);
		g_variant_unref (item);
		g_simple_async_result_set_op_res_gpointer (result, value,
							   (GDestroyNotify) g_variant_unref);
		g_variant_unref (retvals);
	}

	container = MATE_PANEL_APPLET_CONTAINER (g_async_result_get_source_object (G_ASYNC_RESULT (result)));
	g_hash_table_remove (container->priv->pending_ops, result);
	g_simple_async_result_complete (result);
	g_object_unref (result);

	/* g_async_result_get_source_object returns new ref */
	g_object_unref (container);
}

void
mate_panel_applet_container_child_get (MatePanelAppletContainer *container,
				  const gchar          *property_name,
				  GCancellable         *cancellable,
				  GAsyncReadyCallback   callback,
				  gpointer              user_data)
{
	GDBusProxy               *proxy = container->priv->applet_proxy;
	const AppletPropertyInfo *info;
	GSimpleAsyncResult       *result;

	if (!proxy)
		return;

	info = mate_panel_applet_container_child_property_get_info (property_name);
	if (!info) {
		g_simple_async_report_error_in_idle (G_OBJECT (container),
						     callback, user_data,
						     MATE_PANEL_APPLET_CONTAINER_ERROR,
						     MATE_PANEL_APPLET_CONTAINER_INVALID_CHILD_PROPERTY,
						     "%s: Applet has no child property named `%s'",
						     G_STRLOC, property_name);
		return;
	}

	result = g_simple_async_result_new (G_OBJECT (container),
					    callback,
					    user_data,
					    mate_panel_applet_container_child_get);
	if (cancellable)
		g_object_ref (cancellable);
	else
		cancellable = g_cancellable_new ();
	g_hash_table_insert (container->priv->pending_ops, result, cancellable);

	g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
				g_dbus_proxy_get_name (proxy),
				g_dbus_proxy_get_object_path (proxy),
				"org.freedesktop.DBus.Properties",
				"Get",
				g_variant_new ("(ss)",
					       g_dbus_proxy_get_interface_name (proxy),
					       info->dbus_name),
				G_VARIANT_TYPE ("(v)"),
				G_DBUS_CALL_FLAGS_NO_AUTO_START,
				-1, cancellable,
				get_applet_property_cb,
				result);
}

GVariant *
mate_panel_applet_container_child_get_finish (MatePanelAppletContainer *container,
					 GAsyncResult         *result,
					 GError              **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == mate_panel_applet_container_child_get);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_variant_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

static void
child_popup_menu_cb (GObject      *source_object,
		     GAsyncResult *res,
		     gpointer      user_data)
{
	GDBusConnection    *connection = G_DBUS_CONNECTION (source_object);
	GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
	GVariant           *retvals;
	GError             *error = NULL;

	retvals = g_dbus_connection_call_finish (connection, res, &error);
	if (!retvals) {
		g_simple_async_result_set_from_error (result, error);
		g_error_free (error);
	} else {
		g_variant_unref (retvals);
	}

	g_simple_async_result_complete (result);
	g_object_unref (result);
}

void
mate_panel_applet_container_child_popup_menu (MatePanelAppletContainer *container,
					 guint                 button,
					 guint32               timestamp,
					 GCancellable         *cancellable,
					 GAsyncReadyCallback   callback,
					 gpointer              user_data)
{
	GSimpleAsyncResult *result;
	GDBusProxy         *proxy = container->priv->applet_proxy;

	if (!proxy)
		return;

	result = g_simple_async_result_new (G_OBJECT (container),
					    callback,
					    user_data,
					    mate_panel_applet_container_child_popup_menu);

	g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
				g_dbus_proxy_get_name (proxy),
				g_dbus_proxy_get_object_path (proxy),
				MATE_PANEL_APPLET_INTERFACE,
				"PopupMenu",
				g_variant_new ("(uu)", button, timestamp),
				NULL,
				G_DBUS_CALL_FLAGS_NO_AUTO_START,
				-1, cancellable,
				child_popup_menu_cb,
				result);
}

gboolean
mate_panel_applet_container_child_popup_menu_finish (MatePanelAppletContainer *container,
						GAsyncResult         *result,
						GError              **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == mate_panel_applet_container_child_popup_menu);

	return !g_simple_async_result_propagate_error (simple, error);
}
