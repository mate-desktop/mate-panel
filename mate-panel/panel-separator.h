/*
 * panel-separator.h: panel "Separator" module
 *
 * Copyright (C) 2005 Carlos Garcia Campos <carlosgc@gnome.org>
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
 *      Carlos Garcia Campos <carlosgc@gnome.org>
 */

#ifndef PANEL_SEPARATOR_H
#define PANEL_SEPARATOR_H

#include "applet.h"
#include "panel-widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_SEPARATOR         (panel_separator_get_type ())
#define PANEL_SEPARATOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_SEPARATOR, PanelSeparator))
#define PANEL_SEPARATOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_SEPARATOR, PanelSeparatorClass))
#define PANEL_IS_SEPARATOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_SEPARATOR))
#define PANEL_IS_SEPARATOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_SEPARATOR))
#define PANEL_SEPARATOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_SEPARATOR, PanelSeparatorClass))

typedef struct _PanelSeparator        PanelSeparator;
typedef struct _PanelSeparatorClass   PanelSeparatorClass;
typedef struct _PanelSeparatorPrivate PanelSeparatorPrivate;

struct _PanelSeparator {
	GtkEventBox             parent;

	PanelSeparatorPrivate  *priv;
};

struct _PanelSeparatorClass {
	GtkEventBoxClass        parent_class;
};

GType  panel_separator_get_type          (void) G_GNUC_CONST;
void   panel_separator_create            (PanelToplevel    *toplevel,
					  int               position);
void   panel_separator_load_from_gsettings   (PanelWidget      *panel_widget,
					  gboolean          locked,
					  gint              position,
					  const char       *id);
void   panel_separator_set_orientation   (PanelSeparator   *separator,
					  PanelOrientation  orientation);
void   panel_separator_change_background (PanelSeparator   *separator);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_SEPARATOR_H */
