/*
 * panel-gsettings.h: panel gsettings utility methods
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
 *               2012 Stefano Karapetsas
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
 *      Mark McLoughlin <mark@skynet.ie>
 *      Glynn Foster <glynn.foster@sun.com>
 *      Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __PANEL_GSETTINGS_H__
#define __PANEL_GSETTINGS_H__

#include <glib.h>
#include <gio/gio.h>

#include "panel-enums.h"

G_BEGIN_DECLS

gboolean        panel_gsettings_is_valid_keyname (const gchar  *key,
                                                  GError      **error);

gboolean        panel_gsettings_append_strv (GSettings         *settings,
                                             const gchar       *key,
                                             const gchar       *value);

gboolean        panel_gsettings_remove_all_from_strv (GSettings         *settings,
                                                      const gchar       *key,
                                                      const gchar       *value);

GSList*         panel_gsettings_strv_to_gslist (gchar **array);

G_END_DECLS

#endif /* __PANEL_GSETTINGS_H__ */
