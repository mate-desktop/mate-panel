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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Mark McLoughlin <mark@skynet.ie>
 *      Glynn Foster <glynn.foster@sun.com>
 */

#include <config.h>

#include "panel-config-global.h"

#include <string.h>
#include <mateconf/mateconf.h>

#include "panel-globals.h"
#include "panel-mateconf.h"

typedef struct {
	guint               tooltips_enabled : 1;
	guint               enable_animations : 1;
	guint               drawer_auto_close : 1;
	guint               confirm_panel_remove : 1;
	guint               highlight_when_over : 1;
} GlobalConfig;

static GlobalConfig global_config = { 0, };
static gboolean global_config_initialised = FALSE;

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
panel_global_config_set_entry (MateConfEntry *entry)
{
	MateConfValue *value;
	const char *key;

	g_return_if_fail (entry != NULL);

	value = mateconf_entry_get_value (entry);
	key   = panel_mateconf_basename (mateconf_entry_get_key (entry));

	if (!value || !key)
		return;

	if (strcmp (key, "tooltips_enabled") == 0)
		global_config.tooltips_enabled =
				mateconf_value_get_bool (value);

	else if (strcmp (key, "enable_animations") == 0)
		global_config.enable_animations =
				mateconf_value_get_bool (value);

	else if (strcmp (key, "drawer_autoclose") == 0)
		global_config.drawer_auto_close =
			mateconf_value_get_bool (value);

	else if (strcmp (key, "confirm_panel_remove") == 0)
		global_config.confirm_panel_remove =
			mateconf_value_get_bool (value);

	else if (strcmp (key, "highlight_launchers_on_mouseover") == 0)
		global_config.highlight_when_over =
			mateconf_value_get_bool (value);
}

static void
panel_global_config_notify (MateConfClient *client,
			    guint        cnxn_id,
			    MateConfEntry  *entry,
			    gpointer     user_data)
{
        panel_global_config_set_entry (entry);
}

void
panel_global_config_load (void)
{
	MateConfClient *client;
	GSList      *l, *entries;
	const char  *key = "/apps/panel/global";

	client = panel_mateconf_get_client ();

	mateconf_client_add_dir (client, key, MATECONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	entries = mateconf_client_all_entries (client, key, NULL);

	for (l = entries; l; l = l->next) {
		panel_global_config_set_entry (l->data);
		mateconf_entry_unref (l->data);
	}
	g_slist_free (entries);

	mateconf_client_notify_add (client, key, panel_global_config_notify, NULL, NULL, NULL);

	global_config_initialised = TRUE;
}
