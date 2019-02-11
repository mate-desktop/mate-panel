/*
 * Copyright (C) 2016 Alberts Muktupāvels
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// File should only be used on X11

#ifndef PANEL_PLUG_PRIVATE_H
#define PANEL_PLUG_PRIVATE_H

#ifdef PACKAGE_NAME // only check HAVE_X11 if config.h has been included
#ifndef HAVE_X11
#error file should only be included when HAVE_X11 is enabled
#endif
#endif

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

G_BEGIN_DECLS

/* G_DECLARE_FINAL_TYPE is available only since GLib 2.44,
 * but GTK+ 3.18 already requires 2.45.8, so we're safe.
 */

#define PANEL_TYPE_PLUG panel_plug_get_type ()
G_DECLARE_FINAL_TYPE (PanelPlug, panel_plug, PANEL, PLUG, GtkPlug)

GtkWidget *panel_plug_new (void);

G_END_DECLS

#endif
