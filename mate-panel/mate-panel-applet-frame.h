/*
 * mate-panel-applet-frame.h: panel side container for applets
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __MATE_PANEL_APPLET_FRAME_H__
#define __MATE_PANEL_APPLET_FRAME_H__

#include <gtk/gtk.h>

#include "panel-widget.h"
#include "applet.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_APPLET_FRAME         (mate_panel_applet_frame_get_type ())
#define MATE_PANEL_APPLET_FRAME(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_APPLET_FRAME, MatePanelAppletFrame))
#define MATE_PANEL_APPLET_FRAME_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_APPLET_FRAME, MatePanelAppletFrameClass))
#define PANEL_IS_APPLET_FRAME(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_APPLET_FRAME))
#define PANEL_IS_APPLET_FRAME_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_APPLET_FRAME))
#define MATE_PANEL_APPLET_FRAME_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_APPLET_FRAME, MatePanelAppletFrameClass))

typedef struct _MatePanelAppletFrame        MatePanelAppletFrame;
typedef struct _MatePanelAppletFrameClass   MatePanelAppletFrameClass;
typedef struct _MatePanelAppletFramePrivate MatePanelAppletFramePrivate;

struct _MatePanelAppletFrameClass {
        GtkEventBoxClass parent_class;

	void     (*init_properties)       (MatePanelAppletFrame    *frame);

	void     (*sync_menu_state)       (MatePanelAppletFrame    *frame,
					   gboolean             movable,
					   gboolean             removable,
					   gboolean             lockable,
					   gboolean             locked,
					   gboolean             locked_down);

	void     (*popup_menu)            (MatePanelAppletFrame    *frame,
					   guint                button,
					   guint32              timestamp);

	void     (*change_orientation)    (MatePanelAppletFrame    *frame,
					   PanelOrientation     orientation);

	void     (*change_size)           (MatePanelAppletFrame    *frame,
					   guint                size);

	void     (*change_background)     (MatePanelAppletFrame    *frame,
					   PanelBackgroundType  type);
};

struct _MatePanelAppletFrame {
	GtkEventBox parent;

        MatePanelAppletFramePrivate  *priv;
};

GType mate_panel_applet_frame_get_type           (void) G_GNUC_CONST;

void  mate_panel_applet_frame_create             (PanelToplevel       *toplevel,
					     int                  position,
					     const char          *iid);

void  mate_panel_applet_frame_load_from_mateconf    (PanelWidget         *panel_widget,
					     gboolean             locked,
					     int                  position,
					     const char          *id);

void  mate_panel_applet_frame_sync_menu_state    (MatePanelAppletFrame    *frame);

void  mate_panel_applet_frame_change_orientation (MatePanelAppletFrame    *frame,
					     PanelOrientation     orientation);

void  mate_panel_applet_frame_change_size        (MatePanelAppletFrame    *frame,
					     guint                size);

void  mate_panel_applet_frame_change_background  (MatePanelAppletFrame    *frame,
					     PanelBackgroundType  type);

void  mate_panel_applet_frame_set_panel          (MatePanelAppletFrame    *frame,
					     PanelWidget         *panel);


/* For module implementations only */

typedef struct _MatePanelAppletFrameActivating        MatePanelAppletFrameActivating;

PanelOrientation  mate_panel_applet_frame_activating_get_orientation (MatePanelAppletFrameActivating *frame_act);
guint32           mate_panel_applet_frame_activating_get_size        (MatePanelAppletFrameActivating *frame_act);
gboolean          mate_panel_applet_frame_activating_get_locked      (MatePanelAppletFrameActivating *frame_act);
gboolean          mate_panel_applet_frame_activating_get_locked_down (MatePanelAppletFrameActivating *frame_act);
gchar            *mate_panel_applet_frame_activating_get_conf_path   (MatePanelAppletFrameActivating *frame_act);

void  _mate_panel_applet_frame_set_iid               (MatePanelAppletFrame           *frame,
						 const gchar                *iid);

void  _mate_panel_applet_frame_activated             (MatePanelAppletFrame           *frame,
						 MatePanelAppletFrameActivating *frame_act,
						 GError                     *error);

void  _mate_panel_applet_frame_update_flags          (MatePanelAppletFrame *frame,
						 gboolean          major,
						 gboolean          minor,
						 gboolean          has_handle);

void  _mate_panel_applet_frame_update_size_hints     (MatePanelAppletFrame *frame,
						 gint             *size_hints,
						 guint             n_elements);

char *_mate_panel_applet_frame_get_background_string (MatePanelAppletFrame    *frame,
						 PanelWidget         *panel,
						 PanelBackgroundType  type);

void  _mate_panel_applet_frame_applet_broken         (MatePanelAppletFrame *frame);

void  _mate_panel_applet_frame_applet_remove         (MatePanelAppletFrame *frame);
void  _mate_panel_applet_frame_applet_move           (MatePanelAppletFrame *frame);
void  _mate_panel_applet_frame_applet_lock           (MatePanelAppletFrame *frame,
						 gboolean          locked);
#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLET_FRAME_H__ */
