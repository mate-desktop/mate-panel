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

enum {
    PROP_0,
    PROP_LOCATION,
    NUM_PROPERTIES
};

enum {
    WEATHER_UPDATED,
    SET_CURRENT,
    LAST_SIGNAL
};

typedef struct {
        SystemTimezone   *systz;
        GWeatherLocation *glocation;
        GWeatherInfo     *gweather_info;
        gulong            network_id;
        gulong            weather_id;
} ClockLocationPrivate;

static ClockLocation *current_location = NULL;

static guint location_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (ClockLocation, clock_location, G_TYPE_OBJECT)

static void clock_location_dispose (GObject *object);
static void clock_location_finalize (GObject *);
static void clock_location_set_tz (ClockLocation *this);
static void clock_location_unset_tz (ClockLocation *this);
static void weather_info_updated (GWeatherInfo *info, gpointer data);

static void clock_location_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void clock_location_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gint clock_location_compare (ClockLocation *one, ClockLocation *two)
{
    g_return_val_if_fail (CLOCK_IS_LOCATION(one), FALSE);
    g_return_val_if_fail (CLOCK_IS_LOCATION(two), FALSE);
    ClockLocationPrivate *priv_one;
    ClockLocationPrivate *priv_two;

    priv_one = clock_location_get_instance_private (one);
    priv_two = clock_location_get_instance_private (two);

    if (gweather_location_equal (priv_one->glocation, priv_two->glocation)) {
        return 0;
    }
    return -1;
}

gboolean clock_location_equal (ClockLocation *one, ClockLocation *two)
{
    if (clock_location_compare (one, two) == 0)
        return TRUE;
    else
        return FALSE;
}

gboolean clock_locations_has_location (GList *list, ClockLocation *loc)
{
    GList *found = NULL;
    found = g_list_find_custom (list, loc, clock_location_compare);
    if (found) {
        return TRUE;
    } else {
        return FALSE;
    }
}

GList* clock_locations_append (GList *locations, ClockLocation *loc)
{
    if (!clock_locations_has_location (locations, loc)) {
        locations = g_list_append(locations, loc);
    }
    return locations;
}

GList* clock_locations_merge (GList *old, GList *new)
{
    GList *l, *result = NULL;

    for (l = old; l != NULL; l = l->next) {
        if (clock_locations_has_location (new, l->data)) {
            result = g_list_append (result, g_object_ref (l->data));
        }
    }
    g_list_free_full (old, g_object_unref);

    for (l = new; l != NULL; l = l->next) {
        result = clock_locations_append (result, l->data);
    }
    return result;
}

ClockLocation *clock_location_new (GWeatherLocation *gloc)
{
    return g_object_new (CLOCK_TYPE_LOCATION, "location", gloc, NULL);
}

GVariant* clock_location_serialize (ClockLocation *loc)
{
    g_return_val_if_fail (CLOCK_IS_LOCATION(loc), NULL);
    GVariant *value;
    ClockLocationPrivate *priv;

    priv = clock_location_get_instance_private (loc);

    if (clock_location_is_current(loc)) {
        value = g_variant_new ("(bv)", TRUE, gweather_location_serialize (priv->glocation));
    } else {
        if (priv->glocation != NULL) {
            value = g_variant_new ("(bv)", FALSE, gweather_location_serialize (priv->glocation));
        }
    }

    return value;
}

ClockLocation* clock_location_deserialize (GVariant *serialized)
{
    gboolean current;
    GVariant *value;
    GWeatherLocation *world, *gloc;
    ClockLocation *location;

    g_variant_get (serialized, "(bv)", &current, &value);

    world = gweather_location_get_world ();
    gloc = gweather_location_deserialize (world, value);

    location = clock_location_new(gloc);
    return location;
}

static void clock_location_set_gweather (ClockLocation *self, GWeatherLocation *loc)
{
    ClockLocationPrivate *priv;

    priv = clock_location_get_instance_private (self);

    if (priv->glocation != NULL && gweather_location_equal (priv->glocation, loc))
            return;

    if (priv->weather_id != 0) {
        g_signal_handler_disconnect (priv->gweather_info, priv->weather_id);
        priv->weather_id = 0;
    }

    if (priv->glocation != NULL) {
        gweather_location_unref (priv->glocation);
        priv->glocation = NULL;
    }
    if (priv->gweather_info != NULL) {
        gweather_info_abort (priv->gweather_info);
        g_object_unref (priv->gweather_info);
        priv->gweather_info = NULL;
    }

    priv->glocation = gweather_location_ref (loc);
    GWeatherProvider providers = GWEATHER_PROVIDER_METAR
                                 | GWEATHER_PROVIDER_YAHOO
                                 | GWEATHER_PROVIDER_YR_NO;
    priv->gweather_info = g_object_new (GWEATHER_TYPE_INFO, "location", priv->glocation, "enabled-providers", providers, NULL);
    priv->weather_id = g_signal_connect (priv->gweather_info, "updated", G_CALLBACK (weather_info_updated), self);
}

static void clock_location_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    ClockLocation *self;

    self = CLOCK_LOCATION (object);

    switch (prop_id)
    {
        case PROP_LOCATION:
            clock_location_set_gweather (self, g_value_get_boxed (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void clock_location_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    ClockLocation *location;
    ClockLocationPrivate *priv;

    location = CLOCK_LOCATION (object);
    priv = clock_location_get_instance_private (location);

    switch (prop_id)
    {
        case PROP_LOCATION:
            g_value_set_object (value, priv->glocation);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
clock_location_class_init (ClockLocationClass *this_class)
{
        GObjectClass *g_obj_class = G_OBJECT_CLASS (this_class);

        g_obj_class->dispose = clock_location_dispose;
        g_obj_class->finalize = clock_location_finalize;
        g_obj_class->set_property = clock_location_set_property;
        g_obj_class->get_property = clock_location_get_property;

        g_object_class_install_property (g_obj_class,
                PROP_LOCATION,
                g_param_spec_boxed ("location",
                    NULL,
                    NULL,
                    GWEATHER_TYPE_LOCATION,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        location_signals[WEATHER_UPDATED] =
                g_signal_new ("weather-updated",
                              G_OBJECT_CLASS_TYPE (g_obj_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (ClockLocationClass, weather_updated),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, G_TYPE_OBJECT);

        location_signals[SET_CURRENT] =
                g_signal_new ("set-current",
                              G_OBJECT_CLASS_TYPE (g_obj_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (ClockLocationClass, set_current),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

void clock_location_update_weather (ClockLocation *loc)
{
    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

    if (priv->gweather_info)
        gweather_info_update (priv->gweather_info);
}

static void
network_changed (GNetworkMonitor *monitor,
                 gboolean         available,
                 ClockLocation   *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        if (available) {
                gweather_info_update (priv->gweather_info);
        }
}

static void
clock_location_init (ClockLocation *this)
{
    ClockLocationPrivate *priv = clock_location_get_instance_private (this);
    GNetworkMonitor *monitor;

    priv->systz = system_timezone_new ();
    priv->glocation = NULL;
    priv->gweather_info = NULL;
    priv->weather_id = 0;

    monitor = g_network_monitor_get_default();
    priv->network_id = g_signal_connect (monitor, "network-changed",
            G_CALLBACK (network_changed), this);
}

static void clock_location_dispose (GObject *object)
{
    ClockLocation *loc;
    ClockLocationPrivate *priv;

    loc = CLOCK_LOCATION (object);
    priv = clock_location_get_instance_private (loc);

    if (priv->weather_id != 0) {
        g_signal_handler_disconnect (priv->gweather_info, priv->weather_id);
        priv->weather_id = 0;
    }

    if (priv->systz != NULL) {
        g_object_unref (priv->systz);
        priv->systz = NULL;
    }
    if (priv->glocation != NULL) {
        gweather_location_unref (priv->glocation);
        priv->glocation = NULL;
    }
    if (priv->gweather_info != NULL) {
        g_object_unref (priv->gweather_info);
        priv->gweather_info = NULL;
    }

    G_OBJECT_CLASS (clock_location_parent_class)->dispose (object);
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

        G_OBJECT_CLASS (clock_location_parent_class)->finalize (g_obj);
}

const gchar *
clock_location_get_display_name (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        return gweather_location_get_name (priv->glocation);
}

const gchar *
clock_location_get_name (ClockLocation *loc)
{
    return clock_location_get_display_name (loc);
}

const gchar *
clock_location_get_sort_name (ClockLocation *loc)
{
    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
    return gweather_location_get_sort_name (priv->glocation);
}

const gchar *
clock_location_get_city (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

        return gweather_location_get_city_name (priv->glocation);
}

const gchar *
clock_location_get_tzname (ClockLocation *loc)
{
    ClockLocationPrivate *priv;
    GWeatherTimezone *zone;
    const gchar *name;

    priv = clock_location_get_instance_private (loc);
    zone = gweather_location_get_timezone (priv->glocation);
    name = gweather_timezone_get_name (zone);
    if (name != NULL) {
        return name;
    } else {
        return gweather_timezone_get_tzid (zone);
    }
}

const gchar *
clock_location_get_tzid (ClockLocation *loc)
{
    ClockLocationPrivate *priv;
    GWeatherTimezone *zone;

    priv = clock_location_get_instance_private (loc);
    zone = gweather_location_get_timezone (priv->glocation);
    return gweather_timezone_get_tzid (zone);
}

void
clock_location_get_coords (ClockLocation *loc, gfloat *latitude,
                               gfloat *longitude)
{
    double lat, lon;

    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
    gweather_location_get_coords (priv->glocation, &lat, &lon);

    *latitude = (gfloat)lat;
    *longitude = (gfloat)lon;
    return;
}

static void
clock_location_set_tz (ClockLocation *this)
{
    g_return_if_fail (CLOCK_IS_LOCATION(this));
        const gchar* zone;
        time_t now_t;
        struct tm now;

        zone = clock_location_get_tzid (this);
        if (zone != NULL) {
            setenv ("TZ", zone, 1);
            tzset();
            now_t = time (NULL);
            localtime_r (&now_t, &now);
        }
}

static void
clock_location_unset_tz (ClockLocation *this)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (this);
        const char *env_timezone;

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
    g_return_val_if_fail (CLOCK_IS_LOCATION(loc), FALSE);
    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
    const char *zone, *tzid;

    zone = system_timezone_get (priv->systz);
    tzid = clock_location_get_tzid (loc);

    if (zone && tzid != NULL) {
        return strcmp (zone, tzid) == 0;
    } else {
        return clock_location_get_offset (loc) == 0;
    }
}

gboolean
clock_location_is_current (ClockLocation *loc)
{
        if (current_location == loc) {
                return TRUE;
        } else if (current_location != NULL) {
                return FALSE;
        }

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

gint
clock_location_get_offset (ClockLocation *loc)
{
        ClockLocationPrivate *priv = clock_location_get_instance_private (loc);
        GWeatherTimezone *zone;

        zone = gweather_location_get_timezone (priv->glocation);
        if (zone != NULL) {
            return gweather_timezone_get_offset (zone);
        } else {
            return 0;
        }
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
        else
                g_error_free (error);
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

        filename = g_build_filename (SYSTEM_ZONEINFODIR, clock_location_get_tzid (loc), NULL);
        set_system_timezone_async (filename,
                                   (GFunc)make_current_cb,
                                   mcdata,
                                   free_make_current_data);
        g_free (filename);
}

GWeatherInfo *
clock_location_get_weather_info (ClockLocation *loc)
{
    g_return_val_if_fail (CLOCK_IS_LOCATION(loc), NULL);
    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

    return g_object_ref (priv->gweather_info);
}

GWeatherLocation*  clock_location_get_glocation (ClockLocation *loc)
{
    g_return_val_if_fail (CLOCK_IS_LOCATION(loc), NULL);

    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

    return gweather_location_ref(priv->glocation);
}

static void
weather_info_updated (GWeatherInfo *info, gpointer data)
{
    g_return_if_fail (GWEATHER_IS_INFO(info));
    ClockLocation *loc = data;
    g_return_if_fail (CLOCK_IS_LOCATION(loc));
    ClockLocationPrivate *priv = clock_location_get_instance_private (loc);

    if (gweather_info_network_error (priv->gweather_info))
        return;
    if (priv->gweather_info) {
        g_signal_emit (loc, location_signals[WEATHER_UPDATED], 0, priv->gweather_info);
    }
}
