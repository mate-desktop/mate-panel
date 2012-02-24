/*
 * mate-panel-applet-container.c: a container for applets.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef __MATE_PANEL_APPLET_CONTAINER_H__
#define __MATE_PANEL_APPLET_CONTAINER_H__

#include <glib-object.h>
#include <gtk/gtk.h>
#include "panel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_APPLET_CONTAINER            (mate_panel_applet_container_get_type ())
#define MATE_PANEL_APPLET_CONTAINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_APPLET_CONTAINER, MatePanelAppletContainer))
#define MATE_PANEL_APPLET_CONTAINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), PANEL_TYPE_APPLET_CONTAINER, MatePanelAppletContainerClass))
#define PANEL_IS_APPLET_CONTAINER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_APPLET_CONTAINER))
#define PANEL_IS_APPLET_CONTAINER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_APPLET_CONTAINER))
#define MATE_PANEL_APPLET_CONTAINER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), PANEL_TYPE_APPLET_CONTAINER, MatePanelAppletContainerClass))

#define MATE_PANEL_APPLET_CONTAINER_ERROR           (mate_panel_applet_container_error_quark())

typedef enum {
	MATE_PANEL_APPLET_CONTAINER_INVALID_APPLET,
	MATE_PANEL_APPLET_CONTAINER_INVALID_CHILD_PROPERTY
} MatePanelAppletContainerError;

typedef struct _MatePanelAppletContainer        MatePanelAppletContainer;
typedef struct _MatePanelAppletContainerClass   MatePanelAppletContainerClass;
typedef struct _MatePanelAppletContainerPrivate MatePanelAppletContainerPrivate;

struct _MatePanelAppletContainer {
	GtkEventBox parent;

	MatePanelAppletContainerPrivate *priv;
};

struct _MatePanelAppletContainerClass {
	GtkEventBoxClass parent_class;

	/* Signals */
	void (*applet_broken)          (MatePanelAppletContainer *container);
	void (*applet_move)            (MatePanelAppletContainer *container);
	void (*applet_remove)          (MatePanelAppletContainer *container);
	void (*applet_lock)            (MatePanelAppletContainer *container,
					gboolean              locked);
	void (*child_property_changed) (MatePanelAppletContainer *container,
					const gchar          *property_name,
					GVariant             *value);
};

GType      mate_panel_applet_container_get_type                (void) G_GNUC_CONST;
GQuark     mate_panel_applet_container_error_quark             (void) G_GNUC_CONST;
GtkWidget *mate_panel_applet_container_new                     (void);


void       mate_panel_applet_container_add                     (MatePanelAppletContainer *container,
							   GdkScreen            *screen,
							   const gchar          *iid,
							   GCancellable        *cancellable,
							   GAsyncReadyCallback  callback,
							   gpointer             user_data,
							   GVariant            *properties);
gboolean   mate_panel_applet_container_add_finish              (MatePanelAppletContainer *container,
							   GAsyncResult         *result,
							   GError              **error);
void       mate_panel_applet_container_child_popup_menu        (MatePanelAppletContainer *container,
							   guint                 button,
							   guint32               timestamp,
							   GCancellable         *cancellable,
							   GAsyncReadyCallback   callback,
							   gpointer              user_data);
gboolean   mate_panel_applet_container_child_popup_menu_finish (MatePanelAppletContainer *container,
							   GAsyncResult         *result,
							   GError              **error);

void       mate_panel_applet_container_child_set               (MatePanelAppletContainer *container,
							   const gchar          *property_name,
							   const GVariant       *value,
							   GCancellable         *cancellable,
							   GAsyncReadyCallback   callback,
							   gpointer              user_data);
gboolean   mate_panel_applet_container_child_set_finish        (MatePanelAppletContainer *container,
							   GAsyncResult         *result,
							   GError              **error);
void       mate_panel_applet_container_child_get               (MatePanelAppletContainer *container,
							   const gchar          *property_name,
							   GCancellable         *cancellable,
							   GAsyncReadyCallback   callback,
							   gpointer              user_data);
GVariant  *mate_panel_applet_container_child_get_finish        (MatePanelAppletContainer *container,
							   GAsyncResult         *result,
							   GError              **error);

#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLET_CONTAINER_H__ */
