#ifndef __XSTUFF_H__
#define __XSTUFF_H__

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <gdk/gdkx.h>
#include <gtk/gtkx.h>

#include "panel-enums-gsettings.h"

gboolean is_using_x11                   (void);

void xstuff_zoom_animate                (GtkWidget        *widget,
					 cairo_surface_t  *surface,
					 PanelOrientation  orientation,
					 GdkRectangle     *opt_src_rect);

gboolean xstuff_is_display_dead         (void);

void xstuff_init                        (void);

#endif /* __XSTUFF_H__ */
