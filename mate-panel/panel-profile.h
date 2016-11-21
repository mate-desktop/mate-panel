/*
 * panel-profile.h:
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_PROFILE_H__
#define __PANEL_PROFILE_H__

#include <glib.h>
#include <gdk/gdk.h>
#include <gio/gio.h>

#include "panel-toplevel.h"
#include "panel-enums.h"
#include "panel-types.h"
#include "applet.h"

G_BEGIN_DECLS

void panel_profile_settings_load (void);
void panel_profile_load (void);

const char    *panel_profile_get_toplevel_id    (PanelToplevel     *toplevel);
PanelToplevel *panel_profile_get_toplevel_by_id (const char        *toplevel_id);
char          *panel_profile_find_new_id        (PanelGSettingsKeyType  type);


gboolean    panel_profile_get_show_program_list   (void);
void        panel_profile_set_show_program_list   (gboolean show_program_list);
gboolean    panel_profile_is_writable_show_program_list (void);
gboolean    panel_profile_get_enable_program_list (void);
gboolean    panel_profile_get_enable_autocompletion (void);


void           panel_profile_add_to_list            (PanelGSettingsKeyType  type,
						     const char        *id);
void           panel_profile_remove_from_list       (PanelGSettingsKeyType  type,
						     const char        *id);
gboolean       panel_profile_id_lists_are_writable  (void);
void           panel_profile_create_toplevel        (GdkScreen         *screen);
PanelToplevel *panel_profile_load_toplevel          (const char        *toplevel_id);
void           panel_profile_delete_toplevel        (PanelToplevel     *toplevel);
char          *panel_profile_prepare_object         (PanelObjectType    object_type,
						     PanelToplevel     *toplevel,
						     int                position,
						     gboolean           right_stick);
char          *panel_profile_prepare_object_with_id (PanelObjectType    object_type,
						     const char        *toplevel_id,
						     int                position,
						     gboolean           right_stick);
void           panel_profile_delete_object          (AppletInfo        *applet_info);

gboolean    panel_profile_key_is_writable            (PanelToplevel *toplevel,
						      gchar         *key);
gboolean    panel_profile_background_key_is_writable (PanelToplevel *toplevel,
						      gchar         *key);

void        panel_profile_set_toplevel_name           (PanelToplevel *toplevel,
						       const char    *name);
char       *panel_profile_get_toplevel_name           (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_orientation    (PanelToplevel *toplevel,
						       PanelOrientation orientation);
PanelOrientation
            panel_profile_get_toplevel_orientation    (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_size           (PanelToplevel *toplevel,
						       int            size);
int         panel_profile_get_toplevel_size           (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_expand         (PanelToplevel *toplevel,
						       gboolean       expand);
gboolean    panel_profile_get_toplevel_expand         (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_auto_hide      (PanelToplevel *toplevel,
						       gboolean       auto_hide);
gboolean    panel_profile_get_toplevel_auto_hide      (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_enable_buttons (PanelToplevel *toplevel,
						       gboolean       enable_buttons);
gboolean    panel_profile_get_toplevel_enable_buttons (PanelToplevel *toplevel);

void        panel_profile_set_toplevel_enable_arrows  (PanelToplevel *toplevel,
						       gboolean       enable_arrows);
gboolean    panel_profile_get_toplevel_enable_arrows  (PanelToplevel *toplevel);

/* We don't set this in the panel, so there is no set accessor */
void        panel_profile_set_background_type         (PanelToplevel       *toplevel,
						       PanelBackgroundType  background_type);
PanelBackgroundType
	    panel_profile_get_background_type         (PanelToplevel       *toplevel);

void        panel_profile_set_background_color        (PanelToplevel       *toplevel,
						       GdkRGBA             *color);
void        panel_profile_get_background_color        (PanelToplevel       *toplevel,
						       GdkRGBA             *color);

void        panel_profile_set_background_gdk_rgba    (PanelToplevel       *toplevel,
						       GdkRGBA            *color);
void        panel_profile_get_background_gdk_rgba    (PanelToplevel       *toplevel,
						       GdkRGBA            *color);

void        panel_profile_set_background_opacity      (PanelToplevel       *toplevel,
						       guint16              opacity);
guint16     panel_profile_get_background_opacity      (PanelToplevel       *toplevel);

void        panel_profile_set_background_image        (PanelToplevel       *toplevel,
						       const char          *image);
char       *panel_profile_get_background_image        (PanelToplevel       *toplevel);

void        panel_profile_set_background_fit          (PanelToplevel       *toplevel,
						       gboolean             fit);
gboolean    panel_profile_get_background_fit          (PanelToplevel       *toplevel);

void        panel_profile_set_background_stretch      (PanelToplevel       *toplevel,
						       gboolean             stretch);
gboolean    panel_profile_get_background_stretch      (PanelToplevel       *toplevel);

void        panel_profile_set_background_rotate       (PanelToplevel       *toplevel,
						       gboolean             rotate);
gboolean    panel_profile_get_background_rotate       (PanelToplevel       *toplevel);

void        panel_profile_set_attached_custom_icon    (PanelToplevel        *toplevel,
						       const char           *custom_icon);
char       *panel_profile_get_attached_custom_icon    (PanelToplevel        *toplevel);
gboolean    panel_profile_is_writable_attached_custom_icon (PanelToplevel *toplevel);

void        panel_profile_set_attached_tooltip        (PanelToplevel        *toplevel,
						       const char           *custom_icon);
char       *panel_profile_get_attached_tooltip        (PanelToplevel        *toplevel);
gboolean    panel_profile_is_writable_attached_tooltip (PanelToplevel *toplevel);

/* all keys relevant to moving are writable */
gboolean    panel_profile_can_be_moved_freely         (PanelToplevel *toplevel);

GSettings*  panel_profile_get_attached_object_settings (PanelToplevel *toplevel);

G_END_DECLS

#endif /* __PANEL_PROFILE_H__ */
