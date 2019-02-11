/*
 * panel-session.c: panel session management routines
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>
#include <stdlib.h>
#include <libegg/eggsmclient.h>

#ifdef HAVE_X11
#include "xstuff.h"
#endif

#include "panel-shell.h"
#include "panel-session.h"

static gboolean do_not_restart = FALSE;

static void
panel_session_handle_quit (EggSMClient *client,
			   gpointer     data)
{
	panel_shell_quit ();
}

void
panel_session_do_not_restart (void)
{
	do_not_restart = TRUE;

	if (egg_sm_client_get_mode () != EGG_SM_CLIENT_MODE_DISABLED)
		egg_sm_client_set_mode (EGG_SM_CLIENT_MODE_NO_RESTART);
}

void
panel_session_init (void)
{
	EggSMClientMode  mode;
	EggSMClient     *client;

	/* Explicitly tell the session manager we're ready -- we don't do it
	 * before. Note: this depends on setting the mode to DISABLED early
	 * during startup. */

        if (do_not_restart || getenv ("MATE_PANEL_DEBUG"))
		mode = EGG_SM_CLIENT_MODE_NO_RESTART;
	else
		mode = EGG_SM_CLIENT_MODE_NORMAL;

	egg_sm_client_set_mode (mode);

	client = egg_sm_client_get ();

	g_signal_connect (client, "quit",
			  G_CALLBACK (panel_session_handle_quit), NULL);

#ifdef HAVE_X11
	if (is_using_x11 ()) {
		/* We don't want the X WM to try and save/restore our
		* window position */
		gdk_x11_set_sm_client_id (NULL);
	}
#endif
}
