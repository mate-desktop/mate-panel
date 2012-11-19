/*
 * panel-error.h: an easy-to-use error dialog
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Originally based on code from panel-util.h (there was no relevant copyright
 * header at the time).
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

#ifndef PANEL_ERROR_H
#define PANEL_ERROR_H

#include <glib.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget *panel_error_dialog (GtkWindow  *parent,
			       GdkScreen  *screen,
			       const char *dialog_class,
			       gboolean    auto_destroy,
			       const char *primary_text,
			       const char *secondary_text);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_ERROR_H */
