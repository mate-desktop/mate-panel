/*
 * panel-icon-chooser.c: An icon chooser widget
 *
 * Copyright (C) 2010 Novell, Inc.
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
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "panel-gtk.h"
#include "panel-xdg.h"

#include "panel-icon-chooser.h"

#define PANEL_ICON_CHOOSER_ICON_SIZE GTK_ICON_SIZE_DIALOG

struct _PanelIconChooserPrivate
{
	char *fallback_icon_name;
	char *icon;

	char *icon_theme_dir;

	GtkWidget *image;

	GtkWidget *filechooser;
};

enum {
	CHANGED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_FALLBACK_ICON,
	PROP_ICON
};

static guint panel_icon_chooser_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (PanelIconChooser, panel_icon_chooser, GTK_TYPE_BUTTON)

static void _panel_icon_chooser_clicked (GtkButton *button);
static void _panel_icon_chooser_style_set (GtkWidget *widget,
					   GtkStyle  *prev_style);
static void _panel_icon_chooser_screen_changed (GtkWidget *widget,
						GdkScreen *prev_screen);

/* gobject stuff */

static GObject *
panel_icon_chooser_constructor (GType                  type,
				guint                  n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	GObject          *obj;
	PanelIconChooser *chooser;

	obj = G_OBJECT_CLASS (panel_icon_chooser_parent_class)->constructor (type,
									     n_construct_properties,
									     construct_properties);

	chooser = PANEL_ICON_CHOOSER (obj);
	gtk_container_add (GTK_CONTAINER (chooser), chooser->priv->image);
	gtk_widget_show (chooser->priv->image);

	return obj;
}

static void
panel_icon_chooser_get_property (GObject    *object,
				 guint	     prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	PanelIconChooser *chooser;

	g_return_if_fail (PANEL_IS_ICON_CHOOSER (object));

	chooser = PANEL_ICON_CHOOSER (object);

	switch (prop_id) {
	case PROP_FALLBACK_ICON:
		g_value_set_string (value, panel_icon_chooser_get_fallback_icon_name (chooser));
		break;
	case PROP_ICON:
		g_value_set_string (value, panel_icon_chooser_get_icon (chooser));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_icon_chooser_set_property (GObject      *object,
				 guint	       prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	PanelIconChooser *chooser;

	g_return_if_fail (PANEL_IS_ICON_CHOOSER (object));

	chooser = PANEL_ICON_CHOOSER (object);

	switch (prop_id) {
	case PROP_FALLBACK_ICON:
		panel_icon_chooser_set_fallback_icon_name (chooser,
							   g_value_get_string (value));
		break;
	case PROP_ICON:
		panel_icon_chooser_set_icon (chooser,
					     g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_icon_chooser_dispose (GObject *object)
{
	PanelIconChooser *chooser;

	chooser = PANEL_ICON_CHOOSER (object);

	if (chooser->priv->filechooser) {
		gtk_widget_destroy (chooser->priv->filechooser);
		chooser->priv->filechooser = NULL;
	}

	/* remember, destroy can be run multiple times! */

	if (chooser->priv->fallback_icon_name != NULL)
		g_free (chooser->priv->fallback_icon_name);
	chooser->priv->fallback_icon_name = NULL;

	if (chooser->priv->icon != NULL)
		g_free (chooser->priv->icon);
	chooser->priv->icon = NULL;

	if (chooser->priv->icon_theme_dir != NULL)
		g_free (chooser->priv->icon_theme_dir);
	chooser->priv->icon_theme_dir = NULL;

	G_OBJECT_CLASS (panel_icon_chooser_parent_class)->dispose (object);
}

static void
panel_icon_chooser_class_init (PanelIconChooserClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);
	GtkWidgetClass *gtkwidget_class = GTK_WIDGET_CLASS (class);
	GtkButtonClass *gtkbutton_class = GTK_BUTTON_CLASS (class);

	gobject_class->constructor = panel_icon_chooser_constructor;
	gobject_class->get_property = panel_icon_chooser_get_property;
	gobject_class->set_property = panel_icon_chooser_set_property;

	gobject_class->dispose = panel_icon_chooser_dispose;

	gtkwidget_class->style_set = _panel_icon_chooser_style_set;
	gtkwidget_class->screen_changed = _panel_icon_chooser_screen_changed;

	gtkbutton_class->clicked = _panel_icon_chooser_clicked;

	panel_icon_chooser_signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelIconChooserClass,
					       changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	g_object_class_install_property (
		gobject_class,
		PROP_FALLBACK_ICON,
		g_param_spec_string ("fallback-icon-name",
				     "Fallback Icon Name",
				     "Icon name of the icon displayed (but not returned) if the current icon does not exist",
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		gobject_class,
		PROP_ICON,
		g_param_spec_string ("icon",
				     "Icon",
				     "Icon name or path",
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
panel_icon_chooser_init (PanelIconChooser *chooser)
{
	PanelIconChooserPrivate *priv;

	priv = panel_icon_chooser_get_instance_private (chooser);

	chooser->priv = priv;

	priv->fallback_icon_name = g_strdup ("image-missing");
	priv->icon = NULL;
	priv->icon_theme_dir = NULL;

	priv->image = gtk_image_new_from_icon_name (priv->fallback_icon_name,
						    PANEL_ICON_CHOOSER_ICON_SIZE);

	priv->filechooser = NULL;
}

/* internal code */

static void
_panel_icon_chooser_update (PanelIconChooser *chooser)
{
	if (!chooser->priv->icon) {
		gtk_image_set_from_icon_name (GTK_IMAGE (chooser->priv->image),
					      chooser->priv->fallback_icon_name,
					      PANEL_ICON_CHOOSER_ICON_SIZE);

	} else if (g_path_is_absolute (chooser->priv->icon)) {
		gboolean fallback;

		fallback = TRUE;

		if (g_file_test (chooser->priv->icon, G_FILE_TEST_EXISTS)) {
			/* we pass via a pixbuf to force the size we want */
			GdkPixbuf *pixbuf;
			int        width, height;

			gtk_icon_size_lookup (PANEL_ICON_CHOOSER_ICON_SIZE,
					      &width, &height);
			pixbuf = gdk_pixbuf_new_from_file_at_size (chooser->priv->icon,
								   width, height,
								   NULL);

			if (pixbuf) {
				gtk_image_set_from_pixbuf (GTK_IMAGE (chooser->priv->image),
							   pixbuf);
				g_object_unref (pixbuf);
				fallback = FALSE;
			}
		}

		if (fallback) {
			gtk_image_set_from_icon_name (GTK_IMAGE (chooser->priv->image),
						      chooser->priv->fallback_icon_name,
						      PANEL_ICON_CHOOSER_ICON_SIZE);
		}

	} else {
		/* Note: using GThemedIcon doesn't work well, see bug #606752.
		 * When we'll remove the alternative code, we won't need the
		 * style_set/screen_changed handlers anymore.
		 */
#if 0
		GIcon *icon;
		char  *names[2];

		names[0] = panel_xdg_icon_remove_extension (chooser->priv->icon);
		names[1] = chooser->priv->fallback_icon_name;
		icon = g_themed_icon_new_from_names (names, 2);

		gtk_image_set_from_gicon (GTK_IMAGE (chooser->priv->image),
					  icon,
					  PANEL_ICON_CHOOSER_ICON_SIZE);

		g_free (names[0]);
#endif
		GtkIconTheme *icon_theme;
		const char   *icon;
		char         *no_ext;

		no_ext = panel_xdg_icon_remove_extension (chooser->priv->icon);

		icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (chooser)));
		if (gtk_icon_theme_has_icon (icon_theme, no_ext))
			icon = no_ext;
		else
			icon = chooser->priv->fallback_icon_name;

		gtk_image_set_from_icon_name (GTK_IMAGE (chooser->priv->image),
					      icon,
					      PANEL_ICON_CHOOSER_ICON_SIZE);

		g_free (no_ext);
	}
}

static char *
_panel_icon_chooser_find_icon_from_path (PanelIconChooser *chooser,
					 const char       *path)
{
	GdkScreen *screen;
	char      *icon;

	screen = gtk_widget_get_screen (GTK_WIDGET (chooser));

	icon = panel_xdg_icon_name_from_icon_path (path, screen);
	if (!icon)
		icon = g_strdup (path);

	return icon;
}

static void
_panel_icon_chooser_file_chooser_response (GtkFileChooser   *filechooser,
					   gint              response_id,
					   PanelIconChooser *chooser)
{
	if (response_id == GTK_RESPONSE_ACCEPT) {
		char *path;
		char *icon;

		path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (filechooser));
		icon = _panel_icon_chooser_find_icon_from_path (chooser, path);
		g_free (path);

		panel_icon_chooser_set_icon (chooser, icon);
		g_free (icon);
	}

	gtk_widget_destroy (GTK_WIDGET (filechooser));
}

static void
_panel_icon_chooser_clicked (GtkButton *button)
{
	PanelIconChooser *chooser = PANEL_ICON_CHOOSER (button);
	GtkWidget        *filechooser;
	GtkWidget        *toplevel;
	GtkWindow        *parent;
	char             *path;
	gboolean          filechooser_path_set;

	if (chooser->priv->filechooser) {
		gtk_window_present (GTK_WINDOW (chooser->priv->filechooser));
		return;
	}

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	if (gtk_widget_is_toplevel (toplevel))
		parent = GTK_WINDOW (toplevel);
	else
		parent = NULL;

	filechooser = panel_file_chooser_dialog_new (_("Choose an icon"),
						     parent,
						     GTK_FILE_CHOOSER_ACTION_OPEN,
						     "process-stop",
						     GTK_RESPONSE_CANCEL,
						     "document-open",
						     GTK_RESPONSE_ACCEPT,
						     NULL);

	panel_gtk_file_chooser_add_image_preview (GTK_FILE_CHOOSER (filechooser));

	path = g_build_filename (DATADIR, "icons", NULL);
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (filechooser),
					      path, NULL);
	g_free (path);

	path = g_build_filename (DATADIR, "pixmaps", NULL);
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (filechooser),
					      path, NULL);
	g_free (path);

	filechooser_path_set = FALSE;

	if (chooser->priv->icon) {
		char *path = NULL;
		if (g_path_is_absolute (chooser->priv->icon)) {
			path = g_strdup (chooser->priv->icon);
		} else {
			GtkIconTheme *icon_theme;
			GtkIconInfo  *info;
			char         *no_ext;
			int           size;

			icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (chooser)));
			no_ext = panel_xdg_icon_remove_extension (chooser->priv->icon);
			gtk_icon_size_lookup (PANEL_ICON_CHOOSER_ICON_SIZE,
					      &size, NULL);

			info = gtk_icon_theme_lookup_icon (icon_theme, no_ext,
							   size, 0);
			g_free (no_ext);

			if (info) {
				path = g_strdup (gtk_icon_info_get_filename (info));
				g_object_unref (info);
			}
		}

		if (path) {
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (filechooser),
						       path);
			g_free (path);
			filechooser_path_set = TRUE;
		}
	}

	if (!filechooser_path_set) {
		char *path;
		// FIXME? Use current icon theme? But there might not be a lot
		// of icons there...
		path = g_build_filename (DATADIR, "icons", NULL);
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filechooser),
						     path);
		g_free (path);
	}

	gtk_window_set_destroy_with_parent (GTK_WINDOW (filechooser), TRUE);

	g_signal_connect (filechooser, "response",
			  G_CALLBACK (_panel_icon_chooser_file_chooser_response),
			  chooser);

	chooser->priv->filechooser = filechooser;

	g_signal_connect (G_OBJECT (filechooser), "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
			  &chooser->priv->filechooser);

	gtk_widget_show (filechooser);
}

static void
_panel_icon_chooser_style_set (GtkWidget *widget,
			       GtkStyle  *prev_style)
{
	PanelIconChooser *chooser;

	chooser = PANEL_ICON_CHOOSER (widget);

	GTK_WIDGET_CLASS (panel_icon_chooser_parent_class)->style_set (widget, prev_style);

	_panel_icon_chooser_update (chooser);
}

static void
_panel_icon_chooser_screen_changed (GtkWidget *widget,
				    GdkScreen *prev_screen)
{
	PanelIconChooser *chooser;

	chooser = PANEL_ICON_CHOOSER (widget);

	if (GTK_WIDGET_CLASS (panel_icon_chooser_parent_class)->screen_changed)
		GTK_WIDGET_CLASS (panel_icon_chooser_parent_class)->screen_changed (widget, prev_screen);

	_panel_icon_chooser_update (chooser);
}

/* public methods */

GtkWidget  *
panel_icon_chooser_new (const char  *icon)
{
	GtkWidget *chooser;

	chooser = g_object_new (PANEL_TYPE_ICON_CHOOSER,
			       "icon", icon,
			       NULL);

	return chooser;
}

const char *
panel_icon_chooser_get_fallback_icon_name (PanelIconChooser *chooser)
{
	g_return_val_if_fail (PANEL_IS_ICON_CHOOSER (chooser), NULL);

	return chooser->priv->fallback_icon_name;
}

void
panel_icon_chooser_set_fallback_icon_name (PanelIconChooser *chooser,
					   const char       *fallback_icon_name)
{
	g_return_if_fail (PANEL_IS_ICON_CHOOSER (chooser));

	if (g_strcmp0 (chooser->priv->fallback_icon_name, fallback_icon_name) == 0)
		return;

	if (chooser->priv->fallback_icon_name)
		g_free (chooser->priv->fallback_icon_name);
	chooser->priv->fallback_icon_name = g_strdup (fallback_icon_name);

	_panel_icon_chooser_update (chooser);

	g_object_notify (G_OBJECT (chooser), "fallback-icon-name");
}

const char *
panel_icon_chooser_get_icon (PanelIconChooser *chooser)
{
	g_return_val_if_fail (PANEL_IS_ICON_CHOOSER (chooser), NULL);

	return chooser->priv->icon;
}

void
panel_icon_chooser_set_icon (PanelIconChooser *chooser,
			     const char       *icon)
{
	g_return_if_fail (PANEL_IS_ICON_CHOOSER (chooser));

	if (g_strcmp0 (chooser->priv->icon, icon) == 0)
		return;

	if (chooser->priv->icon)
		g_free (chooser->priv->icon);
	chooser->priv->icon = g_strdup (icon);

	_panel_icon_chooser_update (chooser);

	g_object_notify (G_OBJECT (chooser), "icon");

	g_signal_emit (G_OBJECT (chooser),
		       panel_icon_chooser_signals[CHANGED], 0, icon);
}
