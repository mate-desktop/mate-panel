/*
 * mate-panel-applet-factory.c: panel applet writing API.
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
 */

#include "mate-panel-applet.h"

#include "mate-panel-applet-factory.h"

struct _MatePanelAppletFactory {
	GObject    base;

	gchar     *factory_id;
	guint      n_applets;
	GType      applet_type;
	GClosure  *closure;
};

struct _MatePanelAppletFactoryClass {
	GObjectClass base_class;
};

#define MATE_PANEL_APPLET_FACTORY_OBJECT_PATH  "/org/mate/panel/applet/%s"
#define MATE_PANEL_APPLET_FACTORY_SERVICE_NAME "org.mate.panel.applet.%s"

G_DEFINE_TYPE (MatePanelAppletFactory, mate_panel_applet_factory, G_TYPE_OBJECT)

static void
mate_panel_applet_factory_finalize (GObject *object)
{
	MatePanelAppletFactory *factory = MATE_PANEL_APPLET_FACTORY (object);

	if (factory->factory_id) {
		g_free (factory->factory_id);
		factory->factory_id = NULL;
	}

	if (factory->closure) {
		g_closure_unref (factory->closure);
		factory->closure = NULL;
	}

	G_OBJECT_CLASS (mate_panel_applet_factory_parent_class)->finalize (object);
}

static void
mate_panel_applet_factory_init (MatePanelAppletFactory *factory)
{
}

static void
mate_panel_applet_factory_class_init (MatePanelAppletFactoryClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);

	g_object_class->finalize = mate_panel_applet_factory_finalize;
}

static void
mate_panel_applet_factory_applet_removed (MatePanelAppletFactory *factory,
				     GObject            *applet)
{
	factory->n_applets--;
	if (factory->n_applets == 0)
		g_object_unref (factory);
}

MatePanelAppletFactory *
mate_panel_applet_factory_new (const gchar *factory_id,
			  GType        applet_type,
			  GClosure    *closure)
{
	MatePanelAppletFactory *factory;

	factory = MATE_PANEL_APPLET_FACTORY (g_object_new (PANEL_TYPE_APPLET_FACTORY, NULL));
	factory->factory_id = g_strdup (factory_id);
	factory->applet_type = applet_type;
	factory->closure = g_closure_ref (closure);

	return factory;
}

static void
set_applet_constructor_properties (GObject  *applet,
				   GVariant *props)
{
	GVariantIter iter;
	GVariant    *value;
	gchar       *key;

	g_variant_iter_init (&iter, props);
	while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
		switch (g_variant_classify (value)) {
		case G_VARIANT_CLASS_UINT32: {
			guint32 v = g_variant_get_uint32 (value);

			g_object_set (applet, key, v, NULL);
		}
			break;
		case G_VARIANT_CLASS_STRING: {
			const gchar *v = g_variant_get_string (value, NULL);

			g_object_set (applet, key, v, NULL);
		}
			break;
		case G_VARIANT_CLASS_BOOLEAN: {
			gboolean v = g_variant_get_boolean (value);

			g_object_set (applet, key, v, NULL);
		}
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	}
}


static void
mate_panel_applet_factory_get_applet (MatePanelAppletFactory    *factory,
				 GDBusConnection       *connection,
				 GVariant              *parameters,
				 GDBusMethodInvocation *invocation)
{
	GObject     *applet;
	const gchar *applet_id;
	gint         screen_num;
	GVariant    *props;
	GdkScreen   *screen;
	guint32      xid;
	const gchar *object_path;

	g_variant_get (parameters, "(&si@a{sv})", &applet_id, &screen_num, &props);

	applet = g_object_new (factory->applet_type,
			       "id", applet_id,
			       "connection", connection,
			       "closure", factory->closure,
			       NULL);
	factory->n_applets++;
	g_object_weak_ref (applet, (GWeakNotify) mate_panel_applet_factory_applet_removed, factory);

	set_applet_constructor_properties (applet, props);
	g_variant_unref (props);

	screen = screen_num != -1 ?
		gdk_display_get_screen (gdk_display_get_default (), screen_num) :
		gdk_screen_get_default ();

	xid = mate_panel_applet_get_xid (MATE_PANEL_APPLET (applet), screen);
	object_path = mate_panel_applet_get_object_path (MATE_PANEL_APPLET (applet));

	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new ("(ou)", object_path, xid));
}

static void
method_call_cb (GDBusConnection       *connection,
		const gchar           *sender,
		const gchar           *object_path,
		const gchar           *interface_name,
		const gchar           *method_name,
		GVariant              *parameters,
		GDBusMethodInvocation *invocation,
		gpointer               user_data)
{
	MatePanelAppletFactory *factory = MATE_PANEL_APPLET_FACTORY (user_data);

	if (g_strcmp0 (method_name, "GetApplet") == 0) {
		mate_panel_applet_factory_get_applet (factory, connection, parameters, invocation);
	}
}

static const gchar introspection_xml[] =
	"<node>"
	    "<interface name='org.mate.panel.applet.AppletFactory'>"
	      "<method name='GetApplet'>"
	        "<arg name='applet_id' type='s' direction='in'/>"
	        "<arg name='screen' type='i' direction='in'/>"
	        "<arg name='props' type='a{sv}' direction='in'/>"
	        "<arg name='applet' type='o' direction='out'/>"
	        "<arg name='xid' type='u' direction='out'/>"
	      "</method>"
	    "</interface>"
	  "</node>";

static const GDBusInterfaceVTable interface_vtable = {
	method_call_cb,
	NULL,
	NULL
};

static GDBusNodeInfo *introspection_data = NULL;

static void
on_name_acquired (GDBusConnection    *connection,
		  const gchar        *name,
		  MatePanelAppletFactory *factory)
{
	gchar  *object_path;
	GError *error = NULL;

	if (!introspection_data)
		introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
	object_path = g_strdup_printf (MATE_PANEL_APPLET_FACTORY_OBJECT_PATH, factory->factory_id);
	g_dbus_connection_register_object (connection,
					   object_path,
					   introspection_data->interfaces[0],
					   &interface_vtable,
					   factory, NULL,
					   &error);
	if (error) {
		g_printerr ("Failed to register object %s: %s\n", object_path, error->message);
		g_error_free (error);
	}

	g_free (object_path);
}

static void
on_name_lost (GDBusConnection    *connection,
	      const gchar        *name,
	      MatePanelAppletFactory *factory)
{
	g_object_unref (factory);
}

gboolean
mate_panel_applet_factory_register_service (MatePanelAppletFactory *factory)
{
	gchar *service_name;

	service_name = g_strdup_printf (MATE_PANEL_APPLET_FACTORY_SERVICE_NAME, factory->factory_id);
	g_bus_own_name (G_BUS_TYPE_SESSION,
			service_name,
			G_BUS_NAME_OWNER_FLAGS_NONE,
			(GBusAcquiredCallback) on_name_acquired,
			NULL,
			(GBusNameLostCallback) on_name_lost,
			factory, NULL);
	g_free (service_name);

	return TRUE;
}


