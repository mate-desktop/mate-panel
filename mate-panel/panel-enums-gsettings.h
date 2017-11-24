/*
 * panel-enums-gsettings.h:
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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

 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 *	Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __PANEL_ENUMS_GSETTINGS_H__
#define __PANEL_ENUMS_GSETTINGS_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum { /*< flags=0 >*/
	PANEL_ORIENTATION_TOP    = 1 << 0,
	PANEL_ORIENTATION_RIGHT  = 1 << 1,
	PANEL_ORIENTATION_BOTTOM = 1 << 2,
	PANEL_ORIENTATION_LEFT   = 1 << 3
} PanelOrientation;

typedef enum {
	PANEL_ANIMATION_SLOW   = 0,
	PANEL_ANIMATION_MEDIUM = 1,
	PANEL_ANIMATION_FAST   = 2
} PanelAnimationSpeed;

typedef enum {
	PANEL_BACK_NONE  = 0,
	PANEL_BACK_COLOR = 1,
	PANEL_BACK_IMAGE = 2
} PanelBackgroundType;

typedef enum {
	PANEL_OBJECT_DRAWER,
	PANEL_OBJECT_MENU,
	PANEL_OBJECT_LAUNCHER,
	PANEL_OBJECT_APPLET,
	PANEL_OBJECT_ACTION,
	PANEL_OBJECT_MENU_BAR,
	PANEL_OBJECT_SEPARATOR,
} PanelObjectType;

typedef enum {
	PANEL_ACTION_NONE = 0,
	PANEL_ACTION_LOCK,
	PANEL_ACTION_LOGOUT,
	PANEL_ACTION_RUN,
	PANEL_ACTION_SEARCH,
	PANEL_ACTION_FORCE_QUIT,
	PANEL_ACTION_CONNECT_SERVER,
	PANEL_ACTION_SHUTDOWN,
	PANEL_ACTION_LAST
} PanelActionButtonType;

G_END_DECLS

#endif /* __PANEL_ENUMS_GSETTINGS_H__ */
