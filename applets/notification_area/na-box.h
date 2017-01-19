/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* na-tray-tray.h
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2017 Colomban Wendling <cwendling@hypra.fr>
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

#ifndef NA_BOX_H
#define NA_BOX_H

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NA_TYPE_BOX			(na_box_get_type ())
#define NA_BOX(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), NA_TYPE_BOX, NaBox))
#define NA_BOX_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), NA_TYPE_BOX, NaBoxClass))
#define NA_IS_BOX(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NA_TYPE_BOX))
#define NA_IS_BOX_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), NA_TYPE_BOX))
#define NA_BOX_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS ((obj), NA_TYPE_BOX, NaBoxClass))

typedef struct _NaBox		NaBox;
typedef struct _NaBoxClass	NaBoxClass;

struct _NaBoxClass
{
  GtkBoxClass parent_class;
};

GType           na_box_get_type         (void);
GtkWidget      *na_box_new              (GtkOrientation orientation);
void            na_box_force_redraw     (NaBox *box);

G_END_DECLS

#endif /* __NA_TRAY_H__ */
