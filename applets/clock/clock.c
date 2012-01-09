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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Miguel de Icaza
 *      Frederico Mena
 *      Stuart Parmenter
 *      Alexander Larsson
 *      George Lebl
 *      Gediminas Paulauskas
 *      Mark McLoughlin
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
#include <mate-panel-applet-mateconf.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <mateconf/mateconf-client.h>

#include <libmateweather/mateweather-prefs.h>
#include <libmateweather/mateweather-xml.h>
#include <libmateweather/location-entry.h>
#include <libmateweather/timezone-menu.h>

#ifdef HAVE_LIBECAL
#include <libedataserverui/e-passwords.h>
#endif

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

#define N_MATECONF_PREFS 11 /* Keep this in sync with the number of keys below! */
#define KEY_FORMAT		"format"
#define KEY_SHOW_SECONDS	"show_seconds"
#define KEY_SHOW_DATE		"show_date"
#define KEY_SHOW_WEATHER	"show_weather"
#define KEY_SHOW_TEMPERATURE	"show_temperature"
#define KEY_CUSTOM_FORMAT	"custom_format"
#define KEY_SHOW_WEEK		"show_week_numbers"
#define KEY_CITIES		"cities"
#define KEY_TEMPERATURE_UNIT	"temperature_unit"
#define KEY_SPEED_UNIT		"speed_unit"

static MateConfEnumStringPair format_type_enum_map [] = {
	{ CLOCK_FORMAT_12,       "12-hour"  },
	{ CLOCK_FORMAT_24,       "24-hour"  },
	{ CLOCK_FORMAT_UNIX,     "unix"     },
	{ CLOCK_FORMAT_INTERNET, "internet" },
	{ CLOCK_FORMAT_CUSTOM,   "custom"   },
    { CLOCK_FORMAT_FUZZY_HOUR, "fuzzy-hour" },
    { CLOCK_FORMAT_FUZZY_DAY,  "fuzzy-day"  },
	{ 0, NULL }
};

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

        GtkWidget *panel_button;	/* main toggle button for the whole clock */

	GtkWidget *main_obox;		/* orientable box inside panel_button */
        GtkWidget *weather_obox;        /* orientable box for the weather widgets */

	GtkWidget *clockw;		/* main label for the date/time display */

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
        GtkWidget *map_section;
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

        gboolean     use_temperature_default;
        gboolean     use_speed_default;
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
        GtkWidget *custom_hbox;
        GtkWidget *custom_label;
        GtkWidget *custom_entry;
        gboolean   custom_format_shown;

	gboolean   can_handle_format_12;

	guint listeners [N_MATECONF_PREFS];
};

/* Used to count the number of clock instances. It's there to know when we
 * should free resources that are shared. */
static int clock_numbers = 0;

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
	PangoContext *context;
	PangoLayout  *layout;
	int	      width, height;
	int	      focus_width = 0;
	int	      focus_pad = 0;

	context = gtk_widget_get_pango_context (widget);

	layout = pango_layout_new (context);
	pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
	pango_layout_set_text (layout, text, -1);
	pango_layout_get_pixel_size (layout, &width, &height);
	g_object_unref (G_OBJECT (layout));
	layout = NULL;

	gtk_widget_style_get (widget,
			      "focus-line-width", &focus_width,
			      "focus-padding", &focus_pad,
			      NULL);

	width += 2 * (focus_width + focus_pad + gtk_widget_get_style (widget)->xthickness);

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
 		timeouttime = (G_USEC_PER_SEC - tv.tv_usec)/1000+1;

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
			    cd->format == CLOCK_FORMAT_24 ||
                cd->format == CLOCK_FORMAT_FUZZY_HOUR ||
                cd->format == CLOCK_FORMAT_FUZZY_DAY) &&
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
	GtkStyle         *style;
        PangoContext     *context;
        PangoFontMetrics *metrics;
        int               focus_width = 0;
        int               focus_pad = 0;
        int               ascent;
        int               descent;
        int               thickness;

	style = gtk_widget_get_style (widget);
        context = gtk_widget_get_pango_context (widget);
        metrics = pango_context_get_metrics (context,
                                             style->font_desc,
                                             pango_context_get_language (context));

        ascent  = pango_font_metrics_get_ascent  (metrics);
        descent = pango_font_metrics_get_descent (metrics);

        pango_font_metrics_unref (metrics);

        gtk_widget_style_get (widget,
                              "focus-line-width", &focus_width,
                              "focus-padding", &focus_pad,
                              NULL);

        if (orientation == MATE_PANEL_APPLET_ORIENT_UP
            || orientation == MATE_PANEL_APPLET_ORIENT_DOWN) {
                thickness = style->ythickness;
        } else {
                thickness = style->xthickness;
        }

        return PANGO_PIXELS (ascent + descent) + 2 * (focus_width + focus_pad + thickness);
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

	if (cd->format == CLOCK_FORMAT_12)
		/* Translators: This is a strftime format string.
		 * It is used to display the time in 12-hours format (eg, like
		 * in the US: 8:10 am). The %p expands to am/pm. */
		time_format = cd->showseconds ? _("%l:%M:%S %p") : _("%l:%M %p");
        else if (cd->format == CLOCK_FORMAT_FUZZY_HOUR || cd->format == CLOCK_FORMAT_FUZZY_DAY)
                time_format = g_strdup("");
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
		else if (strftime (hour, sizeof (hour), timeformat, tm) <= 0)
			strcpy (hour, "???");
		g_free (timeformat);

		utf8 = g_locale_to_utf8 (hour, -1, NULL, NULL, NULL);
        } else if (cd->format == CLOCK_FORMAT_FUZZY_HOUR) {
                if (strftime (hour, sizeof (hour), cd->timeformat, tm) <= 0)
			strcpy (hour, "");

                utf8 = malloc(100*sizeof(char));
                strcpy(utf8, hour);

                if (tm->tm_min > 52 || tm->tm_min < 8)
                {
                        if (tm->tm_min > 52)
                                tm->tm_hour = (tm->tm_hour + 1) % 24;

                        switch (tm->tm_hour)
                        {
                                case 0:
                                        strcat(utf8, "Midnight");
                                        break;
                                case 12:
                                        strcat(utf8, "Noon");
                                break;
                                case 1:
                                case 13:
                                        strcat(utf8, "One o'clock");
                                        break;
                                case 2:
                                case 14:
                                        strcat(utf8, "Two o'clock");
                                        break;
                                case 3:
                                case 15:
                                        strcat(utf8, "Three o'clock");
                                        break;
                                case 4:
                                case 16:
                                        strcat(utf8, "Four o'clock");
                                        break;
                                case 5:
                                case 17:
                                        strcat(utf8, "Five o'clock");
                                        break;
                                case 6:
                                case 18:
                                        strcat(utf8, "Six o'clock");
                                        break;
                                case 7:
                                case 19:
                                        strcat(utf8, "Seven o'clock");
                                        break;
                                case 8:
                                case 20:
                                        strcat(utf8, "Eight o'clock");
                                        break;
                                case 9:
                                case 21:
                                        strcat(utf8, "Nine o'clock");
                                        break;
                                case 10:
                                case 22:
                                        strcat(utf8, "Ten o'clock");
                                        break;
                                case 11:
                                case 23:
                                        strcat(utf8, "Eleven o'clock");
                                        break;
                        }
                }
                else
                {
                        if (tm->tm_min > 7 && tm->tm_min < 23)
                        {
                                strcat(utf8, "Quarter past ");
                        }
                        if (tm->tm_min > 22 && tm->tm_min < 38)
                        {
                                strcat(utf8, "Half past ");
                        }
                        if (tm->tm_min > 37 && tm->tm_min < 53)
                        {
                                strcat(utf8, "Quarter to ");
                                tm->tm_hour = (tm->tm_hour+1) % 24;
                        }
        
                        switch (tm->tm_hour)
                        {
                                case 0:
                                        strcat(utf8, "midnight");
                                        break;
                                case 12:
                                        strcat(utf8, "noon");
                                break;
                                case 1:
                                case 13:
                                        strcat(utf8, "one");
                                        break;
                                case 2:
                                case 14:
                                        strcat(utf8, "two");
                                        break;
                                case 3:
                                case 15:
                                        strcat(utf8, "three");
                                        break;
                                case 4:
                                case 16:
                                        strcat(utf8, "four");
                                        break;
                                case 5:
                                case 17:
                                        strcat(utf8, "five");
                                        break;
                                case 6:
                                case 18:
                                        strcat(utf8, "six");
                                        break;
                                case 7:
                                case 19:
                                        strcat(utf8, "seven");
                                        break;
                                case 8:
                                case 20:
                                        strcat(utf8, "eight");
                                        break;
                                case 9:
                                case 21:
                                        strcat(utf8, "nine");
                                        break;
                                case 10:
                                case 22:
                                        strcat(utf8, "ten");
                                        break;
                                case 11:
                                case 23:
                                        strcat(utf8, "eleven");
                                        break;
                        }
                }
        } else if (cd->format == CLOCK_FORMAT_FUZZY_DAY) {
                if (strftime (hour, sizeof (hour), cd->timeformat, tm) <= 0)
			strcpy (hour, "");

                utf8 = malloc(100*sizeof(char));
                strcpy(utf8, hour);

                switch (tm->tm_hour)
                {
                        case 23:
                        case 0:
                        case 1:
                        case 2:
                                strcat(utf8, "Late night");
                                break;
                        case 3:
                        case 4:
                        case 5:
                        case 6:
                        case 7:
                                strcat(utf8, "Early morning");
                                break;
                        case 8:
                        case 9:
                        case 10:
                                strcat(utf8, "Morning");
                                break;
                        case 11:
                        case 12:
                        case 13:
                                strcat(utf8, "Mid-day");
                                break;
                        case 14:
                        case 15:
                        case 16:
                                strcat(utf8, "Afternoon");
                                break;
                        case 17:
                        case 18:
                        case 19:
                                strcat(utf8, "Evening");
                                break;
                        case 20:
                        case 21:
                        case 22:
                                strcat(utf8, "Night");
                                break;
                }
	} else {
		if (strftime (hour, sizeof (hour), cd->timeformat, tm) <= 0)
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
        char *utf8;

        use_markup = FALSE;

	time (&cd->current_time);
        utf8 = format_time (cd);

	use_markup = FALSE;
        if (pango_parse_markup (utf8, -1, 0, NULL, NULL, NULL, NULL))
                use_markup = TRUE;

	if (use_markup)
		gtk_label_set_markup (GTK_LABEL (cd->clockw), utf8);
	else
		gtk_label_set_text (GTK_LABEL (cd->clockw), utf8);

	g_free (utf8);

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
        if (!cd->showdate) {
		struct tm *tm;
		char date[256];
		char *utf8, *loc;
                char *zone;
                time_t now_t;
                struct tm now;
                char *tip;

		tm = localtime (&cd->current_time);

		utf8 = NULL;

                /* Show date in tooltip. */
		/* Translators: This is a strftime format string.
		 * It is used to display a date. Please leave "%%s" as it is:
		 * it will be used to insert the timezone name later. */
                loc = g_locale_from_utf8 (_("%A %B %d (%%s)"), -1, NULL, NULL, NULL);
                if (!loc)
                        strcpy (date, "???");
                else if (strftime (date, sizeof (date), loc, tm) <= 0)
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

                gtk_widget_set_tooltip_text (cd->panel_button, tip);
                g_free (utf8);
                g_free (tip);
        } else {
#ifdef HAVE_LIBECAL
		if (cd->calendar_popup)
			gtk_widget_set_tooltip_text (cd->panel_button,
						     _("Click to hide your appointments and tasks"));
		else
			gtk_widget_set_tooltip_text (cd->panel_button,
						     _("Click to view your appointments and tasks"));
#else
		if (cd->calendar_popup)
			gtk_widget_set_tooltip_text (cd->panel_button,
						     _("Click to hide month calendar"));
		else
			gtk_widget_set_tooltip_text (cd->panel_button,
						     _("Click to view month calendar"));
#endif
        }
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
        GList *l;

        for (l = cd->locations; l; l = l->next)
                g_object_unref (l->data);

        g_list_free (cd->locations);
        cd->locations = NULL;
}

static void
destroy_clock (GtkWidget * widget, ClockData *cd)
{
	MateConfClient *client;
	int          i;

	client = mateconf_client_get_default ();

	for (i = 0; i < N_MATECONF_PREFS; i++)
		mateconf_client_notify_remove (
				client, cd->listeners [i]);

	g_object_unref (G_OBJECT (client));

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

#ifdef HAVE_LIBECAL
	if (clock_numbers > 0) {
		e_passwords_shutdown ();
		clock_numbers--;
	}
#endif
}

static gboolean
close_on_escape (GtkWidget       *widget,
		 GdkEventKey     *event,
		 GtkToggleButton *toggle_button)
{
	if (event->keyval == GDK_Escape) {
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
	char      *prefs_dir;

	prefs_dir = mate_panel_applet_get_preferences_key (MATE_PANEL_APPLET (cd->applet));
	window = calendar_window_new (&cd->current_time,
				      prefs_dir,
				      cd->orient == MATE_PANEL_APPLET_ORIENT_UP);
	g_free (prefs_dir);

	calendar_window_set_show_weeks (CALENDAR_WINDOW (window),
					cd->showweek);
	calendar_window_set_time_format (CALENDAR_WINDOW (window),
					 cd->format);

        gtk_window_set_screen (GTK_WINDOW (window),
			       gtk_widget_get_screen (cd->applet));

        g_signal_connect (window, "edit-locations",
                          G_CALLBACK (edit_locations_cb), cd);

	g_signal_connect (window, "delete_event",
			  G_CALLBACK (delete_event), cd->panel_button);
	g_signal_connect (window, "key_press_event",
			  G_CALLBACK (close_on_escape), cd->panel_button);

	return window;
}

static void
position_calendar_popup (ClockData *cd)
{
	GtkRequisition  req;
	GtkAllocation   allocation;
	GdkScreen      *screen;
	GdkRectangle    monitor;
	GdkGravity      gravity = GDK_GRAVITY_NORTH_WEST;
	int             button_w, button_h;
	int             x, y;
	int             w, h;
	int             i, n;
	gboolean        found_monitor = FALSE;

	/* Get root origin of the toggle button, and position above that. */
	gdk_window_get_origin (gtk_widget_get_window (cd->panel_button),
			       &x, &y);

	gtk_window_get_size (GTK_WINDOW (cd->calendar_popup), &w, &h);
	gtk_widget_size_request (cd->calendar_popup, &req);
	w = req.width;
	h = req.height;

	gtk_widget_get_allocation (cd->panel_button, &allocation);
	button_w = allocation.width;
	button_h = allocation.height;

	screen = gtk_window_get_screen (GTK_WINDOW (cd->calendar_popup));

	n = gdk_screen_get_n_monitors (screen);
	for (i = 0; i < n; i++) {
		gdk_screen_get_monitor_geometry (screen, i, &monitor);
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
		monitor.width = gdk_screen_get_width (screen);
		monitor.height = gdk_screen_get_height (screen);
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

	cd->clock_vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (locations_box), cd->clock_vbox);

	cd->clock_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_set_ignore_hidden (cd->clock_group, FALSE);

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

        if (cd->cities_store) {
                g_object_unref (G_OBJECT (cd->cities_store));
                cd->cities_store = NULL;
        }

	/* City name, Timezone name, Coordinates in lat/long */
	cd->cities_store = g_object_ref (gtk_list_store_new (COL_CITY_LAST,
                                                             G_TYPE_STRING,		/* COL_CITY_NAME */
                                                             G_TYPE_STRING,		/* COL_CITY_TZ */
                                                             CLOCK_LOCATION_TYPE));	/* COL_CITY_LOC */

        list = g_list_copy (cities);
        list = g_list_sort (list, sort_locations_by_name);

	while (list) {
		ClockLocation *loc = CLOCK_LOCATION (list->data);

		gtk_list_store_append (cd->cities_store, &iter);
		gtk_list_store_set (cd->cities_store, &iter,
				    COL_CITY_NAME, clock_location_get_display_name (loc),
				    /* FIXME: translate the timezone */
				    COL_CITY_TZ, clock_location_get_timezone (loc),
                                    COL_CITY_LOC, loc,
				    -1);

                list = list->next;
	}


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

        if (cd->cities_section) {
                gtk_widget_destroy (cd->cities_section);
                cd->cities_section = NULL;
        }

        g_list_free (cd->location_tiles);
        cd->location_tiles = NULL;

        cd->cities_section = gtk_vbox_new (FALSE, 6);
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

        while (node) {
                ClockLocation *loc = node->data;

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

                node = g_list_next (node);
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
                gtk_widget_destroy (GTK_WIDGET (cd->map_section));
                cd->map_widget = NULL;
        }

        map = clock_map_new ();
        g_signal_connect (map, "need-locations",
                          G_CALLBACK (map_need_locations_cb), cd);

        cd->map_section = gtk_alignment_new (0, 0, 1, 1);
        cd->map_widget = GTK_WIDGET (map);

        gtk_container_add (GTK_CONTAINER (cd->map_section), cd->map_widget);

        gtk_alignment_set_padding (GTK_ALIGNMENT (cd->map_section), 1, 1, 1, 1);

        gtk_box_pack_start (GTK_BOX (cd->clock_vbox), cd->map_section, FALSE, FALSE, 0);
        gtk_widget_show (cd->map_widget);
        gtk_widget_show (cd->map_section);
}

static void
update_calendar_popup (ClockData *cd)
{
        if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->panel_button))) {
                if (cd->calendar_popup) {
                        gtk_widget_destroy (cd->calendar_popup);
                        cd->calendar_popup = NULL;
                        cd->cities_section = NULL;
                        cd->map_section = NULL;
                        cd->map_widget = NULL;
			cd->clock_vbox = NULL;

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
force_no_focus_padding (GtkWidget *widget)
{
        static gboolean first_time = TRUE;

        if (first_time) {
                gtk_rc_parse_string ("\n"
                                     "   style \"clock-applet-button-style\"\n"
                                     "   {\n"
                                     "      GtkWidget::focus-line-width=0\n"
                                     "      GtkWidget::focus-padding=0\n"
                                     "   }\n"
                                     "\n"
                                     "    widget \"*.clock-applet-button\" style \"clock-applet-button-style\"\n"
                                     "\n");
                first_time = FALSE;
        }

        gtk_widget_set_name (widget, "clock-applet-button");
}

static GtkWidget *
create_main_clock_button (void)
{
        GtkWidget *button;

        button = gtk_toggle_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

        force_no_focus_padding (button);

        return button;
}

static GtkWidget *
create_main_clock_label (ClockData *cd)
{
        GtkWidget *label;

        label = gtk_label_new (NULL);
	g_signal_connect (label, "size_request",
			  G_CALLBACK (clock_size_request),
			  cd);
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
#ifdef HAVE_LIBECAL
	clock_numbers++;
	e_passwords_init ();
#endif

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
                                ClockData	*cd)
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
		else if (strftime (string, sizeof (string), format, tm) <= 0)
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
	else if (strftime (string, sizeof (string), loc, tm) <= 0)
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

                g_error_free (error);
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
        { "ClockPreferences", GTK_STOCK_PROPERTIES, N_("_Preferences"),
          NULL, NULL,
          G_CALLBACK (verb_display_properties_dialog) },
        { "ClockHelp", GTK_STOCK_HELP, N_("_Help"),
          NULL, NULL,
          G_CALLBACK (display_help_dialog) },
        { "ClockAbout", GTK_STOCK_ABOUT, N_("_About"),
          NULL, NULL,
          G_CALLBACK (display_about_dialog) },
        { "ClockCopyTime", GTK_STOCK_COPY, N_("Copy _Time"),
          NULL, NULL,
          G_CALLBACK (copy_time) },
        { "ClockCopyDate", GTK_STOCK_COPY, N_("Copy _Date"),
          NULL, NULL,
          G_CALLBACK (copy_date) },
        { "ClockConfig", GTK_STOCK_PREFERENCES, N_("Ad_just Date & Time"),
          NULL, NULL,
          G_CALLBACK (config_date) }
};

static void
format_changed (MateConfClient  *client,
                guint         cnxn_id,
                MateConfEntry   *entry,
                ClockData    *clock)
{
	const char  *value;
	int          new_format;

	if (!entry->value || entry->value->type != MATECONF_VALUE_STRING)
		return;

	value = mateconf_value_get_string (entry->value);
	if (!mateconf_string_to_enum (format_type_enum_map, value, &new_format))
		return;

	if (!clock->can_handle_format_12 && new_format == CLOCK_FORMAT_12)
		new_format = CLOCK_FORMAT_24;

	if (new_format == clock->format)
		return;

	clock->format = new_format;
	refresh_clock_timeout (clock);

	if (clock->calendar_popup != NULL) {
		calendar_window_set_time_format (CALENDAR_WINDOW (clock->calendar_popup), clock->format);
                position_calendar_popup (clock);
	}

}

static void
show_seconds_changed (MateConfClient  *client,
                   guint         cnxn_id,
                   MateConfEntry   *entry,
                   ClockData    *clock)
{
	gboolean value;

	if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
		return;

	value = mateconf_value_get_bool (entry->value);

	clock->showseconds = (value != 0);
	refresh_clock_timeout (clock);
}

static void
show_date_changed (MateConfClient  *client,
                   guint         cnxn_id,
                   MateConfEntry   *entry,
                   ClockData    *clock)
{
	gboolean value;

	if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
		return;

	value = mateconf_value_get_bool (entry->value);

	clock->showdate = (value != 0);
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
update_weather_bool_value_and_toggle_from_mateconf (ClockData *cd, MateConfEntry *entry,
                                                 gboolean *value_loc, const char *widget_name)
{
	GtkWidget *widget;
        gboolean value;

        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        value = mateconf_value_get_bool (entry->value);

        *value_loc = (value != 0);

	widget = _clock_get_widget (cd, widget_name);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
				      *value_loc);

        update_panel_weather (cd);
}

static void
show_weather_changed (MateConfClient  *client,
                      guint         cnxn_id,
                      MateConfEntry   *entry,
                      ClockData    *cd)
{
        update_weather_bool_value_and_toggle_from_mateconf (cd, entry, &cd->show_weather, "weather_check");
}

static void
show_temperature_changed (MateConfClient  *client,
                          guint         cnxn_id,
                          MateConfEntry   *entry,
                          ClockData    *cd)
{
        update_weather_bool_value_and_toggle_from_mateconf (cd, entry, &cd->show_temperature, "temperature_check");
}

static void
location_weather_updated_cb (ClockLocation *location,
                             WeatherInfo   *info,
                             gpointer       data)
{
	ClockData *cd = data;
	const gchar *icon_name;
	const gchar *temp;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;

	if (!info || !weather_info_is_valid (info))
		return;

	if (!clock_location_is_current (location))
		return;

	icon_name = weather_info_get_icon_name (info);
	/* FIXME: mmh, screen please? Also, don't hardcode to 16 */
	theme = gtk_icon_theme_get_default ();
	pixbuf = gtk_icon_theme_load_icon (theme, icon_name, 16,
					   GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);

	temp = weather_info_get_temp_summary (info);

	gtk_image_set_from_pixbuf (GTK_IMAGE (cd->panel_weather_icon), pixbuf);
	gtk_label_set_text (GTK_LABEL (cd->panel_temperature_label), temp);
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

/* Parser for our serialized locations in mateconf */
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
		clock_location_make_current (loc, GDK_WINDOW_XWINDOW (gtk_widget_get_window (cd->applet)),
					     NULL, NULL, NULL);

        data->cities = g_list_append (data->cities, loc);
}

static GMarkupParser location_parser = {
        location_start_element, NULL, NULL, NULL, NULL
};

static void
cities_changed (MateConfClient  *client,
                guint         cnxn_id,
                MateConfEntry   *entry,
                ClockData    *cd)
{
	LocationParserData data;

        GSList *cur = NULL;

        GMarkupParseContext *context;

	data.cities = NULL;
	data.cd = cd;

        if (!entry->value || entry->value->type != MATECONF_VALUE_LIST)
                return;

        context = g_markup_parse_context_new (&location_parser, 0, &data, NULL);

        cur = mateconf_value_get_list (entry->value);

        while (cur) {
                const char *str = mateconf_value_get_string (cur->data);
                g_markup_parse_context_parse (context, str, strlen (str), NULL);

                cur = cur->next;
        }

        g_markup_parse_context_free (context);

        set_locations (cd, data.cities);
        create_cities_store (cd);
}

static void
update_temperature_combo (ClockData *cd)
{
	GtkWidget *widget;
        int active_index;

	widget = _clock_get_widget (cd, "temperature_combo");

        if (cd->use_temperature_default)
                active_index = 0;
        else
                active_index = cd->temperature_unit - 1;

        gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active_index);
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
clock_migrate_to_26 (ClockData *clock)
{
	gboolean  unixtime;
	gboolean  internettime;
	int       hourformat;

	internettime = mate_panel_applet_mateconf_get_bool (MATE_PANEL_APPLET (clock->applet),
						    "internet_time",
						    NULL);
	unixtime = mate_panel_applet_mateconf_get_bool (MATE_PANEL_APPLET (clock->applet),
						"unix_time",
						NULL);
	hourformat = mate_panel_applet_mateconf_get_int (MATE_PANEL_APPLET (clock->applet),
						 "hour_format",
						 NULL);

	if (unixtime)
		clock->format = CLOCK_FORMAT_UNIX;
	else if (internettime)
		clock->format = CLOCK_FORMAT_INTERNET;
	else if (hourformat == 12)
		clock->format = CLOCK_FORMAT_12;
	else if (hourformat == 24)
		clock->format = CLOCK_FORMAT_24;

	/* It's still possible that we have none of the old keys, in which case
	 * we're not migrating from 2.6, but the config is simply wrong. So
	 * don't set the format key in this case. */
	if (clock->format != CLOCK_FORMAT_INVALID)
		mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (clock->applet),
					       KEY_FORMAT,
					       mateconf_enum_to_string (format_type_enum_map,
								     clock->format),
					       NULL);
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
parse_and_set_temperature_string (const char *str, ClockData *cd)
{
        gint value = 0;
	gboolean use_default = FALSE;

	value = mateweather_prefs_parse_temperature (str, &use_default);

	cd->use_temperature_default = use_default;
	cd->temperature_unit = value;
}

static void
parse_and_set_speed_string (const char *str, ClockData *cd)
{
        gint value = 0;
	gboolean use_default = FALSE;

	value = mateweather_prefs_parse_speed (str, &use_default);

	cd->use_speed_default = use_default;
	cd->speed_unit = value;
}

static void
temperature_unit_changed (MateConfClient  *client,
                          guint         cnxn_id,
                          MateConfEntry   *entry,
                          ClockData    *cd)
{
        const gchar *value;

        if (!entry->value || entry->value->type != MATECONF_VALUE_STRING)
                return;

        value = mateconf_value_get_string (entry->value);
        parse_and_set_temperature_string (value, cd);
	update_temperature_combo (cd);
	update_weather_locations (cd);
}

static void
update_speed_combo (ClockData *cd)
{
	GtkWidget *widget;
        int active_index;

	widget = _clock_get_widget (cd, "wind_speed_combo");

	if (cd->use_speed_default)
                active_index = 0;
        else
                active_index = cd->speed_unit - 1;

        gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active_index);
}

static void
speed_unit_changed (MateConfClient  *client,
                    guint         cnxn_id,
                    MateConfEntry   *entry,
                    ClockData    *cd)
{
        const gchar *value;

        if (!entry->value || entry->value->type != MATECONF_VALUE_STRING)
                return;

        value = mateconf_value_get_string (entry->value);
        parse_and_set_speed_string (value, cd);
	update_speed_combo (cd);
	update_weather_locations (cd);
}

static void
custom_format_changed (MateConfClient  *client,
                       guint         cnxn_id,
                       MateConfEntry   *entry,
                       ClockData    *clock)
{
	const char *value;

	if (!entry->value || entry->value->type != MATECONF_VALUE_STRING)
		return;

	value = mateconf_value_get_string (entry->value);

        g_free (clock->custom_format);
	clock->custom_format = g_strdup (value);

	if (clock->format == CLOCK_FORMAT_CUSTOM)
		refresh_clock (clock);
}

static void
show_week_changed (MateConfClient  *client,
		   guint         cnxn_id,
		   MateConfEntry   *entry,
		   ClockData    *clock)
{
	gboolean value;

	if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
		return;

	value = mateconf_value_get_bool (entry->value);

	if (clock->showweek == (value != 0))
		return;

	clock->showweek = (value != 0);

	if (clock->calendar_popup != NULL) {
		calendar_window_set_show_weeks (CALENDAR_WINDOW (clock->calendar_popup), clock->showweek);
                position_calendar_popup (clock);
	}
}

static guint
setup_mateconf_preference (ClockData *cd, MateConfClient *client, const char *key_name, MateConfClientNotifyFunc callback)
{
        char *key;
        guint id;

        key = mate_panel_applet_mateconf_get_full_key (MATE_PANEL_APPLET (cd->applet),
                                               key_name);
        id = mateconf_client_notify_add (client, key,
                                      callback,
                                      cd, NULL, NULL);
        g_free (key);

        return id;
}

static void
setup_mateconf (ClockData *cd)
{
        struct {
                const char *key_name;
                MateConfClientNotifyFunc callback;
        } prefs[] = {
                { KEY_FORMAT,		(MateConfClientNotifyFunc) format_changed },
                { KEY_SHOW_SECONDS,	(MateConfClientNotifyFunc) show_seconds_changed },
                { KEY_SHOW_DATE,	(MateConfClientNotifyFunc) show_date_changed },
                { KEY_SHOW_WEATHER,	(MateConfClientNotifyFunc) show_weather_changed },
                { KEY_SHOW_TEMPERATURE,	(MateConfClientNotifyFunc) show_temperature_changed },
                { KEY_CUSTOM_FORMAT,	(MateConfClientNotifyFunc) custom_format_changed },
                { KEY_SHOW_WEEK,	(MateConfClientNotifyFunc) show_week_changed },
                { KEY_CITIES,		(MateConfClientNotifyFunc) cities_changed },
                { KEY_TEMPERATURE_UNIT,	(MateConfClientNotifyFunc) temperature_unit_changed },
                { KEY_SPEED_UNIT,	(MateConfClientNotifyFunc) speed_unit_changed },
        };

	MateConfClient *client;
        int          i;

	client = mateconf_client_get_default ();

        for (i = 0; i < G_N_ELEMENTS (prefs); i++)
                cd->listeners[i] = setup_mateconf_preference (cd, client, prefs[i].key_name, prefs[i].callback);

	g_object_unref (G_OBJECT (client));
}

static GList *
parse_mateconf_cities (ClockData *cd, GSList *values)
{
        GSList *cur = values;
	LocationParserData data;
        GMarkupParseContext *context;

	data.cities = NULL;
	data.cd = cd;

        context =
                g_markup_parse_context_new (&location_parser, 0, &data, NULL);

        while (cur) {
                const char *str = (char *)cur->data;
                g_markup_parse_context_parse (context, str, strlen(str), NULL);

                cur = cur->next;
        }

        g_markup_parse_context_free (context);

        return data.cities;
}

static void
load_mateconf_settings (ClockData *cd)
{
        MatePanelApplet *applet;
        int format;
        char *format_str;
        char *value;
        GError *error;
        GSList *values = NULL;
        GList *cities = NULL;

        applet = MATE_PANEL_APPLET (cd->applet);

        cd->format = CLOCK_FORMAT_INVALID;

	format_str = mate_panel_applet_mateconf_get_string (applet, KEY_FORMAT, NULL);
	if (format_str &&
            mateconf_string_to_enum (format_type_enum_map, format_str, &format))
                cd->format = format;
	else
		clock_migrate_to_26 (cd);

        g_free (format_str);

	if (cd->format == CLOCK_FORMAT_INVALID)
		cd->format = clock_locale_format ();

	cd->custom_format = mate_panel_applet_mateconf_get_string (applet, KEY_CUSTOM_FORMAT, NULL);
	cd->showseconds = mate_panel_applet_mateconf_get_bool (applet, KEY_SHOW_SECONDS, NULL);

	error = NULL;
	cd->showdate = mate_panel_applet_mateconf_get_bool (applet, KEY_SHOW_DATE, &error);
	if (error) {
		g_error_free (error);
		/* if on a small screen don't show date by default */
		if (gdk_screen_width () <= 800)
			cd->showdate = FALSE;
		else
			cd->showdate = TRUE;
	}

        cd->show_weather = mate_panel_applet_mateconf_get_bool (applet, KEY_SHOW_WEATHER, NULL);
        cd->show_temperature = mate_panel_applet_mateconf_get_bool (applet, KEY_SHOW_TEMPERATURE, NULL);
	cd->showweek = mate_panel_applet_mateconf_get_bool (applet, KEY_SHOW_WEEK, NULL);
        cd->timeformat = NULL;

	cd->can_handle_format_12 = (clock_locale_format () == CLOCK_FORMAT_12);
	if (!cd->can_handle_format_12 && cd->format == CLOCK_FORMAT_12)
		cd->format = CLOCK_FORMAT_24;

	value = mate_panel_applet_mateconf_get_string (applet, KEY_TEMPERATURE_UNIT, NULL);
	parse_and_set_temperature_string (value, cd);
        g_free (value);

	value = mate_panel_applet_mateconf_get_string (applet, KEY_SPEED_UNIT, NULL);
	parse_and_set_speed_string (value, cd);
        g_free (value);

        values = mate_panel_applet_mateconf_get_list (MATE_PANEL_APPLET (cd->applet), KEY_CITIES,
                                              MATECONF_VALUE_STRING, NULL);

        if (g_slist_length (values) == 0) {
                cities = NULL;
        } else {
                cities = parse_mateconf_cities (cd, values);
        }

        set_locations (cd, cities);
}

static gboolean
fill_clock_applet (MatePanelApplet *applet)
{
	ClockData      *cd;
        GtkActionGroup *action_group;
        GtkAction      *action;
        gchar          *ui_path;
        char           *filename;
	GError         *error;

	mate_panel_applet_add_preferences (applet, CLOCK_SCHEMA_DIR, NULL);
	mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);

	cd = g_new0 (ClockData, 1);
	cd->fixed_width = -1;
	cd->fixed_height = -1;

	cd->applet = GTK_WIDGET (applet);

	setup_mateconf (cd);
        load_mateconf_settings (cd);

	cd->builder = gtk_builder_new ();
	gtk_builder_set_translation_domain (cd->builder, GETTEXT_PACKAGE);
        filename = g_build_filename (BUILDERDIR, "clock.ui", NULL);

	error = NULL;
	gtk_builder_add_from_file (cd->builder, filename, &error);
        if (error) {
		g_warning ("Error loading \"%s\": %s",
			   filename, error->message);
		g_error_free (error);
	}

        g_free (filename);

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
        ui_path = g_build_filename (CLOCK_MENU_UI_DIR, "clock-menu.xml", NULL);
	mate_panel_applet_setup_menu_from_file (MATE_PANEL_APPLET (cd->applet),
					   ui_path, action_group);
        g_free (ui_path);

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

	return TRUE;
}

/* FIXME old clock applet */
#if 0
static void
setup_writability_sensitivity (ClockData *clock, GtkWidget *w, GtkWidget *label, const char *key)
{
        /* FMQ: was used from old preferences dialog; fix this up */
	char *fullkey;
	MateConfClient *client;

	client = mateconf_client_get_default ();

	fullkey = mate_panel_applet_mateconf_get_full_key
		(MATE_PANEL_APPLET (clock->applet), key);

	if ( ! mateconf_client_key_is_writable (client, fullkey, NULL)) {
		g_object_set_data (G_OBJECT (w), NEVER_SENSITIVE,
				   GINT_TO_POINTER (1));
		gtk_widget_set_sensitive (w, FALSE);
		if (label != NULL) {
			g_object_set_data (G_OBJECT (label), NEVER_SENSITIVE,
					   GINT_TO_POINTER (1));
			gtk_widget_set_sensitive (label, FALSE);
		}
	}

	g_free (fullkey);

	g_object_unref (G_OBJECT (client));
}

static void
update_properties_for_format (ClockData   *cd,
                              GtkComboBox *combo,
                              ClockFormat  format)
{

        /* show the custom format things the first time we actually
         * have a custom format set in MateConf, but after that don't
         * unshow it if the format changes
         */
        if (!cd->custom_format_shown &&
            (cd->format == CLOCK_FORMAT_CUSTOM ||
             (cd->custom_format && cd->custom_format [0]))) {
                gtk_widget_show (cd->custom_hbox);
                gtk_widget_show (cd->custom_label);
                gtk_widget_show (cd->custom_entry);

                gtk_combo_box_append_text (combo, _("Custom format"));

                cd->custom_format_shown = TRUE;
        }

        /* Some combinations of options do not make sense */
        switch (format) {
        case CLOCK_FORMAT_12:
        case CLOCK_FORMAT_24:
                gtk_widget_set_sensitive (cd->showseconds_check, TRUE);
                gtk_widget_set_sensitive (cd->showdate_check, TRUE);
		gtk_widget_set_sensitive (cd->custom_entry, FALSE);
		gtk_widget_set_sensitive (cd->custom_label, FALSE);
                break;
        case CLOCK_FORMAT_UNIX:
                gtk_widget_set_sensitive (cd->showseconds_check, FALSE);
                gtk_widget_set_sensitive (cd->showdate_check, FALSE);
                gtk_widget_set_sensitive (cd->custom_entry, FALSE);
                gtk_widget_set_sensitive (cd->custom_label, FALSE);
                break;
        case CLOCK_FORMAT_INTERNET:
                gtk_widget_set_sensitive (cd->showseconds_check, TRUE);
                gtk_widget_set_sensitive (cd->showdate_check, FALSE);
		gtk_widget_set_sensitive (cd->custom_entry, FALSE);
		gtk_widget_set_sensitive (cd->custom_label, FALSE);
                break;
	case CLOCK_FORMAT_CUSTOM:
		gtk_widget_set_sensitive (cd->showseconds_check, FALSE);
		gtk_widget_set_sensitive (cd->showdate_check, FALSE);
		gtk_widget_set_sensitive (cd->custom_entry, TRUE);
		gtk_widget_set_sensitive (cd->custom_label, TRUE);
                break;
        default:
                g_assert_not_reached ();
                break;
	}
}

static void
set_format_cb (GtkComboBox *combo,
	       ClockData   *cd)
{
        /* FMQ: was used from old preferences dialog; fix this up */
        ClockFormat format;

	/* valid values begin from 1 */
	if (cd->can_handle_format_12)
		format = gtk_combo_box_get_active (combo) + 1;
	else
		format = gtk_combo_box_get_active (combo) + 2;

        update_properties_for_format (cd, combo, format);

        if (cd->format != format)
                mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (cd->applet),
                                               KEY_FORMAT,
                                               mateconf_enum_to_string (format_type_enum_map, format),
                                               NULL);
}
#endif

static void
set_show_seconds_cb (GtkWidget *w,
                     ClockData *clock)
{
	mate_panel_applet_mateconf_set_bool (MATE_PANEL_APPLET (clock->applet),
				     KEY_SHOW_SECONDS,
				     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)),
				     NULL);
}

static void
set_show_date_cb (GtkWidget *w,
		  ClockData *clock)
{
	mate_panel_applet_mateconf_set_bool (MATE_PANEL_APPLET (clock->applet),
				     KEY_SHOW_DATE,
				     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)),
				     NULL);
}

static void
set_show_weather_cb (GtkWidget *w,
                     ClockData *clock)
{
	mate_panel_applet_mateconf_set_bool (MATE_PANEL_APPLET (clock->applet),
				     KEY_SHOW_WEATHER,
				     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)),
				     NULL);
}

static void
set_show_temperature_cb (GtkWidget *w,
                         ClockData *clock)
{
	mate_panel_applet_mateconf_set_bool (MATE_PANEL_APPLET (clock->applet),
				     KEY_SHOW_TEMPERATURE,
				     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)),
				     NULL);
}

#if 0
static void
set_show_zones_cb (GtkWidget *w,
		   ClockData *clock)
{
	mate_panel_applet_mateconf_set_bool (MATE_PANEL_APPLET (clock->applet),
				     KEY_SHOW_ZONES,
				     GTK_TOGGLE_BUTTON (w)->active,
				     NULL);
}
#endif

/* FIXME old clock applet */
#if 0
static void
set_custom_format_cb (GtkEntry  *entry,
		      ClockData *cd)
{
        /* FMQ: was used from old preferences dialog; fix this up */
	const char *custom_format;

	custom_format = gtk_entry_get_text (entry);
	mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (cd->applet),
				       KEY_CUSTOM_FORMAT, custom_format, NULL);
}
#endif

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
        ClockLocation *loc;
        GList *node = cd->locations;

        GSList *root = NULL;
        GSList *list = NULL;

        while (node) {
                loc = CLOCK_LOCATION (node->data);
                list = g_slist_prepend (list, loc_to_string (loc));
                node = node->next;
        }

        list = g_slist_reverse (list);
	mate_panel_applet_mateconf_set_list (MATE_PANEL_APPLET (cd->applet),
                                     KEY_CITIES, MATECONF_VALUE_STRING, list, NULL);

        root = list;

        while (list) {
                g_free (list->data);
                list = g_slist_next (list);
        }

        g_slist_free (root);
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
	 * account the new location (via the mateconf notification) */
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
				  "clock", "clock-settings");
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
	 * account the removed location (via the mateconf notification) */
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
set_12hr_24hr_fuzzy_format_radio_cb (GtkWidget *widget, ClockData *cd)
{
        GtkWidget *radio_12hr;
        GtkWidget *radio_24hr;
        GtkWidget *radio_fuzzyhr;
        GtkWidget *radio_fuzzyday;

        /* Set the 12 hour / 24 hour widget */
        radio_12hr     = _clock_get_widget (cd, "12hr_radio");
        radio_24hr     = _clock_get_widget (cd, "24hr_radio");
        radio_fuzzyhr  = _clock_get_widget (cd, "fuzzyhr_radio");
        radio_fuzzyday = _clock_get_widget (cd, "fuzzyday_radio");

	const gchar *val;
        ClockFormat format;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_12hr)))
                format = CLOCK_FORMAT_12;
        else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_fuzzyhr)))
                format = CLOCK_FORMAT_FUZZY_HOUR;
        else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_fuzzyday)))
                format = CLOCK_FORMAT_FUZZY_DAY;
        else
                format = CLOCK_FORMAT_24;

        val = mateconf_enum_to_string (format_type_enum_map, format);

	mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (cd->applet),
				       KEY_FORMAT, val, NULL);
}

static void
temperature_combo_changed (GtkComboBox *combo, ClockData *cd)
{
	int value;
	int old_value;
	const gchar *str;

	value = gtk_combo_box_get_active (combo) + 1;

	if (cd->use_temperature_default)
		old_value = TEMP_UNIT_DEFAULT;
	else
		old_value = cd->temperature_unit;

	if (value == old_value)
		return;

	str = mateweather_prefs_temp_enum_to_string (value);

	mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (cd->applet),
				       KEY_TEMPERATURE_UNIT, str, NULL);
}

static void
speed_combo_changed (GtkComboBox *combo, ClockData *cd)
{
	int value;
	int old_value;
	const gchar *str;

	value = gtk_combo_box_get_active (combo) + 1;

	if (cd->use_speed_default)
		old_value = SPEED_UNIT_DEFAULT;
	else
		old_value = cd->speed_unit;

	if (value == old_value)
		return;

	str = mateweather_prefs_speed_enum_to_string (value);

	mate_panel_applet_mateconf_set_string (MATE_PANEL_APPLET (cd->applet),
				       KEY_SPEED_UNIT, str, NULL);
}

static void
fill_prefs_window (ClockData *cd)
{
        static const int temperatures[] = {
                TEMP_UNIT_DEFAULT,
                TEMP_UNIT_KELVIN,
                TEMP_UNIT_CENTIGRADE,
                TEMP_UNIT_FAHRENHEIT,
                -1
        };

        static const int speeds[] = {
                SPEED_UNIT_DEFAULT,
                SPEED_UNIT_MS,
                SPEED_UNIT_KPH,
                SPEED_UNIT_MPH,
                SPEED_UNIT_KNOTS,
                SPEED_UNIT_BFT,
                -1
        };

        GtkWidget *radio_12hr;
        GtkWidget *radio_24hr;
	GtkWidget *radio_fuzzyhr;
	GtkWidget *radio_fuzzyday;
	GtkWidget *widget;
	GtkCellRenderer *renderer;
        GtkTreeViewColumn *col;
	GtkListStore *store;
        int i;

	/* Set the 12 hour / 24 hour / fuzzy widget */
        radio_12hr     = _clock_get_widget (cd, "12hr_radio");
        radio_24hr     = _clock_get_widget (cd, "24hr_radio");
        radio_fuzzyhr  = _clock_get_widget (cd, "fuzzyhr_radio");
        radio_fuzzyday = _clock_get_widget (cd, "fuzzyday_radio");

        switch (cd->format)
        {
                case CLOCK_FORMAT_12:
                        widget = radio_12hr;
                        break;
                case CLOCK_FORMAT_FUZZY_HOUR:
                        widget = radio_fuzzyhr;
                        break;
                case CLOCK_FORMAT_FUZZY_DAY:
                        widget = radio_fuzzyday;
                        break;
                default:
                        widget = radio_24hr;
                        break;
        }

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);

	g_signal_connect (radio_12hr, "toggled",
			  G_CALLBACK (set_12hr_24hr_fuzzy_format_radio_cb), cd);
	g_signal_connect (radio_24hr, "toggled",
			  G_CALLBACK (set_12hr_24hr_fuzzy_format_radio_cb), cd);
	g_signal_connect (radio_fuzzyhr, "toggled",
			  G_CALLBACK (set_12hr_24hr_fuzzy_format_radio_cb), cd);
	g_signal_connect (radio_fuzzyday, "toggled",
			  G_CALLBACK (set_12hr_24hr_fuzzy_format_radio_cb), cd);

	/* Set the "Show Date" checkbox */
	widget = _clock_get_widget (cd, "date_check");
	g_signal_connect (widget, "toggled",
                          G_CALLBACK (set_show_date_cb), cd);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), cd->showdate);

	/* Set the "Show Seconds" checkbox */
	widget = _clock_get_widget (cd, "seconds_check");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), cd->showseconds);
	g_signal_connect (widget, "toggled",
                          G_CALLBACK (set_show_seconds_cb), cd);

	/* Set the "Show weather" checkbox */
	widget = _clock_get_widget (cd, "weather_check");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), cd->show_weather);
	g_signal_connect (widget, "toggled",
                          G_CALLBACK (set_show_weather_cb), cd);

	/* Set the "Show temperature" checkbox */
	widget = _clock_get_widget (cd, "temperature_check");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), cd->show_temperature);
	g_signal_connect (widget, "toggled",
                          G_CALLBACK (set_show_temperature_cb), cd);

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
                gtk_combo_box_append_text (GTK_COMBO_BOX (widget),
                                           mateweather_prefs_get_temp_display_name (temperatures[i]));

	update_temperature_combo (cd);
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
                gtk_combo_box_append_text (GTK_COMBO_BOX (widget),
                                           mateweather_prefs_get_speed_display_name (speeds[i]));

	update_speed_combo (cd);
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

static void
display_properties_dialog (ClockData *cd, gboolean start_in_locations_page)
{
        ensure_prefs_window_is_created (cd);

        if (start_in_locations_page) {
                GtkWidget *notebook = _clock_get_widget (cd, "notebook");
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
        }

	update_set_time_button (cd);

        gtk_window_set_screen (GTK_WINDOW (cd->prefs_window),
                               gtk_widget_get_screen (cd->applet));
	gtk_window_present (GTK_WINDOW (cd->prefs_window));

        refresh_click_timeout_time_only (cd);

        /* FMQ: cd->props was the old preferences window; remove references to it */
        /* FMQ: connect to the Help button by hand; look at properties_response_cb() for the help code */
#if 0
        /* FMQ: check the code below; replace the proper parts */
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *combo;
	GtkWidget *label;

        gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _("24 hour"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _("UNIX time"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _("Internet time"));

	gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
	gtk_widget_show (combo);

	cd->custom_hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (vbox), cd->custom_hbox, TRUE, TRUE, 0);

	cd->custom_label = gtk_label_new_with_mnemonic (_("Custom _format:"));
	gtk_label_set_use_markup (GTK_LABEL (cd->custom_label), TRUE);
	gtk_label_set_justify (GTK_LABEL (cd->custom_label),
			       GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (cd->custom_label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (cd->custom_hbox),
                            cd->custom_label,
			    FALSE, FALSE, 0);

	cd->custom_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (cd->custom_hbox),
                            cd->custom_entry,
			    FALSE, FALSE, 0);
	gtk_entry_set_text (GTK_ENTRY (cd->custom_entry),
			    cd->custom_format);
	g_signal_connect (cd->custom_entry, "changed",
			  G_CALLBACK (set_custom_format_cb),
			  cd);

	g_signal_connect (cd->props, "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
                          &cd->props);
	g_signal_connect (cd->props, "response",
			  G_CALLBACK (properties_response_cb),
                          cd);

	cd->custom_format_shown = FALSE;
	update_properties_for_format (cd, GTK_COMBO_BOX (combo), cd->format);

	/* valid values begin from 1 */
	if (cd->can_handle_format_12)
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
					  cd->format - 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
					  cd->format - 2);

        g_signal_connect (combo, "changed",
                          G_CALLBACK (set_format_cb), cd);

	/* Now set up the sensitivity based on mateconf key writability */
	setup_writability_sensitivity (cd, combo, label, KEY_FORMAT);
	setup_writability_sensitivity (cd, cd->custom_entry, cd->custom_label,
				       KEY_CUSTOM_FORMAT);
	setup_writability_sensitivity (cd, cd->showseconds_check, NULL, KEY_SHOW_SECONDS);
	setup_writability_sensitivity (cd, cd->showdate_check, NULL, KEY_SHOW_DATE);

	gtk_widget_show (cd->props);
#endif
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
	clock_utils_display_help (cd->applet, "clock", NULL);
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

	char copyright[] = \
		"Copyright \xc2\xa9 1998-2004 Free Software Foundation, Inc.";

	gtk_show_about_dialog(NULL,
		"program-name", _("Clock"),
		"authors", authors,
		"comments", _("The Clock displays the current time and date"),
		"copyright", copyright,
		"documenters", documenters,
		"logo-icon-name", CLOCK_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		"website", "http://matsusoft.com.ar/projects/mate/",
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
