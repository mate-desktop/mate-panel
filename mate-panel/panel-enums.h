/*
 * panel-enums.h:
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
 */

#ifndef __PANEL_ENUMS_H__
#define __PANEL_ENUMS_H__

#include <glib.h>
#include "panel-enums-gsettings.h"

G_BEGIN_DECLS

#define PANEL_HORIZONTAL_MASK (PANEL_ORIENTATION_TOP  | PANEL_ORIENTATION_BOTTOM)
#define PANEL_VERTICAL_MASK   (PANEL_ORIENTATION_LEFT | PANEL_ORIENTATION_RIGHT)

typedef enum {
	PANEL_EDGE_NONE   = 0,
	PANEL_EDGE_TOP    = 1 << 0,
	PANEL_EDGE_BOTTOM = 1 << 1,
	PANEL_EDGE_LEFT   = 1 << 2,
	PANEL_EDGE_RIGHT  = 1 << 3
} PanelFrameEdge;

typedef enum {
	PANEL_STATE_NORMAL       = 0,
	PANEL_STATE_AUTO_HIDDEN  = 1,
	PANEL_STATE_HIDDEN_UP    = 2,
	PANEL_STATE_HIDDEN_DOWN  = 3,
	PANEL_STATE_HIDDEN_LEFT  = 4,
	PANEL_STATE_HIDDEN_RIGHT = 5
} PanelState;

typedef enum {
	PANEL_GSETTINGS_TOPLEVELS,
	PANEL_GSETTINGS_OBJECTS
} PanelGSettingsKeyType;

G_END_DECLS

#endif /* __PANEL_ENUMS_H__ */
