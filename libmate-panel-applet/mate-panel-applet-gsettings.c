/*
 * mate-panel-applet-gsettings.c: panel applet preferences handling.
 *
 * Copyright (C) 2012 Stefano Karapetsas
 * Copyright (C) 2012-2021 MATE Developers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *     Stefano Karapetsas <stefano@karapetsas.com>
 */

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "mate-panel-applet.h"
#include "mate-panel-applet-gsettings.h"

static GVariant *
add_to_dict (GVariant *dict, const gchar *schema, const gchar *path)
{
    GVariantIter iter;
    GVariantBuilder builder;
    gboolean is_schema_found;
    gboolean is_incorrect_schema;
    gint path_counter;

    gchar *key;
    gchar *value;

    g_variant_builder_init (&builder, (const GVariantType *) "a{ss}");
    g_variant_iter_init (&iter, dict);

    is_schema_found = FALSE;
    is_incorrect_schema = FALSE;
    path_counter = 0;

    while (g_variant_iter_next (&iter, "{ss}", &key, &value)) {
        gboolean path_is_found = FALSE;
        if (g_strcmp0 (value, path) == 0) {
            path_is_found = TRUE;
            path_counter++;
            if (g_strcmp0 (key, schema) == 0) {
                is_schema_found = TRUE;
            } else {
                // skip incoorect schema for path
                is_incorrect_schema = TRUE;
                g_free (key);
                g_free (value);
                continue;
            }
        }

        gboolean need_add_to_dict = !path_is_found || path_counter < 2;

        if (need_add_to_dict) {
            g_variant_builder_add (&builder, "{ss}", key, value);
        }

        g_free (key);
        g_free (value);
    }

    if (!is_schema_found) {
        g_variant_builder_add (&builder, "{ss}", schema, path);
    }

    if (!is_schema_found || is_incorrect_schema || (path_counter > 1)) {
        return g_variant_ref_sink (g_variant_builder_end (&builder));
    } else {
        g_variant_builder_clear (&builder);
        // no changes
        return NULL;
    }
}

static void
register_dconf_editor_relocatable_schema (const gchar *schema, const gchar *path)
{
    GSettings *dconf_editor_settings;
    dconf_editor_settings = g_settings_new ("ca.desrt.dconf-editor.Settings");

    if (dconf_editor_settings && g_settings_is_writable (dconf_editor_settings, "relocatable-schemas-user-paths")) {
        GVariant *relocatable_schemas = g_settings_get_value (dconf_editor_settings, "relocatable-schemas-user-paths");

        if (g_variant_is_of_type (relocatable_schemas, G_VARIANT_TYPE_DICTIONARY)) {
            GVariant * new_relocatable_schemas = add_to_dict (relocatable_schemas, schema, path);
            if (new_relocatable_schemas) {
                g_settings_set_value (dconf_editor_settings, "relocatable-schemas-user-paths", new_relocatable_schemas);
                g_variant_unref (new_relocatable_schemas);
            }
        }

        g_variant_unref (relocatable_schemas);
    }

    g_object_unref (dconf_editor_settings);
}

GSettings *
mate_panel_applet_settings_new (MatePanelApplet *applet, gchar *schema)
{
    GSettings *settings = NULL;
    gchar *path;

    g_return_val_if_fail (MATE_PANEL_IS_APPLET (applet), NULL);

    path = mate_panel_applet_get_preferences_path (applet);

    if (path) {
        settings = g_settings_new_with_path (schema, path);
        register_dconf_editor_relocatable_schema (schema, path);
        g_free (path);
    }

    return settings;
}

GList*
mate_panel_applet_settings_get_glist (GSettings *settings, gchar *key)
{
    gchar **array;
    GList *list = NULL;

    array = g_settings_get_strv (settings, key);
    if (array != NULL) {
        for (gint i = 0; array[i]; i++) {
            list = g_list_prepend (list, array[i]);
        }
        g_free (array);
    }
    return g_list_reverse (list);
}

void
mate_panel_applet_settings_set_glist (GSettings *settings, gchar *key, GList *list)
{
    GArray *array;

    array = g_array_new (TRUE, TRUE, sizeof (gchar *));
    for (GList *l = list; l; l = l->next) {
        array = g_array_append_val (array, l->data);
    }
    g_settings_set_strv (settings, key, (const gchar **) array->data);
    g_array_free (array, TRUE);
}

GSList*
mate_panel_applet_settings_get_gslist (GSettings *settings, gchar *key)
{
    gchar **array;
    GSList *list = NULL;

    array = g_settings_get_strv (settings, key);
    if (array != NULL) {
        for (gint i = 0; array[i]; i++) {
            list = g_slist_prepend (list, array[i]);
        }
        g_free (array);
    }
    return g_slist_reverse (list);
}

void
mate_panel_applet_settings_set_gslist (GSettings *settings, gchar *key, GSList *list)
{
    GArray *array;

    array = g_array_new (TRUE, TRUE, sizeof (gchar *));
    for (GSList *l = list; l; l = l->next) {
        array = g_array_append_val (array, l->data);
    }
    g_settings_set_strv (settings, key, (const gchar **) array->data);
    g_array_free (array, TRUE);
}
