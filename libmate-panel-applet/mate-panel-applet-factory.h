/*
 * mate-panel-applet-factory.h: panel applet writing API.
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
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
 */

#ifndef MATE_PANEL_APPLET_FACTORY_H
#define MATE_PANEL_APPLET_FACTORY_H

#include <glib-object.h>

#include "mate-panel-applet.h"

G_BEGIN_DECLS

#define PANEL_TYPE_APPLET_FACTORY mate_panel_applet_factory_get_type ()
G_DECLARE_FINAL_TYPE (MatePanelAppletFactory, mate_panel_applet_factory, MATE_PANEL, APPLET_FACTORY, GObject)

MatePanelAppletFactory *mate_panel_applet_factory_new               (const gchar            *applet_id,
                                                                     gboolean                out_of_process,
                                                                     GType                   applet_type,
                                                                     GClosure               *closure);
gboolean                mate_panel_applet_factory_register_service  (MatePanelAppletFactory *factory);
GtkWidget              *mate_panel_applet_factory_get_applet_widget (const gchar            *id,
                                                                     guint                   uid);

G_END_DECLS

#endif /* MATE_PANEL_APPLET_FACTORY_H */
