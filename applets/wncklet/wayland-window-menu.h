/* Wayland backend for the Window Selector applet */

/*
 * Copyright (C) 2026 MATE Desktop Team
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
 */

#ifndef _WNCKLET_APPLET_WAYLAND_WINDOW_MENU_H_
#define _WNCKLET_APPLET_WAYLAND_WINDOW_MENU_H_

#ifdef PACKAGE_NAME
#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif
#endif

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget *wayland_window_menu_new (void);

#ifdef __cplusplus
}
#endif

#endif /* _WNCKLET_APPLET_WAYLAND_WINDOW_MENU_H_ */
