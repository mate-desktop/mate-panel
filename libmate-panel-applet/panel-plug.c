/*
 * Copyright (C) 2016 Alberts Muktupāvels
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#include <config.h>

#ifndef HAVE_X11
#error file should only be built when HAVE_X11 is enabled
#endif

#include <gtk/gtk.h>

#include "panel-plug-private.h"

struct _PanelPlug
{
  GtkPlug parent;
};

G_DEFINE_TYPE (PanelPlug, panel_plug, GTK_TYPE_PLUG)

static gboolean
panel_plug_draw (GtkWidget *widget,
                 cairo_t   *cr)
{
  GdkWindow *window;
  cairo_pattern_t *pattern;

  if (!gtk_widget_get_realized (widget))
    return GTK_WIDGET_CLASS (panel_plug_parent_class)->draw (widget, cr);

  window = gtk_widget_get_window (widget);
  pattern = gdk_window_get_background_pattern (window);

  if (!pattern)
    {
      GtkStyleContext *context;
      gint width;
      gint height;

      context = gtk_widget_get_style_context (widget);
      width = gtk_widget_get_allocated_width (widget);
      height = gtk_widget_get_allocated_height (widget);

      gtk_render_background (context, cr, 0, 0, width, height);
    }

  return GTK_WIDGET_CLASS (panel_plug_parent_class)->draw (widget, cr);
}

static void
panel_plug_realize (GtkWidget *widget)
{
  GdkScreen *screen;
  GdkVisual *visual;

  screen = gdk_screen_get_default ();
  visual = gdk_screen_get_rgba_visual (screen);

  if (!visual)
    visual = gdk_screen_get_system_visual (screen);

  gtk_widget_set_visual (widget, visual);

  GTK_WIDGET_CLASS (panel_plug_parent_class)->realize (widget);
}

static void
panel_plug_class_init (PanelPlugClass *plug_class)
{
  GtkWidgetClass *widget_class;

  widget_class = GTK_WIDGET_CLASS (plug_class);

  widget_class->draw = panel_plug_draw;
  widget_class->realize = panel_plug_realize;

  gtk_widget_class_set_css_name (widget_class, "PanelApplet");
}

static void
panel_plug_init (PanelPlug *plug)
{
  gtk_widget_set_app_paintable (GTK_WIDGET (plug), TRUE);
}

GtkWidget *
panel_plug_new (void)
{
  return g_object_new (PANEL_TYPE_PLUG, NULL);
}
