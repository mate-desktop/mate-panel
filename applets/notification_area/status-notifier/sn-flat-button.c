/*
 * Copyright (C) 2017 Colomban Wendling <cwendling@hypra.fr>
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

/*
 * A GtkButton that doesn't draw anything specific but the focus, and pass
 * all drawing and sizing requests to its child.
 */

#define SN_FLAT_BUTTON_C

#include <gtk/gtk.h>

#include "sn-flat-button.h"

G_DEFINE_TYPE (SnFlatButton,
               sn_flat_button,
               GTK_TYPE_BUTTON)

static gboolean
sn_flat_button_draw (GtkWidget *widget,
                     cairo_t   *cr)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_container_propagate_draw (GTK_CONTAINER (widget), child, cr);

  if (gtk_widget_is_drawable (widget) && gtk_widget_has_focus (widget))
    {
      GtkStyleContext *context = gtk_widget_get_style_context (widget);
      gdouble x1, y1, x2, y2;

      cairo_clip_extents (cr, &x1, &y1, &x2, &y2);
      gtk_render_focus (context, cr, x1, y1, x2 - x1, y2 - y1);
    }

  return child != NULL;
}

static void
sn_flat_button_size_allocate (GtkWidget     *widget,
                              GtkAllocation *allocation)
{
  GtkWidget *child;

  GTK_WIDGET_CLASS (sn_flat_button_parent_class)->size_allocate (widget,
                                                                 allocation);

  child = gtk_bin_get_child (GTK_BIN (widget));

  if (child && gtk_widget_get_visible (child))
    gtk_widget_size_allocate (child, allocation);
}

static GtkSizeRequestMode
sn_flat_button_get_request_mode (GtkWidget *widget)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    return gtk_widget_get_request_mode (child);
  else
    return GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_request_mode (widget);
}

static void
sn_flat_button_get_preferred_height (GtkWidget *widget,
                                     gint      *min,
                                     gint      *nat)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_get_preferred_height (child, min, nat);
  else
    GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_preferred_height (widget, min, nat);
}

static void
sn_flat_button_get_preferred_width (GtkWidget *widget,
                                    gint      *min,
                                    gint      *nat)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_get_preferred_width (child, min, nat);
  else
    GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_preferred_width (widget, min, nat);
}

static void
sn_flat_button_get_preferred_height_for_width (GtkWidget *widget,
                                               gint       width,
                                               gint      *min,
                                               gint      *nat)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_get_preferred_height_for_width (child, width, min, nat);
  else
    GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_preferred_height_for_width (widget, width, min, nat);
}

static void
sn_flat_button_get_preferred_width_for_height (GtkWidget *widget,
                                               gint       height,
                                               gint      *min,
                                               gint      *nat)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_get_preferred_width_for_height (child, height, min, nat);
  else
    GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_preferred_width_for_height (widget, height, min, nat);
}

static void
sn_flat_button_get_preferred_height_and_baseline_for_width (GtkWidget *widget,
                                                            gint       width,
                                                            gint      *min,
                                                            gint      *nat,
                                                            gint      *min_baseline,
                                                            gint      *nat_baseline)
{
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_get_preferred_height_and_baseline_for_width (child, width, min, nat,
                                                            min_baseline, nat_baseline);
  else
    GTK_WIDGET_CLASS (sn_flat_button_parent_class)->get_preferred_height_and_baseline_for_width (widget, width,
                                                                                                 min, nat,
                                                                                                 min_baseline, nat_baseline);
}

static void
sn_flat_button_class_init (SnFlatButtonClass *klass)
{
  GtkWidgetClass *widget_class;

  widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->draw = sn_flat_button_draw;
  widget_class->size_allocate = sn_flat_button_size_allocate;
  widget_class->get_request_mode = sn_flat_button_get_request_mode;
  widget_class->get_preferred_height = sn_flat_button_get_preferred_height;
  widget_class->get_preferred_height_for_width = sn_flat_button_get_preferred_height_for_width;
  widget_class->get_preferred_height_and_baseline_for_width = sn_flat_button_get_preferred_height_and_baseline_for_width;
  widget_class->get_preferred_width = sn_flat_button_get_preferred_width;
  widget_class->get_preferred_width_for_height = sn_flat_button_get_preferred_width_for_height;
}

static void
sn_flat_button_init (SnFlatButton *self)
{
  gtk_button_set_relief (GTK_BUTTON (self), GTK_RELIEF_NONE);
}
