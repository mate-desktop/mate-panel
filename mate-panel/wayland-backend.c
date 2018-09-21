#include <config.h>

#ifdef HAVE_WAYLAND

#include "wayland-backend.h"

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct zwlr_layer_shell_v1 *layer_shell_global = NULL;

static void wl_regitsty_handle_global(void *data,
									  struct wl_registry *registry,
									  uint32_t id,
									  const char *interface,
									  uint32_t version)
{
	// pull out the layer shell global
	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell_global = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1);
	}
}

static void wl_regitsty_handle_global_remove(void *data,
											 struct wl_registry *registry,
											 uint32_t id) {
	// who cares
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = wl_regitsty_handle_global,
	.global_remove = wl_regitsty_handle_global_remove,
};

static void layer_surface_handle_configure (void *data,
											struct zwlr_layer_surface_v1 *surface,
											uint32_t serial,
											uint32_t w,
											uint32_t h) {
	//width = w;
	//height = h;
	// TODO: resize the GTK window
    //gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	zwlr_layer_surface_v1_ack_configure (surface, serial);
}

static void layer_surface_handle_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	// TODO: close the GTK window
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_handle_configure,
	.closed = layer_surface_handle_closed,
};

void wayland_registry_init()
{
	GdkDisplay *gdk_display = gdk_display_get_default();
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display)) {
		struct wl_display *wl_display = gdk_wayland_display_get_wl_display (gdk_display);
		struct wl_registry *wl_registry = wl_display_get_registry (wl_display);
		wl_registry_add_listener (wl_registry, &wl_registry_listener, NULL);
		wl_display_roundtrip (wl_display);
	}
}

struct wl_output *get_primary_wl_output (GdkDisplay *gdk_display)
{
	GdkMonitor *gdk_monitor = gdk_display_get_primary_monitor (gdk_display);
	if (gdk_monitor == NULL && gdk_display_get_n_monitors (gdk_display) > 0) {
		gdk_monitor = gdk_display_get_monitor (gdk_display, 0);
	}
	if (gdk_monitor)
		return gdk_wayland_monitor_get_wl_output (gdk_monitor);
	else
		return NULL;
}

void wayland_realize_panel_toplevel (GtkWidget *widget)
{
	GdkDisplay *gdk_display = gdk_display_get_default();

	if (!GDK_IS_WAYLAND_DISPLAY(gdk_display))
		return;

	if (!layer_shell_global) {
		g_warning("Layer shell protocol not supported");
		return;
	}

	GdkWindow *window = gtk_widget_get_window (widget);

	gdk_wayland_window_set_use_custom_surface (window);
	struct wl_surface *wl_surface = gdk_wayland_window_get_wl_surface (window);
	g_assert (wl_surface);

	//struct wl_output *wl_output = NULL;
	uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
	char *namespace = "mate"; // not sure what this is for

	struct zwlr_layer_surface_v1 *layer_surface = zwlr_layer_shell_v1_get_layer_surface (layer_shell_global,
																						 wl_surface,
																						 NULL,
																						 layer,
																						 namespace);
	g_assert (layer_surface);
	// GdkRectangle rect;
	// gdk_monitor_get_geometry(gdk_monitor, &rect);
	gint width = 0, height = 0;
	gtk_window_get_size (GTK_WINDOW (widget), &width, &height);
	printf("window size is %d, %d\n", width, height);
	zwlr_layer_surface_v1_set_size (layer_surface, width, height);
	zwlr_layer_surface_v1_set_anchor (layer_surface,
									  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	//zwlr_layer_surface_v1_set_exclusive_zone (layer_surface, exclusive_zone);
	//zwlr_layer_surface_v1_set_margin (layer_surface, margin_top, margin_right, margin_bottom, margin_left);
	zwlr_layer_surface_v1_set_keyboard_interactivity (layer_surface, FALSE);
	zwlr_layer_surface_v1_set_exclusive_zone (layer_surface, 200);
	zwlr_layer_surface_v1_add_listener (layer_surface, &layer_surface_listener, NULL);
	wl_surface_commit (wl_surface);
	struct wl_display *wl_display = gdk_wayland_display_get_wl_display (gdk_display);
	wl_display_roundtrip (wl_display);
}

#endif /* HAVE_WAYLAND */
