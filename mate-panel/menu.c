/*
 * Copyright (C) 1997 - 2000 The Free Software Foundation
 * Copyright (C) 2000 Helix Code, Inc.
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2004 Red Hat Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include "menu.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gdk/gdkkeysyms.h>
#include <mateconf/mateconf-client.h>

#include <libpanel-util/panel-keyfile.h>
#include <libpanel-util/panel-xdg.h>

#include "launcher.h"
#include "panel-util.h"
#include "panel.h"
#include "drawer.h"
#include "panel-config-global.h"
#include "panel-stock-icons.h"
#include "panel-action-button.h"
#include "panel-profile.h"
#include "panel-menu-button.h"
#include "panel-menu-items.h"
#include "panel-globals.h"
#include "panel-run-dialog.h"
#include "panel-lockdown.h"
#include "panel-icon-names.h"

typedef struct {
	GtkWidget    *pixmap;
	const char   *stock_id;
	GIcon        *gicon;
	char         *image;
	char         *fallback_image;
	GtkIconTheme *icon_theme;
	GtkIconSize   icon_size;
} IconToLoad;

typedef struct {
	GtkWidget   *image;
	const char  *stock_id;
	GIcon       *gicon;
	GdkPixbuf   *pixbuf;
	GtkIconSize  icon_size;
} IconToAdd;

static guint load_icons_id = 0;
static GHashTable *loaded_icons = NULL;
static GList *icons_to_load = NULL;
static GList *icons_to_add = NULL;

static GSList *image_menu_items = NULL;

static GtkWidget *populate_menu_from_directory (GtkWidget          *menu,
						MateMenuTreeDirectory *directory);

static void panel_load_menu_image_deferred (GtkWidget   *image_menu_item,
					    GtkIconSize  icon_size,
					    const char  *stock_id,
					    GIcon       *gicon,
					    const char  *image_filename,
					    const char  *fallback_image_filename);

static gboolean panel_menu_key_press_handler (GtkWidget   *widget,
					      GdkEventKey *event);

static inline gboolean desktop_is_home_dir(void)
{
	return mateconf_client_get_bool(panel_mateconf_get_client(), "/apps/caja/preferences/desktop_is_home_dir", NULL);
}

GtkWidget *
add_menu_separator (GtkWidget *menu)
{
	GtkWidget *menuitem;

	menuitem = gtk_separator_menu_item_new ();
	gtk_widget_set_sensitive (menuitem, FALSE);
	gtk_widget_show (menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	return menuitem;
}

static void
activate_app_def (GtkWidget      *menuitem,
		  MateMenuTreeEntry *entry)
{
	const char       *path;

	path = matemenu_tree_entry_get_desktop_file_path (entry);
	panel_menu_item_activate_desktop_file (menuitem, path);
}

PanelWidget *
menu_get_panel (GtkWidget *menu)
{
	PanelWidget *retval = NULL;

	g_return_val_if_fail (menu != NULL, NULL);

	if (GTK_IS_MENU_ITEM (menu))
		menu = gtk_widget_get_parent (menu);

	g_return_val_if_fail (GTK_IS_MENU (menu), NULL);

	while (menu) {
		retval = g_object_get_data (G_OBJECT (menu), "menu_panel");
		if (retval)
			break;

		menu = gtk_widget_get_parent (gtk_menu_get_attach_widget (GTK_MENU (menu)));
		if (!GTK_IS_MENU (menu))
			break;
	}

	if (retval && !PANEL_IS_WIDGET (retval)) {
		g_warning ("Invalid PanelWidget associated with menu");
		retval = NULL;
	}

	if (!retval) {
		g_warning ("Cannot find the PanelWidget associated with menu");
		retval = panels->data;
	}

	return retval;
}

static void
setup_menu_panel (GtkWidget *menu)
{
	PanelWidget *panel;

	panel = g_object_get_data (G_OBJECT (menu), "menu_panel");
	if (panel)
		return;

	panel = menu_get_panel (menu);
	g_object_set_data (G_OBJECT (menu), "menu_panel", panel);

	if (panel)
		gtk_menu_set_screen (GTK_MENU (menu),
				     gtk_widget_get_screen (GTK_WIDGET (panel)));
}

GdkScreen *
menuitem_to_screen (GtkWidget *menuitem)
{
	PanelWidget *panel_widget;

	panel_widget = menu_get_panel (menuitem);

	return gtk_window_get_screen (GTK_WINDOW (panel_widget->toplevel));
}

static void
reload_image_menu_items (void)
{
	GSList* l;

	for (l = image_menu_items; l; l = l->next) {
		GtkWidget *image = l->data;
		gboolean   is_mapped;

		is_mapped = gtk_widget_get_mapped (image);

		if (is_mapped)
			gtk_widget_unmap (image);

		gtk_image_set_from_pixbuf (GTK_IMAGE (image), NULL);

		if (is_mapped)
			gtk_widget_map (image);

	}
}

static void
icon_theme_changed (GtkIconTheme *icon_theme,
		    gpointer      data)
{
	reload_image_menu_items ();
}

GtkWidget *
panel_create_menu (void)
{
	GtkWidget       *retval;
	static gboolean  registered_icon_theme_changer = FALSE;

	if (!registered_icon_theme_changer) {
		registered_icon_theme_changer = TRUE;

		g_signal_connect (gtk_icon_theme_get_default (), "changed",
				  G_CALLBACK (icon_theme_changed), NULL);
	}

	retval = gtk_menu_new ();
	gtk_widget_set_name (retval, "mate-panel-main-menu");

	g_signal_connect (retval, "key_press_event",
			  G_CALLBACK (panel_menu_key_press_handler),
			  NULL);

	return retval;
}

GtkWidget *
create_empty_menu (void)
{
	GtkWidget *retval;

	retval = panel_create_menu ();

	g_signal_connect (retval, "show", G_CALLBACK (setup_menu_panel), NULL);

	/* intercept all right button clicks makes sure they don't
	   go to the object itself */
	g_signal_connect (retval, "button_press_event",
			  G_CALLBACK (menu_dummy_button_press_event), NULL);

	return retval;
}

static void
icon_to_load_free (IconToLoad *icon)
{
	if (!icon)
		return;

	if (icon->pixmap)
		g_object_unref (icon->pixmap);
	icon->pixmap = NULL;

	if (icon->gicon)
		g_object_unref (icon->gicon);
	icon->gicon = NULL;

	g_free (icon->image);          icon->image = NULL;
	g_free (icon->fallback_image); icon->fallback_image = NULL;
	g_free (icon);
}

static IconToLoad *
icon_to_load_copy (IconToLoad *icon)
{
	IconToLoad *retval;

	if (!icon)
		return NULL;

	retval = g_new0 (IconToLoad, 1);

	retval->pixmap         = g_object_ref (icon->pixmap);
	if (icon->gicon)
		retval->gicon  = g_object_ref (icon->gicon);
	else
		retval->gicon  = NULL;
	retval->image          = g_strdup (icon->image);
	retval->fallback_image = g_strdup (icon->fallback_image);
	retval->stock_id       = icon->stock_id;
	retval->icon_size      = icon->icon_size;

	return retval;
}

static void
remove_pixmap_from_loaded (gpointer data, GObject *where_the_object_was)
{
	char *key = data;

	if (loaded_icons != NULL)
		g_hash_table_remove (loaded_icons, key);

	g_free (key);
}

GdkPixbuf *
panel_make_menu_icon (GtkIconTheme *icon_theme,
		      const char   *icon,
		      const char   *fallback,
		      int           size,
		      gboolean     *long_operation)
{
	GdkPixbuf *pb;
	char *file, *key;
	gboolean loaded;

	g_return_val_if_fail (size > 0, NULL);

	file = NULL;
	if (icon != NULL)
		file = panel_find_icon (icon_theme, icon, size);
	if (file == NULL && fallback != NULL)
		file = panel_find_icon (icon_theme, fallback, size);

	if (file == NULL)
		return NULL;

	if (long_operation != NULL)
		*long_operation = TRUE;

	pb = NULL;

	loaded = FALSE;

	key = g_strdup_printf ("%d:%s", size, file);

	if (loaded_icons != NULL &&
	    (pb = g_hash_table_lookup (loaded_icons, key)) != NULL) {
		if (pb != NULL)
			g_object_ref (G_OBJECT (pb));
	}

	if (pb == NULL) {
		pb = gdk_pixbuf_new_from_file (file, NULL);
		if (pb) {
			gint width, height;

			width = gdk_pixbuf_get_width (pb);
			height = gdk_pixbuf_get_height (pb);

			/* if we want 24 and we get 22, do nothing;
			 * else scale */
			if (!(size - 2 <= width && width <= size &&
                              size - 2 <= height && height <= size)) {
				GdkPixbuf *tmp;

				tmp = gdk_pixbuf_scale_simple (pb, size, size,
							       GDK_INTERP_BILINEAR);

				g_object_unref (pb);
				pb = tmp;
			}
		}

		/* add icon to the hash table so we don't load it again */
		loaded = TRUE;
	}

	if (pb == NULL) {
		g_free (file);
		g_free (key);
		return NULL;
	}

	if (loaded &&
	    (gdk_pixbuf_get_width (pb) != size &&
	     gdk_pixbuf_get_height (pb) != size)) {
		GdkPixbuf *pb2;
		int        dest_width;
		int        dest_height;
		int        width;
		int        height;

		width  = gdk_pixbuf_get_width (pb);
		height = gdk_pixbuf_get_height (pb);

		if (height > width) {
			dest_width  = (size * width) / height;
			dest_height = size;
		} else {
			dest_width  = size;
			dest_height = (size * height) / width;
		}

		pb2 = gdk_pixbuf_scale_simple (pb, dest_width, dest_height,
					       GDK_INTERP_BILINEAR);
		g_object_unref (G_OBJECT (pb));
		pb = pb2;
	}

	if (loaded) {
		if (loaded_icons == NULL)
			loaded_icons = g_hash_table_new_full
				(g_str_hash, g_str_equal,
				 (GDestroyNotify) g_free,
				 (GDestroyNotify) g_object_unref);
		g_hash_table_replace (loaded_icons,
				      g_strdup (key),
				      g_object_ref (G_OBJECT (pb)));
		g_object_weak_ref (G_OBJECT (pb),
				   (GWeakNotify) remove_pixmap_from_loaded,
				   g_strdup (key));
	} else {
		/* we didn't load from disk */
		if (long_operation != NULL)
			*long_operation = FALSE;
	}

	g_free (file);
	g_free (key);

	return pb;
}

static void
menu_item_style_set (GtkImage *image,
		     gpointer  data)
{
	GtkWidget   *widget;
	GdkPixbuf   *pixbuf;
	GtkIconSize  icon_size = (GtkIconSize) GPOINTER_TO_INT (data);
	int          icon_height;
	gboolean     is_mapped;

	if (!gtk_icon_size_lookup (icon_size, NULL, &icon_height))
		return;

	pixbuf = gtk_image_get_pixbuf (image);
	if (!pixbuf)
		return;

	if (gdk_pixbuf_get_height (pixbuf) == icon_height)
		return;

	widget = GTK_WIDGET (image);

	is_mapped = gtk_widget_get_mapped (widget);
	if (is_mapped)
		gtk_widget_unmap (widget);

	gtk_image_set_from_pixbuf (image, NULL);

	if (is_mapped)
		gtk_widget_map (widget);
}

static void
do_icons_to_add (void)
{
	while (icons_to_add) {
		IconToAdd *icon_to_add = icons_to_add->data;

		icons_to_add = g_list_delete_link (icons_to_add, icons_to_add);

		if (icon_to_add->stock_id) {
			gtk_image_set_from_stock (
				GTK_IMAGE (icon_to_add->image),
				icon_to_add->stock_id,
				icon_to_add->icon_size);
		} else if (icon_to_add->gicon) {
			gtk_image_set_from_gicon (
				GTK_IMAGE (icon_to_add->image),
				icon_to_add->gicon,
				icon_to_add->icon_size);
		} else {
			g_assert (icon_to_add->pixbuf);

			gtk_image_set_from_pixbuf (
				GTK_IMAGE (icon_to_add->image),
				icon_to_add->pixbuf);

			g_signal_connect (icon_to_add->image, "style-set",
					  G_CALLBACK (menu_item_style_set),
					  GINT_TO_POINTER (icon_to_add->icon_size));

			g_object_unref (icon_to_add->pixbuf);
		}

		if (icon_to_add->gicon)
			g_object_unref (icon_to_add->gicon);
		g_object_unref (icon_to_add->image);
		g_free (icon_to_add);
	}
}

static gboolean
load_icons_handler (gpointer data)
{
	IconToLoad *icon;
	gboolean    long_operation = FALSE;

load_icons_handler_again:

	if (!icons_to_load) {
		load_icons_id = 0;
		do_icons_to_add ();

		return FALSE;
	}

	icon = icons_to_load->data;
	icons_to_load->data = NULL;
	/* pop */
	icons_to_load = g_list_delete_link (icons_to_load, icons_to_load);

	/* if not visible anymore, just ignore */
	if ( ! gtk_widget_get_visible (icon->pixmap)) {
		icon_to_load_free (icon);
		/* we didn't do anything long/hard, so just do this again,
		 * this is fun, don't go back to main loop */
		goto load_icons_handler_again;
	}

	if (icon->stock_id || icon->gicon) {
		IconToAdd *icon_to_add;

		icon_to_add            = g_new (IconToAdd, 1);
		icon_to_add->image     = g_object_ref (icon->pixmap);
		icon_to_add->stock_id  = icon->stock_id;
		icon_to_add->pixbuf    = NULL;
		icon_to_add->icon_size = icon->icon_size;
		if (icon->gicon)
			icon_to_add->gicon = g_object_ref (icon->gicon);
		else
			icon_to_add->gicon = NULL;

		icons_to_add = g_list_prepend (icons_to_add, icon_to_add);
	} else {
		IconToAdd *icon_to_add;
		GdkPixbuf *pb;
		int        icon_height = PANEL_DEFAULT_MENU_ICON_SIZE;

		gtk_icon_size_lookup (icon->icon_size, NULL, &icon_height);

		pb = panel_make_menu_icon (icon->icon_theme,
					   icon->image,
					   icon->fallback_image,
					   icon_height,
					   &long_operation);
		if (!pb) {
			icon_to_load_free (icon);
			if (long_operation)
				/* this may have been a long operation so jump back to
				 * the main loop for a while */
				return TRUE;
			else
				/* we didn't do anything long/hard, so just do this again,
				 * this is fun, don't go back to main loop */
				goto load_icons_handler_again;
		}

		icon_to_add            = g_new (IconToAdd, 1);
		icon_to_add->image     = g_object_ref (icon->pixmap);
		icon_to_add->stock_id  = NULL;
		icon_to_add->gicon     = NULL;
		icon_to_add->pixbuf    = pb;
		icon_to_add->icon_size = icon->icon_size;

		icons_to_add = g_list_prepend (icons_to_add, icon_to_add);
	}

	icon_to_load_free (icon);

	if (!long_operation)
		/* we didn't do anything long/hard, so just do this again,
		 * this is fun, don't go back to main loop */
		goto load_icons_handler_again;

	/* if still more we'll come back */
	return TRUE;
}

static void
add_app_to_panel (GtkWidget      *item,
		  MateMenuTreeEntry *entry)
{
	PanelWidget   *panel_widget;
	PanelToplevel *toplevel;
	PanelData     *pd;
	int            position;

	panel_widget = menu_get_panel (item);
	toplevel = panel_widget->toplevel;

	pd = g_object_get_data (G_OBJECT (toplevel), "PanelData");
	position = pd ?  pd->insertion_pos : -1;

	panel_launcher_create (toplevel,
			       position,
			       matemenu_tree_entry_get_desktop_file_path (entry));
}


static void
add_app_to_desktop (GtkWidget      *item,
		    MateMenuTreeEntry *entry)
{
	char       *source_uri;
	const char *source;
	char       *target_dir;
	char       *target_uri;
	char       *target;
	GError     *error;

	g_return_if_fail (entry != NULL);

	if (desktop_is_home_dir ()) {
		target_dir = g_build_filename (g_get_home_dir (), NULL);
	} else {
		target_dir = g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP));
	}

	source = matemenu_tree_entry_get_desktop_file_path (entry);
	source_uri = g_filename_to_uri (source, NULL, NULL);

	target_uri = panel_make_unique_desktop_uri (target_dir, source_uri);
	g_free (target_dir);
	g_free (source_uri);

	g_return_if_fail (target_uri != NULL);

	target = g_filename_from_uri (target_uri, NULL, NULL);
	g_free (target_uri);

	error = NULL;
	panel_key_file_copy_and_mark_trusted (source, target, &error);

	g_free (target);

	if (error != NULL) {
		g_warning ("Problem while copying launcher to desktop: %s",
			   error->message);
		g_error_free (error);
	}
}


static void add_drawers_from_dir (MateMenuTreeDirectory *directory,
				  int                 pos,
				  const char         *toplevel_id);

static void
add_drawers_from_alias (MateMenuTreeAlias *alias,
			const char     *toplevel_id)
{
	MateMenuTreeItem *aliased_item;

	aliased_item = matemenu_tree_alias_get_item (alias);

	switch (matemenu_tree_item_get_type (aliased_item)) {
	case MATEMENU_TREE_ITEM_DIRECTORY:
		add_drawers_from_dir (MATEMENU_TREE_DIRECTORY (aliased_item),
				      G_MAXINT/2,
				      toplevel_id);
		break;

	case MATEMENU_TREE_ITEM_ENTRY:
		panel_launcher_create_with_id (toplevel_id,
					       G_MAXINT/2,
					       matemenu_tree_entry_get_desktop_file_path (MATEMENU_TREE_ENTRY (aliased_item)));
		break;

	default:
		break;
	}

	matemenu_tree_item_unref (aliased_item);
}

static void
add_drawers_from_dir (MateMenuTreeDirectory *directory,
		      int                 pos,
		      const char         *toplevel_id)
{
	const char *name;
	const char *icon;
	GSList     *items;
	GSList     *l;
	char       *attached_toplevel_id;

	name = matemenu_tree_directory_get_name (directory);
	icon = matemenu_tree_directory_get_icon (directory);

	attached_toplevel_id = panel_drawer_create_with_id (toplevel_id,
							    pos,
							    icon,
							    icon != NULL,
							    name);
	if (!attached_toplevel_id)
		return;

	items = matemenu_tree_directory_get_contents (directory);
	for (l = items; l; l = l->next) {
		MateMenuTreeItem *item = l->data;

		switch (matemenu_tree_item_get_type (item)) {
		case MATEMENU_TREE_ITEM_ENTRY:
			panel_launcher_create_with_id (attached_toplevel_id,
						       G_MAXINT/2,
						       matemenu_tree_entry_get_desktop_file_path (MATEMENU_TREE_ENTRY (item)));
			break;

		case MATEMENU_TREE_ITEM_DIRECTORY:
			add_drawers_from_dir (MATEMENU_TREE_DIRECTORY (item),
					      G_MAXINT/2,
					      attached_toplevel_id);
			break;

		case MATEMENU_TREE_ITEM_ALIAS:
			add_drawers_from_alias (MATEMENU_TREE_ALIAS (item), attached_toplevel_id);
			break;

		default:
			break;
		}

		matemenu_tree_item_unref (item);
	}

	g_slist_free (items);

	g_free (attached_toplevel_id);
}

static void
add_menudrawer_to_panel (GtkWidget      *menuitem,
			 MateMenuTreeEntry *entry)

{
	MateMenuTreeDirectory *directory;
	PanelWidget       *panel;
	PanelData         *pd;
	int                insertion_pos;

	directory = matemenu_tree_item_get_parent (MATEMENU_TREE_ITEM (entry));

	panel = menu_get_panel (menuitem);

	pd = g_object_get_data (G_OBJECT (panel->toplevel), "PanelData");
	insertion_pos = pd ? pd->insertion_pos : -1;

	add_drawers_from_dir (directory,
			      insertion_pos,
			      panel_profile_get_toplevel_id (panel->toplevel));

	matemenu_tree_item_unref (directory);
}

static void
add_menu_to_panel (GtkWidget      *menuitem,
		   MateMenuTreeEntry *entry)
{
	MateMenuTreeDirectory *directory;
	MateMenuTree          *tree;
	PanelWidget        *panel;
	PanelData          *pd;
	int                 insertion_pos;
	char               *menu_path;
	const char         *menu_filename;

	directory = matemenu_tree_item_get_parent (MATEMENU_TREE_ITEM (entry));
	if (!directory) {
		g_warning ("Cannot find the filename for the menu: no directory");
		return;
	}

	tree = matemenu_tree_directory_get_tree (directory);
	if (!tree) {
		matemenu_tree_item_unref (directory);
		g_warning ("Cannot find the filename for the menu: no tree");
		return;
	}

	menu_filename = matemenu_tree_get_menu_file (tree);
	matemenu_tree_unref (tree);
	if (!menu_filename) {
		matemenu_tree_item_unref (directory);
		g_warning ("Cannot find the filename for the menu: no filename");
		return;
	}

	panel = menu_get_panel (menuitem);

	pd = g_object_get_data (G_OBJECT (panel->toplevel), "PanelData");
	insertion_pos = pd ? pd->insertion_pos : -1;

	menu_path = matemenu_tree_directory_make_path (directory, NULL);

	panel_menu_button_create (panel->toplevel,
				  insertion_pos,
				  menu_filename,
				  menu_path,
				  TRUE,
				  matemenu_tree_directory_get_name (directory));

	g_free (menu_path);

	matemenu_tree_item_unref (directory);
}

/*most of this function stolen from the real gtk_menu_popup*/
static void
restore_grabs(GtkWidget *w, gpointer data)
{
	GtkWidget *menu_item = data;
	GtkMenu *menu = GTK_MENU (gtk_widget_get_parent (menu_item));
	GtkWidget *xgrab_shell;
	GtkWidget *parent;

	/* Find the last viewable ancestor, and make an X grab on it
	 */
	parent = GTK_WIDGET (menu);
	xgrab_shell = NULL;
	while (parent) {
		gboolean viewable = TRUE;
		GtkWidget *tmp = parent;

		while (tmp) {
			if (!gtk_widget_get_mapped (tmp)) {
				viewable = FALSE;
				break;
			}
			tmp = gtk_widget_get_parent (tmp);
		}

		if (viewable)
			xgrab_shell = parent;

		parent = GTK_MENU_SHELL (parent)->parent_menu_shell;
	}

	/*only grab if this HAD a grab before*/
	if (xgrab_shell && (GTK_MENU_SHELL (xgrab_shell)->have_xgrab))
          {
	    GdkWindow *window = gtk_widget_get_window (xgrab_shell);

	    if (gdk_pointer_grab (window, TRUE,
				  GDK_BUTTON_PRESS_MASK |
				  GDK_BUTTON_RELEASE_MASK |
				  GDK_ENTER_NOTIFY_MASK |
				  GDK_LEAVE_NOTIFY_MASK,
				  NULL, NULL, 0) == 0)
              {
		if (gdk_keyboard_grab (window, TRUE,
				       GDK_CURRENT_TIME) == 0)
		  GTK_MENU_SHELL (xgrab_shell)->have_xgrab = TRUE;
		else
		  gdk_pointer_ungrab (GDK_CURRENT_TIME);
	      }
         }

	gtk_grab_add (GTK_WIDGET (menu));
}

static void
menu_destroy_context_menu (GtkWidget *item,
			   GtkWidget *menu)
{
	g_signal_handlers_disconnect_by_func (menu, restore_grabs, item);
	gtk_widget_destroy (menu);
}

static GtkWidget *
create_item_context_menu (GtkWidget   *item,
			  PanelWidget *panel_widget)
{
	MateMenuTreeEntry     *entry;
	MateMenuTreeDirectory *directory;
	MateMenuTree          *tree;
	GtkWidget          *menu;
	GtkWidget          *submenu;
	GtkWidget          *menuitem;
	const char         *menu_filename;
	gboolean            id_lists_writable;

	id_lists_writable = panel_profile_id_lists_are_writable ();

	entry = g_object_get_data (G_OBJECT (item), "panel-menu-tree-entry");
	if (!entry)
		return NULL;

	directory = matemenu_tree_item_get_parent (MATEMENU_TREE_ITEM (entry));
	if (!directory)
		return NULL;

	tree = matemenu_tree_directory_get_tree (directory);
	matemenu_tree_item_unref (directory);
	if (!tree)
		return NULL;

	menu_filename = matemenu_tree_get_menu_file (tree);
	matemenu_tree_unref (tree);
	if (!menu_filename)
		return NULL;

	menu = create_empty_menu ();
	g_object_set_data (G_OBJECT (item), "panel-item-context-menu", menu);
	g_object_set_data (G_OBJECT (menu), "menu_panel", panel_widget);

	g_signal_connect (item, "destroy",
			  G_CALLBACK (menu_destroy_context_menu), menu);
	g_signal_connect (menu, "deactivate",
			  G_CALLBACK (restore_grabs), item);

	menuitem = gtk_menu_item_new_with_mnemonic (_("Add this launcher to _panel"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (add_app_to_panel), entry);
	gtk_widget_set_sensitive (menuitem, id_lists_writable);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	gtk_widget_show (menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic (_("Add this launcher to _desktop"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (add_app_to_desktop), entry);
	gtk_widget_set_sensitive (menuitem, id_lists_writable);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	gtk_widget_show (menuitem);


	submenu = create_empty_menu ();

	g_object_set_data (G_OBJECT (submenu), "menu_panel", panel_widget);

	menuitem = gtk_menu_item_new_with_mnemonic (_("_Entire menu"));
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	gtk_widget_show (menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic (_("Add this as _drawer to panel"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (add_menudrawer_to_panel), entry);
	gtk_widget_set_sensitive (menuitem, id_lists_writable);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), menuitem);
	gtk_widget_show (menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic (_("Add this as _menu to panel"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (add_menu_to_panel), entry);
	gtk_widget_set_sensitive (menuitem, id_lists_writable);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), menuitem);
	gtk_widget_show (menuitem);

	return menu;
}

static gboolean
show_item_menu (GtkWidget      *item,
		GdkEventButton *bevent)
{
	PanelWidget *panel_widget;
	GtkWidget   *menu;

	if (panel_lockdown_get_locked_down ())
		return FALSE;

	panel_widget = menu_get_panel (item);

	menu = g_object_get_data (G_OBJECT (item), "panel-item-context-menu");

	if (!menu)
		menu = create_item_context_menu (item, panel_widget);

	if (!menu)
		return FALSE;

	gtk_menu_set_screen (GTK_MENU (menu),
			     gtk_window_get_screen (GTK_WINDOW (panel_widget->toplevel)));

	gtk_menu_popup (GTK_MENU (menu),
			NULL, NULL, NULL, NULL,
			bevent->button,
			bevent->time);

	return TRUE;
}

gboolean
menu_dummy_button_press_event (GtkWidget      *menuitem,
			       GdkEventButton *event)
{
	if (event->button == 3)
		return TRUE;

	return FALSE;
}

static gboolean
menuitem_button_press_event (GtkWidget      *menuitem,
			     GdkEventButton *event)
{
	if (event->button == 3)
		return show_item_menu (menuitem, event);

	return FALSE;
}

static void
drag_begin_menu_cb (GtkWidget *widget, GdkDragContext     *context)
{
	/* FIXME: workaround for a possible gtk+ bug
	 *    See bugs #92085(gtk+) and #91184(panel) for details.
	 *    Maybe it's not needed with GtkTooltip?
	 */
	g_object_set (widget, "has-tooltip", FALSE, NULL);
}

/* This is a _horrible_ hack to have this here. This needs to be added to the
 * GTK+ menuing code in some manner.
 */
static void
drag_end_menu_cb (GtkWidget *widget, GdkDragContext     *context)
{
  GtkWidget *xgrab_shell;
  GtkWidget *parent;

  /* Find the last viewable ancestor, and make an X grab on it
   */
  parent = gtk_widget_get_parent (widget);
  xgrab_shell = NULL;

  /* FIXME: workaround for a possible gtk+ bug
   *    See bugs #92085(gtk+) and #91184(panel) for details.
   */
  g_object_set (widget, "has-tooltip", TRUE, NULL);

  while (parent)
    {
      gboolean viewable = TRUE;
      GtkWidget *tmp = parent;

      while (tmp)
	{
	  if (!gtk_widget_get_mapped (tmp))
	    {
	      viewable = FALSE;
	      break;
	    }
	  tmp = gtk_widget_get_parent (tmp);
	}

      if (viewable)
	xgrab_shell = parent;

      parent = GTK_MENU_SHELL (parent)->parent_menu_shell;
    }

  if (xgrab_shell && !gtk_menu_get_tearoff_state (GTK_MENU(xgrab_shell)))
    {
      GdkWindow *window = gtk_widget_get_window (xgrab_shell);
      GdkCursor *cursor = gdk_cursor_new (GDK_ARROW);

      if ((gdk_pointer_grab (window, TRUE,
			     GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			     GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
			     GDK_POINTER_MOTION_MASK,
			     NULL, cursor, GDK_CURRENT_TIME) == 0))
	{
	  if (gdk_keyboard_grab (window, TRUE,
				 GDK_CURRENT_TIME) == 0)
	    GTK_MENU_SHELL (xgrab_shell)->have_xgrab = TRUE;
	  else
	    {
	      gdk_pointer_ungrab (GDK_CURRENT_TIME);
	    }
	}

      gdk_cursor_unref (cursor);
    }
}

static void
drag_data_get_menu_cb (GtkWidget        *widget,
		       GdkDragContext   *context,
		       GtkSelectionData *selection_data,
		       guint             info,
		       guint             time,
		       MateMenuTreeEntry   *entry)
{
	const char *path;
	char       *uri;
	char       *uri_list;

	path = matemenu_tree_entry_get_desktop_file_path (entry);
	uri = g_filename_to_uri (path, NULL, NULL);
	uri_list = g_strconcat (uri, "\r\n", NULL);
	g_free (uri);

	gtk_selection_data_set (selection_data,
				gtk_selection_data_get_target (selection_data), 8, (guchar *)uri_list,
				strlen (uri_list));
	g_free (uri_list);
}

static void
image_menuitem_size_request (GtkWidget      *menuitem,
			     GtkRequisition *requisition,
			     gpointer        data)
{
	GtkIconSize icon_size = (GtkIconSize) GPOINTER_TO_INT (data);
	int         icon_height;
	int         req_height;

	if (!gtk_icon_size_lookup (icon_size, NULL, &icon_height))
		return;

	/* If we don't have a pixmap for this menuitem
	 * at least make sure its the same height as
	 * the rest.
	 * This is a bit ugly, since we should keep this in sync with what's in
	 * gtk_menu_item_size_request()
	 */
	req_height = icon_height;
	req_height += (gtk_container_get_border_width (GTK_CONTAINER (menuitem)) +
		       (gtk_widget_get_style (menuitem))->ythickness) * 2;
	requisition->height = MAX (requisition->height, req_height);
}

static char *
menu_escape_underscores_and_prepend (const char *text)
{
	GString    *escaped_text;
	const char *src;
	int         inserted;

	if (!text)
		return g_strdup (text);

	escaped_text = g_string_sized_new (strlen (text) + 1);
	g_string_printf (escaped_text, "_%s", text);

	src = text;
	inserted = 1;

	while (*src) {
		gunichar c;

		c = g_utf8_get_char (src);

		if (c == (gunichar)-1) {
			g_warning ("Invalid input string for underscore escaping");
			return g_strdup (text);
		} else if (c == '_') {
			g_string_insert_c (escaped_text,
					   src - text + inserted, '_');
			inserted++;
		}

		src = g_utf8_next_char (src);
	}

	return g_string_free (escaped_text, FALSE);
}

void
setup_menuitem (GtkWidget   *menuitem,
		GtkIconSize  icon_size,
		GtkWidget   *image,
		const char  *title)

{
	GtkWidget *label;
	char      *_title;

	/* this creates a label with an invisible mnemonic */
	label = g_object_new (GTK_TYPE_ACCEL_LABEL, NULL);
	_title = menu_escape_underscores_and_prepend (title);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (label), _title);
	g_free (_title);

	gtk_label_set_pattern (GTK_LABEL (label), "");

	gtk_accel_label_set_accel_widget (GTK_ACCEL_LABEL (label), menuitem);

	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_widget_show (label);

	gtk_container_add (GTK_CONTAINER (menuitem), label);

	if (image) {
		g_object_set_data_full (G_OBJECT (menuitem),
					"Panel:Image",
					g_object_ref (image),
					(GDestroyNotify) g_object_unref);
		gtk_widget_show (image);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem),
					       image);
	} else if (icon_size != GTK_ICON_SIZE_INVALID)
		g_signal_connect (menuitem, "size_request",
				  G_CALLBACK (image_menuitem_size_request),
				  GINT_TO_POINTER (icon_size));

	gtk_widget_show (menuitem);
}

static void
drag_data_get_string_cb (GtkWidget *widget, GdkDragContext     *context,
			 GtkSelectionData   *selection_data, guint info,
			 guint time, const char *string)
{
	gtk_selection_data_set (selection_data,
				gtk_selection_data_get_target (selection_data), 8, (guchar *)string,
				strlen(string));
}

void
setup_uri_drag (GtkWidget  *menuitem,
		const char *uri,
		const char *icon,
		GdkDragAction action)
{
	static GtkTargetEntry menu_item_targets[] = {
		{ "text/uri-list", 0, 0 }
	};

	if (panel_lockdown_get_locked_down ())
		return;

	gtk_drag_source_set (menuitem,
			     GDK_BUTTON1_MASK|GDK_BUTTON2_MASK,
			     menu_item_targets, 1,
			     action);

	if (icon != NULL)
		gtk_drag_source_set_icon_name (menuitem, icon);

	g_signal_connect (G_OBJECT (menuitem), "drag_begin",
			  G_CALLBACK (drag_begin_menu_cb), NULL);
	g_signal_connect_data (G_OBJECT (menuitem), "drag_data_get",
			       G_CALLBACK (drag_data_get_string_cb),
			       g_strdup (uri),
			       (GClosureNotify)g_free,
			       0 /* connect_flags */);
	g_signal_connect (G_OBJECT (menuitem), "drag_end",
			  G_CALLBACK (drag_end_menu_cb), NULL);
}

void
setup_internal_applet_drag (GtkWidget             *menuitem,
			    PanelActionButtonType  type)
{
	static GtkTargetEntry menu_item_targets[] = {
		{ "application/x-mate-panel-applet-internal", 0, 0 }
	};

	if (panel_lockdown_get_locked_down ())
		return;

	gtk_drag_source_set (menuitem,
			     GDK_BUTTON1_MASK|GDK_BUTTON2_MASK,
			     menu_item_targets, 1,
			     GDK_ACTION_COPY);

	if (panel_action_get_icon_name (type)  != NULL)
		gtk_drag_source_set_icon_name (menuitem,
					       panel_action_get_icon_name (type));

	g_signal_connect (G_OBJECT (menuitem), "drag_begin",
			  G_CALLBACK (drag_begin_menu_cb), NULL);
	g_signal_connect_data (G_OBJECT (menuitem), "drag_data_get",
			       G_CALLBACK (drag_data_get_string_cb),
			       g_strdup (panel_action_get_drag_id (type)),
			       (GClosureNotify)g_free,
			       0 /* connect_flags */);
	g_signal_connect (G_OBJECT (menuitem), "drag_end",
			  G_CALLBACK (drag_end_menu_cb), NULL);
}

static void
submenu_to_display (GtkWidget *menu)
{
	MateMenuTree           *tree;
	MateMenuTreeDirectory  *directory;
	const char          *menu_path;
	void               (*append_callback) (GtkWidget *, gpointer);
	gpointer             append_data;

	if (!g_object_get_data (G_OBJECT (menu), "panel-menu-needs-loading"))
		return;

	g_object_set_data (G_OBJECT (menu), "panel-menu-needs-loading", NULL);

	directory = g_object_get_data (G_OBJECT (menu),
				       "panel-menu-tree-directory");
	if (!directory) {
		menu_path = g_object_get_data (G_OBJECT (menu),
					       "panel-menu-tree-path");
		if (!menu_path)
			return;

		tree = g_object_get_data (G_OBJECT (menu), "panel-menu-tree");
		if (!tree)
			return;

		directory = matemenu_tree_get_directory_from_path (tree,
								menu_path);

		g_object_set_data_full (G_OBJECT (menu),
					"panel-menu-tree-directory",
					directory,
					(GDestroyNotify) matemenu_tree_item_unref);
	}

	if (directory)
		populate_menu_from_directory (menu, directory);

	append_callback = g_object_get_data (G_OBJECT (menu),
					     "panel-menu-append-callback");
	append_data     = g_object_get_data (G_OBJECT (menu),
					     "panel-menu-append-callback-data");
	if (append_callback)
		append_callback (menu, append_data);
}

static gboolean
submenu_to_display_in_idle (gpointer data)
{
	GtkWidget *menu = GTK_WIDGET (data);

	g_object_set_data (G_OBJECT (menu), "panel-menu-idle-id", NULL);

	submenu_to_display (menu);

	return FALSE;
}

static void
remove_submenu_to_display_idle (gpointer data)
{
	guint idle_id = GPOINTER_TO_UINT (data);

	g_source_remove (idle_id);
}

static GtkWidget *
create_fake_menu (MateMenuTreeDirectory *directory)
{
	GtkWidget *menu;
	guint      idle_id;

	menu = create_empty_menu ();

	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-tree-directory",
				matemenu_tree_item_ref (directory),
				(GDestroyNotify) matemenu_tree_item_unref);

	g_object_set_data (G_OBJECT (menu),
			   "panel-menu-needs-loading",
			   GUINT_TO_POINTER (TRUE));

	g_signal_connect (menu, "show",
			  G_CALLBACK (submenu_to_display), NULL);

	idle_id = g_idle_add_full (G_PRIORITY_LOW,
				   submenu_to_display_in_idle,
				   menu,
				   NULL);
	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-idle-id",
				GUINT_TO_POINTER (idle_id),
				remove_submenu_to_display_idle);

	g_signal_connect (menu, "button_press_event",
			  G_CALLBACK (menu_dummy_button_press_event), NULL);

	return menu;
}

GtkWidget *
panel_image_menu_item_new (void)
{
	GtkWidget *menuitem;

	menuitem = gtk_image_menu_item_new ();
	gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menuitem),
						   TRUE);
	return menuitem;
}

static GtkWidget *
create_submenu_entry (GtkWidget          *menu,
		      MateMenuTreeDirectory *directory)
{
	GtkWidget *menuitem;
	gboolean   force_categories_icon;

	force_categories_icon = g_object_get_data (G_OBJECT (menu),
						   "panel-menu-force-icon-for-categories") != NULL;

	if (force_categories_icon)
		menuitem = panel_image_menu_item_new ();
	else
		menuitem = gtk_image_menu_item_new ();

	panel_load_menu_image_deferred (menuitem,
					panel_menu_icon_get_size (),
					NULL, NULL,
					matemenu_tree_directory_get_icon (directory),
					PANEL_ICON_FOLDER);

	setup_menuitem (menuitem,
			panel_menu_icon_get_size (),
			NULL,
			matemenu_tree_directory_get_name (directory));

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	gtk_widget_show (menuitem);

	return menuitem;
}

static void
create_submenu (GtkWidget          *menu,
		MateMenuTreeDirectory *directory,
		MateMenuTreeDirectory *alias_directory)
{
	GtkWidget *menuitem;
	GtkWidget *submenu;
	gboolean   force_categories_icon;

	if (alias_directory)
		menuitem = create_submenu_entry (menu, alias_directory);
	else
		menuitem = create_submenu_entry (menu, directory);

	submenu = create_fake_menu (directory);

	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);

	/* Keep the infor that we force (or not) the icons to be visible */
	force_categories_icon = g_object_get_data (G_OBJECT (menu),
						   "panel-menu-force-icon-for-categories") != NULL;
	g_object_set_data (G_OBJECT (submenu),
			   "panel-menu-force-icon-for-categories",
			   GINT_TO_POINTER (force_categories_icon));
}

static void
create_header (GtkWidget       *menu,
	       MateMenuTreeHeader *header)
{
	MateMenuTreeDirectory *directory;
	GtkWidget          *menuitem;

	directory = matemenu_tree_header_get_directory (header);
	menuitem = create_submenu_entry (menu, directory);
	matemenu_tree_item_unref (directory);

	g_object_set_data_full (G_OBJECT (menuitem),
				"panel-matemenu-tree.header",
				matemenu_tree_item_ref (header),
				(GDestroyNotify) matemenu_tree_item_unref);

	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gtk_false), NULL);
}

static void
create_menuitem (GtkWidget          *menu,
		 MateMenuTreeEntry     *entry,
		 MateMenuTreeDirectory *alias_directory)
{
	GtkWidget  *menuitem;

	menuitem = panel_image_menu_item_new ();

	g_object_set_data_full (G_OBJECT (menuitem),
				"panel-menu-tree-entry",
				matemenu_tree_item_ref (entry),
				(GDestroyNotify) matemenu_tree_item_unref);

	if (alias_directory)
		//FIXME: we should probably use this data when we do dnd or
		//context menu for this menu item
		g_object_set_data_full (G_OBJECT (menuitem),
					"panel-menu-tree-alias-directory",
					matemenu_tree_item_ref (alias_directory),
					(GDestroyNotify) matemenu_tree_item_unref);

	panel_load_menu_image_deferred (menuitem,
					panel_menu_icon_get_size (),
					NULL, NULL,
					alias_directory ? matemenu_tree_directory_get_icon (alias_directory) :
							  matemenu_tree_entry_get_icon (entry),
					NULL);

	setup_menuitem (menuitem,
			panel_menu_icon_get_size (),
			NULL,
			alias_directory ? matemenu_tree_directory_get_name (alias_directory) :
					  matemenu_tree_entry_get_display_name (entry));

	if (alias_directory &&
	    matemenu_tree_directory_get_comment (alias_directory))
		panel_util_set_tooltip_text (menuitem,
					     matemenu_tree_directory_get_comment (alias_directory));
	else if	(!alias_directory &&
		 matemenu_tree_entry_get_comment (entry))
		panel_util_set_tooltip_text (menuitem,
					     matemenu_tree_entry_get_comment (entry));
	else if	(!alias_directory &&
		 matemenu_tree_entry_get_generic_name (entry))
		panel_util_set_tooltip_text (menuitem,
					     matemenu_tree_entry_get_generic_name (entry));

	g_signal_connect_after (menuitem, "button_press_event",
				G_CALLBACK (menuitem_button_press_event), NULL);

	if (!panel_lockdown_get_locked_down ()) {
		static GtkTargetEntry menu_item_targets[] = {
			{ "text/uri-list", 0, 0 }
		};

		gtk_drag_source_set (menuitem,
				     GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
				     menu_item_targets, 1,
				     GDK_ACTION_COPY);

		if (matemenu_tree_entry_get_icon (entry) != NULL) {
			const char *icon;
			char       *icon_no_ext;

			icon = matemenu_tree_entry_get_icon (entry);
			if (!g_path_is_absolute (icon)) {
				icon_no_ext = panel_xdg_icon_remove_extension (icon);
				gtk_drag_source_set_icon_name (menuitem,
							       icon_no_ext);
				g_free (icon_no_ext);
			}
		}

		g_signal_connect (G_OBJECT (menuitem), "drag_begin",
				  G_CALLBACK (drag_begin_menu_cb), NULL);
		g_signal_connect (menuitem, "drag_data_get",
				  G_CALLBACK (drag_data_get_menu_cb), entry);
		g_signal_connect (menuitem, "drag_end",
				  G_CALLBACK (drag_end_menu_cb), NULL);
	}

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (activate_app_def), entry);

	gtk_widget_show (menuitem);
}

static void
create_menuitem_from_alias (GtkWidget      *menu,
			    MateMenuTreeAlias *alias)
{
	MateMenuTreeItem *aliased_item;

	aliased_item = matemenu_tree_alias_get_item (alias);

	switch (matemenu_tree_item_get_type (aliased_item)) {
	case MATEMENU_TREE_ITEM_DIRECTORY:
		create_submenu (menu,
				MATEMENU_TREE_DIRECTORY (aliased_item),
				matemenu_tree_alias_get_directory (alias));
		break;

	case MATEMENU_TREE_ITEM_ENTRY:
		create_menuitem (menu,
				 MATEMENU_TREE_ENTRY (aliased_item),
				 matemenu_tree_alias_get_directory (alias));
		break;

	default:
		break;
	}

	matemenu_tree_item_unref (aliased_item);
}

static void
handle_matemenu_tree_changed (MateMenuTree *tree,
			   GtkWidget *menu)
{
	guint idle_id;

	while (GTK_MENU_SHELL (menu)->children)
                gtk_widget_destroy (GTK_MENU_SHELL (menu)->children->data);

	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-tree-directory",
				NULL, NULL);

	g_object_set_data (G_OBJECT (menu),
			   "panel-menu-needs-loading",
			   GUINT_TO_POINTER (TRUE));

	idle_id = g_idle_add_full (G_PRIORITY_LOW,
				   submenu_to_display_in_idle,
				   menu,
				   NULL);
	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-idle-id",
				GUINT_TO_POINTER (idle_id),
				remove_submenu_to_display_idle);
}

static void
remove_matemenu_tree_monitor (GtkWidget *menu,
			  MateMenuTree  *tree)
{
	matemenu_tree_remove_monitor (tree,
				  (MateMenuTreeChangedFunc) handle_matemenu_tree_changed,
				  menu);
}

GtkWidget *
create_applications_menu (const char *menu_file,
			  const char *menu_path,
			  gboolean    always_show_image)
{
	MateMenuTree *tree;
	GtkWidget *menu;
	guint      idle_id;

	menu = create_empty_menu ();

	if (always_show_image)
		g_object_set_data (G_OBJECT (menu),
				   "panel-menu-force-icon-for-categories",
				   GINT_TO_POINTER (TRUE));

	tree = matemenu_tree_lookup (menu_file, MATEMENU_TREE_FLAGS_NONE);
	matemenu_tree_set_sort_key (tree, MATEMENU_TREE_SORT_DISPLAY_NAME);

	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-tree",
				matemenu_tree_ref (tree),
				(GDestroyNotify) matemenu_tree_unref);

	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-tree-path",
				g_strdup (menu_path ? menu_path : "/"),
				(GDestroyNotify) g_free);

	g_object_set_data (G_OBJECT (menu),
			   "panel-menu-needs-loading",
			   GUINT_TO_POINTER (TRUE));

	g_signal_connect (menu, "show",
			  G_CALLBACK (submenu_to_display), NULL);

	idle_id = g_idle_add_full (G_PRIORITY_LOW,
				   submenu_to_display_in_idle,
				   menu,
				   NULL);
	g_object_set_data_full (G_OBJECT (menu),
				"panel-menu-idle-id",
				GUINT_TO_POINTER (idle_id),
				remove_submenu_to_display_idle);

	g_signal_connect (menu, "button_press_event",
			  G_CALLBACK (menu_dummy_button_press_event), NULL);

	matemenu_tree_add_monitor (tree,
			       (MateMenuTreeChangedFunc) handle_matemenu_tree_changed,
			       menu);
	g_signal_connect (menu, "destroy",
			  G_CALLBACK (remove_matemenu_tree_monitor), tree);

	matemenu_tree_unref (tree);

	return menu;
}

static GtkWidget *
populate_menu_from_directory (GtkWidget          *menu,
			      MateMenuTreeDirectory *directory)
{
	GList    *children;
	GSList   *l;
	GSList   *items;
	gboolean  add_separator;

	children = gtk_container_get_children (GTK_CONTAINER (menu));
	add_separator = (children != NULL);
	g_list_free (children);

	items = matemenu_tree_directory_get_contents (directory);

	for (l = items; l; l = l->next) {
		MateMenuTreeItem *item = l->data;

		if (add_separator ||
		    matemenu_tree_item_get_type (item) == MATEMENU_TREE_ITEM_SEPARATOR) {
			add_menu_separator (menu);
			add_separator = FALSE;
		}

		switch (matemenu_tree_item_get_type (item)) {
		case MATEMENU_TREE_ITEM_DIRECTORY:
			create_submenu (menu, MATEMENU_TREE_DIRECTORY (item), NULL);
			break;

		case MATEMENU_TREE_ITEM_ENTRY:
			create_menuitem (menu, MATEMENU_TREE_ENTRY (item), NULL);
			break;

		case MATEMENU_TREE_ITEM_SEPARATOR :
			/* already added */
			break;

		case MATEMENU_TREE_ITEM_ALIAS:
			create_menuitem_from_alias (menu, MATEMENU_TREE_ALIAS (item));
			break;

		case MATEMENU_TREE_ITEM_HEADER:
			create_header (menu, MATEMENU_TREE_HEADER (item));
			break;

		default:
			break;
		}

		matemenu_tree_item_unref (item);
	}

	g_slist_free (items);

	return menu;
}

void
setup_menu_item_with_icon (GtkWidget   *item,
			   GtkIconSize  icon_size,
			   const char  *icon_name,
			   const char  *stock_id,
			   GIcon       *gicon,
			   const char  *title)
{
	if (icon_name || gicon || stock_id)
		panel_load_menu_image_deferred (item, icon_size,
						stock_id, gicon,
						icon_name, NULL);

	setup_menuitem (item, icon_size, NULL, title);
}

static void
main_menu_append (GtkWidget *main_menu,
		  gpointer   data)
{
	PanelWidget *panel;
	GtkWidget   *item;
	gboolean     add_separator;
	GList       *children;
	GList       *last;

	panel = PANEL_WIDGET (data);

	add_separator = FALSE;
	children = gtk_container_get_children (GTK_CONTAINER (main_menu));
	last = g_list_last (children);
	if (last != NULL) {
		add_separator = !GTK_IS_SEPARATOR (GTK_WIDGET (last->data));
	}
	g_list_free (children);

	if (add_separator)
		add_menu_separator (main_menu);

	item = panel_place_menu_item_new (TRUE);
	panel_place_menu_item_set_panel (item, panel);
	gtk_menu_shell_append (GTK_MENU_SHELL (main_menu), item);
	gtk_widget_show (item);

	item = panel_desktop_menu_item_new (TRUE, FALSE);
	panel_desktop_menu_item_set_panel (item, panel);
	gtk_menu_shell_append (GTK_MENU_SHELL (main_menu), item);
	gtk_widget_show (item);

	panel_menu_items_append_lock_logout (main_menu);
}

GtkWidget* create_main_menu(PanelWidget* panel)
{
	GtkWidget* main_menu;

	main_menu = create_applications_menu("mate-applications.menu", NULL, TRUE);

	g_object_set_data(G_OBJECT(main_menu), "menu_panel", panel);
	/* FIXME need to update the panel on parent_set */

	g_object_set_data(G_OBJECT(main_menu), "panel-menu-append-callback", main_menu_append);
	g_object_set_data(G_OBJECT(main_menu), "panel-menu-append-callback-data", panel);

	return main_menu;
}

static GList *
find_in_load_list (GtkWidget *image)
{
	GList *li;
	for (li = icons_to_load; li != NULL; li = li->next) {
		IconToLoad *icon = li->data;
		if (icon->pixmap == image)
			return li;
	}
	return NULL;
}

static void
image_menu_shown (GtkWidget *image, gpointer data)
{
	IconToLoad *new_icon;
	IconToLoad *icon;

	icon = (IconToLoad *) data;

	/* if we've already handled this */
	if (gtk_image_get_storage_type (GTK_IMAGE (image)) != GTK_IMAGE_EMPTY)
		return;

	if (find_in_load_list (image) == NULL) {
		new_icon = icon_to_load_copy (icon);
		new_icon->icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (image));
		icons_to_load = g_list_append (icons_to_load, new_icon);
	}
	if (load_icons_id == 0)
		load_icons_id = g_idle_add (load_icons_handler, NULL);
}

static void
image_menu_destroy (GtkWidget *image, gpointer data)
{
	image_menu_items = g_slist_remove (image_menu_items, image);
}

static void
panel_load_menu_image_deferred (GtkWidget   *image_menu_item,
				GtkIconSize  icon_size,
				const char  *stock_id,
				GIcon       *gicon,
				const char  *image_filename,
				const char  *fallback_image_filename)
{
	IconToLoad *icon;
	GtkWidget *image;
	int        icon_height = PANEL_DEFAULT_MENU_ICON_SIZE;

	icon = g_new (IconToLoad, 1);

	gtk_icon_size_lookup (icon_size, NULL, &icon_height);

	image = gtk_image_new ();
	gtk_widget_set_size_request (image, icon_height, icon_height);

	/* this takes over the floating ref */
	icon->pixmap = g_object_ref_sink (G_OBJECT (image));

	icon->stock_id       = stock_id;
	if (gicon)
		icon->gicon  = g_object_ref (gicon);
	else
		icon->gicon  = NULL;
	icon->image          = g_strdup (image_filename);
	icon->fallback_image = g_strdup (fallback_image_filename);
	icon->icon_size      = icon_size;

	gtk_widget_show (image);

	g_object_set_data_full (G_OBJECT (image_menu_item),
				"Panel:Image",
				g_object_ref (image),
				(GDestroyNotify) g_object_unref);

	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (image_menu_item),
				       image);

	g_signal_connect_data (image, "map",
			       G_CALLBACK (image_menu_shown), icon,
			       (GClosureNotify) icon_to_load_free, 0);

	g_signal_connect (image, "destroy",
			  G_CALLBACK (image_menu_destroy), NULL);

	image_menu_items = g_slist_prepend (image_menu_items, image);
}

static gboolean
panel_menu_key_press_handler (GtkWidget   *widget,
			      GdkEventKey *event)
{
	gboolean retval = FALSE;

	if ((event->keyval == GDK_Menu) ||
	    (event->keyval == GDK_F10 &&
	    (event->state & gtk_accelerator_get_default_mod_mask ()) == GDK_SHIFT_MASK)) {
		GtkMenuShell *menu_shell = GTK_MENU_SHELL (widget);

		if (menu_shell->active_menu_item &&
		    GTK_MENU_ITEM (menu_shell->active_menu_item)->submenu == NULL) {
			GdkEventButton bevent;

			bevent.button = 3;
			bevent.time = GDK_CURRENT_TIME;
			retval = show_item_menu (menu_shell->active_menu_item,
						 &bevent);
		}

	}
	return retval;
}
