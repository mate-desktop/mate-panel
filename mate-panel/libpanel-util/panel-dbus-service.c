/*
 * panel-dbus-service.c: a simple base object to use a DBus service. Only
 * useful when subclassed.
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Based on code from panel-power-manager.c:
 *  Copyright (C) 2006 Ray Strode <rstrode@redhat.com>
 *  (not sure the copyright header was complete)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <string.h>

#include <glib.h>
#include <glib-object.h>

#include "panel-dbus-service.h"

struct _PanelDBusServicePrivate {
	GDBusConnection *dbus_connection;
	GDBusProxy      *service_proxy;
	guint32          is_connected : 1;

	const char      *service_name;
	const char      *service_path;
	const char      *service_interface;
};

static void panel_dbus_service_finalize (GObject *object);
static void panel_dbus_service_class_install_properties (PanelDBusServiceClass *service_class);

static void panel_dbus_service_get_property (GObject    *object,
					     guint       prop_id,
					     GValue     *value,
					     GParamSpec *pspec);
enum {
  PROP_0 = 0,
  PROP_IS_CONNECTED
};

G_DEFINE_TYPE (PanelDBusService, panel_dbus_service, G_TYPE_OBJECT);

static void
panel_dbus_service_class_init (PanelDBusServiceClass *service_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (service_class);

	object_class->finalize = panel_dbus_service_finalize;

	panel_dbus_service_class_install_properties (service_class);

	g_type_class_add_private (service_class,
				  sizeof (PanelDBusServicePrivate));
}

static void
panel_dbus_service_class_install_properties (PanelDBusServiceClass *service_class)
{
	GObjectClass *object_class;
	GParamSpec *param_spec;

	object_class = G_OBJECT_CLASS (service_class);
	object_class->get_property = panel_dbus_service_get_property;

	param_spec = g_param_spec_boolean ("is-connected",
					   "Is connected",
					   "Whether the panel is connected to "
					   "a DBus service",
					   FALSE,
					   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_IS_CONNECTED,
					 param_spec);
}

static void
panel_dbus_service_init (PanelDBusService *service)
{
	service->priv = G_TYPE_INSTANCE_GET_PRIVATE (service,
						     PANEL_TYPE_DBUS_SERVICE,
						     PanelDBusServicePrivate);

	service->priv->dbus_connection = NULL;
	service->priv->service_proxy = NULL;
	service->priv->is_connected = FALSE;

	service->priv->service_name = NULL;
	service->priv->service_path = NULL;
	service->priv->service_interface = NULL;
}

static void
panel_dbus_service_finalize (GObject *object)
{
	PanelDBusService *service;
		
	service = PANEL_DBUS_SERVICE (object);

	if (service->priv->dbus_connection != NULL) {
		g_object_unref (service->priv->dbus_connection);
		service->priv->dbus_connection = NULL;
	}

	if (service->priv->service_proxy != NULL) {
		g_object_unref (service->priv->service_proxy);
		service->priv->service_proxy = NULL;
	}

	G_OBJECT_CLASS (panel_dbus_service_parent_class)->finalize (object);
}

static void
panel_dbus_service_get_property (GObject    *object,
				 guint       prop_id,
				 GValue     *value,
				 GParamSpec *pspec)
{
	PanelDBusService *service;
		
	service = PANEL_DBUS_SERVICE (object);

	switch (prop_id) {
	case PROP_IS_CONNECTED:
		g_value_set_boolean (value,
				     service->priv->is_connected);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
						   prop_id,
						   pspec);
	}
}

gboolean
panel_dbus_service_ensure_connection (PanelDBusService  *service,
				      GError           **error)
{
	GError   *connection_error;
	gboolean  is_connected;

	g_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);

	if (!service->priv->service_name ||
	    !service->priv->service_path ||
	    !service->priv->service_interface)
		return FALSE;

	connection_error = NULL;
	if (service->priv->dbus_connection == NULL) {
		service->priv->dbus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
				NULL,
				&connection_error);

		if (service->priv->dbus_connection == NULL) {
			g_propagate_error (error, connection_error);
			is_connected = FALSE;
			goto out;
		}
	}

	if (service->priv->service_proxy == NULL) {
		service->priv->service_proxy =
			g_dbus_proxy_new_sync (service->priv->dbus_connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					service->priv->service_name,
					service->priv->service_path,
					service->priv->service_interface,
					NULL,
					&connection_error);

		if (service->priv->service_proxy == NULL) {
			g_propagate_error (error, connection_error);
			is_connected = FALSE;
			goto out;
		}
	}
	is_connected = TRUE;

out:
	if (service->priv->is_connected != is_connected) {
		service->priv->is_connected = is_connected;
		g_object_notify (G_OBJECT (service), "is-connected");
	}

	if (!is_connected) {
		if (service->priv->dbus_connection == NULL) {
			if (service->priv->service_proxy != NULL) {
				g_object_unref (service->priv->service_proxy);
				service->priv->service_proxy = NULL;
			}
		}
	}

	return is_connected;
}

void
panel_dbus_service_define_service (PanelDBusService *service,
				   const char       *name,
				   const char       *path,
				   const char       *interface)
{
	g_return_if_fail (PANEL_IS_DBUS_SERVICE (service));

	g_assert (name != NULL);
	g_assert (path != NULL);
	g_assert (interface != NULL);
	g_assert (service->priv->service_name == NULL);
	g_assert (service->priv->service_path == NULL);
	g_assert (service->priv->service_interface == NULL);

	service->priv->service_name = name;
	service->priv->service_path = path;
	service->priv->service_interface = interface;
}

GDBusProxy *
panel_dbus_service_get_proxy (PanelDBusService *service)
{
	g_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), NULL);

	return service->priv->service_proxy;
}
