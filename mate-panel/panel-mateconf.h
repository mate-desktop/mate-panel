/*
 * panel-mateconf.h: panel mateconf utility methods
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
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
 *      Mark McLoughlin <mark@skynet.ie>
 *      Glynn Foster <glynn.foster@sun.com>
 */

#ifndef __PANEL_MATECONF_H__
#define __PANEL_MATECONF_H__

#include <mateconf/mateconf-client.h>

#include "panel-enums.h"

#define PANEL_CONFIG_DIR     "/apps/panel"
#define PANEL_SCHEMAS_DIR    "/schemas/apps/panel"
#define PANEL_DEFAULTS_DIR   "/apps/panel/default_setup"
#define PANEL_OLD_CONFIG_DIR "/apps/panel/profiles/default"

#ifdef __cplusplus
extern "C" {
#endif

MateConfClient *panel_mateconf_get_client          (void);

const char  *panel_mateconf_sprintf             (const char        *format, ...) G_GNUC_PRINTF (1, 2);
const char  *panel_mateconf_basename            (const char        *key);
char        *panel_mateconf_dirname             (const char        *key);
const char  *panel_mateconf_global_key          (const char        *key);
const char  *panel_mateconf_general_key         (const char        *key);
const char  *panel_mateconf_full_key            (PanelMateConfKeyType  type,
					      const char        *id,
					      const char        *key);
const char  *panel_mateconf_key_type_to_id_list (PanelMateConfKeyType  type);

guint		panel_mateconf_notify_add             (const char            *key,
						    MateConfClientNotifyFunc  notify_func,
						    gpointer               user_data);
guint		panel_mateconf_notify_add_while_alive (const char            *key,
						    MateConfClientNotifyFunc  notify_func,
						    GObject               *alive_object);

void            panel_mateconf_copy_dir      (MateConfClient  *client,
					   const char   *src_dir,
					   const char   *dest_dir);

void            panel_mateconf_associate_schemas_in_dir       (MateConfClient *client,
							    const char  *profile_dir,
							    const char  *schema_dir);

gint            panel_mateconf_value_strcmp (gconstpointer a,
					  gconstpointer b);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_MATECONF_H__ */
