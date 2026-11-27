/* Wncklet applet Wayland window previews */

/*
 * Copyright (C) 2026 MATE Developers
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

#ifndef _WNCKLET_APPLET_WAYLAND_PREVIEW_H_
#define _WNCKLET_APPLET_WAYLAND_PREVIEW_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _WaylandPreview WaylandPreview;

WaylandPreview *wayland_preview_new (void);
void wayland_preview_free (WaylandPreview *preview);
void wayland_preview_set_enabled (WaylandPreview *preview, gboolean enabled);
void wayland_preview_set_size (WaylandPreview *preview, gint size);
void wayland_preview_hover (WaylandPreview *preview, GtkWidget *button, GList *windows);
void wayland_preview_leave (WaylandPreview *preview);
void wayland_preview_hide (WaylandPreview *preview);

G_END_DECLS

#endif /* _WNCKLET_APPLET_WAYLAND_PREVIEW_H_ */
