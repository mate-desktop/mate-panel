/*
 * panel-shell.c: panel shell interface implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
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
 *      Jacob Berkman <jacob@ximian.com>
 *      Colin Walters <walters@verbum.org>
 *      Vincent Untz <vuntz@gnome.org>
 */

#include <config.h>
#include <glib/gi18n.h>

#include <libpanel-util/panel-cleanup.h>

#include "panel-profile.h"
#include "panel-session.h"

#include "panel-shell.h"

#define PANEL_DBUS_SERVICE "org.mate.Panel"

static GDBusConnection *dbus_connection = NULL;

static void
panel_shell_on_name_lost (GDBusConnection *connection,
			  const gchar     *sender_name,
			  const gchar     *object_path,
			  const gchar     *interface_name,
			  const gchar     *signal_name,
			  GVariant        *parameters,
			  gpointer         user_data)
{
	/* We lost our DBus name, and there is something replacing us.
	 * Tell the SM not to restart us automatically, then exit. */
	g_printerr ("Panel leaving: a new panel shell is starting.\n");

	panel_session_do_not_restart ();
	panel_shell_quit ();
}

static void
panel_shell_cleanup (gpointer data)
{
	if (dbus_connection != NULL) {
		g_object_unref (dbus_connection);
		dbus_connection = NULL;
	}
}

gboolean
panel_shell_register (gboolean replace)
{
	GBusNameOwnerFlags flags;
	guint32            request_name_reply;
	GVariant          *result;
	gboolean           retval = FALSE;
	GError            *error = NULL;

	if (dbus_connection != NULL)
		return TRUE;

	panel_cleanup_register (PANEL_CLEAN_FUNC (panel_shell_cleanup), NULL);

	/* There isn't a sync version of g_bus_own_name, so we have to requeste
	 * the name manually here. There's no ui yet so it's safe to use this
	 * sync api */
	dbus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (dbus_connection == NULL) {
		g_warning ("Cannot register the panel shell: %s", error->message);
		goto register_out;
	}

	flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
	if (replace)
		flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

	result = g_dbus_connection_call_sync (dbus_connection,
					      "org.freedesktop.DBus",
					      "/org/freedesktop/DBus",
					      "org.freedesktop.DBus",
					      "RequestName",
					      g_variant_new ("(su)",
							     PANEL_DBUS_SERVICE,
							     flags),
					      G_VARIANT_TYPE ("(u)"),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1, NULL, &error);
	if (!result) {
		g_warning ("Cannot register the panel shell: %s",
			   error->message);
		g_error_free (error);

		goto register_out;
	}

	g_variant_get (result, "(u)", &request_name_reply);
	g_variant_unref (result);

	switch (request_name_reply) {
	case 1: /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */
	case 4: /* DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER */
		retval = TRUE;
		g_dbus_connection_signal_subscribe (dbus_connection,
						    "org.freedesktop.DBus",
						    "org.freedesktop.DBus",
						    "NameLost",
						    "/org/freedesktop/DBus",
						    PANEL_DBUS_SERVICE,
						    G_DBUS_SIGNAL_FLAGS_NONE,
						    (GDBusSignalCallback)panel_shell_on_name_lost,
						    NULL, NULL);
		break;
	case 2: /* DBUS_REQUEST_NAME_REPLY_IN_QUEUE */
	case 3: /* DBUS_REQUEST_NAME_REPLY_EXISTS */
		g_printerr ("Cannot register the panel shell: there is "
			    "already one running.\n");
		break;
	default:
		g_warning ("Cannot register the panel shell: unhandled "
			   "reply %u from RequestName", request_name_reply);
	}

register_out:

	if (!retval) {
		panel_session_do_not_restart ();
		panel_shell_cleanup (NULL);
	}

	return retval;
}

void
panel_shell_quit (void)
{
	GSList *toplevels_to_destroy, *l;

        toplevels_to_destroy = g_slist_copy (panel_toplevel_list_toplevels ());
        for (l = toplevels_to_destroy; l; l = l->next)
		gtk_widget_destroy (l->data);
        g_slist_free (toplevels_to_destroy);

	gtk_main_quit ();
}
