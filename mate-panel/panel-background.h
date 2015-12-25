/*
 * panel-background.h: panel background rendering
 *
 * Copyright (C) 2002, 2003 Sun Microsystems, Inc.
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

#ifndef __PANEL_BACKGROUND_H__
#define __PANEL_BACKGROUND_H__

#include <glib.h>
#include <gtk/gtk.h>

#include "panel-enums.h"
#include "panel-types.h"
#include "panel-background-monitor.h"

typedef struct _PanelBackground PanelBackground;

typedef void (*PanelBackgroundChangedNotify) 
				(PanelBackground *background,
				 gpointer         user_data);

struct _PanelBackground {
	PanelBackgroundType     type;

	PanelBackgroundChangedNotify notify_changed;
	gpointer                user_data;

#if GTK_CHECK_VERSION (3, 0, 0)
	GdkRGBA                 color;
#else
	PanelColor              color;
#endif
	char                   *image;
	GdkPixbuf              *loaded_image; 

	GtkOrientation          orientation;
	GdkRectangle            region;
	GdkPixbuf              *transformed_image;
#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_pattern_t        *composited_pattern;
#else
	GdkPixbuf              *composited_image;
#endif

	PanelBackgroundMonitor *monitor;
	GdkPixbuf              *desktop;
	gulong                  monitor_signal;

	GdkWindow              *window;
#if GTK_CHECK_VERSION (3, 0, 0)
	cairo_pattern_t        *default_pattern;
	GdkRGBA                 default_color;
#else
	GdkPixmap              *pixmap;
	GdkColormap            *colormap;
	GdkGC                  *gc;
	GdkPixmap              *default_pixmap;
	GdkColor                default_color;
#endif

	guint                   fit_image : 1;
	guint                   stretch_image : 1;
	guint                   rotate_image : 1;

	guint                   has_alpha : 1;

	guint                   loaded : 1;
	guint                   transformed : 1;
	guint                   composited : 1;
	guint                   prepared : 1;
};

void  panel_background_init              (PanelBackground     *background,
					  PanelBackgroundChangedNotify notify_changed,
					  gpointer             user_data);
void  panel_background_free              (PanelBackground     *background);
void  panel_background_set               (PanelBackground     *background,
					  PanelBackgroundType  type,
#if GTK_CHECK_VERSION (3, 0, 0)
					  const GdkRGBA       *color,
#else
					  PanelColor          *color,
#endif
					  const char          *image,
					  gboolean             fit_image,
					  gboolean             stretch_image,
					  gboolean             rotate_image); 
void  panel_background_set_type          (PanelBackground     *background,
					  PanelBackgroundType  type);
#if !GTK_CHECK_VERSION (3, 0, 0)
void  panel_background_set_gdk_color     (PanelBackground     *background,
					  GdkColor            *gdk_color);
#endif
void  panel_background_set_opacity       (PanelBackground     *background,
					  guint16              opacity);
void  panel_background_set_color         (PanelBackground     *background,
#if GTK_CHECK_VERSION (3, 0, 0)
					  const GdkRGBA             *color);
#else
					  PanelColor          *color);
#endif
void  panel_background_set_image         (PanelBackground     *background,
					  const char          *image);
void  panel_background_set_fit           (PanelBackground     *background,
					  gboolean             fit_image);
void  panel_background_set_stretch       (PanelBackground     *background,
					  gboolean             stretch_image);
void  panel_background_set_rotate        (PanelBackground     *background,
					  gboolean             rotate_image);
void  panel_background_set_default_style (PanelBackground     *background,
#if GTK_CHECK_VERSION (3, 0, 0)
					  GdkRGBA             *color,
					  cairo_pattern_t     *pattern);
#else
					  GdkColor            *color,
					  GdkPixmap           *pixmap);
#endif
void  panel_background_realized          (PanelBackground     *background,
					  GdkWindow           *window);
void  panel_background_unrealized        (PanelBackground     *background);
void  panel_background_change_region     (PanelBackground     *background,
					  GtkOrientation       orientation,
					  int                  x,
					  int                  y,
					  int                  width,
					  int                  height);
char *panel_background_make_string       (PanelBackground     *background,
					  int                  x,
					  int                  y);

PanelBackgroundType  panel_background_get_type   (PanelBackground *background);
#if GTK_CHECK_VERSION (3, 0, 0)
const GdkRGBA       *panel_background_get_color  (PanelBackground *background);
#else
const PanelColor    *panel_background_get_color  (PanelBackground *background);
const GdkPixmap     *panel_background_get_pixmap (PanelBackground *background);
#endif

PanelBackgroundType
      panel_background_effective_type    (PanelBackground     *background);

#if !GTK_CHECK_VERSION (3, 0, 0)
void panel_background_change_background_on_widget (PanelBackground *background,
						   GtkWidget       *widget);

#else
void panel_background_apply_css(GtkWidget* widget, PanelBackground *background);
#endif

#endif /* __PANEL_BACKGROUND_H__ */
