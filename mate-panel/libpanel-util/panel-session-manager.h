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

G_BEGIN_DECLS

#define PANEL_TYPE_SESSION_MANAGER		(panel_session_manager_get_type ())
G_DECLARE_FINAL_TYPE (PanelSessionManager, panel_session_manager, PANEL, SESSION_MANAGER, GObject);

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

G_END_DECLS

#endif /* PANEL_SESSION_MANAGER_H */
