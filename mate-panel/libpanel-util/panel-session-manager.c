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

#include <gio/gio.h>
#include "panel-cleanup.h"
#include "panel-session-manager.h"

struct _PanelSessionManager {
	GObject     parent;
	GDBusProxy *proxy;
};

G_DEFINE_TYPE (PanelSessionManager, panel_session_manager, G_TYPE_OBJECT)

static void
panel_session_manager_finalize (GObject *object)
{
	PanelSessionManager *manager = PANEL_SESSION_MANAGER (object);

	if (manager->proxy != NULL)
		g_object_unref (manager->proxy);

	G_OBJECT_CLASS (panel_session_manager_parent_class)->finalize (object);
}

static void
panel_session_manager_class_init (PanelSessionManagerClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = panel_session_manager_finalize;
}

static void
panel_session_manager_init (PanelSessionManager *manager)
{
	GError *error = NULL;

	manager->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
							G_DBUS_PROXY_FLAGS_NONE,
							NULL,
							"org.gnome.SessionManager",
							"/org/gnome/SessionManager",
							"org.gnome.SessionManager",
							NULL,
							&error);
	if (manager->proxy == NULL) {
		g_warning ("Unable to contact session manager daemon: %s\n", error->message);
		g_error_free (error);
	}
}

void
panel_session_manager_request_logout (PanelSessionManager           *manager,
				      PanelSessionManagerLogoutType  mode)
{
	GError *error = NULL;
	GVariant *ret;

	g_return_if_fail (PANEL_IS_SESSION_MANAGER (manager));

	if (manager->proxy == NULL)
		return;

	ret = g_dbus_proxy_call_sync (manager->proxy, "Logout",
				      g_variant_new ("(u)", mode),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (ret == NULL) {
		g_warning ("Could not ask session manager to log out: %s",
			   error->message);
		g_error_free (error);
	} else {
		g_variant_unref (ret);
	}
}

void
panel_session_manager_request_shutdown (PanelSessionManager *manager)
{
	GError *error = NULL;
	GVariant *ret;

	g_return_if_fail (PANEL_IS_SESSION_MANAGER (manager));

	if (manager->proxy == NULL)
		return;

	ret = g_dbus_proxy_call_sync (manager->proxy, "Shutdown",
				      g_variant_new ("()"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (ret == NULL) {
		g_warning ("Could not ask session manager to shut down: %s",
			   error->message);
		g_error_free (error);
	} else {
		g_variant_unref (ret);
	}
}

gboolean
panel_session_manager_is_shutdown_available (PanelSessionManager *manager)
{
	GError *error = NULL;
	gboolean is_shutdown_available;
	GVariant *ret;

	g_return_val_if_fail (PANEL_IS_SESSION_MANAGER (manager), FALSE);

	if (manager->proxy == NULL)
		return FALSE;

	ret = g_dbus_proxy_call_sync (manager->proxy, "CanShutdown",
				      g_variant_new ("()"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (ret == NULL) {
		g_warning ("Could not ask session manager if shut down is available: %s",
			   error->message);
		g_error_free (error);
		return FALSE;
	} else {
		g_variant_get (ret, "(b)", &is_shutdown_available);
		g_variant_unref (ret);
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
