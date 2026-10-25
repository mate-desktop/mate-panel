/* na-overflow.h - overflow popup window for the notification area
 * Copyright (C) 2026 kosmik2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NA_OVERFLOW_H
#define NA_OVERFLOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Which way the overflow drawer opens, i.e. away from whichever screen edge
 * the panel sits on.
 *
 * This is NOT derivable from the widget's position: gdk_window_get_origin()
 * answers surface-relative coordinates for the panel's layer-shell surface on
 * Wayland (measured: y=1 for a panel at y=1053 on a 1080p screen), so a
 * "which half of the monitor am I in" guess picks the wrong edge there.
 * The panel states the edge over the applet protocol, which is the only
 * truthful source on both backends. */
typedef enum
{
  NA_OPEN_UNKNOWN = 0, /* no panel told us yet (a standalone testtray) */
  NA_OPEN_UP,          /* panel at the bottom edge: drawer grows upwards */
  NA_OPEN_DOWN,        /* panel at the top edge: drawer hangs below */
  NA_OPEN_LEFT,        /* panel on the right edge: drawer opens left */
  NA_OPEN_RIGHT        /* panel on the left edge: drawer opens right */
} NaOpenDirection;

#define NA_TYPE_OVERFLOW (na_overflow_get_type ())
G_DECLARE_FINAL_TYPE (NaOverflow, na_overflow, NA, OVERFLOW, GtkWindow)

NaOverflow *na_overflow_new              (void);
GtkWidget  *na_overflow_get_grid         (NaOverflow  *self);
/* Forward a click to a parked XEmbed icon.
 *
 * A GtkSocket deliberately does not select ButtonPress on its plug window, so a
 * press on an icon inside the drawer is not delivered to the client by X. The
 * only way to get it there is a synthetic XSendEvent straight into the plug
 * window. The list of parked sockets belongs to the grid, which is why the grid
 * registers this handler here.
 *
 * Returns TRUE when the press/release reached the client.
 */
typedef gboolean (*NaOverflowButtonClick) (GdkEventButton *event,
                                           gboolean        is_press,
                                           gpointer         user_data);

void        na_overflow_set_autohide_ms  (NaOverflow           *self,
                                          guint                 ms);
/* The edge the panel sits on, as the panel itself reports it.  Set before the
 * drawer is popped up; drives both the chevron arrow and the popup anchors. */
void        na_overflow_set_open_direction (NaOverflow      *self,
                                            NaOpenDirection  dir);
/* The drawer's size as the grid layout computes it, without the border.
 *
 * The pre-map size REQUEST cannot be trusted here: an SNI button derives its
 * icon size from the allocation it receives, so before the first allocation the
 * whole grid collapses to a single cell, and clamping the position with that
 * underestimated width pushed the drawer off the right edge of the screen
 * (measured: request 34x34 for a drawer that maps far wider). The grid knows
 * the cell count and the cell size it just assigned, so it states the size. */
void        na_overflow_set_nominal_size (NaOverflow *self,
                                          gint        width,
                                          gint        height);
void        na_overflow_set_button_click (NaOverflow           *self,
                                          NaOverflowButtonClick func,
                                          gpointer              user_data,
                                          GDestroyNotify        destroy);

gboolean    na_overflow_popup            (NaOverflow  *self,
                                          GtkWidget   *relative_to,
                                          GtkOrientation panel_orientation);
void        na_overflow_popdown          (NaOverflow  *self);
void        na_overflow_reposition       (NaOverflow  *self);

G_END_DECLS

#endif /* NA_OVERFLOW_H */
