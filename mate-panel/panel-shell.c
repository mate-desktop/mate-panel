/*
 * panel-shell.c: panel shell interface implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 * Copyright (C) 2012-2021 MATE Developers
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

#include "applet.h"
#include "drawer.h"
#include "launcher.h"
#include "panel-addto.h"
#include "panel-lockdown.h"
#include "panel-multimonitor.h"
#include "panel-profile.h"
#include "panel-properties-dialog.h"
#include "panel-session.h"
#include "panel-schemas.h"
#include "panel-toplevel.h"
#include "panel-widget.h"

#include "panel-shell.h"

#define PANEL_DBUS_SERVICE "org.mate.Panel"

#define PANEL_DBUS_SHELL_PATH "/org/mate/panel/Shell"
#define PANEL_DBUS_SHELL_IFACE "org.mate.Panel.Shell"

static GDBusConnection *dbus_connection = NULL;
static guint shell_object_id = 0;

static const gchar panel_shell_introspection_xml[] =
	"<node>"
	"  <interface name='" PANEL_DBUS_SHELL_IFACE "'>"
	"    <method name='AddToPanel'>"
	"      <arg type='s' name='toplevel_id' direction='in'/>"
	"    </method>"
	"    <method name='GetObjects'>"
	"      <arg type='s' name='toplevel_id' direction='in'/>"
	"      <arg type='as' name='object_ids' direction='out'/>"
	"    </method>"
	"    <method name='MoveObject'>"
	"      <arg type='s' name='object_id' direction='in'/>"
	"      <arg type='i' name='direction' direction='in'/>"
	"      <arg type='b' name='moved' direction='out'/>"
	"    </method>"
	"    <method name='ShowProperties'>"
	"      <arg type='s' name='object_id' direction='in'/>"
	"      <arg type='b' name='shown' direction='out'/>"
	"    </method>"
	"    <method name='GetMonitors'>"
	"      <arg type='a(isss(iiii)b)' name='monitors' direction='out'/>"
	"    </method>"
	"    <signal name='MonitorsChanged'/>"
	"  </interface>"
	"</node>";

static gboolean
panel_shell_neighbor_locked (PanelWidget *panel_widget,
			     AppletInfo  *info,
			     int          direction)
{
	GList      *l;
	AppletData *ad;
	AppletData *neighbor;
	AppletInfo *neighbor_info;

	if (g_settings_get_boolean (info->settings, PANEL_OBJECT_LOCKED_KEY))
		return TRUE;

	for (l = panel_widget->applet_list; l; l = l->next) {
		ad = l->data;
		if (ad->applet == info->widget)
			break;
	}

	if (l == NULL)
		return FALSE;

	l = direction > 0 ? l->next : l->prev;
	if (l == NULL)
		return FALSE;

	neighbor = l->data;
	if (neighbor->pack_type != ad->pack_type)
		return FALSE;

	neighbor_info = g_object_get_data (G_OBJECT (neighbor->applet), "applet_info");

	return neighbor_info != NULL &&
	       g_settings_get_boolean (neighbor_info->settings, PANEL_OBJECT_LOCKED_KEY);
}

static gboolean
panel_shell_refuse_if_locked_down (GDBusMethodInvocation *invocation)
{
	if (!panel_lockdown_get_locked_down ())
		return FALSE;

	g_dbus_method_invocation_return_error_literal (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_ACCESS_DENIED,
						       "The panel is locked down");
	return TRUE;
}

static void
panel_shell_method_call (GDBusConnection       *connection,
			 const gchar           *sender,
			 const gchar           *object_path,
			 const gchar           *interface_name,
			 const gchar           *method_name,
			 GVariant              *parameters,
			 GDBusMethodInvocation *invocation,
			 gpointer               user_data)
{
	if (g_strcmp0 (method_name, "AddToPanel") == 0) {
		const gchar   *toplevel_id;
		PanelToplevel *toplevel;

		if (panel_shell_refuse_if_locked_down (invocation))
			return;

		g_variant_get (parameters, "(&s)", &toplevel_id);
		toplevel = panel_profile_get_toplevel_by_id (toplevel_id);

		if (toplevel == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       G_DBUS_ERROR,
							       G_DBUS_ERROR_INVALID_ARGS,
							       "Unknown panel '%s'",
							       toplevel_id);
			return;
		}

		panel_addto_present (NULL, panel_toplevel_get_panel_widget (toplevel));
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "GetObjects") == 0) {
		const gchar     *toplevel_id;
		PanelToplevel   *toplevel;
		PanelWidget     *panel_widget;
		GVariantBuilder  builder;
		GList           *l;

		g_variant_get (parameters, "(&s)", &toplevel_id);
		toplevel = panel_profile_get_toplevel_by_id (toplevel_id);

		if (toplevel == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       G_DBUS_ERROR,
							       G_DBUS_ERROR_INVALID_ARGS,
							       "Unknown panel '%s'",
							       toplevel_id);
			return;
		}

		panel_widget = panel_toplevel_get_panel_widget (toplevel);
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
		for (l = panel_widget->applet_list; l; l = l->next) {
			AppletData *ad = l->data;
			AppletInfo *info = g_object_get_data (G_OBJECT (ad->applet), "applet_info");
			const char *id = info ? mate_panel_applet_get_id (info) : NULL;

			if (id != NULL)
				g_variant_builder_add (&builder, "s", id);
		}

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(as)", &builder));
	} else if (g_strcmp0 (method_name, "MoveObject") == 0) {
		const gchar *object_id;
		gint32       direction;
		AppletInfo  *info;
		PanelWidget *panel_widget;
		gboolean     moved = FALSE;

		if (panel_shell_refuse_if_locked_down (invocation))
			return;

		g_variant_get (parameters, "(&si)", &object_id, &direction);
		info = mate_panel_applet_get_by_id (object_id);

		if (info == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       G_DBUS_ERROR,
							       G_DBUS_ERROR_INVALID_ARGS,
							       "Unknown object '%s'",
							       object_id);
			return;
		}

		panel_widget = PANEL_WIDGET (gtk_widget_get_parent (info->widget));
		if (panel_widget != NULL && mate_panel_applet_can_freely_move (info) &&
		    !panel_shell_neighbor_locked (panel_widget, info, direction))
			moved = panel_widget_move_applet (panel_widget, info->widget, direction);

		if (moved)
			mate_panel_applet_save_queued_positions ();

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(b)", moved));
	} else if (g_strcmp0 (method_name, "ShowProperties") == 0) {
		const gchar *object_id;
		AppletInfo  *info;
		gboolean     shown = FALSE;

		if (panel_shell_refuse_if_locked_down (invocation))
			return;

		g_variant_get (parameters, "(&s)", &object_id);
		info = mate_panel_applet_get_by_id (object_id);

		if (info != NULL && info->type == PANEL_OBJECT_LAUNCHER) {
			launcher_properties (info->data);
			shown = TRUE;
		} else if (info != NULL && info->type == PANEL_OBJECT_DRAWER) {
			panel_properties_dialog_present (((Drawer *) info->data)->toplevel);
			shown = TRUE;
		}

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(b)", shown));
	} else if (g_strcmp0 (method_name, "GetMonitors") == 0) {
		GVariantBuilder builder;
		int             primary = panel_multimonitor_get_primary ();
		int             n = panel_multimonitor_monitors ();
		int             i;

		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(isss(iiii)b)"));
		for (i = 0; i < n; i++) {
			char *id = panel_multimonitor_get_monitor_id (i);
			char *connector = panel_multimonitor_get_connector (i);
			char *name = panel_multimonitor_get_display_name (i);

			g_variant_builder_add (&builder, "(isss(iiii)b)", i,
					       id ? id : "",
					       connector ? connector : "",
					       name ? name : "",
					       panel_multimonitor_x (i),
					       panel_multimonitor_y (i),
					       panel_multimonitor_width (i),
					       panel_multimonitor_height (i),
					       primary >= 0 ? i == primary : i == 0);
			g_free (id);
			g_free (connector);
			g_free (name);
		}

		g_dbus_method_invocation_return_value (invocation,
						       g_variant_new ("(a(isss(iiii)b))", &builder));
	}
}

void
panel_shell_monitors_changed (void)
{
	if (dbus_connection == NULL || shell_object_id == 0)
		return;

	g_dbus_connection_emit_signal (dbus_connection, NULL,
				       PANEL_DBUS_SHELL_PATH, PANEL_DBUS_SHELL_IFACE,
				       "MonitorsChanged", NULL, NULL);
}

static const GDBusInterfaceVTable panel_shell_vtable = {
	panel_shell_method_call,
	NULL,
	NULL,
	{ 0 }
};

static void
panel_shell_export_object (void)
{
	GDBusNodeInfo *node;
	GError        *error = NULL;

	node = g_dbus_node_info_new_for_xml (panel_shell_introspection_xml, NULL);
	if (node == NULL)
		return;

	shell_object_id = g_dbus_connection_register_object (dbus_connection,
							     PANEL_DBUS_SHELL_PATH,
							     node->interfaces[0],
							     &panel_shell_vtable,
							     NULL, NULL, &error);
	if (shell_object_id == 0) {
		g_warning ("Cannot export the panel shell object: %s", error->message);
		g_error_free (error);
	}

	g_dbus_node_info_unref (node);
}

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
		if (shell_object_id != 0) {
			g_dbus_connection_unregister_object (dbus_connection, shell_object_id);
			shell_object_id = 0;
		}
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
		panel_shell_export_object ();
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
