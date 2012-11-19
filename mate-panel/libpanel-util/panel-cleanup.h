/*
 * panel-cleanup.h: utility to clean up things on exit
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

#ifndef PANEL_CLEANUP_H
#define PANEL_CLEANUP_H

#include "glib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_CLEAN_FUNC(f)     ((PanelCleanFunc) (f))

typedef void  (*PanelCleanFunc) (gpointer data);

void panel_cleanup_unref_and_nullify (gpointer data);

void panel_cleanup_do         (void);

void panel_cleanup_register   (PanelCleanFunc func,
			       gpointer       data);
void panel_cleanup_unregister (PanelCleanFunc func,
			       gpointer       data);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_CLEANUP_H */
