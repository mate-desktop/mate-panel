/*
 * panel-layout.c: methods to load default panels from file
 *
 * Copyright (C) 2011 Novell, Inc.
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
 *      Stefano Karapetsas <stefano@karapetsas.com>
 */

#include <config.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#include <libmate-desktop/mate-dconf.h>
#include <libmate-desktop/mate-gsettings.h>

#include "panel-layout.h"
#include "panel-profile.h"
#include "panel-schemas.h"
#include "panel-enums.h"

#define PANEL_LAYOUTS_DIR PANELDATADIR "/layouts/"

typedef struct {
        const char *name;
        GType       type;
} PanelLayoutKeyDefinition;

static PanelLayoutKeyDefinition panel_layout_toplevel_keys[] = {
        { PANEL_TOPLEVEL_NAME_KEY,              G_TYPE_STRING   },
        { PANEL_TOPLEVEL_SCREEN_KEY,            G_TYPE_INT      },
        { PANEL_TOPLEVEL_MONITOR_KEY,           G_TYPE_INT      },
        { PANEL_TOPLEVEL_EXPAND_KEY,            G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_ORIENTATION_KEY,       G_TYPE_STRING   },
        { PANEL_TOPLEVEL_SIZE_KEY,              G_TYPE_INT      },
        { PANEL_TOPLEVEL_X_KEY,                 G_TYPE_INT      },
        { PANEL_TOPLEVEL_Y_KEY,                 G_TYPE_INT      },
        { PANEL_TOPLEVEL_X_RIGHT_KEY,           G_TYPE_INT      },
        { PANEL_TOPLEVEL_Y_BOTTOM_KEY,          G_TYPE_INT      },
        { PANEL_TOPLEVEL_X_CENTERED_KEY,        G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_Y_CENTERED_KEY,        G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_AUTO_HIDE_KEY,         G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_ENABLE_ANIMATIONS_KEY, G_TYPE_BOOLEAN },
        { PANEL_TOPLEVEL_ENABLE_BUTTONS_KEY,    G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_ENABLE_ARROWS_KEY,     G_TYPE_BOOLEAN  },
        { PANEL_TOPLEVEL_HIDE_DELAY_KEY,        G_TYPE_INT      },
        { PANEL_TOPLEVEL_UNHIDE_DELAY_KEY,      G_TYPE_INT      },
        { PANEL_TOPLEVEL_AUTO_HIDE_SIZE_KEY,    G_TYPE_INT      },
        { PANEL_TOPLEVEL_ANIMATION_SPEED_KEY,   G_TYPE_STRING   }
};

static PanelLayoutKeyDefinition panel_layout_object_keys[] = {
        { PANEL_OBJECT_TYPE_KEY,                 G_TYPE_STRING   },
        { PANEL_OBJECT_TOPLEVEL_ID_KEY,          G_TYPE_STRING   },
        { PANEL_OBJECT_POSITION_KEY,             G_TYPE_INT      },
        { PANEL_OBJECT_PANEL_RIGHT_STICK_KEY,    G_TYPE_BOOLEAN  },
        { PANEL_OBJECT_LOCKED_KEY,               G_TYPE_BOOLEAN  },
        { PANEL_OBJECT_APPLET_IID_KEY,           G_TYPE_STRING   },
        { PANEL_OBJECT_ATTACHED_TOPLEVEL_ID_KEY, G_TYPE_STRING   },
        { PANEL_OBJECT_TOOLTIP_KEY,              G_TYPE_STRING   },
        { PANEL_OBJECT_USE_CUSTOM_ICON_KEY,      G_TYPE_BOOLEAN  },
        { PANEL_OBJECT_CUSTOM_ICON_KEY,          G_TYPE_STRING   },
        { PANEL_OBJECT_USE_MENU_PATH_KEY,        G_TYPE_BOOLEAN  },
        { PANEL_OBJECT_MENU_PATH_KEY,            G_TYPE_STRING   },
        { PANEL_OBJECT_HAS_ARROW_KEY,            G_TYPE_BOOLEAN  },
        { PANEL_OBJECT_LAUNCHER_LOCATION_KEY,    G_TYPE_STRING   },
        { PANEL_OBJECT_ACTION_TYPE_KEY,          G_TYPE_STRING   }
};

/*
 * return the default layout file path, making it overridable by
 * distributions
 */
static gchar *
panel_layout_filename ()
{
    GSettings *settings;
    gchar *layout;
    gchar *filename;

    settings = g_settings_new (PANEL_SCHEMA);
    layout = g_settings_get_string (settings, PANEL_DEFAULT_LAYOUT);
    if (g_str_has_prefix (layout, "/")) {
        filename = g_strdup (layout);
    }
    else {
        filename = g_strdup_printf (PANEL_LAYOUTS_DIR "%s.layout", layout);
    }
    g_free (layout);
    g_object_unref (settings);

    if (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
        return filename;
    }
    else {
        g_free (filename);
        return NULL;
    }
}

static gboolean
panel_layout_append_group_helper (GKeyFile                  *keyfile,
                                  const char                *group,
                                  int                        set_screen_to,
                                  const char                *group_prefix,
                                  char                      *id_list_key,
                                  const char                *schema,
                                  const char                *path_prefix,
                                  const char                *default_prefix,
                                  PanelLayoutKeyDefinition  *key_definitions,
                                  int                        key_definitions_len,
                                  const char                *type_for_error_message)
{
    gboolean    retval = FALSE;
    const char *id;
    char       *unique_id = NULL;
    char       *path = NULL;
    GSettings  *settings = NULL;
    char      **keyfile_keys = NULL;
    char       *value_str;
    int         value_int;
    gboolean    value_boolean;
    int         i, j;
    GError     *error = NULL;
    gchar     **existing_ids;
    gboolean    existing_id = FALSE;
    gchar      *dir = NULL;
    gchar      *dconf_path = NULL;
    PanelGSettingsKeyType type;

    /* Try to extract an id from the group, by stripping the prefix,
     * and create a unique id out of that */
    id = group + strlen (group_prefix);
    while (g_ascii_isspace (*id))
        id++;

    if (!*id)
        id = NULL;

    if (id && !mate_gsettings_is_valid_keyname (id, &error)) {
        g_warning ("Invalid id name in layout '%s' (%s)", id, error->message);
        g_error_free (error);
        return FALSE;
    }

    if (g_strcmp0 (id_list_key, PANEL_TOPLEVEL_ID_LIST_KEY) == 0) {
        dir = "toplevels";
        type = PANEL_GSETTINGS_TOPLEVELS;
    }
    else if (g_strcmp0 (id_list_key, PANEL_OBJECT_ID_LIST_KEY) == 0) {
        dir = "objects";
        type = PANEL_GSETTINGS_OBJECTS;
    }
    else {
        g_critical ("Unknown key \"%s\"", id_list_key);
	return FALSE;
    }

    dconf_path = g_strdup_printf (PANEL_RESOURCE_PATH "/%s", dir);
    existing_ids = mate_dconf_list_subdirs (dconf_path, TRUE);

    if (id) {
        if (set_screen_to > 0) {
            id = g_strdup_printf ("%s-screen%d", id, set_screen_to);
        }
        for (i = 0; existing_ids[i]; i++) {
                if (!strcmp (existing_ids[i], id)) {
                    existing_id = TRUE;
                }
        }
    }
    g_strfreev (existing_ids);
    g_free (dconf_path);

    if (existing_id || !id)
        unique_id = panel_profile_find_new_id (type);
    else
        unique_id = g_strdup (id);

    path = g_strdup_printf ("%s%s/", path_prefix, unique_id);
    settings = g_settings_new_with_path (schema, path);
    g_free (path);

    keyfile_keys = g_key_file_get_keys (keyfile, group, NULL, NULL);

    if (keyfile_keys) {

        /* validate/add keys from the keyfile */
        for (i = 0; keyfile_keys[i] != NULL; i++) {
            gboolean found = FALSE;

            for (j = 0; j < key_definitions_len; j++) {
                if (g_strcmp0 (keyfile_keys[i],
                               key_definitions[j].name) == 0) {
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                g_warning ("Unknown key '%s' for %s",
                             keyfile_keys[i],
                             unique_id);
                return FALSE;
            }

            switch (key_definitions[j].type) {
                case G_TYPE_STRING:
                    value_str = g_key_file_get_string (keyfile,
                                                       group, keyfile_keys[i],
                                                       NULL);
                    if (value_str)
                        g_settings_set_string (settings,
                                               key_definitions[j].name,
                                               value_str);
                    g_free (value_str);
                    break;

                case G_TYPE_INT:
                    value_int = g_key_file_get_integer (keyfile,
                                                        group, keyfile_keys[i],
                                                        NULL);
                    g_settings_set_int (settings,
                                        key_definitions[j].name,
                                        value_int);
                    break;

                case G_TYPE_BOOLEAN:
                    value_boolean = g_key_file_get_boolean (keyfile,
                                                            group, keyfile_keys[i],
                                                            NULL);
                    g_settings_set_boolean (settings,
                                            key_definitions[j].name,
                                            value_boolean);
                    break;
                default:
                    g_assert_not_reached ();
                    break;
            }
        }

        if (set_screen_to != -1 &&
                g_strcmp0 (schema, PANEL_TOPLEVEL_SCHEMA) == 0)
            g_settings_set_int (settings,
                                PANEL_TOPLEVEL_SCREEN_KEY,
                                set_screen_to);

        GSettings *panel_settings;
        panel_settings = g_settings_new (PANEL_SCHEMA);
        mate_gsettings_append_strv (panel_settings,
                                     id_list_key,
                                     unique_id);
        g_object_unref (panel_settings);

        retval = TRUE;
    }

    if (keyfile_keys)
        g_strfreev (keyfile_keys);

    if (settings)
        g_object_unref (settings);

    if (unique_id)
        g_free (unique_id);

    return retval;
}

static void
panel_layout_apply_minimal_default (int          set_screen_to,
                                    const char  *schema,
                                    const char  *path_prefix)
{
    const char *unique_id = "bottom";
    char       *path = NULL;
    GSettings  *settings = NULL;

    path = g_strdup_printf ("%s%s/", path_prefix, unique_id);
    settings = g_settings_new_with_path (schema, path);
    g_return_if_fail(settings);
    g_free (path);

    g_settings_set_boolean(settings, "expand", TRUE);
    g_settings_set_string(settings, "orientation", "bottom");
    g_settings_set_int(settings, "size", 24);
    g_settings_set_int(settings, "screen", 0);

    GSettings *panel_settings;
    panel_settings = g_settings_new (PANEL_SCHEMA);
    g_return_if_fail(panel_settings);
    mate_gsettings_append_strv (panel_settings,
                                PANEL_TOPLEVEL_ID_LIST_KEY,
                                unique_id);

    g_object_unref (panel_settings);
    g_object_unref (settings);
}

void
panel_layout_apply_default_from_gkeyfile (GdkScreen *screen)
{
    int          screen_n;
    gchar       *layout_file = NULL;
    GKeyFile    *keyfile = NULL;
    gchar      **groups = NULL;
    GError      *error = NULL;
    int          i;

    screen_n = 0;
#ifdef HAVE_X11
    if (GDK_IS_X11_SCREEN (screen))
	screen_n = gdk_x11_screen_get_screen_number (screen);
#endif // HAVE_11

    layout_file = panel_layout_filename();

    if (layout_file)
    {
        keyfile = g_key_file_new ();
        if (g_key_file_load_from_file (keyfile,
                                       layout_file,
                                       G_KEY_FILE_NONE,
                                       &error))
        {
            groups = g_key_file_get_groups (keyfile, NULL);

            for (i = 0; groups[i] != NULL; i++) {

                if (g_strcmp0 (groups[i], "Toplevel") == 0 ||
                        g_str_has_prefix (groups[i], "Toplevel "))

                    panel_layout_append_group_helper (
                                        keyfile, groups[i],
                                        screen_n,
                                        "Toplevel",
                                        PANEL_TOPLEVEL_ID_LIST_KEY,
                                        PANEL_TOPLEVEL_SCHEMA,
                                        PANEL_TOPLEVEL_PATH,
                                        PANEL_TOPLEVEL_DEFAULT_PREFIX,
                                        panel_layout_toplevel_keys,
                                        G_N_ELEMENTS (panel_layout_toplevel_keys),
                                        "toplevel");

                else if (g_strcmp0 (groups[i], "Object") == 0 ||
                        g_str_has_prefix (groups[i], "Object "))

                    panel_layout_append_group_helper (
                                        keyfile, groups[i],
                                        -1,
                                        "Object",
                                        PANEL_OBJECT_ID_LIST_KEY,
                                        PANEL_OBJECT_SCHEMA,
                                        PANEL_OBJECT_PATH,
                                        PANEL_OBJECT_DEFAULT_PREFIX,
                                        panel_layout_object_keys,
                                        G_N_ELEMENTS (panel_layout_object_keys),
                                        "object");

                else

                    g_warning ("Unknown group in default layout: '%s'",
                               groups[i]);

            }

        }
        else
        {
            g_warning ("Error while parsing default layout from '%s': %s\n",
                       layout_file, error->message);
            g_error_free (error);
        }

    }
    else {
        g_warning ("Cant find the layout file!");
        panel_layout_apply_minimal_default(screen_n,
                                           PANEL_TOPLEVEL_SCHEMA,
                                           PANEL_TOPLEVEL_PATH);
    }

    if (groups)
        g_strfreev (groups);

    if (keyfile)
        g_key_file_free (keyfile);

    if (layout_file)
        g_free (layout_file);
}
