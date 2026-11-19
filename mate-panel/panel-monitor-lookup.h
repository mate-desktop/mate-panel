/*
 * panel-monitor-lookup.h: which monitor a panel goes on.
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

#ifndef __PANEL_MONITOR_LOOKUP_H__
#define __PANEL_MONITOR_LOOKUP_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	char *id;
	int   monitor;
} PanelMonitorEntry;

void        panel_monitor_entries_free  (PanelMonitorEntry *entries,
                                         int                n_entries);

int         panel_monitor_lookup        (const PanelMonitorEntry *entries,
                                         int                      n_entries,
                                         int                      n_monitors,
                                         int                      primary,
                                         const char              *configured_id,
                                         int                      configured_index);

const char *panel_monitor_id_for_index  (const PanelMonitorEntry *entries,
                                         int                      n_entries,
                                         int                      monitor);

G_END_DECLS

#endif /* __PANEL_MONITOR_LOOKUP_H__ */
