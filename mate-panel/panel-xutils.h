/*
 * panel-xutils.h: X related utility methods.
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

#ifndef __PANEL_XUTILS_H__
#define __PANEL_XUTILS_H__

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#include "panel-enums.h"

#ifdef __cplusplus
extern "C" {
#endif

void panel_xutils_set_strut       (GdkWindow             *gdk_window,
				   PanelOrientation       orientation,
				   guint32                strut,
				   guint32                strut_start,
				   guint32                strut_end);

void panel_warp_pointer           (GdkWindow             *gdk_window,
				   int                    x,
				   int                    y);

guint panel_get_real_modifier_mask (guint modifier_mask);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_XUTILS_H__ */
