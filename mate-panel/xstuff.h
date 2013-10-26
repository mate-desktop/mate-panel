#ifndef __XSTUFF_H__
#define __XSTUFF_H__

#include <gdk/gdk.h>
#include <gtk/gtk.h>

void xstuff_delete_property		(GdkWindow *window,
					 const char *name);
gboolean xstuff_is_compliant_wm		(void);

void xstuff_unsetup_desktop_area	(void);
void xstuff_set_pos_size		(GdkWindow *window,
					 int x, int y,
					 int w, int h);
void xstuff_set_wmspec_dock_hints       (GdkWindow *window,
					 gboolean autohide);
void xstuff_set_wmspec_strut		(GdkWindow *window,
					 int left,
					 int right,
					 int top,
					 int bottom);

void xstuff_zoom_animate                (GtkWidget        *widget,
					 GdkPixbuf        *pixbuf,
					 PanelOrientation  orientation,
					 GdkRectangle     *opt_src_rect);

int  xstuff_get_current_workspace       (GdkScreen *screen);

void xstuff_grab_key_on_all_screens     (int      keycode,
					 guint    modifiers,
					 gboolean grab);

gboolean xstuff_is_display_dead         (void);

void xstuff_init                        (void);

#endif /* __XSTUFF_H__ */
