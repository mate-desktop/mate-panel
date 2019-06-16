/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003-2006 Vincent Untz
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __NA_TRAY_APPLET_H__
#define __NA_TRAY_APPLET_H__

#include <mate-panel-applet.h>

#define NA_RESOURCE_PATH "/org/mate/panel/applet/na/"

#define NA_TRAY_SCHEMA                  "org.mate.panel.applet.notification-area"
#define KEY_MIN_ICON_SIZE               "min-icon-size"

G_BEGIN_DECLS

#define NA_TYPE_TRAY_APPLET             (na_tray_applet_get_type ())
#define NA_TRAY_APPLET(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), NA_TYPE_TRAY_APPLET, NaTrayApplet))
#define NA_TRAY_APPLET_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), NA_TYPE_TRAY_APPLET, NaTrayAppletClass))
#define NA_IS_TRAY_APPLET(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NA_TYPE_TRAY_APPLET))
#define NA_IS_TRAY_APPLET_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), NA_TYPE_TRAY_APPLET))
#define NA_TRAY_APPLET_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), NA_TYPE_TRAY_APPLET, NaTrayAppletClass))

typedef struct _NaTrayApplet        NaTrayApplet;
typedef struct _NaTrayAppletClass   NaTrayAppletClass;
typedef struct _NaTrayAppletPrivate NaTrayAppletPrivate;

struct _NaTrayApplet
{
  MatePanelApplet parent_object;

  /*< private >*/
  NaTrayAppletPrivate *priv;
};

struct _NaTrayAppletClass
{
  MatePanelAppletClass parent_class;
};

GType na_tray_applet_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __NA_TRAY_APPLET_H__ */
