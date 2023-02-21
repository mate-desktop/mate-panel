/*
 * mate-panel-applet-gsettings.h: panel applet preferences handling.
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

#ifndef __MATE_PANEL_APPLET_GSETTINGS_H__
#define __MATE_PANEL_APPLET_GSETTINGS_H__

#include <glib.h>
#include <gio/gio.h>

#include <mate-panel-applet.h>

#define PANEL_OBJECT_PREFS_PATH "/org/mate/panel/objects/%s/prefs"

G_BEGIN_DECLS

/**
 * mate_panel_applet_settings_new:
 * @applet A #MatePanelApplet
 * @schema applet's schema id
 *
 * Returns: (transfer full): a #GSettings. free when you used it
 */
GSettings* mate_panel_applet_settings_new (MatePanelApplet *applet, gchar *schema);

GList*     mate_panel_applet_settings_get_glist (GSettings *settings, gchar *key);
void       mate_panel_applet_settings_set_glist (GSettings *settings, gchar *key, GList *list);
GSList*    mate_panel_applet_settings_get_gslist (GSettings *settings, gchar *key);
void       mate_panel_applet_settings_set_gslist (GSettings *settings, gchar *key, GSList *list);

G_END_DECLS

#endif /* __MATE_PANEL_APPLET_GSETTINGS_H__ */
