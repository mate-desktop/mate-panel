/*
 * panel-test-applets.c:
 *
 * Authors:
 *    Mark McLoughlin <mark@skynet.ie>
 *
 * Copyright 2002 Sun Microsystems, Inc.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <mateconf/mateconf.h>

#include <libpanel-util/panel-cleanup.h>

#include <libmate-panel-applet-private/mate-panel-applet-container.h>
#include <libmate-panel-applet-private/mate-panel-applets-manager-dbus.h>

#include "panel-modules.h"

G_GNUC_UNUSED void on_execute_button_clicked (GtkButton *button, gpointer dummy);

static GtkWidget *win = NULL;
static GtkWidget *applet_combo = NULL;
static GtkWidget *prefs_dir_entry = NULL;
static GtkWidget *orient_combo = NULL;
static GtkWidget *size_combo = NULL;

static char *cli_iid = NULL;
static char *cli_prefs_dir = NULL;
static char *cli_size = NULL;
static char *cli_orient = NULL;

static const GOptionEntry options [] = {
	{ "iid", 0, 0, G_OPTION_ARG_STRING, &cli_iid, N_("Specify an applet IID to load"), NULL},
	{ "prefs-dir", 0, 0, G_OPTION_ARG_STRING, &cli_prefs_dir, N_("Specify a mateconf location in which the applet preferences should be stored"), NULL},
	{ "size", 0, 0, G_OPTION_ARG_STRING, &cli_size, N_("Specify the initial size of the applet (xx-small, medium, large etc.)"), NULL},
	{ "orient", 0, 0, G_OPTION_ARG_STRING, &cli_orient, N_("Specify the initial orientation of the applet (top, bottom, left or right)"), NULL},
	{ NULL}
};

enum {
	COLUMN_TEXT,
	COLUMN_ITEM,
	NUMBER_COLUMNS
};

typedef struct {
	const char *name;
	guint       value;
} ComboItem;

static ComboItem orient_items [] = {
	{ NC_("Orientation", "Top"),    PANEL_ORIENTATION_TOP    },
	{ NC_("Orientation", "Bottom"), PANEL_ORIENTATION_BOTTOM },
	{ NC_("Orientation", "Left"),   PANEL_ORIENTATION_LEFT   },
	{ NC_("Orientation", "Right"),  PANEL_ORIENTATION_RIGHT  }
};


static ComboItem size_items [] = {
	{ NC_("Size", "XX Small"), 12  },
	{ NC_("Size", "X Small"),  24  },
	{ NC_("Size", "Small"),    36  },
	{ NC_("Size", "Medium"),   48  },
	{ NC_("Size", "Large"),    64  },
	{ NC_("Size", "X Large"),  80  },
	{ NC_("Size", "XX Large"), 128 }
};

static guint
get_combo_value (GtkWidget *combo_box)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	guint         value;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter))
		return 0;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
	gtk_tree_model_get (model, &iter, COLUMN_ITEM, &value, -1);

	return value;
}

static gchar *
get_combo_applet_id (GtkWidget *combo_box)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	char         *value;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter))
		return NULL;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
	gtk_tree_model_get (model, &iter, COLUMN_ITEM, &value, -1);

	return value;
}

static void
applet_broken_cb (GtkWidget *container,
		  GtkWidget *window)
{
	gtk_widget_destroy (window);
}

static void
applet_activated_cb (GObject      *source_object,
		     GAsyncResult *res,
		     GtkWidget    *applet_window)
{
	GError *error = NULL;

	if (!mate_panel_applet_container_add_finish (MATE_PANEL_APPLET_CONTAINER (source_object),
						res, &error)) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (applet_window),
						 GTK_DIALOG_MODAL|
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Failed to load applet %s"),
						 error->message); // FIXME
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		return;
	}

	gtk_widget_show (applet_window);
}

static void
load_applet_into_window (const char *title,
			 const char *prefs_key,
			 guint       size,
			 guint       orientation)
{
	GtkWidget       *container;
	GtkWidget       *applet_window;
	GVariantBuilder  builder;

	container = mate_panel_applet_container_new ();

	applet_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	//FIXME: we could set the window icon with the applet icon
	gtk_window_set_title (GTK_WINDOW (applet_window), title);
	gtk_container_add (GTK_CONTAINER (applet_window), container);
	gtk_widget_show (container);

	g_signal_connect (container, "applet-broken",
			  G_CALLBACK (applet_broken_cb),
			  applet_window);

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&builder, "{sv}",
			       "prefs-key", g_variant_new_string (prefs_key));
	g_variant_builder_add (&builder, "{sv}",
			       "size", g_variant_new_uint32 (size));
	g_variant_builder_add (&builder, "{sv}",
			       "orient", g_variant_new_uint32 (orientation));
	mate_panel_applet_container_add (MATE_PANEL_APPLET_CONTAINER (container),
				    gtk_widget_get_screen (applet_window),
				    title, NULL,
				    (GAsyncReadyCallback)applet_activated_cb,
				    applet_window,
				    g_variant_builder_end (&builder));
}

static void
load_applet_from_command_line (void)
{
	guint size = 24, orient = PANEL_ORIENTATION_TOP;
	gint i;

	g_assert (cli_iid != NULL);

	if (cli_size || cli_orient) {
		if (cli_size) {
			for (i = 0; i < G_N_ELEMENTS (size_items); i++) {
				if (strcmp (g_dpgettext2 (NULL, "Size", size_items[i].name), cli_size) == 0) {
					size = size_items[i].value;
					break;
				}
			}
		}

		if (cli_orient) {
			for (i = 0; i < G_N_ELEMENTS (orient_items); i++) {
				if (strcmp (g_dpgettext2 (NULL, "Orientation", orient_items[i].name), cli_orient) == 0) {
					orient = orient_items[i].value;
					break;
				}
			}
		}
	}

	g_print ("Loading %s\n", cli_iid);

	load_applet_into_window (cli_iid, cli_prefs_dir, size, orient);
}

G_GNUC_UNUSED void
on_execute_button_clicked (GtkButton *button,
			   gpointer   dummy)
{
	char *title;

	title = get_combo_applet_id (applet_combo);

	load_applet_into_window (title,
				 gtk_entry_get_text (GTK_ENTRY (prefs_dir_entry)),
				 get_combo_value (size_combo),
				 get_combo_value (orient_combo));
	g_free (title);
}

static void
setup_combo (GtkWidget  *combo_box,
	     ComboItem  *items,
	     const char *context,
	     int         nb_items)
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
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    COLUMN_TEXT, g_dpgettext2 (NULL, context, items [i].name),
				    COLUMN_ITEM, items [i].value,
				    -1);
	}

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box),
				    renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box),
					renderer, "text", COLUMN_TEXT, NULL);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
}

static void
setup_options (void)
{
	MatePanelAppletsManager *manager;
	GList               *applet_list, *l;
	int                  i;
	char                *prefs_dir;
	char                *unique_key;
	GtkListStore        *model;
	GtkTreeIter          iter;
	GtkCellRenderer     *renderer;

	model = gtk_list_store_new (NUMBER_COLUMNS,
				    G_TYPE_STRING,
				    G_TYPE_STRING);

	gtk_combo_box_set_model (GTK_COMBO_BOX (applet_combo),
				 GTK_TREE_MODEL (model));

	manager = g_object_new (PANEL_TYPE_APPLETS_MANAGER_DBUS, NULL);
	applet_list = MATE_PANEL_APPLETS_MANAGER_GET_CLASS (manager)->get_applets (manager);
	for (l = applet_list, i = 1; l; l = g_list_next (l), i++) {
		MatePanelAppletInfo *info = (MatePanelAppletInfo *)l->data;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    COLUMN_TEXT, g_strdup (mate_panel_applet_info_get_name (info)),
				    COLUMN_ITEM, g_strdup (mate_panel_applet_info_get_iid (info)),
				    -1);
	}
	g_list_free (applet_list);
	g_object_unref (manager);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (applet_combo),
				    renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (applet_combo),
					renderer, "text", COLUMN_TEXT, NULL);

	gtk_combo_box_set_active (GTK_COMBO_BOX (applet_combo), 0);

	setup_combo (size_combo, size_items, "Size",
		     G_N_ELEMENTS (size_items));
	setup_combo (orient_combo, orient_items, "Orientation",
		     G_N_ELEMENTS (orient_items));

	unique_key = mateconf_unique_key ();
	prefs_dir = g_strdup_printf ("/tmp/%s", unique_key);
	g_free (unique_key);
	gtk_entry_set_text (GTK_ENTRY (prefs_dir_entry), prefs_dir);
	g_free (prefs_dir);
}

int
main (int argc, char **argv)
{
	GtkBuilder *builder;
	char       *uifile;
	char       *applets_dir;
	GError     *error;

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	error = NULL;
	if (!gtk_init_with_args (&argc, &argv,
				 "", (GOptionEntry *) options, GETTEXT_PACKAGE,
				 &error)) {
		if (error) {
			g_printerr ("%s\n", error->message);
			g_error_free (error);
		} else
			g_printerr ("Cannot initiliaze GTK+.\n");

		return 1;
	}

	panel_modules_ensure_loaded ();

	if (g_file_test ("../libmate-panel-applet", G_FILE_TEST_IS_DIR)) {
		applets_dir = g_strdup_printf ("%s:../libmate-panel-applet", MATE_PANEL_APPLETS_DIR);
		g_setenv ("MATE_PANEL_APPLETS_DIR", applets_dir, FALSE);
		g_free (applets_dir);
	}

	if (cli_iid) {
		load_applet_from_command_line ();
		gtk_main ();
		panel_cleanup_do ();

		return 0;
	}

	builder = gtk_builder_new ();
	gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);

	uifile = BUILDERDIR "/panel-test-applets.ui";
	gtk_builder_add_from_file (builder, uifile, &error);

	if (error) {
		g_warning ("Error loading \"%s\": %s", uifile, error->message);
		g_error_free (error);
		panel_cleanup_do ();

		return 1;
	}

	gtk_builder_connect_signals (builder, NULL);

	win             = GTK_WIDGET (gtk_builder_get_object (builder,
							      "toplevel"));
	applet_combo    = GTK_WIDGET (gtk_builder_get_object (builder,
							      "applet-combo"));
	prefs_dir_entry = GTK_WIDGET (gtk_builder_get_object (builder,
							      "prefs-dir-entry"));
	orient_combo    = GTK_WIDGET (gtk_builder_get_object (builder,
							      "orient-combo"));
	size_combo      = GTK_WIDGET (gtk_builder_get_object (builder,
							      "size-combo"));
	g_object_unref (builder);

	setup_options ();

	gtk_widget_show (win);

	gtk_main ();

	panel_cleanup_do ();

	return 0;
}
