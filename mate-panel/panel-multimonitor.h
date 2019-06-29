/*
 * panel-multimonitor.h: Multi-monitor and Xinerama support for the panel.
 *
 * Copyright (C) 2001 George Lebl <jirka@5z.com>
 *               2002 Sun Microsystems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 * USA
 *
 * Authors: George Lebl <jirka@5z.com>,
 *          Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_MULTIMONITOR_H__
#define __PANEL_MULTIMONITOR_H__

#include <gtk/gtk.h>

void	panel_multimonitor_init                  (void);
void	panel_multimonitor_reinit                (void);

int	panel_multimonitor_monitors              (void);

int	panel_multimonitor_x                     (int monitor);
int	panel_multimonitor_y                     (int monitor);
int	panel_multimonitor_width                 (int monitor);
int	panel_multimonitor_height                (int monitor);

int	panel_multimonitor_locate_widget_monitor (GtkWidget *widget);
int	panel_multimonitor_get_monitor_at_point  (int x, int y);
void	panel_multimonitor_is_at_visible_extreme (int        monitor_id,
						  gboolean  *leftmost,
						  gboolean  *rightmost,
						  gboolean  *topmost,
						  gboolean  *bottommost);
void	panel_multimonitor_get_bounds            (GdkPoint *min,
						  GdkPoint *max);

#endif /* __PANEL_MULTIMONITOR_H__ */
