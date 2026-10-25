/* System tray main() */

/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2011 Perberos
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <config.h>

#include <string.h>

#include <mate-panel-applet.h>
#include <mate-panel-applet-gsettings.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "main.h"
#include "na-grid.h"

#ifdef PROVIDE_WATCHER_SERVICE
# include "libstatus-notifier-watcher/gf-status-notifier-watcher.h"
#endif

#define NOTIFICATION_AREA_ICON "mate-panel-notification-area"

typedef struct
{
  GtkWidget *preferences_dialog;
  GtkWidget *min_icon_size_spin;
  GtkWidget *autohide_spin;
  GtkWidget *icons_grid;
} NAPreferencesDialog;

struct _NaTrayAppletPrivate
{
  GtkWidget               *grid;

  NAPreferencesDialog     *dialog;
  GtkBuilder              *builder;

  GSettings               *settings;
  gint                     min_icon_size;
  guint                    overflow_autohide_ms;

#ifdef PROVIDE_WATCHER_SERVICE
  GfStatusNotifierWatcher *sn_watcher;
#endif
};

G_DEFINE_TYPE_WITH_PRIVATE (NaTrayApplet, na_tray_applet, PANEL_TYPE_APPLET)

static void rebuild_icons_list                     (NaTrayApplet *applet);
static void na_preferences_dialog_icon_state_changed (GtkComboBox  *combo,
                                                      gpointer      applet);

static void (*parent_class_realize) (GtkWidget *widget);
static void (*parent_class_style_updated) (GtkWidget *widget);
static void (*parent_class_change_background)(MatePanelApplet* panel_applet, MatePanelAppletBackgroundType type, GdkRGBA* color, cairo_pattern_t* pattern);
static void (*parent_class_change_orient)(MatePanelApplet       *panel_applet, MatePanelAppletOrient  orient);

#ifdef PROVIDE_WATCHER_SERVICE
/* Quite dirty way of providing the org.kde.StatusNotifierWatcher service
 * ourselves, in case the session doesn't already */

static GfStatusNotifierWatcher *sn_watcher_service = NULL;

static GfStatusNotifierWatcher *
sn_watcher_service_ref (void)
{
  GSettings *settings;
  settings = g_settings_new ("org.mate.panel");

  if (g_settings_get_boolean (settings, "enable-sni-support") == TRUE)
    {
      if (sn_watcher_service != NULL)
        g_object_ref (sn_watcher_service);
      else
        {
          sn_watcher_service = gf_status_notifier_watcher_new ();
          g_object_add_weak_pointer ((GObject *) sn_watcher_service,
                                     (gpointer *) &sn_watcher_service);
        }
    }

  g_object_unref (settings);
  return sn_watcher_service;
}
#endif

static GtkOrientation
get_gtk_orientation_from_applet_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_LEFT:
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_ORIENTATION_VERTICAL;
    case MATE_PANEL_APPLET_ORIENT_UP:
    case MATE_PANEL_APPLET_ORIENT_DOWN:
    default:
      return GTK_ORIENTATION_HORIZONTAL;
    }

  g_assert_not_reached ();

  return GTK_ORIENTATION_HORIZONTAL;
}

/* Which way the overflow drawer opens.
 *
 * The applet's orient already names it: the panel maps its own edge through
 * get_mate_panel_applet_orient() (PANEL_ORIENTATION_BOTTOM -> ORIENT_UP), and
 * libmate-panel-applet anchors its own popup menu the same way (ORIENT_UP
 * hangs the menu above the widget). So the value is the direction the drawer
 * grows in, on both backends - unlike any guess from the screen geometry,
 * which cannot work on Wayland where the panel surface origin answers
 * surface-relative. */
static NaOpenDirection
get_open_direction_from_applet_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_UP:    return NA_OPEN_UP;
    case MATE_PANEL_APPLET_ORIENT_DOWN:  return NA_OPEN_DOWN;
    case MATE_PANEL_APPLET_ORIENT_LEFT:  return NA_OPEN_LEFT;
    case MATE_PANEL_APPLET_ORIENT_RIGHT: return NA_OPEN_RIGHT;
    }

  g_assert_not_reached ();

  return NA_OPEN_UP;
}

static void
gsettings_changed_min_icon_size (GSettings    *settings,
                                 gchar        *key,
                                 NaTrayApplet *applet)
{
  applet->priv->min_icon_size = g_settings_get_int (settings, key);

  if (applet->priv->dialog)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (applet->priv->dialog->min_icon_size_spin),
                               applet->priv->min_icon_size);

  na_grid_set_min_icon_size (NA_GRID (applet->priv->grid), applet->priv->min_icon_size);
}

static void
gsettings_changed_overflow_autohide (GSettings    *settings,
                                     gchar        *key,
                                     NaTrayApplet *applet)
{
  applet->priv->overflow_autohide_ms = g_settings_get_int (settings, key);

  if (applet->priv->dialog != NULL && applet->priv->dialog->autohide_spin != NULL)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (applet->priv->dialog->autohide_spin),
                               applet->priv->overflow_autohide_ms);

  na_grid_set_overflow_autohide_ms (NA_GRID (applet->priv->grid),
                                    applet->priv->overflow_autohide_ms);
}

static void
gsettings_changed_icon_lists (GSettings    *settings,
                              gchar        *key,
                              NaTrayApplet *applet)
{
  g_auto(GStrv) ids = g_settings_get_strv (settings, key);

  if (g_strcmp0 (key, KEY_HIDDEN_ICONS) == 0)
    na_grid_set_hidden_icons (NA_GRID (applet->priv->grid), (const gchar *const *) ids);
  else
    na_grid_set_overflow_icons (NA_GRID (applet->priv->grid), (const gchar *const *) ids);
}

static void
setup_gsettings (NaTrayApplet *applet)
{
  applet->priv->settings = mate_panel_applet_settings_new (MATE_PANEL_APPLET (applet), NA_TRAY_SCHEMA);
  g_signal_connect_object (G_OBJECT(applet->priv->settings), "changed::" KEY_MIN_ICON_SIZE, G_CALLBACK (gsettings_changed_min_icon_size), applet, 0);
  g_signal_connect_object (G_OBJECT(applet->priv->settings), "changed::" KEY_HIDDEN_ICONS, G_CALLBACK (gsettings_changed_icon_lists), applet, 0);
  g_signal_connect_object (G_OBJECT(applet->priv->settings), "changed::" KEY_OVERFLOW_ICONS, G_CALLBACK (gsettings_changed_icon_lists), applet, 0);
  g_signal_connect_object (G_OBJECT(applet->priv->settings), "changed::" KEY_OVERFLOW_AUTOHIDE_MS, G_CALLBACK (gsettings_changed_overflow_autohide), applet, 0);
}

/* Push the current values of the overflow keys into the grid.  The "changed"
 * signals only fire on a change, so the initial state has to be applied by
 * hand, exactly like min-icon-size is in na_tray_applet_realize(). */
static void
apply_overflow_settings (NaTrayApplet *applet)
{
  g_auto(GStrv) hidden;
  g_auto(GStrv) overflow;

  hidden = g_settings_get_strv (applet->priv->settings, KEY_HIDDEN_ICONS);
  na_grid_set_hidden_icons (NA_GRID (applet->priv->grid),
                            (const gchar *const *) hidden);

  overflow = g_settings_get_strv (applet->priv->settings, KEY_OVERFLOW_ICONS);
  na_grid_set_overflow_icons (NA_GRID (applet->priv->grid),
                              (const gchar *const *) overflow);

  applet->priv->overflow_autohide_ms =
    g_settings_get_int (applet->priv->settings, KEY_OVERFLOW_AUTOHIDE_MS);
  na_grid_set_overflow_autohide_ms (NA_GRID (applet->priv->grid),
                                    applet->priv->overflow_autohide_ms);
}

static void
na_preferences_dialog_min_icon_size_changed (NaTrayApplet  *applet,
                                             GtkSpinButton *spin_button)
{
  applet->priv->min_icon_size = gtk_spin_button_get_value_as_int (spin_button);
  g_settings_set_int (applet->priv->settings, KEY_MIN_ICON_SIZE, applet->priv->min_icon_size);
}

static void
na_preferences_dialog_autohide_changed (NaTrayApplet  *applet,
                                        GtkSpinButton *spin_button)
{
  applet->priv->overflow_autohide_ms =
    (guint) gtk_spin_button_get_value_as_int (spin_button);
  g_settings_set_int (applet->priv->settings, KEY_OVERFLOW_AUTOHIDE_MS,
                      applet->priv->overflow_autohide_ms);
}

/* The id lists are the storage; a combo change rewrites whichever list the
 * icon belongs to.  States are: 0 = always visible, 1 = overflow, 2 = hidden.
 *
 * Connected with g_signal_connect(), NOT g_signal_connect_swapped(): the
 * "changed" signal carries no arguments, so a swapped connection would invoke
 * this two-parameter handler with the combo coming from an uninitialized
 * register.  That is undefined behaviour, and it segfaulted in
 * g_type_check_instance() on a live session (dmesg 2026-10-19 23:25:41). */
static void
na_preferences_dialog_icon_state_changed (GtkComboBox  *combo,
                                          gpointer      user_data)
{
  NaTrayApplet *applet = user_data;
  const gchar *id = g_object_get_data (G_OBJECT (combo), "na-icon-id");
  gint state = gtk_combo_box_get_active (combo);
  g_autofree gchar **hidden = NULL;
  g_autofree gchar **overflow = NULL;
  g_autoptr(GPtrArray) new_hidden = NULL;
  g_autoptr(GPtrArray) new_overflow = NULL;

  if (id == NULL)
    return;

  hidden = g_settings_get_strv (applet->priv->settings, KEY_HIDDEN_ICONS);
  overflow = g_settings_get_strv (applet->priv->settings, KEY_OVERFLOW_ICONS);

  new_hidden = g_ptr_array_new_with_free_func (g_free);
  new_overflow = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; hidden[i] != NULL; i++)
    if (g_strcmp0 (hidden[i], id) != 0)
      g_ptr_array_add (new_hidden, g_strdup (hidden[i]));

  for (guint i = 0; overflow[i] != NULL; i++)
    if (g_strcmp0 (overflow[i], id) != 0)
      g_ptr_array_add (new_overflow, g_strdup (overflow[i]));

  if (state == 1)
    g_ptr_array_add (new_overflow, g_strdup (id));
  else if (state == 2)
    g_ptr_array_add (new_hidden, g_strdup (id));

  g_ptr_array_add (new_hidden, NULL);
  g_ptr_array_add (new_overflow, NULL);

  /* One write per key, and the grid reacts via the "changed" signal. */
  g_settings_set_strv (applet->priv->settings, KEY_HIDDEN_ICONS,
                       (const gchar *const *) new_hidden->pdata);
  g_settings_set_strv (applet->priv->settings, KEY_OVERFLOW_ICONS,
                       (const gchar *const *) new_overflow->pdata);
}

static void
append_icon_row (NaTrayApplet *applet,
                 NaItem       *item,
                 gint          row)
{
  GtkWidget *grid = applet->priv->dialog->icons_grid;
  const gchar *id = na_item_get_id (item);
  NaItemState state = na_grid_get_item_state (NA_GRID (applet->priv->grid), item);
  GtkWidget *label;
  GtkWidget *combo;
  g_autofree gchar *display = NULL;

  /* Items without any identifier cannot be addressed by a setting at all. */
  if (id == NULL)
    return;

  /* Mark the legacy half so the row is recognisable: its icon leaves the
   * panel as a WINDOW, not as a widget, which is invisible to the user but
   * matters when something goes wrong. */
  if (na_grid_item_is_sni (NA_GRID (applet->priv->grid), item))
    display = g_strdup (id);
  else
    display = g_strdup_printf (_("%s (system tray)"), id);

  label = gtk_label_new (display);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_show (label);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Shown on the panel"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("In the overflow popup"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Hidden completely"));

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
                            state == NA_ITEM_STATE_OVERFLOW ? 1 :
                            state == NA_ITEM_STATE_HIDDEN   ? 2 : 0);
  g_object_set_data_full (G_OBJECT (combo), "na-icon-id",
                          g_strdup (id), g_free);
  g_signal_connect (combo, "changed",
                    G_CALLBACK (na_preferences_dialog_icon_state_changed),
                    applet);

  gtk_widget_show (combo);
  gtk_grid_attach (GTK_GRID (grid), combo, 1, row, 1, 1);
}

static void
destroy_row_cb (GtkWidget *widget,
                gpointer   user_data)
{
  (void) user_data;
  gtk_widget_destroy (widget);
}

static void
rebuild_icons_list (NaTrayApplet *applet)
{
  GtkWidget *grid;
  const GSList *items;
  gint row = 0;

  if (applet->priv->dialog == NULL || applet->priv->dialog->icons_grid == NULL)
    return;

  grid = applet->priv->dialog->icons_grid;

  /* Destroy the rows, not the grid itself. */
  gtk_container_foreach (GTK_CONTAINER (grid), destroy_row_cb, NULL);

  items = na_grid_get_items (NA_GRID (applet->priv->grid));
  for (const GSList *l = items; l != NULL; l = l->next)
    append_icon_row (applet, l->data, row++);

  gtk_widget_show_all (grid);
}

static gboolean
na_preferences_dialog_hide_event (GtkWidget    *widget,
                                  GdkEvent     *event,
                                  NaTrayApplet *applet)
{
  gtk_widget_hide (applet->priv->dialog->preferences_dialog);
  return TRUE;
}

static void
na_preferences_dialog_response (NaTrayApplet *applet,
                                int           response,
                                GtkWidget    *preferences_dialog)
{
  switch (response)
    {
    case GTK_RESPONSE_CLOSE:
      gtk_widget_hide (preferences_dialog);
      break;
    default:
      break;
    }
}

static void
ensure_prefs_window_is_created (NaTrayApplet *applet)
{
  if (applet->priv->dialog)
    return;

  applet->priv->dialog = g_new0 (NAPreferencesDialog, 1);

  applet->priv->dialog->preferences_dialog = GTK_WIDGET (gtk_builder_get_object (applet->priv->builder, "notification_area_preferences_dialog"));

  gtk_window_set_icon_name (GTK_WINDOW (applet->priv->dialog->preferences_dialog), NOTIFICATION_AREA_ICON);

  applet->priv->dialog->min_icon_size_spin = GTK_WIDGET (gtk_builder_get_object (applet->priv->builder, "min_icon_size_spin"));
  g_return_if_fail (applet->priv->dialog->min_icon_size_spin != NULL);

  applet->priv->dialog->icons_grid = GTK_WIDGET (gtk_builder_get_object (applet->priv->builder, "icons_grid"));
  g_return_if_fail (applet->priv->dialog->icons_grid != NULL);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (applet->priv->dialog->min_icon_size_spin), 7, 130);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (applet->priv->dialog->min_icon_size_spin), applet->priv->min_icon_size);

  g_signal_connect_swapped (applet->priv->dialog->min_icon_size_spin, "value-changed",
                            G_CALLBACK (na_preferences_dialog_min_icon_size_changed),
                            applet);

  applet->priv->dialog->autohide_spin = GTK_WIDGET (gtk_builder_get_object (applet->priv->builder, "autohide_spin"));
  g_return_if_fail (applet->priv->dialog->autohide_spin != NULL);

  /* 0 = never hide automatically; the upper bound of 60 s is generous enough to
   * debug a stuck drawer; 100 ms steps, because finer control is pointless. */
  gtk_spin_button_set_range (GTK_SPIN_BUTTON (applet->priv->dialog->autohide_spin), 0, 60000);
  gtk_spin_button_set_increments (GTK_SPIN_BUTTON (applet->priv->dialog->autohide_spin), 100, 500);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (applet->priv->dialog->autohide_spin),
                             applet->priv->overflow_autohide_ms);

  g_signal_connect_swapped (applet->priv->dialog->autohide_spin, "value-changed",
                            G_CALLBACK (na_preferences_dialog_autohide_changed),
                            applet);

  /* The list is only meaningful while the dialog is up; it is rebuilt in
   * properties_dialog() every time the dialog is opened. */

  g_signal_connect_swapped (applet->priv->dialog->preferences_dialog, "response",
                            G_CALLBACK (na_preferences_dialog_response), applet);

  g_signal_connect (applet->priv->dialog->preferences_dialog, "delete-event",
                    G_CALLBACK (na_preferences_dialog_hide_event), applet);
}

static void
properties_dialog (GtkAction    *action,
                   NaTrayApplet *applet)
{
  ensure_prefs_window_is_created (applet);

  /* Dismiss the drawer right away: the user came to change icon states, not to
   * look at the drawer, and moving an icon with the drawer visible is exactly
   * the case where the panel layout diverged from the grid layout. With the
   * default auto-hide the drawer would close by itself, but at
   * overflow-autohide-ms = 0 it would stay up. */
  na_grid_popdown_overflow (NA_GRID (applet->priv->grid));

  /* Icons come and go, so the list is rebuilt on every opening. */
  rebuild_icons_list (applet);

  gtk_window_set_screen (GTK_WINDOW (applet->priv->dialog->preferences_dialog),
                         gtk_widget_get_screen (GTK_WIDGET (applet)));
  gtk_window_present (GTK_WINDOW (applet->priv->dialog->preferences_dialog));
}

static void help_cb(GtkAction* action, NaTrayApplet* applet)
{
	GError* error = NULL;
	char* uri;
	#define NA_HELP_DOC "mate-user-guide"

	uri = g_strdup_printf("help:%s/%s", NA_HELP_DOC, "panels-notification-area");
	gtk_show_uri_on_window (NULL, uri, gtk_get_current_event_time (), &error);
	g_free(uri);

	if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
	{
		g_error_free(error);
	}
	else if(error)
	{
		GtkWidget* dialog;
		char* primary;

		primary = g_markup_printf_escaped (_("Could not display help document '%s'"), NA_HELP_DOC);
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", primary);

		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", error->message);

		g_error_free(error);
		g_free(primary);

		g_signal_connect(dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

		gtk_window_set_icon_name (GTK_WINDOW (dialog), NOTIFICATION_AREA_ICON);
		gtk_window_set_screen (GTK_WINDOW (dialog), gtk_widget_get_screen (GTK_WIDGET (applet)));
		/* we have no parent window */
		gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);
		gtk_window_set_title (GTK_WINDOW (dialog), _("Error displaying help document"));

		gtk_widget_show (dialog);
	}
}

static void about_cb(GtkAction* action, NaTrayApplet* applet)
{
	const gchar* authors[] = {
		"Havoc Pennington <hp@redhat.com>",
		"Anders Carlsson <andersca@gnu.org>",
		"Vincent Untz <vuntz@gnome.org>",
		"Alberts Muktupāvels",
		"Colomban Wendling <cwendling@hypra.fr>",
		"Fabien Broquard <braikar@gmail.com>",
		NULL
	};

	const char* documenters[] = {
		"Sun GNOME Documentation Team <gdocteam@sun.com>",
		NULL
	};

	gtk_show_about_dialog(NULL,
		"program-name", _("Notification Area"),
		"title", _("About Notification Area"),
		"authors", authors,
		/* "comments", _(comments), */
		"copyright", _("Copyright \xc2\xa9 2002 Red Hat, Inc.\n"
		               "Copyright \xc2\xa9 2003-2006 Vincent Untz\n"
		               "Copyright \xc2\xa9 2011 Perberos\n"
		               "Copyright \xc2\xa9 2012-2021 MATE developers"),
		"documenters", documenters,
		"logo-icon-name", NOTIFICATION_AREA_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		NULL);
}

static const GtkActionEntry menu_actions [] = {
	{ "SystemTrayPreferences", "document-properties", N_("_Preferences"),
	  NULL, NULL,
	  G_CALLBACK (properties_dialog) },
	{ "SystemTrayHelp", "help-browser", N_("_Help"),
	  NULL, NULL,
	  G_CALLBACK (help_cb) },
	{ "SystemTrayAbout", "help-about", N_("_About"),
	  NULL, NULL,
	  G_CALLBACK (about_cb) }
};

static void
na_tray_applet_realize (GtkWidget *widget)
{
  NaTrayApplet      *applet = NA_TRAY_APPLET (widget);
  if (parent_class_realize)
    parent_class_realize (widget);

  GtkActionGroup* action_group;
  action_group = gtk_action_group_new("NA Applet Menu Actions");
  gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions(action_group, menu_actions, G_N_ELEMENTS(menu_actions), applet);
  mate_panel_applet_setup_menu_from_resource (MATE_PANEL_APPLET (applet),
                                              NA_RESOURCE_PATH "notification-area-menu.xml",
                                              action_group);
  g_object_unref(action_group);

  setup_gsettings (applet);

  /* load min icon size */
  gsettings_changed_min_icon_size (applet->priv->settings, KEY_MIN_ICON_SIZE, applet);

  /* load the overflow state: the "changed" handlers only fire on change */
  apply_overflow_settings (applet);

  applet->priv->builder = gtk_builder_new ();
  gtk_builder_set_translation_domain (applet->priv->builder, GETTEXT_PACKAGE);
  gtk_builder_add_from_resource (applet->priv->builder, NA_RESOURCE_PATH "notification-area-preferences-dialog.ui", NULL);
}

static void
na_tray_applet_dispose (GObject *object)
{
  g_clear_object (&NA_TRAY_APPLET (object)->priv->settings);
#ifdef PROVIDE_WATCHER_SERVICE
  g_clear_object (&NA_TRAY_APPLET (object)->priv->sn_watcher);
#endif

  g_clear_object (&NA_TRAY_APPLET (object)->priv->builder);

  G_OBJECT_CLASS (na_tray_applet_parent_class)->dispose (object);
}

static void
na_tray_applet_style_updated (GtkWidget *widget)
{
  NaTrayApplet    *applet = NA_TRAY_APPLET (widget);
  gint             padding;
  gint             icon_size;

  if (parent_class_style_updated)
    parent_class_style_updated (widget);

  if (!applet->priv->grid)
    return;

  gtk_widget_style_get (widget,
                        "icon-padding", &padding,
                        "icon-size", &icon_size,
                        NULL);
  g_object_set (applet->priv->grid,
                "icon-padding", padding,
                "icon-size", icon_size,
                NULL);
}

static void
na_tray_applet_change_background(MatePanelApplet* panel_applet, MatePanelAppletBackgroundType type, GdkRGBA* color, cairo_pattern_t* pattern)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (panel_applet);

  if (parent_class_change_background) {
    parent_class_change_background (panel_applet, type, color, pattern);
  }

  if (applet->priv->grid)
    na_grid_force_redraw (NA_GRID (applet->priv->grid));
}

static void
na_tray_applet_change_orient (MatePanelApplet       *panel_applet,
                              MatePanelAppletOrient  orient)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (panel_applet);

  if (parent_class_change_orient)
    parent_class_change_orient (panel_applet, orient);

  if (!applet->priv->grid)
    return;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (applet->priv->grid),
                                  get_gtk_orientation_from_applet_orient (orient));
  na_grid_set_open_direction (NA_GRID (applet->priv->grid),
                              get_open_direction_from_applet_orient (orient));
}

static gboolean
na_tray_applet_button_press_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
  /* Prevent the panel from poping up the applet's popup on the the items,
   * which may also popup a menu which then conflicts.
   * This doesn't prevent the menu from poping up on the applet handle. */

  /* Swallowing here is safe: X propagates an event up the window chain only
   * when the owning window did not select it, so a press that reached this
   * handler was not delivered to the tray client. If the client took it, this
   * handler is not called at all. */
  if (event->button == 3)
    {
      /* Pass a right click on the ARROW on: the panel then shows the applet
       * menu (Properties, Move, Remove). Before this the menu was reachable
       * only by clicking to the left of the arrow, because every right click
       * was swallowed here. Tray icons are still swallowed, or the panel menu
       * would cover the application's own menu. */
      if (na_grid_is_chevron_at (NA_GRID (NA_TRAY_APPLET (widget)->priv->grid),
                                 (gint) event->x_root, (gint) event->y_root))
        return GTK_WIDGET_CLASS (na_tray_applet_parent_class)->button_press_event (widget, event);

      return TRUE;
    }

  return GTK_WIDGET_CLASS (na_tray_applet_parent_class)->button_press_event (widget, event);
}

static gboolean
na_tray_applet_focus (GtkWidget        *widget,
                      GtkDirectionType  direction)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (widget);

  /* We let the grid handle the focus movement because we behave more like a
   * container than a single applet.  But if focus didn't move, we let the
   * applet do its thing. */
  if (gtk_widget_child_focus (applet->priv->grid, direction))
    return TRUE;

  return GTK_WIDGET_CLASS (na_tray_applet_parent_class)->focus (widget, direction);
}

static void
na_tray_applet_class_init (NaTrayAppletClass *class)
{
  GObjectClass     *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass   *widget_class = GTK_WIDGET_CLASS (class);
  MatePanelAppletClass *applet_class = MATE_PANEL_APPLET_CLASS (class);

  object_class->dispose = na_tray_applet_dispose;

  parent_class_realize = widget_class->realize;
  widget_class->realize = na_tray_applet_realize;

  parent_class_style_updated = widget_class->style_updated;
  widget_class->style_updated = na_tray_applet_style_updated;
  parent_class_change_background = applet_class->change_background;
  applet_class->change_background = na_tray_applet_change_background;

  widget_class->button_press_event = na_tray_applet_button_press_event;
  widget_class->focus = na_tray_applet_focus;

  parent_class_change_orient = applet_class->change_orient;
  applet_class->change_orient = na_tray_applet_change_orient;

  gtk_widget_class_install_style_property (
          widget_class,
          g_param_spec_int ("icon-padding",
                            "Padding around icons",
                            "Padding that should be put around icons, in pixels",
                            0, G_MAXINT, 0,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gtk_widget_class_install_style_property (
          widget_class,
          g_param_spec_int ("icon-size",
                            "Icon size",
                            "If non-zero, hardcodes the size of the icons in pixels",
                            0, G_MAXINT, 0,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gtk_widget_class_set_css_name (widget_class, "na-tray-applet");
}

static void
na_tray_applet_init (NaTrayApplet *applet)
{
  MatePanelAppletOrient orient;
  AtkObject *atko;

  applet->priv = na_tray_applet_get_instance_private (applet);

#ifdef PROVIDE_WATCHER_SERVICE
  applet->priv->sn_watcher = sn_watcher_service_ref ();
#endif

  orient = mate_panel_applet_get_orient (MATE_PANEL_APPLET (applet));
  applet->priv->grid = na_grid_new (get_gtk_orientation_from_applet_orient (orient));
  na_grid_set_open_direction (NA_GRID (applet->priv->grid),
                              get_open_direction_from_applet_orient (orient));

  gtk_container_add (GTK_CONTAINER (applet), GTK_WIDGET (applet->priv->grid));
  gtk_widget_show (GTK_WIDGET (applet->priv->grid));

  atko = gtk_widget_get_accessible (GTK_WIDGET (applet));
  atk_object_set_name (atko, _("Panel Notification Area"));

  mate_panel_applet_set_flags (MATE_PANEL_APPLET (applet),
                          MATE_PANEL_APPLET_HAS_HANDLE|MATE_PANEL_APPLET_EXPAND_MINOR);
}

static gboolean
applet_factory (MatePanelApplet *applet,
                const gchar *iid,
                gpointer     user_data)
{
  if (!(strcmp (iid, "NotificationArea") == 0 ||
        strcmp (iid, "SystemTrayApplet") == 0))
    return FALSE;

#ifndef NOTIFICATION_AREA_INPROCESS
  gtk_window_set_default_icon_name (NOTIFICATION_AREA_ICON);
#endif

  gtk_widget_show_all (GTK_WIDGET (applet));

  return TRUE;
}

#ifdef NOTIFICATION_AREA_INPROCESS
	MATE_PANEL_APPLET_IN_PROCESS_FACTORY ("NotificationAreaAppletFactory",
				 NA_TYPE_TRAY_APPLET,
				 "NotificationArea",
				 applet_factory,
				 NULL)
#else
	MATE_PANEL_APPLET_OUT_PROCESS_FACTORY ("NotificationAreaAppletFactory",
				  NA_TYPE_TRAY_APPLET,
				  "NotificationArea",
				  applet_factory,
				  NULL)
#endif
