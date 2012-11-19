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
#include <gtk/gtk.h>
#include "na-tray-manager.h"
#include "na-tray.h"

#define NOTIFICATION_AREA_ICON "mate-panel-notification-area"

static guint n_windows = 0;

typedef struct
{
  GdkScreen *screen;
  guint screen_num;
  GtkWidget *window;
  NaTray *tray;
  GtkWidget *box;
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

  gtk_container_foreach (GTK_CONTAINER (data->box), (GtkCallback) do_add, &n_children);

  g_snprintf (text, sizeof (text), "%u icons", n_children);
  gtk_label_set_text (data->count_label, text);
}

static void
tray_added_cb (GtkContainer *box, GtkWidget *icon, TrayData *data)
{
  g_print ("[Screen %u tray %p] Child %p added to tray: \"%s\"\n",
	   data->screen_num, data->tray, icon, "XXX");//na_tray_child_get_title (icon));

  update_child_count (data);
}

static void
tray_removed_cb (GtkContainer *box, GtkWidget *icon, TrayData *data)
{
  g_print ("[Screen %u tray %p] Child %p removed from tray\n",
	   data->screen_num, data->tray, icon);

  update_child_count (data);
}

static void orientation_changed_cb (GtkComboBox *combo, TrayData *data)
{
  GtkOrientation orientation = (GtkOrientation) gtk_combo_box_get_active (combo);

  g_print ("[Screen %u tray %p] Setting orientation to \"%s\"\n",
	   data->screen_num, data->tray, orientation == 0 ? "horizontal" : "vertical");

  na_tray_set_orientation (data->tray, orientation);
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
					     gdk_screen_get_number (screen));
    gtk_window_set_screen (GTK_WINDOW (dialog), screen);
    g_signal_connect (dialog, "response", G_CALLBACK (warning_dialog_response_cb), screen);
    gtk_window_present (GTK_WINDOW (dialog));
    g_object_weak_ref (G_OBJECT (dialog), (GWeakNotify) maybe_quit, NULL);
    return NULL;
  }

  data = g_new0 (TrayData, 1);
  data->screen = screen;
  data->screen_num = gdk_screen_get_number (screen);

  data->window = window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_object_weak_ref (G_OBJECT (window), (GWeakNotify) maybe_quit, NULL);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  button = gtk_button_new_with_mnemonic ("_Add another tray");
  g_signal_connect (button, "clicked", G_CALLBACK (add_tray_cb), data);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  label = gtk_label_new_with_mnemonic ("_Orientation:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  combo = gtk_combo_box_new_text ();
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "Horizontal");
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo), "Vertical");
  g_signal_connect (combo, "changed",
		    G_CALLBACK (orientation_changed_cb), data);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);

  label = gtk_label_new (NULL);
  data->count_label = GTK_LABEL (label);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  data->tray = na_tray_new_for_screen (screen, GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (data->tray), TRUE, TRUE, 0);

  data->box = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (data->tray))));
  g_signal_connect_after (data->box, "add", G_CALLBACK (tray_added_cb), data);
  g_signal_connect_after (data->box, "remove", G_CALLBACK (tray_removed_cb), data);

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

  gtk_window_set_screen (GTK_WINDOW (window), screen);
  gtk_window_set_default_size (GTK_WINDOW (window), -1, 200);

  /* gtk_window_set_resizable (GTK_WINDOW (window), FALSE); */

  gtk_widget_show_all (window);

  update_child_count (data);

  return data;
}

int
main (int argc, char *argv[])
{
  GdkDisplay *display;
  GdkScreen *screen;
  int n_screens, i;

  gtk_init (&argc, &argv);

  gtk_window_set_default_icon_name (NOTIFICATION_AREA_ICON);

  display = gdk_display_get_default ();
  n_screens =  gdk_display_get_n_screens (display);
  for (i = 0; i < n_screens; ++i) {
    screen = gdk_display_get_screen (display, i);

    create_tray_on_screen (screen, FALSE);
  }
  
  gtk_main ();

  return 0;
}
