/*
 * panel-xutils.c: X related utility methods.
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

#include "config.h"

#ifndef HAVE_X11
#error file should only be built when HAVE_X11 is enabled
#endif

#include "panel-xutils.h"

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static Atom net_wm_strut              = None;
static Atom net_wm_strut_partial      = None;

enum {
	STRUT_LEFT = 0,
	STRUT_RIGHT = 1,
	STRUT_TOP = 2,
	STRUT_BOTTOM = 3,
	STRUT_LEFT_START = 4,
	STRUT_LEFT_END = 5,
	STRUT_RIGHT_START = 6,
	STRUT_RIGHT_END = 7,
	STRUT_TOP_START = 8,
	STRUT_TOP_END = 9,
	STRUT_BOTTOM_START = 10,
	STRUT_BOTTOM_END = 11
};

void
panel_xutils_set_strut (GdkWindow        *gdk_window,
			PanelOrientation  orientation,
			guint32           strut,
			guint32           strut_start,
			guint32           strut_end)
 {
	Display *xdisplay;
	Window   window;
	gulong   struts [12] = { 0, };
	GdkDisplay *display;

	g_return_if_fail (GDK_IS_WINDOW (gdk_window));
	g_return_if_fail (GDK_IS_X11_DISPLAY (gdk_window_get_display (gdk_window)));

	xdisplay = GDK_WINDOW_XDISPLAY (gdk_window);
	window = GDK_WINDOW_XID (gdk_window);

	if (net_wm_strut == None)
		net_wm_strut = XInternAtom (xdisplay, "_NET_WM_STRUT", False);
	if (net_wm_strut_partial == None)
		net_wm_strut_partial = XInternAtom (xdisplay, "_NET_WM_STRUT_PARTIAL", False);

	switch (orientation) {
	case PANEL_ORIENTATION_LEFT:
		struts [STRUT_LEFT] = strut;
		struts [STRUT_LEFT_START] = strut_start;
		struts [STRUT_LEFT_END] = strut_end;
		break;
	case PANEL_ORIENTATION_RIGHT:
		struts [STRUT_RIGHT] = strut;
		struts [STRUT_RIGHT_START] = strut_start;
		struts [STRUT_RIGHT_END] = strut_end;
		break;
	case PANEL_ORIENTATION_TOP:
		struts [STRUT_TOP] = strut;
		struts [STRUT_TOP_START] = strut_start;
		struts [STRUT_TOP_END] = strut_end;
		break;
	case PANEL_ORIENTATION_BOTTOM:
		struts [STRUT_BOTTOM] = strut;
		struts [STRUT_BOTTOM_START] = strut_start;
		struts [STRUT_BOTTOM_END] = strut_end;
		break;
	}

	display = gdk_window_get_display (gdk_window);
	gdk_x11_display_error_trap_push (display);
	XChangeProperty (xdisplay, window, net_wm_strut,
			 XA_CARDINAL, 32, PropModeReplace,
			 (guchar *) &struts, 4);
	XChangeProperty (xdisplay, window, net_wm_strut_partial,
			 XA_CARDINAL, 32, PropModeReplace,
			 (guchar *) &struts, 12);
	gdk_x11_display_error_trap_pop_ignored (display);
}

void
panel_warp_pointer (GdkWindow *gdk_window,
		    int        x,
		    int        y)
{
	Display *xdisplay;
	Window   window;
	GdkDisplay *display;

	g_return_if_fail (GDK_IS_WINDOW (gdk_window));
	g_return_if_fail (GDK_IS_X11_DISPLAY (gdk_window_get_display (gdk_window)));

	xdisplay = GDK_WINDOW_XDISPLAY (gdk_window);
	window = GDK_WINDOW_XID (gdk_window);

	display = gdk_window_get_display (gdk_window);
	gdk_x11_display_error_trap_push (display);
	XWarpPointer (xdisplay, None, window, 0, 0, 0, 0, x, y);
	gdk_x11_display_error_trap_pop_ignored (display);
}

guint
panel_get_real_modifier_mask (guint mask)
{
	guint real_mask;
	Display *display;
	int i, min_keycode, max_keycode, keysyms_per_keycode;
	int max_keycodes_per_modifier;
	KeySym *keysyms_for_keycodes;
	XModifierKeymap *modifier_keymap;

	real_mask = mask & ((Mod5Mask << 1) - 1);

	/* Already real */
	if (mask == real_mask) {
		return mask;
	}

	g_return_val_if_fail (GDK_IS_X11_DISPLAY (gdk_display_get_default ()), mask);
	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	XDisplayKeycodes (display, &min_keycode, &max_keycode);
	keysyms_for_keycodes = XGetKeyboardMapping (display,
						    min_keycode,
						    max_keycode - min_keycode + 1,
						    &keysyms_per_keycode);

	modifier_keymap = XGetModifierMapping (display);
	max_keycodes_per_modifier = modifier_keymap->max_keypermod;

	/* Loop through all the modifiers and find out which "real"
	 * (Mod2..Mod5) modifiers Super, Hyper, and Meta are mapped to.
	 * Note, Mod1 is used by the Alt modifier */
	for (i = Mod2MapIndex * max_keycodes_per_modifier;
	     i < (Mod5MapIndex + 1) * max_keycodes_per_modifier;
	     i++) {
		int keycode;
		int j;
		KeySym *keysyms_for_keycode;
		int map_index;
		int map_mask;

		keycode = modifier_keymap->modifiermap[i];

		/* The array is sparse, there may be some
		 * empty entries.  Filter those out
		 * (along with any invalid entries) */
		if (keycode < min_keycode || keycode > max_keycode)
			continue;

		keysyms_for_keycode = keysyms_for_keycodes +
		                      (keycode - min_keycode) * keysyms_per_keycode;

		map_index = i / max_keycodes_per_modifier;

		g_assert (map_index <= Mod5MapIndex);

		map_mask = 1 << map_index;

		for (j = 0; j < keysyms_per_keycode; j++) {
			switch (keysyms_for_keycode[j]) {
				case XK_Super_L:
				case XK_Super_R:
					if (mask & GDK_SUPER_MASK)
						real_mask |= map_mask;
					break;
				case XK_Hyper_L:
				case XK_Hyper_R:
					if (mask & GDK_HYPER_MASK)
						real_mask |= map_mask;
					break;
				case XK_Meta_L:
				case XK_Meta_R:
					if (mask & GDK_META_MASK)
						real_mask |= map_mask;
					break;
				default:
					break;
			}
		}
	}

	XFreeModifiermap (modifier_keymap);
	XFree (keysyms_for_keycodes);

	return real_mask;
}
