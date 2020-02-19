/*
 * panel-addto.c:
 *
 * Copyright (C) 2004 Vincent Untz
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
 *	Vincent Untz <vincent@vuntz.net>
 */

#include <config.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

#include <matemenu-tree.h>

#include <libpanel-util/panel-glib.h>
#include <libpanel-util/panel-show.h>
#include <libpanel-util/panel-gtk.h>

#include "launcher.h"
#include "panel.h"
#include "drawer.h"
#include "panel-applets-manager.h"
#include "panel-applet-frame.h"
#include "panel-action-button.h"
#include "panel-menu-bar.h"
#include "panel-separator.h"
#include "panel-toplevel.h"
#include "panel-menu-button.h"
#include "panel-globals.h"
#include "panel-lockdown.h"
#include "panel-util.h"
#include "panel-profile.h"
#include "panel-addto.h"
#include "panel-icon-names.h"
#include "panel-schemas.h"
#include "panel-stock-icons.h"

#ifdef HAVE_X11
#include "xstuff.h"
#endif
#ifdef HAVE_WAYLAND
#include "gdk/gdkwayland.h"
#endif

typedef struct {
	PanelWidget *panel_widget;

	GtkWidget    *addto_dialog;
	GtkWidget    *label;
	GtkWidget    *search_entry;
	GtkWidget    *back_button;
	GtkWidget    *add_button;
	GtkWidget    *tree_view;
	GtkTreeModel *applet_model;
	GtkTreeModel *filter_applet_model;
	GtkTreeModel *application_model;
	GtkTreeModel *filter_application_model;

	MateMenuTree    *menu_tree;

	GSList       *applet_list;
	GSList       *application_list;
	GSList       *settings_list;

	gchar        *search_text;
	gchar        *applet_search_text;

	int           insertion_position;
} PanelAddtoDialog;

static GQuark panel_addto_dialog_quark = 0;

typedef enum {
	PANEL_ADDTO_APPLET,
	PANEL_ADDTO_ACTION,
	PANEL_ADDTO_LAUNCHER_MENU,
	PANEL_ADDTO_LAUNCHER,
	PANEL_ADDTO_LAUNCHER_NEW,
	PANEL_ADDTO_MENU,
	PANEL_ADDTO_MENUBAR,
	PANEL_ADDTO_SEPARATOR,
	PANEL_ADDTO_DRAWER
} PanelAddtoItemType;

typedef struct {
	PanelAddtoItemType     type;
	char                  *name;
	char                  *description;
	char                  *icon;
	PanelActionButtonType  action_type;
	char                  *launcher_path;
	char                  *menu_filename;
	char                  *menu_path;
	char                  *iid;
	gboolean               enabled;
	gboolean               static_data;
} PanelAddtoItemInfo;

typedef struct {
	GSList             *children;
	PanelAddtoItemInfo  item_info;
} PanelAddtoAppList;

static PanelAddtoItemInfo special_addto_items [] = {

	{ PANEL_ADDTO_LAUNCHER_NEW,
	  N_("Custom Application Launcher"),
	  N_("Create a new launcher"),
	  PANEL_ICON_LAUNCHER,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "LAUNCHER:ASK",
	  TRUE,
	  TRUE },

	{ PANEL_ADDTO_LAUNCHER_MENU,
	  N_("Application Launcher..."),
	  N_("Copy a launcher from the applications menu"),
	  PANEL_ICON_LAUNCHER,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "LAUNCHER:MENU",
	  TRUE,
	  TRUE }

};

static PanelAddtoItemInfo internal_addto_items [] = {

	{ PANEL_ADDTO_MENU,
	  N_("Compact Menu"),
	  N_("A compact menu"),
	  PANEL_ICON_MAIN_MENU,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "MENU:MAIN",
	  TRUE,
	  TRUE },

	{ PANEL_ADDTO_MENUBAR,
	  N_("Classic Menu"),
	  N_("The classic Applications, Places and System menu bar"),
	  PANEL_ICON_MAIN_MENU,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "MENUBAR:NEW",
	  TRUE,
	  TRUE },

	{ PANEL_ADDTO_SEPARATOR,
	  N_("Separator"),
	  N_("A separator to organize the panel items"),
	  PANEL_ICON_SEPARATOR,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "SEPARATOR:NEW",
	  TRUE,
	  TRUE },

	{ PANEL_ADDTO_DRAWER,
	  N_("Drawer"),
	  N_("A pop out drawer to store other items in"),
	  PANEL_ICON_DRAWER,
	  PANEL_ACTION_NONE,
	  NULL,
	  NULL,
	  NULL,
	  "DRAWER:NEW",
	  TRUE,
	  TRUE }
};

enum {
	COLUMN_ICON_NAME,
	COLUMN_TEXT,
	COLUMN_DATA,
	COLUMN_SEARCH,
	COLUMN_ENABLED,
	NUMBER_COLUMNS
};

enum {
	PANEL_ADDTO_RESPONSE_BACK,
	PANEL_ADDTO_RESPONSE_ADD
};

static void panel_addto_present_applications (PanelAddtoDialog *dialog);
static void panel_addto_present_applets      (PanelAddtoDialog *dialog);
static gboolean panel_addto_filter_func (GtkTreeModel *model,
					 GtkTreeIter  *iter,
					 gpointer      data);

static int
panel_addto_applet_info_sort_func (PanelAddtoItemInfo *a,
				   PanelAddtoItemInfo *b)
{
	return g_utf8_collate (a->name, b->name);
}

static GSList *
panel_addto_prepend_internal_applets (GSList *list)
{
	static gboolean translated = FALSE;
	int             i;

	for (i = 0; i < G_N_ELEMENTS (internal_addto_items); i++) {
		if (!translated) {
			internal_addto_items [i].name        = _(internal_addto_items [i].name);
			internal_addto_items [i].description = _(internal_addto_items [i].description);
		}

                list = g_slist_prepend (list, &internal_addto_items [i]);
        }

	translated = TRUE;

	for (i = PANEL_ACTION_LOCK; i < PANEL_ACTION_LAST; i++) {
		PanelAddtoItemInfo *info;

		if (panel_action_get_is_disabled (i))
			continue;

		info              = g_new0 (PanelAddtoItemInfo, 1);
		info->type        = PANEL_ADDTO_ACTION;
		info->action_type = i;
		info->name        = g_strdup (panel_action_get_text (i));
		info->description = g_strdup (panel_action_get_tooltip (i));
		info->icon        = g_strdup (panel_action_get_icon_name (i));
		info->iid         = g_strdup (panel_action_get_drag_id (i));
		info->enabled     = TRUE;
		info->static_data = FALSE;

		list = g_slist_prepend (list, info);
	}

        return list;
}

static char *
panel_addto_make_text (const char *name,
		       const char *desc)
{
	const char *real_name;
	char       *result;

	real_name = name ? name : _("(empty)");

	if (!PANEL_GLIB_STR_EMPTY (desc)) {
		result = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>\n%s",
						  real_name, desc);
	} else {
		result = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>",
						  real_name);
	}

	return result;
}

static void
panel_addto_drag_data_get_cb (GtkWidget        *widget,
			      GdkDragContext   *context,
			      GtkSelectionData *selection_data,
			      guint             info,
			      guint             time,
			      const char       *string)
{
	gtk_selection_data_set (selection_data,
				gtk_selection_data_get_target (selection_data), 8,
				(guchar *) string, strlen (string));
}

static void
panel_addto_drag_begin_cb (GtkWidget      *widget,
			   GdkDragContext *context,
			   gpointer        data)
{
	GtkTreeModel *filter_model;
	GtkTreeModel *child_model;
	GtkTreePath  *path;
	GtkTreeIter   iter;
	GtkTreeIter   filter_iter;
	gchar         *icon_name;

	filter_model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &path, NULL);
	gtk_tree_model_get_iter (filter_model, &filter_iter, path);
	gtk_tree_path_free (path);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (filter_model),
							  &iter, &filter_iter);

	child_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));
	gtk_tree_model_get (child_model, &iter,
	                    COLUMN_ICON_NAME, &icon_name,
	                    -1);

	gtk_drag_set_icon_name (context, icon_name, 0, 0);
	g_free (icon_name);
}

static void
panel_addto_setup_drag (GtkTreeView          *tree_view,
			const GtkTargetEntry *target,
			const char           *text)
{
	if (!text || panel_lockdown_get_locked_down ())
		return;

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
						GDK_BUTTON1_MASK|GDK_BUTTON2_MASK,
						target, 1, GDK_ACTION_COPY);

	g_signal_connect_data (G_OBJECT (tree_view), "drag_data_get",
			       G_CALLBACK (panel_addto_drag_data_get_cb),
			       g_strdup (text),
			       (GClosureNotify) g_free,
			       0 /* connect_flags */);
	g_signal_connect_after (G_OBJECT (tree_view), "drag-begin",
	                        G_CALLBACK (panel_addto_drag_begin_cb),
	                        NULL);
}

static void
panel_addto_setup_launcher_drag (GtkTreeView *tree_view,
				 const char  *path)
{
        static GtkTargetEntry target[] = {
		{ "text/uri-list", 0, 0 }
	};
	char *uri;
	char *uri_list;

	uri = g_filename_to_uri (path, NULL, NULL);
	uri_list = g_strconcat (uri, "\r\n", NULL);
	panel_addto_setup_drag (tree_view, target, uri_list);
	g_free (uri_list);
	g_free (uri);
}

static void
panel_addto_setup_applet_drag (GtkTreeView *tree_view,
			       const char  *iid)
{
	static GtkTargetEntry target[] = {
		{ "application/x-mate-panel-applet-iid", 0, 0 }
	};

	panel_addto_setup_drag (tree_view, target, iid);
}

static void
panel_addto_setup_internal_applet_drag (GtkTreeView *tree_view,
					const char  *applet_type)
{
	static GtkTargetEntry target[] = {
		{ "application/x-mate-panel-applet-internal", 0, 0 }
	};

	panel_addto_setup_drag (tree_view, target, applet_type);
}

static GSList *
panel_addto_query_applets (GSList *list)
{
	GList *applet_list, *l;

	applet_list = mate_panel_applets_manager_get_applets ();

	for (l = applet_list; l; l = g_list_next (l)) {
		MatePanelAppletInfo *info;
		const char *iid, *name, *description, *icon;
		gboolean enabled;
		PanelAddtoItemInfo *applet;

		info = (MatePanelAppletInfo *)l->data;

		iid = mate_panel_applet_info_get_iid (info);
		name = mate_panel_applet_info_get_name (info);
		description = mate_panel_applet_info_get_description (info);
		icon = mate_panel_applet_info_get_icon (info);

		if (!name || panel_lockdown_is_applet_disabled (iid)) {
			continue;
		}

		enabled = TRUE;
#ifdef HAVE_X11
		if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()) &&
		    !mate_panel_applet_info_get_x11_supported (info)) {
			enabled = FALSE;
			description = _("Not compatible with X11");
		}
#endif
#ifdef HAVE_WAYLAND
		if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()) &&
		    !mate_panel_applet_info_get_wayland_supported (info)) {
			enabled = FALSE;
			description = _("Not compatible with Wayland");
		}
#endif

		applet = g_new0 (PanelAddtoItemInfo, 1);
		applet->type = PANEL_ADDTO_APPLET;
		applet->name = g_strdup (name);
		applet->description = g_strdup (description);
		applet->icon = g_strdup (icon);
		applet->iid = g_strdup (iid);
		applet->enabled = enabled;
		applet->static_data = FALSE;

		list = g_slist_prepend (list, applet);
	}

	g_list_free (applet_list);

	return list;
}

static void
panel_addto_append_item (PanelAddtoDialog *dialog,
			 GtkListStore *model,
			 PanelAddtoItemInfo *applet)
{
	char *text;
	GtkTreeIter iter;

	if (applet == NULL) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    COLUMN_ICON_NAME, NULL,
				    COLUMN_TEXT, NULL,
				    COLUMN_DATA, NULL,
				    COLUMN_SEARCH, NULL,
				    COLUMN_ENABLED, TRUE,
				    -1);
	} else {
		gtk_list_store_append (model, &iter);

		text = panel_addto_make_text (applet->name,
					      applet->description);

		gtk_list_store_set (model, &iter,
				    COLUMN_ICON_NAME, applet->icon,
				    COLUMN_TEXT, text,
				    COLUMN_DATA, applet,
				    COLUMN_SEARCH, applet->name,
				    COLUMN_ENABLED, applet->enabled,
				    -1);

		g_free (text);
	}
}

static void
panel_addto_append_special_applets (PanelAddtoDialog *dialog,
				    GtkListStore *model)
{
	static gboolean translated = FALSE;
	int i;

	for (i = 0; i < G_N_ELEMENTS (special_addto_items); i++) {
		if (!translated) {
			special_addto_items [i].name = _(special_addto_items [i].name);
			special_addto_items [i].description = _(special_addto_items [i].description);
		}

		if (special_addto_items [i].type == PANEL_ADDTO_LAUNCHER_NEW
		    && panel_lockdown_get_disable_command_line ())
			continue;

		panel_addto_append_item (dialog, model, &special_addto_items [i]);
	}

	translated = TRUE;
}

static void
panel_addto_make_applet_model (PanelAddtoDialog *dialog)
{
	GtkListStore *model;
	GSList       *l;

	if (dialog->filter_applet_model != NULL)
		return;

	if (panel_profile_id_lists_are_writable ()) {
		dialog->applet_list = panel_addto_query_applets (dialog->applet_list);
		dialog->applet_list = panel_addto_prepend_internal_applets (dialog->applet_list);
	}

	dialog->applet_list = g_slist_sort (dialog->applet_list,
					    (GCompareFunc) panel_addto_applet_info_sort_func);

	model = gtk_list_store_new (NUMBER_COLUMNS,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_POINTER,
				    G_TYPE_STRING,
				    G_TYPE_BOOLEAN);

	if (panel_profile_id_lists_are_writable ()) {
		panel_addto_append_special_applets (dialog, model);
		if (dialog->applet_list)
			panel_addto_append_item (dialog, model, NULL);
	}

	for (l = dialog->applet_list; l; l = l->next)
		panel_addto_append_item (dialog, model, l->data);

	dialog->applet_model = GTK_TREE_MODEL (model);
	dialog->filter_applet_model = gtk_tree_model_filter_new (GTK_TREE_MODEL (dialog->applet_model),
								 NULL);
	gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (dialog->filter_applet_model),
						panel_addto_filter_func,
						dialog, NULL);
}

static void panel_addto_make_application_list (GSList             **parent_list,
					       MateMenuTreeDirectory  *directory,
					       const char          *filename);

static void
panel_addto_prepend_directory (GSList             **parent_list,
			       MateMenuTreeDirectory  *directory,
			       const char          *filename)
{
	PanelAddtoAppList *data;
	GIcon              *gicon;

	data = g_new0 (PanelAddtoAppList, 1);
	gicon = matemenu_tree_directory_get_icon (directory);

	data->item_info.type          = PANEL_ADDTO_MENU;
	data->item_info.name          = g_strdup (matemenu_tree_directory_get_name (directory));
	data->item_info.description   = g_strdup (matemenu_tree_directory_get_comment (directory));
	data->item_info.icon          = gicon ? g_icon_to_string(gicon) : g_strdup(PANEL_ICON_UNKNOWN);
	data->item_info.menu_filename = g_strdup (filename);
	data->item_info.menu_path     = matemenu_tree_directory_make_path (directory, NULL);
	data->item_info.enabled       = TRUE;
	data->item_info.static_data   = FALSE;

	/* We should set the iid here to something and do
	 * iid = g_strdup_printf ("MENU:%s", tfr->name)
	 * but this means we'd have to free the iid later
	 * and this would complexify too much the free
	 * function.
	 * So the iid is built when we select the row.
	 */

	*parent_list = g_slist_prepend (*parent_list, data);

	panel_addto_make_application_list (&data->children, directory, filename);
}

static void
panel_addto_prepend_entry (GSList         **parent_list,
			   MateMenuTreeEntry  *entry,
			   const char      *filename)
{
	PanelAddtoAppList *data;
	GDesktopAppInfo    *ginfo;
	GIcon              *gicon;

	ginfo = matemenu_tree_entry_get_app_info (entry);
	gicon = g_app_info_get_icon(G_APP_INFO(ginfo));

	data = g_new0 (PanelAddtoAppList, 1);

	data->item_info.type          = PANEL_ADDTO_LAUNCHER;
	data->item_info.name          = g_strdup (g_app_info_get_display_name(G_APP_INFO(ginfo)));
	data->item_info.description   = g_strdup (g_app_info_get_description(G_APP_INFO(ginfo)));
	data->item_info.icon          = gicon ? g_icon_to_string(gicon) : g_strdup(PANEL_ICON_UNKNOWN);
	data->item_info.launcher_path = g_strdup (matemenu_tree_entry_get_desktop_file_path (entry));
	data->item_info.enabled       = TRUE;
	data->item_info.static_data   = FALSE;

	*parent_list = g_slist_prepend (*parent_list, data);
}

static void
panel_addto_prepend_alias (GSList         **parent_list,
			   MateMenuTreeAlias  *alias,
			   const char      *filename)
{
	gpointer item;

	switch (matemenu_tree_alias_get_aliased_item_type (alias)) {
	case MATEMENU_TREE_ITEM_DIRECTORY:
		item = matemenu_tree_alias_get_directory(alias);
		panel_addto_prepend_directory (parent_list,
				item,
				filename);
		matemenu_tree_item_unref (item);
		break;

	case MATEMENU_TREE_ITEM_ENTRY:
		item = matemenu_tree_alias_get_aliased_entry(alias);
		panel_addto_prepend_entry (parent_list,
				item,
				filename);
		matemenu_tree_item_unref (item);
		break;

	default:
		break;
	}
}

static void
panel_addto_make_application_list (GSList             **parent_list,
				   MateMenuTreeDirectory  *directory,
				   const char          *filename)
{
	MateMenuTreeIter *iter;
	iter = matemenu_tree_directory_iter (directory);
	MateMenuTreeItemType type;
	while ((type = matemenu_tree_iter_next (iter)) != MATEMENU_TREE_ITEM_INVALID) {
		gpointer item;
		switch (type) {
		case MATEMENU_TREE_ITEM_DIRECTORY:
			item = matemenu_tree_iter_get_directory(iter);
			panel_addto_prepend_directory (parent_list, item, filename);
			matemenu_tree_item_unref (item);
			break;

		case MATEMENU_TREE_ITEM_ENTRY:
			item = matemenu_tree_iter_get_entry (iter);
			panel_addto_prepend_entry (parent_list, item, filename);
			matemenu_tree_item_unref (item);
			break;

		case MATEMENU_TREE_ITEM_ALIAS:
			item = matemenu_tree_iter_get_alias(iter);
			panel_addto_prepend_alias (parent_list, item, filename);
			matemenu_tree_item_unref (item);
			break;
		default:
			break;
		}
	}
	matemenu_tree_iter_unref (iter);

	*parent_list = g_slist_reverse (*parent_list);
}

static void
panel_addto_populate_application_model (GtkTreeStore *store,
					GtkTreeIter  *parent,
					GSList       *app_list)
{
	PanelAddtoAppList *data;
	GtkTreeIter        iter;
	char              *text;
	GSList            *app;

	for (app = app_list; app != NULL; app = app->next) {
		data = app->data;
		gtk_tree_store_append (store, &iter, parent);

		text = panel_addto_make_text (data->item_info.name,
					      data->item_info.description);
		gtk_tree_store_set (store, &iter,
				    COLUMN_ICON_NAME, data->item_info.icon,
				    COLUMN_TEXT, text,
				    COLUMN_DATA, &(data->item_info),
				    COLUMN_SEARCH, data->item_info.name,
				    COLUMN_ENABLED, data->item_info.enabled,
				    -1);

		g_free (text);

		if (data->children != NULL)
			panel_addto_populate_application_model (store,
								&iter,
								data->children);
	}
}

static void panel_addto_make_application_model(PanelAddtoDialog* dialog)
{
	GtkTreeStore* store;
	MateMenuTree* tree;
	MateMenuTreeDirectory* root;
	GError *error = NULL;

	if (dialog->filter_application_model != NULL)
		return;

	store = gtk_tree_store_new (NUMBER_COLUMNS,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_POINTER,
				    G_TYPE_STRING,
				    G_TYPE_BOOLEAN);

	tree = matemenu_tree_new ("mate-applications.menu", MATEMENU_TREE_FLAGS_SORT_DISPLAY_NAME);
	if (! matemenu_tree_load_sync (tree, &error)) {
		g_warning("Applications menu tree loading got error:%s\n", error->message);
		g_error_free(error);
		g_clear_object(&tree);
	}

	if ((root = matemenu_tree_get_root_directory (tree)) != NULL )
	{
		panel_addto_make_application_list(&dialog->application_list, root, "mate-applications.menu");
		panel_addto_populate_application_model(store, NULL, dialog->application_list);

		matemenu_tree_item_unref(root);
	}

	g_clear_object(&tree);

	tree = matemenu_tree_new ("mate-settings.menu", MATEMENU_TREE_FLAGS_SORT_DISPLAY_NAME);
	if (! matemenu_tree_load_sync (tree, &error)) {
		g_warning("Settings menu tree loading got error:%s\n", error->message);
		g_error_free(error);
		g_clear_object(&tree);
	}

	if ((root = matemenu_tree_get_root_directory(tree)))
	{
		GtkTreeIter iter;

		gtk_tree_store_append(store, &iter, NULL);
		gtk_tree_store_set (store, &iter,
				    COLUMN_ICON_NAME, NULL,
				    COLUMN_TEXT, NULL,
				    COLUMN_DATA, NULL,
				    COLUMN_SEARCH, NULL,
				    COLUMN_ENABLED, TRUE,
				    -1);

		panel_addto_make_application_list(&dialog->settings_list, root, "mate-settings.menu");
		panel_addto_populate_application_model(store, NULL, dialog->settings_list);

		matemenu_tree_item_unref(root);
	}

	g_object_unref(tree);

	dialog->application_model = GTK_TREE_MODEL(store);
	dialog->filter_application_model = gtk_tree_model_filter_new(GTK_TREE_MODEL(dialog->application_model), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(dialog->filter_application_model), panel_addto_filter_func, dialog, NULL);
}

static void
panel_addto_add_item (PanelAddtoDialog   *dialog,
	 	      PanelAddtoItemInfo *item_info)
{
	g_assert (item_info != NULL);

	switch (item_info->type) {
	case PANEL_ADDTO_APPLET:
		mate_panel_applet_frame_create (dialog->panel_widget->toplevel,
					   dialog->insertion_position,
					   item_info->iid);
		break;
	case PANEL_ADDTO_ACTION:
		panel_action_button_create (dialog->panel_widget->toplevel,
					    dialog->insertion_position,
					    item_info->action_type);
		break;
	case PANEL_ADDTO_LAUNCHER_MENU:
		panel_addto_present_applications (dialog);
		break;
	case PANEL_ADDTO_LAUNCHER:
		panel_launcher_create (dialog->panel_widget->toplevel,
				       dialog->insertion_position,
				       item_info->launcher_path);
		break;
	case PANEL_ADDTO_LAUNCHER_NEW:
		ask_about_launcher (NULL, dialog->panel_widget,
				    dialog->insertion_position, FALSE);
		break;
	case PANEL_ADDTO_MENU:
		panel_menu_button_create (dialog->panel_widget->toplevel,
					  dialog->insertion_position,
					  item_info->menu_filename,
					  item_info->menu_path,
					  item_info->menu_path != NULL,
					  item_info->name);
		break;
	case PANEL_ADDTO_MENUBAR:
		panel_menu_bar_create (dialog->panel_widget->toplevel,
				       dialog->insertion_position);
		break;
	case PANEL_ADDTO_SEPARATOR:
		panel_separator_create (dialog->panel_widget->toplevel,
					dialog->insertion_position);
		break;
	case PANEL_ADDTO_DRAWER:
		panel_drawer_create (dialog->panel_widget->toplevel,
				     dialog->insertion_position,
				     NULL, FALSE, NULL);
		break;
	}
}

static void
panel_addto_dialog_response (GtkWidget *widget_dialog,
			     guint response_id,
			     PanelAddtoDialog *dialog)
{
	GtkTreeSelection *selection;
	GtkTreeModel     *filter_model;
	GtkTreeModel     *child_model;
	GtkTreeIter       iter;
	GtkTreeIter       filter_iter;

	switch (response_id) {
	case GTK_RESPONSE_HELP:
		panel_show_help (gtk_window_get_screen (GTK_WINDOW (dialog->addto_dialog)),
				 "mate-user-guide", "gospanel-15", NULL);
		break;

	case PANEL_ADDTO_RESPONSE_ADD:
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->tree_view));
		if (gtk_tree_selection_get_selected (selection, &filter_model,
						     &filter_iter)) {
			PanelAddtoItemInfo *data;

			gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (filter_model),
									  &iter,
									  &filter_iter);
			child_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));
			gtk_tree_model_get (child_model, &iter,
					    COLUMN_DATA, &data, -1);

			if (data != NULL)
				panel_addto_add_item (dialog, data);
		}
		break;

	case PANEL_ADDTO_RESPONSE_BACK:
		/* This response only happens when we're showing the
		 * application list and the user wants to go back to the
		 * applet list. */
		panel_addto_present_applets (dialog);
		break;

	case GTK_RESPONSE_CLOSE:
		gtk_widget_destroy (widget_dialog);
		break;

	default:
		break;
	}
}

static void
panel_addto_dialog_destroy (GtkWidget *widget_dialog,
			    PanelAddtoDialog *dialog)
{
	panel_toplevel_pop_autohide_disabler (PANEL_TOPLEVEL (dialog->panel_widget->toplevel));
	g_object_set_qdata (G_OBJECT (dialog->panel_widget->toplevel),
			    panel_addto_dialog_quark,
			    NULL);
}

static void
panel_addto_present_applications (PanelAddtoDialog *dialog)
{
	if (dialog->filter_application_model == NULL)
		panel_addto_make_application_model (dialog);
	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->tree_view),
				 dialog->filter_application_model);
	gtk_window_set_focus (GTK_WINDOW (dialog->addto_dialog),
			      dialog->search_entry);
	gtk_widget_set_sensitive (dialog->back_button, TRUE);

	if (dialog->applet_search_text)
		g_free (dialog->applet_search_text);

	dialog->applet_search_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->search_entry)));
	/* show everything */
	gtk_entry_set_text (GTK_ENTRY (dialog->search_entry), "");
}

static void
panel_addto_present_applets (PanelAddtoDialog *dialog)
{
	if (dialog->filter_applet_model == NULL)
		panel_addto_make_applet_model (dialog);
	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->tree_view),
				 dialog->filter_applet_model);
	gtk_window_set_focus (GTK_WINDOW (dialog->addto_dialog),
			      dialog->search_entry);
	gtk_widget_set_sensitive (dialog->back_button, FALSE);

	if (dialog->applet_search_text) {
		gtk_entry_set_text (GTK_ENTRY (dialog->search_entry),
				    dialog->applet_search_text);
		gtk_editable_set_position (GTK_EDITABLE (dialog->search_entry),
					   -1);

		g_free (dialog->applet_search_text);
		dialog->applet_search_text = NULL;
	}
}

static void
panel_addto_dialog_free_item_info (PanelAddtoItemInfo *item_info)
{
	if (item_info == NULL || item_info->static_data)
		return;

	if (item_info->name != NULL)
		g_free (item_info->name);
	item_info->name = NULL;

	if (item_info->description != NULL)
		g_free (item_info->description);
	item_info->description = NULL;

	if (item_info->icon != NULL)
		g_free (item_info->icon);
	item_info->icon = NULL;

	if (item_info->iid != NULL)
		g_free (item_info->iid);
	item_info->iid = NULL;

	if (item_info->launcher_path != NULL)
		g_free (item_info->launcher_path);
	item_info->launcher_path = NULL;

	if (item_info->menu_filename != NULL)
		g_free (item_info->menu_filename);
	item_info->menu_filename = NULL;

	if (item_info->menu_path != NULL)
		g_free (item_info->menu_path);
	item_info->menu_path = NULL;
}

static void
panel_addto_dialog_free_application_list (GSList *application_list)
{
	PanelAddtoAppList *data;
	GSList            *app;

	if (application_list == NULL)
		return;

	for (app = application_list; app != NULL; app = app->next) {
		data = app->data;
		if (data->children) {
			panel_addto_dialog_free_application_list (data->children);
		}
		panel_addto_dialog_free_item_info (&data->item_info);
		g_free (data);
	}
	g_slist_free (application_list);
}

static void
panel_addto_name_notify (GSettings        *settings,
			 gchar            *key,
			 PanelAddtoDialog *dialog);

static void
panel_addto_dialog_free (PanelAddtoDialog *dialog)
{
	GSList      *item;

	g_signal_handlers_disconnect_by_func(dialog->panel_widget->toplevel->settings,
					     G_CALLBACK (panel_addto_name_notify),
					     dialog);

	if (dialog->search_text)
		g_free (dialog->search_text);
	dialog->search_text = NULL;

	if (dialog->applet_search_text)
		g_free (dialog->applet_search_text);
	dialog->applet_search_text = NULL;

	if (dialog->addto_dialog)
		gtk_widget_destroy (dialog->addto_dialog);
	dialog->addto_dialog = NULL;

	for (item = dialog->applet_list; item != NULL; item = item->next) {
		PanelAddtoItemInfo *applet;

		applet = (PanelAddtoItemInfo *) item->data;
		if (!applet->static_data) {
			panel_addto_dialog_free_item_info (applet);
			g_free (applet);
		}
	}
	g_slist_free (dialog->applet_list);

	panel_addto_dialog_free_application_list (dialog->application_list);
	panel_addto_dialog_free_application_list (dialog->settings_list);

	if (dialog->filter_applet_model)
		g_object_unref (dialog->filter_applet_model);
	dialog->filter_applet_model = NULL;

	if (dialog->applet_model)
		g_object_unref (dialog->applet_model);
	dialog->applet_model = NULL;

	if (dialog->filter_application_model)
		g_object_unref (dialog->filter_application_model);
	dialog->filter_application_model = NULL;

	if (dialog->application_model)
		g_object_unref (dialog->application_model);
	dialog->application_model = NULL;

	g_clear_object (&dialog->menu_tree);

	g_free (dialog);
}

static void
panel_addto_name_change (PanelAddtoDialog *dialog,
			 const char       *name)
{
	char *title;
	char *label;

	label = NULL;

	if (!PANEL_GLIB_STR_EMPTY (name))
		label = g_strdup_printf (_("Find an _item to add to \"%s\":"),
					 name);

	if (panel_toplevel_get_is_attached (dialog->panel_widget->toplevel)) {
		title = g_strdup_printf (_("Add to Drawer"));
		if (label == NULL)
			label = g_strdup (_("Find an _item to add to the drawer:"));
	} else {
		title = g_strdup_printf (_("Add to Panel"));
		if (label == NULL)
			label = g_strdup (_("Find an _item to add to the panel:"));
	}

	gtk_window_set_title (GTK_WINDOW (dialog->addto_dialog), title);
	g_free (title);

	gtk_label_set_text_with_mnemonic (GTK_LABEL (dialog->label), label);
	g_free (label);
}

static void
panel_addto_name_notify (GSettings        *settings,
			 gchar            *key,
			 PanelAddtoDialog *dialog)
{
	gchar *name = g_settings_get_string (settings, key);
	panel_addto_name_change (dialog, name);
	g_free (name);
}

static gboolean
panel_addto_filter_func (GtkTreeModel *model,
			 GtkTreeIter  *iter,
			 gpointer      userdata)
{
	PanelAddtoDialog   *dialog;
	PanelAddtoItemInfo *data;

	dialog = (PanelAddtoDialog *) userdata;

	if (!dialog->search_text || !dialog->search_text[0])
		return TRUE;

	gtk_tree_model_get (model, iter, COLUMN_DATA, &data, -1);

	if (data == NULL)
		return FALSE;

	/* This is more a workaround than anything else: show all the root
	 * items in a tree store */
	if (GTK_IS_TREE_STORE (model) &&
	    gtk_tree_store_iter_depth (GTK_TREE_STORE (model), iter) == 0)
		return TRUE;

	return (panel_g_utf8_strstrcase (data->name,
					 dialog->search_text) != NULL ||
	        panel_g_utf8_strstrcase (data->description,
					 dialog->search_text) != NULL);
}

static void
panel_addto_search_entry_changed (GtkWidget        *entry,
				  PanelAddtoDialog *dialog)
{
	GtkTreeModel *model;
	char         *new_text;
	GtkTreeIter   iter;
	GtkTreePath  *path;

	new_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->search_entry)));
	g_strchomp (new_text);

	if (dialog->search_text &&
	    g_utf8_collate (new_text, dialog->search_text) == 0) {
		g_free (new_text);
		return;
	}

	if (dialog->search_text)
		g_free (dialog->search_text);
	dialog->search_text = new_text;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->tree_view));
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (model));

	path = gtk_tree_path_new_first ();
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		GtkTreeSelection *selection;

		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (dialog->tree_view),
					      path, NULL, FALSE, 0, 0);
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->tree_view));
		gtk_tree_selection_select_path (selection, path);
	}
	gtk_tree_path_free (path);
}

static void
panel_addto_search_entry_activated (GtkWidget        *entry,
				    PanelAddtoDialog *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog->addto_dialog),
			     PANEL_ADDTO_RESPONSE_ADD);
}

static gboolean
panel_addto_selection_func (GtkTreeSelection  *selection,
			    GtkTreeModel      *model,
			    GtkTreePath       *path,
			    gboolean           path_currently_selected,
			    gpointer           data)
{
	GtkTreeIter         iter;
	gboolean            enabled;

	if (!gtk_tree_model_get_iter (model, &iter, path))
		return FALSE;

	gtk_tree_model_get (model, &iter,
			    COLUMN_ENABLED, &enabled,
			    -1);
	return enabled;
}

static void
panel_addto_selection_changed (GtkTreeSelection *selection,
			       PanelAddtoDialog *dialog)
{
	GtkTreeModel       *filter_model;
	GtkTreeModel       *child_model;
	GtkTreeIter         iter;
	GtkTreeIter         filter_iter;
	PanelAddtoItemInfo *data;
	char               *iid;

	if (!gtk_tree_selection_get_selected (selection,
					      &filter_model,
					      &filter_iter)) {
		gtk_widget_set_sensitive (GTK_WIDGET (dialog->add_button),
					  FALSE);
		return;
	}

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (filter_model),
							  &iter, &filter_iter);
	child_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));
	gtk_tree_model_get (child_model, &iter, COLUMN_DATA, &data, -1);

	if (!data) {
		gtk_widget_set_sensitive (GTK_WIDGET (dialog->add_button),
					  FALSE);
		return;
	}

	gtk_widget_set_sensitive (GTK_WIDGET (dialog->add_button), TRUE);

	if (data->type == PANEL_ADDTO_LAUNCHER_MENU) {
		gtk_button_set_image (GTK_BUTTON (dialog->add_button),
				      gtk_image_new_from_icon_name ("go-next", GTK_ICON_SIZE_BUTTON));
	} else {
		gtk_button_set_image (GTK_BUTTON (dialog->add_button),
				      gtk_image_new_from_icon_name ("list-add", GTK_ICON_SIZE_BUTTON));
	}

	/* only allow dragging applets if we can add applets */
	if (panel_profile_id_lists_are_writable ()) {
		switch (data->type) {
		case PANEL_ADDTO_LAUNCHER:
			panel_addto_setup_launcher_drag (GTK_TREE_VIEW (dialog->tree_view),
							 data->launcher_path);
			break;
		case PANEL_ADDTO_APPLET:
			panel_addto_setup_applet_drag (GTK_TREE_VIEW (dialog->tree_view),
						       data->iid);
			break;
		case PANEL_ADDTO_LAUNCHER_MENU:
			gtk_tree_view_unset_rows_drag_source (GTK_TREE_VIEW (dialog->tree_view));
			break;
		case PANEL_ADDTO_MENU:
			/* build the iid for menus other than the main menu */
			if (data->iid == NULL) {
				iid = g_strdup_printf ("MENU:%s/%s",
						       data->menu_filename,
						       data->menu_path);
			} else {
				iid = g_strdup (data->iid);
			}
			panel_addto_setup_internal_applet_drag (GTK_TREE_VIEW (dialog->tree_view),
							        iid);
			g_free (iid);
			break;
		default:
			panel_addto_setup_internal_applet_drag (GTK_TREE_VIEW (dialog->tree_view),
							        data->iid);
			break;
		}
	}
}

static void
panel_addto_selection_activated (GtkTreeView       *view,
				 GtkTreePath       *path,
				 GtkTreeViewColumn *column,
				 PanelAddtoDialog  *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog->addto_dialog),
			     PANEL_ADDTO_RESPONSE_ADD);
}

static gboolean
panel_addto_separator_func (GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer data)
{
	int column = GPOINTER_TO_INT (data);
	char *text;

	gtk_tree_model_get (model, iter, column, &text, -1);

	if (!text)
		return TRUE;

	g_free(text);
	return FALSE;
}

static PanelAddtoDialog *
panel_addto_dialog_new (PanelWidget *panel_widget)
{
	PanelAddtoDialog *dialog;
	GtkWidget *dialog_vbox;
	GtkWidget *inner_vbox;
	GtkWidget *find_hbox;
	GtkWidget *sw;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	dialog = g_new0 (PanelAddtoDialog, 1);

	g_object_set_qdata_full (G_OBJECT (panel_widget->toplevel),
				 panel_addto_dialog_quark,
				 dialog,
				 (GDestroyNotify) panel_addto_dialog_free);

	dialog->panel_widget = panel_widget;

	g_signal_connect (dialog->panel_widget->toplevel->settings,
			  "changed::" PANEL_TOPLEVEL_NAME_KEY,
			  G_CALLBACK (panel_addto_name_notify),
			  dialog);

	dialog->addto_dialog = gtk_dialog_new ();

	panel_dialog_add_button (GTK_DIALOG (dialog->addto_dialog),
				 _("_Help"), "help-browser", GTK_RESPONSE_HELP);

	dialog->back_button = panel_dialog_add_button (GTK_DIALOG (dialog->addto_dialog),
						       _("_Back"), "go-previous",
						       PANEL_ADDTO_RESPONSE_BACK);

	dialog->add_button = panel_dialog_add_button (GTK_DIALOG (dialog->addto_dialog),
						      _("_Add"), "list-add",
						      PANEL_ADDTO_RESPONSE_ADD);

	panel_dialog_add_button (GTK_DIALOG (dialog->addto_dialog),
				 _("_Close"), "window-close",
				 GTK_RESPONSE_CLOSE);

	gtk_widget_set_sensitive (GTK_WIDGET (dialog->add_button), FALSE);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog->addto_dialog),
					 PANEL_ADDTO_RESPONSE_ADD);

	gtk_container_set_border_width (GTK_CONTAINER (dialog->addto_dialog), 5);

	dialog_vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog->addto_dialog));
	gtk_box_set_spacing (GTK_BOX (dialog_vbox), 12);
	gtk_container_set_border_width (GTK_CONTAINER (dialog_vbox), 5);

	g_signal_connect (G_OBJECT (dialog->addto_dialog), "response",
			  G_CALLBACK (panel_addto_dialog_response), dialog);
	g_signal_connect (dialog->addto_dialog, "destroy",
			  G_CALLBACK (panel_addto_dialog_destroy), dialog);

	inner_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start (GTK_BOX (dialog_vbox), inner_vbox, TRUE, TRUE, 0);

	find_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start (GTK_BOX (inner_vbox), find_hbox, FALSE, FALSE, 0);

	dialog->label = gtk_label_new_with_mnemonic ("");
	gtk_label_set_xalign (GTK_LABEL (dialog->label), 0.0);
	gtk_label_set_yalign (GTK_LABEL (dialog->label), 0.5);
	gtk_label_set_use_markup (GTK_LABEL (dialog->label), TRUE);

	gtk_box_pack_start (GTK_BOX (find_hbox), dialog->label,
			    FALSE, FALSE, 0);

	dialog->search_entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (dialog->search_entry), "changed",
			  G_CALLBACK (panel_addto_search_entry_changed), dialog);
	g_signal_connect (G_OBJECT (dialog->search_entry), "activate",
			  G_CALLBACK (panel_addto_search_entry_activated), dialog);

	gtk_box_pack_end (GTK_BOX (find_hbox), dialog->search_entry,
			  TRUE, TRUE, 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (dialog->label),
				       dialog->search_entry);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_IN);
	gtk_box_pack_start (GTK_BOX (inner_vbox), sw, TRUE, TRUE, 0);

	dialog->tree_view = gtk_tree_view_new ();
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (dialog->tree_view),
					   FALSE);
	gtk_tree_view_expand_all (GTK_TREE_VIEW (dialog->tree_view));

	renderer = g_object_new (GTK_TYPE_CELL_RENDERER_PIXBUF,
				 "xpad", 4,
				 "ypad", 4,
				 "stock-size", panel_add_to_icon_get_size(),
				 NULL);

	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (dialog->tree_view),
						     -1, NULL,
						     renderer,
						     "icon_name", COLUMN_ICON_NAME,
						     "sensitive", COLUMN_ENABLED,
						     NULL);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (dialog->tree_view),
						     -1, NULL,
						     renderer,
						     "markup", COLUMN_TEXT,
						     "sensitive", COLUMN_ENABLED,
						     NULL);

	//FIXME use the same search than the one for the search entry?
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (dialog->tree_view),
					 COLUMN_SEARCH);

	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (dialog->tree_view),
					      panel_addto_separator_func,
					      GINT_TO_POINTER (COLUMN_TEXT),
					      NULL);


	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	column = gtk_tree_view_get_column (GTK_TREE_VIEW (dialog->tree_view),
					   COLUMN_TEXT);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

	gtk_tree_selection_set_select_function (selection, panel_addto_selection_func, NULL, NULL);

	g_signal_connect (selection, "changed",
			  G_CALLBACK (panel_addto_selection_changed),
			  dialog);

	g_signal_connect (dialog->tree_view, "row-activated",
			  G_CALLBACK (panel_addto_selection_activated),
			  dialog);

	gtk_container_add (GTK_CONTAINER (sw), dialog->tree_view);

	gtk_widget_show_all (dialog_vbox);

	panel_toplevel_push_autohide_disabler (dialog->panel_widget->toplevel);
	panel_widget_register_open_dialog (panel_widget,
					   dialog->addto_dialog);

	panel_addto_name_change (dialog,
				 panel_toplevel_get_name (dialog->panel_widget->toplevel));

	return dialog;
}

#define MAX_ADDTOPANEL_HEIGHT 490

void
panel_addto_present (GtkMenuItem *item,
		     PanelWidget *panel_widget)
{
	PanelAddtoDialog *dialog;
	PanelToplevel *toplevel;
	PanelData     *pd;
	GdkScreen *screen;
	GdkMonitor *monitor;
	GdkRectangle monitor_geom;
	gint height;

	toplevel = panel_widget->toplevel;
	pd = g_object_get_data (G_OBJECT (toplevel), "PanelData");

	if (!panel_addto_dialog_quark)
		panel_addto_dialog_quark =
			g_quark_from_static_string ("panel-addto-dialog");

	dialog = g_object_get_qdata (G_OBJECT (toplevel),
				     panel_addto_dialog_quark);

	screen = gtk_window_get_screen (GTK_WINDOW (toplevel));
	monitor = gdk_display_get_monitor_at_window (gtk_widget_get_display (GTK_WIDGET (toplevel)),
						     gtk_widget_get_window (GTK_WIDGET (toplevel)));
	gdk_monitor_get_geometry (monitor, &monitor_geom);
	height = MIN (MAX_ADDTOPANEL_HEIGHT, 3 * (monitor_geom.height / 4));

	if (!dialog) {
		dialog = panel_addto_dialog_new (panel_widget);
		panel_addto_present_applets (dialog);
	}

	dialog->insertion_position = pd ? pd->insertion_pos : -1;
	gtk_window_set_screen (GTK_WINDOW (dialog->addto_dialog), screen);
	gtk_window_set_default_size (GTK_WINDOW (dialog->addto_dialog),
				     height * 8 / 7, height);
	gtk_window_present (GTK_WINDOW (dialog->addto_dialog));
}
