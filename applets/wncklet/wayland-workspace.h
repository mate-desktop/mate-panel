/* Wncklet applet Wayland workspace backend */

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

#ifndef _WNCKLET_APPLET_WAYLAND_WORKSPACE_H_
#define _WNCKLET_APPLET_WAYLAND_WORKSPACE_H_

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget*  wayland_workspace_new            (void);
void        wayland_workspace_set_orientation(GtkWidget *pager_widget,
                                              GtkOrientation orientation);
void        wayland_workspace_set_rows       (GtkWidget *pager_widget,
                                              int n_rows);
void        wayland_workspace_set_show_all   (GtkWidget *pager_widget,
                                              gboolean show_all);
void        wayland_workspace_set_show_names (GtkWidget *pager_widget,
                                              gboolean show_names);
int         wayland_workspace_get_count      (GtkWidget *pager_widget);
const char* wayland_workspace_get_name       (GtkWidget *pager_widget,
                                              int index);

#ifdef __cplusplus
}
#endif

#endif /* _WNCKLET_APPLET_WAYLAND_WORKSPACE_H_ */
