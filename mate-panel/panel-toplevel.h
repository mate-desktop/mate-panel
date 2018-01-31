/*
 * panel-toplevel.h: The panel's toplevel window object.
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

 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_TOPLEVEL_H__
#define __PANEL_TOPLEVEL_H__

#include <gtk/gtk.h>

#include "panel-background.h"
#include "panel-enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* We need PanelWidget type but don't want to include
   the panel-widget.h */
#ifndef TYPEDEF_PANEL_WIDGET
typedef struct _PanelWidget		PanelWidget;
#define TYPEDEF_PANEL_WIDGET
#endif /* TYPEDEF_PANEL_WIDGET */

#define PANEL_TYPE_TOPLEVEL         (panel_toplevel_get_type ())
#define PANEL_TOPLEVEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_TOPLEVEL, PanelToplevel))
#define PANEL_TOPLEVEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_TOPLEVEL, PanelToplevelClass))
#define PANEL_IS_TOPLEVEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_TOPLEVEL))
#define PANEL_IS_TOPLEVEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_TOPLEVEL))
#define PANEL_TOPLEVEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_TOPLEVEL, PanelToplevelClass))

typedef struct _PanelToplevel        PanelToplevel;
typedef struct _PanelToplevelClass   PanelToplevelClass;
typedef struct _PanelToplevelPrivate PanelToplevelPrivate;

struct _PanelToplevel {
	GtkWindow              window_instance;
	GSettings             *settings;
	GSettings             *queued_settings;
	GSettings             *background_settings;
	PanelBackground        background;
	PanelToplevelPrivate  *priv;
};

struct _PanelToplevelClass {
	GtkWindowClass         window_class;

	/* key bindings */
	gboolean  (*popup_panel_menu) (PanelToplevel *toplevel);
	gboolean  (*toggle_expand)    (PanelToplevel *toplevel);
	gboolean  (*expand)           (PanelToplevel *toplevel);
	gboolean  (*unexpand)         (PanelToplevel *toplevel);
	gboolean  (*toggle_hidden)    (PanelToplevel *toplevel);
	gboolean  (*begin_move)       (PanelToplevel *toplevel);
	gboolean  (*begin_resize)     (PanelToplevel *toplevel);

	/* signals */
	void      (*hiding)           (PanelToplevel *toplevel);
	void      (*unhiding)         (PanelToplevel *toplevel);
};

GType                panel_toplevel_get_type               (void) G_GNUC_CONST;

PanelWidget         *panel_toplevel_get_panel_widget       (PanelToplevel       *toplevel);

void                 panel_toplevel_set_name               (PanelToplevel       *toplevel,
							    const char          *name);
const char* panel_toplevel_get_name(PanelToplevel* toplevel);
void                 panel_toplevel_set_settings_path      (PanelToplevel       *toplevel,
							    const char          *settings_path);
const char* panel_toplevel_get_description(PanelToplevel* toplevel);
void                 panel_toplevel_set_expand             (PanelToplevel       *toplevel,
							    gboolean             expand);
gboolean             panel_toplevel_get_expand             (PanelToplevel       *toplevel);
void                 panel_toplevel_set_orientation        (PanelToplevel       *toplevel,
							    PanelOrientation     orientation);
PanelOrientation     panel_toplevel_get_orientation        (PanelToplevel       *toplevel);
void                 panel_toplevel_set_size               (PanelToplevel       *toplevel,
							    int                  size);
int                  panel_toplevel_get_size               (PanelToplevel       *toplevel);
void                 panel_toplevel_set_monitor            (PanelToplevel       *toplevel,
							    int                  monitor);
int                  panel_toplevel_get_monitor            (PanelToplevel       *toplevel);
void                 panel_toplevel_set_auto_hide_size     (PanelToplevel       *toplevel,
							    int                  autohide_size);
int                  panel_toplevel_get_auto_hide_size     (PanelToplevel       *toplevel);
void                 panel_toplevel_set_x                  (PanelToplevel       *toplevel,
							    int                  x,
							    int                  x_right,
							    gboolean             x_centered);
void                 panel_toplevel_set_y                  (PanelToplevel       *toplevel,
							    int                  y,
							    int                  y_bottom,
							    gboolean             y_centered);
void                 panel_toplevel_get_position           (PanelToplevel       *toplevel,
							    int                 *x,
							    int                 *x_right,
							    int                 *y,
							    int                 *y_bottom);
gboolean             panel_toplevel_get_x_centered         (PanelToplevel       *toplevel);
gboolean             panel_toplevel_get_y_centered         (PanelToplevel       *toplevel);

void                 panel_toplevel_rotate                 (PanelToplevel       *toplevel,
							    gboolean             clockwise);

void                 panel_toplevel_attach_to_widget       (PanelToplevel       *toplevel,
							    PanelToplevel       *attach_toplevel,
							    GtkWidget           *attach_widget);
void                 panel_toplevel_detach                 (PanelToplevel       *toplevel);
gboolean             panel_toplevel_get_is_attached        (PanelToplevel       *toplevel);
PanelToplevel       *panel_toplevel_get_attach_toplevel    (PanelToplevel       *toplevel);
GtkWidget           *panel_toplevel_get_attach_widget      (PanelToplevel       *toplevel);

gboolean             panel_toplevel_get_is_floating	   (PanelToplevel       *toplevel);

gboolean             panel_toplevel_get_is_hidden          (PanelToplevel       *toplevel);
PanelState           panel_toplevel_get_state              (PanelToplevel       *toplevel);

void                 panel_toplevel_hide                   (PanelToplevel       *toplevel,
							    gboolean             auto_hide,
							    GtkDirectionType     direction);
void                 panel_toplevel_unhide                 (PanelToplevel       *toplevel);
void                 panel_toplevel_queue_auto_hide        (PanelToplevel       *toplevel);
void                 panel_toplevel_queue_auto_unhide      (PanelToplevel       *toplevel);
void                 panel_toplevel_queue_initial_unhide   (PanelToplevel       *toplevel);
void                 panel_toplevel_push_autohide_disabler (PanelToplevel       *toplevel);
void                 panel_toplevel_pop_autohide_disabler  (PanelToplevel       *toplevel);

void                 panel_toplevel_set_auto_hide          (PanelToplevel       *toplevel,
							    gboolean             autohide);
gboolean             panel_toplevel_get_auto_hide          (PanelToplevel       *toplevel);
void                 panel_toplevel_set_hide_delay         (PanelToplevel       *toplevel,
							    int                  hide_delay);
int                  panel_toplevel_get_hide_delay         (PanelToplevel       *toplevel);
void                 panel_toplevel_set_unhide_delay       (PanelToplevel       *toplevel,
							    int                  unhide_delay);
int                  panel_toplevel_get_unhide_delay       (PanelToplevel       *toplevel);

void                 panel_toplevel_set_animate            (PanelToplevel       *toplevel,
							    gboolean             animate);
gboolean             panel_toplevel_get_animate            (PanelToplevel       *toplevel);
void                 panel_toplevel_set_animation_speed    (PanelToplevel       *toplevel,
							    PanelAnimationSpeed  animation_speed);
PanelAnimationSpeed  panel_toplevel_get_animation_speed    (PanelToplevel       *toplevel);

void                 panel_toplevel_set_enable_buttons     (PanelToplevel       *toplevel,
							    gboolean             enable_buttons);
gboolean             panel_toplevel_get_enable_buttons     (PanelToplevel       *toplevel);
void                 panel_toplevel_set_enable_arrows      (PanelToplevel       *toplevel,
							    gboolean             enable_arrows);
gboolean             panel_toplevel_get_enable_arrows      (PanelToplevel       *toplevel);
void                 panel_toplevel_update_edges           (PanelToplevel       *toplevel);

gboolean             panel_toplevel_is_last_unattached     (PanelToplevel       *toplevel);
int                  panel_toplevel_get_minimum_size       (PanelToplevel *toplevel);
int                  panel_toplevel_get_maximum_size       (PanelToplevel *toplevel);
GSList              *panel_toplevel_list_toplevels         (void);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_TOPLEVEL_H__ */
