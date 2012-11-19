/*
 * panel-dbus-service.h: a simple base object to use a DBus service. Only
 * useful when subclassed.
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Based on code from panel-power-manager.h:
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

#ifndef PANEL_DBUS_SERVICE_H
#define PANEL_DBUS_SERVICE_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_DBUS_SERVICE			(panel_dbus_service_get_type ())
#define PANEL_DBUS_SERVICE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_DBUS_SERVICE, PanelDBusService))
#define PANEL_DBUS_SERVICE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_DBUS_SERVICE, PanelDBusServiceClass))
#define PANEL_IS_DBUS_SERVICE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_DBUS_SERVICE))
#define PANEL_IS_DBUS_SERVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_DBUS_SERVICE))
#define PANEL_DBUS_SERVICE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_DBUS_SERVICE, PanelDBusServiceClass))

typedef struct _PanelDBusService	PanelDBusService;
typedef struct _PanelDBusServiceClass	PanelDBusServiceClass;
typedef struct _PanelDBusServicePrivate	PanelDBusServicePrivate;

struct _PanelDBusService {
	GObject parent;

	/*< private > */
	PanelDBusServicePrivate *priv;
};

struct _PanelDBusServiceClass {
	GObjectClass parent_class;
};

GType panel_dbus_service_get_type (void);

void     panel_dbus_service_define_service    (PanelDBusService *service,
					       const char       *name,
					       const char       *path,
					       const char       *interface);

gboolean panel_dbus_service_ensure_connection (PanelDBusService  *service,
					       GError           **error);

DBusGProxy *panel_dbus_service_get_proxy (PanelDBusService *service);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_DBUS_SERVICE_H */
