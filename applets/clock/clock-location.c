#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "clock-location.h"
#include "clock-marshallers.h"
#include "set-timezone.h"
#include "system-timezone.h"

typedef struct {
        gchar *name;
        gchar *city;

        SystemTimezone *systz;

        gchar *timezone;

        gchar *tzname;

        gfloat latitude;
        gfloat longitude;

        gchar *weather_code;
        WeatherInfo *weather_info;
        guint weather_timeout;
        guint weather_retry_time;

        TempUnit temperature_unit;
        SpeedUnit speed_unit;
} ClockLocationPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClockLocation, clock_location, G_TYPE_OBJECT)

#define WEATHER_TIMEOUT_BASE 30
#define WEATHER_TIMEOUT_MAX  1800
#define WEATHER_EMPTY_CODE   "-"

enum {
        WEATHER_UPDATED,
        SET_CURRENT,
        LAST_SIGNAL
};

static guint location_signals[LAST_SIGNAL] = { 0 };

static void clock_location_finalize (GObject *);
static void clock_location_set_tz (ClockLocation *this);
static void clock_location_unset_tz (ClockLocation *this);
static gboolean update_weather_info (gpointer data);
static void setup_weather_updates (ClockLocation *loc);

static gchar *clock_location_get_valid_weather_code (const gchar *code);

ClockLocation *
clock_location_find_and_ref (GList       *locations,
                             const gchar *name,
                             const gchar *city,
                             const gchar *timezone,
                             gfloat       latitude,
                             gfloat       longitude,
                             const gchar *code)
{
        GList *l;
        ClockLocationPrivate *priv;

        for (l = locations; l != NULL; l = l->next) {
                priv = clock_location_get_instance_private (l->data);

                if (priv->latitude == latitude &&
                    priv->longitude == longitude &&
                    g_strcmp0 (priv->weather_code, code) == 0 &&
                    g_strcmp0 (priv->timezone, timezone) == 0 &&
                    g_strcmp0 (priv->city, city) == 0 &&
                    g_strcmp0 (priv->name, name) == 0)
                        break;
        }

        if (l != NULL)
                return g_object_ref (CLOCK_LOCATION (l->data));
        else
                return NULL;
}

ClockLocation *
clock_location_new (const gchar *name, const gchar *city,
                    const gchar *timezone,
                    gfloat latitude, gfloat longitude,
                    const gchar *code, WeatherPrefs *prefs)
{
        ClockLocation *this;
        ClockLocationPrivate *priv;

        this = g_object_new (CLOCK_LOCATION_TYPE, NULL);
        priv = clock_location_get_instance_private (this);

        priv->name = g_strdup (name);
        priv->city = g_strdup (city);
        priv->timezone = g_strdup (timezone);

        /* initialize priv->tzname */
        clock_location_set_tz (this);
        clock_location_unset_tz (this);

        priv->latitude = latitude;
        priv->longitude = longitude;

        priv->weather_code = clock_location_get_valid_weather_code (code);

        if (prefs) {
                priv->temperature_unit = prefs->temperature_unit;
                priv->speed_unit = prefs->speed_unit;
        }

        setup_weather_updates (this);

        return this;
}

static ClockLocation *current_location = NULL;

static void
clock_location_class_init (ClockLocationClass *this_class)
{
        GObjectClass *g_obj_class = G_OBJECT_CLASS (this_class);

        g_obj_class->finalize = clock_location_finalize;

        location_signals[WEATHER_UPDATED] =
                g_signal_new ("weather-updated",
                              G_OBJECT_CLASS_TYPE (g_obj_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (ClockLocationClass, weather_updated),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);

        location_signals[SET_CURRENT] =
                g_signal_new ("set-current",
                              G_OBJECT_CLASS_TYPE (g_obj_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (ClockLocationClass, set_current),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
network_changed (GNetworkMonitor *monitor,
                 gboolean         available,
                 ClockLocation   *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (available) {
                priv->weather_retry_time = WEATHER_TIMEOUT_BASE;
                update_weather_info (loc);
        }
}

static void
clock_location_init (ClockLocation *this)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (this);
        GNetworkMonitor *monitor;

        priv->name = NULL;
        priv->city = NULL;

        priv->systz = system_timezone_new ();

        priv->timezone = NULL;

        priv->tzname = NULL;

        priv->latitude = 0;
        priv->longitude = 0;

        monitor = g_network_monitor_get_default();
        g_signal_connect (monitor, "network-changed",
                          G_CALLBACK (network_changed), this);

        priv->temperature_unit = TEMP_UNIT_CENTIGRADE;
        priv->speed_unit = SPEED_UNIT_MS;
}

static void
clock_location_finalize (GObject *g_obj)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (CLOCK_LOCATION(g_obj));
        GNetworkMonitor      *monitor;

        monitor = g_network_monitor_get_default ();
        g_signal_handlers_disconnect_by_func (monitor,
                                              G_CALLBACK (network_changed),
                                              CLOCK_LOCATION (g_obj));

        if (priv->name) {
                g_free (priv->name);
                priv->name = NULL;
        }

        if (priv->city) {
                g_free (priv->city);
                priv->city = NULL;
        }

        if (priv->systz) {
                g_object_unref (priv->systz);
                priv->systz = NULL;
        }

        if (priv->timezone) {
                g_free (priv->timezone);
                priv->timezone = NULL;
        }

        if (priv->tzname) {
                g_free (priv->tzname);
                priv->tzname = NULL;
        }

        if (priv->weather_code) {
                g_free (priv->weather_code);
                priv->weather_code = NULL;
        }

        if (priv->weather_info) {
                weather_info_free (priv->weather_info);
                priv->weather_info = NULL;
        }

        if (priv->weather_timeout) {
                g_source_remove (priv->weather_timeout);
                priv->weather_timeout = 0;
        }

        G_OBJECT_CLASS (clock_location_parent_class)->finalize (g_obj);
}

const gchar *
clock_location_get_display_name (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (priv->name && priv->name[0])
                return priv->name;
        else
                return priv->city;
}

const gchar *
clock_location_get_name (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->name;
}

void
clock_location_set_name (ClockLocation *loc, const gchar *name)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (priv->name) {
                g_free (priv->name);
                priv->name = NULL;
        }

        priv->name = g_strdup (name);
}

const gchar *
clock_location_get_city (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->city;
}

void
clock_location_set_city (ClockLocation *loc, const gchar *city)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (priv->city) {
                g_free (priv->city);
                priv->city = NULL;
        }

        priv->city = g_strdup (city);
}

gchar *
clock_location_get_timezone (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->timezone;
}

void
clock_location_set_timezone (ClockLocation *loc, const gchar *timezone)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (priv->timezone) {
                g_free (priv->timezone);
                priv->timezone = NULL;
        }

        priv->timezone = g_strdup (timezone);
}

gchar *
clock_location_get_tzname (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->tzname;
}

void
clock_location_get_coords (ClockLocation *loc, gfloat *latitude,
                               gfloat *longitude)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        *latitude = priv->latitude;
        *longitude = priv->longitude;
}

void
clock_location_set_coords (ClockLocation *loc, gfloat latitude,
                               gfloat longitude)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        priv->latitude = latitude;
        priv->longitude = longitude;
}

static void
clock_location_set_tzname (ClockLocation *this, const char *tzname)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (CLOCK_LOCATION(this));

        if (priv->tzname) {
                if (strcmp (priv->tzname, tzname) == 0) {
                        return;
                }

                g_free (priv->tzname);
                priv->tzname = NULL;
        }

        if (tzname) {
                priv->tzname = g_strdup (tzname);
        } else {
                priv->tzname = NULL;
        }
}

static void
clock_location_set_tz (ClockLocation *this)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (this);

        time_t now_t;
        struct tm now;

        if (priv->timezone == NULL) {
                return;
        }

        setenv ("TZ", priv->timezone, 1);
        tzset();

        now_t = time (NULL);
        localtime_r (&now_t, &now);

        if (now.tm_isdst > 0) {
                clock_location_set_tzname (this, tzname[1]);
        } else {
                clock_location_set_tzname (this, tzname[0]);
        }
}

static void
clock_location_unset_tz (ClockLocation *this)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (this);
        const char *env_timezone;

        if (priv->timezone == NULL) {
                return;
        }

        env_timezone = system_timezone_get_env (priv->systz);

        if (env_timezone) {
                setenv ("TZ", env_timezone, 1);
        } else {
                unsetenv ("TZ");
        }
        tzset();
}

void
clock_location_localtime (ClockLocation *loc, struct tm *tm)
{
        time_t now;

        clock_location_set_tz (loc);

        time (&now);
        localtime_r (&now, tm);

        clock_location_unset_tz (loc);
}

gboolean
clock_location_is_current_timezone (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        const char *zone;

        zone = system_timezone_get (priv->systz);

        if (zone)
                return strcmp (zone, priv->timezone) == 0;
        else
                return clock_location_get_offset (loc) == 0;
}

gboolean
clock_location_is_current (ClockLocation *loc)
{
        if (current_location == loc)
                return TRUE;
        else if (current_location != NULL)
                return FALSE;

        if (clock_location_is_current_timezone (loc)) {
                /* Note that some code in clock.c depends on the fact that
                 * calling this function can set the current location if
                 * there's none */
                current_location = loc;
                g_object_add_weak_pointer (G_OBJECT (current_location),
                                           (gpointer *)&current_location);
                g_signal_emit (current_location, location_signals[SET_CURRENT],
                               0, NULL);

                return TRUE;
        }

        return FALSE;
}


glong
clock_location_get_offset (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        glong sys_timezone, local_timezone;
        glong offset;
        time_t t;
        struct tm *tm;

        t = time (NULL);

        unsetenv ("TZ");
        tm = localtime (&t);
        sys_timezone = timezone;

        if (tm->tm_isdst > 0) {
                sys_timezone -= 3600;
        }

        setenv ("TZ", priv->timezone, 1);
        tm = localtime (&t);
        local_timezone = timezone;

        if (tm->tm_isdst > 0) {
                local_timezone -= 3600;
        }

        offset = local_timezone - sys_timezone;

        clock_location_unset_tz (loc);

        return offset;
}

typedef struct {
        ClockLocation *location;
        GFunc callback;
        gpointer data;
        GDestroyNotify destroy;
} MakeCurrentData;

static void
make_current_cb (gpointer data, GError *error)
{
        MakeCurrentData *mcdata = data;

        if (error == NULL) {
                if (current_location)
                        g_object_remove_weak_pointer (G_OBJECT (current_location),
                                                      (gpointer *)&current_location);
                current_location = mcdata->location;
                g_object_add_weak_pointer (G_OBJECT (current_location),
                                           (gpointer *)&current_location);
                g_signal_emit (current_location, location_signals[SET_CURRENT],
                               0, NULL);
        }

        if (mcdata->callback)
                mcdata->callback (mcdata->data, error);
}

static void
free_make_current_data (gpointer data)
{
        MakeCurrentData *mcdata = data;

        if (mcdata->destroy)
                mcdata->destroy (mcdata->data);

        g_object_unref (mcdata->location);
        g_free (mcdata);
}

void
clock_location_make_current (ClockLocation *loc,
                             GFunc          callback,
                             gpointer       data,
                             GDestroyNotify destroy)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        gchar *filename;
        MakeCurrentData *mcdata;

        if (loc == current_location) {
                if (destroy)
                        destroy (data);
                return;
        }

        if (clock_location_is_current_timezone (loc)) {
                if (current_location)
                        g_object_remove_weak_pointer (G_OBJECT (current_location),
                                                      (gpointer *)&current_location);
                current_location = loc;
                g_object_add_weak_pointer (G_OBJECT (current_location),
                                           (gpointer *)&current_location);
                g_signal_emit (current_location, location_signals[SET_CURRENT],
                               0, NULL);
                if (callback)
                               callback (data, NULL);
                if (destroy)
                        destroy (data);
                return;
        }

        mcdata = g_new (MakeCurrentData, 1);

        mcdata->location = g_object_ref (loc);
        mcdata->callback = callback;
        mcdata->data = data;
        mcdata->destroy = destroy;

        filename = g_build_filename (SYSTEM_ZONEINFODIR, priv->timezone, NULL);
        set_system_timezone_async (filename,
                                   (GFunc)make_current_cb,
                                   mcdata,
                                   free_make_current_data);
        g_free (filename);
}

static gchar *
clock_location_get_valid_weather_code (const gchar *code)
{
        if (!code || code[0] == '\0')
                return g_strdup (WEATHER_EMPTY_CODE);
        else
                return g_strdup (code);
}

const gchar *
clock_location_get_weather_code (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->weather_code;
}

void
clock_location_set_weather_code (ClockLocation *loc, const gchar *code)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        g_free (priv->weather_code);
        priv->weather_code = clock_location_get_valid_weather_code (code);

        setup_weather_updates (loc);
}

WeatherInfo *
clock_location_get_weather_info (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return priv->weather_info;
}

static void
set_weather_update_timeout (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        guint timeout;

        if (!weather_info_network_error (priv->weather_info)) {
                /* The last update succeeded; set the next update to
                 * happen in half an hour, and reset the retry timer.
                 */
                timeout = WEATHER_TIMEOUT_MAX;
                priv->weather_retry_time = WEATHER_TIMEOUT_BASE;
        } else {
                /* The last update failed; set the next update
                 * according to the retry timer, and exponentially
                 * back off the retry timer.
                 */
                timeout = priv->weather_retry_time;
                priv->weather_retry_time *= 2;
                if (priv->weather_retry_time > WEATHER_TIMEOUT_MAX)
                        priv->weather_retry_time = WEATHER_TIMEOUT_MAX;
        }

        if (priv->weather_timeout)
                g_source_remove (priv->weather_timeout);
        priv->weather_timeout =
                g_timeout_add_seconds (timeout, update_weather_info, loc);
}

static void
weather_info_updated (WeatherInfo *info, gpointer data)
{
        ClockLocation *loc = data;
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        set_weather_update_timeout (loc);
        g_signal_emit (loc, location_signals[WEATHER_UPDATED],
                       0, priv->weather_info);
}

static gboolean
update_weather_info (gpointer data)
{
        ClockLocation *loc = (ClockLocation *) data;
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        WeatherPrefs prefs = {
                FORECAST_STATE,
                FALSE,
                NULL,
                TEMP_UNIT_CENTIGRADE,
                SPEED_UNIT_MS,
                PRESSURE_UNIT_MB,
                DISTANCE_UNIT_KM
        };

        // set temperature and speed units only if different from
        // invalid/default
        if (priv->temperature_unit > TEMP_UNIT_DEFAULT)
                prefs.temperature_unit = priv->temperature_unit;
        if (priv->speed_unit > SPEED_UNIT_DEFAULT)
                prefs.speed_unit = priv->speed_unit;

        weather_info_abort (priv->weather_info);
        weather_info_update (priv->weather_info,
                             &prefs, weather_info_updated, loc);

        return TRUE;
}

static gchar *
rad2dms (gfloat lat, gfloat lon)
{
        gchar h, h2;
        gfloat d, deg, min, d2, deg2, min2;

        h = lat > 0 ? 'N' : 'S';
        d = fabs (lat);
        deg = floor (d);
        min = floor (60 * (d - deg));
        h2 = lon > 0 ? 'E' : 'W';
        d2 = fabs (lon);
        deg2 = floor (d2);
        min2 = floor (60 * (d2 - deg2));
        return g_strdup_printf ("%02d-%02d%c %02d-%02d%c",
                                (int)deg, (int)min, h,
                                (int)deg2, (int)min2, h2);
}

static void
setup_weather_updates (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        WeatherLocation *wl;
        WeatherPrefs prefs = {
                FORECAST_STATE,
                FALSE,
                NULL,
                TEMP_UNIT_CENTIGRADE,
                SPEED_UNIT_MS,
                PRESSURE_UNIT_MB,
                DISTANCE_UNIT_KM
        };

        gchar *dms;

        prefs.temperature_unit = priv->temperature_unit;
        prefs.speed_unit = priv->speed_unit;

        if (priv->weather_info) {
                weather_info_free (priv->weather_info);
                priv->weather_info = NULL;
        }

        if (priv->weather_timeout) {
                g_source_remove (priv->weather_timeout);
                priv->weather_timeout = 0;
        }

        if (!priv->weather_code ||
            strcmp (priv->weather_code, WEATHER_EMPTY_CODE) == 0)
                return;

        dms = rad2dms (priv->latitude, priv->longitude);
        wl = weather_location_new (priv->city, priv->weather_code,
                                   NULL, NULL, dms, NULL, NULL);

        priv->weather_info =
                weather_info_new (wl, &prefs, weather_info_updated, loc);

        set_weather_update_timeout (loc);

        weather_location_free (wl);
        g_free (dms);
}

void
clock_location_set_weather_prefs (ClockLocation *loc,
                                  WeatherPrefs *prefs)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        priv->temperature_unit = prefs->temperature_unit;
        priv->speed_unit = prefs->speed_unit;

        update_weather_info (loc);
}

