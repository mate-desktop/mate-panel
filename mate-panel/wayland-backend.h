#ifndef __WAYLAND_BACKEND_H__
#define __WAYLAND_BACKEND_H__

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>

#include "panel-toplevel.h"

gboolean is_using_wayland (void);

// wayland_* functions must only be called when using Wayland
void wayland_registry_init (void);
void wayland_realize_panel_toplevel (GtkWidget *window);
void wayland_set_strut (GdkWindow        *gdk_window,
			PanelOrientation  orientation,
			guint32           strut,
			guint32           strut_start,
			guint32           strut_end);
void wayland_popup_menu_setup (GtkWidget *menu, GtkWidget *attach_widget);
void wayland_tooltip_setup (GtkWidget  *widget,
			    gint        x,
			    gint        y,
			    gboolean    keyboard_tip,
			    GtkTooltip *tooltip,
			    void       *_data);

#endif /* __WAYLAND_BACKEND_H__ */
