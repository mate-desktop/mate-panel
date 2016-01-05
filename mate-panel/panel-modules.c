/*
 * panel-modules.c
 *
 * Copyright (C) 2010 Vincent Untz <vuntz@gnome.org>
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
 */

#include <config.h>
#include <gio/gio.h>

#include <libmate-panel-applet-private/panel-applets-manager-dbus.h>

#include "panel-applets-manager.h"
#include "panel-modules.h"

static void
panel_modules_ensure_extension_points_registered (void)
{
	static gboolean registered_extensions = FALSE;
	GIOExtensionPoint *ep;

	if (!registered_extensions) {
		registered_extensions = TRUE;

		ep = g_io_extension_point_register (MATE_PANEL_APPLETS_MANAGER_EXTENSION_POINT_NAME);
		g_io_extension_point_set_required_type (ep, PANEL_TYPE_APPLETS_MANAGER);
	}
 }

void
panel_modules_ensure_loaded (void)
{
	static gboolean loaded_dirs = FALSE;
	const char *module_path;

	panel_modules_ensure_extension_points_registered ();

	if (!loaded_dirs) {
		GList *modules;
		loaded_dirs = TRUE;

		/* We load the modules explicitly instead of using scan_all
		 * so that we can leak a reference to them.  This prevents them
		 * from getting unloaded later (something they aren't designed
		 * to cope with) */
		modules = g_io_modules_load_all_in_directory (PANEL_MODULES_DIR);
		g_list_free (modules);

		module_path = g_getenv ("MATE_PANEL_EXTRA_MODULES");

		if (module_path) {
			gchar **paths;
			int i;

			paths = g_strsplit (module_path, ":", 0);

			for (i = 0; paths[i] != NULL; i++) {
				modules = g_io_modules_load_all_in_directory (paths[i]);
				g_list_free (modules);
			}

			g_strfreev (paths);
		}

		mate_panel_applets_manager_dbus_get_type ();
	}
}
