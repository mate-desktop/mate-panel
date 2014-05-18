/*
 * panel-action-protocol.h: _MATE_PANEL_ACTION protocol impl.
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

#include "panel-action-protocol.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include "menu.h"
#include "applet.h"
#include "panel-globals.h"
#include "panel-toplevel.h"
#include "panel-util.h"
#include "panel-force-quit.h"
#include "panel-run-dialog.h"
#include "panel-menu-button.h"
#include "panel-menu-bar.h"

static Atom atom_mate_panel_action            = None;
static Atom atom_gnome_panel_action           = None;
static Atom atom_mate_panel_action_main_menu  = None;
static Atom atom_mate_panel_action_run_dialog = None;
static Atom atom_gnome_panel_action_main_menu  = None;
static Atom atom_gnome_panel_action_run_dialog = None;
static Atom atom_mate_panel_action_kill_dialog = None;

static void
panel_action_protocol_main_menu (GdkScreen *screen,
				 guint32    activate_time)
{
	PanelWidget *panel_widget;
	GtkWidget   *menu;
	AppletInfo  *info;

	info = mate_panel_applet_get_by_type (PANEL_OBJECT_MENU_BAR, screen);
	if (info) {
		panel_menu_bar_popup_menu (PANEL_MENU_BAR (info->widget),
					   activate_time);
		return;
	}

	info = mate_panel_applet_get_by_type (PANEL_OBJECT_MENU, screen);
	if (info && !panel_menu_button_get_use_menu_path (PANEL_MENU_BUTTON (info->widget))) {
		panel_menu_button_popup_menu (PANEL_MENU_BUTTON (info->widget),
					      1, activate_time);
		return;
	}

	panel_widget = panels->data;
	menu = create_main_menu (panel_widget);

	panel_toplevel_push_autohide_disabler (panel_widget->toplevel);

	gtk_menu_set_screen (GTK_MENU (menu), screen);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			NULL, NULL, 0, activate_time);
}

static void
panel_action_protocol_run_dialog (GdkScreen *screen,
				  guint32    activate_time)
{
	panel_run_dialog_present (screen, activate_time);
}

static void
panel_action_protocol_kill_dialog (GdkScreen *screen,
				   guint32    activate_time)
{
	panel_force_quit (screen, activate_time);
}

static GdkFilterReturn
panel_action_protocol_filter (GdkXEvent *gdk_xevent,
			      GdkEvent  *event,
			      gpointer   data)
{
	GdkWindow *window;
	GdkScreen *screen;
#if GTK_CHECK_VERSION (3, 0, 0)
	GdkDisplay *display;
#endif
	XEvent    *xevent = (XEvent *) gdk_xevent;

	if (xevent->type != ClientMessage)
		return GDK_FILTER_CONTINUE;

	if ((xevent->xclient.message_type != atom_mate_panel_action) &&
	   (xevent->xclient.message_type != atom_gnome_panel_action))
		return GDK_FILTER_CONTINUE;

#if GTK_CHECK_VERSION (3, 0, 0)
	screen = gdk_event_get_screen (event);
	display = gdk_screen_get_display (screen);
	window = gdk_x11_window_lookup_for_display (display, xevent->xclient.window);
#else
	window = gdk_window_lookup (xevent->xclient.window);
	screen = gdk_drawable_get_screen (window);
#endif
	if (!window)
		return GDK_FILTER_CONTINUE;

#if GTK_CHECK_VERSION (3, 0, 0)
	if (window != gdk_screen_get_root_window (screen))
		return GDK_FILTER_CONTINUE;
#endif

	if (xevent->xclient.data.l [0] == atom_mate_panel_action_main_menu)
		panel_action_protocol_main_menu (screen, xevent->xclient.data.l [1]);
	else if (xevent->xclient.data.l [0] == atom_mate_panel_action_run_dialog)
		panel_action_protocol_run_dialog (screen, xevent->xclient.data.l [1]);
	else if (xevent->xclient.data.l [0] == atom_gnome_panel_action_main_menu)
		panel_action_protocol_main_menu (screen, xevent->xclient.data.l [1]);
	else if (xevent->xclient.data.l [0] == atom_gnome_panel_action_run_dialog)
		panel_action_protocol_run_dialog (screen, xevent->xclient.data.l [1]);
	else if (xevent->xclient.data.l [0] == atom_mate_panel_action_kill_dialog)
		panel_action_protocol_kill_dialog (screen, xevent->xclient.data.l [1]);
	else
		return GDK_FILTER_CONTINUE;

	return GDK_FILTER_REMOVE;
}

void
panel_action_protocol_init (void)
{
	GdkDisplay *display;

	display = gdk_display_get_default ();

	atom_mate_panel_action =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_MATE_PANEL_ACTION",
			     FALSE);
	atom_gnome_panel_action =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_GNOME_PANEL_ACTION",
			     FALSE);
	atom_mate_panel_action_main_menu =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_MATE_PANEL_ACTION_MAIN_MENU",
			     FALSE);
	atom_mate_panel_action_run_dialog =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_MATE_PANEL_ACTION_RUN_DIALOG",
			     FALSE);
	atom_gnome_panel_action_main_menu =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_GNOME_PANEL_ACTION_MAIN_MENU",
			     FALSE);
	atom_gnome_panel_action_run_dialog =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_GNOME_PANEL_ACTION_RUN_DIALOG",
			     FALSE);
	atom_mate_panel_action_kill_dialog =
		XInternAtom (GDK_DISPLAY_XDISPLAY (display),
			     "_MATE_PANEL_ACTION_KILL_DIALOG",
			     FALSE);

	/* We'll filter event sent on non-root windows later */
	gdk_window_add_filter (NULL, panel_action_protocol_filter, NULL);
}
