/*
 * panel-xdg-output.c: connector names and descriptions of Wayland monitors.
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <config.h>

#include <string.h>

#include <gdk/gdkwayland.h>

#include "xdg-output-unstable-v1-client-protocol.h"

#include "panel-xdg-output.h"

#define XDG_OUTPUT_DATA "panel-xdg-output"

typedef struct {
	struct zxdg_output_v1 *proxy;
	char                  *name;
	char                  *description;
} XdgOutput;

static struct wl_display               *display = NULL;
static struct wl_event_queue           *queue = NULL;
static struct zxdg_output_manager_v1   *manager = NULL;
static gboolean                         tried = FALSE;

static void
registry_global (void               *data,
		 struct wl_registry *registry,
		 uint32_t            name,
		 const char         *interface,
		 uint32_t            version)
{
	if (manager == NULL && version >= 2 &&
	    strcmp (interface, zxdg_output_manager_v1_interface.name) == 0)
		manager = wl_registry_bind (registry, name,
					    &zxdg_output_manager_v1_interface,
					    MIN (version, 3));
}

static void
registry_global_remove (void               *data,
			struct wl_registry *registry,
			uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static gboolean
ensure_manager (void)
{
	struct wl_display  *wrapper;
	struct wl_registry *registry;
	GdkDisplay         *gdk_display = gdk_display_get_default ();

	if (tried)
		return manager != NULL;
	tried = TRUE;

	if (!GDK_IS_WAYLAND_DISPLAY (gdk_display))
		return FALSE;

	display = gdk_wayland_display_get_wl_display (gdk_display);
	queue = wl_display_create_queue (display);
	wrapper = wl_proxy_create_wrapper (display);
	wl_proxy_set_queue ((struct wl_proxy *) wrapper, queue);
	registry = wl_display_get_registry (wrapper);
	wl_proxy_wrapper_destroy (wrapper);
	wl_registry_add_listener (registry, &registry_listener, NULL);
	wl_display_roundtrip_queue (display, queue);
	wl_registry_destroy (registry);

	return manager != NULL;
}

static void
xdg_output_logical_position (void                  *data,
			     struct zxdg_output_v1 *proxy,
			     int32_t                x,
			     int32_t                y)
{
}

static void
xdg_output_logical_size (void                  *data,
			 struct zxdg_output_v1 *proxy,
			 int32_t                width,
			 int32_t                height)
{
}

static void
xdg_output_done (void                  *data,
		 struct zxdg_output_v1 *proxy)
{
}

static void
xdg_output_name (void                  *data,
		 struct zxdg_output_v1 *proxy,
		 const char            *name)
{
	XdgOutput *output = data;

	g_free (output->name);
	output->name = g_strdup (name);
}

static void
xdg_output_description (void                  *data,
			struct zxdg_output_v1 *proxy,
			const char            *description)
{
	XdgOutput *output = data;

	g_free (output->description);
	output->description = g_strdup (description);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	xdg_output_logical_position,
	xdg_output_logical_size,
	xdg_output_done,
	xdg_output_name,
	xdg_output_description,
};

static void
xdg_output_free (gpointer data)
{
	XdgOutput *output = data;

	zxdg_output_v1_destroy (output->proxy);
	g_free (output->name);
	g_free (output->description);
	g_free (output);
}

static XdgOutput *
xdg_output_for (GdkMonitor *monitor)
{
	XdgOutput        *output;
	struct wl_output *wl_output;

	if (!GDK_IS_WAYLAND_MONITOR (monitor) || !ensure_manager ())
		return NULL;

	output = g_object_get_data (G_OBJECT (monitor), XDG_OUTPUT_DATA);
	if (output != NULL) {
		wl_display_dispatch_queue_pending (display, queue);
		return output;
	}

	wl_output = gdk_wayland_monitor_get_wl_output (monitor);
	if (wl_output == NULL)
		return NULL;

	output = g_new0 (XdgOutput, 1);
	output->proxy = zxdg_output_manager_v1_get_xdg_output (manager, wl_output);
	zxdg_output_v1_add_listener (output->proxy, &xdg_output_listener, output);
	g_object_set_data_full (G_OBJECT (monitor), XDG_OUTPUT_DATA, output, xdg_output_free);
	wl_display_roundtrip_queue (display, queue);

	return output;
}

char *
panel_xdg_output_get_name (GdkMonitor *monitor)
{
	XdgOutput *output = xdg_output_for (monitor);

	return output != NULL ? g_strdup (output->name) : NULL;
}

char *
panel_xdg_output_get_description (GdkMonitor *monitor)
{
	XdgOutput *output = xdg_output_for (monitor);

	return output != NULL ? g_strdup (output->description) : NULL;
}
