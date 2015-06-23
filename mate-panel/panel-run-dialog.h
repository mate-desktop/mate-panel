/*
 * panel-run-dialog.h:
 *
 * Copyright (C) 2003 Frank Worsley <fworsley@shaw.ca>
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
 *	Frank Worsley <fworsley@shaw.ca>
 */

#ifndef __PANEL_RUN_DIALOG_H__
#define __PANEL_RUN_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

void panel_run_dialog_present           (GdkScreen  *screen,
					 guint32    activate_time);

void panel_run_dialog_quit_on_destroy (void);

G_END_DECLS

#endif /* __PANEL_RUN_DIALOG_H__ */
