#ifndef __CLOCK_LOCATION_H__
#define __CLOCK_LOCATION_H__

#include <time.h>
#include <glib.h>
#include <glib-object.h>

#if GTK_CHECK_VERSION (3, 0, 0)
#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather.h>
#else
#include <libmateweather/weather.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_LOCATION_TYPE         (clock_location_get_type ())
#define CLOCK_LOCATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CLOCK_LOCATION_TYPE, ClockLocation))
#define CLOCK_LOCATION_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), CLOCK_LOCATION_TYPE, ClockLocationClass))
#define IS_CLOCK_LOCATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CLOCK_LOCATION_TYPE))
#define IS_CLOCK_LOCATION_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), CLOCK_LOCATION_TYPE))
#define CLOCK_LOCATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CLOCK_LOCATION_TYPE, ClockLocationClass))

typedef struct
{
        GObject g_object;
} ClockLocation;

typedef struct
{
        GObjectClass g_object_class;

#if GTK_CHECK_VERSION (3, 0, 0)
	void (* weather_updated) (ClockLocation *location, GWeatherInfo *info);
#else
	void (* weather_updated) (ClockLocation *location, WeatherInfo *info);
#endif

	void (* set_current) (ClockLocation *location);
} ClockLocationClass;

GType clock_location_get_type (void);

#if GTK_CHECK_VERSION (3, 0, 0)
ClockLocation *clock_location_new (const gchar *name, const gchar *city,
				   const gchar *timezone,
				   gfloat latitude, gfloat longitude,
				   const gchar *code,
				   GWeatherTemperatureUnit temperature_unit,
				   GWeatherSpeedUnit speed_unit);
#else
ClockLocation *clock_location_new (const gchar *name, const gchar *city,
				   const gchar *timezone,
				   gfloat latitude, gfloat longitude,
				   const gchar *code,
				   WeatherPrefs *prefs);
#endif

ClockLocation *clock_location_find_and_ref (GList       *locations,
                                            const gchar *name,
                                            const gchar *city,
                                            const gchar *timezone,
                                            gfloat       latitude,
                                            gfloat       longitude,
                                            const gchar *code);

gchar *clock_location_get_tzname (ClockLocation *loc);

const gchar *clock_location_get_display_name (ClockLocation *loc);

const gchar *clock_location_get_name (ClockLocation *loc);
void clock_location_set_name (ClockLocation *loc, const gchar *name);

const gchar *clock_location_get_city (ClockLocation *loc);
void clock_location_set_city (ClockLocation *loc, const gchar *city);

const gchar *clock_location_get_timezone (ClockLocation *loc);
void clock_location_set_timezone (ClockLocation *loc, const gchar *timezone);

void clock_location_get_coords (ClockLocation *loc, gfloat *latitude, gfloat *longitude);
void clock_location_set_coords (ClockLocation *loc, gfloat latitude, gfloat longitude);

void clock_location_localtime (ClockLocation *loc, struct tm *tm);

gboolean clock_location_is_current (ClockLocation *loc);
void clock_location_make_current (ClockLocation *loc,
				  GFunc          callback,
				  gpointer       data,
				  GDestroyNotify destroy);
gboolean clock_location_is_current_timezone (ClockLocation *loc);

const gchar *clock_location_get_weather_code (ClockLocation *loc);
void         clock_location_set_weather_code (ClockLocation *loc, const gchar *code);
#if GTK_CHECK_VERSION (3, 0, 0)
GWeatherInfo *clock_location_get_weather_info (ClockLocation *loc);
void         clock_location_set_weather_prefs (ClockLocation *loc,
					       GWeatherTemperatureUnit temperature_unit,
					       GWeatherSpeedUnit speed_unit);
#else
WeatherInfo *clock_location_get_weather_info (ClockLocation *loc);
void         clock_location_set_weather_prefs (ClockLocation *loc,
					       WeatherPrefs *weather_prefs);
#endif

glong clock_location_get_offset (ClockLocation *loc);

#ifdef __cplusplus
}
#endif
#endif /* __CLOCK_LOCATION_H__ */
