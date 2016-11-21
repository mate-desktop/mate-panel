/*
 * mate-panel-applet-gsettings.c: panel applet preferences handling.
 *
 * Copyright (C) 2012 Stefano Karapetsas
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

GSettings *
mate_panel_applet_settings_new (MatePanelApplet *applet, gchar *schema)
{
    GSettings *settings = NULL;
    gchar *path;

    g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

    path = mate_panel_applet_get_preferences_path (applet);

    if (path) {
        settings = g_settings_new_with_path (schema, path);
        g_free (path);
    }

    return settings;
}

GList*
mate_panel_applet_settings_get_glist (GSettings *settings, gchar *key)
{
    gchar **array;
    GList *list = NULL;
    gint i;
    array = g_settings_get_strv (settings, key);
    if (array != NULL) {
        for (i = 0; array[i]; i++) {
            list = g_list_append (list, g_strdup (array[i]));
        }
    }
    g_strfreev (array);
    return list;
}

void
mate_panel_applet_settings_set_glist (GSettings *settings, gchar *key, GList *list)
{
    GArray *array;
    GList *l;
    array = g_array_new (TRUE, TRUE, sizeof (gchar *));
    for (l = list; l; l = l->next) {
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
    gint i;
    array = g_settings_get_strv (settings, key);
    if (array != NULL) {
        for (i = 0; array[i]; i++) {
            list = g_slist_append (list, g_strdup (array[i]));
        }
    }
    g_strfreev (array);
    return list;
}

void
mate_panel_applet_settings_set_gslist (GSettings *settings, gchar *key, GSList *list)
{
    GArray *array;
    GSList *l;
    array = g_array_new (TRUE, TRUE, sizeof (gchar *));
    for (l = list; l; l = l->next) {
        array = g_array_append_val (array, l->data);
    }
    g_settings_set_strv (settings, key, (const gchar **) array->data);
    g_array_free (array, TRUE);
}
