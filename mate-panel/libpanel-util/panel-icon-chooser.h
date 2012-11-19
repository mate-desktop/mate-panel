/*
 * panel-icon-chooser.h: An icon chooser widget
 *
 * Copyright (C) 2010 Novell, Inc.
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
 *	Vincent Untz <vuntz@gnome.org>
 */

#ifndef PANEL_ICON_CHOOSER_H
#define PANEL_ICON_CHOOSER_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_ICON_CHOOSER			(panel_icon_chooser_get_type ())
#define PANEL_ICON_CHOOSER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_ICON_CHOOSER, PanelIconChooser))
#define PANEL_ICON_CHOOSER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_ICON_CHOOSER, PanelIconChooserClass))
#define PANEL_IS_ICON_CHOOSER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_ICON_CHOOSER))
#define PANEL_IS_ICON_CHOOSER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_ICON_CHOOSER))
#define PANEL_ICON_CHOOSER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), PANEL_TYPE_ICON_CHOOSER, PanelIconChooserClass))

typedef struct _PanelIconChooser      PanelIconChooser;
typedef struct _PanelIconChooserClass PanelIconChooserClass;

typedef struct _PanelIconChooserPrivate PanelIconChooserPrivate;

struct _PanelIconChooserClass
{
	GtkButtonClass parent_class;

	void (* changed)         (PanelIconChooser *chooser,
				  const char       *icon);
};

struct _PanelIconChooser
{
	GtkButton parent_instance;

	PanelIconChooserPrivate *priv;
};

GType       panel_icon_chooser_get_type (void);

GtkWidget  *panel_icon_chooser_new                    (const char  *icon);

const char *panel_icon_chooser_get_fallback_icon_name (PanelIconChooser *chooser);

void        panel_icon_chooser_set_fallback_icon_name (PanelIconChooser *chooser,
						       const char       *fallback_icon_name);

const char *panel_icon_chooser_get_icon               (PanelIconChooser *chooser);

void        panel_icon_chooser_set_icon               (PanelIconChooser *chooser,
						       const char       *icon);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_ICON_CHOOSER_H */
