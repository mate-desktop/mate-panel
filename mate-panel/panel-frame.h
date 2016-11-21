/*
 * panel-frame.h: A frame which only draws certain edges.
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

#ifndef __PANEL_FRAME_H__
#define __PANEL_FRAME_H__

#include <gtk/gtk.h>
#include "panel-enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_FRAME         (panel_frame_get_type ())
#define PANEL_FRAME(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_FRAME, PanelFrame))
#define PANEL_FRAME_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_FRAME, PanelFrameClass))
#define PANEL_IS_FRAME(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_FRAME))
#define PANEL_IS_FRAME_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_FRAME))
#define PANEL_FRAME_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_FRAME, PanelFrameClass))

typedef struct _PanelFrame      PanelFrame;
typedef struct _PanelFrameClass PanelFrameClass;

struct _PanelFrame {
	GtkBin         bin_instance;

	PanelFrameEdge edges;
};

struct _PanelFrameClass {
	GtkBinClass         bin_class;
};

GType           panel_frame_get_type  (void) G_GNUC_CONST;
GtkWidget      *panel_frame_new       (PanelFrameEdge  edges);

void            panel_frame_set_edges (PanelFrame     *toplevel,
				       PanelFrameEdge  edges);
PanelFrameEdge  panel_frame_get_edges (PanelFrame     *toplevel);

void            panel_frame_draw      (GtkWidget      *widget,
				       cairo_t        *cr,
				       PanelFrameEdge  edges);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_FRAME_H__ */
