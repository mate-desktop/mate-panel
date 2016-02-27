#ifndef PANEL_COLOR_H
#define PANEL_COLOR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#if GTK_CHECK_VERSION (3, 0, 0)
void gtk_style_shade (GdkRGBA *a, GdkRGBA *b, gdouble k);
#endif

G_END_DECLS

#endif

