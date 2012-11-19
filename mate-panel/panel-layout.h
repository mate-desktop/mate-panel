/*
 * panel-layout.h: methods to load default panels from file
 *
 * Copyright (C) 2012 Stefano Karapetsas
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
 *      Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __PANEL_LAYOUT_H__
#define __PANEL_LAYOUT_H__

#include <glib.h>
#include <gio/gio.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

void        panel_layout_apply_default_from_gkeyfile (GdkScreen *screen);

G_END_DECLS

#endif /* __PANEL_LAYOUT_H__ */
