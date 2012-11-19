/*
 * panel-cleanup.c: utility to clean up things on exit
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

#include <glib.h>
#include <glib-object.h>

#include "panel-cleanup.h"

typedef struct {
	PanelCleanFunc func;
	gpointer       data;
} PanelClean;

static GSList *cleaner;

void
panel_cleanup_do (void)
{
	GSList *l;

	if (!cleaner)
		return;

	for (l = cleaner; l; l = l->next) {
		PanelClean *clean;

		clean = l->data;
		clean->func (clean->data);
		g_slice_free (PanelClean, clean);
	}

	g_slist_free (cleaner);
	cleaner = NULL;
}

void
panel_cleanup_register (PanelCleanFunc func,
			gpointer       data)
{
	PanelClean *clean;

	g_return_if_fail (func != NULL);

	clean = g_slice_new (PanelClean);
	clean->func = func;
	clean->data = data;

	cleaner = g_slist_prepend (cleaner, clean);
}

void
panel_cleanup_unregister (PanelCleanFunc func,
			  gpointer       data)
{
	GSList *l, *next;
	PanelClean *clean;

	g_return_if_fail (func != NULL);

	if (!cleaner)
		return;

	l = cleaner;

	do {
		next = l->next;

		clean = l->data;
		if (clean->func == func && clean->data == data) {
			g_slice_free (PanelClean, clean);
			cleaner = g_slist_delete_link (cleaner, l);
		}

		l = next;
	} while (l);
}

void
panel_cleanup_unref_and_nullify (gpointer data)
{
	GObject **obj;

	g_return_if_fail (data != NULL);

	obj = data;

	g_object_unref (*obj);
	*obj = NULL;
}
