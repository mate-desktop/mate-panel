/*
 * Copyright (C) 2010 Carlos Garcia Campos
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Carlos Garcia Campos <carlosgc@gnome.org>
 */

#ifndef PANEL_APPLET_PRIVATE_H
#define PANEL_APPLET_RPIVATE_H

#include "mate-panel-applet.h"

G_BEGIN_DECLS

// Returns 0 when not on X11
guint32      mate_panel_applet_get_xid           (MatePanelApplet *applet,
                                             GdkScreen   *screen);
const gchar *mate_panel_applet_get_object_path   (MatePanelApplet *applet);

GtkWidget   *mate_panel_applet_get_applet_widget (const gchar *factory_id,
                                              guint        uid);

G_END_DECLS

#endif
