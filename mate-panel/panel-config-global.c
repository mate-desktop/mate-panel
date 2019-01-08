/*
 * panel-config-global.c: panel global configuration module
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
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
 *      Mark McLoughlin <mark@skynet.ie>
 *      Glynn Foster <glynn.foster@sun.com>
 *      Stefano Karapetsas <stefano@karapetsas.com>
 */

#include <config.h>

#include "panel-config-global.h"

#include <string.h>
#include <gio/gio.h>

#include "panel-globals.h"

typedef struct {
	guint               tooltips_enabled : 1;
	guint               enable_animations : 1;
	guint               drawer_auto_close : 1;
	guint               confirm_panel_remove : 1;
	guint               highlight_when_over : 1;
} GlobalConfig;

static GlobalConfig global_config = { 0, };
static gboolean global_config_initialised = FALSE;
static GSettings *panel_global_settings = NULL;

gboolean
panel_global_config_get_highlight_when_over (void)
{
	g_assert (global_config_initialised == TRUE);

	return global_config.highlight_when_over;
}

gboolean
panel_global_config_get_enable_animations (void)
{
	g_assert (global_config_initialised == TRUE);

	return global_config.enable_animations;
}

gboolean
panel_global_config_get_drawer_auto_close (void)
{
	g_assert (global_config_initialised == TRUE);

	return global_config.drawer_auto_close;
}

gboolean
panel_global_config_get_tooltips_enabled (void)
{
	g_assert (global_config_initialised == TRUE);

	return global_config.tooltips_enabled;
}

gboolean
panel_global_config_get_confirm_panel_remove (void)
{
	g_assert (global_config_initialised == TRUE);

	return global_config.confirm_panel_remove;
}

static void
panel_global_config_set_entry (GSettings *settings, gchar *key)
{
	g_return_if_fail (settings != NULL);
	g_return_if_fail (key != NULL);

	if (strcmp (key, "tooltips-enabled") == 0)
		global_config.tooltips_enabled =
				g_settings_get_boolean (settings, key);

	else if (strcmp (key, "enable-animations") == 0)
		global_config.enable_animations =
				g_settings_get_boolean (settings, key);

	else if (strcmp (key, "drawer-autoclose") == 0)
		global_config.drawer_auto_close =
			g_settings_get_boolean (settings, key);

	else if (strcmp (key, "confirm-panel-remove") == 0)
		global_config.confirm_panel_remove =
			g_settings_get_boolean (settings, key);

	else if (strcmp (key, "highlight-launchers-on-mouseover") == 0)
		global_config.highlight_when_over =
			g_settings_get_boolean (settings, key);
}

static void
panel_global_config_notify (GSettings   *settings,
							gchar       *key,
							gpointer     user_data)
{
        panel_global_config_set_entry (settings, key);
}

void
panel_global_config_load (void)
{
	GSettingsSchema *schema;
	gchar **keys;
	gint i;

	panel_global_settings = g_settings_new ("org.mate.panel");

	g_object_get (panel_global_settings, "settings-schema", &schema, NULL);
	keys = g_settings_schema_list_keys (schema);
	g_settings_schema_unref (schema);

	for (i = 0; keys[i]; i++) {
		panel_global_config_set_entry (panel_global_settings, keys[i]);
	}
	g_strfreev (keys);

	g_signal_connect (panel_global_settings, "changed", G_CALLBACK (panel_global_config_notify), NULL);

	global_config_initialised = TRUE;
}
