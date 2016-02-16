/*
 * panel-stock-icons.h: panel stock icons registration
 *
 * Copyright (C) 2002 Sun Microsystems, Inc.
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

#ifndef __PANEL_STOCK_ICONS_H__
#define __PANEL_STOCK_ICONS_H__

#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* themeable size - "panel-menu" -- This is used for the icons in the menus */
#define PANEL_DEFAULT_MENU_ICON_SIZE		24
/* themeable size - "panel-foobar" -- This is only used for the icon of the
 * Applications item in the menu bar */
#define PANEL_DEFAULT_MENU_BAR_ICON_SIZE	22

#define PANEL_ADD_TO_DEFAULT_ICON_SIZE		32

/* stock icons */
#define PANEL_STOCK_FORCE_QUIT          "mate-panel-force-quit"

/* stock items  - no point in theme the icons one these,
 * they use stock gtk icons and just modify the text
 * for the stock item.
 */
#define PANEL_STOCK_EXECUTE             "panel-execute"
#define PANEL_STOCK_CLEAR               "panel-clear"
#define PANEL_STOCK_DONT_DELETE         "panel-dont-delete"
/* FIXME: put a more representative icon here */
#define PANEL_STOCK_DEFAULT_ICON	"application-default-icon"

void        panel_init_stock_icons_and_items (void);
GtkIconSize panel_menu_icon_get_size         (void);
GtkIconSize panel_menu_bar_icon_get_size     (void);
GtkIconSize panel_add_to_icon_get_size       (void);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_STOCK_ICONS_H__ */
