#include <config.h>

#include "panel-volume.h"

#include <glib/gi18n.h>

#include "panel-schemas.h"

#ifdef HAVE_LIBPULSE
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#endif

typedef struct
{
    GSettings *settings;
    GtkWidget *window;
    GtkWidget *label;
    guint hide_timeout;
} PanelVolumeUi;

static PanelVolumeUi volume_ui;

static GSettings *
panel_volume_settings (void)
{
    if (volume_ui.settings == NULL)
        volume_ui.settings = g_settings_new (PANEL_SCHEMA);
    return volume_ui.settings;
}

static gboolean
panel_volume_hide_osd (gpointer unused)
{
	(void) unused;
    volume_ui.hide_timeout = 0;
    if (volume_ui.window != NULL)
        gtk_widget_hide (volume_ui.window);
    return G_SOURCE_REMOVE;
}

static void
panel_volume_create_osd (void)
{
    if (volume_ui.window != NULL)
        return;

    volume_ui.window = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_window_set_decorated (GTK_WINDOW (volume_ui.window), FALSE);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (volume_ui.window), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW (volume_ui.window), TRUE);
    gtk_window_set_type_hint (GTK_WINDOW (volume_ui.window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_keep_above (GTK_WINDOW (volume_ui.window), TRUE);
    volume_ui.label = gtk_label_new (NULL);
    gtk_widget_set_margin_start (volume_ui.label, 18);
    gtk_widget_set_margin_end (volume_ui.label, 18);
    gtk_widget_set_margin_top (volume_ui.label, 10);
    gtk_widget_set_margin_bottom (volume_ui.label, 10);
    gtk_container_add (GTK_CONTAINER (volume_ui.window), volume_ui.label);
}

static void
panel_volume_show_osd (guint percent)
{
    GdkDisplay *display;
    GdkSeat *seat;
    GdkDevice *pointer;
    GdkMonitor *monitor;
    GdkRectangle geometry;
    gint pointer_x, pointer_y;
    gint width, height;
    gchar *markup;

    if (!g_settings_get_boolean (panel_volume_settings (), PANEL_VOLUME_SCROLL_OSD_KEY))
        return;

    panel_volume_create_osd ();
    markup = g_strdup_printf ("<b>%s %u%%</b>", _("Volume"), percent);
    gtk_label_set_markup (GTK_LABEL (volume_ui.label), markup);
    g_free (markup);
    gtk_widget_show_all (volume_ui.window);

    display = gtk_widget_get_display (volume_ui.window);
    seat = gdk_display_get_default_seat (display);
    pointer = gdk_seat_get_pointer (seat);
    gdk_device_get_position (pointer, NULL, &pointer_x, &pointer_y);
    monitor = gdk_display_get_monitor_at_point (display, pointer_x, pointer_y);
    if (monitor == NULL)
        monitor = gdk_display_get_primary_monitor (display);
    gdk_monitor_get_geometry (monitor, &geometry);
    gtk_window_get_size (GTK_WINDOW (volume_ui.window), &width, &height);
    gtk_window_move (GTK_WINDOW (volume_ui.window),
                     geometry.x + (geometry.width - width) / 2,
                     geometry.y + (geometry.height - height) / 2);

    if (volume_ui.hide_timeout != 0)
        g_source_remove (volume_ui.hide_timeout);
    volume_ui.hide_timeout = g_timeout_add (1200, panel_volume_hide_osd, NULL);
}

#ifdef HAVE_LIBPULSE
typedef struct
{
    gint direction;
    guint step;
} VolumeRequest;

typedef struct
{
    pa_glib_mainloop *mainloop;
    pa_context *context;
    gint pending_direction;
    guint pending_step;
} PulseVolume;

static PulseVolume pulse_volume;

static void panel_volume_request_server_info (VolumeRequest *request);

static void
panel_volume_context_state_changed (pa_context *context, void *userdata)
{
	(void) userdata;
    if (pa_context_get_state (context) == PA_CONTEXT_READY &&
        pulse_volume.pending_direction != 0) {
        VolumeRequest *request = g_new0 (VolumeRequest, 1);
        request->direction = pulse_volume.pending_direction;
        request->step = pulse_volume.pending_step;
        pulse_volume.pending_direction = 0;
        pulse_volume.pending_step = 0;
        panel_volume_request_server_info (request);
    }
}

static void
panel_volume_sink_info (pa_context *context, const pa_sink_info *info,
                        int eol, void *userdata)
{
    VolumeRequest *request = userdata;
    pa_cvolume volume;
    pa_volume_t current, next;
    pa_operation *operation;
    gint64 change = (gint64) PA_VOLUME_NORM * request->step / 100;
    gint64 target;

    if (eol != 0) {
		g_free (request);
		return;
	}
	if (info == NULL)
		return;

    volume = info->volume;
    current = pa_cvolume_avg (&volume);
    target = (gint64) current + (request->direction > 0 ? change : -change);
    next = (pa_volume_t) CLAMP (target, 0, (gint64) PA_VOLUME_NORM);
    pa_cvolume_set (&volume, info->volume.channels, next);
    operation = pa_context_set_sink_volume_by_name (context, info->name, &volume, NULL, NULL);
    if (operation != NULL)
        pa_operation_unref (operation);
    panel_volume_show_osd ((guint) ((100.0 * next) / PA_VOLUME_NORM + 0.5));
	if (g_settings_get_boolean (panel_volume_settings (), PANEL_VOLUME_SCROLL_FEEDBACK_KEY))
		gdk_display_beep (gdk_display_get_default ());
}

static void
panel_volume_server_info (pa_context *context, const pa_server_info *info,
                          void *userdata)
{
    VolumeRequest *request = userdata;
    pa_operation *operation;

    if (info == NULL || info->default_sink_name == NULL) {
        g_free (request);
        return;
    }
    operation = pa_context_get_sink_info_by_name (context, info->default_sink_name,
                                                   panel_volume_sink_info, request);
    if (operation != NULL)
        pa_operation_unref (operation);
}

static void
panel_volume_request_server_info (VolumeRequest *request)
{
    pa_operation *operation = pa_context_get_server_info (pulse_volume.context,
                                                           panel_volume_server_info,
                                                           request);
    if (operation != NULL)
        pa_operation_unref (operation);
}

static gboolean
panel_volume_connect (void)
{
    if (pulse_volume.context == NULL) {
        pulse_volume.mainloop = pa_glib_mainloop_new (g_main_context_default ());
        if (pulse_volume.mainloop == NULL)
            return FALSE;
        pulse_volume.context = pa_context_new (pa_glib_mainloop_get_api (pulse_volume.mainloop),
                                               "MATE Panel");
        if (pulse_volume.context == NULL)
            return FALSE;
        pa_context_set_state_callback (pulse_volume.context,
                                       panel_volume_context_state_changed, NULL);
        if (pa_context_connect (pulse_volume.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
            return FALSE;
    }
    return pa_context_get_state (pulse_volume.context) == PA_CONTEXT_READY;
}
#endif

gboolean
panel_volume_handle_scroll (const GdkEventScroll *event)
{
    GSettings *settings = panel_volume_settings ();
#ifdef HAVE_LIBPULSE
    gint direction;
    guint step;
#endif

    if (!g_settings_get_boolean (settings, PANEL_ENABLE_SCROLL_VOLUME_KEY))
        return FALSE;
    if (event->direction == GDK_SCROLL_UP)
        direction = 1;
    else if (event->direction == GDK_SCROLL_DOWN)
        direction = -1;
    else if (event->direction == GDK_SCROLL_SMOOTH && event->delta_y != 0.0)
        direction = event->delta_y < 0.0 ? 1 : -1;
    else
        return FALSE;
    step = g_settings_get_uint (settings, PANEL_VOLUME_SCROLL_STEP_KEY);

#ifdef HAVE_LIBPULSE
    if (!panel_volume_connect ()) {
        pulse_volume.pending_direction = direction;
        pulse_volume.pending_step = step;
    } else {
        VolumeRequest *request = g_new0 (VolumeRequest, 1);
        request->direction = direction;
        request->step = step;
        panel_volume_request_server_info (request);
    }
    return TRUE;
#else
    return FALSE;
#endif
}
