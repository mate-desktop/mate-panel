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

#ifndef NA_GRID_H
#define NA_GRID_H

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NA_TYPE_GRID			(na_grid_get_type ())
#define NA_GRID(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), NA_TYPE_GRID, NaGrid))
#define NA_GRID_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), NA_TYPE_GRID, NaGridClass))
#define NA_IS_GRID(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NA_TYPE_GRID))
#define NA_IS_GRID_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), NA_TYPE_GRID))
#define NA_GRID_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS ((obj), NA_TYPE_GRID, NaGridClass))

typedef struct _NaGrid		NaGrid;
typedef struct _NaGridClass	NaGridClass;

struct _NaGridClass
{
  GtkGridClass parent_class;
};

GType           na_grid_get_type               (void);
void            set_grid_display_mode          (NaGrid *grid, gboolean use_only_one_line, gint min_icon_size);
void            refresh_grid                   (NaGrid *self);
GtkWidget      *na_grid_new                    (GtkOrientation orientation);
void            na_grid_force_redraw           (NaGrid *grid/*, gboolean use_only_one_line, gint min_icon_size*/);

G_END_DECLS

#endif /* __NA_TRAY_H__ */
