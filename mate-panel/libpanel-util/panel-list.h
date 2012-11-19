/*
 * panel-list.h: GList & GSList extensions
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

#ifndef PANEL_LIST_H
#define PANEL_LIST_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

GList *panel_g_list_insert_before (GList        *list,
				   GList        *sibling,
				   GList        *link);
GList *panel_g_list_insert_after  (GList        *list,
				   GList        *sibling,
				   GList        *link);
GList *panel_g_list_swap_next     (GList        *list,
				   GList        *dl);
GList *panel_g_list_swap_prev     (GList        *list,
				   GList        *dl);
GList *panel_g_list_resort_item   (GList        *list,
				   gpointer      data,
				   GCompareFunc  func);

GSList *panel_g_slist_make_unique (GSList       *list,
				   GCompareFunc  compare,
				   gboolean      free_data);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_LIST_H */
