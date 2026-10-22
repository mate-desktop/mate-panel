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

#include <config.h>

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#ifndef HAVE_X11
#include <gdk/gdkwayland.h>
#define GDK_IS_X11_DISPLAY(object)        !(G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TYPE_WAYLAND_DISPLAY))
#endif

#include <gtk/gtk.h>

#include "na-item.h"

G_BEGIN_DECLS

#define NA_TYPE_GRID (na_grid_get_type ())
G_DECLARE_FINAL_TYPE (NaGrid, na_grid, NA, GRID, GtkGrid)

/* Where a given tray icon is currently shown. */
typedef enum
{
  NA_ITEM_STATE_PINNED,   /* on the panel, in the grid itself */
  NA_ITEM_STATE_OVERFLOW, /* inside the overflow popup */
  NA_ITEM_STATE_HIDDEN    /* not shown at all, held by the grid */
} NaItemState;

void            na_grid_set_min_icon_size       (NaGrid *grid,
                                                 gint    min_icon_size);
GtkWidget      *na_grid_new                     (GtkOrientation orientation);
void            na_grid_force_redraw            (NaGrid *grid);

/* Overflow support.  The id lists are matched against na_item_get_id() and are
 * only honoured for StatusNotifierItem icons: XEmbed icons always stay on the
 * panel, because keeping an embedded socket unmapped for a long time is not
 * something every client survives.  Both lists empty means an ordinary,
 * unpatched-looking notification area. */
void            na_grid_set_hidden_icons        (NaGrid             *grid,
                                                 const gchar *const *ids);
void            na_grid_set_overflow_icons      (NaGrid             *grid,
                                                 const gchar *const *ids);
void            na_grid_set_overflow_autohide_ms (NaGrid             *grid,
                                                  guint               ms);
void            na_grid_popdown_overflow        (NaGrid             *grid);

/* TRUE if a root-coordinate point falls inside the arrow button. This lets a
 * right click on the arrow reach the applet menu: the shared handler swallows
 * button 3 so that the panel menu does not cover the tray icons' own menus, and
 * without this test the applet menu could only be summoned by clicking the
 * empty space of the applet. */
gboolean        na_grid_is_chevron_at           (NaGrid *grid,
                                                 gint    root_x,
                                                 gint    root_y);

/* Accessors for the preferences dialog.  The returned list and its items are
 * owned by the grid. */
const GSList   *na_grid_get_items               (NaGrid      *grid);
NaItemState     na_grid_get_item_state          (NaGrid      *grid,
                                                 NaItem      *item);
gboolean        na_grid_item_is_sni             (NaGrid      *grid,
                                                 NaItem      *item);

G_END_DECLS

#endif /* __NA_GRID_H__ */
