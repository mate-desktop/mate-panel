/*
 * mate-panel-applet-frame-matecomponent.h: panel side container for applets
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
 * Copyright (C) 2010 Vincent Untz <vuntz@gnome.org>
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

#ifndef __MATE_PANEL_APPLET_FRAME_MATECOMPONENT_H__
#define __MATE_PANEL_APPLET_FRAME_MATECOMPONENT_H__

#include <mate-panel-applet-frame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_APPLET_FRAME_MATECOMPONENT         (mate_panel_applet_frame_matecomponent_get_type ())
#define MATE_PANEL_APPLET_FRAME_MATECOMPONENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_APPLET_FRAME_MATECOMPONENT, MatePanelAppletFrameMateComponent))
#define MATE_PANEL_APPLET_FRAME_MATECOMPONENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_APPLET_FRAME_MATECOMPONENT, MatePanelAppletFrameMateComponentClass))
#define PANEL_IS_APPLET_FRAME_MATECOMPONENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_APPLET_FRAME_MATECOMPONENT))
#define PANEL_IS_APPLET_FRAME_MATECOMPONENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_APPLET_FRAME_MATECOMPONENT))
#define MATE_PANEL_APPLET_FRAME_MATECOMPONENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_APPLET_FRAME_MATECOMPONENT, MatePanelAppletFrameMateComponentClass))

typedef struct _MatePanelAppletFrameMateComponent        MatePanelAppletFrameMateComponent;
typedef struct _MatePanelAppletFrameMateComponentClass   MatePanelAppletFrameMateComponentClass;
typedef struct _MatePanelAppletFrameMateComponentPrivate MatePanelAppletFrameMateComponentPrivate;

struct _MatePanelAppletFrameMateComponentClass {
        MatePanelAppletFrameClass parent_class;
};

struct _MatePanelAppletFrameMateComponent{
	MatePanelAppletFrame parent;

        MatePanelAppletFrameMateComponentPrivate  *priv;
};

GType     mate_panel_applet_frame_matecomponent_get_type           (void) G_GNUC_CONST;

gboolean  mate_panel_applet_frame_matecomponent_load               (const gchar                 *iid,
							MatePanelAppletFrameActivating  *frame_act);

#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLET_FRAME_MATECOMPONENT_H__ */
