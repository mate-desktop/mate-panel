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
#include <gio/gio.h>

#include "set-timezone.h"

#define DATETIME_DBUS_NAME "org.mate.SettingsDaemon.DateTimeMechanism"
#define DATETIME_DBUS_PATH "/"

static GDBusProxy *
get_bus_proxy (void)
{
	GError            *error = NULL;
	static GDBusProxy *proxy = NULL;
	if (proxy == NULL) {
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       DATETIME_DBUS_NAME,
						       DATETIME_DBUS_PATH,
						       DATETIME_DBUS_NAME,
						       NULL,
						       &error);
		if (proxy == NULL) {
			g_warning ("Unable to contact datetime settings daemon: %s\n", error->message);
			g_error_free (error);
		}
	}
	return proxy;
}

#define CACHE_VALIDITY_SEC 2

typedef  void (*CanDoFunc) (gint value);

static void
notify_can_do (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
	GDBusProxy *proxy;
	GVariant   *variant;
	GError     *error = NULL;
	gint32      value;

	CanDoFunc callback = user_data;

	proxy = get_bus_proxy ();
	variant = g_dbus_proxy_call_finish (proxy, res, &error);
	if (variant == NULL) {
		g_warning ("Call can set time zone dbus method: %s", error->message);
		g_error_free (error);
	} else {
		g_variant_get (variant, "(i)", &value);
		g_variant_unref (variant);
		callback (value);
	}
}

static void
refresh_can_do (const gchar *action, CanDoFunc callback)
{
	GDBusProxy *proxy;

	proxy = get_bus_proxy ();
	if (proxy == NULL)
		return;

	g_dbus_proxy_call (proxy,
			   action,
			   g_variant_new ("()"),
			   G_DBUS_CALL_FLAGS_NONE,
			   G_MAXINT,
			   NULL,
			   notify_can_do,
			   callback);
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
set_time_notify (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
	SetTimeCallbackData *data  = user_data;
	GError              *error = NULL;
	GDBusProxy          *proxy;
	GVariant            *variant;

	proxy = get_bus_proxy ();
	variant = g_dbus_proxy_call_finish (proxy, res, &error);
	if (variant == NULL) {
		if (error != NULL) {
			if (data->callback)
				data->callback (data->data, error);
			g_error_free (error);
		} else {
			if (data->callback)
				data->callback (data->data, NULL);
		}
	} else {
		g_variant_unref (variant);
		if (data->callback)
			data->callback (data->data, NULL);
	}
	free_data (data);
}

static void
set_time_async (SetTimeCallbackData *data)
{
	GDBusProxy *proxy;

	proxy = get_bus_proxy ();
	if (proxy == NULL)
		return;

	data->ref_count++;
	if (strcmp (data->call, "SetTime") == 0)
		g_dbus_proxy_call (proxy,
				   "SetTime",
				   g_variant_new ("(x)", data->time),
				   G_DBUS_CALL_FLAGS_NONE,
				   G_MAXINT,
				   NULL,
				   set_time_notify,
				   data);
	else
		g_dbus_proxy_call (proxy,
				   "SetTimezone",
				   g_variant_new ("(s)", data->filename),
				   G_DBUS_CALL_FLAGS_NONE,
				   G_MAXINT,
				   NULL,
				   set_time_notify,
				   data);
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
