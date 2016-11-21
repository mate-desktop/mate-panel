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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "main.h"
#include "na-tray-manager.h"
#include "na-tray.h"
#include "fixedtip.h"

#define NOTIFICATION_AREA_ICON "mate-panel-notification-area"

struct _NaTrayAppletPrivate
{
  NaTray *tray;
};

G_DEFINE_TYPE (NaTrayApplet, na_tray_applet, PANEL_TYPE_APPLET)

static void (*parent_class_realize) (GtkWidget *widget);
static void (*parent_class_unrealize) (GtkWidget *widget);
static void (*parent_class_style_updated) (GtkWidget *widget);
static void (*parent_class_change_background)(MatePanelApplet* panel_applet, MatePanelAppletBackgroundType type, GdkRGBA* color, cairo_pattern_t* pattern);
static void (*parent_class_change_orient)(MatePanelApplet       *panel_applet, MatePanelAppletOrient  orient);


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

static void help_cb(GtkAction* action, NaTrayApplet* applet)
{
	GError* error = NULL;
	char* uri;
	#define NA_HELP_DOC "mate-user-guide"

	uri = g_strdup_printf("help:%s/%s", NA_HELP_DOC, "panels-notification-area");

	gtk_show_uri(gtk_widget_get_screen(GTK_WIDGET(applet)), uri, gtk_get_current_event_time(), &error);

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
		NULL
	};

	const char* documenters[] = {
		"Sun GNOME Documentation Team <gdocteam@sun.com>",
		NULL
	};

	const char copyright[] = \
		"Copyright \xc2\xa9 2002 Red Hat, Inc.\n"
		"Copyright \xc2\xa9 2003-2006 Vincent Untz\n"
		"Copyright \xc2\xa9 2011 Perberos\n"
		"Copyright \xc2\xa9 2012-2016 MATE developers";

	gtk_show_about_dialog(NULL,
		"program-name", _("Notification Area"),
		"authors", authors,
		//"comments", _(comments),
		"copyright", copyright,
		"documenters", documenters,
		"logo-icon-name", NOTIFICATION_AREA_ICON,
		"translator-credits", _("translator-credits"),
		"version", VERSION,
		NULL);
}

static const GtkActionEntry menu_actions [] = {
	{ "SystemTrayHelp", GTK_STOCK_HELP, N_("_Help"),
	  NULL, NULL,
	  G_CALLBACK (help_cb) },
	{ "SystemTrayAbout", GTK_STOCK_ABOUT, N_("_About"),
	  NULL, NULL,
	  G_CALLBACK (about_cb) }
};


static void
na_tray_applet_realize (GtkWidget *widget)
{
  NaTrayApplet      *applet = NA_TRAY_APPLET (widget);
  MatePanelAppletOrient  orient;

  if (parent_class_realize)
    parent_class_realize (widget);

  g_assert (applet->priv->tray == NULL);

  orient = mate_panel_applet_get_orient (MATE_PANEL_APPLET (widget));

  applet->priv->tray = na_tray_new_for_screen (gtk_widget_get_screen (widget),
                                               get_gtk_orientation_from_applet_orient (orient));

  gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (applet->priv->tray));
  gtk_widget_show (GTK_WIDGET (applet->priv->tray));

  GtkActionGroup* action_group;
  gchar* ui_path;
  action_group = gtk_action_group_new("NA Applet Menu Actions");
  gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions(action_group, menu_actions, G_N_ELEMENTS(menu_actions), applet);
  ui_path = g_build_filename(NOTIFICATION_AREA_MENU_UI_DIR, "notification-area-menu.xml", NULL);
  mate_panel_applet_setup_menu_from_file(MATE_PANEL_APPLET(applet), ui_path, action_group);
  g_free(ui_path);
  g_object_unref(action_group);
}

static void
na_tray_applet_unrealize (GtkWidget *widget)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (widget);

  g_assert (applet->priv->tray != NULL);

  gtk_widget_destroy (GTK_WIDGET (applet->priv->tray));
  applet->priv->tray = NULL;

  if (parent_class_unrealize)
    parent_class_unrealize (widget);
}

static void
na_tray_applet_style_updated (GtkWidget *widget)
{
  NaTrayApplet    *applet = NA_TRAY_APPLET (widget);
  GtkStyleContext *context;
  GdkRGBA          fg;
  GdkRGBA          error;
  GdkRGBA          warning;
  GdkRGBA          success;
  gint             padding;
  gint             icon_size;

  if (parent_class_style_updated)
    parent_class_style_updated (widget);

  if (!applet->priv->tray)
    return;

  context = gtk_widget_get_style_context (widget);

  gtk_style_context_save (context);
  gtk_style_context_set_state (context, GTK_STATE_FLAG_NORMAL);

  gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &fg);

  if (!gtk_style_context_lookup_color (context, "error_color", &error))
    error = fg;
  if (!gtk_style_context_lookup_color (context, "warning_color", &warning))
    warning = fg;
  if (!gtk_style_context_lookup_color (context, "success_color", &success))
    success = fg;

  gtk_style_context_restore (context);

  na_tray_set_colors (applet->priv->tray, &fg, &error, &warning, &success);

  gtk_widget_style_get (widget, "icon-padding", &padding, NULL);
  na_tray_set_padding (applet->priv->tray, padding);

  gtk_widget_style_get (widget, "icon-size", &icon_size, NULL);
  na_tray_set_icon_size (applet->priv->tray, icon_size);
}

static void
na_tray_applet_change_background(MatePanelApplet* panel_applet, MatePanelAppletBackgroundType type, GdkRGBA* color, cairo_pattern_t* pattern)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (panel_applet);

  if (parent_class_change_background) {
    parent_class_change_background (panel_applet, type, color, pattern);
  }

  if (!applet->priv->tray)
    return;

  na_tray_force_redraw (applet->priv->tray);
}

static void
na_tray_applet_change_orient (MatePanelApplet       *panel_applet,
                              MatePanelAppletOrient  orient)
{
  NaTrayApplet *applet = NA_TRAY_APPLET (panel_applet);

  if (parent_class_change_orient)
    parent_class_change_orient (panel_applet, orient);

  if (!applet->priv->tray)
    return;

  na_tray_set_orientation (applet->priv->tray,
                           get_gtk_orientation_from_applet_orient (orient));
}

#if !GTK_CHECK_VERSION (3, 20, 0)
static inline void
force_no_focus_padding (GtkWidget *widget)
{
  GtkCssProvider *provider;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider,
                                   "NaTrayApplet {\n"
                                   " -GtkWidget-focus-line-width: 0px;\n"
                                   " -GtkWidget-focus-padding: 0px;\n"
				   "}",
                                   -1, NULL);
  gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
                                  GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
}
#endif

static void
na_tray_applet_class_init (NaTrayAppletClass *class)
{
  GtkWidgetClass   *widget_class = GTK_WIDGET_CLASS (class);
  MatePanelAppletClass *applet_class = MATE_PANEL_APPLET_CLASS (class);

  parent_class_realize = widget_class->realize;
  widget_class->realize = na_tray_applet_realize;

  parent_class_unrealize = widget_class->unrealize;
  widget_class->unrealize = na_tray_applet_unrealize;
  parent_class_style_updated = widget_class->style_updated;
  widget_class->style_updated = na_tray_applet_style_updated;
  parent_class_change_background = applet_class->change_background;
  applet_class->change_background = na_tray_applet_change_background;

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

  g_type_class_add_private (class, sizeof (NaTrayAppletPrivate));

#if GTK_CHECK_VERSION (3, 20, 0)
  gtk_widget_class_set_css_name (widget_class, "na-tray-applet");
#endif
}

static void
na_tray_applet_init (NaTrayApplet *applet)
{
  AtkObject *atko;

  applet->priv = G_TYPE_INSTANCE_GET_PRIVATE (applet, NA_TYPE_TRAY_APPLET,
                                              NaTrayAppletPrivate);

  /* Defer creating NaTray until applet is added to panel so
   * gtk_widget_get_screen returns correct information */
  applet->priv->tray = NULL;

  atko = gtk_widget_get_accessible (GTK_WIDGET (applet));
  atk_object_set_name (atko, _("Panel Notification Area"));

  mate_panel_applet_set_flags (MATE_PANEL_APPLET (applet),
                          MATE_PANEL_APPLET_HAS_HANDLE|MATE_PANEL_APPLET_EXPAND_MINOR);

#if !GTK_CHECK_VERSION (3, 20, 0)
  force_no_focus_padding (GTK_WIDGET (applet));
#endif
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
