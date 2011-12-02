/*
 * panel-compatibility.c: panel backwards compatibility support
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
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "string.h"

#include <libpanel-util/panel-glib.h>

#include "panel-compatibility.h"

#include "panel-profile.h"
#include "panel-menu-bar.h"
#include "mate-panel-applet-frame.h"
#include "mate-panel-applets-manager.h"
#include "panel-globals.h"
#include "panel-util.h"

typedef enum {
	PANEL_ORIENT_UP,
	PANEL_ORIENT_DOWN,
	PANEL_ORIENT_LEFT,
	PANEL_ORIENT_RIGHT,
} PanelOrient;

static MateConfEnumStringPair panel_orient_map [] = {
	{ PANEL_ORIENT_UP,    "panel-orient-up" },
	{ PANEL_ORIENT_DOWN,  "panel-orient-down" },
	{ PANEL_ORIENT_LEFT,  "panel-orient-left" },
	{ PANEL_ORIENT_RIGHT, "panel-orient-right" },
	{ 0,                  NULL }
};

static gboolean
panel_compatibility_map_orient_string (const char  *str,
				       PanelOrient *orient)
{
	int mapped;

	g_return_val_if_fail (orient != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_orient_map, str, &mapped))
		return FALSE;

	*orient = mapped;

	return TRUE;
}

static MateConfEnumStringPair panel_orientation_map [] = {
	{ GTK_ORIENTATION_HORIZONTAL, "panel-orientation-horizontal" },
	{ GTK_ORIENTATION_VERTICAL,   "panel-orientation-vertical" },
	{ 0,                          NULL }
};


static gboolean
panel_compatibility_map_orientation_string (const char     *str,
					    GtkOrientation *orientation)
{
	int mapped;

	g_return_val_if_fail (orientation != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_orientation_map, str, &mapped))
		return FALSE;

	*orientation = mapped;

	return TRUE;
}

typedef enum {
	BORDER_TOP,
	BORDER_RIGHT,
	BORDER_BOTTOM,
	BORDER_LEFT
} BorderEdge;

static MateConfEnumStringPair panel_edge_map [] = {
	{ BORDER_TOP,    "panel-edge-top" },
	{ BORDER_RIGHT,  "panel-edge-right" },
	{ BORDER_BOTTOM, "panel-edge-bottom" },
	{ BORDER_LEFT,   "panel-edge-left" },
	{ 0,             NULL }
};

static gboolean
panel_compatibility_map_edge_string (const char *str,
				     BorderEdge *edge)
{
	int mapped;

	g_return_val_if_fail (edge != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_edge_map, str, &mapped))
		return FALSE;

	*edge = mapped;

	return TRUE;
}

typedef enum {
	EDGE_PANEL,
	DRAWER_PANEL,
	ALIGNED_PANEL,
	SLIDING_PANEL,
	FLOATING_PANEL,
	MENU_PANEL
} PanelType;

static MateConfEnumStringPair panel_type_map [] = {
	{ EDGE_PANEL,      "edge-panel" },
	{ DRAWER_PANEL,    "drawer-panel" },
	{ ALIGNED_PANEL,   "aligned-panel" },
	{ SLIDING_PANEL,   "sliding-panel" },
	{ FLOATING_PANEL,  "floating-panel" },
	{ MENU_PANEL,      "menu-panel" },
	{ 0,               NULL }
};

static gboolean
panel_compatibility_map_panel_type_string (const char *str,
					   PanelType  *type)
{
	int mapped;

	g_return_val_if_fail (type != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_type_map, str, &mapped))
		return FALSE;

	*type = mapped;

	return TRUE;
}

enum {
	PANEL_SIZE_XX_SMALL = 12,
	PANEL_SIZE_X_SMALL  = 24,
	PANEL_SIZE_SMALL    = 36,
	PANEL_SIZE_MEDIUM   = 48,
	PANEL_SIZE_LARGE    = 64,
	PANEL_SIZE_X_LARGE  = 80,
	PANEL_SIZE_XX_LARGE = 128
};

static MateConfEnumStringPair panel_size_map [] = {
	{ PANEL_SIZE_XX_SMALL, "panel-size-xx-small" },
	{ PANEL_SIZE_X_SMALL,  "panel-size-x-small" },
	{ PANEL_SIZE_SMALL,    "panel-size-small" },
	{ PANEL_SIZE_MEDIUM,   "panel-size-medium" },
	{ PANEL_SIZE_LARGE,    "panel-size-large" },
	{ PANEL_SIZE_X_LARGE,  "panel-size-x-large" },
	{ PANEL_SIZE_XX_LARGE, "panel-size-xx-large" },
	{ 0,                   NULL }
};

static gboolean
panel_compatibility_map_panel_size_string (const char *str,
					   int        *size)
{
	int mapped;

	g_return_val_if_fail (size != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_size_map, str, &mapped))
		return FALSE;

	*size = mapped;

	return TRUE;
}

static MateConfEnumStringPair panel_background_type_map [] = {
	{ PANEL_BACK_NONE,   "no-background" },
	{ PANEL_BACK_COLOR,  "color-background" },
	{ PANEL_BACK_IMAGE,  "pixmap-background" },
	{ 0,                 NULL }
};

static gboolean
panel_compatibility_map_background_type_string (const char          *str,
						PanelBackgroundType *type)
{
	int mapped;

	g_return_val_if_fail (type != NULL, FALSE);

	if (!str)
		return FALSE;

	if (!mateconf_string_to_enum (panel_background_type_map, str, &mapped))
		return FALSE;

	*type = mapped;

	return TRUE;
}

static void
panel_compatibility_migrate_background_settings (MateConfClient *client,
						 const char  *toplevel_dir,
						 const char  *panel_dir)
{
	PanelBackgroundType  type;
	const char          *key;
	char                *background_dir;
	char                *type_str;
	char                *color_str;
	char                *image_str;
	gboolean             fit;
	gboolean             stretch;
	gboolean             rotate;
	int                  opacity;

	background_dir = mateconf_concat_dir_and_key (toplevel_dir, "background");

	/* panel_background_type -> background/type */
	key = panel_mateconf_sprintf ("%s/panel_background_type", panel_dir);
	type_str = mateconf_client_get_string (client, key, NULL);

	if (panel_compatibility_map_background_type_string (type_str, &type)) {
		key = panel_mateconf_sprintf ("%s/type", background_dir);
		mateconf_client_set_string (client,
					 key,
					 panel_profile_map_background_type (type),
					 NULL);
	}

	g_free (type_str);

	/* panel_background_color -> background/color */
	key = panel_mateconf_sprintf ("%s/panel_background_color", panel_dir);
	color_str = mateconf_client_get_string (client, key, NULL);

	if (color_str) {
		key = panel_mateconf_sprintf ("%s/color", background_dir);
		mateconf_client_set_string (client, key, color_str, NULL);
	}

	g_free (color_str);

	/* panel_background_color_alpha -> background/opacity */
	key = panel_mateconf_sprintf ("%s/panel_background_color_alpha", panel_dir);
	opacity = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/opacity", background_dir);
	mateconf_client_set_int (client, key, opacity, NULL);

	/* panel_background_pixmap -> background/image */
	key = panel_mateconf_sprintf ("%s/panel_background_pixmap", panel_dir);
	image_str = mateconf_client_get_string (client, key, NULL);

	if (image_str) {
		key = panel_mateconf_sprintf ("%s/image", background_dir);
		mateconf_client_set_string (client, key, image_str, NULL);
	}

	g_free (image_str);

	/* panel_background_pixmap_fit -> background/fit */
	key = panel_mateconf_sprintf ("%s/panel_background_pixmap_fit", panel_dir);
	fit = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/fit", background_dir);
	mateconf_client_set_bool (client, key, fit, NULL);

	/* panel_background_pixmap_stretch -> background/stretch */
	key = panel_mateconf_sprintf ("%s/panel_background_pixmap_stretch", panel_dir);
	stretch = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/stretch", background_dir);
	mateconf_client_set_bool (client, key, stretch, NULL);

	/* panel_background_pixmap_rotate -> background/rotate */
	key = panel_mateconf_sprintf ("%s/panel_background_pixmap_rotate", panel_dir);
	rotate = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/rotate", background_dir);
	mateconf_client_set_bool (client, key, rotate, NULL);

	g_free (background_dir);
}

static void
panel_compatibility_migrate_edge_setting (MateConfClient *client,
					  const char  *toplevel_dir,
					  const char  *panel_dir)
{
	BorderEdge  edge;
	const char *key;
	char       *edge_str;

	key = panel_mateconf_sprintf ("%s/screen_edge", panel_dir);
	edge_str = mateconf_client_get_string (client, key, NULL);

	if (panel_compatibility_map_edge_string (edge_str, &edge)) {
		PanelOrientation orientation;

		switch (edge) {
		case BORDER_TOP:
			orientation = PANEL_ORIENTATION_TOP;
			break;
		case BORDER_BOTTOM:
			orientation = PANEL_ORIENTATION_BOTTOM;
			break;
		case BORDER_LEFT:
			orientation = PANEL_ORIENTATION_LEFT;
			break;
		case BORDER_RIGHT:
			orientation = PANEL_ORIENTATION_RIGHT;
			break;
		default:
			orientation = 0;
			g_assert_not_reached ();
			break;
		}

		key = panel_mateconf_sprintf ("%s/orientation", toplevel_dir);
		mateconf_client_set_string (client,
					 key,
					 panel_profile_map_orientation (orientation),
					 NULL);
	}

	g_free (edge_str);
}

static void
panel_compatibility_migrate_edge_panel_settings (MateConfClient *client,
						 const char  *toplevel_dir,
						 const char  *panel_dir)
{
	const char *key;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, TRUE, NULL);

	panel_compatibility_migrate_edge_setting (client, toplevel_dir, panel_dir);
}

static void
panel_compatibility_migrate_drawer_panel_settings (MateConfClient *client,
						   const char  *toplevel_dir,
						   const char  *panel_dir)
{
	PanelOrient  orient;
	const char  *key;
	char        *orient_str;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, FALSE, NULL);

	key = panel_mateconf_sprintf ("%s/panel_orient", panel_dir);
	orient_str = mateconf_client_get_string (client, key, NULL);

	if (panel_compatibility_map_orient_string (orient_str, &orient)) {
		PanelOrientation orientation;

		switch (orient) {
		case PANEL_ORIENT_DOWN:
			orientation = PANEL_ORIENTATION_TOP;
			break;
		case PANEL_ORIENT_UP:
			orientation = PANEL_ORIENTATION_BOTTOM;
			break;
		case PANEL_ORIENT_RIGHT:
			orientation = PANEL_ORIENTATION_LEFT;
			break;
		case PANEL_ORIENT_LEFT:
			orientation = PANEL_ORIENTATION_RIGHT;
			break;
		default:
			orientation = 0;
			g_assert_not_reached ();
			break;
		}

		key = panel_mateconf_sprintf ("%s/orientation", toplevel_dir);
		mateconf_client_set_string (client,
					 key,
					 panel_profile_map_orientation (orientation),
					 NULL);
	}

	g_free (orient_str);
}

static void
panel_compatibility_migrate_corner_panel_settings (MateConfClient *client,
						   const char  *toplevel_dir,
						   const char  *panel_dir)

{
	const char *key;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, FALSE, NULL);

	/* screen edge */
	panel_compatibility_migrate_edge_setting (client, toplevel_dir, panel_dir);

	g_warning ("FIXME: implement migrating the 'panel_align' setting");
}

static void
panel_compatibility_migrate_sliding_panel_settings (MateConfClient *client,
						    const char  *toplevel_dir,
						    const char  *panel_dir)
{
	const char *key;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, FALSE, NULL);

	/* screen edge */
	panel_compatibility_migrate_edge_setting (client, toplevel_dir, panel_dir);

	g_warning ("FIXME: implement migrating the 'panel_anchor' and 'panel_offset' settings");
}

static void
panel_compatibility_migrate_floating_panel_settings (MateConfClient *client,
						     const char  *toplevel_dir,
						     const char  *panel_dir)
{
	GtkOrientation  orientation;
	const char     *key;
	char           *orientation_str;
	int             x, y;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, FALSE, NULL);

	key = panel_mateconf_sprintf ("%s/panel_orient", panel_dir);
	orientation_str = mateconf_client_get_string (client, key, NULL);

	if (panel_compatibility_map_orientation_string (orientation_str, &orientation)) {
		PanelOrientation panel_orientation;

		switch (orientation) {
		case GTK_ORIENTATION_HORIZONTAL:
			panel_orientation = PANEL_ORIENTATION_TOP;
			break;
		case GTK_ORIENTATION_VERTICAL:
			panel_orientation = PANEL_ORIENTATION_LEFT;
			break;
		default:
			panel_orientation = 0;
			g_assert_not_reached ();
			break;
		}

		key = panel_mateconf_sprintf ("%s/orientation", toplevel_dir);
		mateconf_client_set_string (client,
					 key,
					 panel_profile_map_orientation (panel_orientation),
					 NULL);
	}

	g_free (orientation_str);

	/* x */
	key = panel_mateconf_sprintf ("%s/panel_x_position", panel_dir);
	x = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/x", toplevel_dir);
	mateconf_client_set_int (client, key, x, NULL);

	/* y */
	key = panel_mateconf_sprintf ("%s/panel_y_position", panel_dir);
	y = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/y", toplevel_dir);
	mateconf_client_set_int (client, key, y, NULL);
}

static void
panel_compatibility_migrate_menu_panel_settings (MateConfClient *client,
						 const char  *toplevel_dir,
						 const char  *panel_dir)
{
	const char *key;
	const char *toplevel_id;
	char       *id;

	key = panel_mateconf_sprintf ("%s/expand", toplevel_dir);
	mateconf_client_set_bool (client, key, TRUE, NULL);

	key = panel_mateconf_sprintf ("%s/orientation", toplevel_dir);
	mateconf_client_set_string (client, key,
				 panel_profile_map_orientation (PANEL_ORIENTATION_TOP),
				 NULL);

	toplevel_id = panel_mateconf_basename (toplevel_dir);

	/* menu bar on far right corner */
        id = panel_profile_prepare_object_with_id (PANEL_OBJECT_MENU_BAR, toplevel_id, 0, FALSE);
        panel_profile_add_to_list (PANEL_MATECONF_OBJECTS, id);
	g_free (id);

	/* window menu on far left corner */
        id = panel_profile_prepare_object_with_id (PANEL_OBJECT_APPLET, toplevel_id, 0, TRUE);

	key = panel_mateconf_full_key (PANEL_MATECONF_APPLETS, id, "matecomponent_iid");
        mateconf_client_set_string (client, key, "OAFIID:MATE_WindowMenuApplet", NULL);

        panel_profile_add_to_list (PANEL_MATECONF_APPLETS, id);
	g_free (id);
}

static void
panel_compatibility_migrate_panel_type (MateConfClient *client,
					const char  *toplevel_dir,
					const char  *panel_dir,
					gboolean    *is_drawer)
{
	PanelType   type;
	const char *key;
	char       *type_str;

	key = panel_mateconf_sprintf ("%s/panel_type", panel_dir);
	type_str = mateconf_client_get_string (client, key, NULL);

	if (!panel_compatibility_map_panel_type_string (type_str, &type)) {
		g_free (type_str);
		return;
	}

	g_free (type_str);

	switch (type) {
	case EDGE_PANEL:
		panel_compatibility_migrate_edge_panel_settings (client, toplevel_dir, panel_dir);
		break;
	case DRAWER_PANEL:
		panel_compatibility_migrate_drawer_panel_settings (client, toplevel_dir, panel_dir);
		*is_drawer = TRUE;
		break;
	case ALIGNED_PANEL:
		panel_compatibility_migrate_corner_panel_settings (client, toplevel_dir, panel_dir);
		break;
	case SLIDING_PANEL:
		panel_compatibility_migrate_sliding_panel_settings (client, toplevel_dir, panel_dir);
		break;
	case FLOATING_PANEL:
		panel_compatibility_migrate_floating_panel_settings (client, toplevel_dir, panel_dir);
		break;
	case MENU_PANEL:
		panel_compatibility_migrate_menu_panel_settings (client, toplevel_dir, panel_dir);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static char *
panel_compatibility_migrate_panel_settings (MateConfClient *client,
					    GSList      *toplevel_id_list,
					    const char  *panel_id,
					    gboolean    *is_drawer)
{
	const char *key;
	char       *toplevel_id;
	char       *toplevel_dir;
	char       *panel_dir;
	char       *size_str;
	int         screen;
	int         monitor;
	int         size;
	gboolean    enable_buttons;
	gboolean    enable_arrows;
	gboolean    auto_hide;

	toplevel_id = panel_profile_find_new_id (PANEL_MATECONF_TOPLEVELS);

	toplevel_dir = g_strdup_printf (PANEL_CONFIG_DIR "/toplevels/%s", toplevel_id);
	panel_dir    = g_strdup_printf (PANEL_CONFIG_DIR "/panels/%s", panel_id);

	panel_mateconf_associate_schemas_in_dir (
			client, toplevel_dir, PANEL_SCHEMAS_DIR "/toplevels");

	/* screen */
	key = panel_mateconf_sprintf ("%s/screen", panel_dir);
	screen = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/screen", toplevel_dir);
	mateconf_client_set_int (client, key, screen, NULL);

	/* monitor */
	key = panel_mateconf_sprintf ("%s/monitor", panel_dir);
	monitor = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/monitor", toplevel_dir);
	mateconf_client_set_int (client, key, monitor, NULL);

	/* size */
	key = panel_mateconf_sprintf ("%s/panel_size", panel_dir);
	size_str = mateconf_client_get_string (client, key, NULL);

	if (panel_compatibility_map_panel_size_string (size_str, &size)) {
		key = panel_mateconf_sprintf ("%s/size", toplevel_dir);
		mateconf_client_set_int (client, key, size, NULL);
	}

	g_free (size_str);

	/* enable_buttons */
	key = panel_mateconf_sprintf ("%s/hide_buttons_enabled", panel_dir);
	enable_buttons = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/enable_buttons", toplevel_dir);
	mateconf_client_set_bool (client, key, enable_buttons, NULL);

	/* enable_arrows */
	key = panel_mateconf_sprintf ("%s/hide_button_pixmaps_enabled", panel_dir);
	enable_arrows = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/enable_arrows", toplevel_dir);
	mateconf_client_set_bool (client, key, enable_arrows, NULL);

	/* auto hide */
	key = panel_mateconf_sprintf ("%s/panel_hide_mode", panel_dir);
	auto_hide = mateconf_client_get_int (client, key, NULL);

	key = panel_mateconf_sprintf ("%s/auto_hide", toplevel_dir);
	mateconf_client_set_bool (client, key, auto_hide, NULL);

	/* migrate different panel types to toplevels */
	panel_compatibility_migrate_panel_type (client, toplevel_dir, panel_dir, is_drawer);

	/* background settings */
	panel_compatibility_migrate_background_settings (client, toplevel_dir, panel_dir);

	g_free (toplevel_dir);	
	g_free (panel_dir);

	return toplevel_id;
}

static gboolean
panel_compatibility_migrate_panel_id (MateConfClient       *client,
				      PanelMateConfKeyType  key_type,
				      const char        *object_id,
				      GHashTable        *panel_id_hash)
{
	const char *key;
	char       *panel_id;
	char       *toplevel_id;
	gboolean    retval = FALSE;

	/* panel_id -> toplevel_id */
	key = panel_mateconf_full_key (key_type, object_id, "panel_id");
	panel_id = mateconf_client_get_string (client, key, NULL);

	if (panel_id && (toplevel_id = g_hash_table_lookup (panel_id_hash, panel_id))) {
		key = panel_mateconf_full_key (key_type, object_id, "toplevel_id");
		mateconf_client_set_string (client, key, toplevel_id, NULL);

		retval = TRUE;
	}

	g_free (panel_id);

	return retval;
}

static void
panel_compatibility_migrate_drawer_settings (MateConfClient       *client,
					     PanelMateConfKeyType  key_type,
					     const char        *object_id,
					     GHashTable        *panel_id_hash)
{
	const char *key;
	char       *toplevel_id;
	char       *panel_id;
	char       *custom_icon;
	char       *pixmap;

	/* unique-drawer-panel-id -> attached_toplevel_id */
	key = panel_mateconf_full_key (key_type, object_id, "attached_toplevel_id");
	toplevel_id = mateconf_client_get_string (client, key, NULL);

	key = panel_mateconf_full_key (key_type, object_id, "unique-drawer-panel-id");
	panel_id = mateconf_client_get_string (client, key, NULL);

	if (!toplevel_id && panel_id &&
	    (toplevel_id = g_hash_table_lookup (panel_id_hash, panel_id))) {
		key = panel_mateconf_full_key (key_type, object_id, "attached_toplevel_id");
		mateconf_client_set_string (client, key, toplevel_id, NULL);

		toplevel_id = NULL;
	}

	/* pixmap -> custom_icon */	
	key = panel_mateconf_full_key (key_type, object_id, "custom_icon");
	custom_icon = mateconf_client_get_string (client, key, NULL);

	key = panel_mateconf_full_key (key_type, object_id, "pixmap");
	pixmap = mateconf_client_get_string (client, key, NULL);

	if (!custom_icon && pixmap) {
		key = panel_mateconf_full_key (key_type, object_id, "custom_icon");
		mateconf_client_set_string (client, key, pixmap, NULL);

		key = panel_mateconf_full_key (key_type, object_id, "use_custom_icon");
		mateconf_client_set_bool (client, key, TRUE, NULL);
	}

	g_free (toplevel_id);
	g_free (panel_id);
	g_free (custom_icon);
	g_free (pixmap);
}

static void
panel_compatibility_migrate_menu_button_settings (MateConfClient       *client,
						  PanelMateConfKeyType  key_type,
						  const char        *object_id)
{
	const char *key;
	gboolean    use_custom_icon;
	gboolean    use_menu_path;
	char       *custom_icon;
	char       *menu_path;

	/* custom-icon -> use_custom_icon */
	key = panel_mateconf_full_key (key_type, object_id, "custom-icon");
	use_custom_icon = mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_full_key (key_type, object_id, "use_custom_icon");
	mateconf_client_set_bool (client, key, use_custom_icon, NULL);

	/* custom-icon-file -> custom_icon */
	key = panel_mateconf_full_key (key_type, object_id, "custom-icon-file");
	custom_icon = mateconf_client_get_string (client, key, NULL);

	if (custom_icon) {
		key = panel_mateconf_full_key (key_type, object_id, "custom_icon");
		mateconf_client_set_string (client, key, custom_icon, NULL);
	}

	/* main_menu -> ! use_menu_path */
	key = panel_mateconf_full_key (key_type, object_id, "main-menu");
	use_menu_path = ! mateconf_client_get_bool (client, key, NULL);

	key = panel_mateconf_full_key (key_type, object_id, "use_menu_path");
	mateconf_client_set_bool (client, key, use_menu_path, NULL);

	/* path -> menu_path */
	key = panel_mateconf_full_key (key_type, object_id, "path");
	menu_path = mateconf_client_get_string (client, key, NULL);

	if (menu_path) {
		key = panel_mateconf_full_key (key_type, object_id, "menu_path");
		mateconf_client_set_string (client, key, menu_path, NULL);
	}

	g_free (custom_icon);
	g_free (menu_path);
}

static void
panel_compatibility_migrate_objects (MateConfClient       *client,
				     PanelMateConfKeyType  key_type,
				     GHashTable        *panel_id_hash)
{
	const char *key;
	GSList     *l, *objects;

	key = panel_mateconf_general_key (panel_mateconf_key_type_to_id_list (key_type));
	objects = mateconf_client_get_list (client, key, MATECONF_VALUE_STRING, NULL);

	for (l = objects; l; l = l->next) {
		const char      *id = l->data;
		PanelObjectType  object_type;
		char            *object_type_str;

		if (!panel_compatibility_migrate_panel_id (client, key_type, id, panel_id_hash)) {
			g_free (l->data);
			continue;
		}

		key = panel_mateconf_full_key (key_type, id, "object_type");
		object_type_str = mateconf_client_get_string (client, key, NULL);

		if (panel_profile_map_object_type_string (object_type_str, &object_type)) {
			switch (object_type) {
			case PANEL_OBJECT_DRAWER:
				panel_compatibility_migrate_drawer_settings (
						client, key_type, id, panel_id_hash);
				break;
			case PANEL_OBJECT_MENU:
				panel_compatibility_migrate_menu_button_settings (
						client, key_type, id);
				break;
			default:
				break;
			}
		}
		g_free (object_type_str);
		g_free (l->data);
	}
	g_slist_free (objects);
}

/* Major hack, but we now set toplevel_id_list in the defaults database,
 * so we need to figure out if its actually set in the users database.
 */

static MateConfEngine *
get_homedir_source (void)
{
	MateConfEngine *engine;
	GError      *error = NULL;
	char        *source;

	source = g_strdup_printf ("xml:readwrite:%s/.mateconf", g_get_home_dir ());

	if (!(engine = mateconf_engine_get_for_address (source, &error))) {
#if 0
		g_warning ("Cannot get MateConf source '%s': %s\n",
			   source, error->message);
#endif
		g_error_free (error);
		g_free (source);
		return NULL;
	}

	g_free (source);

	return engine;
}

static gboolean
is_general_key_set (MateConfEngine *engine,
		    const char  *config_dir,
		    const char  *general_key)
{
	MateConfEntry *entry;
	const char *key;
	gboolean    retval;

	key = panel_mateconf_sprintf ("%s/general/%s", config_dir, general_key);

	if (!(entry = mateconf_engine_get_entry (engine, key, NULL, FALSE, NULL)))
		return FALSE;

	retval = mateconf_entry_get_value (entry)       != NULL ||
		 mateconf_entry_get_schema_name (entry) != NULL;

	mateconf_entry_unref (entry);

	return retval;
}

static gboolean
panel_compatibility_detect_needs_migration (void)
{
	MateConfEngine *engine;
	gboolean     needs_migration = FALSE;

	if (!(engine = get_homedir_source ()))
		return FALSE;

	if (!is_general_key_set (engine, PANEL_CONFIG_DIR, "panel_id_list"))
		goto no_migration;

	if (is_general_key_set (engine, PANEL_CONFIG_DIR, "toplevel_id_list"))
		goto no_migration;

	needs_migration = TRUE;

 no_migration:
	mateconf_engine_unref (engine);

	return needs_migration;
}

/* If toplevel_id_list is unset, migrate all the panels in
 * panel_id_list to toplevels
 */
void
panel_compatibility_migrate_panel_id_list (MateConfClient *client)
{
	GHashTable *panel_id_hash;
	const char *key;
	GSList     *panel_id_list;
	GSList     *toplevel_id_list = NULL;
	GSList     *l;

	if (!panel_compatibility_detect_needs_migration ())
		return;

	panel_id_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	key = panel_mateconf_general_key ("panel_id_list");
	panel_id_list = mateconf_client_get_list (client, key, MATECONF_VALUE_STRING, NULL);

	for (l = panel_id_list; l; l = l->next) {
		char     *new_id;
		gboolean  is_drawer = FALSE;

		new_id = panel_compatibility_migrate_panel_settings (client,
								     toplevel_id_list,
								     l->data,
								     &is_drawer);

		/* Drawer toplevels don't belong on the toplevel list */
		if (!is_drawer)
			toplevel_id_list = g_slist_prepend (toplevel_id_list, new_id);

		g_hash_table_insert (panel_id_hash, l->data, new_id);
	}

	key = panel_mateconf_general_key ("toplevel_id_list");
	mateconf_client_set_list (client, key, MATECONF_VALUE_STRING, toplevel_id_list, NULL);

	g_slist_free (panel_id_list);
	g_slist_free (toplevel_id_list);

	panel_compatibility_migrate_objects (client, PANEL_MATECONF_OBJECTS, panel_id_hash);
	panel_compatibility_migrate_objects (client, PANEL_MATECONF_APPLETS, panel_id_hash);

	g_hash_table_destroy (panel_id_hash);
}

static void
copy_mateconf_dir (MateConfClient  *client,
		const char   *src_dir,
		const char   *dest_dir)
{
	GSList *list, *l;

	list = mateconf_client_all_entries (client, src_dir, NULL);
	for (l = list; l; l = l->next) {
		MateConfEntry *entry = l->data;
		const char *key;
		char       *tmp;

		tmp = g_path_get_basename (mateconf_entry_get_key (entry));
		key = panel_mateconf_sprintf ("%s/%s", dest_dir, tmp);
		g_free (tmp);

		if (mateconf_entry_get_schema_name (entry))
			mateconf_engine_associate_schema (client->engine,
						       key,
						       mateconf_entry_get_schema_name (entry),
						       NULL);

		if (entry->value)
			mateconf_client_set (client, key, entry->value, NULL);

		mateconf_entry_unref (entry);
	}
	g_slist_free (list);

	list = mateconf_client_all_dirs (client, src_dir, NULL);
	for (l = list; l; l = l->next) {
		char *subdir = l->data;
		char *src_subdir;
		char *dest_subdir;
		char *tmp;

		tmp = g_path_get_basename (subdir);
		src_subdir  = mateconf_concat_dir_and_key (src_dir,  tmp);
		dest_subdir = mateconf_concat_dir_and_key (dest_dir, tmp);
		g_free (tmp);

		copy_mateconf_dir (client, src_subdir, dest_subdir);

		g_free (src_subdir);
		g_free (dest_subdir);
		g_free (subdir);
	}

	g_slist_free (list);
}

void
panel_compatibility_maybe_copy_old_config (MateConfClient *client)
{
	MateConfEngine *engine;
	const char  *key;

	key = panel_mateconf_general_key ("profiles_migrated");
	if (mateconf_client_get_bool (client, key, NULL))
		return;

	if (!(engine = get_homedir_source ()))
		goto no_migration_needed;;

	if (!is_general_key_set (engine, PANEL_OLD_CONFIG_DIR, "panel_id_list")    &&
	    !is_general_key_set (engine, PANEL_OLD_CONFIG_DIR, "toplevel_id_list") &&
	    !is_general_key_set (engine, PANEL_OLD_CONFIG_DIR, "applet_id_list")   &&
	    !is_general_key_set (engine, PANEL_OLD_CONFIG_DIR, "object_id_list"))
		goto no_migration_needed;

	copy_mateconf_dir (client, PANEL_OLD_CONFIG_DIR, PANEL_CONFIG_DIR);

	key = panel_mateconf_general_key ("profiles_migrated");
	mateconf_client_set_bool (client, key, TRUE, NULL);

 no_migration_needed:
	if (engine)
		mateconf_engine_unref (engine);
}

void
panel_compatibility_migrate_applications_scheme (MateConfClient *client,
						 const char  *key)
{
	char *location;

	location = mateconf_client_get_string (client, key, NULL);

	if (!location)
		return;

	if (!strncmp (location, "applications:", strlen ("applications:")) ||
	    !strncmp (location, "applications-all-users:", strlen ("applications-all-users:")) ||
	    !strncmp (location, "all-applications:", strlen ("all-applications:")) ||
	    !strncmp (location, "preferences:", strlen ("preferences:")) ||
	    !strncmp (location, "preferences-all-users:", strlen ("preferences-all-users:")) ||
	    !strncmp (location, "all-preferences:", strlen ("all-preferences:")) ||
	    !strncmp (location, "system-settings:", strlen ("system-settings:")) ||
	    !strncmp (location, "server-settings:", strlen ("server-settings:"))) {
		char *basename;
		char *new_location;

		basename = g_path_get_basename (location);
		new_location = panel_g_lookup_in_applications_dirs (basename);
		g_free (basename);

		if (new_location != NULL) {
			mateconf_client_set_string (client, key,
						 new_location, NULL);
			g_free (new_location);
		}
	}

	g_free (location);
}

void
panel_compatibility_migrate_screenshot_action (MateConfClient *client,
					      const char  *id)
{
	const char *key;

	panel_profile_remove_from_list (PANEL_MATECONF_OBJECTS, id);

	key = panel_mateconf_full_key (PANEL_MATECONF_OBJECTS, id,
				    "launcher_location");
	mateconf_client_set_string (client, key, "mate-screenshot.desktop", NULL);

	key = panel_mateconf_full_key (PANEL_MATECONF_OBJECTS, id,
				    "object_type");
	//FIXME: ideally, we would use panel_object_type_map, but it's private
	//in panel-profile.c
	mateconf_client_set_string (client, key, "launcher-object", NULL);

	panel_profile_add_to_list (PANEL_MATECONF_OBJECTS, id);
}

gchar *
panel_compatibility_get_applet_iid (const gchar *id)
{
	MateConfClient *client = panel_mateconf_get_client ();
	MatePanelAppletInfo *info;
	const char *key;
	gchar *applet_iid;
	gboolean needs_migration;
	const char *iid;

	/*
	 * There are two compatibility steps here:
	 *
	 * 1) we need to migrate from matecomponent_iid to applet_iid if there's no
	 *    value in the applet_iid key. Always.
	 *
	 * 2) we need to try to migrate the iid to a new iid. We can't assume
	 *    that the fact that the applet_iid key was used mean anything
	 *    since the value there could well be a matecomponent iid.
	 *    The reason we really have to try to migrate first is this case:
	 *    if an applet was added with the matecomponent iid but gets ported later
	 *    to dbus, then the reference to the matecomponent iid will only be valid
	 *    as an old reference.
	 *    And if migration fails, we just use the iid as it is.
	 */

	needs_migration = FALSE;

	key = panel_mateconf_full_key (PANEL_MATECONF_APPLETS, id, "applet_iid");
	applet_iid = mateconf_client_get_string (client, key, NULL);

	if (!applet_iid || !applet_iid[0]) {
		needs_migration = TRUE;

		key = panel_mateconf_full_key (PANEL_MATECONF_APPLETS, id, "matecomponent_iid");
		applet_iid = mateconf_client_get_string (client, key, NULL);

		if (!applet_iid || !applet_iid[0])
			return NULL;
	}

	info = mate_panel_applets_manager_get_applet_info_from_old_id (applet_iid);
	if (!info)
		info = mate_panel_applets_manager_get_applet_info (applet_iid);

	if (!info)
		return NULL;

	iid = mate_panel_applet_info_get_iid (info);

	/* migrate if the iid in the configuration is different than the real
	 * iid that will get used */
	if (!g_str_equal (iid, applet_iid))
		needs_migration = TRUE;

	g_free (applet_iid);

	if (needs_migration) {
		key = panel_mateconf_full_key (PANEL_MATECONF_APPLETS, id, "applet_iid");
		mateconf_client_set_string (client, key, iid, NULL);
	}

	return g_strdup (iid);
}
