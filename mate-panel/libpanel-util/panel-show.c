/*
 * panel-show.c: a helper around gtk_show_uri
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

#include <gtk/gtk.h>

#include "panel-error.h"
#include "panel-glib.h"
#include "panel-launch.h"

#include "panel-show.h"

static void
_panel_show_error_dialog (const gchar *uri,
			  GdkScreen   *screen,
			  const gchar *message)
{
	char *primary;

	primary = g_markup_printf_escaped (_("Could not open location '%s'"),
					   uri);
	panel_error_dialog (NULL, screen, "cannot_show_url", TRUE,
			    primary, message);
	g_free (primary);
}

typedef struct {
	GMountOperation *mount_op;
	GdkScreen *screen;
} PanelShowMountOperationHandle;

static void
_panel_show_mount_async_callback (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	GError *error = NULL;
	GFile *file;
	PanelShowMountOperationHandle *handle = user_data;

	file = G_FILE (source_object);

	error = NULL;
	if (g_file_mount_enclosing_volume_finish (file, result, &error)) {
		char *uri = g_file_get_uri (file);

		panel_show_uri (handle->screen, uri,
				gtk_get_current_event_time (), NULL);
		g_free (uri);
	} else {
		if (!g_error_matches (error, G_IO_ERROR,
				      G_IO_ERROR_PERMISSION_DENIED) &&
		    !g_error_matches (error, G_IO_ERROR,
			    	      G_IO_ERROR_FAILED_HANDLED)) {
			char *uri;

			uri = g_file_get_uri (file);
			_panel_show_error_dialog (uri, handle->screen,
						  error->message);
			g_free (uri);
		}
		g_error_free (error);
	}

	if (handle->mount_op)
		g_object_unref (handle->mount_op);

	g_slice_free (PanelShowMountOperationHandle, handle);
}

static gboolean _panel_show_handle_error(const gchar* uri, GdkScreen* screen, GError* local_error, GError** error)
{
	if (local_error == NULL)
	{
		return TRUE;
	}

	else if (g_error_matches (local_error,
				  G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (local_error);
		return TRUE;
	}

	else if (g_error_matches (local_error,
				  G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
		GFile *file;
		PanelShowMountOperationHandle *handle;

		handle = g_slice_new (PanelShowMountOperationHandle);
		file = g_file_new_for_uri (uri);

		/* If it's not mounted, try to mount it ourselves */
		handle->mount_op = gtk_mount_operation_new (NULL);
		gtk_mount_operation_set_screen (GTK_MOUNT_OPERATION (handle->mount_op),
						screen);
		handle->screen = screen;

		g_file_mount_enclosing_volume (file, G_MOUNT_MOUNT_NONE,
					       handle->mount_op, NULL,
					       _panel_show_mount_async_callback,
					       handle);
		g_object_unref (file);

		return TRUE;
	}

	else if (error != NULL)
		g_propagate_error (error, local_error);

	else {
		_panel_show_error_dialog (uri, screen, local_error->message);
		g_error_free (local_error);
	}

	return FALSE;
}

static gboolean panel_show_caja_search_uri(GdkScreen* screen, const gchar* uri, guint32 timestamp, GError** error)
{
	char* desktopfile = NULL;
	GDesktopAppInfo* appinfo = NULL;
	gboolean ret;

	desktopfile = panel_g_lookup_in_applications_dirs("caja-folder-handler.desktop");

	if (desktopfile)
	{
		appinfo = g_desktop_app_info_new_from_filename(desktopfile);
		g_free(desktopfile);
	}

	if (!appinfo)
	{
		_panel_show_error_dialog (uri, screen, _("No application to handle search folders is installed."));
		return FALSE;
	}

	ret = panel_app_info_launch_uri(appinfo, uri, screen, timestamp, error);
	g_object_unref(appinfo);

	return ret;
}

gboolean panel_show_uri(GdkScreen* screen, const gchar* uri, guint32 timestamp, GError** error)
{
	GError* local_error = NULL;

	g_return_val_if_fail(GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail(uri != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (g_str_has_prefix(uri, "x-caja-search:"))
	{
		return panel_show_caja_search_uri(screen, uri, timestamp, error);
	}

	gtk_show_uri_on_window (NULL, uri,timestamp, &local_error);

	return _panel_show_handle_error(uri, screen, local_error, error);
}

gboolean
panel_show_uri_force_mime_type (GdkScreen    *screen,
				const gchar  *uri,
				const gchar  *mime_type,
				guint32       timestamp,
				GError      **error)
{
	GFile    *file;
	GAppInfo *appinfo;
	gboolean  ret;
	GdkDisplay *display;
	GdkAppLaunchContext *context;
	GList    *uris;

	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (mime_type != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file = g_file_new_for_uri (uri);
	appinfo = g_app_info_get_default_for_type (mime_type,
					       !g_file_is_native (file));
	g_object_unref (file);

	if (appinfo == NULL) {
		/* no application for the mime type, so let's fallback on
		 * automatic detection */
		return panel_show_uri (screen, uri, timestamp, error);
	}

	uris = g_list_append (NULL, (gpointer)uri);
	display = gdk_screen_get_display (screen);
	context = gdk_display_get_app_launch_context (display);
	ret = g_app_info_launch_uris (appinfo, uris, G_APP_LAUNCH_CONTEXT(context), error);
	g_object_unref (context);
	g_list_free (uris);
	g_object_unref (appinfo);

	return ret;
}

static void
_panel_show_help_error_dialog (const gchar *doc,
			       GdkScreen   *screen,
			       const gchar *message)
{
	char *primary;

	primary = g_markup_printf_escaped (_("Could not display help document '%s'"),
					   doc);
	panel_error_dialog (NULL, screen, "cannot_show_help", TRUE,
			    primary, message);
	g_free (primary);
}

static gboolean
_panel_show_help_handle_error (const gchar  *doc,
			       GdkScreen    *screen,
			       GError       *local_error,
			       GError      **error)
{
	if (local_error == NULL)
		return TRUE;

	else if (g_error_matches (local_error,
				  G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (local_error);
		return TRUE;
	}

	else if (error != NULL)
		g_propagate_error (error, local_error);

	else {
		_panel_show_help_error_dialog (doc, screen,
					       local_error->message);
		g_error_free (local_error);
	}

	return FALSE;
}

gboolean
panel_show_help (GdkScreen    *screen,
		 const gchar  *doc,
		 const gchar  *link,
		 GError      **error)
{
	GError *local_error = NULL;
	char   *uri;

	g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);
	g_return_val_if_fail (doc != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (link)
		uri = g_strdup_printf ("help:%s/%s", doc, link);
	else
		uri = g_strdup_printf ("help:%s", doc);

	gtk_show_uri_on_window (NULL, uri, gtk_get_current_event_time (), &local_error);
	g_free (uri);

	return _panel_show_help_handle_error (doc, screen, local_error, error);
}
