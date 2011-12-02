#ifndef __CLOCK_SUNPOS_H__
#define __CLOCK_SUNPOS_H__

#include <glib.h>

void sun_position(time_t unix_time, gdouble *lat, gdouble *lon);

#endif
