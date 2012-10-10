/*
 * panel-dconf.h: helper API for dconf
 *
 * Copyright (C) 2011 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#ifndef __PANEL_DCONF_H__
#define __PANEL_DCONF_H__

#include <glib.h>

G_BEGIN_DECLS

gboolean panel_dconf_write_sync (const gchar  *key,
                                 GVariant     *value,
                                 GError      **error);

gboolean panel_dconf_recursive_reset (const gchar  *dir,
                                      GError     **error);

gchar **panel_dconf_list_subdirs (const gchar *dir,
                                  gboolean     remove_trailing_slash);

G_END_DECLS

#endif /* __PANEL_DCONF_H__ */
