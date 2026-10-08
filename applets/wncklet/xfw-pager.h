/* Wncklet applet workspace pager backed by libxfce4windowing */

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

#ifndef _WNCKLET_APPLET_XFW_PAGER_H_
#define _WNCKLET_APPLET_XFW_PAGER_H_

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget*  xfw_pager_new            (void);
void        xfw_pager_set_orientation(GtkWidget *pager_widget,
                                      GtkOrientation orientation);
void        xfw_pager_set_rows       (GtkWidget *pager_widget,
                                      int n_rows);
void        xfw_pager_set_show_all   (GtkWidget *pager_widget,
                                      gboolean show_all);
void        xfw_pager_set_show_names (GtkWidget *pager_widget,
                                      gboolean show_names);
int         xfw_pager_get_count      (GtkWidget *pager_widget);
const char* xfw_pager_get_name       (GtkWidget *pager_widget,
                                      int index);
int         xfw_pager_get_active_index (GtkWidget *pager_widget);
void        xfw_pager_activate_nth     (GtkWidget *pager_widget,
                                        int index);

#ifdef __cplusplus
}
#endif

#endif /* _WNCKLET_APPLET_XFW_PAGER_H_ */