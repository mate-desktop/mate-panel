#ifndef __CLOCK_LOCATION_H__
#define __CLOCK_LOCATION_H__

#include <time.h>
#include <glib.h>
#include <glib-object.h>
#include <libgweather/gweather.h>
#include "clock-utils.h"

G_BEGIN_DECLS

#define CLOCK_TYPE_LOCATION              (clock_location_get_type ())
G_DECLARE_DERIVABLE_TYPE (ClockLocation, clock_location, CLOCK, LOCATION, GObject)

struct _ClockLocationClass
{
    GObjectClass     parent_class;
    void (* weather_updated) (ClockLocation *location, GWeatherInfo *info);
    void (* set_current)     (ClockLocation *location);
};

GList*            clock_locations_append             (GList *locations, ClockLocation *loc);
GList*            clock_locations_merge              (GList *old, GList *new);
gboolean          clock_locations_has_location       (GList *locations, ClockLocation *loc);

ClockLocation*    clock_location_new                 (GWeatherLocation *gloc);
GVariant*         clock_location_serialize           (ClockLocation *loc);
ClockLocation*    clock_location_deserialize         (GVariant *serialized);
gboolean          clock_location_equal               (ClockLocation *one, ClockLocation *two);

const gchar*      clock_location_get_tzname          (ClockLocation *loc);
const gchar*      clock_location_get_tzid            (ClockLocation *loc);

const gchar*      clock_location_get_display_name    (ClockLocation *loc);
const gchar*      clock_location_get_name            (ClockLocation *loc);
const gchar*      clock_location_get_sort_name       (ClockLocation *loc);
const gchar*      clock_location_get_city            (ClockLocation *loc);
void              clock_location_set_timezone        (ClockLocation *loc, const gchar *timezone);
void              clock_location_get_coords          (ClockLocation *loc, gfloat *latitude, gfloat *longitude);
void              clock_location_localtime           (ClockLocation *loc, struct tm *tm);
gboolean          clock_location_is_current          (ClockLocation *loc);
void              clock_location_make_current        (ClockLocation *loc,
                                                      GFunc          callback,
                                                      gpointer       data,
                                                      GDestroyNotify destroy);
gboolean          clock_location_is_current_timezone (ClockLocation *loc);
const gchar*      clock_location_get_weather_code    (ClockLocation *loc);
GWeatherInfo*     clock_location_get_weather_info    (ClockLocation *loc);
void              clock_location_update_weather      (ClockLocation *loc);
GWeatherLocation* clock_location_get_glocation       (ClockLocation *loc);
gint              clock_location_get_offset          (ClockLocation *loc);

G_END_DECLS

#endif /* __CLOCK_LOCATION_H__ */
