/*
 * panel-multiscreen.h: Multi-screen and Xinerama support for the panel.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * Authors: George Lebl <jirka@5z.com>,
 *          Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_MULTISCREEN_H__
#define __PANEL_MULTISCREEN_H__

#include <gtk/gtk.h>

void	panel_multiscreen_init                  (void);
void	panel_multiscreen_reinit                (void);

int	panel_multiscreen_screens               (void);
int	panel_multiscreen_monitors              (GdkScreen *screen);

int	panel_multiscreen_x                     (GdkScreen *screen,
						 int        monitor);
int	panel_multiscreen_y                     (GdkScreen *screen,
						 int        monitor);
int	panel_multiscreen_width                 (GdkScreen *screen,
						 int        monitor);
int	panel_multiscreen_height                (GdkScreen *screen,
						 int        monitor);
int	panel_multiscreen_locate_widget_monitor (GtkWidget *widget);
int     panel_multiscreen_get_monitor_at_point  (GdkScreen *screen,
						 int        x,
						 int        y);
void    panel_multiscreen_is_at_visible_extreme (GdkScreen *screen,
						 int        monitor,
						 gboolean  *leftmost,
						 gboolean  *rightmost,
						 gboolean  *topmost,
						 gboolean  *bottommost);
char  **panel_make_environment_for_screen       (GdkScreen *screen,
						 char     **envp);

#endif /* __PANEL_MULTISCREEN_H__ */
