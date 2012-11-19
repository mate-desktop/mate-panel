/*
 * calendar-window.h: toplevel window containing a calendar and
 * tasks/appointments
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

#ifndef CALENDAR_WINDOW_H
#define CALENDAR_WINDOW_H

#include <gtk/gtk.h>
#include "clock-utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CALENDAR_TYPE_WINDOW         (calendar_window_get_type ())
#define CALENDAR_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CALENDAR_TYPE_WINDOW, CalendarWindow))
#define CALENDAR_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CALENDAR_TYPE_WINDOW, CalendarWindowClass))
#define CALENDAR_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CALENDAR_TYPE_WINDOW))
#define CALENDAR_IS_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CALENDAR_TYPE_WINDOW))
#define CALENDAR_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CALENDAR_TYPE_WINDOW, CalendarWindowClass))

typedef struct _CalendarWindow        CalendarWindow;
typedef struct _CalendarWindowClass   CalendarWindowClass;
typedef struct _CalendarWindowPrivate CalendarWindowPrivate;

struct _CalendarWindow {
	GtkWindow               parent_instance;

	CalendarWindowPrivate  *priv;
};

struct _CalendarWindowClass {
	GtkWindowClass parent_class;

	void (* edit_locations) (CalendarWindow *calwin);
};

GType      calendar_window_get_type (void) G_GNUC_CONST;
GtkWidget *calendar_window_new      (time_t     *static_current_time,
				     const char *prefs_dir,
				     gboolean    invert_order);

void       calendar_window_refresh  (CalendarWindow *calwin);

GtkWidget *calendar_window_get_locations_box (CalendarWindow *calwin);

gboolean   calendar_window_get_invert_order (CalendarWindow *calwin);
void       calendar_window_set_invert_order (CalendarWindow *calwin,
					     gboolean        invert_order);
gboolean   calendar_window_get_show_weeks   (CalendarWindow *calwin);
void       calendar_window_set_show_weeks   (CalendarWindow *calwin,
					     gboolean        show_weeks);
ClockFormat calendar_window_get_time_format (CalendarWindow *calwin);
void       calendar_window_set_time_format  (CalendarWindow *calwin,
					     ClockFormat     time_format);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_WINDOW_H */
