/*
 * panel-session.h:
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

#ifndef PANEL_SESSION_MANAGER_H
#define PANEL_SESSION_MANAGER_H

#include <glib-object.h>

#include "panel-dbus-service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_SESSION_MANAGER		(panel_session_manager_get_type ())
#define PANEL_SESSION_MANAGER(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_SESSION_MANAGER, PanelSessionManager))
#define PANEL_SESSION_MANAGER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_SESSION_MANAGER, PanelSessionManagerClass))
#define PANEL_IS_SESSION_MANAGER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_SESSION_MANAGER))
#define PANEL_IS_SESSION_MANAGER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_SESSION_MANAGER))
#define PANEL_SESSION_MANAGER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_SESSION_MANAGER, PanelSessionManagerClass))

typedef struct _PanelSessionManager		PanelSessionManager;
typedef struct _PanelSessionManagerClass	PanelSessionManagerClass;

struct _PanelSessionManager {
	PanelDBusService parent;
};

struct _PanelSessionManagerClass {
	PanelDBusServiceClass parent_class;
};

GType panel_session_manager_get_type (void);

/* Keep in sync with the values defined in mate-session/session.h */
typedef enum {
	PANEL_SESSION_MANAGER_LOGOUT_MODE_NORMAL = 0,
	PANEL_SESSION_MANAGER_LOGOUT_MODE_NO_CONFIRMATION,
	PANEL_SESSION_MANAGER_LOGOUT_MODE_FORCE
} PanelSessionManagerLogoutType;

PanelSessionManager *panel_session_manager_get (void);

void panel_session_manager_request_logout   (PanelSessionManager           *session,
					     PanelSessionManagerLogoutType  mode);
void panel_session_manager_request_shutdown (PanelSessionManager *session);

gboolean panel_session_manager_is_shutdown_available (PanelSessionManager *session);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_SESSION_MANAGER_H */
