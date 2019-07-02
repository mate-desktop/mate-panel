/*
 * wayland-backend.h: Support for running on Wayland compositors
 *
 * Copyright (C) 2019 William Wold
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
 *	William Wold <wm@wmww.sh>
 */

#ifndef __WAYLAND_BACKEND_H__
#define __WAYLAND_BACKEND_H__

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>

#include "panel-toplevel.h"

void wayland_panel_toplevel_init (PanelToplevel* toplevel);
void wayland_panel_toplevel_update_placement (PanelToplevel* toplevel);

#endif /* __WAYLAND_BACKEND_H__ */
