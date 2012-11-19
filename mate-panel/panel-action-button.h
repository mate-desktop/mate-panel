/*
 * panel-action-button.h: panel "Action Button" module
 *
 * Copyright (C) 2002 Sun Microsystems, Inc.
 * Copyright (C) 2004 Red Hat, Inc.
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

#ifndef __PANEL_ACTION_BUTTON_H__
#define __PANEL_ACTION_BUTTON_H__

#include <gtk/gtk.h>
#include "button-widget.h"
#include "panel-widget.h"
#include "panel-enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_ACTION_BUTTON         (panel_action_button_get_type ())
#define PANEL_ACTION_BUTTON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_ACTION_BUTTON, PanelActionButton))
#define PANEL_ACTION_BUTTON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_ACTION_BUTTON, PanelActionButtonClass))
#define PANEL_IS_ACTION_BUTTON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_ACTION_BUTTON))
#define PANEL_IS_ACTION_BUTTON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_ACTION_BUTTON))
#define PANEL_ACTION_BUTTON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_ACTION_BUTTON, PanelActionButtonClass))

typedef struct _PanelActionButton        PanelActionButton;
typedef struct _PanelActionButtonClass   PanelActionButtonClass;
typedef struct _PanelActionButtonPrivate PanelActionButtonPrivate;

struct _PanelActionButton{
	ButtonWidget               button;

	PanelActionButtonPrivate  *priv;
};

struct _PanelActionButtonClass {
	ButtonWidgetClass          button_class;
};

GType      panel_action_button_get_type  (void) G_GNUC_CONST;

void       panel_action_button_create           (PanelToplevel         *toplevel,
						 int                    position,
						 PanelActionButtonType  type);

void       panel_action_button_set_type         (PanelActionButton     *button,
						 PanelActionButtonType  type);

void       panel_action_button_load_from_gsettings  (PanelWidget            *panel,
						 gboolean                locked,
						 int                     position,
						 gboolean                exactpos,
						 const char             *id);

void       panel_action_button_invoke_menu      (PanelActionButton      *button,
						 const char             *callback_name);

void       panel_action_button_set_dnd_enabled  (PanelActionButton      *button,
						 gboolean                dnd_enabled);

gboolean   panel_action_button_load_from_drag   (PanelToplevel          *toplevel,
						 int                     position,
						 const char             *drag_string,
						 int                    *old_applet_idx);

gboolean             panel_action_get_is_disabled (PanelActionButtonType type);
GCallback            panel_action_get_invoke      (PanelActionButtonType type);
const char* panel_action_get_icon_name(PanelActionButtonType type);
const char* panel_action_get_text(PanelActionButtonType type);
const char* panel_action_get_tooltip(PanelActionButtonType type);
const char* panel_action_get_drag_id(PanelActionButtonType type);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_ACTION_BUTTON_H__ */
