/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "set-timezone.h"


static DBusGConnection *
get_system_bus (void)
{
        GError          *error;
        static DBusGConnection *bus = NULL;

	if (bus == NULL) {
        	error = NULL;
        	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        	if (bus == NULL) {
                	g_warning ("Couldn't connect to system bus: %s", 
				   error->message);
                	g_error_free (error);
		}
        }

        return bus;
}

#define CACHE_VALIDITY_SEC 2

typedef  void (*CanDoFunc) (gint value);

static void
notify_can_do (DBusGProxy     *proxy,
	       DBusGProxyCall *call,
	       void           *user_data)
{
	CanDoFunc callback = user_data;
	GError *error = NULL;
	gint value;

	if (dbus_g_proxy_end_call (proxy, call,
				   &error,
				   G_TYPE_INT, &value,
				   G_TYPE_INVALID)) {
		callback (value);
	}
}

static void
refresh_can_do (const gchar *action, CanDoFunc callback)
{
        DBusGConnection *bus;
        DBusGProxy      *proxy;

        bus = get_system_bus ();
        if (bus == NULL)
                return;

	proxy = dbus_g_proxy_new_for_name (bus,
					   "org.mate.SettingsDaemon.DateTimeMechanism",
					   "/",
					   "org.mate.SettingsDaemon.DateTimeMechanism");

	dbus_g_proxy_begin_call_with_timeout (proxy,
					      action,
					      notify_can_do,
					      callback, NULL,
					      INT_MAX,
					      G_TYPE_INVALID);
}

static gint   settimezone_cache = 0;
static time_t settimezone_stamp = 0;

static void
update_can_settimezone (gint res)
{
	settimezone_cache = res;
	time (&settimezone_stamp);
}

gint
can_set_system_timezone (void)
{
	time_t          now;

	time (&now);
	if (ABS (now - settimezone_stamp) > CACHE_VALIDITY_SEC) {
		refresh_can_do ("CanSetTimezone", update_can_settimezone);
		settimezone_stamp = now;
	}

	return settimezone_cache;
}

static gint   settime_cache = 0;
static time_t settime_stamp = 0;

static void
update_can_settime (gint res)
{
	settime_cache = res;
	time (&settime_stamp);
}

gint
can_set_system_time (void)
{
	time_t now;

	time (&now);
	if (ABS (now - settime_stamp) > CACHE_VALIDITY_SEC) {
		refresh_can_do ("CanSetTime", update_can_settime);
		settime_stamp = now;
	}

	return settime_cache;
}

typedef struct {
	gint ref_count;
        gchar *call;
	gint64 time;
	gchar *filename;
	GFunc callback;
	gpointer data;
	GDestroyNotify notify;
} SetTimeCallbackData;

static void
free_data (gpointer d)
{
	SetTimeCallbackData *data = d;

	data->ref_count--;
	if (data->ref_count == 0) {
		if (data->notify)
			data->notify (data->data);
		g_free (data->filename);
		g_free (data);
	}
}

static void
set_time_notify (DBusGProxy     *proxy,
		 DBusGProxyCall *call,
		 void           *user_data)
{
	SetTimeCallbackData *data = user_data;
	GError *error = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID)) {
		if (data->callback) 
			data->callback (data->data, NULL);
	}
	else {
		if (error->domain == DBUS_GERROR &&
		    error->code == DBUS_GERROR_NO_REPLY) {
			/* these errors happen because dbus doesn't
			 * use monotonic clocks
			 */	
			g_warning ("ignoring no-reply error when setting time");
			g_error_free (error);
			if (data->callback)
				data->callback (data->data, NULL);
		}
		else {
			if (data->callback)
				data->callback (data->data, error);
			else
				g_error_free (error);
		}		
	}
}

static void
set_time_async (SetTimeCallbackData *data)
{
        DBusGConnection *bus;
        DBusGProxy      *proxy;

        bus = get_system_bus ();
        if (bus == NULL)
                return;

	proxy = dbus_g_proxy_new_for_name (bus,
					   "org.mate.SettingsDaemon.DateTimeMechanism",
					   "/",
					   "org.mate.SettingsDaemon.DateTimeMechanism");

	data->ref_count++;
	if (strcmp (data->call, "SetTime") == 0)
		dbus_g_proxy_begin_call_with_timeout (proxy, 
						      "SetTime",
						      set_time_notify,
						      data, free_data,
						      INT_MAX,
						      /* parameters: */
						      G_TYPE_INT64, data->time,
						      G_TYPE_INVALID,
						      /* return values: */
						      G_TYPE_INVALID);
	else 
		dbus_g_proxy_begin_call_with_timeout (proxy, 
						      "SetTimezone",
						      set_time_notify,
						      data, free_data,
						      INT_MAX,
						      /* parameters: */
						      G_TYPE_STRING, data->filename,
						      G_TYPE_INVALID,
						      /* return values: */
						      G_TYPE_INVALID);
}

void
set_system_time_async (gint64         time,
		       GFunc          callback,
		       gpointer       d,
		       GDestroyNotify notify)
{
	SetTimeCallbackData *data;

	if (time == -1)
		return;

	data = g_new0 (SetTimeCallbackData, 1);
	data->ref_count = 1;
	data->call = "SetTime";
	data->time = time;
	data->filename = NULL;
	data->callback = callback;
	data->data = d;
	data->notify = notify;

	set_time_async (data);
	free_data (data);
}

void
set_system_timezone_async (const gchar    *filename,
			   GFunc           callback,
			   gpointer        d,
			   GDestroyNotify  notify)
{
	SetTimeCallbackData *data;

	if (filename == NULL)
		return;

	data = g_new0 (SetTimeCallbackData, 1);
	data->ref_count = 1;
	data->call = "SetTimezone";
	data->time = -1;
	data->filename = g_strdup (filename);
	data->callback = callback;
	data->data = d;
	data->notify = notify;

	set_time_async (data);
	free_data (data);
}
