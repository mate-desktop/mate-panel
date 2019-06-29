/*
 * panel-list.c: GList & GSList extensions
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Originally based on code from panel-util.c (there was no relevant copyright
 * header at the time), but the code was:
 * Copyright (C) Mark McLoughlin <mark@skynet.ie>
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

#include <glib.h>

#include "panel-list.h"

GList *
panel_g_list_insert_before (GList *list,
			    GList *sibling,
			    GList *link)
{
	if (!list) {
		g_return_val_if_fail (sibling == NULL, list);
		return link;
	} else if (sibling) {
		if (sibling->prev) {
			link->prev = sibling->prev;
			link->prev->next = link;
			link->next = sibling;
			sibling->prev = link;
			return list;
		} else {
			link->next = sibling;
			sibling->prev = link;
			g_return_val_if_fail (sibling == list, link);
			return link;
		}
	} else {
		GList *last;

		last = list;
		while (last->next)
			last = last->next;

		last->next = link;
		link->prev = last;
		return list;
	}
}

GList *
panel_g_list_insert_after (GList *list,
			   GList *sibling,
			   GList *link)
{
	if (!list) {
		g_return_val_if_fail (sibling == NULL, link);
		return link;
	} else if (sibling) {
		if (sibling->next) {
			link->next = sibling->next;
			link->next->prev = link;
			link->prev = sibling;
			sibling->next = link;
			return list;
		} else {
			sibling->next = link;
			link->prev = sibling;
			return list;
		}

	} else {
		link->next = list;
		list->prev = link;
		return link;
	}
}

GList *
panel_g_list_swap_next (GList *list,
			GList *dl)
{
	GList *t;

	if (!dl)
		return list;

	if (!dl->next)
		return list;

	if (dl->prev)
		dl->prev->next = dl->next;
	t = dl->prev;
	dl->prev = dl->next;
	dl->next->prev = t;
	if (dl->next->next)
		dl->next->next->prev = dl;
	t = dl->next->next;
	dl->next->next = dl;
	dl->next = t;

	if (list == dl)
		return dl->prev;

	return list;
}

GList *
panel_g_list_swap_prev (GList *list,
			GList *dl)
{
	GList *t;

	if (!dl)
		return list;

	if (!dl->prev)
		return list;

	if (dl->next)
		dl->next->prev = dl->prev;
	t = dl->next;
	dl->next = dl->prev;
	dl->prev->next = t;
	if (dl->prev->prev)
		dl->prev->prev->next = dl;
	t = dl->prev->prev;
	dl->prev->prev = dl;
	dl->prev = t;

	if (list == dl->next)
		return dl;

	return list;
}

/*maybe this should be a glib function?
 it resorts a single item in the list*/
GList *
panel_g_list_resort_item (GList        *list,
			  gpointer      data,
			  GCompareFunc  func)
{
	GList *dl;

	g_return_val_if_fail (func != NULL, list);

	if (!list)
		return NULL;

	dl = g_list_find (list, data);

	if (dl == NULL)
		return list;

	while (dl->next &&
	       (*func)(dl->data, dl->next->data) > 0)
		list = panel_g_list_swap_next (list, dl);
	while (dl->prev &&
	       (*func) (dl->data, dl->prev->data) < 0)
		list = panel_g_list_swap_prev (list, dl);

	return list;
}

GSList *
panel_g_slist_make_unique (GSList       *list,
			   GCompareFunc  compare,
			   gboolean      free_data)
{
	GSList *sorted, *l;

	g_return_val_if_fail (compare != NULL, list);

	if (!list)
		return NULL;

	sorted = g_slist_copy (list);
	sorted = g_slist_sort (sorted, compare);

	for (l = sorted; l; l = l->next) {
		GSList *next;

		next = l->next;
		if (l->data && next && next->data)
			if (!compare (l->data, next->data)) {
				list = g_slist_remove (list, l->data);
				if (free_data)
					g_free (l->data);
			}
	}

	g_slist_free (sorted);

	return list;
}
