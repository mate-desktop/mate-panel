#ifndef __XSTUFF_H__
#define __XSTUFF_H__

#include <gdk/gdk.h>
#include <gtk/gtk.h>

void xstuff_zoom_animate                (GtkWidget        *widget,
					 GdkPixbuf        *pixbuf,
					 PanelOrientation  orientation,
					 GdkRectangle     *opt_src_rect);

gboolean xstuff_is_display_dead         (void);

void xstuff_init                        (void);

#endif /* __XSTUFF_H__ */
