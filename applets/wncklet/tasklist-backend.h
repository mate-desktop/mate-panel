/* Wncklet applet tasklist backend interface */

/*
 * Copyright (C) 2019 William Wold
 * Copyright (C) 2026 Aleksey Samoilov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* This header is deliberately free of libwnck, libxfce4windowing and of any
 * HAVE_X11 / HAVE_WAYLAND guard: nothing windowing-system specific belongs
 * here, so that every backend and the core can include it unconditionally.
 */

#ifndef _WNCKLET_APPLET_TASKLIST_BACKEND_H_
#define _WNCKLET_APPLET_TASKLIST_BACKEND_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
	TASKLIST_NEVER_GROUP,
	TASKLIST_AUTO_GROUP,
	TASKLIST_ALWAYS_GROUP
} TasklistGroupingType;

/* Window state as reported by a backend, so that the core does not have to
 * know about XfwWindowState or WnckWindowState. */
typedef enum {
	TASKLIST_STATE_ACTIVE     = 1 << 0,
	TASKLIST_STATE_MAXIMIZED  = 1 << 1,
	TASKLIST_STATE_MINIMIZED  = 1 << 2,
	TASKLIST_STATE_FULLSCREEN = 1 << 3,
	TASKLIST_STATE_URGENT     = 1 << 4
} TasklistWindowState;

/* A backend provides the windowing-system specific half of the tasklist.
 * Screens, windows and applications cross this boundary as opaque handles, so
 * that no windowing library type ever reaches the core.
 *
 * The core owns no signal handlers for backend objects. It asks the backend to
 * start or stop following the backend's own signals, which keeps the number of
 * handler ids the core has to track at zero and lets tasklist_rebuild() pause
 * and resume tracking cheaply.
 *
 * Ownership of handles is entirely the backend's concern. A window or
 * application handle stays valid for as long as the backend reports tracking
 * for it, and the core never unrefs one.
 */
typedef struct _TasklistBackend TasklistBackend;

struct _TasklistBackend {
	/* screens */

	/* The default screen, or NULL if there is none. */
	gpointer  (*get_screen)                (void);
	/* The windows of a screen, as a GList of window handles owned by the
	 * backend. Only windows that should get a task button are reported. */
	GList    *(*list_windows)              (gpointer screen);
	gpointer  (*get_active_window)         (gpointer screen);

	/* windows */

	const gchar *(*get_window_name)        (gpointer window);
	GIcon       *(*get_window_gicon)       (gpointer window);
	GdkPixbuf   *(*get_window_pixbuf)      (gpointer window, gint size, gint scale);
	TasklistWindowState (*get_window_state) (gpointer window);
	gpointer    (*get_window_application)  (gpointer window);
	/* The identifier used in the drag-and-drop payload. On X11 this is the
	 * window XID; on Wayland there is no XID and the handle is used. */
	gulong      (*get_window_id)          (gpointer window);

	/* applications, used as the grouping key */

	const gchar *(*get_app_name)           (gpointer app);
	GIcon       *(*get_app_gicon)          (gpointer app);

	/* Icon shown while no icon is known yet. */
	const gchar *(*get_fallback_icon_name) (void);

	/* actions */

	void      (*activate_window)           (gpointer window, guint32 user_time);
	void      (*close_window)              (gpointer window, guint32 timestamp);
	void      (*set_window_maximized)      (gpointer window, gboolean maximized);
	void      (*set_window_minimized)      (gpointer window, gboolean minimized);

	/* signal tracking. Both tracking calls must use the core widget as the
	 * signal user data, so that stopping is a single
	 * g_signal_handlers_disconnect_by_data() call on the core widget. */
	void      (*set_screen_tracking)       (GtkWidget *core, gpointer screen, gboolean track);
	void      (*set_window_tracking)       (GtkWidget *core, gpointer window, gboolean track);
	void      (*set_app_tracking)          (GtkWidget *core, gpointer app, gboolean track);
};

/* Building and configuring the tasklist. The tasklist is a plain widget, so
 * that the applet shell does not need to know how it is implemented. */
GtkWidget *tasklist_core_new                    (const TasklistBackend *backend);
void       tasklist_core_set_orientation        (GtkWidget *core, GtkOrientation orient);
void       tasklist_core_set_middle_click_close (GtkWidget *core, gboolean enabled);
void       tasklist_core_set_scroll_enabled     (GtkWidget *core, gboolean enabled);
void       tasklist_core_set_grouping           (GtkWidget *core, TasklistGroupingType grouping);

/* Callbacks a backend makes when its own signals fire. */
void tasklist_core_window_added               (GtkWidget *core, gpointer window);
void tasklist_core_window_removed             (GtkWidget *core, gpointer window);
void tasklist_core_window_state_changed       (GtkWidget *core, gpointer window);
void tasklist_core_window_name_changed        (GtkWidget *core, gpointer window);
void tasklist_core_window_icon_changed        (GtkWidget *core, gpointer window);
void tasklist_core_window_application_changed (GtkWidget *core, gpointer window);
void tasklist_core_app_icon_changed           (GtkWidget *core, gpointer app);
void tasklist_core_app_name_changed           (GtkWidget *core, gpointer app);
void tasklist_core_active_window_changed      (GtkWidget *core);

G_END_DECLS

#endif /* _WNCKLET_APPLET_TASKLIST_BACKEND_H_ */
