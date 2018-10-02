/*
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2006, 2007 Christian Persch
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
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include "system-tray/na-tray-manager.h"
#ifdef PROVIDE_WATCHER_SERVICE
# include "libstatus-notifier-watcher/gf-status-notifier-watcher.h"
#endif
#include "na-grid.h"

#define NOTIFICATION_AREA_ICON "mate-panel-notification-area"

static guint n_windows = 0;

typedef struct
{
  GdkScreen *screen;
  guint screen_num;
  GtkWidget *window;
  GtkWidget *traybox;
  GtkLabel *count_label;
} TrayData;

static void
do_add (GtkWidget *child, guint *n_children)
{
  *n_children += 1;
}

static void
update_child_count (TrayData *data)
{
  guint n_children = 0;
  char text[64];

  if (!gtk_widget_get_realized (data->window))
    return;

  gtk_container_foreach (GTK_CONTAINER (data->traybox), (GtkCallback) do_add, &n_children);

  g_snprintf (text, sizeof (text), "%u icons", n_children);
  gtk_label_set_text (data->count_label, text);
}

static void
tray_added_cb (GtkContainer *box, GtkWidget *icon, TrayData *data)
{
  g_print ("[Screen %u tray %p] Child %p added to tray: \"%s\"\n",
	   data->screen_num, data->traybox, icon, "XXX");//na_tray_child_get_title (icon));

  update_child_count (data);
}

static void
tray_removed_cb (GtkContainer *box, GtkWidget *icon, TrayData *data)
{
  g_print ("[Screen %u tray %p] Child %p removed from tray\n",
	   data->screen_num, data->traybox, icon);

  update_child_count (data);
}

static void orientation_changed_cb (GtkComboBox *combo, TrayData *data)
{
  GtkOrientation orientation = (GtkOrientation) gtk_combo_box_get_active (combo);

  g_print ("[Screen %u tray %p] Setting orientation to \"%s\"\n",
	   data->screen_num, data->traybox, orientation == 0 ? "horizontal" : "vertical");

  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->traybox), orientation);
}

static void
maybe_quit (gpointer data,
	    GObject *zombie)
{
  if (--n_windows == 0) {
    gtk_main_quit ();
  }
}

static TrayData *create_tray_on_screen (GdkScreen *screen, gboolean force);

static void
warning_dialog_response_cb (GtkWidget *dialog,
			    gint response,
			    GdkScreen *screen)
{
  if (response == GTK_RESPONSE_YES) {
    create_tray_on_screen (screen, TRUE);
  }

  gtk_widget_destroy (dialog);
}

static void
add_tray_cb (GtkWidget *button, TrayData *data)
{
  create_tray_on_screen (data->screen, TRUE);
}

static TrayData *
create_tray_on_screen (GdkScreen *screen,
		       gboolean force)
{
  GtkWidget *window, *hbox, *vbox, *button, *combo, *label;
  TrayData *data;

  n_windows++;

  if (!force && na_tray_manager_check_running (screen)) {
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
				     "Override tray manager?");
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					     "There is already a tray manager running on screen %d.",
					     gdk_x11_screen_get_screen_number (screen));
    gtk_window_set_screen (GTK_WINDOW (dialog), screen);
    g_signal_connect (dialog, "response", G_CALLBACK (warning_dialog_response_cb), screen);
    gtk_window_present (GTK_WINDOW (dialog));
    g_object_weak_ref (G_OBJECT (dialog), (GWeakNotify) maybe_quit, NULL);
    return NULL;
  }

  data = g_new0 (TrayData, 1);
  data->screen = screen;
  data->screen_num = gdk_x11_screen_get_screen_number (screen);

  data->window = window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_object_weak_ref (G_OBJECT (window), (GWeakNotify) maybe_quit, NULL);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  button = gtk_button_new_with_mnemonic ("_Add another tray");
  g_signal_connect (button, "clicked", G_CALLBACK (add_tray_cb), data);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  label = gtk_label_new_with_mnemonic ("_Orientation:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_yalign (GTK_LABEL (label), 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), "Horizontal");
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), "Vertical");
  g_signal_connect (combo, "changed",
		    G_CALLBACK (orientation_changed_cb), data);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);

  label = gtk_label_new (NULL);
  data->count_label = GTK_LABEL (label);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_yalign (GTK_LABEL (label), 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  data->traybox = na_grid_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (data->traybox), TRUE, TRUE, 0);

  g_signal_connect_after (data->traybox, "add", G_CALLBACK (tray_added_cb), data);
  g_signal_connect_after (data->traybox, "remove", G_CALLBACK (tray_removed_cb), data);

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

  gtk_window_set_screen (GTK_WINDOW (window), screen);
  gtk_window_set_default_size (GTK_WINDOW (window), -1, 200);

  /* gtk_window_set_resizable (GTK_WINDOW (window), FALSE); */

  gtk_widget_show_all (window);

  update_child_count (data);

  return data;
}

static gboolean
signal_handler (gpointer data G_GNUC_UNUSED)
{
  gtk_main_quit ();

  return FALSE;
}

#ifdef PROVIDE_WATCHER_SERVICE
static GfStatusNotifierWatcher *
status_notifier_watcher_maybe_new (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GfStatusNotifierWatcher *service = NULL;

  /* check if the service already exists
   * FIXME: is that a not-too-stupid way of doing it?  bah, so long as it works.
   *        it's for testing purposes only anyway. */
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                         G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                         NULL,
                                         "org.kde.StatusNotifierWatcher",
                                         "/StatusNotifierWatcher",
                                         "org.kde.StatusNotifierWatcher",
                                         NULL, &error);
  if (proxy)
    g_object_unref (proxy);
  else
    {
      g_warning ("Failed to connect to org.kde.StatusNotifierWatcher (%s), starting our own.",
                 error->message);
      g_clear_error (&error);

      service = gf_status_notifier_watcher_new ();
    }

  return service;
}
#endif

int
main (int argc, char *argv[])
{
  GdkDisplay *display;
  GdkScreen *screen;
#ifdef PROVIDE_WATCHER_SERVICE
  GfStatusNotifierWatcher *service;
#endif

  gtk_init (&argc, &argv);

  g_unix_signal_add (SIGTERM, signal_handler, NULL);
  g_unix_signal_add (SIGINT, signal_handler, NULL);

#ifdef PROVIDE_WATCHER_SERVICE
  service = status_notifier_watcher_maybe_new ();
#endif

  gtk_window_set_default_icon_name (NOTIFICATION_AREA_ICON);

  display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);

  create_tray_on_screen (screen, FALSE);

  gtk_main ();

#ifdef PROVIDE_WATCHER_SERVICE
  if (service)
    g_object_unref (service);
#endif

  return 0;
}
