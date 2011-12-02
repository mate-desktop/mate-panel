/*
 * panel-compatibility.h: panel backwards compatibility support
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_COMPATIBILITY_H__
#define __PANEL_COMPATIBILITY_H__

#include <glib.h>
#include <mateconf/mateconf-client.h>

#ifdef __cplusplus
extern "C" {
#endif

void panel_compatibility_migrate_panel_id_list (MateConfClient *client);
void panel_compatibility_maybe_copy_old_config (MateConfClient *client);
void panel_compatibility_migrate_applications_scheme (MateConfClient *client,
						      const char  *key);
void panel_compatibility_migrate_screenshot_action (MateConfClient *client,
						   const char  *id);

gchar *panel_compatibility_get_applet_iid (const gchar *id);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_MENU_BAR_H__ */
