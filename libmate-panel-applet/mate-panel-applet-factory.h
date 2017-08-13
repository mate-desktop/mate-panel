/*
 * mate-panel-applet-factory.h: panel applet writing API.
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
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

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_APPLET_FACTORY            (mate_panel_applet_factory_get_type ())
#define MATE_PANEL_APPLET_FACTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_APPLET_FACTORY, MatePanelAppletFactory))
#define MATE_PANEL_APPLET_FACTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), PANEL_TYPE_APPLET_FACTORY, MatePanelAppletFactoryClass))
#define PANEL_IS_APPLET_FACTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_APPLET_FACTORY))

typedef struct _MatePanelAppletFactory        MatePanelAppletFactory;
typedef struct _MatePanelAppletFactoryClass   MatePanelAppletFactoryClass;

GType               mate_panel_applet_factory_get_type         (void) G_GNUC_CONST;
MatePanelAppletFactory *mate_panel_applet_factory_new              (const gchar        *applet_id,
								gboolean            out_of_process,
							   GType               applet_type,
							   GClosure           *closure);
gboolean            mate_panel_applet_factory_register_service (MatePanelAppletFactory *factory);
GtkWidget          *mate_panel_applet_factory_get_applet_widget (const gchar        *id,
                                                            guint               uid);
#ifdef __cplusplus
}
#endif

#endif /* MATE_PANEL_APPLET_FACTORY_H */
