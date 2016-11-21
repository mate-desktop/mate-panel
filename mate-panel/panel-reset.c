/*
 * panel-reset.c
 *
 * Copyright (C) 2010 Perberos <perberos@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *    Perberos <perberos@gmail.com>
 *    Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __PANEL_RESET_C__
#define __PANEL_RESET_C__

#include <stdlib.h>
#include <gio/gio.h>
#include "panel-reset.h"
#include "panel-schemas.h"

void
panel_reset()
{
	GSettings *settings;

	settings = g_settings_new (PANEL_SCHEMA);
	g_settings_set_strv (settings, PANEL_OBJECT_ID_LIST_KEY, NULL);
	g_settings_sync ();
	g_settings_set_strv (settings, PANEL_TOPLEVEL_ID_LIST_KEY, NULL);
	g_settings_sync ();

	g_object_unref (settings);
}

#endif /* !__PANEL_RESET_C__ */
