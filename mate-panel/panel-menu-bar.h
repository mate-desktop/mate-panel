/*
 * panel-menu-bar.h: panel Applications/Places/Desktop menu bar
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_MENU_BAR_H__
#define __PANEL_MENU_BAR_H__

#include <gtk/gtk.h>
#include "panel-widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_MENU_BAR         (panel_menu_bar_get_type ())
#define PANEL_MENU_BAR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_MENU_BAR, PanelMenuBar))
#define PANEL_MENU_BAR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_MENU_BAR, PanelMenuBarClass))
#define PANEL_IS_MENU_BAR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_MENU_BAR))
#define PANEL_IS_MENU_BAR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_MENU_BAR))
#define PANEL_MENU_BAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_MENU_BAR, PanelMenuBarClass))

typedef struct _PanelMenuBar        PanelMenuBar;
typedef struct _PanelMenuBarClass   PanelMenuBarClass;
typedef struct _PanelMenuBarPrivate PanelMenuBarPrivate;

struct _PanelMenuBar{
	GtkMenuBar            menubar;

	PanelMenuBarPrivate  *priv;
};

struct _PanelMenuBarClass {
	GtkMenuBarClass       menubar_class;
};

GType      panel_menu_bar_get_type  (void) G_GNUC_CONST;

void       panel_menu_bar_create           (PanelToplevel *toplevel,
					    int            position);

void       panel_menu_bar_load_from_gsettings  (PanelWidget  *panel,
					    gboolean      locked,
					    int           position,
					    gboolean      exactpos,
					    const char   *id);

void       panel_menu_bar_invoke_menu      (PanelMenuBar *menubar,
					    const char   *callback_name);

void       panel_menu_bar_popup_menu       (PanelMenuBar *menubar,
					    guint32       activate_time);

void       panel_menu_bar_change_background (PanelMenuBar *menubar);

void             panel_menu_bar_set_orientation (PanelMenuBar     *menubar,
						 PanelOrientation  orientation);
PanelOrientation panel_menu_bar_get_orientation (PanelMenuBar     *menubar);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_MENU_BAR_H__ */
