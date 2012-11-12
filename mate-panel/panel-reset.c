/*
 * panel-reset.c
 *
 * Copyright (C) 2010 Perberos <perberos@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *    Perberos <perberos@gmail.com>
 *    Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __PANEL_RESET_C__
#define __PANEL_RESET_C__

#include <stdlib.h>
#include <glib.h>
#include "panel-reset.h"
#include "panel-schemas.h"
#include <libpanel-util/panel-dconf.h>

void
panel_reset()
{
	panel_dconf_recursive_reset (PANEL_GENERAL_PATH, NULL);
	panel_dconf_recursive_reset (PANEL_TOPLEVEL_PATH, NULL);
	panel_dconf_recursive_reset (PANEL_OBJECT_PATH, NULL);

	/* TODO: send a dbus message to mate-panel, if active, to reload the panel
	 * configuration */
}

#endif /* !__PANEL_RESET_C__ */
