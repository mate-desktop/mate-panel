/*
 * panel-force-quit.h
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

#ifndef __PANEL_FORCE_QUIT_H__
#define __PANEL_FORCE_QUIT_H__

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

void panel_force_quit (GdkScreen *screen,
		       guint      time);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_FORCE_QUIT_H__ */
