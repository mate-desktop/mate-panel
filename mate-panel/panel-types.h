/*global type definitions*/
#ifndef PANEL_TYPES_H
#define PANEL_TYPES_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

typedef enum {
	PANEL_SPEED_SLOW,
	PANEL_SPEED_MEDIUM,
	PANEL_SPEED_FAST
} PanelSpeed;

typedef struct {
	GdkColor gdk;
	guint16  alpha;
} PanelColor;

#endif
