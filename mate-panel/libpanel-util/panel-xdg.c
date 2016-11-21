/*
 * panel-xdg.c: miscellaneous XDG-related functions.
 *
 * Copyright (C) 2010 Novell, Inc.
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

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "panel-xdg.h"

#define DEFAULT_THEME_NAME "hicolor"

/*
 * Originally based on code from panel-util.c. This part of the code was:
 * Copyright (C) 2006 Vincent Untz <vuntz@gnome.org>
 */

char *
panel_xdg_icon_remove_extension (const char *icon)
{
	char *icon_no_extension;
	char *p;

	icon_no_extension = g_strdup (icon);
	p = strrchr (icon_no_extension, '.');
	if (p &&
	    (strcmp (p, ".png") == 0 ||
	     strcmp (p, ".xpm") == 0 ||
	     strcmp (p, ".svg") == 0)) {
	    *p = 0;
	}

	return icon_no_extension;
}

/*
 * End of code coming from panel-util.c
 */

char *
panel_xdg_icon_name_from_icon_path (const char *path,
				    GdkScreen  *screen)
{
	GtkIconTheme  *theme;
	GtkSettings   *settings;
	char          *theme_name;
	char          *icon;
	char         **paths;
	int            n_paths;
	int            i;
	GFile         *file;

	/* we look if the icon comes from the current icon theme */
	if (!screen)
		screen = gdk_screen_get_default ();

	settings = gtk_settings_get_for_screen (screen);
	g_object_get (settings,
		      "gtk-icon-theme-name", &theme_name,
		      NULL);

	theme = gtk_icon_theme_get_for_screen (screen);
	gtk_icon_theme_get_search_path (theme, &paths, &n_paths);

	file = g_file_new_for_path (path);
	icon = NULL;

	for (i = 0; i < n_paths; i++) {
		GFile *parent;
		char  *basename;

		parent = g_file_new_for_path (paths[i]);

		if (!g_file_has_prefix (file, parent)) {
			g_object_unref (parent);
			continue;
		}

		basename = g_file_get_basename (parent);

		if (g_strcmp0 (basename, "pixmaps") == 0) {
			char *relative_path;

			relative_path = g_file_get_relative_path (parent, file);

			/* if the icon is in a subdir of pixmaps, then it's not
			 * a real icon name */
			if (!strchr (relative_path, G_DIR_SEPARATOR))
				icon = panel_xdg_icon_remove_extension (relative_path);

			g_free (relative_path);
		} else {
			/* real icon theme; but is it the current one? */
			GFile    *theme_dir;
			gboolean  current;

			theme_dir = g_file_get_child (parent, theme_name);

			current = FALSE;
			if (g_file_has_prefix (file, theme_dir)) {
				/* it's the current one */
				current = TRUE;
			} else {
				/* it's the default one */
				g_object_unref (theme_dir);
				theme_dir = g_file_get_child (parent, DEFAULT_THEME_NAME);
				current = g_file_has_prefix (file, theme_dir);
			}

			g_object_unref (theme_dir);

			if (current) {
				char *buffer;

				buffer = g_file_get_basename (file);
				icon = panel_xdg_icon_remove_extension (buffer);
				g_free (buffer);
			}
		}

		g_free (basename);
		g_object_unref (parent);

		break;
	}

	g_object_unref (file);
	g_free (theme_name);

	return icon;
}
