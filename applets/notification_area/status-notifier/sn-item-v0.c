/*
 * Copyright (C) 2016 Alberts Muktupāvels
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

#include "config.h"

#include <math.h>

#include "sn-item.h"
#include "sn-item-v0.h"
#include "sn-item-v0-gen.h"

#define SN_ITEM_INTERFACE "org.kde.StatusNotifierItem"

typedef struct
{
  cairo_surface_t *surface;
  gint             width;
  gint             height;
} SnIconPixmap;

typedef struct
{
  gchar         *icon_name;
  SnIconPixmap **icon_pixmap;
  gchar         *title;
  gchar         *text;
} SnTooltip;

struct _SnItemV0
{
  SnItem         parent;

  GtkWidget     *image;
  gint           icon_size;
  gint           effective_icon_size;

  GCancellable  *cancellable;
  SnItemV0Gen   *proxy;

  gchar         *id;
  gchar         *category;
  gchar         *status;

  gchar         *title;
  gint32         window_id;
  gchar         *icon_name;
  SnIconPixmap **icon_pixmap;
  gchar         *overlay_icon_name;
  SnIconPixmap **overlay_icon_pixmap;
  gchar         *attention_icon_name;
  SnIconPixmap **attention_icon_pixmap;
  gchar         *attention_movie_name;
  SnTooltip     *tooltip;
  gchar         *icon_theme_path;
  gchar         *menu;
  gboolean       item_is_menu;

  guint          update_id;
};

enum
{
  PROP_0,

  PROP_ICON_SIZE,
  PROP_ICON_PADDING,

  LAST_PROP
};

static GParamSpec *properties[LAST_PROP] = { NULL };

G_DEFINE_TYPE (SnItemV0, sn_item_v0, SN_TYPE_ITEM)

static cairo_surface_t *
scale_surface (SnIconPixmap   *pixmap,
               GtkOrientation  orientation,
               gint            size)
{
  gdouble ratio;
  gdouble new_width;
  gdouble new_height;
  gdouble scale_x;
  gdouble scale_y;
  gint width;
  gint height;
  cairo_content_t content;
  cairo_surface_t *scaled;
  cairo_t *cr;

  g_return_val_if_fail (pixmap != NULL, NULL);

  ratio = pixmap->width / (gdouble) pixmap->height;
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      new_height = (gdouble) size;
      new_width = new_height * ratio;
    }
  else
    {
      new_width = (gdouble) size;
      new_height = new_width * ratio;
    }

  scale_x = new_width / pixmap->width;
  scale_y = new_height / pixmap->height;

  width = ceil (new_width);
  height = ceil (new_height);

  content = CAIRO_CONTENT_COLOR_ALPHA;
  scaled = cairo_surface_create_similar (pixmap->surface, content, width, height);
  cr = cairo_create (scaled);

  cairo_scale (cr, scale_x, scale_y);
  cairo_set_source_surface (cr, pixmap->surface, 0, 0);
  cairo_paint (cr);

  cairo_destroy (cr);
  return scaled;
}

static gint
compare_size (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
  SnIconPixmap *p1;
  SnIconPixmap *p2;
  GtkOrientation orientation;

  p1 = (SnIconPixmap *) a;
  p2 = (SnIconPixmap *) b;
  orientation = GPOINTER_TO_UINT (user_data);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    return p1->height - p2->height;
  else
    return p1->width - p2->width;
}

static cairo_surface_t *
get_surface (SnItemV0       *v0,
             GtkOrientation  orientation,
             gint            size)
{
  gint i;
  GList *pixmaps = NULL;
  SnIconPixmap *pixmap = NULL;
  GList *l;

  g_assert (v0->icon_pixmap != NULL && v0->icon_pixmap[0] != NULL);

  for (i = 0; v0->icon_pixmap[i] != NULL; i++)
    pixmaps = g_list_prepend (pixmaps, v0->icon_pixmap[i]);

  pixmaps = g_list_sort_with_data (pixmaps, compare_size,
                                   GUINT_TO_POINTER (orientation));

  pixmap = (SnIconPixmap *) pixmaps->data;
  for (l = pixmaps; l != NULL; l = l->next)
    {
      SnIconPixmap *p = (SnIconPixmap *) l->data;

      if (p->height > size && p->width > size)
        {
          break;
        }
      pixmap = p;
    }

  g_list_free (pixmaps);

  if (pixmap == NULL || pixmap->surface == NULL)
    return NULL;
  else if (pixmap->height > size || pixmap->width > size)
    return scale_surface (pixmap, orientation, size);
  else
    return cairo_surface_reference (pixmap->surface);
}

static cairo_surface_t *
get_icon_by_name (const gchar *icon_name,
                  gint         requested_size,
                  gint         scale)
{
  GtkIconTheme *icon_theme;
  gint *sizes;
  gint i;
  gint chosen_size = 0;

  g_return_val_if_fail (icon_name != NULL && icon_name[0] != '\0', NULL);
  g_return_val_if_fail (requested_size > 0, NULL);

  icon_theme = gtk_icon_theme_get_default ();
  gtk_icon_theme_rescan_if_needed (icon_theme);

  sizes = gtk_icon_theme_get_icon_sizes (icon_theme, icon_name);
  for (i = 0; sizes[i] != 0; i++)
    {
      if (sizes[i] == requested_size ||
          sizes[i] == -1) /* scalable */
        {
          /* perfect match, stop here */
          chosen_size = requested_size;
          break;
        }
      else if (sizes[i] < requested_size && sizes[i] > chosen_size)
        chosen_size = sizes[i];
    }
  g_free (sizes);

  if (chosen_size == 0)
    chosen_size = requested_size;

  return gtk_icon_theme_load_surface (icon_theme, icon_name,
                                      chosen_size, scale,
                                      NULL, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
}

static void
update (SnItemV0 *v0)
{
  AtkObject *accessible;
  GtkImage *image;
  SnTooltip *tip;
  gint icon_size;
  gboolean visible;
  g_return_if_fail (SN_IS_ITEM_V0 (v0));

  image = GTK_IMAGE (v0->image);

  if (v0->icon_size > 0)
    icon_size = v0->icon_size;
  else
    icon_size = MAX (1, v0->effective_icon_size);

  if (v0->icon_name != NULL && v0->icon_name[0] != '\0')
    {
      cairo_surface_t *surface;
      gint scale;

      scale = gtk_widget_get_scale_factor (GTK_WIDGET (image));
      surface = get_icon_by_name (v0->icon_name, icon_size, scale);

      if (!surface)
        {
          GdkPixbuf *pixbuf;
          /*try to find icons specified by path and filename*/
          pixbuf = gdk_pixbuf_new_from_file (v0->icon_name, NULL);
          if (pixbuf && icon_size > 1)
            {
              /*An icon specified by path and filename may be the wrong size for the tray */
              pixbuf = gdk_pixbuf_scale_simple (pixbuf, icon_size-2, icon_size-2, GDK_INTERP_BILINEAR);
              surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
            }
          if (pixbuf)
            g_object_unref (pixbuf);
        }
      if (!surface)
        {
          /*deal with missing icon or failure to load icon*/
          surface = get_icon_by_name ("image-missing", icon_size, scale);
        }
      gtk_image_set_from_surface (image, surface);
      cairo_surface_destroy (surface);
    }
  else if (v0->icon_pixmap != NULL && v0->icon_pixmap[0] != NULL)
    {
      cairo_surface_t *surface;

      surface = get_surface (v0,
                             gtk_orientable_get_orientation (GTK_ORIENTABLE (v0)),
                             icon_size);
      if (surface != NULL)
        {
          gtk_image_set_from_surface (image, surface);
          cairo_surface_destroy (surface);
        }
    }
  else
    {
      gtk_image_set_from_icon_name (image, "image-missing", GTK_ICON_SIZE_MENU);
      gtk_image_set_pixel_size (image, icon_size);
    }

  tip = v0->tooltip;

  if (tip != NULL)
    {
      gchar *markup;

      markup = NULL;

      if ((tip->title != NULL && *tip->title != '\0') &&
          (tip->text != NULL && *tip->text != '\0'))
        {
          markup = g_strdup_printf ("%s\n%s", tip->title, tip->text);
        }
      else if (tip->title != NULL && *tip->title != '\0')
        {
          markup = g_strdup (tip->title);
        }
      else if (tip->text != NULL && *tip->text != '\0')
        {
          markup = g_strdup (tip->text);
        }

      gtk_widget_set_tooltip_markup (GTK_WIDGET (v0), markup);
      g_free (markup);
    }
  else
    {
      gtk_widget_set_tooltip_markup (GTK_WIDGET (v0), NULL);
    }

  accessible = gtk_widget_get_accessible (GTK_WIDGET (v0));

  if (v0->title != NULL && *v0->title != '\0')
    atk_object_set_name (accessible, v0->title);
  else
    atk_object_set_name (accessible, v0->id);

  /* TODO: hide "Passive" items with a setting? */
  /*Special case mate-polkit*/
  if (g_strcmp0 (v0->status, "password-dialog") != 0){
    visible = g_strcmp0 (v0->status, "Passive") != 0;
    gtk_widget_set_visible (GTK_WIDGET (v0), visible);
    }
  else
  gtk_widget_set_visible (GTK_WIDGET (v0), TRUE);
}

static gboolean
update_cb (gpointer user_data)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (user_data);

  v0->update_id = 0;
  update (v0);

  return G_SOURCE_REMOVE;
}

static void
queue_update (SnItemV0 *v0)
{
  if (v0->update_id != 0)
    return;

  v0->update_id = g_timeout_add (10, update_cb, v0);
  g_source_set_name_by_id (v0->update_id, "[status-notifier] update_cb");
}

static cairo_surface_t *
surface_from_variant (GVariant *variant,
                      gint      width,
                      gint      height)
{
  cairo_format_t format;
  gint stride;
  guint32 *data;
  gint x;
  gint y;
  guchar *p;

  format = CAIRO_FORMAT_ARGB32;
  stride = cairo_format_stride_for_width (format, width);
  data = (guint32 *) g_variant_get_data (variant);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {
    gint i;

    for (i = 0; i < width * height; i++)
      data[i] = GUINT32_FROM_BE (data[i]);
  }
#endif

  p = (guchar *) data;
  /* premultiply alpha for Cairo */
  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          guchar alpha = p[x * 4 + 3];

          p[x * 4 + 0] = (p[x * 4 + 0] * alpha) / 255;
          p[x * 4 + 1] = (p[x * 4 + 1] * alpha) / 255;
          p[x * 4 + 2] = (p[x * 4 + 2] * alpha) / 255;
        }

      p += stride;
    }

  return cairo_image_surface_create_for_data ((guchar *) data, format,
                                              width, height, stride);
}

static cairo_surface_t *
icon_surface_new (GVariant *variant,
                  gint      width,
                  gint      height)
{
  cairo_surface_t *surface;
  cairo_surface_t *tmp;
  cairo_t *cr;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    return NULL;

  tmp = surface_from_variant (variant, width, height);
  if (cairo_surface_status (tmp) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (surface);
      return NULL;
    }

  cr = cairo_create (surface);
  if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (surface);
      cairo_surface_destroy (tmp);
      return NULL;
    }

  cairo_set_source_surface (cr, tmp, 0, 0);
  cairo_paint (cr);

  cairo_surface_destroy (tmp);
  cairo_destroy (cr);

  return surface;
}

static SnIconPixmap **
icon_pixmap_new (GVariant *variant)
{
  GPtrArray *array;
  GVariantIter iter;
  gint width;
  gint height;
  GVariant *value;

  if (variant == NULL || g_variant_iter_init (&iter, variant) == 0)
    return NULL;

  array = g_ptr_array_new ();
  while (g_variant_iter_next (&iter, "(ii@ay)", &width, &height, &value))
    {
      cairo_surface_t *surface;

      if (width == 0 || height == 0)
        {
          g_variant_unref (value);
          continue;
        }

      surface = icon_surface_new (value, width, height);
      g_variant_unref (value);

      if (surface != NULL)
        {
          SnIconPixmap *pixmap;

          pixmap = g_new0 (SnIconPixmap, 1);

          pixmap->surface = surface;
          pixmap->width = width;
          pixmap->height = height;

          g_ptr_array_add (array, pixmap);
        }
    }

  g_ptr_array_add (array, NULL);
  return (SnIconPixmap **) g_ptr_array_free (array, FALSE);
}

static void
icon_pixmap_free (SnIconPixmap **data)
{
  gint i;

  if (data == NULL)
    return;

  for (i = 0; data[i] != NULL; i++)
    {
      cairo_surface_destroy (data[i]->surface);
      g_free (data[i]);
    }

  g_free (data);
}

static SnTooltip *
sn_tooltip_new (GVariant *variant)
{
  const gchar *icon_name;
  GVariant *icon_pixmap;
  const gchar *title;
  const gchar *text;
  SnTooltip *tooltip;

  if (variant == NULL)
    return NULL;

  if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("(sa(iiay)ss)")))
    {
      g_warning ("Type for 'ToolTip' property should be '(sa(iiay)ss)' "
                 "but got '%s'", g_variant_get_type_string (variant));

      return NULL;
    }

  g_variant_get (variant, "(&s@a(iiay)&s&s)",
                 &icon_name, &icon_pixmap,
                 &title, &text);

  tooltip = g_new0 (SnTooltip, 1);

  tooltip->icon_name = g_strdup (icon_name);
  tooltip->icon_pixmap = icon_pixmap_new (icon_pixmap);
  tooltip->title = g_strdup (title);
  tooltip->text = g_strdup (text);

  g_variant_unref (icon_pixmap);
  return tooltip;
}

static void
sn_tooltip_free (SnTooltip *tooltip)
{
  if (tooltip == NULL)
    return;

  g_free (tooltip->icon_name);
  icon_pixmap_free (tooltip->icon_pixmap);
  g_free (tooltip->title);
  g_free (tooltip->text);

  g_free (tooltip);
}

static GVariant *
get_property (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data,
              gboolean     *cancelled)
{
  GVariant *variant;
  GError *error;
  GVariant *property;

  error = NULL;
  variant = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                           res, &error);

  *cancelled = FALSE;
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      *cancelled = TRUE;
      g_error_free (error);
      return NULL;
    }

  if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS))
    {
      g_error_free (error);
      return NULL;
    }

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return NULL;
    }

  g_variant_get (variant, "(v)", &property);
  g_variant_unref (variant);

  return property;
}

static void
update_property (SnItemV0            *v0,
                 const gchar         *property,
                 GAsyncReadyCallback  callback)
{
  GDBusProxy *proxy;
  SnItem *item;

  proxy = G_DBUS_PROXY (v0->proxy);
  item = SN_ITEM (v0);

  g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
                          sn_item_get_bus_name (item),
                          sn_item_get_object_path (item),
                          "org.freedesktop.DBus.Properties", "Get",
                          g_variant_new ("(ss)", SN_ITEM_INTERFACE, property),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE, -1,
                          v0->cancellable, callback, v0);
}

static void
update_title (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->title, g_free);
  v0->title = g_variant_dup_string (variant, NULL);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
new_title_cb (SnItemV0 *v0)
{
  update_property (v0, "Title", update_title);
}

static void
update_icon_name (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->icon_name, g_free);
  v0->icon_name = g_variant_dup_string (variant, NULL);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
update_icon_pixmap (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->icon_pixmap, icon_pixmap_free);
  v0->icon_pixmap = icon_pixmap_new (variant);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
new_icon_cb (SnItemV0 *v0)
{
  update_property (v0, "IconName", update_icon_name);
  update_property (v0, "IconPixmap", update_icon_pixmap);
}

static void
update_overlay_icon_name (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->overlay_icon_name, g_free);
  v0->overlay_icon_name = g_variant_dup_string (variant, NULL);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
update_overlay_icon_pixmap (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->overlay_icon_pixmap, icon_pixmap_free);
  v0->overlay_icon_pixmap = icon_pixmap_new (variant);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
new_overlay_icon_cb (SnItemV0 *v0)
{
  update_property (v0, "OverlayIconName", update_overlay_icon_name);
  update_property (v0, "OverlayIconPixmap", update_overlay_icon_pixmap);
}

static void
update_attention_icon_name (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->attention_icon_name, g_free);
  v0->attention_icon_name = g_variant_dup_string (variant, NULL);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
update_attention_icon_pixmap (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->attention_icon_pixmap, icon_pixmap_free);
  v0->attention_icon_pixmap = icon_pixmap_new (variant);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
new_attention_icon_cb (SnItemV0 *v0)
{
  update_property (v0, "AttentionIconName", update_attention_icon_name);
  update_property (v0, "AttentionIconPixmap", update_attention_icon_pixmap);
}

static void
update_tooltip (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *variant;
  gboolean cancelled;

  variant = get_property (source_object, res, user_data, &cancelled);
  if (cancelled)
    return;

  v0 = SN_ITEM_V0 (user_data);

  g_clear_pointer (&v0->tooltip, sn_tooltip_free);
  v0->tooltip = sn_tooltip_new (variant);
  g_clear_pointer (&variant, g_variant_unref);

  queue_update (v0);
}

static void
new_tooltip_cb (SnItemV0 *v0)
{
  update_property (v0, "ToolTip", update_tooltip);
}

static void
new_status_cb (SnItemV0 *v0,
               GVariant *parameters)
{
  GVariant *variant;

  variant = g_variant_get_child_value (parameters, 0);

  g_free (v0->status);
  v0->status = g_variant_dup_string (variant, NULL);
  g_variant_unref (variant);

  queue_update (v0);
}

static void
new_icon_theme_path_cb (SnItemV0 *v0,
                        GVariant *parameters)
{
  GVariant *variant;

  variant = g_variant_get_child_value (parameters, 0);

  g_free (v0->icon_theme_path);
  v0->icon_theme_path = g_variant_dup_string (variant, NULL);
  g_variant_unref (variant);

  if (v0->icon_theme_path != NULL)
    {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_default ();

      gtk_icon_theme_append_search_path (icon_theme, v0->icon_theme_path);
    }

  queue_update (v0);
}

static void
g_properties_changed_cb (GDBusProxy *proxy,
                         GVariant   *changed_properties,
                         GStrv       invalidated_properties,
                         SnItemV0   *v0)
{
  gchar *debug;

  debug = g_variant_print (changed_properties, FALSE);
  g_debug ("g_properties_changed_cb: %s", debug);
  g_free (debug);
}

static void
g_signal_cb (GDBusProxy *proxy,
             gchar      *sender_name,
             gchar      *signal_name,
             GVariant   *parameters,
             SnItemV0   *v0)
{
  if (g_strcmp0 (signal_name, "NewTitle") == 0)
    new_title_cb (v0);
  else if (g_strcmp0 (signal_name, "NewIcon") == 0)
    new_icon_cb (v0);
  else if (g_strcmp0 (signal_name, "NewOverlayIcon") == 0)
    new_overlay_icon_cb (v0);
  else if (g_strcmp0 (signal_name, "NewAttentionIcon") == 0)
    new_attention_icon_cb (v0);
  else if (g_strcmp0 (signal_name, "NewToolTip") == 0)
    new_tooltip_cb (v0);
  else if (g_strcmp0 (signal_name, "NewStatus") == 0)
    new_status_cb (v0, parameters);
  else if (g_strcmp0 (signal_name, "NewIconThemePath") == 0)
    new_icon_theme_path_cb (v0, parameters);
  else
    g_debug ("signal '%s' not handled!", signal_name);
}

static void
get_all_cb (GObject      *source_object,
            GAsyncResult *res,
            gpointer      user_data)
{
  SnItemV0 *v0;
  GVariant *properties;
  GError *error;
  GVariantIter *iter;
  gchar *key;
  GVariant *value;

  error = NULL;
  properties = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                              res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  v0 = SN_ITEM_V0 (user_data);

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  g_variant_get (properties, "(a{sv})", &iter);
  while (g_variant_iter_next (iter, "{sv}", &key, &value))
    {
      if (g_strcmp0 (key, "Category") == 0)
        v0->category = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "Id") == 0)
        v0->id = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "Title") == 0)
        v0->title = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "Status") == 0)
        v0->status = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "WindowId") == 0)
        v0->window_id = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "IconName") == 0)
        v0->icon_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "IconPixmap") == 0)
        v0->icon_pixmap = icon_pixmap_new (value);
      else if (g_strcmp0 (key, "OverlayIconName") == 0)
        v0->overlay_icon_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "OverlayIconPixmap") == 0)
        v0->overlay_icon_pixmap = icon_pixmap_new (value);
      else if (g_strcmp0 (key, "AttentionIconName") == 0)
        v0->attention_icon_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "AttentionIconPixmap") == 0)
        v0->attention_icon_pixmap = icon_pixmap_new (value);
      else if (g_strcmp0 (key, "AttentionMovieName") == 0)
        v0->attention_movie_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "ToolTip") == 0)
        v0->tooltip = sn_tooltip_new (value);
      else if (g_strcmp0 (key, "IconThemePath") == 0)
        v0->icon_theme_path = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "Menu") == 0)
        v0->menu = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "ItemIsMenu") == 0)
        v0->item_is_menu = g_variant_get_boolean (value);
      else
        g_debug ("property '%s' not handled!", key);

      g_variant_unref (value);
      g_free (key);
    }

  g_variant_iter_free (iter);
  g_variant_unref (properties);

  if (v0->id == NULL || v0->category == NULL || v0->status == NULL)
    {
      SnItem *item;
      const gchar *bus_name;
      const gchar *object_path;

      item = SN_ITEM (v0);
      bus_name = sn_item_get_bus_name (item);
      object_path = sn_item_get_object_path (item);

      g_warning ("Invalid Status Notifier Item (%s, %s)",
                 bus_name, object_path);

      return;
    }

  if (v0->icon_theme_path != NULL)
    {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_default ();

      gtk_icon_theme_append_search_path (icon_theme, v0->icon_theme_path);
    }

  g_signal_connect (v0->proxy, "g-properties-changed",
                    G_CALLBACK (g_properties_changed_cb), v0);

  g_signal_connect (v0->proxy, "g-signal",
                    G_CALLBACK (g_signal_cb), v0);

  update (v0);
  sn_item_emit_ready (SN_ITEM (v0));
}

static void
proxy_ready_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  SnItemV0 *v0;
  SnItemV0Gen *proxy;
  GError *error;

  error = NULL;
  proxy = sn_item_v0_gen_proxy_new_for_bus_finish (res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  v0 = SN_ITEM_V0 (user_data);
  v0->proxy = proxy;

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  g_dbus_connection_call (g_dbus_proxy_get_connection (G_DBUS_PROXY (proxy)),
                          sn_item_get_bus_name (SN_ITEM (v0)),
                          sn_item_get_object_path (SN_ITEM (v0)),
                          "org.freedesktop.DBus.Properties", "GetAll",
                          g_variant_new ("(s)", SN_ITEM_INTERFACE),
                          G_VARIANT_TYPE ("(a{sv})"),
                          G_DBUS_CALL_FLAGS_NONE, -1,
                          v0->cancellable, get_all_cb, v0);
}

static void
sn_item_v0_constructed (GObject *object)
{
  SnItemV0 *v0;
  SnItem *item;

  v0 = SN_ITEM_V0 (object);
  item = SN_ITEM (v0);

  G_OBJECT_CLASS (sn_item_v0_parent_class)->constructed (object);

  v0->cancellable = g_cancellable_new ();
  sn_item_v0_gen_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                    G_DBUS_PROXY_FLAGS_NONE,
                                    sn_item_get_bus_name (item),
                                    sn_item_get_object_path (item),
                                    v0->cancellable,
                                    proxy_ready_cb, v0);
}

static void
sn_item_v0_dispose (GObject *object)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (object);

  g_cancellable_cancel (v0->cancellable);
  g_clear_object (&v0->cancellable);
  g_clear_object (&v0->proxy);

  if (v0->update_id != 0)
    {
      g_source_remove (v0->update_id);
      v0->update_id = 0;
    }

  G_OBJECT_CLASS (sn_item_v0_parent_class)->dispose (object);
}

static void
sn_item_v0_finalize (GObject *object)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (object);

  g_clear_pointer (&v0->id, g_free);
  g_clear_pointer (&v0->category, g_free);
  g_clear_pointer (&v0->status, g_free);

  g_clear_pointer (&v0->title, g_free);
  g_clear_pointer (&v0->icon_name, g_free);
  g_clear_pointer (&v0->icon_pixmap, icon_pixmap_free);
  g_clear_pointer (&v0->overlay_icon_name, g_free);
  g_clear_pointer (&v0->overlay_icon_pixmap, icon_pixmap_free);
  g_clear_pointer (&v0->attention_icon_name, g_free);
  g_clear_pointer (&v0->attention_icon_pixmap, icon_pixmap_free);
  g_clear_pointer (&v0->attention_movie_name, g_free);
  g_clear_pointer (&v0->tooltip, sn_tooltip_free);
  g_clear_pointer (&v0->icon_theme_path, g_free);
  g_clear_pointer (&v0->menu, g_free);

  G_OBJECT_CLASS (sn_item_v0_parent_class)->finalize (object);
}

static const gchar *
sn_item_v0_get_id (SnItem *item)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  return v0->id;
}

static const gchar *
sn_item_v0_get_category (SnItem *item)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  return v0->category;
}

static const gchar *
sn_item_v0_get_menu (SnItem *item)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  return v0->menu;
}

static void
context_menu_cb (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (user_data);

  sn_item_v0_gen_call_context_menu_finish (v0->proxy, res, NULL);
}

static void
sn_item_v0_context_menu (SnItem *item,
                         gint    x,
                         gint    y)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  sn_item_v0_gen_call_context_menu (v0->proxy, x, y, NULL,
                                    context_menu_cb, v0);
}

static void
activate_cb (GObject      *source_object,
             GAsyncResult *res,
             gpointer      user_data)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (user_data);

  sn_item_v0_gen_call_activate_finish (v0->proxy, res, NULL);
}

static void
sn_item_v0_activate (SnItem *item,
                     gint    x,
                     gint    y)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  sn_item_v0_gen_call_activate (v0->proxy, x, y, NULL,
                                activate_cb, v0);
}

static void
secondary_activate_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (user_data);

  sn_item_v0_gen_call_secondary_activate_finish (v0->proxy, res, NULL);
}

static void
sn_item_v0_secondary_activate (SnItem *item,
                               gint    x,
                               gint    y)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (item);

  sn_item_v0_gen_call_secondary_activate (v0->proxy, x, y, NULL,
                                          secondary_activate_cb, v0);
}

static void
scroll_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (user_data);

  sn_item_v0_gen_call_scroll_finish (v0->proxy, res, NULL);
}

static void
sn_item_v0_scroll (SnItem            *item,
                   gint               delta,
                   SnItemOrientation  orientation)
{
  SnItemV0 *v0;
  const gchar *tmp;

  v0 = SN_ITEM_V0 (item);

  switch (orientation)
    {
      case SN_ITEM_ORIENTATION_VERTICAL:
        tmp = "Vertical";
        break;

      case SN_ITEM_ORIENTATION_HORIZONTAL:
      default:
        tmp = "Horizontal";
        break;
    }

  sn_item_v0_gen_call_scroll (v0->proxy, delta, tmp, NULL, scroll_cb, v0);
}

static void
sn_item_v0_size_allocate (GtkWidget      *widget,
                          GtkAllocation  *allocation)
{
  SnItemV0 *v0 = SN_ITEM_V0 (widget);

  GTK_WIDGET_CLASS (sn_item_v0_parent_class)->size_allocate (widget, allocation);

  /* FIXME: this leads to grow-only size, unless there's underallocation.
   *        not a problem in the panel, but one in the test app. */
  if (v0->icon_size <= 0)
    {
      gint prev_effective_icon_size = v0->effective_icon_size;

      if (gtk_orientable_get_orientation (GTK_ORIENTABLE (v0)) == GTK_ORIENTATION_HORIZONTAL)
        v0->effective_icon_size = allocation->height;
      else
        v0->effective_icon_size = allocation->width;

      if (v0->effective_icon_size != prev_effective_icon_size)
        queue_update (SN_ITEM_V0 (widget));
    }
}

static void
sn_item_v0_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (object);

  switch (property_id)
    {
      case PROP_ICON_SIZE:
        g_value_set_uint (value, v0->icon_size);
        break;

      case PROP_ICON_PADDING:
        g_value_set_int (value, sn_item_v0_get_icon_padding (v0));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
sn_item_v0_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  SnItemV0 *v0;

  v0 = SN_ITEM_V0 (object);

  switch (property_id)
    {
      case PROP_ICON_SIZE:
        sn_item_v0_set_icon_size (v0, g_value_get_int (value));
        break;

      case PROP_ICON_PADDING:
        sn_item_v0_set_icon_padding (v0, g_value_get_int (value));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
install_properties (GObjectClass *object_class)
{
  properties[PROP_ICON_SIZE] =
    g_param_spec_int ("icon-size", "Icon size", "Icon size", 0, G_MAXINT, 16,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ICON_PADDING] =
    g_param_spec_int ("icon-padding", "Icon padding", "Icon padding", 0,
                      G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
sn_item_v0_class_init (SnItemV0Class *v0_class)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  SnItemClass *item_class;

  object_class = G_OBJECT_CLASS (v0_class);
  widget_class = GTK_WIDGET_CLASS (v0_class);
  item_class = SN_ITEM_CLASS (v0_class);

  object_class->constructed = sn_item_v0_constructed;
  object_class->dispose = sn_item_v0_dispose;
  object_class->finalize = sn_item_v0_finalize;
  object_class->get_property = sn_item_v0_get_property;
  object_class->set_property = sn_item_v0_set_property;

  item_class->get_id = sn_item_v0_get_id;
  item_class->get_category = sn_item_v0_get_category;
  item_class->get_menu = sn_item_v0_get_menu;

  item_class->context_menu = sn_item_v0_context_menu;
  item_class->activate = sn_item_v0_activate;
  item_class->secondary_activate = sn_item_v0_secondary_activate;
  item_class->scroll = sn_item_v0_scroll;

  widget_class->size_allocate = sn_item_v0_size_allocate;

  gtk_widget_class_set_css_name (widget_class, "sn-item");

  install_properties (object_class);
}

static void
sn_item_v0_init (SnItemV0 *v0)
{
  v0->icon_size = 16;
  v0->effective_icon_size = 0;
  v0->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (v0), v0->image);
  gtk_widget_show (v0->image);
}

SnItem *
sn_item_v0_new (const gchar *bus_name,
                const gchar *object_path)
{
  return g_object_new (SN_TYPE_ITEM_V0,
                       "bus-name", bus_name,
                       "object-path", object_path,
                       NULL);
}

gint
sn_item_v0_get_icon_padding (SnItemV0 *v0)
{
  GtkOrientation orientation;
  gint a, b;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (v0));

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      a = gtk_widget_get_margin_start (v0->image);
      b = gtk_widget_get_margin_end (v0->image);
    }
  else
    {
      a = gtk_widget_get_margin_top (v0->image);
      b = gtk_widget_get_margin_bottom (v0->image);
    }

  return (a + b) / 2;
}

void
sn_item_v0_set_icon_padding (SnItemV0 *v0,
                             gint padding)
{
  GtkOrientation orientation;
  gint padding_x = 0;
  gint padding_y = 0;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (v0));

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    padding_x = padding;
  else
    padding_y = padding;

  gtk_widget_set_margin_start (v0->image, padding_x);
  gtk_widget_set_margin_end (v0->image, padding_x);
  gtk_widget_set_margin_top (v0->image, padding_y);
  gtk_widget_set_margin_bottom (v0->image, padding_y);
}

gint
sn_item_v0_get_icon_size (SnItemV0 *v0)
{
  return v0->icon_size;
}

void
sn_item_v0_set_icon_size (SnItemV0 *v0,
                          gint size)
{
  if (v0->icon_size != size)
    {
      v0->icon_size = size;
      g_object_notify_by_pspec (G_OBJECT (v0), properties[PROP_ICON_SIZE]);

      if (v0->id != NULL)
        queue_update (v0);
    }
}
