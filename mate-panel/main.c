/* Mate panel: Initialization routines
 * (C) 1997,1998,1999,2000 the Free Software Foundation
 * (C) 2000 Eazel, Inc.
 *
 * Authors: Federico Mena
 *          Miguel de Icaza
 *          George Lebl
 */

#include <config.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#include <glib/gi18n.h>

#include <libegg/eggdesktopfile.h>
#include <libegg/eggsmclient.h>

#include <libpanel-util/panel-cleanup.h>
#include <libpanel-util/panel-glib.h>

#include "panel-profile.h"
#include "panel-config-global.h"
#include "panel-shell.h"
#include "panel-multiscreen.h"
#include "panel-session.h"
#include "panel-schemas.h"
#include "panel-stock-icons.h"
#include "panel-action-protocol.h"
#include "panel-lockdown.h"
#include "panel-icon-names.h"
#include "panel-reset.h"
#include "panel-run-dialog.h"
#include "xstuff.h"

#include "nothing.cP"

/* globals */
GSList *panels = NULL;
GSList *panel_list = NULL;

static char*    deprecated_profile;
static char*    layout;
static gboolean replace = FALSE;
static gboolean reset = FALSE;
static gboolean run_dialog = FALSE;

static const GOptionEntry options[] = {
  { "replace", 0, 0, G_OPTION_ARG_NONE, &replace, N_("Replace a currently running panel"), NULL },
  /* keep this for compatibilty with old MATE < 2.10 */
  { "profile", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &deprecated_profile, NULL, NULL },
  /* this feature was request in #mate irc channel */
  { "reset", 0, 0, G_OPTION_ARG_NONE, &reset, N_("Reset the panel configuration to default"), NULL },
  /* open run dialog */
  { "run-dialog", 0, 0, G_OPTION_ARG_NONE, &run_dialog, N_("Execute the run dialog"), NULL },
  /* default panels layout */
  { "layout", 0, 0, G_OPTION_ARG_STRING, &layout, N_("Set the default panel layout"), NULL },
  { NULL }
};

int
main (int argc, char **argv)
{
	char           *desktopfile;
	GOptionContext *context;
	GError         *error;

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* We will register explicitly when we're ready -- see panel-session.c */
	egg_sm_client_set_mode (EGG_SM_CLIENT_MODE_DISABLED);

	g_set_prgname ("mate-panel");

	desktopfile = panel_g_lookup_in_applications_dirs ("mate-panel.desktop");
	if (desktopfile) {
		egg_set_desktop_file (desktopfile);
		g_free (desktopfile);
	}

	context = g_option_context_new ("");
	g_option_context_add_group (context,
				    egg_sm_client_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	error = NULL;
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		g_option_context_free (context);

		return 1;
	}

	g_option_context_free (context);

	/* set the default layout */
	if (layout != NULL && layout[0] != 0)
	{
		GSettings *settings;
		settings = g_settings_new (PANEL_SCHEMA);
		g_settings_set_string (settings, PANEL_DEFAULT_LAYOUT, layout);
		g_object_unref (settings);
		g_message ("Panel layout set to '%s'", layout);
		/* exit, except if reset argument is given */
		if (!reset)
			return 0;
	}

	/* reset the configuration and exit. */
	if (reset == TRUE)
	{
		panel_reset();
		return 0;
	}

	/* open the run dialog and exit */
	if (run_dialog == TRUE)
	{
		panel_multiscreen_init ();
		panel_global_config_load ();
		panel_lockdown_init ();
		panel_profile_settings_load ();
		panel_run_dialog_present (gdk_screen_get_default (),
		                          gtk_get_current_event_time ());
		panel_run_dialog_quit_on_destroy ();
		gtk_main ();
		panel_lockdown_finalize ();
		panel_cleanup_do ();
		return 0;
	}

	if (!egg_get_desktop_file ()) {
		g_set_application_name (_("Panel"));
		gtk_window_set_default_icon_name (PANEL_ICON_PANEL);
	}

	if (!panel_shell_register (replace)) {
		panel_cleanup_do ();
		return -1;
	}

	panel_action_protocol_init ();
	panel_multiscreen_init ();
	panel_init_stock_icons_and_items ();

	panel_global_config_load ();
	panel_lockdown_init ();
	panel_profile_load ();

	/*add forbidden lists to ALL panels*/
	g_slist_foreach (panels,
	                 (GFunc)panel_widget_add_forbidden,
	                 NULL);

	xstuff_init ();

	/* Flush to make sure our struts are seen by everyone starting
	 * immediate after (eg, the caja desktop). */
	gdk_flush ();

	/* Do this at the end, to be sure that we're really ready when
	 * connecting to the session manager */
	panel_session_init ();

	gtk_main ();

	panel_lockdown_finalize ();

	panel_cleanup_do ();

	return 0;
}
