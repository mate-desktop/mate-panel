/*
 * clock-utils.h
 *
 * Copyright (C) 2007 Vincent Untz <vuntz@gnome.org>
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
 *      Vincent Untz <vuntz@gnome.org>
 *
 * Most of the original code comes from clock.c
 */

#ifndef __CLOCK_UTILS_H__
#define __CLOCK_UTILS_H__

#include <gtk/gtk.h>
#include <libgweather/gweather.h>

G_BEGIN_DECLS

/* Needs to match the indices in the combo of the prefs dialog */
typedef enum {
	CLOCK_FORMAT_INVALID = 0,
	CLOCK_FORMAT_12,
	CLOCK_FORMAT_24,
	CLOCK_FORMAT_UNIX,
	CLOCK_FORMAT_INTERNET,
	CLOCK_FORMAT_CUSTOM
} ClockFormat;

gboolean clock_locale_supports_am_pm (void);
ClockFormat clock_locale_format (void);

void clock_utils_display_help (GtkWidget  *widget,
			       const char *doc_id,
			       const char *link_id);

GdkPixbuf *clock_utils_pixbuf_from_svg_resource_at_size (const char *resource,
	                                                     int         width,
	                                                     int         height);

const gchar* clock_utils_get_temp_display_name (GWeatherTemperatureUnit temp);
const gchar* clock_utils_get_speed_display_name (GWeatherSpeedUnit speed);
const gchar* clock_utils_get_distance_display_name (GWeatherDistanceUnit distance);
const gchar* clock_utils_get_pressure_display_name (GWeatherPressureUnit pressure);

G_END_DECLS

#endif /* __CLOCK_UTILS_H__ */
