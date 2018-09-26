#ifndef __XSTUFF_H__
#define __XSTUFF_H__

#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <gdk/gdkx.h>
#include <gtk/gtkx.h>

gboolean is_using_x11                   ();

void xstuff_zoom_animate                (GtkWidget        *widget,
					 cairo_surface_t  *surface,
					 PanelOrientation  orientation,
					 GdkRectangle     *opt_src_rect);

// This will always return false when not using X
gboolean xstuff_is_display_dead         (void);

void xstuff_init                        (void);

#endif /* __XSTUFF_H__ */
