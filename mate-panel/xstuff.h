#ifndef __XSTUFF_H__
#define __XSTUFF_H__

#include <cairo.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "panel-enums-gsettings.h"

gboolean is_using_x11                   (void);

void xstuff_zoom_animate                (GtkWidget        *widget,
					 cairo_surface_t  *surface,
					 PanelOrientation  orientation,
					 GdkRectangle     *opt_src_rect);

gboolean xstuff_is_display_dead         (void);

void xstuff_init                        (void);

#endif /* __XSTUFF_H__ */
