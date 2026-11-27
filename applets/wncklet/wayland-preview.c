/* Wncklet applet Wayland window previews */

/*
 * Copyright (C) 2026 MATE Developers
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

#define _GNU_SOURCE

#include <config.h>

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <libxfce4windowing/libxfce4windowing.h>
#include <mate-panel-applet.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "wayland-preview.h"

#define SHOW_DELAY_MS 400
#define HIDE_DELAY_MS 250
#define REFRESH_MS 1000
#define ITEM_SPACING 6
#define POPUP_BORDER 6
#define POPUP_GAP 4
#define MIN_THUMBNAIL 32
#define MIN_ASPECT 0.5
#define MAX_ASPECT 1.8

typedef struct
{
	struct ext_foreign_toplevel_handle_v1 *handle;
	gchar *title;
	gchar *app_id;
} ExtToplevel;

typedef struct
{
	struct wl_registry *registry;
	struct ext_foreign_toplevel_list_v1 *list;
	struct ext_foreign_toplevel_image_capture_source_manager_v1 *sources;
	struct ext_image_copy_capture_manager_v1 *copy;
	struct wl_shm *shm;
	GList *toplevels;
	GHashTable *shots;
} Globals;

typedef struct
{
	cairo_surface_t *surface;
	gint width;
	gint height;
} Shot;

typedef struct _Item Item;
typedef struct _Capture Capture;

struct _Capture
{
	WaylandPreview *preview;
	XfwWindow *window;
	struct ext_image_capture_source_v1 *source;
	struct ext_image_copy_capture_session_v1 *session;
	struct ext_image_copy_capture_frame_v1 *frame;
	struct wl_buffer *buffer;
	void *data;
	size_t size;
	guint32 width;
	guint32 height;
	guint32 format;
	gboolean have_format;
};

struct _Item
{
	WaylandPreview *preview;
	XfwWindow *window;
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *label;
	gint width;
	gint height;
};

struct _WaylandPreview
{
	gboolean enabled;
	gint size;
	GtkWidget *popup;
	GtkWidget *anchor;
	GPtrArray *items;
	gboolean horizontal;
	gint room;
	gint chrome;
	GtkWidget *pending_button;
	GList *pending_windows;
	guint show_timeout;
	guint hide_timeout;
	guint refresh_timeout;
	GList *captures;
};

static Globals *globals = NULL;

static gboolean preview_layout (WaylandPreview *preview);
static void preview_rebuild (WaylandPreview *preview);

static void
ext_toplevel_free (ExtToplevel *toplevel)
{
	ext_foreign_toplevel_handle_v1_destroy (toplevel->handle);
	g_free (toplevel->title);
	g_free (toplevel->app_id);
	g_free (toplevel);
}

static void
handle_closed (void *data, struct ext_foreign_toplevel_handle_v1 *handle)
{
	ExtToplevel *toplevel = data;

	globals->toplevels = g_list_remove (globals->toplevels, toplevel);
	ext_toplevel_free (toplevel);
}

static void
handle_done (void *data, struct ext_foreign_toplevel_handle_v1 *handle)
{
}

static void
handle_title (void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *title)
{
	ExtToplevel *toplevel = data;

	g_free (toplevel->title);
	toplevel->title = g_strdup (title);
}

static void
handle_app_id (void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *app_id)
{
	ExtToplevel *toplevel = data;

	g_free (toplevel->app_id);
	toplevel->app_id = g_strdup (app_id);
}

static void
handle_identifier (void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *identifier)
{
}

static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
	handle_closed,
	handle_done,
	handle_title,
	handle_app_id,
	handle_identifier,
};

static void
list_toplevel (void *data, struct ext_foreign_toplevel_list_v1 *list,
	       struct ext_foreign_toplevel_handle_v1 *handle)
{
	ExtToplevel *toplevel = g_new0 (ExtToplevel, 1);

	toplevel->handle = handle;
	ext_foreign_toplevel_handle_v1_add_listener (handle, &handle_listener, toplevel);
	globals->toplevels = g_list_append (globals->toplevels, toplevel);
}

static void
list_finished (void *data, struct ext_foreign_toplevel_list_v1 *list)
{
}

static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
	list_toplevel,
	list_finished,
};

static void
registry_global (void *data, struct wl_registry *registry, uint32_t name,
		 const char *interface, uint32_t version)
{
	if (strcmp (interface, ext_foreign_toplevel_list_v1_interface.name) == 0 && !globals->list)
	{
		globals->list = wl_registry_bind (registry, name, &ext_foreign_toplevel_list_v1_interface, 1);
		ext_foreign_toplevel_list_v1_add_listener (globals->list, &list_listener, NULL);
	}
	else if (strcmp (interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0 &&
		 !globals->sources)
	{
		globals->sources = wl_registry_bind (registry, name,
						     &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
	}
	else if (strcmp (interface, ext_image_copy_capture_manager_v1_interface.name) == 0 && !globals->copy)
	{
		globals->copy = wl_registry_bind (registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
	}
	else if (strcmp (interface, wl_shm_interface.name) == 0 && !globals->shm)
	{
		globals->shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
	}
}

static void
registry_global_remove (void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static void
shot_free (Shot *shot)
{
	cairo_surface_destroy (shot->surface);
	g_free (shot);
}

static void
shot_window_gone (gpointer data, GObject *window)
{
	g_hash_table_remove (globals->shots, window);
}

static Shot *
shot_lookup (XfwWindow *window)
{
	return g_hash_table_lookup (globals->shots, window);
}

static void
shot_store (XfwWindow *window, cairo_surface_t *surface, gint width, gint height)
{
	Shot *shot = g_new0 (Shot, 1);

	shot->surface = cairo_surface_reference (surface);
	shot->width = width;
	shot->height = height;
	if (!g_hash_table_contains (globals->shots, window))
		g_object_weak_ref (G_OBJECT (window), shot_window_gone, NULL);
	g_hash_table_replace (globals->shots, window, shot);
}

static gboolean
globals_init (void)
{
	GdkDisplay *display = gdk_display_get_default ();

	if (globals)
		return TRUE;
	if (!GDK_IS_WAYLAND_DISPLAY (display))
		return FALSE;

	globals = g_new0 (Globals, 1);
	globals->shots = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) shot_free);
	globals->registry = wl_display_get_registry (gdk_wayland_display_get_wl_display (display));
	wl_registry_add_listener (globals->registry, &registry_listener, NULL);
	return TRUE;
}

static gboolean
globals_ready (void)
{
	return globals && globals->list && globals->sources && globals->copy && globals->shm;
}

static const gchar *
window_app_id (XfwWindow *window)
{
	const gchar *const *ids = xfw_window_get_class_ids (window);

	return ids && ids[0] ? ids[0] : "";
}

static gboolean
same_window (XfwWindow *window, const gchar *app_id, const gchar *title)
{
	const gchar *name = xfw_window_get_name (window);

	return g_strcmp0 (window_app_id (window), app_id) == 0 && g_strcmp0 (name ? name : "", title) == 0;
}

static ExtToplevel *
find_toplevel (XfwWindow *window)
{
	const gchar *app_id = window_app_id (window);
	const gchar *title = xfw_window_get_name (window);
	guint rank = 0;
	GList *l;

	if (!title)
		title = "";

	for (l = xfw_screen_get_windows (xfw_window_get_screen (window)); l != NULL; l = l->next)
	{
		if (l->data == window)
			break;
		if (same_window (l->data, app_id, title))
			rank++;
	}

	for (l = globals->toplevels; l != NULL; l = l->next)
	{
		ExtToplevel *toplevel = l->data;

		if (g_strcmp0 (toplevel->app_id ? toplevel->app_id : "", app_id) != 0 ||
		    g_strcmp0 (toplevel->title ? toplevel->title : "", title) != 0)
			continue;
		if (rank == 0)
			return toplevel;
		rank--;
	}
	return NULL;
}

static void
capture_free (Capture *capture)
{
	if (capture->preview)
		capture->preview->captures = g_list_remove (capture->preview->captures, capture);
	g_object_unref (capture->window);
	if (capture->frame)
		ext_image_copy_capture_frame_v1_destroy (capture->frame);
	if (capture->session)
		ext_image_copy_capture_session_v1_destroy (capture->session);
	if (capture->source)
		ext_image_capture_source_v1_destroy (capture->source);
	if (capture->buffer)
		wl_buffer_destroy (capture->buffer);
	if (capture->data)
		munmap (capture->data, capture->size);
	g_free (capture);
}

static void
item_show_shot (Item *item, Shot *shot)
{
	cairo_surface_t *captured = shot->surface;
	gint width = shot->width;
	gint height = shot->height;
	gint scale = gtk_widget_get_scale_factor (item->image);
	double fit = MIN ((double) item->width / width, (double) item->height / height);
	cairo_surface_t *thumbnail;
	cairo_t *cr;

	thumbnail = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, item->width * scale, item->height * scale);
	cairo_surface_set_device_scale (thumbnail, scale, scale);
	cr = cairo_create (thumbnail);
	cairo_translate (cr, (item->width - width * fit) / 2.0, (item->height - height * fit) / 2.0);
	cairo_scale (cr, fit, fit);
	cairo_set_source_surface (cr, captured, 0, 0);
	cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
	cairo_paint (cr);
	cairo_destroy (cr);

	gtk_image_set_from_surface (GTK_IMAGE (item->image), thumbnail);
	cairo_surface_destroy (thumbnail);
}

static void
frame_transform (void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t transform)
{
}

static void
frame_damage (void *data, struct ext_image_copy_capture_frame_v1 *frame,
	      int32_t x, int32_t y, int32_t width, int32_t height)
{
}

static void
frame_presentation_time (void *data, struct ext_image_copy_capture_frame_v1 *frame,
			 uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
}

static void
frame_ready (void *data, struct ext_image_copy_capture_frame_v1 *frame)
{
	Capture *capture = data;

	cairo_format_t format = capture->format == WL_SHM_FORMAT_ARGB8888 ? CAIRO_FORMAT_ARGB32
									 : CAIRO_FORMAT_RGB24;
	cairo_surface_t *captured;

	captured = cairo_image_surface_create (format, capture->width, capture->height);
	cairo_surface_flush (captured);
	for (guint32 row = 0; row < capture->height; row++)
		memcpy (cairo_image_surface_get_data (captured) + row * cairo_image_surface_get_stride (captured),
			(guint8 *) capture->data + row * capture->width * 4, capture->width * 4);
	cairo_surface_mark_dirty (captured);
	shot_store (capture->window, captured, capture->width, capture->height);
	cairo_surface_destroy (captured);

	if (capture->preview && capture->preview->items)
	{
		WaylandPreview *preview = capture->preview;
		Shot *shot = shot_lookup (capture->window);
		guint i;

		if (preview_layout (preview) && !preview->hide_timeout)
		{
			capture_free (capture);
			preview_rebuild (preview);
			return;
		}
		for (i = 0; i < preview->items->len; i++)
		{
			Item *item = g_ptr_array_index (preview->items, i);

			if (item->window == capture->window)
				item_show_shot (item, shot);
		}
	}
	capture_free (capture);
}

static void
frame_failed (void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t reason)
{
	capture_free (data);
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	frame_transform,
	frame_damage,
	frame_presentation_time,
	frame_ready,
	frame_failed,
};

static gboolean
capture_make_buffer (Capture *capture)
{
	struct wl_shm_pool *pool;
	int stride = capture->width * 4;
	int fd;

	capture->size = (size_t) stride * capture->height;
	fd = memfd_create ("mate-panel-window-preview", MFD_CLOEXEC);
	if (fd < 0)
		return FALSE;
	if (ftruncate (fd, capture->size) < 0)
	{
		close (fd);
		return FALSE;
	}
	capture->data = mmap (NULL, capture->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (capture->data == MAP_FAILED)
	{
		capture->data = NULL;
		close (fd);
		return FALSE;
	}
	pool = wl_shm_create_pool (globals->shm, fd, capture->size);
	capture->buffer = wl_shm_pool_create_buffer (pool, 0, capture->width, capture->height, stride,
						     capture->format);
	wl_shm_pool_destroy (pool);
	close (fd);
	return TRUE;
}

static void
session_buffer_size (void *data, struct ext_image_copy_capture_session_v1 *session,
		     uint32_t width, uint32_t height)
{
	Capture *capture = data;

	capture->width = width;
	capture->height = height;
}

static void
session_shm_format (void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t format)
{
	Capture *capture = data;

	if (format == WL_SHM_FORMAT_ARGB8888 ||
	    (format == WL_SHM_FORMAT_XRGB8888 && !capture->have_format))
	{
		capture->format = format;
		capture->have_format = TRUE;
	}
}

static void
session_dmabuf_device (void *data, struct ext_image_copy_capture_session_v1 *session, struct wl_array *device)
{
}

static void
session_dmabuf_format (void *data, struct ext_image_copy_capture_session_v1 *session,
		       uint32_t format, struct wl_array *modifiers)
{
}

static void
session_done (void *data, struct ext_image_copy_capture_session_v1 *session)
{
	Capture *capture = data;

	if (capture->frame)
		return;
	if (!capture->have_format || capture->width == 0 || capture->height == 0 ||
	    !capture_make_buffer (capture))
	{
		capture_free (capture);
		return;
	}
	capture->frame = ext_image_copy_capture_session_v1_create_frame (capture->session);
	ext_image_copy_capture_frame_v1_add_listener (capture->frame, &frame_listener, capture);
	ext_image_copy_capture_frame_v1_attach_buffer (capture->frame, capture->buffer);
	ext_image_copy_capture_frame_v1_damage_buffer (capture->frame, 0, 0, capture->width, capture->height);
	ext_image_copy_capture_frame_v1_capture (capture->frame);
}

static void
session_stopped (void *data, struct ext_image_copy_capture_session_v1 *session)
{
	Capture *capture = data;

	if (!capture->frame)
		capture_free (capture);
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
	session_buffer_size,
	session_shm_format,
	session_dmabuf_device,
	session_dmabuf_format,
	session_done,
	session_stopped,
};

static void
capture_start (WaylandPreview *preview, XfwWindow *window)
{
	ExtToplevel *toplevel;
	Capture *capture;
	GList *l;

	if (!globals_ready ())
		return;
	for (l = preview->captures; l != NULL; l = l->next)
		if (((Capture *) l->data)->window == window)
			return;
	toplevel = find_toplevel (window);
	if (!toplevel)
		return;

	capture = g_new0 (Capture, 1);
	capture->preview = preview;
	capture->window = g_object_ref (window);
	preview->captures = g_list_prepend (preview->captures, capture);
	capture->source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source (globals->sources,
											     toplevel->handle);
	capture->session = ext_image_copy_capture_manager_v1_create_session (globals->copy, capture->source, 0);
	ext_image_copy_capture_session_v1_add_listener (capture->session, &session_listener, capture);
}

static void
item_free (Item *item)
{
	g_object_unref (item->window);
	g_free (item);
}

static void
preview_cancel_show (WaylandPreview *preview)
{
	if (preview->show_timeout)
	{
		g_source_remove (preview->show_timeout);
		preview->show_timeout = 0;
	}
	if (preview->pending_button)
	{
		g_object_remove_weak_pointer (G_OBJECT (preview->pending_button), (gpointer *) &preview->pending_button);
		preview->pending_button = NULL;
	}
	g_list_free_full (preview->pending_windows, g_object_unref);
	preview->pending_windows = NULL;
}

static void
preview_cancel_hide (WaylandPreview *preview)
{
	if (preview->hide_timeout)
	{
		g_source_remove (preview->hide_timeout);
		preview->hide_timeout = 0;
	}
}

void
wayland_preview_hide (WaylandPreview *preview)
{
	preview_cancel_show (preview);
	preview_cancel_hide (preview);
	if (preview->refresh_timeout)
	{
		g_source_remove (preview->refresh_timeout);
		preview->refresh_timeout = 0;
	}
	if (preview->anchor)
	{
		g_object_remove_weak_pointer (G_OBJECT (preview->anchor), (gpointer *) &preview->anchor);
		preview->anchor = NULL;
	}
	if (preview->items)
	{
		g_ptr_array_free (preview->items, TRUE);
		preview->items = NULL;
	}
	if (preview->popup)
	{
		gtk_widget_destroy (preview->popup);
		preview->popup = NULL;
	}
}

static gboolean
preview_hide_timeout (gpointer data)
{
	WaylandPreview *preview = data;

	preview->hide_timeout = 0;
	wayland_preview_hide (preview);
	return G_SOURCE_REMOVE;
}

static void
preview_schedule_hide (WaylandPreview *preview)
{
	if (!preview->hide_timeout)
		preview->hide_timeout = g_timeout_add (HIDE_DELAY_MS, preview_hide_timeout, preview);
}

static gboolean
preview_refresh (gpointer data)
{
	WaylandPreview *preview = data;
	guint i;

	for (i = 0; preview->items && i < preview->items->len; i++)
		capture_start (preview, ((Item *) g_ptr_array_index (preview->items, i))->window);
	return G_SOURCE_CONTINUE;
}

static gboolean
popup_enter (GtkWidget *widget, GdkEventCrossing *event, WaylandPreview *preview)
{
	preview_cancel_hide (preview);
	return FALSE;
}

static gboolean
popup_leave (GtkWidget *widget, GdkEventCrossing *event, WaylandPreview *preview)
{
	if (event->detail != GDK_NOTIFY_INFERIOR)
		preview_schedule_hide (preview);
	return FALSE;
}

static void
item_clicked (GtkButton *button, Item *item)
{
	WaylandPreview *preview = item->preview;

	xfw_window_activate (item->window, NULL, gtk_get_current_event_time (), NULL);
	wayland_preview_hide (preview);
}

static GtkPositionType
preview_side (GtkWidget *button)
{
	GtkWidget *applet = gtk_widget_get_ancestor (button, PANEL_TYPE_APPLET);

	if (!applet)
		return GTK_POS_TOP;

	switch (mate_panel_applet_get_orient (MATE_PANEL_APPLET (applet)))
	{
		case MATE_PANEL_APPLET_ORIENT_DOWN:
			return GTK_POS_BOTTOM;
		case MATE_PANEL_APPLET_ORIENT_LEFT:
			return GTK_POS_LEFT;
		case MATE_PANEL_APPLET_ORIENT_RIGHT:
			return GTK_POS_RIGHT;
		case MATE_PANEL_APPLET_ORIENT_UP:
		default:
			return GTK_POS_TOP;
	}
}

static double
item_aspect (Item *item)
{
	Shot *shot = shot_lookup (item->window);
	double aspect = shot ? (double) shot->width / shot->height : 4.0 / 3.0;

	return CLAMP (aspect, MIN_ASPECT, MAX_ASPECT);
}

static gint
preview_room (GtkWidget *toplevel, gboolean horizontal)
{
	GdkDisplay *display = gtk_widget_get_display (toplevel);
	GdkWindow *window = gtk_widget_get_window (toplevel);
	GdkMonitor *monitor = window ? gdk_display_get_monitor_at_window (display, window) : NULL;
	GdkRectangle area;

	if (!monitor)
		return G_MAXINT / 2;
	gdk_monitor_get_workarea (monitor, &area);
	return (horizontal ? area.width : area.height) * 9 / 10 - 2 * POPUP_BORDER;
}

static gboolean
preview_layout (WaylandPreview *preview)
{
	gint n = (gint) preview->items->len;
	gint room = preview->room - (n - 1) * ITEM_SPACING - n * preview->chrome;
	double total = 0.0, common;
	gboolean changed = FALSE;
	guint i;

	for (i = 0; i < preview->items->len; i++)
	{
		double aspect = item_aspect (g_ptr_array_index (preview->items, i));

		total += preview->horizontal ? aspect : 1.0 / aspect;
	}
	common = preview->horizontal ? preview->size * 3.0 / 4.0 : preview->size;
	if (total > 0.0 && common * total > room)
		common = MAX (room / total, MIN_THUMBNAIL);

	for (i = 0; i < preview->items->len; i++)
	{
		Item *item = g_ptr_array_index (preview->items, i);
		double aspect = item_aspect (item);
		gint width = MAX ((gint) (preview->horizontal ? common * aspect : common), 1);
		gint height = MAX ((gint) (preview->horizontal ? common : common / aspect), 1);

		if (width == item->width && height == item->height)
			continue;
		changed = changed || item->width != 0;
		item->width = width;
		item->height = height;
		gtk_widget_set_size_request (item->image, width, height);
		gtk_widget_set_size_request (item->label, width, -1);
	}
	return changed;
}

static gint
preview_chrome (WaylandPreview *preview, GtkWidget *box)
{
	gint length, content = 0;
	guint i;

	if (preview->horizontal)
		gtk_widget_get_preferred_width (box, NULL, &length);
	else
		gtk_widget_get_preferred_height (box, NULL, &length);
	for (i = 0; i < preview->items->len; i++)
	{
		Item *item = g_ptr_array_index (preview->items, i);

		content += preview->horizontal ? item->width : item->height;
	}
	content += 2 * POPUP_BORDER + ((gint) preview->items->len - 1) * ITEM_SPACING;
	return MAX ((length - content) / (gint) preview->items->len, 0);
}

static Item *
item_new (WaylandPreview *preview, XfwWindow *window)
{
	Item *item = g_new0 (Item, 1);
	GtkWidget *box, *label;
	GIcon *icon;
	const gchar *name = xfw_window_get_name (window);

	item->preview = preview;
	item->window = g_object_ref (window);

	item->button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (item->button), GTK_RELIEF_NONE);
	gtk_widget_set_valign (item->button, GTK_ALIGN_START);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

	item->image = gtk_image_new ();
	icon = xfw_window_get_gicon (window);
	if (icon)
		gtk_image_set_from_gicon (GTK_IMAGE (item->image), icon, GTK_ICON_SIZE_DIALOG);

	item->label = gtk_label_new (name ? name : "");
	label = item->label;
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 1);

	gtk_box_pack_start (GTK_BOX (box), item->image, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (item->button), box);
	g_signal_connect (item->button, "clicked", G_CALLBACK (item_clicked), item);

	g_ptr_array_add (preview->items, item);
	return item;
}

static void
preview_show (WaylandPreview *preview, GtkWidget *button, GList *windows)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (button);
	GtkPositionType side = preview_side (button);
	gboolean horizontal = side == GTK_POS_TOP || side == GTK_POS_BOTTOM;
	GtkWidget *box;
	GtkAllocation allocation;
	GdkRectangle rect;
	GdkGravity rect_anchor, popup_anchor;
	gint x, y, dx = 0, dy = 0;
	GList *l;
	guint i;

	wayland_preview_hide (preview);
	if (!GTK_IS_WINDOW (toplevel) || !gtk_widget_get_realized (button) || !windows)
		return;

	preview->items = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);
	preview->popup = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_type_hint (GTK_WINDOW (preview->popup), GDK_WINDOW_TYPE_HINT_TOOLTIP);
	gtk_window_set_transient_for (GTK_WINDOW (preview->popup), GTK_WINDOW (toplevel));
	gtk_widget_add_events (preview->popup, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect (preview->popup, "enter-notify-event", G_CALLBACK (popup_enter), preview);
	g_signal_connect (preview->popup, "leave-notify-event", G_CALLBACK (popup_leave), preview);

	box = gtk_box_new (horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, ITEM_SPACING);
	gtk_container_set_border_width (GTK_CONTAINER (box), POPUP_BORDER);
	gtk_container_add (GTK_CONTAINER (preview->popup), box);
	preview->horizontal = horizontal;
	preview->room = preview_room (toplevel, horizontal);
	preview->chrome = 0;
	for (l = windows; l != NULL; l = l->next)
		gtk_box_pack_start (GTK_BOX (box), item_new (preview, l->data)->button, FALSE, FALSE, 0);
	gtk_widget_show_all (box);
	preview_layout (preview);
	preview->chrome = preview_chrome (preview, box);
	preview_layout (preview);
	for (i = 0; i < preview->items->len; i++)
	{
		Item *item = g_ptr_array_index (preview->items, i);
		Shot *shot = shot_lookup (item->window);

		if (shot)
			item_show_shot (item, shot);
		capture_start (preview, item->window);
	}

	gtk_widget_get_allocation (button, &allocation);
	if (!gtk_widget_translate_coordinates (button, toplevel, 0, 0, &x, &y))
		x = y = 0;
	rect.x = x;
	rect.y = y;
	rect.width = allocation.width;
	rect.height = allocation.height;

	switch (side)
	{
		case GTK_POS_BOTTOM:
			rect_anchor = GDK_GRAVITY_SOUTH;
			popup_anchor = GDK_GRAVITY_NORTH;
			dy = POPUP_GAP;
			break;
		case GTK_POS_LEFT:
			rect_anchor = GDK_GRAVITY_WEST;
			popup_anchor = GDK_GRAVITY_EAST;
			dx = -POPUP_GAP;
			break;
		case GTK_POS_RIGHT:
			rect_anchor = GDK_GRAVITY_EAST;
			popup_anchor = GDK_GRAVITY_WEST;
			dx = POPUP_GAP;
			break;
		case GTK_POS_TOP:
		default:
			rect_anchor = GDK_GRAVITY_NORTH;
			popup_anchor = GDK_GRAVITY_SOUTH;
			dy = -POPUP_GAP;
			break;
	}

	gtk_window_resize (GTK_WINDOW (preview->popup), 1, 1);
	gtk_widget_realize (preview->popup);
	gdk_window_move_to_rect (gtk_widget_get_window (preview->popup), &rect, rect_anchor, popup_anchor,
				 GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE, dx, dy);
	gtk_widget_show_all (preview->popup);

	preview->anchor = button;
	g_object_add_weak_pointer (G_OBJECT (button), (gpointer *) &preview->anchor);
	preview->refresh_timeout = g_timeout_add (REFRESH_MS, preview_refresh, preview);
}

static void
preview_rebuild (WaylandPreview *preview)
{
	GtkWidget *button = preview->anchor;
	GList *windows = NULL;
	guint i;

	if (!button)
		return;
	for (i = preview->items->len; i > 0; i--)
		windows = g_list_prepend (windows, g_object_ref (((Item *) g_ptr_array_index (preview->items, i - 1))->window));
	g_object_ref (button);
	preview_show (preview, button, windows);
	g_object_unref (button);
	g_list_free_full (windows, g_object_unref);
}

static gboolean
preview_show_timeout (gpointer data)
{
	WaylandPreview *preview = data;
	GtkWidget *button = preview->pending_button;
	GList *windows = preview->pending_windows;

	preview->show_timeout = 0;
	preview->pending_windows = NULL;
	if (button)
	{
		g_object_remove_weak_pointer (G_OBJECT (button), (gpointer *) &preview->pending_button);
		preview->pending_button = NULL;
		preview_show (preview, button, windows);
	}
	g_list_free_full (windows, g_object_unref);
	return G_SOURCE_REMOVE;
}

void
wayland_preview_hover (WaylandPreview *preview, GtkWidget *button, GList *windows)
{
	GList *copy;

	g_return_if_fail (preview != NULL);

	if (!preview->enabled || !windows || !globals_init ())
		return;

	preview_cancel_hide (preview);
	if (preview->popup && preview->anchor == button)
		return;

	copy = g_list_copy_deep (windows, (GCopyFunc) g_object_ref, NULL);
	for (GList *l = copy; l != NULL; l = l->next)
		capture_start (preview, l->data);
	if (preview->popup)
	{
		preview_show (preview, button, copy);
		g_list_free_full (copy, g_object_unref);
		return;
	}

	preview_cancel_show (preview);
	preview->pending_button = button;
	g_object_add_weak_pointer (G_OBJECT (button), (gpointer *) &preview->pending_button);
	preview->pending_windows = copy;
	preview->show_timeout = g_timeout_add (SHOW_DELAY_MS, preview_show_timeout, preview);
}

void
wayland_preview_leave (WaylandPreview *preview)
{
	g_return_if_fail (preview != NULL);

	preview_cancel_show (preview);
	if (preview->popup)
		preview_schedule_hide (preview);
}

void
wayland_preview_set_enabled (WaylandPreview *preview, gboolean enabled)
{
	g_return_if_fail (preview != NULL);

	preview->enabled = enabled;
	if (!enabled)
		wayland_preview_hide (preview);
	else
		globals_init ();
}

void
wayland_preview_set_size (WaylandPreview *preview, gint size)
{
	g_return_if_fail (preview != NULL);

	preview->size = MAX (size, MIN_THUMBNAIL);
}

WaylandPreview *
wayland_preview_new (void)
{
	WaylandPreview *preview = g_new0 (WaylandPreview, 1);

	preview->size = 200;
	return preview;
}

void
wayland_preview_free (WaylandPreview *preview)
{
	GList *l;

	if (!preview)
		return;
	wayland_preview_hide (preview);
	for (l = preview->captures; l != NULL; l = l->next)
		((Capture *) l->data)->preview = NULL;
	g_list_free (preview->captures);
	g_free (preview);
}
