/*
 * panel-launch.c: some helpers to launch desktop files
 *
 * Copyright (C) 2008 Novell, Inc.
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
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "panel-error.h"
#include "panel-glib.h"

#include "panel-launch.h"

static void
_panel_launch_error_dialog (const gchar *name,
			    GdkScreen   *screen,
			    const gchar *message)
{
	char *primary;

	if (name)
		primary = g_markup_printf_escaped (_("Could not launch '%s'"),
						   name);
	else
		primary = g_strdup (_("Could not launch application"));

	panel_error_dialog (NULL, screen, "cannot_launch", TRUE,
			    primary, message);
	g_free (primary);
}

static gboolean
_panel_launch_handle_error (const gchar  *name,
			    GdkScreen    *screen,
			    GError       *local_error,
			    GError      **error)
{
	if (g_error_matches (local_error,
			     G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
	    g_error_free (local_error);
	    return TRUE;
	}

	else if (error != NULL)
		g_propagate_error (error, local_error);

	else {
		_panel_launch_error_dialog (name, screen, local_error->message);
		g_error_free (local_error);
	}

	return FALSE;
}

static void
dummy_child_watch (GPid     pid,
		   gint     status,
		   gpointer user_data)
{
	/* Nothing, this is just to ensure we don't double fork
	* and break pkexec:
	* https://bugzilla.gnome.org/show_bug.cgi?id=675789
	*/
}

static void
gather_pid_callback (GDesktopAppInfo   *gapp,
		     GPid               pid,
		     gpointer           data)
{
	g_child_watch_add (pid, dummy_child_watch, NULL);
}

gboolean
panel_app_info_launch_uris (GDesktopAppInfo   *appinfo,
			    GList      *uris,
			    GdkScreen  *screen,
			    const gchar *action,
			    guint32     timestamp,
			    GError    **error)
{
	GdkAppLaunchContext *context;
	GError              *local_error;
	gboolean             retval;

	g_return_val_if_fail (G_IS_DESKTOP_APP_INFO (appinfo), FALSE);
	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	GdkDisplay *display = gdk_display_get_default ();
	context = gdk_display_get_app_launch_context (display);
	gdk_app_launch_context_set_screen (context, screen);
	gdk_app_launch_context_set_timestamp (context, timestamp);

	local_error = NULL;
	if (action == NULL) {
		retval = g_desktop_app_info_launch_uris_as_manager (appinfo, uris,
						   G_APP_LAUNCH_CONTEXT (context),
						   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
						   NULL, NULL, gather_pid_callback, appinfo,
						   &local_error);
	} else {
		g_desktop_app_info_launch_action (appinfo, action, G_APP_LAUNCH_CONTEXT (context));
		retval = TRUE;
	}

	g_object_unref (context);

	if ((local_error == NULL) && (retval == TRUE))
		return TRUE;

	return _panel_launch_handle_error (g_app_info_get_name (G_APP_INFO(appinfo)),
					   screen, local_error, error);
}

gboolean
panel_app_info_launch_uri (GDesktopAppInfo     *appinfo,
			   const gchar  *uri,
			   GdkScreen    *screen,
			   guint32       timestamp,
			   GError      **error)
{
	GList    *uris;
	gboolean  retval;

	g_return_val_if_fail (G_IS_APP_INFO (appinfo), FALSE);
	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	uris = NULL;
	if (uri)
		uris = g_list_prepend (uris, (gpointer) uri);

	retval = panel_app_info_launch_uris (appinfo, uris,
					     screen, NULL, timestamp, error);

	g_list_free (uris);

	return retval;
}

gboolean
panel_launch_key_file (GKeyFile   *keyfile,
		       GList      *uri_list,
		       GdkScreen  *screen,
		       const gchar *action,
		       GError    **error)
{
	GDesktopAppInfo *appinfo;
	gboolean         retval;

	g_return_val_if_fail (keyfile != NULL, FALSE);
	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	appinfo = g_desktop_app_info_new_from_keyfile (keyfile);
	if (appinfo == NULL)
		return FALSE;

	retval = panel_app_info_launch_uris (appinfo,
					     uri_list, screen, action,
					     gtk_get_current_event_time (),
					     error);

	g_object_unref (appinfo);
	return retval;
}

gboolean
panel_launch_desktop_file (const char  *desktop_file,
			   GdkScreen   *screen,
			   GError     **error)
{
	GDesktopAppInfo *appinfo;
	gboolean         retval;

	g_return_val_if_fail (desktop_file != NULL, FALSE);
	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	appinfo = NULL;

	if (g_path_is_absolute (desktop_file))
		appinfo = g_desktop_app_info_new_from_filename (desktop_file);
	else {
		char *full;

		full = panel_g_lookup_in_applications_dirs (desktop_file);
		if (full) {
			appinfo = g_desktop_app_info_new_from_filename (full);
			g_free (full);
		}
	}

	if (appinfo == NULL)
		return FALSE;

	retval = panel_app_info_launch_uris (appinfo, NULL, screen, NULL,
					     gtk_get_current_event_time (),
					     error);

	g_object_unref (appinfo);

	return retval;
}

/*
 * Set the DISPLAY variable, to be use by g_spawn_async.
 */
static void
set_environment (gpointer display)
{
	g_setenv ("DISPLAY", display, TRUE);
}

gboolean
panel_launch_desktop_file_with_fallback (const char  *desktop_file,
					 const char  *fallback_exec,
					 GdkScreen   *screen,
					 GError     **error)
{
	char       *argv[2] = { (char *) fallback_exec, NULL };
	GError     *local_error;
	gboolean    retval;
	GPid        pid;
	GdkDisplay *display;
	char       *display_name;

	g_return_val_if_fail (desktop_file != NULL, FALSE);
	g_return_val_if_fail (fallback_exec != NULL, FALSE);
	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* need to pass a non-NULL error to avoid getting a dialog */
	local_error = NULL;
	if (panel_launch_desktop_file (desktop_file, screen, &local_error))
		return TRUE;

	if (local_error) {
		g_error_free (local_error);
		local_error = NULL;
	}

	display = gdk_screen_get_display (screen);
	display_name = g_strdup (gdk_display_get_name (display));
	retval = g_spawn_async (NULL, /* working directory */
				argv,
				NULL, /* envp */
				G_SPAWN_SEARCH_PATH,
				set_environment,
				&display_name,
				&pid,
				&local_error);
				g_free (display_name);

        if (local_error == NULL && retval == TRUE) {
		g_child_watch_add (pid, dummy_child_watch, NULL);
		}
		return TRUE;

	return _panel_launch_handle_error (fallback_exec,
					   screen, local_error, error);
}
