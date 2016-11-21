/*
 * panel-force-quit.c:
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "panel-force-quit.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <X11/extensions/XInput2.h>

#include "panel-icon-names.h"
#include "panel-stock-icons.h"

static GdkFilterReturn popup_filter (GdkXEvent *gdk_xevent,
				     GdkEvent  *event,
				     GtkWidget *popup);

static Atom wm_state_atom = None;

static GtkWidget *
display_popup_window (GdkScreen *screen)
{
	GtkWidget     *retval;
	GtkWidget     *vbox;
	GtkWidget     *image;
	GtkWidget     *frame;
	GtkWidget     *label;
	int            screen_width, screen_height;
	GtkAllocation  allocation;

	retval = gtk_window_new (GTK_WINDOW_POPUP);
	atk_object_set_role (gtk_widget_get_accessible (retval), ATK_ROLE_ALERT);
	gtk_window_set_screen (GTK_WINDOW (retval), screen);
	gtk_window_stick (GTK_WINDOW (retval));
	gtk_widget_add_events (retval, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_add (GTK_CONTAINER (retval), frame);
	gtk_widget_show (frame);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
	gtk_container_add (GTK_CONTAINER (frame), vbox);
	gtk_widget_show (vbox);

	image = gtk_image_new_from_icon_name (PANEL_ICON_FORCE_QUIT,
					      GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
	gtk_box_pack_start (GTK_BOX (vbox), image, TRUE, TRUE, 4);
	gtk_widget_show (image);

	label = gtk_label_new (_("Click on a window to force the application to quit. "
				 "To cancel press <ESC>."));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 4);
	gtk_widget_show (label);

	gtk_widget_realize (retval);

	screen_width  = gdk_screen_get_width  (screen);
	screen_height = gdk_screen_get_height (screen);

	gtk_widget_get_allocation (retval, &allocation);

	gtk_window_move (GTK_WINDOW (retval),
			 (screen_width  - allocation.width) / 2,
			 (screen_height - allocation.height) / 2);

	gtk_widget_show (GTK_WIDGET (retval));

	return retval;
}

static void
remove_popup (GtkWidget *popup)
{
	GdkWindow        *root;
	GdkDisplay       *display;
#if GTK_CHECK_VERSION (3, 20, 0)
	GdkSeat          *seat;
#else
	GdkDevice        *pointer;
	GdkDevice        *keyboard;
	GdkDeviceManager *device_manager;
#endif
	root = gdk_screen_get_root_window (
			gtk_window_get_screen (GTK_WINDOW (popup)));
	gdk_window_remove_filter (root, (GdkFilterFunc) popup_filter, popup);

	gtk_widget_destroy (popup);

	display = gdk_window_get_display (root);
#if GTK_CHECK_VERSION (3, 20, 0)
	seat = gdk_display_get_default_seat (display);

	gdk_seat_ungrab (seat);
#else
	device_manager = gdk_display_get_device_manager (display);
	pointer = gdk_device_manager_get_client_pointer (device_manager);
	keyboard = gdk_device_get_associated_device (pointer);

	gdk_device_ungrab (pointer, GDK_CURRENT_TIME);
	gdk_device_ungrab (keyboard, GDK_CURRENT_TIME);
#endif
}

static gboolean
wm_state_set (Display *display,
	      Window   window)
{
	gulong  nitems;
	gulong  bytes_after;
	gulong *prop;
	Atom    ret_type = None;
	int     ret_format;
	int     result;

	gdk_error_trap_push ();
	result = XGetWindowProperty (display, window, wm_state_atom,
				     0, G_MAXLONG, False, wm_state_atom,
				     &ret_type, &ret_format, &nitems,
				     &bytes_after, (gpointer) &prop);

	if (gdk_error_trap_pop ())
		return FALSE;

	if (result != Success)
		return FALSE;

	XFree (prop);

	if (ret_type != wm_state_atom)
		return FALSE;

	return TRUE;
}

static Window 
find_managed_window (Display *display,
		     Window   window)
{
	Window  root;
	Window  parent;
	Window *kids = NULL;
	Window  retval;
	guint   nkids;
	int     i, result;

	if (wm_state_set (display, window))
		return window;

	gdk_error_trap_push ();
	result = XQueryTree (display, window, &root, &parent, &kids, &nkids);
	if (gdk_error_trap_pop () || !result)
		return None;

	retval = None;

	for (i = 0; i < nkids; i++) {
		if (wm_state_set (display, kids [i])) {
			retval = kids [i];
			break;
		}

		retval = find_managed_window (display, kids [i]);
		if (retval != None)
			break;
	}

	if (kids)
		XFree (kids);

	return retval;
}

static void
kill_window_response (GtkDialog *dialog,
		      gint       response_id,
		      gpointer   user_data)
{
	if (response_id == GTK_RESPONSE_ACCEPT) {
		Display *display;
		Window window = (Window) user_data;

		display = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (dialog)));

		gdk_error_trap_push ();
		XKillClient (display, window);
		gdk_flush ();
		gdk_error_trap_pop_ignored ();
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/* From marco */
static void
kill_window_question (gpointer window)
{
	GtkWidget *dialog;
 
	dialog = gtk_message_dialog_new (NULL, 0,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Force this application to exit?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  _("If you choose to force an application "
						  "to exit, unsaved changes in any open documents "
						  "in it might get lost."));

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_CANCEL,
				PANEL_STOCK_FORCE_QUIT,
				GTK_RESPONSE_ACCEPT,
				NULL);
 
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_CANCEL);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Force Quit"));

	g_signal_connect (dialog, "response",
			  G_CALLBACK (kill_window_response), window);

	gtk_widget_show (dialog);
}

static void 
handle_button_press_event (GtkWidget *popup,
			   Display *display,
			   Window subwindow)
{
	Window window;

	remove_popup (popup);

	if (subwindow == None)
		return;

	if (wm_state_atom == None)
		wm_state_atom = XInternAtom (display, "WM_STATE", FALSE);

	window = find_managed_window (display, subwindow);

	if (window != None) {
		if (!gdk_x11_window_lookup_for_display (gdk_x11_lookup_xdisplay (display), window))
			kill_window_question ((gpointer) window);
	}
}

static GdkFilterReturn
popup_filter (GdkXEvent *gdk_xevent,
	      GdkEvent  *event,
	      GtkWidget *popup)
{
	XEvent *xevent = (XEvent *) gdk_xevent;
	XIEvent *xiev;
	XIDeviceEvent *xidev;

	switch (xevent->type) {
	case ButtonPress:
		handle_button_press_event (popup, xevent->xbutton.display, xevent->xbutton.subwindow);
		return GDK_FILTER_REMOVE;
	case KeyPress:
		if (xevent->xkey.keycode == XKeysymToKeycode (xevent->xany.display, XK_Escape)) {
			remove_popup (popup);
			return GDK_FILTER_REMOVE;
		}
		break;
	case GenericEvent:
		xiev = (XIEvent *) xevent->xcookie.data;
		xidev = (XIDeviceEvent *) xiev;
		switch (xiev->evtype) {
		case XI_KeyPress:
			if (xidev->detail == XKeysymToKeycode (xevent->xany.display, XK_Escape)) {
				remove_popup (popup);
				return GDK_FILTER_REMOVE;
			}
			break;
		case XI_ButtonPress:
			handle_button_press_event (popup, xidev->display, xidev->child);
			return GDK_FILTER_REMOVE;
		}
		break;
	default:
		break;
	}

	return GDK_FILTER_CONTINUE;
}

void
panel_force_quit (GdkScreen *screen,
		  guint      time)
{
	GdkGrabStatus  status;
	GdkCursor     *cross;
	GtkWidget     *popup;
	GdkWindow     *root;
	GdkDisplay    *display;
	GdkDevice     *pointer;
	GdkDevice     *keyboard;
#if GTK_CHECK_VERSION (3, 20, 0)
	GdkSeat       *seat;
#else
	GdkDeviceManager *device_manager;
#endif

	popup = display_popup_window (screen);

	root = gdk_screen_get_root_window (screen);

	gdk_window_add_filter (root, (GdkFilterFunc) popup_filter, popup);
	cross = gdk_cursor_new_for_display (gdk_display_get_default (),
	                                    GDK_CROSS);
	display = gdk_window_get_display (root);
#if GTK_CHECK_VERSION (3, 20, 0)
	seat = gdk_display_get_default_seat (display);
	pointer = gdk_seat_get_pointer (seat);
#else
	device_manager = gdk_display_get_device_manager (display);
	pointer = gdk_device_manager_get_client_pointer (device_manager);
#endif
	keyboard = gdk_device_get_associated_device (pointer);

	status = gdk_device_grab (pointer, root,
				  GDK_OWNERSHIP_NONE, FALSE,
				  GDK_BUTTON_PRESS_MASK,
				  cross, time);

	g_object_unref (cross);

	status = gdk_device_grab (keyboard, root,
				  GDK_OWNERSHIP_NONE, FALSE,
				  GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK,
				  NULL, time);

	if (status != GDK_GRAB_SUCCESS) {
		g_warning ("Pointer grab failed\n");
		remove_popup (popup);
		return;
	}

	gdk_flush ();
}
