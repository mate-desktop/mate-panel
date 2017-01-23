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

#include "config.h"

#include "sn-dbus-menu.h"
#include "sn-item.h"

#include "na-item.h"

struct _SnItemPrivate
{
  gchar          *bus_name;
  gchar          *object_path;

  GtkOrientation  orientation;

  GtkMenu        *menu;
};

enum
{
  PROP_0,

  PROP_BUS_NAME,
  PROP_OBJECT_PATH,

  PROP_ORIENTATION,

  LAST_PROP
};

enum
{
  SIGNAL_READY,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void na_item_init (NaItemInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (SnItem, sn_item, SN_TYPE_FLAT_BUTTON,
                                  G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE,
                                                         NULL)
                                  G_IMPLEMENT_INTERFACE (NA_TYPE_ITEM,
                                                         na_item_init))

#define sn_item_get_instance_private(i) (SN_ITEM (i)->priv)

static void
sn_item_dispose (GObject *object)
{
  SnItem *item;
  SnItemPrivate *priv;

  item = SN_ITEM (object);
  priv = sn_item_get_instance_private (item);

  g_clear_object (&priv->menu);

  G_OBJECT_CLASS (sn_item_parent_class)->dispose (object);
}

static void
sn_item_finalize (GObject *object)
{
  SnItem *item;
  SnItemPrivate *priv;

  item = SN_ITEM (object);
  priv = sn_item_get_instance_private (item);

  g_clear_pointer (&priv->bus_name, g_free);
  g_clear_pointer (&priv->object_path, g_free);

  G_OBJECT_CLASS (sn_item_parent_class)->finalize (object);
}

static void
sn_item_get_property (GObject    *object,
                      guint       property_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  SnItem *item;
  SnItemPrivate *priv;

  item = SN_ITEM (object);
  priv = sn_item_get_instance_private (item);

  switch (property_id)
    {
      case PROP_BUS_NAME:
        g_value_set_string (value, priv->bus_name);
        break;

      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;

      case PROP_ORIENTATION:
        g_value_set_enum (value, priv->orientation);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
sn_item_set_property (GObject      *object,
                      guint         property_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  SnItem *item;
  SnItemPrivate *priv;

  item = SN_ITEM (object);
  priv = sn_item_get_instance_private (item);

  switch (property_id)
    {
      case PROP_BUS_NAME:
        priv->bus_name = g_value_dup_string (value);
        break;

      case PROP_OBJECT_PATH:
        priv->object_path = g_value_dup_string (value);
        break;

      case PROP_ORIENTATION:
        priv->orientation = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
sn_item_get_action_coordinates (SnItem *item,
                                gint   *x,
                                gint   *y)
{
  GtkWidget *widget;
  SnItemPrivate *priv;
  GdkWindow *window;
  GtkWidget *toplevel;
  gint width;
  gint height;

  priv = sn_item_get_instance_private (item);
  widget = GTK_WIDGET (item);
  window = gtk_widget_get_window (widget);
  toplevel = gtk_widget_get_toplevel (widget);

  gdk_window_get_geometry (window, x, y, &width, &height);
  gtk_widget_translate_coordinates (widget, toplevel, *x, *y, x, y);

  if (priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    *y += height;
  else
    *x += width;
}

#if ! GTK_CHECK_VERSION (3, 22, 0)
static void
sn_item_popup_menu_position_func (GtkMenu  *menu,
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
#endif

static gboolean
sn_item_button_press_event (GtkWidget      *widget,
                            GdkEventButton *event)
{
  SnItem *item;
  SnItemPrivate *priv;
  gint x;
  gint y;

  if (event->button < 2 || event->button > 3)
    return GTK_WIDGET_CLASS (sn_item_parent_class)->button_press_event (widget, event);

  item = SN_ITEM (widget);
  priv = sn_item_get_instance_private (item);

  sn_item_get_action_coordinates (item, &x, &y);

  if (event->button == 2)
    {
#if GTK_CHECK_VERSION (3, 20, 0)
      gdk_seat_ungrab (gdk_device_get_seat (event->device));
#else
      gdk_device_ungrab (event->device, GDK_CURRENT_TIME);
#endif
      SN_ITEM_GET_CLASS (item)->secondary_activate (item, x, y);
    }
  else if (event->button == 3)
    {
      if (priv->menu != NULL)
        {
#if GTK_CHECK_VERSION (3, 22, 0)
          gtk_menu_popup_at_widget (priv->menu, widget,
                                    GDK_GRAVITY_SOUTH_WEST,
                                    GDK_GRAVITY_NORTH_WEST,
                                    (GdkEvent *) event);
#else
          gtk_menu_popup (priv->menu, NULL, NULL,
                          sn_item_popup_menu_position_func, widget,
                          event->button, event->time);
#endif
        }
      else
        {
#if GTK_CHECK_VERSION (3, 20, 0)
          gdk_seat_ungrab (gdk_device_get_seat (event->device));
#else
          gdk_device_ungrab (event->device, GDK_CURRENT_TIME);
#endif
          SN_ITEM_GET_CLASS (item)->context_menu (item, x, y);
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  return GTK_WIDGET_CLASS (sn_item_parent_class)->button_press_event (widget, event);
}

static gboolean
sn_item_popup_menu (GtkWidget *widget)
{
  SnItem *item;
  SnItemPrivate *priv;

  item = SN_ITEM (widget);
  priv = sn_item_get_instance_private (item);

  if (priv->menu != NULL)
    {
#if GTK_CHECK_VERSION (3, 22, 0)
      gtk_menu_popup_at_widget (priv->menu, widget,
                                GDK_GRAVITY_SOUTH_WEST,
                                GDK_GRAVITY_NORTH_WEST,
                                NULL);
#else
      guint button = 0;
      guint32 active_time = GDK_CURRENT_TIME;
      GdkEvent *event = gtk_get_current_event ();

      if (event)
        {
          gdk_event_get_button (event, &button);
          active_time = gdk_event_get_time (event);
        }

      gtk_menu_popup (priv->menu, NULL, NULL,
                      sn_item_popup_menu_position_func, widget,
                      button, active_time);
#endif
    }
  else
    {
      gint x;
      gint y;

      sn_item_get_action_coordinates (item, &x, &y);

      SN_ITEM_GET_CLASS (item)->context_menu (item, x, y);
    }

  return TRUE;
}

static void
sn_item_clicked (GtkButton *button)
{
  SnItem *item;
  gint x;
  gint y;

  item = SN_ITEM (button);

  sn_item_get_action_coordinates (item, &x, &y);

  SN_ITEM_GET_CLASS (item)->activate (item, x, y);
}

static gboolean
sn_item_scroll_event (GtkWidget      *widget,
                      GdkEventScroll *event)
{
  SnItem *item;
  GdkScrollDirection direction;
  SnItemOrientation orientation;
  gdouble dx;
  gdouble dy;
  gint delta;

  item = SN_ITEM (widget);

  if (!gdk_event_get_scroll_direction ((GdkEvent *) event, &direction))
    {
      g_assert_not_reached ();
    }
  else
    {
      switch (direction)
        {
          case GDK_SCROLL_UP:
          case GDK_SCROLL_DOWN:
            orientation = SN_ITEM_ORIENTATION_VERTICAL;
            break;

          case GDK_SCROLL_LEFT:
          case GDK_SCROLL_RIGHT:
            orientation = SN_ITEM_ORIENTATION_HORIZONTAL;
            break;

          case GDK_SCROLL_SMOOTH:
          default:
            g_assert_not_reached ();
            break;
        }
    }

  if (!gdk_event_get_scroll_deltas ((GdkEvent *) event, &dx, &dy))
    {
      switch (direction)
        {
          case GDK_SCROLL_UP:
          case GDK_SCROLL_LEFT:
            delta = 1;
            break;

          case GDK_SCROLL_DOWN:
          case GDK_SCROLL_RIGHT:
            delta = -1;
            break;

          case GDK_SCROLL_SMOOTH:
          default:
            g_assert_not_reached ();
            break;
        }
    }
  else
    {
      if (dy != 0)
        delta = (gint) dy;
      else
        delta = (gint) dx;
    }

  SN_ITEM_GET_CLASS (item)->scroll (item, delta, orientation);

  return GDK_EVENT_STOP;
}

static void
sn_item_ready (SnItem *item)
{
  const gchar *menu;
  SnItemPrivate *priv;

  menu = SN_ITEM_GET_CLASS (item)->get_menu (item);
  if (menu == NULL)
    return;

  if (menu == NULL || *menu == '\0' || g_strcmp0 (menu, "/") == 0)
    return;

  priv = sn_item_get_instance_private (item);
  priv->menu = sn_dbus_menu_new (priv->bus_name, menu);
  g_object_ref_sink (priv->menu);
}

static const gchar *
sn_item_get_id (NaItem *item)
{
  return SN_ITEM_GET_CLASS (item)->get_id (SN_ITEM (item));
}

static NaItemCategory
sn_item_get_category (NaItem *item)
{
  const gchar *string;
  NaItemCategory category;

  string = SN_ITEM_GET_CLASS (item)->get_category (SN_ITEM (item));

  if (g_strcmp0 (string, "Hardware") == 0)
    category = NA_ITEM_CATEGORY_HARDWARE;
  else if (g_strcmp0 (string, "SystemServices") == 0)
    category = NA_ITEM_CATEGORY_SYSTEM_SERVICES;
  else if (g_strcmp0 (string, "Communications") == 0)
    category = NA_ITEM_CATEGORY_COMMUNICATIONS;
  else
    category = NA_ITEM_CATEGORY_APPLICATION_STATUS;

  return category;
}

static void
na_item_init (NaItemInterface *iface)
{
  iface->get_id = sn_item_get_id;
  iface->get_category = sn_item_get_category;
}

static void
install_properties (GObjectClass *object_class)
{
  g_object_class_install_property (object_class, PROP_BUS_NAME,
    g_param_spec_string ("bus-name", "bus-name", "bus-name", NULL,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OBJECT_PATH,
    g_param_spec_string ("object-path", "object-path", "object-path", NULL,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS));

  g_object_class_override_property (object_class, PROP_ORIENTATION,
                                    "orientation");
}

static void
install_signals (SnItemClass *item_class)
{
  signals[SIGNAL_READY] =
    g_signal_new ("ready", G_TYPE_FROM_CLASS (item_class), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SnItemClass, ready), NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
sn_item_class_init (SnItemClass *item_class)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkButtonClass *button_class;

  object_class = G_OBJECT_CLASS (item_class);
  widget_class = GTK_WIDGET_CLASS (item_class);
  button_class = GTK_BUTTON_CLASS (item_class);

  object_class->dispose = sn_item_dispose;
  object_class->finalize = sn_item_finalize;
  object_class->get_property = sn_item_get_property;
  object_class->set_property = sn_item_set_property;

  widget_class->button_press_event = sn_item_button_press_event;
  widget_class->popup_menu = sn_item_popup_menu;
  widget_class->scroll_event = sn_item_scroll_event;

  button_class->clicked = sn_item_clicked;

  item_class->ready = sn_item_ready;

  install_properties (object_class);
  install_signals (item_class);
  
  g_type_class_add_private (item_class, sizeof (SnItemPrivate));
}

static void
sn_item_init (SnItem *item)
{
  item->priv = G_TYPE_INSTANCE_GET_PRIVATE (item, SN_TYPE_ITEM, SnItemPrivate);

  gtk_widget_add_events (GTK_WIDGET (item), GDK_SCROLL_MASK);
}

const gchar *
sn_item_get_bus_name (SnItem *item)
{
  SnItemPrivate *priv;

  priv = sn_item_get_instance_private (item);

  return priv->bus_name;
}

const gchar *
sn_item_get_object_path (SnItem *item)
{
  SnItemPrivate *priv;

  priv = sn_item_get_instance_private (item);

  return priv->object_path;
}

void
sn_item_emit_ready (SnItem *item)
{
  g_signal_emit (item, signals[SIGNAL_READY], 0);
}
