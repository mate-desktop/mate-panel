/*
 * Copyright (C) 2016 Colomban Wendling <cwendling@hypra.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SN_COMPAT_GTK_3_22_H
#define SN_COMPAT_GTK_3_22_H

#include <gtk/gtk.h>

#include "sn-compat-gtk.h"

#if ! GTK_CHECK_VERSION (3, 22, 0)

G_BEGIN_DECLS


static inline void
sn_compat_gtk_menu_popup_at_widget_position_func (GtkMenu  *menu,
                                                  gint     *x,
                                                  gint     *y,
                                                  gboolean *push_in,
                                                  gpointer  widget)
{
  GtkAllocation  widget_alloc;
  GtkRequisition menu_req;
  GdkWindow     *window;
  GdkScreen     *screen;
  gint           monitor_num;
  GdkRectangle   monitor;

  gtk_widget_get_allocation (widget, &widget_alloc);
  gtk_widget_get_preferred_size (GTK_WIDGET (menu), &menu_req, NULL);

  window = gtk_widget_get_window (widget);
  gdk_window_get_origin (window, x, y);

  *x += widget_alloc.x;
  *y += widget_alloc.y;

  screen = gtk_widget_get_screen (widget);
  monitor_num = gdk_screen_get_monitor_at_point (screen, *x, *y);
  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  /* put the menu on the left if we can't put it on the right */
  if (*x + menu_req.width > monitor.x + monitor.width)
    *x -= menu_req.width - widget_alloc.width;
  /* and push it back in if all else failed */
  if (*x < monitor.x)
    *x = monitor.x;

  /* put the menu above if we can't put it below */
  if (*y + widget_alloc.height + menu_req.height > monitor.y + monitor.height)
    *y -= menu_req.height;
  else
    *y += widget_alloc.height;
}

/* WARNING: Only supports anchoring the menu northwest to the widget
 * southwest. */
static inline void
sn_compat_gtk_menu_popup_at_widget (GtkMenu        *menu,
                                    GtkWidget      *widget,
                                    GdkGravity      widget_anchor,
                                    GdkGravity      menu_anchor,
                                    const GdkEvent *trigger_event)
{
  guint button = 0;

  g_warn_if_fail (widget_anchor == GDK_GRAVITY_SOUTH_WEST);
  g_warn_if_fail (menu_anchor == GDK_GRAVITY_NORTH_WEST);

  if (! trigger_event)
    trigger_event = gtk_get_current_event ();

  g_return_if_fail (trigger_event != NULL);

  gdk_event_get_button (trigger_event, &button);

  gtk_menu_popup (menu, NULL, NULL,
                  sn_compat_gtk_menu_popup_at_widget_position_func,
                  widget, button, gdk_event_get_time (trigger_event));
}
#define gtk_menu_popup_at_widget sn_compat_gtk_menu_popup_at_widget


G_END_DECLS

#endif /* GTK version */
#endif
