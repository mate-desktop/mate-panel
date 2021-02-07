/* Test for system timezone handling
 *
 * Copyright (C) 2008-2010 Novell, Inc.
 *
 * Authors: Vincent Untz <vuntz@gnome.org>
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
 */

#include <glib.h>
#include "system-timezone.h"

static void
timezone_print (void)
{
	SystemTimezone *systz;

	systz = system_timezone_new ();
        g_print ("Current timezone: %s\n", system_timezone_get (systz));
	g_object_unref (systz);
}

static int
timezone_set (const char *new_tz)
{
        GError *error;

        error = NULL;
        if (!system_timezone_set (new_tz, &error)) {
                g_printerr ("%s\n", error->message);
                g_error_free (error);
                return 1;
        }

	return 0;
}

static void
timezone_changed (SystemTimezone *systz,
		  const char     *new_tz,
		  gpointer        data)
{
	g_print ("Timezone changed to: %s\n", new_tz);
}

static void
timezone_monitor (void)
{
	SystemTimezone *systz;
	GMainLoop      *mainloop;

	systz = system_timezone_new ();
	g_signal_connect (systz, "changed",
			  G_CALLBACK (timezone_changed), NULL);

	mainloop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (mainloop);
	g_main_loop_unref (mainloop);

	g_object_unref (systz);
}

int
main (int    argc,
      char **argv)
{
	int      retval;

	gboolean  get = FALSE;
	gboolean  monitor = FALSE;
	char     *tz_set = NULL;

	GError         *error;
	GOptionContext *context;
        GOptionEntry options[] = {
                { "get", 'g', 0, G_OPTION_ARG_NONE, &get, "Get the current timezone", NULL },
                { "set", 's', 0, G_OPTION_ARG_STRING, &tz_set, "Set the timezone to TIMEZONE", "TIMEZONE" },
                { "monitor", 'm', 0, G_OPTION_ARG_NONE, &monitor, "Monitor timezone changes", NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

	retval = 0;

	context = g_option_context_new ("");
	g_option_context_add_main_entries (context, options, NULL);

	error = NULL;
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		g_option_context_free (context);

		return 1;
	}

	g_option_context_free (context);

	if (get || (!tz_set && !monitor))
		timezone_print ();
	else if (tz_set)
		retval = timezone_set (tz_set);
	else if (monitor)
		timezone_monitor ();
	else
		g_assert_not_reached ();

        return retval;
}
