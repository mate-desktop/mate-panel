/*
 * panel-background-monitor.h:
 *
 * Copyright (C) 2001, 2002 Ian McKellar <yakk@yakk.net>
 *                     2002 Sun Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *      Ian McKellar <yakk@yakk.net>
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_BACKGROUND_MONITOR_H__
#define __PANEL_BACKGROUND_MONITOR_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#define PANEL_TYPE_BACKGROUND_MONITOR         (panel_background_monitor_get_type ())
#define PANEL_BACKGROUND_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o),      \
					       PANEL_TYPE_BACKGROUND_MONITOR,        \
					       PanelBackgroundMonitor))
#define PANEL_BACKGROUND_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k),         \
					       PANEL_TYPE_BACKGROUND_MONITOR,        \
					       PanelBackgroundMonitorClass))
#define PANEL_IS_BACKGROUND_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o),      \
					       PANEL_TYPE_BACKGROUND_MONITOR))
#define PANEL_IS_BACKGROUND_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k),         \
					       PANEL_TYPE_BACKGROUND_MONITOR))
#define PANEL_BACKGROUND_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),       \
					       PANEL_TYPE_BACKGROUND_MONITOR,        \
					       PanelBackgroundMonitorClass))

typedef struct _PanelBackgroundMonitorClass PanelBackgroundMonitorClass;
typedef struct _PanelBackgroundMonitor      PanelBackgroundMonitor;

GType                   panel_background_monitor_get_type       (void);
PanelBackgroundMonitor *panel_background_monitor_get_for_screen (GdkScreen *screen);
GdkPixbuf              *panel_background_monitor_get_region     (PanelBackgroundMonitor *monitor,
								 int                     x,
								 int                     y,
								 int                     width,
								 int                     height);

#endif /* __PANEL_BACKGROUND_MONITOR_H__ */
