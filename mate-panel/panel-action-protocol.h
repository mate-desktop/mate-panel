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

#ifndef __PANEL_ACTION_PROTOCOL_H__
#define __PANEL_ACTION_PROTOCOL_H__

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <glib.h>

G_BEGIN_DECLS

void panel_action_protocol_init (void);

G_END_DECLS

#endif /* __PANEL_ACTION_PROTOCOL_H__ */
