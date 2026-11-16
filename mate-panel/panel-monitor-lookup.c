/*
 * panel-monitor-lookup.c: which monitor a panel goes on.
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include "panel-monitor-lookup.h"

void
panel_monitor_entries_free (PanelMonitorEntry *entries,
                            int                n_entries)
{
	int i;

	for (i = 0; i < n_entries; i++)
		g_free (entries[i].id);
	g_free (entries);
}

int
panel_monitor_lookup (const PanelMonitorEntry *entries,
                      int                      n_entries,
                      int                      n_monitors,
                      int                      primary,
                      const char              *configured_id,
                      int                      configured_index)
{
	int i;

	if (n_monitors <= 0)
		return -1;

	if (configured_id != NULL && configured_id[0] != '\0') {
		for (i = 0; i < n_entries; i++) {
			if (entries[i].monitor >= 0 && entries[i].monitor < n_monitors &&
			    g_strcmp0 (entries[i].id, configured_id) == 0)
				return entries[i].monitor;
		}
	} else if (configured_index >= 0 && configured_index < n_monitors) {
		return configured_index;
	}

	return primary >= 0 && primary < n_monitors ? primary : 0;
}

const char *
panel_monitor_id_for_index (const PanelMonitorEntry *entries,
                            int                      n_entries,
                            int                      monitor)
{
	int i;

	for (i = 0; i < n_entries; i++) {
		if (entries[i].monitor == monitor && entries[i].id != NULL && entries[i].id[0] != '\0')
			return entries[i].id;
	}

	return NULL;
}
