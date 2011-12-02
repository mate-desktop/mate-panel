/*
 * Copyright (C) 2010 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Vincent Untz <vuntz@gnome.org>
 */

#include "config.h"

#include <gio/gio.h>

#include <mate-panel-applets-manager.h>
#include "mate-panel-applets-manager-matecomponent.h"

void
g_io_module_load (GIOModule *module)
{
	mate_panel_applets_manager_matecomponent_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}

char **
g_io_module_query (void)
{
  char *eps[] = {
    MATE_PANEL_APPLETS_MANAGER_EXTENSION_POINT_NAME,
    NULL
  };
  return g_strdupv (eps);
}
