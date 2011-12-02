#ifndef __CLOCK_MAP_H__
#define __CLOCK_MAP_H__

#include "clock.h"
#include "clock-location.h"

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_MAP_TYPE         (clock_map_get_type ())
#define CLOCK_MAP(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CLOCK_MAP_TYPE, ClockMap))
#define CLOCK_MAP_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), CLOCK_MAP_TYPE, ClockMapClass))
#define IS_CLOCK_MAP(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CLOCK_MAP_TYPE))
#define IS_CLOCK_MAP_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), CLOCK_MAP_TYPE))
#define CLOCK_MAP_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CLOCK_MAP_TYPE, ClockMapClass))

typedef struct
{
        GtkWidget parent;
} ClockMap;

typedef struct
{
        GtkWidgetClass parent_class;

	GList *(* need_locations) (ClockMap *map);
} ClockMapClass;

GType clock_map_get_type (void);

ClockMap *clock_map_new (void);
void clock_map_refresh (ClockMap *this);
void clock_map_update_time (ClockMap *this);
void clock_map_blink_location (ClockMap *this, ClockLocation *loc);


#ifdef __cplusplus
}
#endif
#endif /* __CLOCK_MAP_H__ */
