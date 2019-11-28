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

#include "sn-dbus-menu-item.h"

static GdkPixbuf *
pxibuf_new (GVariant *variant)
{
  gsize length;
  const guchar *data;
  GInputStream *stream;
  GdkPixbuf *pixbuf;
  GError *error;

  data = g_variant_get_fixed_array (variant, &length, sizeof (guchar));

  if (length == 0)
    return NULL;

  stream = g_memory_input_stream_new_from_data (data, length, NULL);

  if (stream == NULL)
    return NULL;

  error = NULL;
  pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, &error);
  g_object_unref (stream);

  if (error != NULL)
    {
      g_warning ("Unable to build GdkPixbuf from icon data: %s", error->message);
      g_error_free (error);
    }

  return pixbuf;
}

static SnShortcut *
sn_shortcut_new (guint           key,
                 GdkModifierType mask)
{
  SnShortcut *shortcut;

  shortcut = g_new0 (SnShortcut, 1);

  shortcut->key = key;
  shortcut->mask = mask;

  return shortcut;
}

static SnShortcut **
sn_shortcuts_new (GVariant *variant)
{
  GPtrArray *array;
  GVariantIter shortcuts;
  GVariantIter *shortcut;

  if (variant == NULL || g_variant_iter_init (&shortcuts, variant) == 0)
    return NULL;

  array = g_ptr_array_new ();
  while (g_variant_iter_next (&shortcuts, "as", &shortcut))
    {
      guint key;
      GdkModifierType mask;
      const gchar *string;

      key = 0;
      mask = 0;

      while (g_variant_iter_next (shortcut, "&s", &string))
        {
          if (g_strcmp0 (string, "Control") == 0)
            mask |= GDK_CONTROL_MASK;
          else if (g_strcmp0 (string, "Alt") == 0)
            mask |= GDK_MOD1_MASK;
          else if (g_strcmp0 (string, "Shift") == 0)
            mask |= GDK_SHIFT_MASK;
          else if (g_strcmp0 (string, "Super") == 0)
            mask |= GDK_SUPER_MASK;
          else
            gtk_accelerator_parse (string, &key, NULL);
        }

      g_ptr_array_add (array,sn_shortcut_new (key, mask));
      g_variant_iter_free (shortcut);
    }

  g_ptr_array_add (array, NULL);
  return (SnShortcut **) g_ptr_array_free (array, FALSE);
}

static void
sn_shortcuts_free (SnShortcut **shortcuts)
{
  guint i;

  if (shortcuts == NULL)
    return;

  for (i = 0; shortcuts[i] != NULL; i++)
    g_free (shortcuts[i]);

  g_free (shortcuts);
}

SnDBusMenuItem *
sn_dbus_menu_item_new (GVariant *props)
{
  SnDBusMenuItem *item;
  GVariantIter iter;
  const gchar *prop;
  GVariant *value;

  item = g_new0 (SnDBusMenuItem, 1);

  item->enabled = TRUE;
  item->toggle_state = -1;
  item->visible = TRUE;

  g_variant_iter_init (&iter, props);
  while (g_variant_iter_next (&iter, "{&sv}", &prop, &value))
    {
      if (g_strcmp0 (prop, "accessible-desc") == 0)
        item->accessible_desc = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "children-display") == 0)
        item->children_display = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "disposition") == 0)
        item->disposition = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "enabled") == 0)
        item->enabled = g_variant_get_boolean (value);
      else if (g_strcmp0 (prop, "icon-name") == 0)
        item->icon_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "icon-data") == 0)
        item->icon_data = pxibuf_new (value);
      else if (g_strcmp0 (prop, "label") == 0)
        item->label = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "shortcut") == 0)
        item->shortcuts = sn_shortcuts_new (value);
      else if (g_strcmp0 (prop, "toggle-type") == 0)
        item->toggle_type = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "toggle-state") == 0)
        item->toggle_state = g_variant_get_int32 (value);
      else if (g_strcmp0 (prop, "type") == 0)
        item->type = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (prop, "visible") == 0)
        item->visible = g_variant_get_boolean (value);
      else
        g_debug ("unknown property '%s'", prop);

      g_variant_unref (value);
    }

  if (g_strcmp0 (item->type, "separator") == 0)
    {
      item->item = gtk_separator_menu_item_new ();
    }
  else
    {
      if (g_strcmp0 (item->toggle_type, "checkmark") == 0)
        {
          item->item = gtk_check_menu_item_new ();
        }
      else if (g_strcmp0 (item->toggle_type, "radio") == 0)
        {
          item->item = gtk_check_menu_item_new ();
          gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM(item->item),TRUE);
          AtkObject *atk_obj;
          atk_obj = gtk_widget_get_accessible (item->item);
          atk_object_set_role (atk_obj,ATK_ROLE_RADIO_MENU_ITEM);
        }
      else
        {
          GtkWidget *image = NULL;

          if (item->icon_name)
            {
              image = gtk_image_new_from_icon_name (item->icon_name,
                                                    GTK_ICON_SIZE_MENU);
            }
          else if (item->icon_data)
            {
              cairo_surface_t *surface;
              surface = gdk_cairo_surface_create_from_pixbuf (item->icon_data, 0, NULL);
              image = gtk_image_new_from_surface (surface);
              cairo_surface_destroy (surface);
            }

          item->item = gtk_image_menu_item_new ();
          gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item->item),
                                         image);
        }

      if (g_strcmp0 (item->children_display, "submenu") == 0)
        {
          GtkWidget *submenu;

          submenu = gtk_menu_new ();
          gtk_menu_item_set_submenu (GTK_MENU_ITEM (item->item), submenu);

          item->submenu = GTK_MENU (submenu);
          g_object_ref_sink (item->submenu);
        }

      gtk_menu_item_set_use_underline (GTK_MENU_ITEM (item->item), TRUE);
      gtk_menu_item_set_label (GTK_MENU_ITEM (item->item), item->label);

      if (item->shortcuts)
        {
          guint i;

          for (i = 0; item->shortcuts[i] != NULL; i++)
            {
            }
        }

      if (item->toggle_state != -1 && GTK_IS_CHECK_MENU_ITEM (item->item))
        {
          GtkCheckMenuItem *check;

          check = GTK_CHECK_MENU_ITEM (item->item);

          if (item->toggle_state == 1)
            gtk_check_menu_item_set_active (check, TRUE);
          else if (item->toggle_state == 0)
            gtk_check_menu_item_set_active (check, FALSE);
        }
    }

  gtk_widget_set_sensitive (item->item, item->enabled);
  gtk_widget_set_visible (item->item, item->visible);

  g_object_ref_sink (item->item);
  return item;
}

void
sn_dubs_menu_item_free (gpointer data)
{
  SnDBusMenuItem *item;

  item = (SnDBusMenuItem *) data;
  if (item == NULL)
    return;

  g_clear_pointer (&item->accessible_desc, g_free);
  g_clear_pointer (&item->children_display, g_free);
  g_clear_pointer (&item->disposition, g_free);
  g_clear_pointer (&item->icon_name, g_free);
  g_clear_object (&item->icon_data);
  g_clear_pointer (&item->label, g_free);
  g_clear_pointer (&item->shortcuts, sn_shortcuts_free);
  g_clear_pointer (&item->toggle_type, g_free);
  g_clear_pointer (&item->type, g_free);

  gtk_widget_destroy (item->item);
  g_clear_object (&item->item);
  g_clear_object (&item->submenu);

  g_free (item);
}

void
sn_dbus_menu_item_update_props (SnDBusMenuItem *item,
                                GVariant       *props)
{
  GVariantIter iter;
  const gchar *prop;
  GVariant *value;

  g_variant_iter_init (&iter, props);
  while (g_variant_iter_next (&iter, "{&sv}", &prop, &value))
    {
      if (g_strcmp0 (prop, "accessible-desc") == 0)
        {
          g_free (item->accessible_desc);
          item->accessible_desc = g_variant_dup_string (value, NULL);
        }
      else if (g_strcmp0 (prop, "children-display") == 0)
        {
          g_free (item->children_display);
          item->children_display = g_variant_dup_string (value, NULL);
        }
      else if (g_strcmp0 (prop, "disposition") == 0)
        {
          g_free (item->disposition);
          item->disposition = g_variant_dup_string (value, NULL);
        }
      else if (g_strcmp0 (prop, "enabled") == 0)
        {
          item->enabled = g_variant_get_boolean (value);
          gtk_widget_set_sensitive (item->item, item->enabled);
        }
      else if (g_strcmp0 (prop, "icon-name") == 0)
        {
          GtkWidget *image;

          g_free (item->icon_name);
          item->icon_name = g_variant_dup_string (value, NULL);

          if (item->icon_name)
            {
              image = gtk_image_new_from_icon_name (item->icon_name,
                                                    GTK_ICON_SIZE_MENU);
            }
          else
            {
              image = NULL;
            }

          gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item->item),
                                         image);
        }
      else if (g_strcmp0 (prop, "icon-data") == 0)
        {
          GtkWidget *image;

          g_clear_object (&item->icon_data);
          item->icon_data = pxibuf_new (value);

          if (item->icon_data)
            {
              cairo_surface_t *surface;
              surface = gdk_cairo_surface_create_from_pixbuf (item->icon_data, 0, NULL);
              image = gtk_image_new_from_surface (surface);
              cairo_surface_destroy (surface);
            }
          else
            {
              image = NULL;
            }

          gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item->item),
                                         image);
        }
      else if (g_strcmp0 (prop, "label") == 0)
        {
          g_free (item->label);
          item->label = g_variant_dup_string (value, NULL);

          if (!GTK_IS_SEPARATOR_MENU_ITEM (item->item))
            gtk_menu_item_set_label (GTK_MENU_ITEM (item->item), item->label);
        }
      else if (g_strcmp0 (prop, "shortcut") == 0)
        {
          sn_shortcuts_free (item->shortcuts);
          item->shortcuts = sn_shortcuts_new (value);
        }
      else if (g_strcmp0 (prop, "toggle-type") == 0)
        {
          g_free (item->toggle_type);
          item->toggle_type = g_variant_dup_string (value, NULL);
        }
      else if (g_strcmp0 (prop, "toggle-state") == 0)
        {
          item->toggle_state = g_variant_get_int32 (value);

          if (item->toggle_state != -1 && GTK_IS_CHECK_MENU_ITEM (item->item))
            {
              GtkCheckMenuItem *check;

              check = GTK_CHECK_MENU_ITEM (item->item);

              g_signal_handler_block (item->item, item->activate_id);

              if (item->toggle_state == 1)
                gtk_check_menu_item_set_active (check, TRUE);
              else if (item->toggle_state == 0)
                gtk_check_menu_item_set_active (check, FALSE);

              g_signal_handler_unblock (item->item, item->activate_id);
            }
        }
      else if (g_strcmp0 (prop, "type") == 0)
        {
          g_free (item->type);
          item->type = g_variant_dup_string (value, NULL);
        }
      else if (g_strcmp0 (prop, "visible") == 0)
        {
          item->visible = g_variant_get_boolean (value);
          gtk_widget_set_visible (item->item, item->visible);
        }
      else
        {
          g_debug ("updating unknown property - '%s'", prop);
        }

      g_variant_unref (value);
    }
}

void
sn_dbus_menu_item_remove_props (SnDBusMenuItem *item,
                                GVariant       *props)
{
  GVariantIter iter;
  const gchar *prop;

  g_variant_iter_init (&iter, props);
  while (g_variant_iter_next (&iter, "&s", &prop))
    {
      if (g_strcmp0 (prop, "accessible-desc") == 0)
        {
          g_clear_pointer (&item->accessible_desc, g_free);
        }
      else if (g_strcmp0 (prop, "children-display") == 0)
        {
          g_clear_pointer (&item->children_display, g_free);
        }
      else if (g_strcmp0 (prop, "disposition") == 0)
        {
          g_clear_pointer (&item->disposition, g_free);
        }
      else if (g_strcmp0 (prop, "enabled") == 0)
        {
          item->enabled = TRUE;
          gtk_widget_set_sensitive (item->item, item->enabled);
        }
      else if (g_strcmp0 (prop, "icon-name") == 0)
        {
          g_clear_pointer (&item->icon_name, g_free);
          if (GTK_IS_IMAGE_MENU_ITEM (item->item))
            {
              gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item->item),
                                             NULL);
            }
        }
      else if (g_strcmp0 (prop, "icon-data") == 0)
        {
          g_clear_object (&item->icon_data);
          if (GTK_IS_IMAGE_MENU_ITEM (item->item))
            {
              gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item->item),
                                             NULL);
            }
        }
      else if (g_strcmp0 (prop, "label") == 0)
        {
          g_clear_pointer (&item->label, g_free);
          if (!GTK_IS_SEPARATOR_MENU_ITEM (item->item))
            gtk_menu_item_set_label (GTK_MENU_ITEM (item->item), item->label);
        }
      else if (g_strcmp0 (prop, "shortcut") == 0)
        {
          g_clear_pointer (&item->shortcuts, sn_shortcuts_free);
        }
      else if (g_strcmp0 (prop, "toggle-type") == 0)
        {
          g_clear_pointer (&item->toggle_type, g_free);
        }
      else if (g_strcmp0 (prop, "toggle-state") == 0)
        {
          item->toggle_state = -1;
        }
      else if (g_strcmp0 (prop, "type") == 0)
        {
          g_clear_pointer (&item->type, g_free);
        }
      else if (g_strcmp0 (prop, "visible") == 0)
        {
          item->visible = TRUE;
          gtk_widget_set_visible (item->item, item->visible);
        }
      else
        {
          g_debug ("removing unknown property - '%s'", prop);
        }
    }
}
