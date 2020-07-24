/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * clock.c: the MATE clock applet
 *
 * Copyright (C) 1997-2003 Free Software Foundation, Inc.
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
 *
 * Authors:
 *      Miguel de Icaza
 *      Frederico Mena
 *      Stuart Parmenter
 *      Alexander Larsson
 *      George Lebl
 *      Gediminas Paulauskas
 *      Mark McLoughlin
 *      Stefano Karapetsas
 */

#include "config.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <locale.h>

#include <mate-panel-applet.h>
#include <mate-panel-applet-gsettings.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#include <libmateweather/mateweather-prefs.h>
#include <libmateweather/location-entry.h>
#include <libmateweather/timezone-menu.h>

#include "clock.h"

#include "calendar-window.h"
#include "clock-location.h"
#include "clock-location-tile.h"
#include "clock-map.h"
#include "clock-utils.h"
#include "set-timezone.h"
#include "system-timezone.h"

#define INTERNETSECOND (864)
#define INTERNETBEAT   (86400)

#define NEVER_SENSITIVE "never_sensitive"

#define KEY_FORMAT                "format"
#define KEY_SHOW_SECONDS        "show-seconds"
#define KEY_SHOW_DATE                "show-date"
#define KEY_SHOW_WEATHER        "show-weather"
#define KEY_SHOW_TEMPERATURE        "show-temperature"
#define KEY_CUSTOM_FORMAT        "custom-format"
#define KEY_SHOW_WEEK                "show-week-numbers"
#define KEY_CITIES                "cities"
#define KEY_TEMPERATURE_UNIT        "temperature-unit"
#define KEY_SPEED_UNIT                "speed-unit"

enum {
        COL_CITY_NAME = 0,
        COL_CITY_TZ,
        COL_CITY_LOC,
        COL_CITY_LAST
};

typedef struct _ClockData ClockData;

struct _ClockData {
        /* widgets */
        GtkWidget *applet;

        GtkWidget *panel_button;        /* main toggle button for the whole clock */

        GtkWidget *main_obox;                /* orientable box inside panel_button */
        GtkWidget *weather_obox;        /* orientable box for the weather widgets */

        GtkWidget *clockw;                /* main label for the date/time display */

        GtkWidget *panel_weather_icon;
        GtkWidget *panel_temperature_label;

        GtkWidget *props;
        GtkWidget *calendar_popup;

        GtkWidget *clock_vbox;
        GtkSizeGroup *clock_group;

        GtkBuilder *builder;

        /* Preferences dialog */
        GtkWidget *prefs_window;
        GtkTreeView *prefs_locations;

        GtkWidget *prefs_location_add_button;
        GtkWidget *prefs_location_edit_button;
        GtkWidget *prefs_location_remove_button;

        MateWeatherLocationEntry *location_entry;
        MateWeatherTimezoneMenu *zone_combo;

        GtkWidget *time_settings_button;
        GtkWidget *calendar;
        GtkWidget *hours_spin;
        GtkWidget *minutes_spin;
        GtkWidget *seconds_spin;
        GtkWidget *set_time_button;

        GtkListStore *cities_store;
        GtkWidget *cities_section;
        GtkWidget *map_widget;

        /* Window to set the time */
        GtkWidget *set_time_window;
        GtkWidget *current_time_label;

        /* preferences */
        ClockFormat  format;
        char        *custom_format;
        gboolean     showseconds;
        gboolean     showdate;
        gboolean     showweek;
        gboolean     show_weather;
        gboolean     show_temperature;

        TempUnit     temperature_unit;
        SpeedUnit    speed_unit;

        /* Locations */
        GList *locations;
        GList *location_tiles;

        /* runtime data */
        time_t             current_time;
        char              *timeformat;
        guint              timeout;
        MatePanelAppletOrient  orient;
        int                size;
        GtkAllocation      old_allocation;

        SystemTimezone *systz;

        int fixed_width;
        int fixed_height;

        GtkWidget *showseconds_check;
        GtkWidget *showdate_check;
        GtkWidget *showweeks_check;
        GtkWidget *custom_hbox;
        GtkWidget *custom_label;
        GtkWidget *custom_entry;
        gboolean   custom_format_shown;

        gboolean   can_handle_format_12;

        GSettings *settings;

        const gchar *weather_icon_name;
};

/* Used to count the number of clock instances. It's there to know when we
 * should free resources that are shared. */
/* FIXME unused variable, remove?
static int clock_numbers = 0;
*/

static void  update_clock (ClockData * cd);
static void  update_tooltip (ClockData * cd);
static void  update_panel_weather (ClockData *cd);
static int   clock_timeout_callback (gpointer data);
static float get_itime    (time_t current_time);

static void set_atk_name_description (GtkWidget *widget,
                                      const char *name,
                                      const char *desc);
static void verb_display_properties_dialog (GtkAction  *action,
                                            ClockData  *cd);

static void display_properties_dialog (ClockData  *cd,
                                       gboolean    start_in_locations_page);
static void display_help_dialog       (GtkAction  *action,
                                       ClockData  *cd);
static void display_about_dialog      (GtkAction  *action,
                                       ClockData  *cd);
static void position_calendar_popup   (ClockData  *cd);
static void update_orient (ClockData *cd);
static void applet_change_orient (MatePanelApplet       *applet,
                                  MatePanelAppletOrient  orient,
                                  ClockData         *cd);

static void edit_hide (GtkWidget *unused, ClockData *cd);
static gboolean edit_delete (GtkWidget *unused, GdkEvent *event, ClockData *cd);
static void save_cities_store (ClockData *cd);

/* ClockBox, an instantiable GtkBox */

typedef GtkBox      ClockBox;
typedef GtkBoxClass ClockBoxClass;

static GType clock_box_get_type (void);

G_DEFINE_TYPE (ClockBox, clock_box, GTK_TYPE_BOX)

static void
clock_box_init (ClockBox *box)
{
}

static void
clock_box_class_init (ClockBoxClass *klass)
{
}

/* Clock */

static inline GtkWidget *
_clock_get_widget (ClockData  *cd,
                   const char *name)
{
        return GTK_WIDGET (gtk_builder_get_object (cd->builder, name));
}

static void
unfix_size (ClockData *cd)
{
        cd->fixed_width = -1;
        cd->fixed_height = -1;
        gtk_widget_queue_resize (cd->panel_button);
}

static int
calculate_minimum_width (GtkWidget   *widget,
                         const gchar *text)
{
        PangoContext    *pango_context;
        PangoLayout     *layout;
        int              width, height;
        GtkStyleContext *style_context;
        GtkStateFlags    state;
        GtkBorder        padding;

        pango_context = gtk_widget_get_pango_context (widget);

        layout = pango_layout_new (pango_context);
        pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
        pango_layout_set_text (layout, text, -1);
        pango_layout_get_pixel_size (layout, &width, &height);
        g_object_unref (G_OBJECT (layout));
        layout = NULL;

        state = gtk_widget_get_state_flags (widget);
        style_context = gtk_widget_get_style_context (widget);
        gtk_style_context_get_padding (style_context, state, &padding);
        width += padding.left + padding.right;

        return width;
}

static void
clock_set_timeout (ClockData *cd,
                   time_t     now)
{
        int timeouttime;

        if (cd->format == CLOCK_FORMAT_INTERNET) {
                int itime_ms;

                itime_ms = ((unsigned int) (get_itime (now) * 1000));

                if (!cd->showseconds)
                        timeouttime = (999 - itime_ms % 1000) * 86.4 + 1;
                else {
                        struct timeval tv;
                        gettimeofday (&tv, NULL);
                        itime_ms += (tv.tv_usec * 86.4) / 1000;
                        timeouttime = ((999 - itime_ms % 1000) * 86.4) / 100 + 1;
                }
        } else {
                 struct timeval tv;

                gettimeofday (&tv, NULL);
                 timeouttime = (G_USEC_PER_SEC - tv.tv_usec)/1000+20;

                /* timeout of one minute if we don't care about the seconds */
                 if (cd->format != CLOCK_FORMAT_UNIX &&
                    !cd->showseconds &&
                    (!cd->set_time_window || !gtk_widget_get_visible (cd->set_time_window)))
                         timeouttime += 1000 * (59 - now % 60);
         }

        cd->timeout = g_timeout_add (timeouttime,
                                     clock_timeout_callback,
                                     cd);
}

static int
clock_timeout_callback (gpointer data)
{
        ClockData *cd = data;
        time_t new_time;

        time (&new_time);

        if (!cd->showseconds &&
            (!cd->set_time_window || !gtk_widget_get_visible (cd->set_time_window)) &&
            cd->format != CLOCK_FORMAT_UNIX &&
            cd->format != CLOCK_FORMAT_CUSTOM) {
                if (cd->format == CLOCK_FORMAT_INTERNET &&
                    (unsigned int)get_itime (new_time) !=
                    (unsigned int)get_itime (cd->current_time)) {
                        update_clock (cd);
                } else if ((cd->format == CLOCK_FORMAT_12 ||
                            cd->format == CLOCK_FORMAT_24) &&
                           new_time / 60 != cd->current_time / 60) {
                        update_clock (cd);
                }
        } else {
                update_clock (cd);
        }

        clock_set_timeout (cd, new_time);

        return FALSE;
}

static float
get_itime (time_t current_time)
{
        struct tm *tm;
        float itime;
        time_t bmt;

        /* BMT (Biel Mean Time) is GMT+1 */
        bmt = current_time + 3600;
        tm = gmtime (&bmt);
        itime = (tm->tm_hour*3600.0 + tm->tm_min*60.0 + tm->tm_sec)/86.4;

        return itime;
}

/* adapted from panel-toplevel.c */
static int
calculate_minimum_height (GtkWidget        *widget,
                          MatePanelAppletOrient orientation)
{
        GtkStateFlags    state;
        GtkStyleContext *style_context;
        const PangoFontDescription *font_desc;
        GtkBorder        padding;
        PangoContext     *pango_context;
        PangoFontMetrics *metrics;
        int               ascent;
        int               descent;
        int               thickness;

        state = gtk_widget_get_state_flags (widget);
        style_context = gtk_widget_get_style_context (widget);

        gtk_style_context_get (style_context, state, GTK_STYLE_PROPERTY_FONT, &font_desc, NULL);
        pango_context = gtk_widget_get_pango_context (widget);
        metrics = pango_context_get_metrics (pango_context,
                                             font_desc,
                                             pango_context_get_language (pango_context));

        ascent  = pango_font_metrics_get_ascent  (metrics);
        descent = pango_font_metrics_get_descent (metrics);

        pango_font_metrics_unref (metrics);

        gtk_style_context_get_padding (style_context, state, &padding);

        if (orientation == MATE_PANEL_APPLET_ORIENT_UP
            || orientation == MATE_PANEL_APPLET_ORIENT_DOWN) {
                thickness = padding.top + padding.bottom;
        } else {
                thickness = padding.left + padding.right;
        }

        return PANGO_PIXELS (ascent + descent) + thickness;
}

static gboolean
use_two_line_format (ClockData *cd)
{
        if (cd->size >= 2 * calculate_minimum_height (cd->panel_button, cd->orient))
                return TRUE;

        return FALSE;
}

static char *
get_updated_timeformat (ClockData *cd)
{
 /* Show date in another line if panel is vertical, or
  * horizontal but large enough to hold two lines of text
  */
        char       *result;
        const char *time_format;
        const char *date_format;
        char       *clock_format;
        const gchar *env_language;
        const gchar *env_lc_time;
        gboolean     use_lctime;

        /* Override LANGUAGE with the LC_TIME environment variable
         * This is needed for gettext to fetch our clock format
         * according to LC_TIME, and not according to the DE LANGUAGE.
         */
        env_language = g_getenv("LANGUAGE");
        env_lc_time = g_getenv("LC_TIME");
        use_lctime = (env_language != NULL) && (env_lc_time != NULL) && (g_strcmp0 (env_language, env_lc_time) != 0);

        if (use_lctime) {
            g_setenv("LANGUAGE", env_lc_time, TRUE);
        }

        if (cd->format == CLOCK_FORMAT_12)
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 12-hours format (eg, like
                 * in the US: 8:10 am). The %p expands to am/pm. */
                time_format = cd->showseconds ? _("%l:%M:%S %p") : _("%l:%M %p");
        else
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 24-hours format (eg, like
                 * in France: 20:10). */
                time_format = cd->showseconds ? _("%H:%M:%S") : _("%H:%M");

        if (!cd->showdate)
                clock_format = g_strdup (time_format);

        else {
                /* Translators: This is a strftime format string.
                 * It is used to display the date. Replace %e with %d if, when
                 * the day of the month as a decimal number is a single digit,
                 * it should begin with a 0 in your locale (e.g. "May 01"
                 * instead of "May  1"). */
                date_format = _("%a %b %e");

                if (use_two_line_format (cd))
                        /* translators: reverse the order of these arguments
                         *              if the time should come before the
                         *              date on a clock in your locale.
                         */
                        clock_format = g_strdup_printf (_("%1$s\n%2$s"),
                                                        date_format,
                                                        time_format);
                else
                        /* translators: reverse the order of these arguments
                         *              if the time should come before the
                         *              date on a clock in your locale.
                         */
                        clock_format = g_strdup_printf (_("%1$s, %2$s"),
                                                        date_format,
                                                        time_format);
        }

        /* Set back LANGUAGE the way it was before */
        if (use_lctime) {
            g_setenv("LANGUAGE", env_language, TRUE);
        }

        result = g_locale_from_utf8 (clock_format, -1, NULL, NULL, NULL);
        g_free (clock_format);

        /* let's be paranoid */
        if (!result)
                result = g_strdup ("???");

        return result;
}

static void
update_timeformat (ClockData *cd)
{
        if (cd->timeformat)
                g_free (cd->timeformat);
        cd->timeformat = get_updated_timeformat (cd);
}

/* sets accessible name and description for the widget */
static void
set_atk_name_description (GtkWidget  *widget,
                          const char *name,
                          const char *desc)
{
        AtkObject *obj;
        obj = gtk_widget_get_accessible (widget);

        /* return if gail is not loaded */
        if (!GTK_IS_ACCESSIBLE (obj))
                return;

        if (desc != NULL)
                atk_object_set_description (obj, desc);
        if (name != NULL)
                atk_object_set_name (obj, name);
}

static void
update_location_tiles (ClockData *cd)
{
        GList *l;

        for (l = cd->location_tiles; l; l = l->next) {
                ClockLocationTile *tile;

                tile = CLOCK_LOCATION_TILE (l->data);
                clock_location_tile_refresh (tile, FALSE);
        }
}

static char *
format_time (ClockData *cd)
{
        struct tm *tm;
        char hour[256];
        char *utf8;

        utf8 = NULL;

        tm = localtime (&cd->current_time);

        if (cd->format == CLOCK_FORMAT_UNIX) {
                if (use_two_line_format (cd)) {
                        utf8 = g_strdup_printf ("%lu\n%05lu",
                                                (unsigned long)(cd->current_time / 100000L),
                                                (unsigned long)(cd->current_time % 100000L));
                } else {
                        utf8 = g_strdup_printf ("%lu",
                                                (unsigned long)cd->current_time);
                }
        } else if (cd->format == CLOCK_FORMAT_INTERNET) {
                float itime = get_itime (cd->current_time);
                if (cd->showseconds)
                        utf8 = g_strdup_printf ("@%3.2f", itime);
                else
                        utf8 = g_strdup_printf ("@%3d", (unsigned int) itime);
        } else if (cd->format == CLOCK_FORMAT_CUSTOM) {
                char *timeformat = g_locale_from_utf8 (cd->custom_format, -1,
                                                       NULL, NULL, NULL);
                if (!timeformat)
                        strcpy (hour, "???");
                else if (strftime (hour, sizeof (hour), timeformat, tm)== 0)
                        strcpy (hour, "???");
                g_free (timeformat);

                utf8 = g_locale_to_utf8 (hour, -1, NULL, NULL, NULL);
        } else {
                if (strftime (hour, sizeof (hour), cd->timeformat, tm) == 0)
                        strcpy (hour, "???");

                utf8 = g_locale_to_utf8 (hour, -1, NULL, NULL, NULL);
        }

        if (!utf8)
                utf8 = g_strdup (hour);

        return utf8;
}

static gchar *
format_time_24 (ClockData *cd)
{
        struct tm *tm;
        gchar buf[128];

        tm = localtime (&cd->current_time);
        strftime (buf, sizeof (buf) - 1, "%k:%M:%S", tm);
        return g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
}

static void
update_clock (ClockData * cd)
{
        gboolean use_markup;
        char *utf8, *text;

        time (&cd->current_time);
        utf8 = format_time (cd);

        use_markup = FALSE;
        if (pango_parse_markup (utf8, -1, 0, NULL, &text, NULL, NULL))
                use_markup = TRUE;
        else
                text = g_strdup (utf8);

        if (use_markup)
                gtk_label_set_markup (GTK_LABEL (cd->clockw), utf8);
        else
                gtk_label_set_text (GTK_LABEL (cd->clockw), utf8);

        set_atk_name_description (cd->applet, text, NULL);

        g_free (utf8);
        g_free (text);

        update_orient (cd);
        gtk_widget_queue_resize (cd->panel_button);

        update_tooltip (cd);
        update_location_tiles (cd);

        if (cd->map_widget && cd->calendar_popup && gtk_widget_get_visible (cd->calendar_popup))
                clock_map_update_time (CLOCK_MAP (cd->map_widget));

        if (cd->current_time_label &&
            gtk_widget_get_visible (cd->current_time_label)) {
                utf8 = format_time_24 (cd);
                gtk_label_set_text (GTK_LABEL (cd->current_time_label), utf8);
                g_free (utf8);
        }
}

static void
update_tooltip (ClockData * cd)
{
        char *tip;
        char *old_tip;
        if (!cd->showdate) {
                struct tm *tm;
                char date[256];
                char *utf8, *loc;
                char *zone;
                time_t now_t;
                struct tm now;

                tm = localtime (&cd->current_time);

                utf8 = NULL;

                /* Show date in tooltip. */
                /* Translators: This is a strftime format string.
                 * It is used to display a date. Please leave "%%s" as it is:
                 * it will be used to insert the timezone name later. */
                loc = g_locale_from_utf8 (_("%A %B %d (%%s)"), -1, NULL, NULL, NULL);
                if (!loc)
                        strcpy (date, "???");
                else if (strftime (date, sizeof (date), loc, tm) == 0)
                        strcpy (date, "???");
                g_free (loc);

                utf8 = g_locale_to_utf8 (date, -1, NULL, NULL, NULL);

                /* Add the timezone name */

                tzset ();
                time (&now_t);
                localtime_r (&now_t, &now);

                if (now.tm_isdst > 0) {
                        zone = tzname[1];
                } else {
                        zone = tzname[0];
                }

                tip = g_strdup_printf (utf8, zone);

                g_free (utf8);
        } else {
                if (cd->calendar_popup)
                        tip = _("Click to hide month calendar");
                else
                        tip = _("Click to view month calendar");
        }

        /* Update only when the new tip is different.
         * This can prevent problems with OpenGL on some drivers */
        old_tip = gtk_widget_get_tooltip_text (cd->panel_button);
        if (g_strcmp0 (old_tip, tip))
            gtk_widget_set_tooltip_text (cd->panel_button, tip);

        g_free (old_tip);
        if (!cd->showdate)
                g_free (tip);
}

static void
refresh_clock (ClockData *cd)
{
        unfix_size (cd);
        update_clock (cd);
}

static void
refresh_clock_timeout(ClockData *cd)
{
        unfix_size (cd);

        update_timeformat (cd);

        if (cd->timeout)
                g_source_remove (cd->timeout);

        update_clock (cd);

        clock_set_timeout (cd, cd->current_time);
}

/**
 * This is like refresh_clock_timeout(), except that we only care about whether
 * the time actually changed. We don't care about the format.
 */
static void
refresh_click_timeout_time_only (ClockData *cd)
{
        if (cd->timeout)
                g_source_remove (cd->timeout);
        clock_timeout_callback (cd);
}

static void
free_locations (ClockData *cd)
{
        if (cd->locations != NULL) {
                GList *l;

                for (l = cd->locations; l; l = l->next)
                        g_object_unref (l->data);

                g_list_free (cd->locations);
        }
        cd->locations = NULL;
}

static void
destroy_clock (GtkWidget * widget, ClockData *cd)
{
        if (cd->settings)
                g_signal_handlers_disconnect_by_data( cd->settings, cd);

        if (cd->systz)
                g_signal_handlers_disconnect_by_data( cd->systz, cd);

        if (cd->settings)
                g_object_unref (cd->settings);
        cd->settings = NULL;

        if (cd->timeout)
                g_source_remove (cd->timeout);
        cd->timeout = 0;

        if (cd->props)
                gtk_widget_destroy (cd->props);
        cd->props = NULL;

        if (cd->calendar_popup)
                gtk_widget_destroy (cd->calendar_popup);
        cd->calendar_popup = NULL;

        g_free (cd->timeformat);

        g_free (cd->custom_format);

        free_locations (cd);

        if (cd->location_tiles)
                g_list_free (cd->location_tiles);
        cd->location_tiles = NULL;

        if (cd->systz) {
                g_object_unref (cd->systz);
                cd->systz = NULL;
        }

        if (cd->cities_store) {
                g_object_unref (cd->cities_store);
                cd->cities_store = NULL;
        }

        if (cd->builder) {
                g_object_unref (cd->builder);
                cd->builder = NULL;
        }

        g_free (cd);
}

static gboolean
close_on_escape (GtkWidget       *widget,
                 GdkEventKey     *event,
                 GtkToggleButton *toggle_button)
{
        if (event->keyval == GDK_KEY_Escape) {
                gtk_toggle_button_set_active (toggle_button, FALSE);
                return TRUE;
        }

        return FALSE;
}

static gboolean
delete_event (GtkWidget       *widget,
              GdkEvent        *event,
              GtkToggleButton *toggle_button)
{
        gtk_toggle_button_set_active (toggle_button, FALSE);
        return TRUE;
}

static void
edit_locations_cb (CalendarWindow *calwin, gpointer data)
{
        ClockData *cd;

        cd = data;

        display_properties_dialog (cd, TRUE);
}

static GtkWidget *
create_calendar (ClockData *cd)
{
        GtkWidget *window;
        char      *prefs_path;

        prefs_path = mate_panel_applet_get_preferences_path (MATE_PANEL_APPLET (cd->applet));
        window = calendar_window_new (&cd->current_time,
                                      prefs_path,
                                      cd->orient == MATE_PANEL_APPLET_ORIENT_UP);
        g_free (prefs_path);

        calendar_window_set_show_weeks (CALENDAR_WINDOW (window),
                                        cd->showweek);

        gtk_window_set_screen (GTK_WINDOW (window),
                               gtk_widget_get_screen (cd->applet));

        g_signal_connect (window, "edit-locations",
                          G_CALLBACK (edit_locations_cb), cd);

        g_signal_connect (window, "delete_event",
                          G_CALLBACK (delete_event), cd->panel_button);
        g_signal_connect (window, "key_press_event",
                          G_CALLBACK (close_on_escape), cd->panel_button);

        /*Name this window so the default theme can be overridden in panel theme,
        otherwise default GtkWindow bg will be pulled in and override transparency */
        gtk_widget_set_name(window, "MatePanelPopupWindow");

        /* Make transparency possible in the theme */
        GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(window));
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        gtk_widget_set_visual(GTK_WIDGET(window), visual);

        return window;
}

static void
position_calendar_popup (ClockData *cd)
{
#ifdef HAVE_X11
        GtkRequisition  req;
        GtkAllocation   allocation;
        GdkDisplay     *display;
        GdkScreen      *screen;
        GdkRectangle    monitor;
        GdkGravity      gravity = GDK_GRAVITY_NORTH_WEST;
        int             button_w, button_h;
        int             x, y;
        int             w, h;
        int             i, n;
        gboolean        found_monitor = FALSE;

        if (!GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
                return;

        /* Get root origin of the toggle button, and position above that. */
        gdk_window_get_origin (gtk_widget_get_window (cd->panel_button),
                               &x, &y);

        gtk_window_get_size (GTK_WINDOW (cd->calendar_popup), &w, &h);
        gtk_widget_get_preferred_size (cd->calendar_popup, &req, NULL);
        w = req.width;
        h = req.height;

        gtk_widget_get_allocation (cd->panel_button, &allocation);
        button_w = allocation.width;
        button_h = allocation.height;

        screen = gtk_window_get_screen (GTK_WINDOW (cd->calendar_popup));
        display = gdk_screen_get_display (screen);

        n = gdk_display_get_n_monitors (display);
        for (i = 0; i < n; i++) {
                gdk_monitor_get_geometry (gdk_display_get_monitor (display, i), &monitor);
                if (x >= monitor.x && x <= monitor.x + monitor.width &&
                    y >= monitor.y && y <= monitor.y + monitor.height) {
                        found_monitor = TRUE;
                        break;
                }
        }

        if (!found_monitor) {
                /* eek, we should be on one of those xinerama
                   monitors */
                monitor.x = 0;
                monitor.y = 0;
                monitor.width = WidthOfScreen (gdk_x11_screen_get_xscreen (screen));
                monitor.height = HeightOfScreen (gdk_x11_screen_get_xscreen (screen));
        }

        /* Based on panel orientation, position the popup.
         * Ignore window gravity since the window is undecorated.
         * The orientations are all named backward from what
         * I expected.
         */
        switch (cd->orient) {
        case MATE_PANEL_APPLET_ORIENT_RIGHT:
                x += button_w;
                if ((y + h) > monitor.y + monitor.height)
                        y -= (y + h) - (monitor.y + monitor.height);

                if ((y + h) > (monitor.height / 2))
                        gravity = GDK_GRAVITY_SOUTH_WEST;
                else
                        gravity = GDK_GRAVITY_NORTH_WEST;

                break;
        case MATE_PANEL_APPLET_ORIENT_LEFT:
                x -= w;
                if ((y + h) > monitor.y + monitor.height)
                        y -= (y + h) - (monitor.y + monitor.height);

                if ((y + h) > (monitor.height / 2))
                        gravity = GDK_GRAVITY_SOUTH_EAST;
                else
                        gravity = GDK_GRAVITY_NORTH_EAST;

                break;
        case MATE_PANEL_APPLET_ORIENT_DOWN:
                y += button_h;
                if ((x + w) > monitor.x + monitor.width)
                        x -= (x + w) - (monitor.x + monitor.width);

                gravity = GDK_GRAVITY_NORTH_WEST;

                break;
        case MATE_PANEL_APPLET_ORIENT_UP:
                y -= h;
                if ((x + w) > monitor.x + monitor.width)
                        x -= (x + w) - (monitor.x + monitor.width);

                gravity = GDK_GRAVITY_SOUTH_WEST;

                break;
        }

        gtk_window_move (GTK_WINDOW (cd->calendar_popup), x, y);
        gtk_window_set_gravity (GTK_WINDOW (cd->calendar_popup), gravity);
#endif
}

static void
add_to_group (GtkWidget *child, gpointer data)
{
        GtkSizeGroup *group = data;

        gtk_size_group_add_widget (group, child);
}

static void
create_clock_window (ClockData *cd)
{
        GtkWidget *locations_box;

        locations_box = calendar_window_get_locations_box (CALENDAR_WINDOW (cd->calendar_popup));
        gtk_widget_show (locations_box);

        cd->clock_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_add (GTK_CONTAINER (locations_box), cd->clock_vbox);

        cd->clock_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

        gtk_container_foreach (GTK_CONTAINER (locations_box),
                               (GtkCallback) add_to_group,
                               cd->clock_group);
}

static gint
sort_locations_by_name (gconstpointer a, gconstpointer b)
{
        ClockLocation *loc_a = (ClockLocation *) a;
        ClockLocation *loc_b = (ClockLocation *) b;

        const char *name_a = clock_location_get_display_name (loc_a);
        const char *name_b = clock_location_get_display_name (loc_b);

        return strcmp (name_a, name_b);
}

static void
create_cities_store (ClockData *cd)
{
        GtkTreeIter iter;
        GList *cities = cd->locations;
        GList *list = NULL;
        GList *l;

        if (cd->cities_store) {
                g_object_unref (G_OBJECT (cd->cities_store));
                cd->cities_store = NULL;
        }

        /* City name, Timezone name, Coordinates in lat/long */
        cd->cities_store = gtk_list_store_new (COL_CITY_LAST,
                                               G_TYPE_STRING,                /* COL_CITY_NAME */
                                               G_TYPE_STRING,                /* COL_CITY_TZ */
                                               CLOCK_LOCATION_TYPE);         /* COL_CITY_LOC */

        list = g_list_copy (cities);
        list = g_list_sort (list, sort_locations_by_name);

        for (l = list; l; l = l->next) {
                ClockLocation *loc = CLOCK_LOCATION (l->data);

                gtk_list_store_append (cd->cities_store, &iter);
                gtk_list_store_set (cd->cities_store, &iter,
                                    COL_CITY_NAME, clock_location_get_display_name (loc),
                                    /* FIXME: translate the timezone */
                                    COL_CITY_TZ, clock_location_get_timezone (loc),
                                    COL_CITY_LOC, loc,
                                    -1);
        }
        g_list_free (list);


        if (cd->prefs_window) {
                GtkWidget *widget = _clock_get_widget (cd, "cities_list");
                gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
                GTK_TREE_MODEL (cd->cities_store));
        }
}

static gint
sort_locations_by_time (gconstpointer a, gconstpointer b)
{
        ClockLocation *loc_a = (ClockLocation *) a;
        ClockLocation *loc_b = (ClockLocation *) b;

        struct tm tm_a;
        struct tm tm_b;
        gint ret;

        clock_location_localtime (loc_a, &tm_a);
        clock_location_localtime (loc_b, &tm_b);

        ret = (tm_a.tm_year == tm_b.tm_year) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_year < tm_b.tm_year) ? -1 : 1;
        }

        ret = (tm_a.tm_mon == tm_b.tm_mon) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_mon < tm_b.tm_mon) ? -1 : 1;
        }

        ret = (tm_a.tm_mday == tm_b.tm_mday) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_mday < tm_b.tm_mday) ? -1 : 1;
        }

        ret = (tm_a.tm_hour == tm_b.tm_hour) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_hour < tm_b.tm_hour) ? -1 : 1;
        }

        ret = (tm_a.tm_min == tm_b.tm_min) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_min < tm_b.tm_min) ? -1 : 1;
        }

        ret = (tm_a.tm_sec == tm_b.tm_sec) ? 0 : 1;
        if (ret) {
                return (tm_a.tm_sec < tm_b.tm_sec) ? -1 : 1;
        }

        return ret;
}

static void
location_tile_pressed_cb (ClockLocationTile *tile, gpointer data)
{
        ClockData *cd = data;
        ClockLocation *loc;

        loc = clock_location_tile_get_location (tile);

        clock_map_blink_location (CLOCK_MAP (cd->map_widget), loc);

        g_object_unref (loc);
}

static ClockFormat
location_tile_need_clock_format_cb(ClockLocationTile *tile, gpointer data)
{
        ClockData *cd = data;

        return cd->format;
}

static void
create_cities_section (ClockData *cd)
{
        GList *node;
        ClockLocationTile *city;
        GList *cities;
        GList *l;

        if (cd->cities_section) {
                gtk_widget_destroy (cd->cities_section);
                cd->cities_section = NULL;
        }

        if (cd->location_tiles)
                g_list_free (cd->location_tiles);
        cd->location_tiles = NULL;

        cd->cities_section = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width (GTK_CONTAINER (cd->cities_section), 0);

        cities = cd->locations;
        if (g_list_length (cities) == 0) {
                /* if the list is empty, don't bother showing the
                   cities section */
                gtk_widget_hide (cd->cities_section);
                return;
        }

        /* Copy the existing list, so we can sort it nondestructively */
        node = g_list_copy (cities);
        node = g_list_sort (node, sort_locations_by_time);
        node = g_list_reverse (node);

        for (l = node; l; l = g_list_next (l)) {
                ClockLocation *loc = l->data;

                city = clock_location_tile_new (loc, CLOCK_FACE_SMALL);
                g_signal_connect (city, "tile-pressed",
                                  G_CALLBACK (location_tile_pressed_cb), cd);
                g_signal_connect (city, "need-clock-format",
                                  G_CALLBACK (location_tile_need_clock_format_cb), cd);

                gtk_box_pack_start (GTK_BOX (cd->cities_section),
                                    GTK_WIDGET (city),
                                    FALSE, FALSE, 0);

                cd->location_tiles = g_list_prepend (cd->location_tiles, city);

                clock_location_tile_refresh (city, TRUE);
        }

        g_list_free (node);

        gtk_box_pack_end (GTK_BOX (cd->clock_vbox),
                          cd->cities_section, FALSE, FALSE, 0);

        gtk_widget_show_all (cd->cities_section);
}

static GList *
map_need_locations_cb (ClockMap *map, gpointer data)
{
        ClockData *cd = data;

        return cd->locations;
}

static void
create_map_section (ClockData *cd)
{
        ClockMap *map;

        if (cd->map_widget) {
                gtk_widget_destroy (cd->map_widget);
                cd->map_widget = NULL;
        }

        map = clock_map_new ();
        g_signal_connect (map, "need-locations",
                          G_CALLBACK (map_need_locations_cb), cd);

        cd->map_widget = GTK_WIDGET (map);

        gtk_widget_set_margin_top (cd->map_widget, 1);
        gtk_widget_set_margin_bottom (cd->map_widget, 1);
        gtk_widget_set_margin_start (cd->map_widget, 1);
        gtk_widget_set_margin_end (cd->map_widget, 1);

        gtk_box_pack_start (GTK_BOX (cd->clock_vbox), cd->map_widget, TRUE, TRUE, 0);
        gtk_widget_show (cd->map_widget);
}

static void
update_calendar_popup (ClockData *cd)
{
        if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->panel_button))) {
                if (cd->calendar_popup) {
                        gtk_widget_destroy (cd->calendar_popup);
                        cd->calendar_popup = NULL;
                        cd->cities_section = NULL;
                        cd->map_widget = NULL;
                        cd->clock_vbox = NULL;

                        if (cd->location_tiles)
                                g_list_free (cd->location_tiles);
                        cd->location_tiles = NULL;
                }
                update_tooltip (cd);
                return;
        }

        if (!cd->calendar_popup) {
                cd->calendar_popup = create_calendar (cd);
                g_object_add_weak_pointer (G_OBJECT (cd->calendar_popup),
                                           (gpointer *) &cd->calendar_popup);
                update_tooltip (cd);

                create_clock_window (cd);
                create_cities_store (cd);
                create_cities_section (cd);
                create_map_section (cd);
        }

        if (cd->calendar_popup && gtk_widget_get_realized (cd->panel_button)) {
                calendar_window_refresh (CALENDAR_WINDOW (cd->calendar_popup));
                position_calendar_popup (cd);
                gtk_window_present (GTK_WINDOW (cd->calendar_popup));
        }
}

static void
toggle_calendar (GtkWidget *button,
                 ClockData *cd)
{
        /* if time is wrong, the user might try to fix it by clicking on the
         * clock */
        refresh_click_timeout_time_only (cd);
        update_calendar_popup (cd);
}

static gboolean
do_not_eat_button_press (GtkWidget      *widget,
                         GdkEventButton *event)
{
        if (event->button != 1)
                g_signal_stop_emission_by_name (widget, "button_press_event");

        return FALSE;
}

/* Don't request smaller size then the last one we did, this avoids
   jumping when proportional fonts are used.  We must take care to
   call "unfix_size" whenever options are changed or such where
   we'd want to forget the fixed size */
/*FIXME-this cannot be used because size request gsignal invalid for label */
/*
static void
clock_size_request (GtkWidget *clock, GtkRequisition *req, gpointer data)
{
        ClockData *cd = data;

        if (req->width > cd->fixed_width)
                cd->fixed_width = req->width;
        if (req->height > cd->fixed_height)
                cd->fixed_height = req->height;
        req->width = cd->fixed_width;
        req->height = cd->fixed_height;
}
*/
static void
clock_update_text_gravity (GtkWidget *label)
{
        PangoLayout  *layout;
        PangoContext *context;

        layout = gtk_label_get_layout (GTK_LABEL (label));
        context = pango_layout_get_context (layout);
        pango_context_set_base_gravity (context, PANGO_GRAVITY_AUTO);
}

static inline void
force_no_button_vertical_padding (GtkWidget *widget)
{
        GtkCssProvider  *provider;

        provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (provider,
                                         "#clock-applet-button {\n"
                                         "padding-top: 0px;\n"
                                         "padding-bottom: 0px;\n"
                                         "padding-left: 4px;\n"
                                         "padding-right: 4px;\n"
                                         "}",
                                         -1, NULL);
        gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (provider);


        gtk_widget_set_name (widget, "clock-applet-button");
}

static GtkWidget *
create_main_clock_button (void)
{
        GtkWidget *button;

        button = gtk_toggle_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

        force_no_button_vertical_padding (button);

        return button;
}

static GtkWidget *
create_main_clock_label (ClockData *cd)
{
        GtkWidget *label;

        label = gtk_label_new (NULL);
/*Fixme-this is invalid for labels with any recent GTK3 version, maybe all of them*/
/*
        g_signal_connect (label, "size_request",
                          G_CALLBACK (clock_size_request),
                          cd);
*/
        g_signal_connect_swapped (label, "style_set",
                                  G_CALLBACK (unfix_size),
                                  cd);
        gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
        clock_update_text_gravity (label);
        g_signal_connect (label, "screen-changed",
                          G_CALLBACK (clock_update_text_gravity),
                          NULL);

        return label;
}

static gboolean
weather_tooltip (GtkWidget   *widget,
                 gint         x,
                 gint         y,
                 gboolean     keyboard_mode,
                 GtkTooltip  *tooltip,
                 ClockData   *cd)
{
        GList *locations, *l;
        WeatherInfo *info;

        locations = cd->locations;

        for (l = locations; l; l = l->next) {
                ClockLocation *location = l->data;
                if (clock_location_is_current (location)) {
                        info = clock_location_get_weather_info (location);
                        if (!info || !weather_info_is_valid (info))
                                continue;

                        weather_info_setup_tooltip (info, location, tooltip, cd->format);

                        return TRUE;
                }
        }

        return FALSE;
}

static void
create_clock_widget (ClockData *cd)
{
        /* Main toggle button */
        cd->panel_button = create_main_clock_button ();
        g_signal_connect (cd->panel_button, "button_press_event",
                          G_CALLBACK (do_not_eat_button_press), NULL);
        g_signal_connect (cd->panel_button, "toggled",
                          G_CALLBACK (toggle_calendar), cd);
        g_signal_connect (G_OBJECT (cd->panel_button), "destroy",
                          G_CALLBACK (destroy_clock),
                          cd);
        gtk_widget_show (cd->panel_button);

        /* Main orientable box */
        cd->main_obox = g_object_new (clock_box_get_type (), NULL);
        gtk_box_set_spacing (GTK_BOX (cd->main_obox), 12); /* spacing between weather and time */
        gtk_container_add (GTK_CONTAINER (cd->panel_button), cd->main_obox);
        gtk_widget_show (cd->main_obox);

        /* Weather orientable box */
        cd->weather_obox = g_object_new (clock_box_get_type (), NULL);
        gtk_box_set_spacing (GTK_BOX (cd->weather_obox), 2); /* spacing between weather icon and temperature */
        gtk_box_pack_start (GTK_BOX (cd->main_obox), cd->weather_obox, FALSE, FALSE, 0);
        gtk_widget_set_has_tooltip (cd->weather_obox, TRUE);
        g_signal_connect (cd->weather_obox, "query-tooltip",
                          G_CALLBACK (weather_tooltip), cd);

        /* Weather widgets */
        cd->panel_weather_icon = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (cd->weather_obox), cd->panel_weather_icon, FALSE, FALSE, 0);

        cd->panel_temperature_label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (cd->weather_obox), cd->panel_temperature_label, FALSE, FALSE, 0);

        /* Main label for time display */
        cd->clockw = create_main_clock_label (cd);
        gtk_box_pack_start (GTK_BOX (cd->main_obox), cd->clockw, FALSE, FALSE, 0);
        gtk_widget_show (cd->clockw);

        /* Done! */

        set_atk_name_description (GTK_WIDGET (cd->applet), NULL,
                                  _("Computer Clock"));

        gtk_container_add (GTK_CONTAINER (cd->applet), cd->panel_button);
        gtk_container_set_border_width (GTK_CONTAINER (cd->applet), 0);

        cd->props = NULL;
        cd->orient = -1;
        cd->size = mate_panel_applet_get_size (MATE_PANEL_APPLET (cd->applet));

        update_panel_weather (cd);

        /* Refresh the clock so that it paints its first state */
        refresh_clock_timeout (cd);
        applet_change_orient (MATE_PANEL_APPLET (cd->applet),
                              mate_panel_applet_get_orient (MATE_PANEL_APPLET (cd->applet)),
                              cd);
}

static void
update_orient (ClockData *cd)
{
        const gchar   *text;
        int            min_width;
        GtkAllocation  allocation;
        gdouble        new_angle;
        gdouble        angle;

        text = gtk_label_get_text (GTK_LABEL (cd->clockw));
        min_width = calculate_minimum_width (cd->panel_button, text);
        gtk_widget_get_allocation (cd->panel_button, &allocation);

        if (cd->orient == MATE_PANEL_APPLET_ORIENT_LEFT &&
            min_width > allocation.width)
                new_angle = 270;
        else if (cd->orient == MATE_PANEL_APPLET_ORIENT_RIGHT &&
                 min_width > allocation.width)
                new_angle = 90;
        else
                new_angle = 0;

        angle = gtk_label_get_angle (GTK_LABEL (cd->clockw));
        if (angle != new_angle) {
                unfix_size (cd);
                gtk_label_set_angle (GTK_LABEL (cd->clockw), new_angle);
                gtk_label_set_angle (GTK_LABEL (cd->panel_temperature_label), new_angle);
        }
}

/* this is when the panel orientation changes */
static void
applet_change_orient (MatePanelApplet       *applet,
                      MatePanelAppletOrient  orient,
                      ClockData         *cd)
{
        GtkOrientation o;

        if (orient == cd->orient)
                return;

        cd->orient = orient;

        switch (cd->orient) {
        case MATE_PANEL_APPLET_ORIENT_RIGHT:
                o = GTK_ORIENTATION_VERTICAL;
                break;
        case MATE_PANEL_APPLET_ORIENT_LEFT:
                o = GTK_ORIENTATION_VERTICAL;
                break;
        case MATE_PANEL_APPLET_ORIENT_DOWN:
                o = GTK_ORIENTATION_HORIZONTAL;
                break;
        case MATE_PANEL_APPLET_ORIENT_UP:
                o = GTK_ORIENTATION_HORIZONTAL;
                break;
        default:
                g_assert_not_reached ();
                return;
        }

        gtk_orientable_set_orientation (GTK_ORIENTABLE (cd->main_obox), o);
        gtk_orientable_set_orientation (GTK_ORIENTABLE (cd->weather_obox), o);

        unfix_size (cd);
        update_clock (cd);
        update_calendar_popup (cd);
}

/* this is when the panel size changes */
static void
panel_button_change_pixel_size (GtkWidget     *widget,
                                GtkAllocation *allocation,
                                ClockData        *cd)
{
        int new_size;

        if (cd->old_allocation.width  == allocation->width &&
            cd->old_allocation.height == allocation->height)
                return;

        cd->old_allocation.width  = allocation->width;
        cd->old_allocation.height = allocation->height;

        if (cd->orient == MATE_PANEL_APPLET_ORIENT_LEFT ||
            cd->orient == MATE_PANEL_APPLET_ORIENT_RIGHT)
                new_size = allocation->width;
        else
                new_size = allocation->height;

        cd->size = new_size;

        unfix_size (cd);
        update_timeformat (cd);
        update_clock (cd);
}

static void
copy_time (GtkAction *action,
           ClockData *cd)
{
        char string[256];
        char *utf8;

        if (cd->format == CLOCK_FORMAT_UNIX) {
                g_snprintf (string, sizeof(string), "%lu",
                            (unsigned long)cd->current_time);
        } else if (cd->format == CLOCK_FORMAT_INTERNET) {
                float itime = get_itime (cd->current_time);
                if (cd->showseconds)
                        g_snprintf (string, sizeof (string), "@%3.2f", itime);
                else
                        g_snprintf (string, sizeof (string), "@%3d",
                                    (unsigned int) itime);
        } else {
                struct tm *tm;
                char      *format;

                if (cd->format == CLOCK_FORMAT_CUSTOM) {
                        format = g_locale_from_utf8 (cd->custom_format, -1,
                                                     NULL, NULL, NULL);
                } else if (cd->format == CLOCK_FORMAT_12) {
                        if (cd->showseconds)
                                /* Translators: This is a strftime format
                                 * string.
                                 * It is used to display the time in 12-hours
                                 * format with a leading 0 if needed (eg, like
                                 * in the US: 08:10 am). The %p expands to
                                 * am/pm. */
                                format = g_locale_from_utf8 (_("%I:%M:%S %p"), -1, NULL, NULL, NULL);
                        else
                                /* Translators: This is a strftime format
                                 * string.
                                 * It is used to display the time in 12-hours
                                 * format with a leading 0 if needed (eg, like
                                 * in the US: 08:10 am). The %p expands to
                                 * am/pm. */
                                format = g_locale_from_utf8 (_("%I:%M %p"), -1, NULL, NULL, NULL);
                } else {
                        if (cd->showseconds)
                                /* Translators: This is a strftime format
                                 * string.
                                 * It is used to display the time in 24-hours
                                 * format (eg, like in France: 20:10). */
                                format = g_locale_from_utf8 (_("%H:%M:%S"), -1, NULL, NULL, NULL);
                        else
                                /* Translators: This is a strftime format
                                 * string.
                                 * It is used to display the time in 24-hours
                                 * format (eg, like in France: 20:10). */
                                format = g_locale_from_utf8 (_("%H:%M"), -1, NULL, NULL, NULL);
                }

                tm = localtime (&cd->current_time);

                if (!format)
                        strcpy (string, "???");
                else if (strftime (string, sizeof (string), format, tm) == 0)
                        strcpy (string, "???");
                g_free (format);
        }

        utf8 = g_locale_to_utf8 (string, -1, NULL, NULL, NULL);
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_PRIMARY),
                                utf8, -1);
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
                                utf8, -1);
        g_free (utf8);
}

static void
copy_date (GtkAction *action,
           ClockData *cd)
{
        struct tm *tm;
        char string[256];
        char *utf8, *loc;

        tm = localtime (&cd->current_time);

        /* Translators: This is a strftime format string.
         * It is used to display a date in the full format (so that people can
         * copy and paste it elsewhere). */
        loc = g_locale_from_utf8 (_("%A, %B %d %Y"), -1, NULL, NULL, NULL);
        if (!loc)
                strcpy (string, "???");
        else if (strftime (string, sizeof (string), loc, tm) == 0)
                strcpy (string, "???");
        g_free (loc);

        utf8 = g_locale_to_utf8 (string, -1, NULL, NULL, NULL);
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_PRIMARY),
                                utf8, -1);
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
                                utf8, -1);
        g_free (utf8);
}

static void
update_set_time_button (ClockData *cd)
{
        gint can_set;

        /* this returns more than just a boolean; check the documentation of
         * the dbus method for more information */
        can_set = can_set_system_time ();

        if (cd->time_settings_button)
                gtk_widget_set_sensitive (cd->time_settings_button, can_set);

        if (cd->set_time_button) {
                gtk_widget_set_sensitive (cd->set_time_button, can_set != 0);
                gtk_button_set_label (GTK_BUTTON (cd->set_time_button),
                                      can_set == 1 ?
                                        _("Set System Time...") :
                                        _("Set System Time"));
        }
}

static void
set_time_callback (ClockData *cd, GError *error)
{
        GtkWidget *window;
        GtkWidget *dialog;

        if (error) {
                dialog = gtk_message_dialog_new (NULL,
                                                 0,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 _("Failed to set the system time"));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
                g_signal_connect (dialog, "response",
                                  G_CALLBACK (gtk_widget_destroy), NULL);
                gtk_window_present (GTK_WINDOW (dialog));
        }
        else
                update_set_time_button (cd);

        window = _clock_get_widget (cd, "set-time-window");
        gtk_widget_hide (window);
}

static void
set_time (GtkWidget *widget, ClockData *cd)
{
        struct tm t;
        time_t tim;
        guint year, month, day;

        time (&tim);
        /* sets t.isdst -- we could set it to -1 to have mktime() guess the
         * right value , but we don't know if this works with all libc */
        localtime_r (&tim, &t);

        t.tm_sec = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (cd->seconds_spin));
        t.tm_min = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (cd->minutes_spin));
        t.tm_hour = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (cd->hours_spin));
        gtk_calendar_get_date (GTK_CALENDAR (cd->calendar), &year, &month, &day);
        t.tm_year = year - 1900;
        t.tm_mon = month;
        t.tm_mday = day;

        tim = mktime (&t);

        set_system_time_async (tim, (GFunc)set_time_callback, cd, NULL);
}

static void
cancel_time_settings (GtkWidget *button, ClockData *cd)
{
        gtk_widget_hide (cd->set_time_window);

        refresh_click_timeout_time_only (cd);
}

static gboolean
delete_time_settings (GtkWidget *widget, GdkEvent *event, gpointer data)
{
        cancel_time_settings (widget, data);

        return TRUE;
}

static void
fill_time_settings_window (ClockData *cd)
{
        time_t now_t;
        struct tm now;

        /* Fill the time settings */
        tzset ();
        time (&now_t);
        localtime_r (&now_t, &now);

        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cd->seconds_spin), now.tm_sec);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cd->minutes_spin), now.tm_min);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cd->hours_spin), now.tm_hour);

        gtk_calendar_select_month (GTK_CALENDAR (cd->calendar), now.tm_mon,
                                   now.tm_year + 1900);
        gtk_calendar_select_day (GTK_CALENDAR (cd->calendar), now.tm_mday);
}

static void
wrap_cb (GtkSpinButton *spin, ClockData *cd)
{
        gdouble value;
        gdouble min, max;
        GtkSpinType direction;

        value = gtk_spin_button_get_value (spin);
        gtk_spin_button_get_range (spin, &min, &max);

        if (value == min)
                direction = GTK_SPIN_STEP_FORWARD;
        else
                direction = GTK_SPIN_STEP_BACKWARD;

        if (spin == (GtkSpinButton *) cd->seconds_spin)
                gtk_spin_button_spin (GTK_SPIN_BUTTON (cd->minutes_spin), direction, 1.0);
        else if (spin == (GtkSpinButton *) cd->minutes_spin)
                gtk_spin_button_spin (GTK_SPIN_BUTTON (cd->hours_spin), direction, 1.0);
        else {
                guint year, month, day;
                GDate *date;

                gtk_calendar_get_date (GTK_CALENDAR (cd->calendar), &year, &month, &day);

                date = g_date_new_dmy (day, month + 1, year);

                if (direction == GTK_SPIN_STEP_FORWARD)
                        g_date_add_days (date, 1);
                else
                        g_date_subtract_days (date, 1);

                year = g_date_get_year (date);
                month = g_date_get_month (date) - 1;
                day = g_date_get_day (date);

                gtk_calendar_select_month (GTK_CALENDAR (cd->calendar), month, year);
                gtk_calendar_select_day (GTK_CALENDAR (cd->calendar), day);

                g_date_free (date);
        }
}

static gboolean
output_cb (GtkSpinButton *spin,
           gpointer       data)
{
        GtkAdjustment *adj;
        gchar *text;
        int value;

        adj = gtk_spin_button_get_adjustment (spin);
        value = (int) gtk_adjustment_get_value (adj);
        text = g_strdup_printf ("%02d", value);
        gtk_entry_set_text (GTK_ENTRY (spin), text);
        g_free (text);

        return TRUE;
}

static void
ensure_time_settings_window_is_created (ClockData *cd)
{
        GtkWidget *cancel_button;

        if (cd->set_time_window)
                return;

        cd->set_time_window = _clock_get_widget (cd, "set-time-window");
        g_signal_connect (cd->set_time_window, "delete_event",
                          G_CALLBACK (delete_time_settings), cd);

        cd->calendar = _clock_get_widget (cd, "calendar");
        cd->hours_spin = _clock_get_widget (cd, "hours_spin");
        cd->minutes_spin = _clock_get_widget (cd, "minutes_spin");
        cd->seconds_spin = _clock_get_widget (cd, "seconds_spin");

        gtk_entry_set_width_chars (GTK_ENTRY (cd->hours_spin), 2);
        gtk_entry_set_width_chars (GTK_ENTRY (cd->minutes_spin), 2);
        gtk_entry_set_width_chars (GTK_ENTRY (cd->seconds_spin), 2);
        gtk_entry_set_alignment (GTK_ENTRY (cd->hours_spin), 1.0);
        gtk_entry_set_alignment (GTK_ENTRY (cd->minutes_spin), 1.0);
        gtk_entry_set_alignment (GTK_ENTRY (cd->seconds_spin), 1.0);
        g_signal_connect (cd->seconds_spin, "wrapped", G_CALLBACK (wrap_cb), cd);
        g_signal_connect (cd->minutes_spin, "wrapped", G_CALLBACK (wrap_cb), cd);
        g_signal_connect (cd->hours_spin, "wrapped", G_CALLBACK (wrap_cb), cd);

        g_signal_connect (cd->minutes_spin, "output", G_CALLBACK (output_cb), cd);
        g_signal_connect (cd->seconds_spin, "output", G_CALLBACK (output_cb), cd);

        cd->set_time_button = _clock_get_widget (cd, "set-time-button");
        g_signal_connect (cd->set_time_button, "clicked", G_CALLBACK (set_time), cd);

        cancel_button = _clock_get_widget (cd, "cancel-set-time-button");
        g_signal_connect (cancel_button, "clicked", G_CALLBACK (cancel_time_settings), cd);

        cd->current_time_label = _clock_get_widget (cd, "current_time_label");
}

static void
run_time_settings (GtkWidget *unused, ClockData *cd)
{
        ensure_time_settings_window_is_created (cd);
        fill_time_settings_window (cd);

        update_set_time_button (cd);

        gtk_window_present (GTK_WINDOW (cd->set_time_window));

        refresh_click_timeout_time_only (cd);
}

static void
config_date (GtkAction *action,
             ClockData *cd)
{
        run_time_settings (NULL, cd);
}

/* current timestamp */
static const GtkActionEntry clock_menu_actions [] = {
        { "ClockPreferences", "document-properties", N_("_Preferences"),
          NULL, NULL,
          G_CALLBACK (verb_display_properties_dialog) },
        { "ClockHelp", "help-browser", N_("_Help"),
          NULL, NULL,
          G_CALLBACK (display_help_dialog) },
        { "ClockAbout", "help-about", N_("_About"),
          NULL, NULL,
          G_CALLBACK (display_about_dialog) },
        { "ClockCopyTime", "edit-copy", N_("Copy _Time"),
          NULL, NULL,
          G_CALLBACK (copy_time) },
        { "ClockCopyDate", "edit-copy", N_("Copy _Date"),
          NULL, NULL,
          G_CALLBACK (copy_date) },
        { "ClockConfig", "preferences-system", N_("Ad_just Date & Time"),
          NULL, NULL,
          G_CALLBACK (config_date) }
};

static void
format_changed (GSettings    *settings,
                gchar        *key,
                ClockData    *clock)
{
        int          new_format;
        new_format = g_settings_get_enum (settings, key);

        if (!clock->can_handle_format_12 && new_format == CLOCK_FORMAT_12)
                new_format = CLOCK_FORMAT_24;

        if (new_format == clock->format)
                return;

        clock->format = new_format;
        refresh_clock_timeout (clock);

        if (clock->calendar_popup != NULL) {
                position_calendar_popup (clock);
        }

}

static void
show_seconds_changed (GSettings    *settings,
                      gchar        *key,
                      ClockData    *clock)
{
        clock->showseconds = g_settings_get_boolean (settings, key);
        refresh_clock_timeout (clock);
}

static void
show_date_changed (GSettings    *settings,
                   gchar        *key,
                   ClockData    *clock)
{
        clock->showdate = g_settings_get_boolean (settings, key);
        update_timeformat (clock);
        refresh_clock (clock);
}

static void
update_panel_weather (ClockData *cd)
{
        if (cd->show_weather)
                gtk_widget_show (cd->panel_weather_icon);
        else
                gtk_widget_hide (cd->panel_weather_icon);

        if (cd->show_temperature)
                gtk_widget_show (cd->panel_temperature_label);
        else
                gtk_widget_hide (cd->panel_temperature_label);

        if ((cd->show_weather || cd->show_temperature) &&
            g_list_length (cd->locations) > 0)
                gtk_widget_show (cd->weather_obox);
        else
                gtk_widget_hide (cd->weather_obox);

        gtk_widget_queue_resize (cd->applet);
}

static void
update_weather_bool_value_and_toggle_from_gsettings (ClockData *cd, gchar *key,
                                                 gboolean *value_loc, const char *widget_name)
{
        GtkWidget *widget;
        gboolean value;

        value = g_settings_get_boolean (cd->settings, key);

        *value_loc = (value != 0);

        widget = _clock_get_widget (cd, widget_name);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
                                      *value_loc);

        update_panel_weather (cd);
}

static void
show_weather_changed (GSettings    *settings,
                      gchar        *key,
                      ClockData    *cd)
{
        update_weather_bool_value_and_toggle_from_gsettings (cd, key, &cd->show_weather, "weather_check");
}

static void
show_temperature_changed (GSettings    *settings,
                          gchar        *key,
                          ClockData    *cd)
{
        update_weather_bool_value_and_toggle_from_gsettings (cd, key, &cd->show_temperature, "temperature_check");
}

static void
weather_icon_updated_cb (MatePanelApplet *applet,
                             gint icon_size,
                             gpointer       data)
{
        ClockData *cd = data;
        GtkIconTheme *theme;
        cairo_surface_t *surface;
        gint icon_scale;

        if (cd->weather_icon_name == NULL)
                return;

        theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (cd->applet)));

        icon_size = mate_panel_applet_get_size (MATE_PANEL_APPLET (cd->applet));
        icon_scale = gtk_widget_get_scale_factor (GTK_WIDGET (cd->applet));
        /*Iterate through the icon sizes so they can be kept sharp*/
        if (icon_size < 22)
                icon_size = 16;
        else if (icon_size < 24)
                icon_size = 22;
        else if (icon_size < 32)
                icon_size = 24;
        else if (icon_size < 48)
                icon_size = 32;
        else
                icon_size = 48;

        surface = gtk_icon_theme_load_surface (theme, cd->weather_icon_name, icon_size, icon_scale,
                                               NULL, GTK_ICON_LOOKUP_GENERIC_FALLBACK |
                                                     GTK_ICON_LOOKUP_FORCE_SIZE,
                                                                          NULL);

        gtk_image_set_from_surface (GTK_IMAGE (cd->panel_weather_icon), surface);

        cairo_surface_destroy (surface);
}

static void
location_weather_updated_cb (ClockLocation *location,
                             WeatherInfo   *info,
                             gpointer       data)
{
        ClockData *cd = data;
        const gchar *temp;
        GtkIconTheme *theme;
        cairo_surface_t *surface;
        gint icon_size, icon_scale;

        if (!info || !weather_info_is_valid (info))
                return;

        if (!clock_location_is_current (location))
                return;

        cd->weather_icon_name = weather_info_get_icon_name (info);
        if (cd->weather_icon_name == NULL)
                return;

        theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (cd->applet)));

        icon_size = mate_panel_applet_get_size (MATE_PANEL_APPLET (cd->applet));
        icon_scale = gtk_widget_get_scale_factor (GTK_WIDGET (cd->applet));

        /*Iterate through the icon sizes so they can be kept sharp*/
        if (icon_size < 22)
                icon_size = 16;
        else if (icon_size < 24)
                icon_size = 22;
        else if (icon_size < 32)
                icon_size = 24;
        else if (icon_size < 48)
                icon_size = 32;
        else
                icon_size = 48;

        surface = gtk_icon_theme_load_surface (theme, cd->weather_icon_name, icon_size, icon_scale,
                                               NULL, GTK_ICON_LOOKUP_GENERIC_FALLBACK |
                                                     GTK_ICON_LOOKUP_FORCE_SIZE,
                                                                          NULL);

        temp = weather_info_get_temp_summary (info);

        gtk_image_set_from_surface (GTK_IMAGE (cd->panel_weather_icon), surface);
        gtk_label_set_text (GTK_LABEL (cd->panel_temperature_label), temp);

        cairo_surface_destroy (surface);
}

static void
location_set_current_cb (ClockLocation *loc,
                         gpointer       data)
{
        ClockData *cd = data;
        WeatherInfo *info;

        info = clock_location_get_weather_info (loc);
        location_weather_updated_cb (loc, info, cd);

        if (cd->map_widget)
                clock_map_refresh (CLOCK_MAP (cd->map_widget));
        update_location_tiles (cd);
        save_cities_store (cd);
}

static void
locations_changed (ClockData *cd)
{
        GList *l;
        ClockLocation *loc;
        glong id;

        if (!cd->locations) {
                if (cd->weather_obox)
                        gtk_widget_hide (cd->weather_obox);
                if (cd->panel_weather_icon)
                        gtk_image_set_from_pixbuf (GTK_IMAGE (cd->panel_weather_icon),
                                                   NULL);
                if (cd->panel_temperature_label)
                        gtk_label_set_text (GTK_LABEL (cd->panel_temperature_label),
                                            "");
        } else {
                if (cd->weather_obox)
                        gtk_widget_show (cd->weather_obox);
        }

        for (l = cd->locations; l; l = l->next) {
                loc = l->data;

                id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (loc), "weather-updated"));
                if (id == 0) {
                        id = g_signal_connect (loc, "weather-updated",
                                                G_CALLBACK (location_weather_updated_cb), cd);
                        g_object_set_data (G_OBJECT (loc), "weather-updated", GINT_TO_POINTER (id));
                        g_signal_connect (loc, "set-current",
                                          G_CALLBACK (location_set_current_cb), cd);
                }
        }

        if (cd->map_widget)
                clock_map_refresh (CLOCK_MAP (cd->map_widget));

        if (cd->clock_vbox)
                create_cities_section (cd);
}


static void
set_locations (ClockData *cd, GList *locations)
{
        free_locations (cd);
        cd->locations = locations;
        locations_changed (cd);
}

typedef struct {
        GList *cities;
        ClockData *cd;
} LocationParserData;

/* Parser for our serialized locations in gsettings */
static void
location_start_element (GMarkupParseContext *context,
                        const gchar *element_name,
                        const gchar **attribute_names,
                        const gchar **attribute_values,
                        gpointer user_data,
                        GError **error)
{
        ClockLocation *loc;
        LocationParserData *data = user_data;
        ClockData *cd = data->cd;
        WeatherPrefs prefs;
        const gchar *att_name;

        gchar *name = NULL;
        gchar *city = NULL;
        gchar *timezone = NULL;
        gfloat latitude = 0.0;
        gfloat longitude = 0.0;
        gchar *code = NULL;
        gboolean current = FALSE;

        int index = 0;

        prefs.temperature_unit = cd->temperature_unit;
        prefs.speed_unit = cd->speed_unit;

        if (strcmp (element_name, "location") != 0) {
                return;
        }

        setlocale (LC_NUMERIC, "POSIX");

        for (att_name = attribute_names[index]; att_name != NULL;
             att_name = attribute_names[++index]) {
                if (strcmp (att_name, "name") == 0) {
                        name = (gchar *)attribute_values[index];
                } else if (strcmp (att_name, "city") == 0) {
                        city = (gchar *)attribute_values[index];
                } else if (strcmp (att_name, "timezone") == 0) {
                        timezone = (gchar *)attribute_values[index];
                } else if (strcmp (att_name, "latitude") == 0) {
                        sscanf (attribute_values[index], "%f", &latitude);
                } else if (strcmp (att_name, "longitude") == 0) {
                        sscanf (attribute_values[index], "%f", &longitude);
                } else if (strcmp (att_name, "code") == 0) {
                        code = (gchar *)attribute_values[index];
                }
                else if (strcmp (att_name, "current") == 0) {
                        if (strcmp (attribute_values[index], "true") == 0) {
                                current = TRUE;
                        }
                }
        }

        setlocale (LC_NUMERIC, "");

        if ((!name && !city) || !timezone) {
                return;
        }

        /* migration from the old configuration, when name == city */
        if (!city)
                city = name;

        loc = clock_location_find_and_ref (cd->locations, name, city,
                                           timezone, latitude, longitude, code);
        if (!loc)
                loc = clock_location_new (name, city, timezone,
                                          latitude, longitude, code, &prefs);

        if (current && clock_location_is_current_timezone (loc))
                clock_location_make_current (loc, NULL, NULL, NULL);

        data->cities = g_list_append (data->cities, loc);
}

static GMarkupParser location_parser = {
        location_start_element, NULL, NULL, NULL, NULL
};

static void
cities_changed (GSettings    *settings,
                gchar        *key,
                ClockData    *cd)
{
        LocationParserData data;
        GSList *cur = NULL;
        GSList *l;

        GMarkupParseContext *context;

        data.cities = NULL;
        data.cd = cd;

        context = g_markup_parse_context_new (&location_parser, 0, &data, NULL);

        cur = mate_panel_applet_settings_get_gslist (settings, key);

        for (l = cur; l; l = l->next) {
                char *str = l->data;
                g_markup_parse_context_parse (context, str, strlen (str), NULL);
        }
        g_slist_free_full (cur, g_free);

        g_markup_parse_context_free (context);

        set_locations (cd, data.cities);
        create_cities_store (cd);
}

static void
update_weather_locations (ClockData *cd)
{
        GList *locations, *l;
        WeatherPrefs prefs = {
                FORECAST_STATE,
                FALSE,
                NULL,
                TEMP_UNIT_CENTIGRADE,
                SPEED_UNIT_MS,
                PRESSURE_UNIT_MB,
                DISTANCE_UNIT_KM
        };

        prefs.temperature_unit = cd->temperature_unit;
        prefs.speed_unit = cd->speed_unit;

        locations = cd->locations;

        for (l = locations; l; l = l->next) {
                clock_location_set_weather_prefs (l->data, &prefs);
        }
}

static void
clock_timezone_changed (SystemTimezone *systz,
                        const char     *new_tz,
                        ClockData      *cd)
{
        /* This will refresh the current location */
        save_cities_store (cd);

        refresh_click_timeout_time_only (cd);
}

static void
temperature_unit_changed (GSettings    *settings,
                          gchar        *key,
                          ClockData    *cd)
{
        cd->temperature_unit = g_settings_get_enum (settings, key);
        if (cd->temperature_unit > 0)
        {
                GtkWidget *widget;
                gint oldvalue;
                widget = _clock_get_widget (cd, "temperature_combo");
                oldvalue = gtk_combo_box_get_active (GTK_COMBO_BOX (widget)) + 2;
                if (oldvalue != cd->speed_unit)
                        gtk_combo_box_set_active (GTK_COMBO_BOX (widget), cd->temperature_unit - 2);
        }
        update_weather_locations (cd);
}

static void
speed_unit_changed (GSettings    *settings,
                    gchar        *key,
                    ClockData    *cd)
{
        cd->speed_unit = g_settings_get_enum (settings, key);
        if (cd->speed_unit > 0)
        {
                GtkWidget *widget;
                gint oldvalue;
                widget = _clock_get_widget (cd, "wind_speed_combo");
                oldvalue = gtk_combo_box_get_active (GTK_COMBO_BOX (widget)) + 2;
                if (oldvalue != cd->speed_unit)
                        gtk_combo_box_set_active (GTK_COMBO_BOX (widget), cd->speed_unit - 2);
        }
        update_weather_locations (cd);
}

static void
custom_format_changed (GSettings    *settings,
                       gchar        *key,
                       ClockData    *clock)
{
        gchar *value;
        value = g_settings_get_string (settings, key);

        g_free (clock->custom_format);
        clock->custom_format = g_strdup (value);

        if (clock->format == CLOCK_FORMAT_CUSTOM)
                refresh_clock (clock);
        g_free (value);
}

static void
show_week_changed (GSettings    *settings,
                   gchar        *key,
                   ClockData    *clock)
{
        gboolean value;

        value = g_settings_get_boolean (settings, key);

        if (clock->showweek == (value != 0))
                return;

        clock->showweek = (value != 0);

        if (clock->calendar_popup != NULL) {
                calendar_window_set_show_weeks (CALENDAR_WINDOW (clock->calendar_popup), clock->showweek);
                position_calendar_popup (clock);
        }
}

static void
setup_gsettings (ClockData *cd)
{
        cd->settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (cd->applet), CLOCK_SCHEMA);

        /* hack to allow users to set custom format in dconf-editor */
        gint format;
        gchar *custom_format;
        format = g_settings_get_enum (cd->settings, KEY_FORMAT);
        custom_format = g_settings_get_string (cd->settings, KEY_CUSTOM_FORMAT);
        g_settings_set_enum (cd->settings, KEY_FORMAT, format);
        g_settings_set_string (cd->settings, KEY_CUSTOM_FORMAT, custom_format);
        if (custom_format != NULL)
                g_free (custom_format);

        g_signal_connect (cd->settings, "changed::" KEY_FORMAT, G_CALLBACK (format_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SHOW_SECONDS, G_CALLBACK (show_seconds_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SHOW_DATE, G_CALLBACK (show_date_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SHOW_WEATHER, G_CALLBACK (show_weather_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SHOW_TEMPERATURE, G_CALLBACK (show_temperature_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_CUSTOM_FORMAT, G_CALLBACK (custom_format_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SHOW_WEEK, G_CALLBACK (show_week_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_CITIES, G_CALLBACK (cities_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_TEMPERATURE_UNIT, G_CALLBACK (temperature_unit_changed), cd);
        g_signal_connect (cd->settings, "changed::" KEY_SPEED_UNIT, G_CALLBACK (speed_unit_changed), cd);
}

static GList *
parse_gsettings_cities (ClockData *cd, gchar **values)
{
        gint i;
        LocationParserData data;
        GMarkupParseContext *context;

        data.cities = NULL;
        data.cd = cd;

        context = g_markup_parse_context_new (&location_parser, 0, &data, NULL);

        if (values) {
            for (i = 0; values[i]; i++) {
                    g_markup_parse_context_parse (context, values[i], strlen(values[i]), NULL);
            }
        }

        g_markup_parse_context_free (context);

        return data.cities;
}

static void
load_gsettings (ClockData *cd)
{
        gchar **values;
        GList *cities = NULL;

        cd->format = g_settings_get_enum (cd->settings, KEY_FORMAT);

        if (cd->format == CLOCK_FORMAT_INVALID)
                cd->format = clock_locale_format ();

        cd->custom_format = g_settings_get_string (cd->settings, KEY_CUSTOM_FORMAT);
        cd->showseconds = g_settings_get_boolean (cd->settings, KEY_SHOW_SECONDS);
        cd->showdate = g_settings_get_boolean (cd->settings, KEY_SHOW_DATE);
        cd->show_weather = g_settings_get_boolean (cd->settings, KEY_SHOW_WEATHER);
        cd->show_temperature = g_settings_get_boolean (cd->settings, KEY_SHOW_TEMPERATURE);
        cd->showweek = g_settings_get_boolean (cd->settings, KEY_SHOW_WEEK);
        cd->timeformat = NULL;

        cd->can_handle_format_12 = (clock_locale_format () == CLOCK_FORMAT_12);
        if (!cd->can_handle_format_12 && cd->format == CLOCK_FORMAT_12)
                cd->format = CLOCK_FORMAT_24;

        cd->temperature_unit = g_settings_get_enum (cd->settings, KEY_TEMPERATURE_UNIT);
        cd->speed_unit = g_settings_get_enum (cd->settings, KEY_SPEED_UNIT);

        values = g_settings_get_strv (cd->settings, KEY_CITIES);

        if (!values || (g_strv_length (values) == 0)) {
                cities = NULL;
        } else {
                cities = parse_gsettings_cities (cd, values);
        }
        g_strfreev (values);

        set_locations (cd, cities);
}

static gboolean
fill_clock_applet (MatePanelApplet *applet)
{
        ClockData      *cd;
        GtkActionGroup *action_group;
        GtkAction      *action;

        mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);

        cd = g_new0 (ClockData, 1);
        cd->fixed_width = -1;
        cd->fixed_height = -1;

        cd->applet = GTK_WIDGET (applet);

        setup_gsettings (cd);
        load_gsettings (cd);

        cd->builder = gtk_builder_new ();
        gtk_builder_set_translation_domain (cd->builder, GETTEXT_PACKAGE);
        gtk_builder_add_from_resource (cd->builder, CLOCK_RESOURCE_PATH "clock.ui", NULL);

        create_clock_widget (cd);

#ifndef CLOCK_INPROCESS
        gtk_window_set_default_icon_name (CLOCK_ICON);
#endif
        gtk_widget_show (cd->applet);

        /* FIXME: Update this comment. */
        /* we have to bind change_orient before we do applet_widget_add
           since we need to get an initial change_orient signal to set our
           initial oriantation, and we get that during the _add call */
        g_signal_connect (G_OBJECT (cd->applet),
                          "change_orient",
                          G_CALLBACK (applet_change_orient),
                          cd);

        g_signal_connect (G_OBJECT (cd->panel_button),
                          "size_allocate",
                          G_CALLBACK (panel_button_change_pixel_size),
                          cd);

        mate_panel_applet_set_background_widget (MATE_PANEL_APPLET (cd->applet),
                                            GTK_WIDGET (cd->applet));

        action_group = gtk_action_group_new ("ClockApplet Menu Actions");
        gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
        gtk_action_group_add_actions (action_group,
                                      clock_menu_actions,
                                      G_N_ELEMENTS (clock_menu_actions),
                                      cd);
        mate_panel_applet_setup_menu_from_resource (MATE_PANEL_APPLET (cd->applet),
                                                    CLOCK_RESOURCE_PATH "clock-menu.xml",
                                                    action_group);

        if (mate_panel_applet_get_locked_down (MATE_PANEL_APPLET (cd->applet))) {
                action = gtk_action_group_get_action (action_group, "ClockPreferences");
                gtk_action_set_visible (action, FALSE);

                action = gtk_action_group_get_action (action_group, "ClockConfig");
                gtk_action_set_visible (action, FALSE);
        }

        cd->systz = system_timezone_new ();
        g_signal_connect (cd->systz, "changed",
                          G_CALLBACK (clock_timezone_changed), cd);

        action = gtk_action_group_get_action (action_group, "ClockConfig");
        gtk_action_set_visible (action, can_set_system_time ());
        g_object_unref (action_group);

        /* Make sure the weather icon gets updated when the panel size changes*/
        g_signal_connect (cd->applet,
                          "change_size",
                          G_CALLBACK (weather_icon_updated_cb),
                          cd);

        return TRUE;
}

static void
prefs_locations_changed (GtkTreeSelection *selection, ClockData *cd)
{
        gint n;

        n = gtk_tree_selection_count_selected_rows (selection);
        gtk_widget_set_sensitive (cd->prefs_location_edit_button, n > 0);
        gtk_widget_set_sensitive (cd->prefs_location_remove_button, n > 0);
}

static gchar *
loc_to_string (ClockLocation *loc)
{
        const gchar *name, *city;
        gfloat latitude, longitude;
        gchar *ret;

        name = clock_location_get_name (loc);
        city = clock_location_get_city (loc);
        clock_location_get_coords (loc, &latitude, &longitude);

        setlocale (LC_NUMERIC, "POSIX");

        ret = g_markup_printf_escaped
                ("<location name=\"%s\" city=\"%s\" timezone=\"%s\" latitude=\"%f\" longitude=\"%f\" code=\"%s\" current=\"%s\"/>",
                 name ? name : "",
                 city ? city : "",
                 clock_location_get_timezone (loc),
                 latitude, longitude,
                 clock_location_get_weather_code (loc),
                 clock_location_is_current (loc) ? "true" : "false");

        setlocale (LC_NUMERIC, "");

        return ret;
}

static void
save_cities_store (ClockData *cd)
{
        GList *locs = NULL;
        GList *node;

        for (node = cd->locations; node != NULL; node = node->next) {
                locs = g_list_prepend (locs, loc_to_string (CLOCK_LOCATION (node->data)));
        }

        locs = g_list_reverse (locs);
        mate_panel_applet_settings_set_glist (cd->settings, KEY_CITIES, locs);
        g_list_free_full (locs, g_free);
}

static void
run_prefs_edit_save (GtkButton *button, ClockData *cd)
{
        GtkWidget *edit_window = _clock_get_widget (cd, "edit-location-window");

        ClockLocation *loc = g_object_get_data (G_OBJECT (edit_window), "clock-location");

        GtkWidget *lat_entry = _clock_get_widget (cd, "edit-location-latitude-entry");
        GtkWidget *lon_entry = _clock_get_widget (cd, "edit-location-longitude-entry");
        GtkWidget *lat_combo = _clock_get_widget (cd, "edit-location-latitude-combo");
        GtkWidget *lon_combo = _clock_get_widget (cd, "edit-location-longitude-combo");

        const gchar *timezone, *weather_code;
        gchar *city, *name;

        MateWeatherLocation *gloc;
        gfloat lat = 0;
        gfloat lon = 0;

        timezone = mateweather_timezone_menu_get_tzid (cd->zone_combo);
        if (!timezone) {
                edit_hide (NULL, cd);
                return;
        }

        city = NULL;
        weather_code = NULL;
        name = NULL;

        gloc = mateweather_location_entry_get_location (cd->location_entry);
        if (gloc) {
                city = mateweather_location_get_city_name (gloc);
                weather_code = mateweather_location_get_code (gloc);
        }

        if (mateweather_location_entry_has_custom_text (cd->location_entry)) {
                name = gtk_editable_get_chars (GTK_EDITABLE (cd->location_entry), 0, -1);
        }

        sscanf (gtk_entry_get_text (GTK_ENTRY (lat_entry)), "%f", &lat);
        sscanf (gtk_entry_get_text (GTK_ENTRY (lon_entry)), "%f", &lon);

        if (gtk_combo_box_get_active (GTK_COMBO_BOX (lat_combo)) != 0) {
                lat = -lat;
        }

        if (gtk_combo_box_get_active (GTK_COMBO_BOX (lon_combo)) != 0) {
                lon = -lon;
        }

        if (loc) {
                clock_location_set_timezone (loc, timezone);
                clock_location_set_name (loc, name);
                clock_location_set_city (loc, city);
                clock_location_set_coords (loc, lat, lon);
                clock_location_set_weather_code (loc, weather_code);
        } else {
                WeatherPrefs prefs;

                prefs.temperature_unit = cd->temperature_unit;
                prefs.speed_unit = cd->speed_unit;

                loc = clock_location_new (name, city, timezone, lat, lon, weather_code, &prefs);
                /* has the side-effect of setting the current location if
                 * there's none and this one can be considered as a current one
                 */
                clock_location_is_current (loc);

                cd->locations = g_list_append (cd->locations, loc);
        }
        g_free (name);
        g_free (city);

        /* This will update everything related to locations to take into
         * account the new location (via the gsettings changed signal) */
        save_cities_store (cd);

        edit_hide (edit_window, cd);
}

static void
update_coords_helper (gfloat value, GtkWidget *entry, GtkWidget *combo)
{
        gchar *tmp;

        tmp = g_strdup_printf ("%f", fabsf (value));
        gtk_entry_set_text (GTK_ENTRY (entry), tmp);
        g_free (tmp);

        if (value > 0) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
        } else {
                gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 1);
        }
}

static void
update_coords (ClockData *cd, gboolean valid, gfloat lat, gfloat lon)
{
        GtkWidget *lat_entry = _clock_get_widget (cd, "edit-location-latitude-entry");
        GtkWidget *lon_entry = _clock_get_widget (cd, "edit-location-longitude-entry");
        GtkWidget *lat_combo = _clock_get_widget (cd, "edit-location-latitude-combo");
        GtkWidget *lon_combo = _clock_get_widget (cd, "edit-location-longitude-combo");

        if (!valid) {
                gtk_entry_set_text (GTK_ENTRY (lat_entry), "");
                gtk_entry_set_text (GTK_ENTRY (lon_entry), "");
                gtk_combo_box_set_active (GTK_COMBO_BOX (lat_combo), -1);
                gtk_combo_box_set_active (GTK_COMBO_BOX (lon_combo), -1);

                return;
        }

        update_coords_helper (lat, lat_entry, lat_combo);
        update_coords_helper (lon, lon_entry, lon_combo);
}

static void
fill_timezone_combo_from_location (ClockData *cd, ClockLocation *loc)
{
        if (loc != NULL) {
                mateweather_timezone_menu_set_tzid (cd->zone_combo,
                                                 clock_location_get_timezone (loc));
        } else {
                mateweather_timezone_menu_set_tzid (cd->zone_combo, NULL);
        }
}

static void
location_update_ok_sensitivity (ClockData *cd)
{
        GtkWidget *ok_button;
        const gchar *timezone;
        gchar *name;

        ok_button = _clock_get_widget (cd, "edit-location-ok-button");

        timezone = mateweather_timezone_menu_get_tzid (cd->zone_combo);
        name = gtk_editable_get_chars (GTK_EDITABLE (cd->location_entry), 0, -1);

        if (timezone && name && name[0] != '\0') {
                gtk_widget_set_sensitive (ok_button, TRUE);
        } else {
                gtk_widget_set_sensitive (ok_button, FALSE);
        }

        g_free (name);
}

static void
location_changed (GObject *object, GParamSpec *param, ClockData *cd)
{
        MateWeatherLocationEntry *entry = MATEWEATHER_LOCATION_ENTRY (object);
        MateWeatherLocation *gloc;
        MateWeatherTimezone *zone;
        gboolean latlon_valid;
        double latitude = 0.0, longitude = 0.0;

        gloc = mateweather_location_entry_get_location (entry);

        latlon_valid = gloc && mateweather_location_has_coords (gloc);
        if (latlon_valid)
                mateweather_location_get_coords (gloc, &latitude, &longitude);
        update_coords (cd, latlon_valid, latitude, longitude);

        zone = gloc ? mateweather_location_get_timezone (gloc) : NULL;
        if (zone)
                mateweather_timezone_menu_set_tzid (cd->zone_combo, mateweather_timezone_get_tzid (zone));
        else
                mateweather_timezone_menu_set_tzid (cd->zone_combo, NULL);

        if (gloc)
                mateweather_location_unref (gloc);
}

static void
location_name_changed (GObject *object, ClockData *cd)
{
    location_update_ok_sensitivity (cd);
}

static void
location_timezone_changed (GObject *object, GParamSpec *param, ClockData *cd)
{
    location_update_ok_sensitivity (cd);
}

static void
edit_clear (ClockData *cd)
{
        GtkWidget *lat_entry = _clock_get_widget (cd, "edit-location-latitude-entry");
        GtkWidget *lon_entry = _clock_get_widget (cd, "edit-location-longitude-entry");
        GtkWidget *lat_combo = _clock_get_widget (cd, "edit-location-latitude-combo");
        GtkWidget *lon_combo = _clock_get_widget (cd, "edit-location-longitude-combo");

        /* clear out the old data */
        mateweather_location_entry_set_location (cd->location_entry, NULL);
        mateweather_timezone_menu_set_tzid (cd->zone_combo, NULL);

        gtk_entry_set_text (GTK_ENTRY (lat_entry), "");
        gtk_entry_set_text (GTK_ENTRY (lon_entry), "");

        gtk_combo_box_set_active (GTK_COMBO_BOX (lat_combo), -1);
        gtk_combo_box_set_active (GTK_COMBO_BOX (lon_combo), -1);
}

static void
edit_hide (GtkWidget *unused, ClockData *cd)
{
        GtkWidget *edit_window = _clock_get_widget (cd, "edit-location-window");

        gtk_widget_hide (edit_window);
        edit_clear (cd);
}

static gboolean
edit_delete (GtkWidget *unused, GdkEvent *event, ClockData *cd)
{
        edit_hide (unused, cd);

        return TRUE;
}

static gboolean
edit_hide_event (GtkWidget *widget, GdkEvent *event, ClockData *cd)
{
        edit_hide (widget, cd);

        return TRUE;
}

static void
prefs_hide (GtkWidget *widget, ClockData *cd)
{
        GtkWidget *tree;

        edit_hide (widget, cd);

        gtk_widget_hide (cd->prefs_window);

        tree = _clock_get_widget (cd, "cities_list");

        gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree)));

        refresh_click_timeout_time_only (cd);
}

static gboolean
prefs_hide_event (GtkWidget *widget, GdkEvent *event, ClockData *cd)
{
        prefs_hide (widget, cd);

        return TRUE;
}

static void
prefs_help (GtkWidget *widget, ClockData *cd)
{
        clock_utils_display_help (cd->prefs_window,
                                  "mate-clock", "clock-settings");
}

static void
remove_tree_row (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
        ClockData *cd = data;
        ClockLocation *loc = NULL;

        gtk_tree_model_get (model, iter, COL_CITY_LOC, &loc, -1);
        cd->locations = g_list_remove (cd->locations, loc);
        g_object_unref (loc);

        /* This will update everything related to locations to take into
         * account the removed location (via the gsettings changed signal) */
        save_cities_store (cd);
}

static void
run_prefs_locations_remove (GtkButton *button, ClockData *cd)
{
        GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (cd->prefs_locations));

        gtk_tree_selection_selected_foreach (sel, remove_tree_row, cd);
}

static void
run_prefs_locations_add (GtkButton *button, ClockData *cd)
{
        GtkWidget *edit_window = _clock_get_widget (cd, "edit-location-window");

        fill_timezone_combo_from_location (cd, NULL);

        g_object_set_data (G_OBJECT (edit_window), "clock-location", NULL);
        gtk_window_set_title (GTK_WINDOW (edit_window), _("Choose Location"));
        gtk_window_set_transient_for (GTK_WINDOW (edit_window), GTK_WINDOW (cd->prefs_window));

        if (g_object_get_data (G_OBJECT (edit_window), "delete-handler") == NULL) {
                g_object_set_data (G_OBJECT (edit_window), "delete-handler",
                                   GINT_TO_POINTER (g_signal_connect (edit_window, "delete_event", G_CALLBACK (edit_delete), cd)));
        }

        location_update_ok_sensitivity (cd);

        gtk_widget_grab_focus (GTK_WIDGET (cd->location_entry));
        gtk_editable_set_position (GTK_EDITABLE (cd->location_entry), -1);

        gtk_window_present_with_time (GTK_WINDOW (edit_window), gtk_get_current_event_time ());
}

static void
edit_tree_row (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
        ClockData *cd = data;
        ClockLocation *loc;
        const char *name;
        gchar *tmp;
        gfloat lat, lon;

        /* fill the dialog with this location's data, show it */
        GtkWidget *edit_window = _clock_get_widget (cd, "edit-location-window");

        GtkWidget *lat_entry = _clock_get_widget (cd, "edit-location-latitude-entry");

        GtkWidget *lon_entry = _clock_get_widget (cd, "edit-location-longitude-entry");

        GtkWidget *lat_combo = _clock_get_widget (cd, "edit-location-latitude-combo");

        GtkWidget *lon_combo = _clock_get_widget (cd, "edit-location-longitude-combo");

        edit_clear (cd);

        gtk_tree_model_get (model, iter, COL_CITY_LOC, &loc, -1);

        mateweather_location_entry_set_city (cd->location_entry,
                                          clock_location_get_city (loc),
                                          clock_location_get_weather_code (loc));
        name = clock_location_get_name (loc);
        if (name && name[0]) {
                gtk_entry_set_text (GTK_ENTRY (cd->location_entry), name);
        }

        clock_location_get_coords (loc, &lat, &lon);

        fill_timezone_combo_from_location (cd, loc);

        tmp = g_strdup_printf ("%f", fabsf(lat));
        gtk_entry_set_text (GTK_ENTRY (lat_entry), tmp);
        g_free (tmp);

        if (lat > 0) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (lat_combo), 0);
        } else {
                gtk_combo_box_set_active (GTK_COMBO_BOX (lat_combo), 1);
        }

        tmp = g_strdup_printf ("%f", fabsf(lon));
        gtk_entry_set_text (GTK_ENTRY (lon_entry), tmp);
        g_free (tmp);

        if (lon > 0) {
                gtk_combo_box_set_active (GTK_COMBO_BOX (lon_combo), 0);
        } else {
                gtk_combo_box_set_active (GTK_COMBO_BOX (lon_combo), 1);
        }

        location_update_ok_sensitivity (cd);

        g_object_set_data (G_OBJECT (edit_window), "clock-location", loc);

        gtk_widget_grab_focus (GTK_WIDGET (cd->location_entry));
        gtk_editable_set_position (GTK_EDITABLE (cd->location_entry), -1);

        gtk_window_set_title (GTK_WINDOW (edit_window), _("Edit Location"));
        gtk_window_present (GTK_WINDOW (edit_window));
}

static void
run_prefs_locations_edit (GtkButton *unused, ClockData *cd)
{
        GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (cd->prefs_locations));

        gtk_tree_selection_selected_foreach (sel, edit_tree_row, cd);
}

static void
set_12hr_format_radio_cb (GtkWidget *widget, ClockData *cd)
{
        ClockFormat format;

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
                format = CLOCK_FORMAT_12;
        else
                format = CLOCK_FORMAT_24;

        g_settings_set_enum (cd->settings, KEY_FORMAT, format);
}

static void
temperature_combo_changed (GtkComboBox *combo, ClockData *cd)
{
        int value;
        int old_value;

        value = gtk_combo_box_get_active (combo) + 2;
        old_value = cd->temperature_unit;

        if (value == old_value)
                return;

        g_settings_set_enum (cd->settings, KEY_TEMPERATURE_UNIT, value);
}

static void
speed_combo_changed (GtkComboBox *combo, ClockData *cd)
{
        int value;
        int old_value;

        value = gtk_combo_box_get_active (combo) + 2;
        old_value = cd->speed_unit;

        if (value == old_value)
                return;

        g_settings_set_enum (cd->settings, KEY_SPEED_UNIT, value);
}


static void
fill_prefs_window (ClockData *cd)
{
        static const int temperatures[] = {
                TEMP_UNIT_KELVIN,
                TEMP_UNIT_CENTIGRADE,
                TEMP_UNIT_FAHRENHEIT,
                -1
        };

        static const int speeds[] = {
                SPEED_UNIT_MS,
                SPEED_UNIT_KPH,
                SPEED_UNIT_MPH,
                SPEED_UNIT_KNOTS,
                SPEED_UNIT_BFT,
                -1
        };

        GtkWidget *radio_12hr;
        GtkWidget *radio_24hr;
        GtkWidget *widget;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *col;
        GtkListStore *store;
        GtkTreeIter iter;
        int i;

        /* Set the 12 hour / 24 hour widget */
        radio_12hr = _clock_get_widget (cd, "12hr_radio");
        radio_24hr = _clock_get_widget (cd, "24hr_radio");

        if (cd->format == CLOCK_FORMAT_12)
                widget = radio_12hr;
        else
                widget = radio_24hr;

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);

        g_signal_connect (radio_12hr, "toggled",
                          G_CALLBACK (set_12hr_format_radio_cb), cd);

        /* Set the "Show Date" checkbox */
        widget = _clock_get_widget (cd, "date_check");
        g_settings_bind (cd->settings, KEY_SHOW_DATE, widget, "active",
                         G_SETTINGS_BIND_DEFAULT);

        /* Set the "Show Seconds" checkbox */
        widget = _clock_get_widget (cd, "seconds_check");
        g_settings_bind (cd->settings, KEY_SHOW_SECONDS, widget, "active",
                         G_SETTINGS_BIND_DEFAULT);

        /* Set the "Show Week Numbers" checkbox */
        widget = _clock_get_widget (cd, "weeks_check");
        g_settings_bind (cd->settings, KEY_SHOW_WEEK, widget, "active",
        G_SETTINGS_BIND_DEFAULT);

        /* Set the "Show weather" checkbox */
        widget = _clock_get_widget (cd, "weather_check");
        g_settings_bind (cd->settings, KEY_SHOW_WEATHER, widget, "active",
                         G_SETTINGS_BIND_DEFAULT);

        /* Set the "Show temperature" checkbox */
        widget = _clock_get_widget (cd, "temperature_check");
        g_settings_bind (cd->settings, KEY_SHOW_TEMPERATURE, widget, "active",
                         G_SETTINGS_BIND_DEFAULT);

        /* Fill the Cities list */
        widget = _clock_get_widget (cd, "cities_list");

        renderer = gtk_cell_renderer_text_new ();
        col = gtk_tree_view_column_new_with_attributes (_("City Name"), renderer, "text", COL_CITY_NAME, NULL);
        gtk_tree_view_insert_column (GTK_TREE_VIEW (widget), col, -1);

        renderer = gtk_cell_renderer_text_new ();
        col = gtk_tree_view_column_new_with_attributes (_("City Time Zone"), renderer, "text", COL_CITY_TZ, NULL);
        gtk_tree_view_insert_column (GTK_TREE_VIEW (widget), col, -1);

        if (cd->cities_store == NULL)
                create_cities_store (cd);

        gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
                                 GTK_TREE_MODEL (cd->cities_store));

        /* Temperature combo */
        widget = _clock_get_widget (cd, "temperature_combo");
        store = gtk_list_store_new (1, G_TYPE_STRING);
        gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
        renderer = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), renderer, TRUE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), renderer, "text", 0, NULL);

        for (i = 0; temperatures[i] != -1; i++)
                gtk_list_store_insert_with_values (store, &iter, -1,
                                                   0, mateweather_prefs_get_temp_display_name (temperatures[i]),
                                                   -1);

        if (cd->temperature_unit > 0)
                gtk_combo_box_set_active (GTK_COMBO_BOX (widget),
                                          cd->temperature_unit - 2);
        g_signal_connect (widget, "changed",
                          G_CALLBACK (temperature_combo_changed), cd);

        /* Wind speed combo */
        widget = _clock_get_widget (cd, "wind_speed_combo");
        store = gtk_list_store_new (1, G_TYPE_STRING);
        gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
        renderer = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), renderer, TRUE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), renderer, "text", 0, NULL);

        for (i = 0; speeds[i] != -1; i++)
                gtk_list_store_insert_with_values (store, &iter, -1,
                                                   0, mateweather_prefs_get_speed_display_name (speeds[i]),
                                                   -1);

        if (cd->speed_unit > 0)
                gtk_combo_box_set_active (GTK_COMBO_BOX (widget),
                                          cd->speed_unit - 2);
        g_signal_connect (widget, "changed",
                          G_CALLBACK (speed_combo_changed), cd);
}

static void
ensure_prefs_window_is_created (ClockData *cd)
{
        GtkWidget *edit_window;
        GtkWidget *prefs_close_button;
        GtkWidget *prefs_help_button;
        GtkWidget *clock_options;
        GtkWidget *edit_cancel_button;
        GtkWidget *edit_ok_button;
        GtkWidget *location_box;
        GtkWidget *zone_box;
        GtkWidget *location_name_label;
        GtkWidget *timezone_label;
        GtkTreeSelection *selection;
        MateWeatherLocation *world;

        if (cd->prefs_window)
                return;

        cd->prefs_window = _clock_get_widget (cd, "prefs-window");

        gtk_window_set_icon_name (GTK_WINDOW (cd->prefs_window), CLOCK_ICON);

        prefs_close_button = _clock_get_widget (cd, "prefs-close-button");
        prefs_help_button = _clock_get_widget (cd, "prefs-help-button");
        clock_options = _clock_get_widget (cd, "clock-options");
        cd->prefs_locations = GTK_TREE_VIEW (_clock_get_widget (cd, "cities_list"));
        location_name_label = _clock_get_widget (cd, "location-name-label");
        timezone_label = _clock_get_widget (cd, "timezone-label");


        if (!clock_locale_supports_am_pm ())
                gtk_widget_hide (clock_options);

        selection = gtk_tree_view_get_selection (cd->prefs_locations);
        g_signal_connect (G_OBJECT (selection), "changed",
                          G_CALLBACK (prefs_locations_changed), cd);

        g_signal_connect (G_OBJECT (cd->prefs_window), "delete_event",
                          G_CALLBACK (prefs_hide_event), cd);

        g_signal_connect (G_OBJECT (prefs_close_button), "clicked",
                          G_CALLBACK (prefs_hide), cd);

        g_signal_connect (G_OBJECT (prefs_help_button), "clicked",
                          G_CALLBACK (prefs_help), cd);

        cd->prefs_location_remove_button = _clock_get_widget (cd, "prefs-locations-remove-button");

        g_signal_connect (G_OBJECT (cd->prefs_location_remove_button), "clicked",
                          G_CALLBACK (run_prefs_locations_remove), cd);

        cd->prefs_location_add_button = _clock_get_widget (cd, "prefs-locations-add-button");

        g_signal_connect (G_OBJECT (cd->prefs_location_add_button), "clicked",
                          G_CALLBACK (run_prefs_locations_add), cd);

        cd->prefs_location_edit_button = _clock_get_widget (cd, "prefs-locations-edit-button");

        g_signal_connect (G_OBJECT (cd->prefs_location_edit_button), "clicked",
                          G_CALLBACK (run_prefs_locations_edit), cd);

        edit_window = _clock_get_widget (cd, "edit-location-window");

        gtk_window_set_transient_for (GTK_WINDOW (edit_window),
                                      GTK_WINDOW (cd->prefs_window));

        g_signal_connect (G_OBJECT (edit_window), "delete_event",
                          G_CALLBACK (edit_hide_event), cd);

        edit_cancel_button = _clock_get_widget (cd, "edit-location-cancel-button");

        edit_ok_button = _clock_get_widget (cd, "edit-location-ok-button");

        world = mateweather_location_new_world (FALSE);

        location_box = _clock_get_widget (cd, "edit-location-name-box");
        cd->location_entry = MATEWEATHER_LOCATION_ENTRY (mateweather_location_entry_new (world));
        gtk_widget_show (GTK_WIDGET (cd->location_entry));
        gtk_container_add (GTK_CONTAINER (location_box), GTK_WIDGET (cd->location_entry));
        gtk_label_set_mnemonic_widget (GTK_LABEL (location_name_label),
                                       GTK_WIDGET (cd->location_entry));

        g_signal_connect (G_OBJECT (cd->location_entry), "notify::location",
                          G_CALLBACK (location_changed), cd);
        g_signal_connect (G_OBJECT (cd->location_entry), "changed",
                          G_CALLBACK (location_name_changed), cd);

        zone_box = _clock_get_widget (cd, "edit-location-timezone-box");
        cd->zone_combo = MATEWEATHER_TIMEZONE_MENU (mateweather_timezone_menu_new (world));
        gtk_widget_show (GTK_WIDGET (cd->zone_combo));
        gtk_container_add (GTK_CONTAINER (zone_box), GTK_WIDGET (cd->zone_combo));
        gtk_label_set_mnemonic_widget (GTK_LABEL (timezone_label),
                                       GTK_WIDGET (cd->zone_combo));

        g_signal_connect (G_OBJECT (cd->zone_combo), "notify::tzid",
                          G_CALLBACK (location_timezone_changed), cd);

        mateweather_location_unref (world);

        g_signal_connect (G_OBJECT (edit_cancel_button), "clicked",
                          G_CALLBACK (edit_hide), cd);

        g_signal_connect (G_OBJECT (edit_ok_button), "clicked",
                          G_CALLBACK (run_prefs_edit_save), cd);

        /* Set up the time setting section */

        cd->time_settings_button = _clock_get_widget (cd, "time-settings-button");
        g_signal_connect (cd->time_settings_button, "clicked",
                          G_CALLBACK (run_time_settings), cd);

        /* fill it with the current preferences */
        fill_prefs_window (cd);
}

static gboolean
dialog_page_scroll_event_cb (GtkWidget *widget, GdkEventScroll *event, GtkWindow *window)
{
        GtkNotebook *notebook = GTK_NOTEBOOK (widget);
        GtkWidget *child, *event_widget, *action_widget;

        child = gtk_notebook_get_nth_page (notebook, gtk_notebook_get_current_page (notebook));
        if (child == NULL) {
                return FALSE;
        }

        event_widget = gtk_get_event_widget ((GdkEvent *) event);

        /* Ignore scroll events from the content of the page */
        if (event_widget == NULL || event_widget == child || gtk_widget_is_ancestor (event_widget, child)) {
                return FALSE;
        }

        /* And also from the action widgets */
        action_widget = gtk_notebook_get_action_widget (notebook, GTK_PACK_START);
        if (event_widget == action_widget || (action_widget != NULL && gtk_widget_is_ancestor (event_widget, action_widget))) {
                return FALSE;
        }
        action_widget = gtk_notebook_get_action_widget (notebook, GTK_PACK_END);
        if (event_widget == action_widget || (action_widget != NULL && gtk_widget_is_ancestor (event_widget, action_widget))) {
                return FALSE;
        }

        switch (event->direction) {
        case GDK_SCROLL_RIGHT:
        case GDK_SCROLL_DOWN:
                gtk_notebook_next_page (notebook);
                break;
        case GDK_SCROLL_LEFT:
        case GDK_SCROLL_UP:
                gtk_notebook_prev_page (notebook);
                break;
        case GDK_SCROLL_SMOOTH:
                switch (gtk_notebook_get_tab_pos (notebook)) {
                case GTK_POS_LEFT:
                case GTK_POS_RIGHT:
                        if (event->delta_y > 0) {
                                gtk_notebook_next_page (notebook);
                        }
                        else if (event->delta_y < 0) {
                                gtk_notebook_prev_page (notebook);
                        }
                        break;
                case GTK_POS_TOP:
                case GTK_POS_BOTTOM:
                        if (event->delta_x > 0) {
                                gtk_notebook_next_page (notebook);
                        }
                        else if (event->delta_x < 0) {
                                gtk_notebook_prev_page (notebook);
                        }
                        break;
                }
        break;
        }

        return TRUE;
}

static void
display_properties_dialog (ClockData *cd, gboolean start_in_locations_page)
{
        ensure_prefs_window_is_created (cd);

        GtkWidget *notebook = _clock_get_widget (cd, "notebook");
        gtk_widget_add_events (notebook, GDK_SCROLL_MASK);
        g_signal_connect (GTK_NOTEBOOK (notebook), "scroll-event",
                          G_CALLBACK (dialog_page_scroll_event_cb), GTK_WINDOW (cd->prefs_window));
        

        if (start_in_locations_page) {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
        }

        update_set_time_button (cd);

        gtk_window_set_screen (GTK_WINDOW (cd->prefs_window),
                               gtk_widget_get_screen (cd->applet));
        gtk_window_present (GTK_WINDOW (cd->prefs_window));

        refresh_click_timeout_time_only (cd);
}

static void
verb_display_properties_dialog (GtkAction *action,
                                ClockData *cd)
{
        display_properties_dialog (cd, FALSE);
}

static void
display_help_dialog (GtkAction *action,
                     ClockData *cd)
{
        clock_utils_display_help (cd->applet, "mate-clock", NULL);
}

static void display_about_dialog(GtkAction* action, ClockData* cd)
{
        static const gchar* authors[] = {
                "George Lebl <jirka@5z.com>",
                "Gediminas Paulauskas <menesis@delfi.lt>",
                NULL
        };

        static const char* documenters[] = {
                "Dan Mueth <d-mueth@uchicago.edu>",
                NULL
        };

        gtk_show_about_dialog(NULL,
                "program-name", _("Clock"),
                "title", _("About Clock"),
                "authors", authors,
                "comments", _("The Clock displays the current time and date"),
                "copyright", _("Copyright \xc2\xa9 1998-2004 Free Software Foundation, Inc.\n"
                               "Copyright \xc2\xa9 2012-2020 MATE developers"),
                "documenters", documenters,
                "logo-icon-name", CLOCK_ICON,
                "translator-credits", _("translator-credits"),
                "version", VERSION,
                "website", "http://mate-desktop.org/",
                NULL);
}

static gboolean
clock_factory (MatePanelApplet *applet,
               const char  *iid,
               gpointer     data)
{
        gboolean retval = FALSE;

        if (!strcmp (iid, "ClockApplet"))
                retval = fill_clock_applet (applet);

        return retval;
}

#ifdef CLOCK_INPROCESS
MATE_PANEL_APPLET_IN_PROCESS_FACTORY ("ClockAppletFactory",
                                 PANEL_TYPE_APPLET,
                                 "ClockApplet",
                                 clock_factory,
                                 NULL)
#else
MATE_PANEL_APPLET_OUT_PROCESS_FACTORY ("ClockAppletFactory",
                                  PANEL_TYPE_APPLET,
                                  "ClockApplet",
                                  clock_factory,
                                  NULL)
#endif
