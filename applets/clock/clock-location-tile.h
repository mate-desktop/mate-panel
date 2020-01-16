#ifndef __CLOCK_LOCATION_TILE_H__
#define __CLOCK_LOCATION_TILE_H__

#include <gtk/gtk.h>

#include "clock.h"
#include "clock-face.h"
#include "clock-location.h"
#include "clock-utils.h"

G_BEGIN_DECLS

#define CLOCK_TYPE_LOCATION_TILE          (clock_location_tile_get_type ())
G_DECLARE_DERIVABLE_TYPE (ClockLocationTile, clock_location_tile, CLOCK, LOCATION_TILE, GtkBin)

struct _ClockLocationTileClass
{
        GtkBinClass parent_class;

        void (* tile_pressed) (ClockLocationTile *tile);
        int  (* need_clock_format) (ClockLocationTile *tile);
};

ClockLocationTile *clock_location_tile_new (ClockLocation *loc,
                                            ClockFaceSize size);

ClockLocation *clock_location_tile_get_location (ClockLocationTile *this);

void weather_info_setup_tooltip (GWeatherInfo *info, ClockLocation *location, GtkTooltip *tip,
                                 ClockFormat clock_format);

void clock_location_tile_refresh (ClockLocationTile *this,
                                  gboolean           force_refresh);

G_END_DECLS

#endif /* __CLOCK_H__ */
