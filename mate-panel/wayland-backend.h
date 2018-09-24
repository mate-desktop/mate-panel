#ifndef __WAYLAND_BACKEND_H__
#define __WAYLAND_BACKEND_H__

#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif

#include <gdk/gdk.h>
#include <gtk/gtk.h>

gboolean is_using_wayland ();

// wayland_* functions must only be called when using Wayland
void wayland_registry_init ();
void wayland_realize_panel_toplevel (GtkWidget *window);

#endif /* __WAYLAND_BACKEND_H__ */
