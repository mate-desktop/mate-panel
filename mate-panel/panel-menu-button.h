/*
 * panel-menu-button.h: panel menu button
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
 *      Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_MENU_BUTTON_H__
#define __PANEL_MENU_BUTTON_H__

#include "button-widget.h"
#include "panel-widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_MENU_BUTTON         (panel_menu_button_get_type ())
#define PANEL_MENU_BUTTON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_MENU_BUTTON, PanelMenuButton))
#define PANEL_MENU_BUTTON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_MENU_BUTTON, PanelMenuButtonClass))
#define PANEL_IS_MENU_BUTTON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_MENU_BUTTON))
#define PANEL_IS_MENU_BUTTON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_MENU_BUTTON))
#define PANEL_MENU_BUTTON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_MENU_BUTTON, PanelMenuButtonClass))

typedef struct _PanelMenuButton        PanelMenuButton;
typedef struct _PanelMenuButtonClass   PanelMenuButtonClass;
typedef struct _PanelMenuButtonPrivate PanelMenuButtonPrivate;

struct _PanelMenuButton {
	ButtonWidget            button;

	PanelMenuButtonPrivate *priv;
};

struct _PanelMenuButtonClass {
	ButtonWidgetClass       button_class;
};

GType      panel_menu_button_get_type            (void) G_GNUC_CONST;

gboolean   panel_menu_button_create              (PanelToplevel    *toplevel,
						  int               position,
						  const char       *filename,
						  const char       *menu_path,
						  gboolean          use_menu_path,
						  const char       *tooltip);

void       panel_menu_button_set_menu_path       (PanelMenuButton  *button,
						  const char       *menu_path);
void       panel_menu_button_set_custom_icon     (PanelMenuButton  *button,
						  const char       *custom_icon);
void       panel_menu_button_set_tooltip         (PanelMenuButton *button,
						  const char      *tooltip);
void       panel_menu_button_set_use_menu_path   (PanelMenuButton  *button,
						  gboolean          use_menu_path);
gboolean   panel_menu_button_get_use_menu_path   (PanelMenuButton  *button);
void       panel_menu_button_set_use_custom_icon (PanelMenuButton  *button,
						  gboolean          use_custom_icon);
void       panel_menu_button_set_has_arrow       (PanelMenuButton *button,
				           gboolean         has_arrow);

void       panel_menu_button_load_from_gsettings     (PanelWidget      *panel,
						  gboolean          locked,
						  int               position,
						  gboolean          exactpos,
						  const char       *id);

void       panel_menu_button_invoke_menu         (PanelMenuButton  *button,
						  const char       *callback_name);

void       panel_menu_button_popup_menu          (PanelMenuButton  *button,
						  guint             n_button,
						  guint32           activate_time);

void       panel_menu_button_set_dnd_enabled     (PanelMenuButton  *button,
						  gboolean          dnd_enabled);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_MENU_BUTTON_H__ */
