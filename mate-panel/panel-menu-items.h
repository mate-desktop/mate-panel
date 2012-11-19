/*
 * Copyright (C) 2005 Vincent Untz
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
 *	Vincent Untz <vincent@vuntz.net>
 */


#ifndef __PANEL_MENU_ITEMS_H__
#define __PANEL_MENU_ITEMS_H__

#include <gtk/gtk.h>
#include "panel-widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_PLACE_MENU_ITEM         (panel_place_menu_item_get_type ())
#define PANEL_PLACE_MENU_ITEM(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_PLACE_MENU_ITEM, PanelPlaceMenuItem))
#define PANEL_PLACE_MENU_ITEM_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_PLACE_MENU_ITEM, PanelPlaceMenuItemClass))
#define PANEL_IS_PLACE_MENU_ITEM(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_PLACE_MENU_ITEM))
#define PANEL_IS_PLACE_MENU_ITEM_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_PLACE_MENU_ITEM))
#define PANEL_PLACE_MENU_ITEM_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_PLACE_MENU_ITEM, PanelPlaceMenuItemClass))

typedef struct _PanelPlaceMenuItem        PanelPlaceMenuItem;
typedef struct _PanelPlaceMenuItemClass   PanelPlaceMenuItemClass;
typedef struct _PanelPlaceMenuItemPrivate PanelPlaceMenuItemPrivate;

struct _PanelPlaceMenuItem {
	GtkImageMenuItem            menuitem;

	PanelPlaceMenuItemPrivate  *priv;
};

struct _PanelPlaceMenuItemClass {
	GtkImageMenuItemClass       menuitem_class;
};

GType panel_place_menu_item_get_type (void) G_GNUC_CONST;



#define PANEL_TYPE_DESKTOP_MENU_ITEM         (panel_desktop_menu_item_get_type ())
#define PANEL_DESKTOP_MENU_ITEM(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_DESKTOP_MENU_ITEM, PanelDesktopMenuItem))
#define PANEL_DESKTOP_MENU_ITEM_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_DESKTOP_MENU_ITEM, PanelDesktopMenuItemClass))
#define PANEL_IS_DESKTOP_MENU_ITEM(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_DESKTOP_MENU_ITEM))
#define PANEL_IS_DESKTOP_MENU_ITEM_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_DESKTOP_MENU_ITEM))
#define PANEL_DESKTOP_MENU_ITEM_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_DESKTOP_MENU_ITEM, PanelDesktopMenuItemClass))

typedef struct _PanelDesktopMenuItem        PanelDesktopMenuItem;
typedef struct _PanelDesktopMenuItemClass   PanelDesktopMenuItemClass;
typedef struct _PanelDesktopMenuItemPrivate PanelDesktopMenuItemPrivate;

struct _PanelDesktopMenuItem{
	GtkImageMenuItem            menuitem;

	PanelDesktopMenuItemPrivate  *priv;
};

struct _PanelDesktopMenuItemClass {
	GtkImageMenuItemClass       menuitem_class;
};

GType panel_desktop_menu_item_get_type (void) G_GNUC_CONST;


GtkWidget* panel_place_menu_item_new(gboolean use_image);
GtkWidget* panel_desktop_menu_item_new(gboolean use_image, gboolean append_lock_logout);

void panel_place_menu_item_set_panel   (GtkWidget   *item,
					PanelWidget *panel);
void panel_desktop_menu_item_set_panel (GtkWidget   *item,
					PanelWidget *panel);

void panel_menu_items_append_lock_logout (GtkWidget *menu);
void panel_menu_item_activate_desktop_file (GtkWidget  *menuitem,
					    const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_MENU_ITEMS_H__ */
