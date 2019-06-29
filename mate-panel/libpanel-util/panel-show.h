/*
 * panel-show.h: a helper around gtk_show_uri_on_window for gtk 3.22
 * or gtk_show_uri for gtk 3.20 and earlier
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

#ifndef PANEL_SHOW_H
#define PANEL_SHOW_H

#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean panel_show_uri (GdkScreen    *screen,
			 const gchar  *uri,
			 guint32       timestamp,
			 GError      **error);

gboolean panel_show_uri_force_mime_type (GdkScreen    *screen,
					 const gchar  *uri,
					 const gchar  *mime_type,
					 guint32       timestamp,
					 GError      **error);

gboolean panel_show_help (GdkScreen    *screen,
			  const gchar  *doc,
			  const gchar  *link,
			  GError      **error);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_SHOW_H */
