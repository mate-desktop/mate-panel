/*
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

 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __PANEL_STRUTS_H__
#define __PANEL_STRUTS_H__

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <glib.h>
#include "panel-toplevel.h"

#ifdef __cplusplus
extern "C" {
#endif

gboolean panel_struts_register_strut           (PanelToplevel    *toplevel,
						GdkScreen        *screen,
						int               monitor,
						PanelOrientation  orientation,
						int               strut_size,
						int               strut_start,
						int               strut_end,
						gint              scale);

void     panel_struts_unregister_strut         (PanelToplevel    *toplevel);

void     panel_struts_set_window_hint          (PanelToplevel    *toplevel);
void     panel_struts_unset_window_hint        (PanelToplevel    *toplevel);

gboolean panel_struts_update_toplevel_geometry (PanelToplevel    *toplevel,
						int              *x,
						int              *y,
						int              *w,
						int              *h);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_STRUTS_H__ */
