/* Wncklet applet Wayland backend */

/*
 * Copyright (C) 2019 William Wold
 * Copyright (C) 2026 Aleksey Samoilov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* This file only implements the windowing-system half of the tasklist. All of
 * the widget logic lives in tasklist-core.c and is shared with the other
 * backends.
 */

#include <config.h>

#ifndef HAVE_WAYLAND
#error file should only be compiled when HAVE_WAYLAND is enabled
#endif

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <libxfce4windowing/libxfce4windowing.h>

#include "tasklist-backend.h"
#include "wayland-backend.h"

static gpointer
wayland_get_screen (void)
{
	return xfw_screen_get_default ();
}

static GList *
wayland_list_windows (gpointer screen)
{
	return xfw_screen_get_windows (screen);
}

static gpointer
wayland_get_active_window (gpointer screen)
{
	return xfw_screen_get_active_window (screen);
}

static const gchar *
wayland_get_window_name (gpointer window)
{
	return xfw_window_get_name (window);
}

static GIcon *
wayland_get_window_gicon (gpointer window)
{
	return xfw_window_get_gicon (window);
}

static GdkPixbuf *
wayland_get_window_pixbuf (gpointer window, gint size, gint scale)
{
	return xfw_window_get_icon (window, size, scale);
}

static TasklistWindowState
wayland_get_window_state (gpointer window)
{
	XfwWindowState state;
	TasklistWindowState result = 0;

	state = xfw_window_get_state (window);

	if (state & XFW_WINDOW_STATE_ACTIVE)
		result |= TASKLIST_STATE_ACTIVE;
	if (state & XFW_WINDOW_STATE_MAXIMIZED)
		result |= TASKLIST_STATE_MAXIMIZED;
	if (state & XFW_WINDOW_STATE_MINIMIZED)
		result |= TASKLIST_STATE_MINIMIZED;
	if (state & XFW_WINDOW_STATE_FULLSCREEN)
		result |= TASKLIST_STATE_FULLSCREEN;
	if (state & XFW_WINDOW_STATE_URGENT)
		result |= TASKLIST_STATE_URGENT;

	return result;
}

static gpointer
wayland_get_window_application (gpointer window)
{
	return xfw_window_get_application (window);
}

static gulong
wayland_get_window_id (gpointer window)
{
	/* On Wayland there is no window XID, so the handle is used to identify
	 * a window in the drag-and-drop payload */
	return (gulong) window;
}

static const gchar *
wayland_get_app_name (gpointer app)
{
	return xfw_application_get_name (app);
}

static GIcon *
wayland_get_app_gicon (gpointer app)
{
	return xfw_application_get_gicon (app);
}

static const gchar *
wayland_get_fallback_icon_name (void)
{
	return "unknown";
}

static void
wayland_activate_window (gpointer window, guint32 user_time)
{
	xfw_window_activate (window, NULL, user_time, NULL);
}

static void
wayland_close_window (gpointer window, guint32 timestamp)
{
	xfw_window_close (window, timestamp, NULL);
}

static void
wayland_set_window_maximized (gpointer window, gboolean maximized)
{
	xfw_window_set_maximized (window, maximized, NULL);
}

static void
wayland_set_window_minimized (gpointer window, gboolean minimized)
{
	xfw_window_set_minimized (window, minimized, NULL);
}

/* The core widget is passed in as the signal user data, so that the core can
 * stop every handler it owns with a single
 * g_signal_handlers_disconnect_by_data() on itself. */

static void
wayland_screen_window_opened (XfwScreen *screen, XfwWindow *window, gpointer core)
{
	tasklist_core_window_added (GTK_WIDGET (core), window);
}

static void
wayland_screen_window_closed (XfwScreen *screen, XfwWindow *window, gpointer core)
{
	tasklist_core_window_removed (GTK_WIDGET (core), window);
}

static void
wayland_screen_active_window_changed (XfwScreen *screen,
				      XfwWindow *previous_window,
				      gpointer core)
{
	tasklist_core_active_window_changed (GTK_WIDGET (core));
}

static void
wayland_window_state_changed (XfwWindow *window,
			      XfwWindowState changed_mask,
			      XfwWindowState new_state,
			      gpointer core)
{
	tasklist_core_window_state_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_name_changed (XfwWindow *window, gpointer core)
{
	tasklist_core_window_name_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_icon_changed (XfwWindow *window, gpointer core)
{
	tasklist_core_window_icon_changed (GTK_WIDGET (core), window);
}

static void
wayland_window_application_changed (XfwWindow *window,
				   GParamSpec *pspec,
				   gpointer core)
{
	tasklist_core_window_application_changed (GTK_WIDGET (core), window);
}

static void
wayland_app_icon_changed (XfwApplication *app, gpointer core)
{
	tasklist_core_app_icon_changed (GTK_WIDGET (core), app);
}

static void
wayland_app_name_changed (XfwApplication *app, GParamSpec *pspec, gpointer core)
{
	tasklist_core_app_name_changed (GTK_WIDGET (core), app);
}

static void
wayland_set_screen_tracking (GtkWidget *core, gpointer screen, gboolean track)
{
	if (track)
	{
		g_signal_connect (screen, "window-opened",
				  G_CALLBACK (wayland_screen_window_opened), core);
		g_signal_connect (screen, "window-closed",
				  G_CALLBACK (wayland_screen_window_closed), core);
		g_signal_connect (screen, "active-window-changed",
				  G_CALLBACK (wayland_screen_active_window_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (screen, core);
	}
}

static void
wayland_set_window_tracking (GtkWidget *core, gpointer window, gboolean track)
{
	if (track)
	{
		g_signal_connect (window, "state-changed",
				  G_CALLBACK (wayland_window_state_changed), core);
		g_signal_connect (window, "name-changed",
				  G_CALLBACK (wayland_window_name_changed), core);
		g_signal_connect (window, "icon-changed",
				  G_CALLBACK (wayland_window_icon_changed), core);
		g_signal_connect (window, "notify::application",
				  G_CALLBACK (wayland_window_application_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (window, core);
	}
}

static void
wayland_set_app_tracking (GtkWidget *core, gpointer app, gboolean track)
{
	if (track)
	{
		g_signal_connect (app, "icon-changed",
				  G_CALLBACK (wayland_app_icon_changed), core);
		g_signal_connect (app, "notify::name",
				  G_CALLBACK (wayland_app_name_changed), core);
	}
	else
	{
		g_signal_handlers_disconnect_by_data (app, core);
	}
}

static const TasklistBackend wayland_backend = {
	.get_screen               = wayland_get_screen,
	.list_windows             = wayland_list_windows,
	.get_active_window        = wayland_get_active_window,
	.get_window_name          = wayland_get_window_name,
	.get_window_gicon         = wayland_get_window_gicon,
	.get_window_pixbuf        = wayland_get_window_pixbuf,
	.get_window_state         = wayland_get_window_state,
	.get_window_application   = wayland_get_window_application,
	.get_window_id            = wayland_get_window_id,
	.get_app_name             = wayland_get_app_name,
	.get_app_gicon            = wayland_get_app_gicon,
	.get_fallback_icon_name   = wayland_get_fallback_icon_name,
	.activate_window          = wayland_activate_window,
	.close_window             = wayland_close_window,
	.set_window_maximized     = wayland_set_window_maximized,
	.set_window_minimized     = wayland_set_window_minimized,
	.set_screen_tracking      = wayland_set_screen_tracking,
	.set_window_tracking      = wayland_set_window_tracking,
	.set_app_tracking         = wayland_set_app_tracking,
};

const TasklistBackend *
wayland_tasklist_backend (void)
{
	GdkDisplay *display;

	display = gdk_display_get_default ();
	if (display == NULL || !GDK_IS_WAYLAND_DISPLAY (display))
		return NULL;

	xfw_set_client_type (XFW_CLIENT_TYPE_PAGER);

	return &wayland_backend;
}
