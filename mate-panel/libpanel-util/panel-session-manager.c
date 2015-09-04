/*
 * panel-session.c:
 *
 * Copyright (C) 2008 Novell, Inc.
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

#include <dbus/dbus-glib.h>

#include "panel-cleanup.h"
#include "panel-dbus-service.h"

#include "panel-session-manager.h"

static GObject *panel_session_manager_constructor (GType                  type,
						   guint                  n_construct_properties,
						   GObjectConstructParam *construct_properties);

G_DEFINE_TYPE (PanelSessionManager, panel_session_manager, PANEL_TYPE_DBUS_SERVICE);

static void
panel_session_manager_class_init (PanelSessionManagerClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = panel_session_manager_constructor;
}

static void
panel_session_manager_init (PanelSessionManager *manager)
{
}

static GObject *
panel_session_manager_constructor (GType                  type,
				   guint                  n_construct_properties,
				   GObjectConstructParam *construct_properties)
{
	GObject *obj;
	GError  *error;

	obj = G_OBJECT_CLASS (panel_session_manager_parent_class)->constructor (
							type,
							n_construct_properties,
							construct_properties);


	panel_dbus_service_define_service (PANEL_DBUS_SERVICE (obj),
					   "org.gnome.SessionManager",
					   "/org/gnome/SessionManager",
					   "org.gnome.SessionManager");

	error = NULL;
	if (!panel_dbus_service_ensure_connection (PANEL_DBUS_SERVICE (obj),
						   &error)) {
		g_message ("Could not connect to session manager: %s",
			   error->message);
		g_error_free (error);
	}

	return obj;
}

void
panel_session_manager_request_logout (PanelSessionManager           *manager,
				      PanelSessionManagerLogoutType  mode)
{
	GError *error;
	DBusGProxy *proxy;

	g_return_if_fail (PANEL_IS_SESSION_MANAGER (manager));

	error = NULL;

	if (!panel_dbus_service_ensure_connection (PANEL_DBUS_SERVICE (manager),
						   &error)) {
		g_warning ("Could not connect to session manager: %s",
			   error->message);
		g_error_free (error);
		return;
	}

	proxy = panel_dbus_service_get_proxy (PANEL_DBUS_SERVICE (manager));

	if (!dbus_g_proxy_call (proxy, "Logout", &error,
				G_TYPE_UINT, mode, G_TYPE_INVALID,
				G_TYPE_INVALID) &&
	    error != NULL) {
		g_warning ("Could not ask session manager to log out: %s",
			   error->message);
		g_error_free (error);
	}
}

void
panel_session_manager_request_shutdown (PanelSessionManager *manager)
{
	GError *error;
	DBusGProxy *proxy;

	g_return_if_fail (PANEL_IS_SESSION_MANAGER (manager));

	error = NULL;

	if (!panel_dbus_service_ensure_connection (PANEL_DBUS_SERVICE (manager),
						   &error)) {
		g_warning ("Could not connect to session manager: %s",
			   error->message);
		g_error_free (error);
		return;
	}

	proxy = panel_dbus_service_get_proxy (PANEL_DBUS_SERVICE (manager));

	if (!dbus_g_proxy_call (proxy, "Shutdown", &error,
				G_TYPE_INVALID,
				G_TYPE_INVALID) &&
	    error != NULL) {
		g_warning ("Could not ask session manager to shut down: %s",
			   error->message);
		g_error_free (error);
	}
}

gboolean
panel_session_manager_is_shutdown_available (PanelSessionManager *manager)
{
	GError *error;
	DBusGProxy *proxy;
	gboolean is_shutdown_available;

	g_return_val_if_fail (PANEL_IS_SESSION_MANAGER (manager), FALSE);

	error = NULL;

	if (!panel_dbus_service_ensure_connection (PANEL_DBUS_SERVICE (manager),
						   &error)) {
		g_warning ("Could not connect to session manager: %s",
			   error->message);
		g_error_free (error);

		return FALSE;
	}

	proxy = panel_dbus_service_get_proxy (PANEL_DBUS_SERVICE (manager));

	if (!dbus_g_proxy_call (proxy, "CanShutdown", &error,
				G_TYPE_INVALID, G_TYPE_BOOLEAN,
				&is_shutdown_available, G_TYPE_INVALID) &&
	    error != NULL) {
		g_warning ("Could not ask session manager if shut down is available: %s",
			   error->message);
		g_error_free (error);

		return FALSE;
	}

	return is_shutdown_available;
}

PanelSessionManager *
panel_session_manager_get (void)
{
	static PanelSessionManager *manager = NULL;

	if (manager == NULL) {
		manager = g_object_new (PANEL_TYPE_SESSION_MANAGER, NULL);
		panel_cleanup_register (panel_cleanup_unref_and_nullify,
					&manager);
	}

	return manager;
}
