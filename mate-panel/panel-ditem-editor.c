/*
 * panel-ditem-editor.c:
 *
 * Copyright (C) 2004, 2006 Vincent Untz
 *
 * The Mate Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Mate Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libpanel-util/panel-icon-chooser.h>
#include <libpanel-util/panel-keyfile.h>
#include <libpanel-util/panel-show.h>
#include <libpanel-util/panel-xdg.h>
#include <libpanel-util/panel-gtk.h>

#include "panel-ditem-editor.h"
#include "panel-icon-names.h"
#include "panel-util.h"
#include "panel-marshal.h"

struct _PanelDItemEditorPrivate
{
	/* we keep a ditem around, since we can never have absolutely
	   everything in the display so we load a file, or get a ditem,
	   sync the display and ref the ditem */
	GKeyFile *key_file;
	gboolean  free_key_file;
	/* the revert ditem will only contain relevant keys */
	GKeyFile *revert_key_file;

	gboolean  reverting;
	gboolean  dirty;
	guint     save_timeout;

	char     *uri; /* file location */
	gboolean  type_directory;
	gboolean  new_file;
	gboolean  combo_setuped;

	PanelDitemSaveUri save_uri;
	gpointer          save_uri_data;

	GtkWidget *grid;
	GtkWidget *type_label;
	GtkWidget *type_combo;
	GtkWidget *name_label;
	GtkWidget *name_entry;
	GtkWidget *command_hbox;
	GtkWidget *command_label;
	GtkWidget *command_entry;
	GtkWidget *command_browse_button;
	GtkWidget *command_browse_filechooser;
	GtkWidget *comment_label;
	GtkWidget *comment_entry;
	GtkWidget *icon_chooser;

	GtkWidget *help_button;
	GtkWidget *revert_button;
	GtkWidget *close_button;
	GtkWidget *cancel_button;
	GtkWidget *ok_button;
};

/* Time in seconds after which we save the file on the disk */
#define SAVE_FREQUENCY 2

enum {
	REVERT_BUTTON
};

typedef enum {
	PANEL_DITEM_EDITOR_TYPE_NULL,
	PANEL_DITEM_EDITOR_TYPE_APPLICATION,
	PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION,
	PANEL_DITEM_EDITOR_TYPE_LINK,
	PANEL_DITEM_EDITOR_TYPE_DIRECTORY
} PanelDItemEditorType;

enum {
	COLUMN_TEXT,
	COLUMN_TYPE,
	NUMBER_COLUMNS
};

typedef struct {
	const char           *name;
	const char           *show_for;
	PanelDItemEditorType  type;
} ComboItem;

static ComboItem type_items [] = {
	{ N_("Application"),             "Application",
	  PANEL_DITEM_EDITOR_TYPE_APPLICATION          },
	{ N_("Application in Terminal"), "Application",
	  PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION },
	{ N_("Location"),                "Link",
	  PANEL_DITEM_EDITOR_TYPE_LINK                 },
	/* FIXME: hack hack hack: we will remove this item from the combo
	 * box if we show it */
	{ NULL,                          "Directory",
	  PANEL_DITEM_EDITOR_TYPE_DIRECTORY            }
};

typedef struct {
	const char *key;
	GType       type;
	gboolean    default_value;
	gboolean    locale;
} RevertKey;

static RevertKey revert_keys [] = {
	{ "Type",     G_TYPE_STRING,  FALSE, FALSE },
	{ "Terminal", G_TYPE_BOOLEAN, FALSE, FALSE },
	{ "Exec",     G_TYPE_STRING,  FALSE, FALSE },
	{ "URL",      G_TYPE_STRING,  FALSE, FALSE },
	/* locale keys */
	{ "Icon",     G_TYPE_STRING,  FALSE, TRUE  },
	{ "Name",     G_TYPE_STRING,  FALSE, TRUE  },
	{ "Comment",  G_TYPE_STRING,  FALSE, TRUE  },
	{ "X-MATE-FullName", G_TYPE_STRING,  FALSE, TRUE  },
	/* C version of those keys */
	{ "Icon",     G_TYPE_STRING,  FALSE, FALSE },
	{ "Name",     G_TYPE_STRING,  FALSE, FALSE },
	{ "Comment",  G_TYPE_STRING,  FALSE, FALSE },
	{ "X-MATE-FullName", G_TYPE_STRING,  FALSE, FALSE }
};

enum {
	SAVED,
	CHANGED,
	NAME_CHANGED,
	COMMAND_CHANGED,
	COMMENT_CHANGED,
	ICON_CHANGED,
	ERROR_REPORTED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_KEYFILE,
	PROP_URI,
	PROP_TYPEDIRECTORY
};

static guint ditem_edit_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (PanelDItemEditor, panel_ditem_editor, GTK_TYPE_DIALOG)

static void panel_ditem_editor_setup_ui (PanelDItemEditor *dialog);

static void type_combo_changed (PanelDItemEditor *dialog);

static void response_cb (GtkDialog *dialog,
			 gint       response_id);

static void setup_icon_chooser (PanelDItemEditor *dialog,
			        const char       *icon_name);

static gboolean panel_ditem_editor_save         (PanelDItemEditor *dialog,
						 gboolean          report_errors);
static gboolean panel_ditem_editor_save_timeout (gpointer data);
static void panel_ditem_editor_revert (PanelDItemEditor *dialog);

static void panel_ditem_editor_key_file_loaded (PanelDItemEditor  *dialog);
static gboolean panel_ditem_editor_load_uri (PanelDItemEditor  *dialog,
					     GError           **error);

static void panel_ditem_editor_set_key_file (PanelDItemEditor *dialog,
					     GKeyFile         *key_file);

static gboolean panel_ditem_editor_get_type_directory (PanelDItemEditor *dialog);
static void panel_ditem_editor_set_type_directory (PanelDItemEditor *dialog,
						   gboolean          type_directory);


static PanelDItemEditorType
map_type_from_desktop_item (const char *type,
			    gboolean    terminal)
{
	if (type == NULL)
		return PANEL_DITEM_EDITOR_TYPE_NULL;
	else if (!strcmp (type, "Application")) {
		if (terminal)
			return PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION;
		else
			return PANEL_DITEM_EDITOR_TYPE_APPLICATION;
	} else if (!strcmp (type, "Link"))
		return PANEL_DITEM_EDITOR_TYPE_LINK;
	else if (!strcmp (type, "Directory"))
		return PANEL_DITEM_EDITOR_TYPE_DIRECTORY;
	else
		return PANEL_DITEM_EDITOR_TYPE_NULL;
}

static GObject *
panel_ditem_editor_constructor (GType                  type,
				guint                  n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	GObject          *obj;
	PanelDItemEditor *dialog;
	GFile            *file;
	gboolean          loaded;
	char             *desktop_type;

	obj = G_OBJECT_CLASS (panel_ditem_editor_parent_class)->constructor (type,
									     n_construct_properties,
									     construct_properties);

	dialog = PANEL_DITEM_EDITOR (obj);

	if (dialog->priv->key_file) {
		panel_ditem_editor_key_file_loaded (dialog);
		dialog->priv->new_file = FALSE;
		dialog->priv->free_key_file = FALSE;
		loaded = TRUE;
	} else {
		dialog->priv->key_file = panel_key_file_new_desktop ();
		if (dialog->priv->type_directory)
			panel_key_file_set_string (dialog->priv->key_file,
						   "Type", "Directory");
		dialog->priv->free_key_file = TRUE;
		loaded = FALSE;
	}

	if (!loaded && dialog->priv->uri) {
		file = g_file_new_for_uri (dialog->priv->uri);
		if (g_file_query_exists (file, NULL)) {
			//FIXME what if there's an error?
			panel_ditem_editor_load_uri (dialog, NULL);
			dialog->priv->new_file = FALSE;
		} else {
			dialog->priv->new_file = TRUE;
		}
		g_object_unref (file);
	} else {
		dialog->priv->new_file = !loaded;
	}

	dialog->priv->dirty = FALSE;

	desktop_type = panel_key_file_get_string (dialog->priv->key_file,
						  "Type");
	if (desktop_type && !strcmp (desktop_type, "Directory"))
		dialog->priv->type_directory = TRUE;
	g_free (desktop_type);

	panel_ditem_editor_setup_ui (dialog);

	if (dialog->priv->new_file)
		setup_icon_chooser (dialog, NULL);

	return obj;
}

static void
panel_ditem_editor_get_property (GObject    *object,
				 guint	     prop_id,
				 GValue	    *value,
				 GParamSpec *pspec)
{
	PanelDItemEditor *dialog;

	g_return_if_fail (PANEL_IS_DITEM_EDITOR (object));

	dialog = PANEL_DITEM_EDITOR (object);

	switch (prop_id) {
	case PROP_KEYFILE:
		g_value_set_pointer (value, panel_ditem_editor_get_key_file (dialog));
		break;
	case PROP_URI:
		g_value_set_string (value, panel_ditem_editor_get_uri (dialog));
		break;
	case PROP_TYPEDIRECTORY:
		g_value_set_boolean (value, panel_ditem_editor_get_type_directory (dialog));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_ditem_editor_set_property (GObject       *object,
				 guint	       prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	PanelDItemEditor *dialog;

	g_return_if_fail (PANEL_IS_DITEM_EDITOR (object));

	dialog = PANEL_DITEM_EDITOR (object);

	switch (prop_id) {
	case PROP_KEYFILE:
		panel_ditem_editor_set_key_file (dialog,
						 g_value_get_pointer (value));
		break;
	case PROP_URI:
		panel_ditem_editor_set_uri (dialog,
					    g_value_get_string (value));
		break;
	case PROP_TYPEDIRECTORY:
		panel_ditem_editor_set_type_directory (dialog,
						       g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_ditem_editor_dispose (GObject *object)
{
	PanelDItemEditor *dialog;

	dialog = PANEL_DITEM_EDITOR (object);

	/* If there was a timeout, then something changed after last save,
	 * so we must save again now */
	if (dialog->priv->save_timeout) {
		g_source_remove (dialog->priv->save_timeout);
		dialog->priv->save_timeout = 0;
		panel_ditem_editor_save (dialog, FALSE);
	}

	/* remember, destroy can be run multiple times! */

	if (dialog->priv->free_key_file && dialog->priv->key_file != NULL)
		g_key_file_free (dialog->priv->key_file);
	dialog->priv->key_file = NULL;

	if (dialog->priv->revert_key_file != NULL)
		g_key_file_free (dialog->priv->revert_key_file);
	dialog->priv->revert_key_file = NULL;

	if (dialog->priv->uri != NULL)
		g_free (dialog->priv->uri);
	dialog->priv->uri = NULL;

	G_OBJECT_CLASS (panel_ditem_editor_parent_class)->dispose (object);
}

static void
panel_ditem_editor_class_init (PanelDItemEditorClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);

	gobject_class->constructor = panel_ditem_editor_constructor;
	gobject_class->get_property = panel_ditem_editor_get_property;
        gobject_class->set_property = panel_ditem_editor_set_property;

	gobject_class->dispose = panel_ditem_editor_dispose;

	ditem_edit_signals[SAVED] =
		g_signal_new ("saved",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	ditem_edit_signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	ditem_edit_signals[NAME_CHANGED] =
		g_signal_new ("name_changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       name_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	ditem_edit_signals[COMMAND_CHANGED] =
		g_signal_new ("command_changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       command_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	ditem_edit_signals[COMMENT_CHANGED] =
		g_signal_new ("comment_changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       comment_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	ditem_edit_signals[ICON_CHANGED] =
		g_signal_new ("icon_changed",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       icon_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	ditem_edit_signals[ERROR_REPORTED] =
		g_signal_new ("error_reported",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PanelDItemEditorClass,
					       error_reported),
			      NULL,
			      NULL,
			      panel_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2,
			      G_TYPE_STRING, G_TYPE_STRING);

	g_object_class_install_property (
		gobject_class,
		PROP_KEYFILE,
		g_param_spec_pointer ("keyfile",
				      "Key File",
				      "A key file containing the data from the .desktop file",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		gobject_class,
		PROP_URI,
		g_param_spec_string ("uri",
				     "URI",
				     "The URI of the .desktop file",
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		gobject_class,
		PROP_TYPEDIRECTORY,
		g_param_spec_boolean ("type-directory",
				      "Type Directory",
				      "Whether the edited file is a .directory file or not",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static GtkWidget *
label_new_with_mnemonic (const char *text)
{
	GtkWidget *label;
	char      *bold;

	bold = g_strdup_printf ("<b>%s</b>", text);
	label = gtk_label_new_with_mnemonic (bold);
	g_free (bold);

	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 1.0);
	gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	gtk_widget_show (label);

	return label;
}

static inline void
grid_attach_label (GtkGrid *grid, GtkWidget *label, int left, int top, int width, int height)
{
	gtk_grid_attach (grid, label, left, top, width, height);
}

static inline void
grid_attach_entry (GtkGrid *grid, GtkWidget *entry, int left, int top, int width, int height)
{
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_grid_attach (grid, entry, left, top, width, height);
}

static void
setup_combo (GtkWidget            *combo_box,
	     ComboItem            *items,
	     int                   nb_items,
	     const char           *for_type)
{
	GtkListStore          *model;
	GtkTreeIter            iter;
	GtkCellRenderer       *renderer;
	int                    i;

	model = gtk_list_store_new (NUMBER_COLUMNS,
				    G_TYPE_STRING,
				    G_TYPE_INT);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box),
				 GTK_TREE_MODEL (model));

	for (i = 0; i < nb_items; i++) {
		if (for_type && strcmp (for_type, items [i].show_for))
			continue;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    COLUMN_TEXT, _(items [i].name),
				    COLUMN_TYPE, items [i].type,
				    -1);
	}

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box),
				    renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box),
					renderer, "text", COLUMN_TEXT, NULL);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
}

static PanelDItemEditorType
panel_ditem_editor_get_item_type (PanelDItemEditor *dialog)
{
	GtkTreeIter           iter;
	GtkTreeModel         *model;
	PanelDItemEditorType  type;

	if (dialog->priv->type_directory)
		return PANEL_DITEM_EDITOR_TYPE_DIRECTORY;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (dialog->priv->type_combo),
					    &iter))
		return PANEL_DITEM_EDITOR_TYPE_NULL;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (dialog->priv->type_combo));
	gtk_tree_model_get (model, &iter, COLUMN_TYPE, &type, -1);

	return type;
}

static void
panel_ditem_editor_make_ui (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;
	GtkWidget *dialog_vbox;

	priv = dialog->priv;

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

	dialog_vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_box_set_spacing (GTK_BOX (dialog_vbox), 2);

	priv->grid = gtk_grid_new ();
	gtk_container_set_border_width (GTK_CONTAINER (priv->grid), 5);
	gtk_grid_set_row_spacing (GTK_GRID (priv->grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (priv->grid), 12);
	gtk_box_pack_start (GTK_BOX (dialog_vbox), priv->grid, TRUE, TRUE, 0);
	gtk_widget_show (priv->grid);

	/* Type */
	priv->type_label = label_new_with_mnemonic (_("_Type:"));
	priv->type_combo = gtk_combo_box_new ();
	gtk_widget_show (priv->type_combo);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->type_label),
				       priv->type_combo);

	/* Name */
	priv->name_label = label_new_with_mnemonic (_("_Name:"));
	priv->name_entry = gtk_entry_new ();
	gtk_widget_show (priv->name_entry);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->name_label),
				       priv->name_entry);

	/* Icon */
	priv->icon_chooser = panel_icon_chooser_new (NULL);
	panel_icon_chooser_set_fallback_icon_name (PANEL_ICON_CHOOSER (priv->icon_chooser),
						   PANEL_ICON_LAUNCHER);
	gtk_grid_attach (GTK_GRID (priv->grid), priv->icon_chooser, 0, 0, 1, 2);
	gtk_widget_show (priv->icon_chooser);

	/* Command */
	priv->command_label = label_new_with_mnemonic ("");

	priv->command_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_show (priv->command_hbox);

	priv->command_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (priv->command_hbox),
			    priv->command_entry,
			    TRUE, TRUE, 0);
	gtk_widget_show (priv->command_entry);

	priv->command_browse_button = gtk_button_new_with_mnemonic (_("_Browse..."));
	gtk_box_pack_start (GTK_BOX (priv->command_hbox),
			    priv->command_browse_button,
			    FALSE, FALSE, 0);
	gtk_widget_show (priv->command_browse_button);

	/* Comment */
	priv->comment_label = label_new_with_mnemonic (_("Co_mment:"));
	priv->comment_entry = gtk_entry_new ();
	gtk_widget_show (priv->comment_entry);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->comment_label),
				       priv->comment_entry);

	priv->help_button = panel_dialog_add_button (GTK_DIALOG (dialog),
						     _("_Help"), "help-browser",
						     GTK_RESPONSE_HELP);

	priv->revert_button = panel_dialog_add_button (GTK_DIALOG (dialog),
						       _("_Revert"), "document-revert",
						       REVERT_BUTTON);

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
					   REVERT_BUTTON,
					   FALSE);

	priv->close_button = panel_dialog_add_button (GTK_DIALOG (dialog),
						      _("_Close"), "window-close",
						      GTK_RESPONSE_CLOSE);

	priv->cancel_button = panel_dialog_add_button (GTK_DIALOG (dialog),
						       _("_Cancel"), "process-stop",
						       GTK_RESPONSE_CANCEL);

	priv->ok_button = panel_dialog_add_button (GTK_DIALOG (dialog),
						   _("_OK"), "gtk-ok",
						   GTK_RESPONSE_OK);

	/* FIXME: There needs to be a way to edit ALL keys/sections */
}

static void
panel_ditem_editor_setup_ui (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;
	PanelDItemEditorType     type;
	gboolean                 show_combo;
	GList                   *focus_chain;

	priv = dialog->priv;
	type = panel_ditem_editor_get_item_type (dialog);

	if (priv->new_file) {
		gtk_widget_hide (priv->revert_button);
		gtk_widget_hide (priv->close_button);
		gtk_widget_show (priv->cancel_button);
		gtk_widget_show (priv->ok_button);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_OK);

		if (!priv->combo_setuped) {
			setup_combo (priv->type_combo,
				     type_items, G_N_ELEMENTS (type_items),
				     NULL);
			priv->combo_setuped = TRUE;
		}

		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->type_combo), 0);

		show_combo = !priv->type_directory;
	} else {

		gtk_widget_show (priv->revert_button);
		gtk_widget_show (priv->close_button);
		gtk_widget_hide (priv->cancel_button);
		gtk_widget_hide (priv->ok_button);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_CLOSE);

		show_combo = (type != PANEL_DITEM_EDITOR_TYPE_LINK) &&
			     (type != PANEL_DITEM_EDITOR_TYPE_DIRECTORY);
	}

	if (show_combo) {
		GtkTreeIter           iter;
		GtkTreeModel         *model;
		PanelDItemEditorType  buf_type;

		grid_attach_label (GTK_GRID (priv->grid), priv->type_label, 1, 0, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->type_combo, 2, 0, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->name_label, 1, 1, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->name_entry, 2, 1, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->command_label, 1, 2, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->command_hbox, 2, 2, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->comment_label, 1, 3, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->comment_entry, 2, 3, 1, 1);

		/* FIXME: hack hack hack */
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->type_combo));
		if (!gtk_tree_model_get_iter_first (model, &iter))
			g_assert_not_reached ();
		do {
			gtk_tree_model_get (model, &iter,
					    COLUMN_TYPE, &buf_type, -1);
			if (buf_type == PANEL_DITEM_EDITOR_TYPE_DIRECTORY) {
				gtk_list_store_remove (GTK_LIST_STORE (model),
						       &iter);
				break;
			}
		} while (gtk_tree_model_iter_next (model, &iter));
	} else if (type == PANEL_DITEM_EDITOR_TYPE_DIRECTORY) {
		grid_attach_label (GTK_GRID (priv->grid), priv->name_label, 1, 0, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->name_entry, 2, 0, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->comment_label, 1, 1, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->comment_entry, 2, 1, 1, 1);
	} else {
		grid_attach_label (GTK_GRID (priv->grid), priv->name_label, 1, 0, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->name_entry, 2, 0, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->command_label, 1, 1, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->command_hbox, 2, 1, 1, 1);

		grid_attach_label (GTK_GRID (priv->grid), priv->comment_label, 1, 2, 1, 1);
		grid_attach_entry (GTK_GRID (priv->grid), priv->comment_entry, 2, 2, 1, 1);
	}

	type_combo_changed (dialog);

	/* set a focus chain since GTK+ doesn't want to put the icon entry
	 * as the first widget in the chain */
	focus_chain = NULL;
	focus_chain = g_list_prepend (focus_chain, priv->icon_chooser);
	focus_chain = g_list_prepend (focus_chain, priv->type_combo);
	focus_chain = g_list_prepend (focus_chain, priv->name_entry);
	focus_chain = g_list_prepend (focus_chain, priv->command_hbox);
	focus_chain = g_list_prepend (focus_chain, priv->comment_entry);
	focus_chain = g_list_reverse (focus_chain);
	gtk_container_set_focus_chain (GTK_CONTAINER (priv->grid), focus_chain);
	g_list_free (focus_chain);

	gtk_widget_grab_focus (priv->name_entry);
}

/*
 * Will save after SAVE_FREQUENCY milliseconds of no changes. If something is
 * changed, the save is postponed to another SAVE_FREQUENCY seconds. This seems
 * to be a saner behaviour than just saving every N seconds.
 */
static void
panel_ditem_editor_changed (PanelDItemEditor *dialog)
{
	if (!dialog->priv->new_file) {
		if (dialog->priv->save_timeout != 0)
			g_source_remove (dialog->priv->save_timeout);

		dialog->priv->save_timeout = g_timeout_add_seconds (
							SAVE_FREQUENCY,
							panel_ditem_editor_save_timeout,
							dialog);

		/* We can revert to the original state */
		if (dialog->priv->revert_key_file != NULL)
			gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
							   REVERT_BUTTON,
							   TRUE);
	}

	dialog->priv->dirty = TRUE;
	g_signal_emit (G_OBJECT (dialog), ditem_edit_signals[CHANGED], 0);
}

static void
panel_ditem_editor_activated (PanelDItemEditor *dialog)
{
	if (gtk_widget_get_visible (dialog->priv->ok_button))
		gtk_dialog_response (GTK_DIALOG (dialog),
				     GTK_RESPONSE_OK);
	else if (gtk_widget_get_visible (dialog->priv->close_button))
		gtk_dialog_response (GTK_DIALOG (dialog),
				     GTK_RESPONSE_CLOSE);
}

static void
panel_ditem_editor_name_changed (PanelDItemEditor *dialog)
{
	const char *name;

	name = gtk_entry_get_text (GTK_ENTRY (dialog->priv->name_entry));

	if (!dialog->priv->reverting) {
		/* When reverting, we don't need to set the content of the key
		 * file; we only want to send a signal. Changing the key file
		 * could actually break the revert since it might overwrite the
		 * old Name value with the X-MATE-FullName value */
		if (name && name[0])
			panel_key_file_set_locale_string (dialog->priv->key_file,
							  "Name", name);
		else
			panel_key_file_remove_all_locale_key (dialog->priv->key_file,
							      "Name");

		panel_key_file_remove_all_locale_key (dialog->priv->key_file,
						      "X-MATE-FullName");
	}

	g_signal_emit (G_OBJECT (dialog), ditem_edit_signals[NAME_CHANGED], 0,
		       name);
}

static void
panel_ditem_editor_command_changed (PanelDItemEditor *dialog)
{
	PanelDItemEditorType  type;
	const char           *exec_or_uri;
	GtkIconTheme         *icon_theme;
	char                 *icon;

	exec_or_uri = gtk_entry_get_text (GTK_ENTRY (dialog->priv->command_entry));

	if (exec_or_uri && exec_or_uri[0])
		type = panel_ditem_editor_get_item_type (dialog);
	else
		type = PANEL_DITEM_EDITOR_TYPE_NULL;

	switch (type) {
	case PANEL_DITEM_EDITOR_TYPE_APPLICATION:
	case PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION:
		panel_key_file_remove_key (dialog->priv->key_file, "URL");
		panel_key_file_set_string (dialog->priv->key_file, "Exec",
					   exec_or_uri);

		icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (dialog)));
		icon = guess_icon_from_exec (icon_theme, exec_or_uri);
		if (icon) {
			char *current;

			current = panel_key_file_get_locale_string (dialog->priv->key_file,
								    "Icon");

			if (!current || strcmp (icon, current))
				setup_icon_chooser (dialog, icon);

			g_free (current);
			g_free (icon);
		}
		break;
	case PANEL_DITEM_EDITOR_TYPE_LINK:
		panel_key_file_remove_key (dialog->priv->key_file, "Exec");
		panel_key_file_set_string (dialog->priv->key_file, "URL",
					   exec_or_uri);
		break;
	default:
		panel_key_file_remove_key (dialog->priv->key_file, "Exec");
		panel_key_file_remove_key (dialog->priv->key_file, "URL");
	}

	g_signal_emit (G_OBJECT (dialog), ditem_edit_signals[COMMAND_CHANGED],
		       0, exec_or_uri);
}

static void
panel_ditem_editor_comment_changed (PanelDItemEditor *dialog)
{
	const char *comment;

	comment = gtk_entry_get_text (GTK_ENTRY (dialog->priv->comment_entry));

	if (comment && comment[0])
		panel_key_file_set_locale_string (dialog->priv->key_file,
						  "Comment", comment);
	else
		panel_key_file_remove_all_locale_key (dialog->priv->key_file,
						      "Comment");

	g_signal_emit (G_OBJECT (dialog), ditem_edit_signals[COMMENT_CHANGED],
		       0, comment);
}

static void
panel_ditem_editor_icon_changed (PanelDItemEditor *dialog,
				 const char       *icon)
{
	if (icon) {
		panel_key_file_set_string (dialog->priv->key_file,
						  "Icon", icon);
		panel_key_file_set_locale_string (dialog->priv->key_file,
						  "Icon", icon);
	}
	else
		panel_key_file_remove_all_locale_key (dialog->priv->key_file,
						      "Icon");

	g_signal_emit (G_OBJECT (dialog), ditem_edit_signals[ICON_CHANGED], 0,
		       icon);
}

static void
command_browse_chooser_response (GtkFileChooser   *chooser,
				 gint              response_id,
				 PanelDItemEditor *dialog)
{
	char *uri;
	char *text;

	if (response_id == GTK_RESPONSE_ACCEPT) {
		switch (panel_ditem_editor_get_item_type (dialog)) {
		case PANEL_DITEM_EDITOR_TYPE_APPLICATION:
		case PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION:
			text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
			uri = panel_util_make_exec_uri_for_desktop (text);
			g_free (text);
			break;
		case PANEL_DITEM_EDITOR_TYPE_LINK:
			uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (chooser));
			break;
		default:
			g_assert_not_reached ();
		}

		gtk_entry_set_text (GTK_ENTRY (dialog->priv->command_entry),
				    uri);
		g_free (uri);
	}

	gtk_widget_destroy (GTK_WIDGET (chooser));
	dialog->priv->command_browse_filechooser = NULL;
}

static void
update_chooser_for_type (PanelDItemEditor *dialog)
{
	const char *title;
	gboolean    local_only;
	GtkWidget  *chooser;

	if (!dialog->priv->command_browse_filechooser)
		return;

	switch (panel_ditem_editor_get_item_type (dialog)) {
	case PANEL_DITEM_EDITOR_TYPE_APPLICATION:
	case PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION:
		title = _("Choose an application...");
		local_only = TRUE;
		break;
	case PANEL_DITEM_EDITOR_TYPE_LINK:
		title = _("Choose a file...");
		local_only = FALSE;
		break;
	default:
		g_assert_not_reached ();
	}

	chooser = dialog->priv->command_browse_filechooser;

	gtk_window_set_title (GTK_WINDOW (chooser),
			      title);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (chooser),
					 local_only);
}

static void
command_browse_button_clicked (PanelDItemEditor *dialog)
{
	GtkWidget *chooser;

	if (dialog->priv->command_browse_filechooser) {
		gtk_window_present (GTK_WINDOW (dialog->priv->command_browse_filechooser));
		return;
	}

	chooser = panel_file_chooser_dialog_new ("", GTK_WINDOW (dialog),
						 GTK_FILE_CHOOSER_ACTION_OPEN,
						 "process-stop",
						 GTK_RESPONSE_CANCEL,
						 "document-open",
						 GTK_RESPONSE_ACCEPT,
						 NULL);

	gtk_window_set_destroy_with_parent (GTK_WINDOW (chooser), TRUE);

	g_signal_connect (chooser, "response",
			  G_CALLBACK (command_browse_chooser_response), dialog);

	dialog->priv->command_browse_filechooser = chooser;
	update_chooser_for_type (dialog);

	gtk_widget_show (chooser);
}

static void
panel_ditem_editor_connect_signals (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;

	priv = dialog->priv;

#define CONNECT_CHANGED(widget, callback) \
	g_signal_connect_swapped (G_OBJECT (widget), "changed", \
				  G_CALLBACK (callback), \
				  dialog); \
	g_signal_connect_swapped (G_OBJECT (widget), "changed", \
				  G_CALLBACK (panel_ditem_editor_changed), \
				  dialog);

	CONNECT_CHANGED (priv->type_combo, type_combo_changed);
	CONNECT_CHANGED (priv->name_entry, panel_ditem_editor_name_changed);
	CONNECT_CHANGED (priv->command_entry, panel_ditem_editor_command_changed);
	CONNECT_CHANGED (priv->comment_entry, panel_ditem_editor_comment_changed);
	CONNECT_CHANGED (priv->icon_chooser, panel_ditem_editor_icon_changed);

	g_signal_connect_swapped (priv->name_entry, "activate",
				  G_CALLBACK (panel_ditem_editor_activated),
				  dialog);
	g_signal_connect_swapped (priv->command_entry, "activate",
				  G_CALLBACK (panel_ditem_editor_activated),
				  dialog);
	g_signal_connect_swapped (priv->comment_entry, "activate",
				  G_CALLBACK (panel_ditem_editor_activated),
				  dialog);

	g_signal_connect_swapped (priv->command_browse_button, "clicked",
				  G_CALLBACK (command_browse_button_clicked),
				  dialog);

	/* We do a signal connection here rather than overriding the method in
	 * class_init because GtkDialog::response is a RUN_LAST signal. We
	 * want *our* handler to be run *first*, regardless of whether the user
	 * installs response handlers of his own.
	 */
	g_signal_connect (dialog, "response", G_CALLBACK (response_cb), NULL);
}

static void
panel_ditem_editor_block_signals (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;

	priv = dialog->priv;

#define BLOCK_CHANGED(widget, callback) \
	g_signal_handlers_block_by_func (G_OBJECT (widget), \
					 G_CALLBACK (callback), \
					 dialog); \
	g_signal_handlers_block_by_func (G_OBJECT (widget), \
					 G_CALLBACK (panel_ditem_editor_changed), \
					 dialog);
	BLOCK_CHANGED (priv->type_combo, type_combo_changed);
	BLOCK_CHANGED (priv->name_entry, panel_ditem_editor_name_changed);
	BLOCK_CHANGED (priv->command_entry, panel_ditem_editor_command_changed);
	BLOCK_CHANGED (priv->comment_entry, panel_ditem_editor_comment_changed);
	BLOCK_CHANGED (priv->icon_chooser, panel_ditem_editor_icon_changed);
}

static void
panel_ditem_editor_unblock_signals (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;

	priv = dialog->priv;

#define UNBLOCK_CHANGED(widget, callback) \
	g_signal_handlers_unblock_by_func (G_OBJECT (widget), \
					   G_CALLBACK (callback), \
					   dialog); \
	g_signal_handlers_unblock_by_func (G_OBJECT (widget), \
					   G_CALLBACK (panel_ditem_editor_changed), \
					   dialog);
	UNBLOCK_CHANGED (priv->type_combo, type_combo_changed);
	UNBLOCK_CHANGED (priv->name_entry, panel_ditem_editor_name_changed);
	UNBLOCK_CHANGED (priv->command_entry, panel_ditem_editor_command_changed);
	UNBLOCK_CHANGED (priv->comment_entry, panel_ditem_editor_comment_changed);
	UNBLOCK_CHANGED (priv->icon_chooser, panel_ditem_editor_icon_changed);
}

static void
panel_ditem_editor_init (PanelDItemEditor *dialog)
{
	PanelDItemEditorPrivate *priv;

	priv = panel_ditem_editor_get_instance_private (dialog);

	dialog->priv = priv;

	priv->key_file = NULL;
	priv->free_key_file = FALSE;
	priv->revert_key_file = NULL;
	priv->reverting = FALSE;
	priv->dirty = FALSE;
	priv->save_timeout = 0;
	priv->uri = NULL;
	priv->type_directory = FALSE;
	priv->new_file = TRUE;
	priv->save_uri = NULL;
	priv->save_uri_data = NULL;
	priv->combo_setuped = FALSE;
	priv->command_browse_filechooser = NULL;

	panel_ditem_editor_make_ui (dialog);
	panel_ditem_editor_connect_signals (dialog);
}

static void
type_combo_changed (PanelDItemEditor *dialog)
{
	const char *text;
	char       *bold;

	switch (panel_ditem_editor_get_item_type (dialog)) {
	case PANEL_DITEM_EDITOR_TYPE_APPLICATION:
		text = _("Comm_and:");
		if (dialog->priv->combo_setuped) {
			panel_key_file_set_string (dialog->priv->key_file,
						   "Type", "Application");
			panel_key_file_set_boolean (dialog->priv->key_file,
						    "Terminal", FALSE);
		}
		break;
	case PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION:
		text = _("Comm_and:");
		if (dialog->priv->combo_setuped) {
			panel_key_file_set_string (dialog->priv->key_file,
						   "Type", "Application");
			panel_key_file_set_boolean (dialog->priv->key_file,
						    "Terminal", TRUE);
		}
		break;
	case PANEL_DITEM_EDITOR_TYPE_LINK:
		text = _("_Location:");
		if (dialog->priv->combo_setuped) {
			panel_key_file_set_string (dialog->priv->key_file,
						   "Type", "Link");
			panel_key_file_remove_key (dialog->priv->key_file,
						   "Terminal");
		}
		break;
	case PANEL_DITEM_EDITOR_TYPE_DIRECTORY:
		if (dialog->priv->combo_setuped) {
			panel_key_file_set_string (dialog->priv->key_file,
						   "Type", "Directory");
		}
		return;
	default:
		g_assert_not_reached ();
	}

	bold = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (dialog->priv->command_label),
					    bold);
	g_free (bold);

	gtk_label_set_mnemonic_widget (GTK_LABEL (dialog->priv->command_label),
				       dialog->priv->command_entry);

	update_chooser_for_type (dialog);
}

static void
setup_icon_chooser (PanelDItemEditor *dialog,
		    const char       *icon_name)
{
	char *buffer;

	if (!icon_name || icon_name[0] == '\0') {
		if (dialog->priv->type_directory) {
			buffer = g_strdup (PANEL_ICON_FOLDER);
		} else {
			buffer = g_strdup (PANEL_ICON_LAUNCHER);
		}
	} else {
		buffer = g_strdup (icon_name);
	}

	panel_icon_chooser_set_icon (PANEL_ICON_CHOOSER (dialog->priv->icon_chooser),
				     buffer);

	g_free (buffer);
}

/* Conform display to ditem */
void
panel_ditem_editor_sync_display (PanelDItemEditor *dialog)
{
	char                 *type;
	PanelDItemEditorType  editor_type;
	gboolean              run_in_terminal;
	GKeyFile             *key_file;
	char                 *buffer;
	GtkTreeIter           iter;
	GtkTreeModel         *model;
	PanelDItemEditorType  buf_type;

	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	key_file = dialog->priv->key_file;

	/* Name */
	buffer = panel_key_file_get_locale_string (key_file, "X-MATE-FullName");
	if (!buffer)
		buffer = panel_key_file_get_locale_string (key_file, "Name");
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->name_entry),
			    buffer ? buffer : "");
	g_free (buffer);

	/* Type */
	type = panel_key_file_get_string (key_file, "Type");
	if (!dialog->priv->combo_setuped) {
		setup_combo (dialog->priv->type_combo,
			     type_items, G_N_ELEMENTS (type_items),
			     type);
		dialog->priv->combo_setuped = TRUE;
	}

	run_in_terminal = panel_key_file_get_boolean (key_file, "Terminal",
						      FALSE);
	editor_type = map_type_from_desktop_item (type, run_in_terminal);
	g_free (type);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (dialog->priv->type_combo));
	if (!gtk_tree_model_get_iter_first (model, &iter))
		g_assert_not_reached ();
	do {
		gtk_tree_model_get (model, &iter, COLUMN_TYPE, &buf_type, -1);
		if (editor_type == buf_type) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (dialog->priv->type_combo),
						       &iter);
			break;
		}
	} while (gtk_tree_model_iter_next (model, &iter));

	g_assert (editor_type == buf_type ||
		  editor_type == PANEL_DITEM_EDITOR_TYPE_NULL);

	/* Command */
	if (editor_type == PANEL_DITEM_EDITOR_TYPE_LINK)
		buffer = panel_key_file_get_string (key_file, "URL");
	else if (editor_type == PANEL_DITEM_EDITOR_TYPE_APPLICATION ||
		 editor_type == PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION)
		buffer = panel_key_file_get_string (key_file, "Exec");
	else
		buffer = NULL;

	gtk_entry_set_text (GTK_ENTRY (dialog->priv->command_entry),
			    buffer ? buffer : "");
	g_free (buffer);

	/* Comment */
	buffer = panel_key_file_get_locale_string (key_file, "Comment");
	gtk_entry_set_text (GTK_ENTRY (dialog->priv->comment_entry),
			    buffer ? buffer : "");
	g_free (buffer);


	/* Icon */
	buffer = panel_key_file_get_locale_string (key_file, "Icon");
	setup_icon_chooser (dialog, buffer);
	g_free (buffer);

	if (dialog->priv->save_timeout != 0) {
		g_source_remove (dialog->priv->save_timeout);
		dialog->priv->save_timeout = 0;
	}
}

static gboolean
panel_ditem_editor_save (PanelDItemEditor *dialog,
			 gboolean          report_errors)
{
	GKeyFile   *key_file;
	const char *const_buf;
	GError     *error;

	g_return_val_if_fail (dialog != NULL, FALSE);
	g_return_val_if_fail (dialog->priv->save_uri != NULL ||
			      dialog->priv->uri != NULL, FALSE);

	if (dialog->priv->save_timeout != 0)
		g_source_remove (dialog->priv->save_timeout);
	dialog->priv->save_timeout = 0;

	if (!dialog->priv->dirty)
		return TRUE;

	/* Verify that the required informations are set */
	const_buf = gtk_entry_get_text (GTK_ENTRY (dialog->priv->name_entry));
	if (const_buf == NULL || const_buf [0] == '\0') {
		if (report_errors) {
			if (!dialog->priv->type_directory)
				g_signal_emit (G_OBJECT (dialog),
					       ditem_edit_signals[ERROR_REPORTED], 0,
					       _("Could not save launcher"),
					       _("The name of the launcher is not set."));
			else
				g_signal_emit (G_OBJECT (dialog),
					       ditem_edit_signals[ERROR_REPORTED], 0,
					       _("Could not save directory properties"),
					       _("The name of the directory is not set."));
		}
		return FALSE;
	}

	const_buf = gtk_entry_get_text (GTK_ENTRY (dialog->priv->command_entry));
	if (!dialog->priv->type_directory &&
	    (const_buf == NULL || const_buf [0] == '\0')) {
		PanelDItemEditorType  type;
		char                 *err;

		type = panel_ditem_editor_get_item_type (dialog);

		switch (type) {
		case PANEL_DITEM_EDITOR_TYPE_APPLICATION:
		case PANEL_DITEM_EDITOR_TYPE_TERMINAL_APPLICATION:
			err = _("The command of the launcher is not set.");
			break;
		case PANEL_DITEM_EDITOR_TYPE_LINK:
			err = _("The location of the launcher is not set.");
			break;
		default:
			g_assert_not_reached ();
		}

		if (report_errors)
			g_signal_emit (G_OBJECT (dialog),
				       ditem_edit_signals[ERROR_REPORTED], 0,
				       _("Could not save launcher"), err);
		return FALSE;
	}

	key_file = dialog->priv->key_file;

	panel_key_file_ensure_C_key (key_file, "Name");
	panel_key_file_ensure_C_key (key_file, "Comment");
	panel_key_file_ensure_C_key (key_file, "Icon");

	if (dialog->priv->save_uri) {
		char *uri;

		uri = dialog->priv->save_uri (dialog,
					      dialog->priv->save_uri_data);

		if (uri) {
			panel_ditem_editor_set_uri (dialog, uri);
			g_free (uri);
		}
	}

	/* And now, try to save */
	error = NULL;
	panel_key_file_to_file (dialog->priv->key_file,
				dialog->priv->uri,
				&error);
	if (error != NULL) {
		if (report_errors)
			g_signal_emit (G_OBJECT (dialog),
				       ditem_edit_signals[ERROR_REPORTED], 0,
				       _("Could not save launcher"),
				       error->message);
		g_error_free (error);
		return FALSE;
	} else {
		g_signal_emit (G_OBJECT (dialog),
			       ditem_edit_signals[SAVED], 0);
	}

	dialog->priv->dirty = FALSE;

	return TRUE;
}

static gboolean
panel_ditem_editor_save_timeout (gpointer data)
{
	PanelDItemEditor *dialog;

	dialog = PANEL_DITEM_EDITOR (data);
	panel_ditem_editor_save (dialog, FALSE);

	return FALSE;
}

static void
response_cb (GtkDialog *dialog,
	     gint       response_id)
{
	GError *error = NULL;

	switch (response_id) {
	case GTK_RESPONSE_HELP:
		if (!panel_show_help (gtk_window_get_screen (GTK_WINDOW (dialog)),
				      "mate-user-guide", "gospanel-52", &error)) {
			g_signal_emit (G_OBJECT (dialog),
				       ditem_edit_signals[ERROR_REPORTED], 0,
				       _("Could not display help document"),
				       error->message);
			g_error_free (error);
		}
		break;
	case REVERT_BUTTON:
		panel_ditem_editor_revert (PANEL_DITEM_EDITOR (dialog));
		gtk_dialog_set_response_sensitive (dialog,
						   REVERT_BUTTON,
						   FALSE);
		break;
	case GTK_RESPONSE_OK:
	case GTK_RESPONSE_CLOSE:
		if (panel_ditem_editor_save (PANEL_DITEM_EDITOR (dialog), TRUE))
			gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_DELETE_EVENT:
		if (!PANEL_DITEM_EDITOR (dialog)->priv->new_file)
			/* We need to revert the changes */
			gtk_dialog_response (dialog, REVERT_BUTTON);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_CANCEL:
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
panel_ditem_editor_revert (PanelDItemEditor *dialog)
{
	int       i;
	char     *string;
	gboolean  boolean;
	GKeyFile *key_file;
	GKeyFile *revert_key_file;

	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	dialog->priv->reverting = TRUE;

	key_file = dialog->priv->key_file;
	revert_key_file = dialog->priv->revert_key_file;

	g_assert (revert_key_file != NULL);

	for (i = 0; i < G_N_ELEMENTS (revert_keys); i++) {
		if (revert_keys [i].type == G_TYPE_STRING) {
			if (revert_keys [i].locale) {
				string = panel_key_file_get_locale_string (
						revert_key_file,
						revert_keys [i].key);
				if (string == NULL)
					panel_key_file_remove_all_locale_key (
							key_file,
							revert_keys [i].key);
				else
					panel_key_file_set_locale_string (
							key_file,
							revert_keys [i].key,
							string);
			} else {
				string = panel_key_file_get_string (
						revert_key_file,
						revert_keys [i].key);
				if (string == NULL)
					panel_key_file_remove_key (
							key_file,
							revert_keys [i].key);
				else
					panel_key_file_set_string (
							key_file,
							revert_keys [i].key,
							string);
			}
			g_free (string);
		} else if (revert_keys [i].type == G_TYPE_BOOLEAN) {
			boolean = panel_key_file_get_boolean (
					revert_key_file,
					revert_keys [i].key,
					revert_keys [i].default_value);
			panel_key_file_set_boolean (key_file,
						    revert_keys [i].key,
						    boolean);
		} else {
			g_assert_not_reached ();
		}
	}

	panel_ditem_editor_sync_display (dialog);

	if (!dialog->priv->new_file) {
		if (dialog->priv->save_timeout != 0)
			g_source_remove (dialog->priv->save_timeout);

		dialog->priv->save_timeout = g_timeout_add_seconds (
							SAVE_FREQUENCY,
							panel_ditem_editor_save_timeout,
							dialog);
	}

	dialog->priv->reverting = FALSE;
}

static void
panel_ditem_editor_set_revert (PanelDItemEditor *dialog)
{
	int       i;
	char     *string;
	gboolean  boolean;
	GKeyFile *key_file;
	GKeyFile *revert_key_file;

	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	key_file = dialog->priv->key_file;
	if (dialog->priv->revert_key_file)
		g_key_file_free (dialog->priv->revert_key_file);
	dialog->priv->revert_key_file = g_key_file_new ();
	revert_key_file = dialog->priv->revert_key_file;

	for (i = 0; i < G_N_ELEMENTS (revert_keys); i++) {
		if (revert_keys [i].type == G_TYPE_STRING) {
			if (revert_keys [i].locale) {
				string = panel_key_file_get_locale_string (
						key_file,
						revert_keys [i].key);
				if (string != NULL)
					panel_key_file_set_locale_string (
							revert_key_file,
							revert_keys [i].key,
							string);
			} else {
				string = panel_key_file_get_string (
						key_file,
						revert_keys [i].key);
				if (string != NULL)
					panel_key_file_set_string (
							revert_key_file,
							revert_keys [i].key,
							string);
			}
			g_free (string);
		} else if (revert_keys [i].type == G_TYPE_BOOLEAN) {
			boolean = panel_key_file_get_boolean (
					key_file,
					revert_keys [i].key,
					revert_keys [i].default_value);
			panel_key_file_set_boolean (revert_key_file,
						    revert_keys [i].key,
						    boolean);
		} else {
			g_assert_not_reached ();
		}
	}
}

static void
panel_ditem_editor_key_file_loaded (PanelDItemEditor  *dialog)
{
	/* the user is not changing any value here, so block the signals about
	 * changing a value */
	panel_ditem_editor_block_signals (dialog);
	panel_ditem_editor_sync_display (dialog);
	panel_ditem_editor_unblock_signals (dialog);

	/* This should be after panel_ditem_editor_sync_display ()
	 * so the revert button is insensitive */
	if (dialog->priv->revert_key_file != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
						   REVERT_BUTTON,
						   TRUE);
	else
		panel_ditem_editor_set_revert (dialog);
}

static gboolean
panel_ditem_editor_load_uri (PanelDItemEditor  *dialog,
			     GError           **error)
{
        GKeyFile *key_file;

	g_return_val_if_fail (PANEL_IS_DITEM_EDITOR (dialog), FALSE);
        g_return_val_if_fail (dialog->priv->uri != NULL, FALSE);

	key_file = g_key_file_new ();

	if (!panel_key_file_load_from_uri (key_file,
					   dialog->priv->uri,
					   G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
					   error)) {
		g_key_file_free (key_file);
		return FALSE;
	}

	if (dialog->priv->free_key_file && dialog->priv->key_file)
		g_key_file_free (dialog->priv->key_file);
	dialog->priv->key_file = key_file;

	panel_ditem_editor_key_file_loaded (dialog);

	return TRUE;
}

static GtkWidget *
panel_ditem_editor_new_full (GtkWindow   *parent,
			     GKeyFile    *key_file,
			     const char  *uri,
			     const char  *title,
			     gboolean     type_directory)
{
	GtkWidget *dialog;

	dialog = g_object_new (PANEL_TYPE_DITEM_EDITOR,
			       "title", title,
			       "keyfile", key_file,
			       "uri", uri,
			       "type-directory", type_directory,
			       NULL);

	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

	return dialog;
}

GtkWidget *
panel_ditem_editor_new (GtkWindow   *parent,
			GKeyFile    *key_file,
			const char  *uri,
			const char  *title)
{
	return panel_ditem_editor_new_full (parent, key_file, uri,
					    title, FALSE);
}

GtkWidget *
panel_ditem_editor_new_directory (GtkWindow   *parent,
				  GKeyFile    *key_file,
				  const char  *uri,
				  const char  *title)
{
	return panel_ditem_editor_new_full (parent, key_file, uri,
					    title, TRUE);
}

static void
panel_ditem_editor_set_key_file (PanelDItemEditor *dialog,
				 GKeyFile         *key_file)
{
	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	if (dialog->priv->key_file == key_file)
		return;

	if (dialog->priv->free_key_file && dialog->priv->key_file)
		g_key_file_free (dialog->priv->key_file);
	dialog->priv->key_file = key_file;

	g_object_notify (G_OBJECT (dialog), "keyfile");
}

void
panel_ditem_editor_set_uri (PanelDItemEditor *dialog,
			    const char       *uri)
{
	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	if (!dialog->priv->uri && (!uri || !uri [0]))
		return;

	if (dialog->priv->uri && uri && uri [0] &&
	    !strcmp (dialog->priv->uri, uri))
		return;

	if (dialog->priv->uri)
		g_free (dialog->priv->uri);
	dialog->priv->uri = NULL;

	if (uri && uri [0])
		dialog->priv->uri = g_strdup (uri);

	g_object_notify (G_OBJECT (dialog), "uri");
}

static void
panel_ditem_editor_set_type_directory (PanelDItemEditor *dialog,
				       gboolean          type_directory)
{
	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	if (dialog->priv->type_directory == type_directory)
		return;

	dialog->priv->type_directory = type_directory;

	g_object_notify (G_OBJECT (dialog), "type-directory");
}

GKeyFile *
panel_ditem_editor_get_key_file (PanelDItemEditor *dialog)
{
	g_return_val_if_fail (PANEL_IS_DITEM_EDITOR (dialog), NULL);

	return dialog->priv->key_file;
}

GKeyFile *
panel_ditem_editor_get_revert_key_file (PanelDItemEditor *dialog)
{
	g_return_val_if_fail (PANEL_IS_DITEM_EDITOR (dialog), NULL);

	return dialog->priv->revert_key_file;
}

const char* panel_ditem_editor_get_uri(PanelDItemEditor* dialog)
{
	g_return_val_if_fail(PANEL_IS_DITEM_EDITOR(dialog), NULL);

	return dialog->priv->uri;
}

static gboolean
panel_ditem_editor_get_type_directory (PanelDItemEditor *dialog)
{
	g_return_val_if_fail (PANEL_IS_DITEM_EDITOR (dialog), FALSE);

	return dialog->priv->type_directory;
}

void
panel_ditem_register_save_uri_func (PanelDItemEditor  *dialog,
				    PanelDitemSaveUri  save_uri,
				    gpointer           data)
{
	g_return_if_fail (PANEL_IS_DITEM_EDITOR (dialog));

	dialog->priv->save_uri = save_uri;
	dialog->priv->save_uri_data = data;
}
