/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* na-tray-tray.h
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2003-2006 Vincent Untz
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Used to be: eggtraytray.h
 */

#ifndef __NA_TRAY_H__
#define __NA_TRAY_H__

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NA_TYPE_TRAY			(na_tray_get_type ())
#define NA_TRAY(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), NA_TYPE_TRAY, NaTray))
#define NA_TRAY_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), NA_TYPE_TRAY, NaTrayClass))
#define NA_IS_TRAY(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NA_TYPE_TRAY))
#define NA_IS_TRAY_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), NA_TYPE_TRAY))
#define NA_TRAY_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS ((obj), NA_TYPE_TRAY, NaTrayClass))

typedef struct _NaTray		NaTray;
typedef struct _NaTrayPrivate	NaTrayPrivate;
typedef struct _NaTrayClass	NaTrayClass;

struct _NaTray
{
  GtkBin parent_instance;

  NaTrayPrivate *priv;
};

struct _NaTrayClass
{
  GtkBinClass parent_class;
};

GType           na_tray_get_type        (void);
NaTray         *na_tray_new_for_screen  (GdkScreen     *screen,
					 GtkOrientation orientation);
void            na_tray_set_orientation	(NaTray        *tray,
					 GtkOrientation orientation);
GtkOrientation  na_tray_get_orientation (NaTray        *tray);
void		na_tray_force_redraw	(NaTray        *tray);

#ifdef __cplusplus
}
#endif

#endif /* __NA_TRAY_H__ */
