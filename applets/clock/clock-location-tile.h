#ifndef __CLOCK_LOCATION_TILE_H__
#define __CLOCK_LOCATION_TILE_H__

#include <gtk/gtk.h>

#include "clock.h"
#include "clock-face.h"
#include "clock-location.h"
#include "clock-utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_LOCATION_TILE_TYPE         (clock_location_tile_get_type ())
#define CLOCK_LOCATION_TILE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CLOCK_LOCATION_TILE_TYPE, ClockLocationTile))
#define CLOCK_LOCATION_TILE_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), CLOCK_LOCATION_TILE_TYPE, ClockLocationTileClass))
#define IS_CLOCK_LOCATION_TILE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CLOCK_LOCATION_TILE_TYPE))
#define IS_CLOCK_LOCATION_TILE_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), CLOCK_LOCATION_TILE_TYPE))
#define CLOCK_LOCATION_TILE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CLOCK_LOCATION_TILE_TYPE, ClockLocationTileClass))

typedef struct
{
        GtkBin parent;
} ClockLocationTile;

typedef struct
{
        GtkBinClass parent_class;

        void (* tile_pressed) (ClockLocationTile *tile);
        int  (* need_clock_format) (ClockLocationTile *tile);
} ClockLocationTileClass;

GType clock_location_tile_get_type (void);

ClockLocationTile *clock_location_tile_new (ClockLocation *loc,
                                            ClockFaceSize size);

ClockLocation *clock_location_tile_get_location (ClockLocationTile *this);

void weather_info_setup_tooltip (WeatherInfo *info, ClockLocation *location, GtkTooltip *tip,
                                 ClockFormat clock_format);

void clock_location_tile_refresh (ClockLocationTile *this,
                                  gboolean           force_refresh);

#ifdef __cplusplus
}
#endif
#endif /* __CLOCK_H__ */
