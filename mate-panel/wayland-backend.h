#ifndef __WAYLAND_BACKEND_H__
#define __WAYLAND_BACKEND_H__

#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>

#include "panel.h"

#ifndef __PANEL_TOPLEVEL_H__
struct _PanelToplevel;
typedef struct _PanelToplevel PanelToplevel;
#endif

gboolean is_using_wayland (void);

// wayland_* functions must only be called when using Wayland
void wayland_registry_init (void);
void wayland_realize_panel_toplevel (GtkWidget *window);
void wayland_menu_setup (GtkWidget *menu, PanelData *panel_data);
void wayland_menu_popup (GtkMenu *menu, PanelData *panel_data);

#endif /* __WAYLAND_BACKEND_H__ */
