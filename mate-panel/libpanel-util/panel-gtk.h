/*
 * panel-gtk.h: various small extensions to gtk+
 *
 * Copyright (C) 2009-2010 Novell, Inc.
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

#ifndef PANEL_GTK_H
#define PANEL_GTK_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_GTK_BUILDER_GET(builder, name) GTK_WIDGET (gtk_builder_get_object (builder, name))

void panel_gtk_file_chooser_add_image_preview (GtkFileChooser *chooser);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_GTK_H */
