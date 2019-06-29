#include "config.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libpanel-util/panel-error.h>
#include <libpanel-util/panel-keyfile.h>

#include "panel-ditem-editor.h"
#include "panel-icon-names.h"
#include "panel-util.h"

/* FIXME Symbols needed by panel-util.c - sucky */
#include "applet.h"
GSList *mate_panel_applet_list_applets (void) { return NULL; }
#include "panel-config-global.h"
gboolean panel_global_config_get_tooltips_enabled (void) { return FALSE; }
#include "panel-lockdown.h"
gboolean panel_lockdown_get_disable_lock_screen (void) { return FALSE; }

static int dialogs = 0;
static gboolean create_new = FALSE;
static char **desktops = NULL;

static GOptionEntry options[] = {
	{ "create-new", 0, 0, G_OPTION_ARG_NONE, &create_new, N_("Create new file in the given directory"), NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &desktops, NULL, N_("[FILE...]") },
	{ NULL }
};

static void
dialog_destroyed (GtkWidget *dialog, gpointer data)
{
	dialogs --;

	if (dialogs <= 0)
		gtk_main_quit ();
}

static void
validate_for_filename (char *file)
{
	char *ptr;

	g_return_if_fail (file != NULL);

	ptr = file;
	while (*ptr != '\0') {
		if (*ptr == '/')
			*ptr = '_';
		ptr++;
	}
}

static char *
find_uri_on_save (PanelDItemEditor *dialog,
		  gpointer          data)
{
	GKeyFile *keyfile;
	char     *name;
	char     *filename;
	char     *uri;
	char     *dir;

	keyfile = panel_ditem_editor_get_key_file (dialog);
	name = panel_key_file_get_string (keyfile, "Name");

	validate_for_filename (name);
	filename = g_filename_from_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);

	if (filename == NULL)
		filename = g_strdup ("foo");

	dir = g_object_get_data (G_OBJECT (dialog), "dir");
	uri = panel_make_unique_desktop_path_from_name (dir, filename);

	g_free (filename);

	return uri;
}

static void
error_reported (GtkWidget  *dialog,
		const char *primary,
		const char *secondary,
		gpointer    data)
{
	panel_error_dialog (GTK_WINDOW (dialog), NULL,
			    "error_editing_launcher", TRUE,
			    primary, secondary);
}

int
main (int argc, char * argv[])
{
	GError *error = NULL;
	int i;

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (!gtk_init_with_args (&argc, &argv,
	                         _("- Edit .desktop files"),
	                         options,
	                         GETTEXT_PACKAGE,
	                         &error)) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		return 1;
	}

	gtk_window_set_default_icon_name (PANEL_ICON_LAUNCHER);

	if (desktops == NULL ||
	    desktops[0] == NULL) {
		g_printerr ("mate-desktop-item-edit: no file to edit\n");
		return 0;
	}

	for (i = 0; desktops[i] != NULL; i++) {
		GFile     *file;
		GFileInfo *info;
		GFileType  type;
		char      *uri;
		char      *path;
		GtkWidget *dlg = NULL;

		file = g_file_new_for_commandline_arg (desktops[i]);
		uri  = g_file_get_uri (file);
		path = g_file_get_path (file);
		info = g_file_query_info (file, "standard::type",
					  G_FILE_QUERY_INFO_NONE, NULL, NULL);
		g_object_unref (file);

		if (info) {
			type = g_file_info_get_file_type (info);

			if (type == G_FILE_TYPE_DIRECTORY && create_new) {

				dlg = panel_ditem_editor_new (NULL, NULL, NULL,
							     _("Create Launcher"));
				g_object_set_data_full (G_OBJECT (dlg), "dir",
							g_strdup (path),
							(GDestroyNotify)g_free);

				panel_ditem_register_save_uri_func (PANEL_DITEM_EDITOR (dlg),
								    find_uri_on_save,
								    NULL);

			} else if (type == G_FILE_TYPE_DIRECTORY) {
				/* Rerun this iteration with the .directory
				 * file
				 * Note: No need to free, for one we can't free
				 * an individual member of desktops and
				 * secondly we will soon exit */
			        desktops[i] = g_build_path ("/", uri,
							    ".directory", NULL);
				i--;
			} else if (type == G_FILE_TYPE_REGULAR
				   && g_str_has_suffix (desktops [i],
					   		".directory")
				   && !create_new) {
				dlg = panel_ditem_editor_new_directory (NULL,
									NULL,
									uri,
									_("Directory Properties"));
			} else if (type == G_FILE_TYPE_REGULAR
				   && g_str_has_suffix (desktops [i],
					   		".desktop")
				   && !create_new) {
				dlg = panel_ditem_editor_new (NULL, NULL, uri,
							      _("Launcher Properties"));
			} else if (type == G_FILE_TYPE_REGULAR
				   && create_new) {
				g_printerr ("mate-desktop-item-edit: %s "
					    "already exists\n", uri);
			} else {
				g_printerr ("mate-desktop-item-edit: %s "
					    "does not look like a desktop "
					    "item\n", uri);
			}

			g_object_unref (info);

		} else if (g_str_has_suffix (desktops [i], ".directory")) {
			/* a non-existant file.  Well we can still edit that
			 * sort of.  We will just create it new */
			dlg = panel_ditem_editor_new_directory (NULL, NULL, uri,
								_("Directory Properties"));

		} else if (g_str_has_suffix (desktops [i], ".desktop")) {
			/* a non-existant file.  Well we can still edit that
			 * sort of.  We will just create it new */
			dlg = panel_ditem_editor_new (NULL, NULL, uri,
						      _("Create Launcher"));

		} else {
			g_printerr ("mate-desktop-item-edit: %s does not "
				    "have a .desktop or .directory "
				    "suffix\n", uri);
		}

		if (dlg != NULL) {
			dialogs ++;
			g_signal_connect (G_OBJECT (dlg), "destroy",
					  G_CALLBACK (dialog_destroyed), NULL);
			g_signal_connect (G_OBJECT (dlg), "error_reported",
					  G_CALLBACK (error_reported), NULL);
			gtk_widget_show (dlg);
		}

		g_free (uri);
		g_free (path);
	}

	if (dialogs > 0)
		gtk_main ();

        return 0;
}
