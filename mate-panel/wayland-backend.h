#ifndef __WAYLAND_BACKEND_H__
#define __WAYLAND_BACKEND_H__

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be included when HAVE_WAYLAND is enabled
#endif

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>

#include "panel-toplevel.h"

void wayland_panel_toplevel_init (PanelToplevel* toplevel);
void wayland_panel_toplevel_update_placement (PanelToplevel* toplevel);

#endif /* __WAYLAND_BACKEND_H__ */
