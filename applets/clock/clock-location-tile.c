#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "clock.h"
#include "clock-face.h"
#include "clock-location-tile.h"
#include "clock-location.h"
#include "clock-utils.h"
#include "clock-marshallers.h"
#include "set-timezone.h"


#if GTK_CHECK_VERSION (3, 0, 0)
G_DEFINE_TYPE (ClockLocationTile, clock_location_tile, GTK_TYPE_BIN)
#else
G_DEFINE_TYPE (ClockLocationTile, clock_location_tile, GTK_TYPE_ALIGNMENT)
#endif

enum {
	TILE_PRESSED,
	NEED_CLOCK_FORMAT,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
        ClockLocation *location;

        struct tm last_refresh;
	long last_offset;

        ClockFaceSize size;

	GtkWidget *box;
        GtkWidget *clock_face;
        GtkWidget *city_label;
        GtkWidget *time_label;

        GtkWidget *current_button;
        GtkWidget *current_label;
        GtkWidget *current_marker;
        GtkWidget *current_spacer;
        GtkSizeGroup *current_group;
        GtkSizeGroup *button_group;

        GtkWidget *weather_icon;

	gulong location_weather_updated_id;
} ClockLocationTilePrivate;

static void clock_location_tile_finalize (GObject *);

#define PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLOCK_LOCATION_TILE_TYPE, ClockLocationTilePrivate))

static void clock_location_tile_fill (ClockLocationTile *this);
#if GTK_CHECK_VERSION (3, 0, 0)
static void update_weather_icon (ClockLocation *loc, GWeatherInfo *info, gpointer data);
#else
static void update_weather_icon (ClockLocation *loc, WeatherInfo *info, gpointer data);
#endif
static gboolean weather_tooltip (GtkWidget *widget,
                                 gint x, gint y,
		                 gboolean    keyboard_mode,
		                 GtkTooltip *tooltip,
		                 gpointer    data);

ClockLocationTile *
clock_location_tile_new (ClockLocation *loc,
			 ClockFaceSize size)
{
        ClockLocationTile *this;
        ClockLocationTilePrivate *priv;

        this = g_object_new (CLOCK_LOCATION_TILE_TYPE, NULL);
        priv = PRIVATE (this);

        priv->location = g_object_ref (loc);
        priv->size = size;

        clock_location_tile_fill (this);

	update_weather_icon (loc, clock_location_get_weather_info (loc), this);
	gtk_widget_set_has_tooltip (priv->weather_icon, TRUE);

	g_signal_connect (priv->weather_icon, "query-tooltip",
			  G_CALLBACK (weather_tooltip), this);
	priv->location_weather_updated_id = g_signal_connect (G_OBJECT (loc), "weather-updated",
							      G_CALLBACK (update_weather_icon), this);

        return this;
}

static void
clock_location_tile_class_init (ClockLocationTileClass *this_class)
{
        GObjectClass *g_obj_class = G_OBJECT_CLASS (this_class);

        g_obj_class->finalize = clock_location_tile_finalize;

        g_type_class_add_private (this_class, sizeof (ClockLocationTilePrivate));

	signals[TILE_PRESSED] = g_signal_new ("tile-pressed",
					      G_TYPE_FROM_CLASS (g_obj_class),
					      G_SIGNAL_RUN_FIRST,
					      G_STRUCT_OFFSET (ClockLocationTileClass, tile_pressed),
					      NULL,
					      NULL,
					      g_cclosure_marshal_VOID__VOID,
					      G_TYPE_NONE, 0);
	signals[NEED_CLOCK_FORMAT] = g_signal_new ("need-clock-format",
						   G_TYPE_FROM_CLASS (g_obj_class),
						   G_SIGNAL_RUN_LAST,
						   G_STRUCT_OFFSET (ClockLocationTileClass, need_clock_format),
						   NULL,
						   NULL,
						   _clock_marshal_INT__VOID,
						   G_TYPE_INT, 0);
}

static void
clock_location_tile_init (ClockLocationTile *this)
{
        ClockLocationTilePrivate *priv = PRIVATE (this);

        priv->location = NULL;

        memset (&(priv->last_refresh), 0, sizeof (struct tm));
	priv->last_offset = 0;

        priv->size = CLOCK_FACE_SMALL;

        priv->clock_face = NULL;
        priv->city_label = NULL;
        priv->time_label = NULL;
}

static void
clock_location_tile_finalize (GObject *g_obj)
{
        ClockLocationTilePrivate *priv = PRIVATE (g_obj);

        if (priv->location) {
		g_signal_handler_disconnect (priv->location, priv->location_weather_updated_id);
		priv->location_weather_updated_id = 0;

                g_object_unref (priv->location);
                priv->location = NULL;
        }

        if (priv->button_group) {
                g_object_unref (priv->button_group);
                priv->button_group = NULL;
        }

        if (priv->current_group) {
                g_object_unref (priv->current_group);
                priv->current_group = NULL;
        }

        G_OBJECT_CLASS (clock_location_tile_parent_class)->finalize (g_obj);
}

static gboolean
press_on_tile      (GtkWidget             *widget,
                    GdkEventButton        *event,
                    ClockLocationTile *tile)
{
	g_signal_emit (tile, signals[TILE_PRESSED], 0);

        return TRUE;
}

static void
make_current_cb (gpointer data, GError *error)
{
	GtkWidget *dialog;

        if (error) {
                dialog = gtk_message_dialog_new (NULL,
                                                 0,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 _("Failed to set the system timezone"));
                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
                g_signal_connect (dialog, "response",
                                  G_CALLBACK (gtk_widget_destroy), NULL);
                gtk_window_present (GTK_WINDOW (dialog));

                g_error_free (error);
        }
}

static void
make_current (GtkWidget *widget, ClockLocationTile *tile)
{
        ClockLocationTilePrivate *priv = PRIVATE (tile);

	clock_location_make_current (priv->location,
				     (GFunc)make_current_cb, tile, NULL);
}

static gboolean
enter_or_leave_tile (GtkWidget             *widget,
                     GdkEventCrossing      *event,
                     ClockLocationTile *tile)
{
	ClockLocationTilePrivate *priv = PRIVATE (tile);

	if (event->mode != GDK_CROSSING_NORMAL) {
		return TRUE;
	}

	if (clock_location_is_current (priv->location)) {
		gtk_widget_hide (priv->current_button);
		gtk_widget_hide (priv->current_spacer);
		gtk_widget_show (priv->current_marker);

		return TRUE;
	}

	if (event->type == GDK_ENTER_NOTIFY) {
		gint can_set;

		if (clock_location_is_current_timezone (priv->location))
			can_set = 2;
		else
			can_set = can_set_system_timezone ();
		if (can_set != 0) {
			gtk_label_set_markup (GTK_LABEL (priv->current_label),
						can_set == 1 ?
							_("<small>Set...</small>") :
							_("<small>Set</small>"));
			gtk_widget_hide (priv->current_spacer);
			gtk_widget_hide (priv->current_marker);
			gtk_widget_show (priv->current_button);
		}
		else {
			gtk_widget_hide (priv->current_marker);
			gtk_widget_hide (priv->current_button);
			gtk_widget_show (priv->current_spacer);
		}
	}
	else {
		if (event->detail != GDK_NOTIFY_INFERIOR) {
			gtk_widget_hide (priv->current_button);
			gtk_widget_hide (priv->current_marker);
			gtk_widget_show (priv->current_spacer);
		}
	}

	return TRUE;
}

static void
clock_location_tile_fill (ClockLocationTile *this)
{
        ClockLocationTilePrivate *priv = PRIVATE (this);
#if !GTK_CHECK_VERSION (3, 0, 0)
        GtkWidget *align;
#endif
        GtkWidget *strut;
        GtkWidget *box;
#if !GTK_CHECK_VERSION (3, 0, 0)
        GtkWidget *alignment;
#endif
        GtkWidget *tile;
        GtkWidget *head_section;

        priv->box = gtk_event_box_new ();

        gtk_widget_add_events (priv->box, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect (priv->box, "button-press-event",
                          G_CALLBACK (press_on_tile), this);
        g_signal_connect (priv->box, "enter-notify-event",
                          G_CALLBACK (enter_or_leave_tile), this);
        g_signal_connect (priv->box, "leave-notify-event",
                          G_CALLBACK (enter_or_leave_tile), this);

#if GTK_CHECK_VERSION (3, 0, 0)
        tile = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_top (tile, 3);
        gtk_widget_set_margin_bottom (tile, 3);
        gtk_widget_set_margin_start (tile, 3);
#else
        tile = gtk_hbox_new (FALSE, 6);
        alignment = gtk_alignment_new (0, 0, 1, 0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 3, 3, 3, 0);
#endif

        priv->city_label = gtk_label_new (NULL);
#if GTK_CHECK_VERSION (3, 0, 0)
        gtk_widget_set_margin_end (priv->city_label, 3);
#endif
#if GTK_CHECK_VERSION (3, 16, 0)
        gtk_label_set_xalign (GTK_LABEL (priv->city_label), 0.0);
        gtk_label_set_yalign (GTK_LABEL (priv->city_label), 0.0);
#else
        gtk_misc_set_alignment (GTK_MISC (priv->city_label), 0, 0);
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
        head_section = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start (GTK_BOX (head_section), priv->city_label, FALSE, FALSE, 0);
#else
        head_section = gtk_vbox_new (FALSE, 0);
        align = gtk_alignment_new (0, 0, 0, 0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 0, 3);
        gtk_container_add (GTK_CONTAINER (align), priv->city_label);
        gtk_box_pack_start (GTK_BOX (head_section), align, FALSE, FALSE, 0);
#endif

        priv->time_label = gtk_label_new (NULL);
#if GTK_CHECK_VERSION (3, 0, 0)
        gtk_widget_set_margin_end (priv->time_label, 3);
#endif
#if GTK_CHECK_VERSION (3, 16, 0)
        gtk_label_set_xalign (GTK_LABEL (priv->time_label), 0.0);
        gtk_label_set_yalign (GTK_LABEL (priv->time_label), 0.0);
#else
        gtk_misc_set_alignment (GTK_MISC (priv->time_label), 0, 0);
#endif

        priv->weather_icon = gtk_image_new ();
#if GTK_CHECK_VERSION (3, 0, 0)
        gtk_widget_set_valign (priv->weather_icon, GTK_ALIGN_START);
#else
        align = gtk_alignment_new (0, 0, 0, 0);
        gtk_container_add (GTK_CONTAINER (align), priv->weather_icon);
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start (GTK_BOX (head_section), box, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (box), priv->weather_icon, FALSE, FALSE, 0);
#else
        box = gtk_hbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (head_section), box, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (box), align, FALSE, FALSE, 0);
#endif
        gtk_box_pack_start (GTK_BOX (box), priv->time_label, FALSE, FALSE, 0);

        priv->current_button = gtk_button_new ();
	/* The correct label is set on EnterNotify events */
	priv->current_label = gtk_label_new ("");
        gtk_widget_show (priv->current_label);
        gtk_widget_set_no_show_all (priv->current_button, TRUE);
        gtk_container_add (GTK_CONTAINER (priv->current_button), priv->current_label);
        gtk_widget_set_tooltip_text (priv->current_button,
				     _("Set location as current location and use its timezone for this computer"));

	priv->current_marker = gtk_image_new_from_icon_name ("go-home", GTK_ICON_SIZE_BUTTON);
#if GTK_CHECK_VERSION (3, 0, 0)
	gtk_widget_set_halign (priv->current_marker, GTK_ALIGN_END);
	gtk_widget_set_valign (priv->current_marker, GTK_ALIGN_CENTER);
#else
	gtk_misc_set_alignment (GTK_MISC (priv->current_marker), 1.0, 0.5);
#endif
	gtk_widget_set_no_show_all (priv->current_marker, TRUE);

	priv->current_spacer = gtk_event_box_new ();
	gtk_widget_set_no_show_all (priv->current_spacer, TRUE);

        strut = gtk_event_box_new ();
        gtk_box_pack_start (GTK_BOX (box), strut, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (box), priv->current_button, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (box), priv->current_marker, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (box), priv->current_spacer, FALSE, FALSE, 0);
        priv->button_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
        gtk_size_group_set_ignore_hidden (priv->button_group, FALSE);
        gtk_size_group_add_widget (priv->button_group, strut);
        gtk_size_group_add_widget (priv->button_group, priv->current_button);

	/* 
	 * Avoid resizing the popup as the tiles display the current marker, 
	 * set button or nothing. For that purpose, replace 'nothing' with 
	 * an event box, and force the button, marker and spacer to have the 
	 * same size via a size group. The visibility of the three is managed
 	 * manually to ensure that only one of them is shown at any time. 
 	 * (The all have to be shown initially to get the sizes worked out, 
 	 * but they are never visible together). 
	 */
        priv->current_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
        gtk_size_group_set_ignore_hidden (priv->current_group, FALSE);
        gtk_size_group_add_widget (priv->current_group, priv->current_button);
        gtk_size_group_add_widget (priv->current_group, priv->current_marker);
        gtk_size_group_add_widget (priv->current_group, priv->current_spacer);
	
	gtk_widget_show (priv->current_button);
	gtk_widget_show (priv->current_marker);
	gtk_widget_show (priv->current_spacer);

        g_signal_connect (priv->current_button, "clicked",
                          G_CALLBACK (make_current), this);

        priv->clock_face = clock_face_new_with_location (
                priv->size, priv->location, head_section);

        gtk_box_pack_start (GTK_BOX (tile), priv->clock_face, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (tile), head_section, TRUE, TRUE, 0);

#if GTK_CHECK_VERSION (3, 0, 0)
        gtk_container_add (GTK_CONTAINER (priv->box), tile);
#else
        gtk_container_add (GTK_CONTAINER (alignment), tile);
        gtk_container_add (GTK_CONTAINER (priv->box), alignment);
#endif
        gtk_container_add (GTK_CONTAINER (this), priv->box);
}

static gboolean
clock_needs_face_refresh (ClockLocationTile *this)
{
        ClockLocationTilePrivate *priv = PRIVATE (this);
        struct tm now;

        clock_location_localtime (priv->location, &now);

        if (now.tm_year > priv->last_refresh.tm_year
            || now.tm_mon > priv->last_refresh.tm_mon
            || now.tm_mday > priv->last_refresh.tm_mday
            || now.tm_hour > priv->last_refresh.tm_hour
            || now.tm_min > priv->last_refresh.tm_min) {
                return TRUE;
        }

        if ((priv->size == CLOCK_FACE_LARGE)
            && now.tm_sec > priv->last_refresh.tm_sec) {
                return TRUE;
        }

        return FALSE;
}

static gboolean
clock_needs_label_refresh (ClockLocationTile *this)
{
        ClockLocationTilePrivate *priv = PRIVATE (this);
        struct tm now;
	long offset;

        clock_location_localtime (priv->location, &now);
	offset = clock_location_get_offset (priv->location);

        if (now.tm_year > priv->last_refresh.tm_year
            || now.tm_mon > priv->last_refresh.tm_mon
            || now.tm_mday > priv->last_refresh.tm_mday
            || now.tm_hour > priv->last_refresh.tm_hour
            || now.tm_min > priv->last_refresh.tm_min
	    || offset != priv->last_offset) {
                return TRUE;
        }

        return FALSE;
}

static void
copy_tm (struct tm *from, struct tm *to)
{
        to->tm_sec = from->tm_sec;
        to->tm_min = from->tm_min;
        to->tm_hour = from->tm_hour;
        to->tm_mday = from->tm_mday;
        to->tm_mon = from->tm_mon;
        to->tm_year = from->tm_year;
        to->tm_wday = from->tm_wday;
        to->tm_yday = from->tm_yday;
}

static char *
format_time (struct tm   *now, 
             char        *tzname,
             ClockFormat  clock_format,
	     long         offset)
{
	char buf[256];
	char *format;
	time_t local_t;
	struct tm local_now;
	char *utf8;	
	char *tmp;	
	long hours, minutes;

	time (&local_t);
	localtime_r (&local_t, &local_now);

	if (local_now.tm_wday != now->tm_wday) {
		if (clock_format == CLOCK_FORMAT_12) {
			/* Translators: This is a strftime format string.
			 * It is used to display the time in 12-hours format
			 * (eg, like in the US: 8:10 am), when the local
			 * weekday differs from the weekday at the location
			 * (the %A expands to the weekday). The %p expands to
			 * am/pm. */
			format = _("%l:%M <small>%p (%A)</small>");
		}
		else {
			/* Translators: This is a strftime format string.
			 * It is used to display the time in 24-hours format
			 * (eg, like in France: 20:10), when the local
			 * weekday differs from the weekday at the location
			 * (the %A expands to the weekday). */
			format = _("%H:%M <small>(%A)</small>");
		}
	}
	else {
		if (clock_format == CLOCK_FORMAT_12) {
			/* Translators: This is a strftime format string.
			 * It is used to display the time in 12-hours format
			 * (eg, like in the US: 8:10 am). The %p expands to
			 * am/pm. */
			format = _("%l:%M <small>%p</small>");
		}
		else {
			/* Translators: This is a strftime format string.
			 * It is used to display the time in 24-hours format
			 * (eg, like in France: 20:10). */
			format = _("%H:%M");
		}
	}

	if (strftime (buf, sizeof (buf), format, now) <= 0) {
		strcpy (buf, "???");
	}

        hours = offset / 3600;
        minutes = labs (offset % 3600) / 60;

	if (minutes != 0) {
		tmp = g_strdup_printf ("%s <small>%s %+ld:%ld</small>", buf, tzname, hours, minutes);
	}
	else if (hours != 0) {
		tmp = g_strdup_printf ("%s <small>%s %+ld</small>", buf, tzname, hours);
	}
	else {
		tmp = g_strdup_printf ("%s <small>%s</small>", buf, tzname);
	}

	utf8 = g_locale_to_utf8 (tmp, -1, NULL, NULL, NULL);

	g_free (tmp);

	return utf8;
}

static char *
convert_time_to_str (time_t now, ClockFormat clock_format)
{
	const gchar *format;
	struct tm *tm;
	gchar buf[128];

	if (clock_format == CLOCK_FORMAT_12) {
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 12-hours format (eg, like
                 * in the US: 8:10 am). The %p expands to am/pm.
                 */
		format = _("%l:%M %p");
	}
	else {
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 24-hours format (eg, like
                 * in France: 20:10).
                 */
		format = _("%H:%M");
	}

	tm = localtime (&now);
	strftime (buf, sizeof (buf) - 1, format, tm);

	return g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
}

void
clock_location_tile_refresh (ClockLocationTile *this, gboolean force_refresh)
{
        ClockLocationTilePrivate *priv = PRIVATE (this);
        gchar *tmp, *tzname;
        struct tm now;
	long offset;
	int format;

	g_return_if_fail (IS_CLOCK_LOCATION_TILE (this));

        if (clock_location_is_current (priv->location)) {
		gtk_widget_hide (priv->current_spacer);
		gtk_widget_hide (priv->current_button);
		gtk_widget_show (priv->current_marker);
	}
	else {
		if (gtk_widget_get_visible (priv->current_marker)) {
			gtk_widget_hide (priv->current_marker);
			gtk_widget_hide (priv->current_button);
			gtk_widget_show (priv->current_spacer);
		}
	}

        if (clock_needs_face_refresh (this)) {
                clock_face_refresh (CLOCK_FACE (priv->clock_face));
        }

        if (!force_refresh && !clock_needs_label_refresh (this)) {
                return;
        }

        clock_location_localtime (priv->location, &now);
        tzname = clock_location_get_tzname (priv->location);

        copy_tm (&now, &(priv->last_refresh));
	priv->last_offset = clock_location_get_offset (priv->location);

        tmp = g_strdup_printf ("<big><b>%s</b></big>",
                               clock_location_get_display_name (priv->location));
        gtk_label_set_markup (GTK_LABEL (priv->city_label), tmp);
        g_free (tmp);

	g_signal_emit (this, signals[NEED_CLOCK_FORMAT], 0, &format);

	offset = - priv->last_offset;

	tmp = format_time (&now, tzname, format, offset);

        gtk_label_set_markup (GTK_LABEL (priv->time_label), tmp);

        g_free (tmp);
}

#if GTK_CHECK_VERSION (3, 0, 0)
void
weather_info_setup_tooltip (GWeatherInfo *info, ClockLocation *location, GtkTooltip *tooltip,
			    ClockFormat clock_format)
#else
void
weather_info_setup_tooltip (WeatherInfo *info, ClockLocation *location, GtkTooltip *tooltip,
			    ClockFormat clock_format)
#endif
{
        GdkPixbuf *pixbuf = NULL;
        GtkIconTheme *theme = NULL;
	const gchar *conditions;
	gchar *temp, *apparent;
	gchar *line1, *line2, *line3, *line4, *tip;
	const gchar *icon_name;
	const gchar *sys_timezone;
	time_t sunrise_time, sunset_time;
	gchar *sunrise_str, *sunset_str;

#if GTK_CHECK_VERSION (3, 0, 0)
        icon_name = gweather_info_get_icon_name (info);
#else
	icon_name = weather_info_get_icon_name (info);
#endif
        theme = gtk_icon_theme_get_default ();
        pixbuf = gtk_icon_theme_load_icon (theme, icon_name, 48,
                                           GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);
        if (pixbuf)
                gtk_tooltip_set_icon (tooltip, pixbuf);

#if GTK_CHECK_VERSION (3, 0, 0)
	conditions = gweather_info_get_conditions (info);
#else
	conditions = weather_info_get_conditions (info);
#endif
	if (strcmp (conditions, "-") != 0) {
#if GTK_CHECK_VERSION (3, 0, 0)
		gchar * sky_info = gweather_info_get_sky (info);
		line1 = g_strdup_printf (_("%s, %s"),
					 conditions,
					 sky_info);
		g_free (sky_info);
#else
		line1 = g_strdup_printf (_("%s, %s"),
					 conditions,
					 weather_info_get_sky (info));
#endif
	}
	else
#if GTK_CHECK_VERSION (3, 0, 0)
		line1 = gweather_info_get_sky (info);
#else
		line1 = g_strdup (weather_info_get_sky (info));
#endif

	/* we need to g_strdup() since both functions return the same address
	 * of a static buffer */
#if GTK_CHECK_VERSION (3, 0, 0)
	temp = gweather_info_get_temp (info);
	apparent = gweather_info_get_apparent (info);
#else
	temp = g_strdup (weather_info_get_temp (info));
	apparent = g_strdup (weather_info_get_apparent (info));
#endif
	if (strcmp (apparent, temp) != 0 &&
	    /* FMQ: it's broken to read from another module's translations; add some API to libmateweather. */
            strcmp (apparent, dgettext ("mate-applets-2.0", "Unknown")) != 0)
		/* Translators: The two strings are temperatures. */
		line2 = g_strdup_printf (_("%s, feels like %s"), temp, apparent);
	else
		line2 = g_strdup (temp);
	g_free (temp);
	g_free (apparent);

        if (strcmp (apparent, dgettext ("mate-applets-2.0", "Unknown")) != 0) {
#if GTK_CHECK_VERSION (3, 0, 0)
		gchar *wind =  gweather_info_get_wind (info);
#else
		const gchar *wind = weather_info_get_wind (info);
#endif
		line3 = g_strdup_printf ("%s\n", wind);
#if GTK_CHECK_VERSION (3, 0, 0)
		g_free (wind);
#endif
	}
	else
		line3 = g_strdup ("");

	sys_timezone = getenv ("TZ");
	setenv ("TZ", clock_location_get_timezone (location), 1);
	tzset ();
#if GTK_CHECK_VERSION (3, 0, 0)
	if (gweather_info_get_value_sunrise (info, &sunrise_time))
#else
	if (weather_info_get_value_sunrise (info, &sunrise_time))
#endif
		sunrise_str = convert_time_to_str (sunrise_time, clock_format);
	else
		sunrise_str = g_strdup ("???");
#if GTK_CHECK_VERSION (3, 0, 0)
	if (gweather_info_get_value_sunset (info, &sunset_time))
#else
	if (weather_info_get_value_sunset (info, &sunset_time))
#endif
		sunset_str = convert_time_to_str (sunset_time, clock_format);
	else
		sunset_str = g_strdup ("???");
	line4 = g_strdup_printf (_("Sunrise: %s / Sunset: %s"),
				 sunrise_str, sunset_str);
	g_free (sunrise_str);
	g_free (sunset_str);

	if (sys_timezone)
		setenv ("TZ", sys_timezone, 1);
	else
		unsetenv ("TZ");
	tzset ();

	tip = g_strdup_printf ("<b>%s</b>\n%s\n%s%s", line1, line2, line3, line4);
	gtk_tooltip_set_markup (tooltip, tip);
	g_free (line1);
	g_free (line2);
	g_free (line3);
	g_free (line4);
	g_free (tip);
}

static gboolean
weather_tooltip (GtkWidget  *widget,
		 gint        x,
		 gint	     y,
		 gboolean    keyboard_mode,
		 GtkTooltip *tooltip,
		 gpointer    data)
{
        ClockLocationTile *tile = data;
        ClockLocationTilePrivate *priv = PRIVATE (tile);
#if GTK_CHECK_VERSION (3, 0, 0)
	GWeatherInfo *info;
#else
	WeatherInfo *info;
#endif
	int clock_format;

	info = clock_location_get_weather_info (priv->location);

#if GTK_CHECK_VERSION (3, 0, 0)
	if (!info || !gweather_info_is_valid (info))
#else
	if (!info || !weather_info_is_valid (info))
#endif
		return FALSE;

	g_signal_emit (tile, signals[NEED_CLOCK_FORMAT], 0, &clock_format);

	weather_info_setup_tooltip (info, priv->location, tooltip, clock_format);

	return TRUE;
}

#if GTK_CHECK_VERSION (3, 0, 0)
static void
update_weather_icon (ClockLocation *loc, GWeatherInfo *info, gpointer data)
#else
static void
update_weather_icon (ClockLocation *loc, WeatherInfo *info, gpointer data)
#endif
{
        ClockLocationTile *tile = data;
        ClockLocationTilePrivate *priv = PRIVATE (tile);
        GdkPixbuf *pixbuf = NULL;
        GtkIconTheme *theme = NULL;
        const gchar *icon_name;

#if GTK_CHECK_VERSION (3, 0, 0)
        if (!info || !gweather_info_is_valid (info))
                return;
#else
        if (!info || !weather_info_is_valid (info))
                return;
#endif

#if GTK_CHECK_VERSION (3, 0, 0)
        icon_name = gweather_info_get_icon_name (info);
#else
        icon_name = weather_info_get_icon_name (info);
#endif
        theme = gtk_icon_theme_get_default ();
        pixbuf = gtk_icon_theme_load_icon (theme, icon_name, 16,
                                           GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);

        if (pixbuf) {
                gtk_image_set_from_pixbuf (GTK_IMAGE (priv->weather_icon), pixbuf);
#if GTK_CHECK_VERSION (3, 0, 0)
                gtk_widget_set_margin_end (priv->weather_icon, 6);
#else
                gtk_alignment_set_padding (GTK_ALIGNMENT (gtk_widget_get_parent (priv->weather_icon)), 0, 0, 0, 6);
#endif
        }
}

ClockLocation *
clock_location_tile_get_location (ClockLocationTile *this)
{
        ClockLocationTilePrivate *priv;

	g_return_val_if_fail (IS_CLOCK_LOCATION_TILE (this), NULL);

	priv = PRIVATE (this);

	return g_object_ref (priv->location);
}
