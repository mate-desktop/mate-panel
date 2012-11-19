/*
 * panel-shell.h: panel shell interface implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 Novell, Inc.
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
 *      Jacob Berkman <jacob@ximian.com>
 *      Colin Walters <walters@verbum.org>
 *      Vincent Untz <vuntz@gnome.org>
 */

#ifndef __PANEL_SHELL_H__
#define __PANEL_SHELL_H__

#include <glib.h>

gboolean panel_shell_register (gboolean replace);
void     panel_shell_quit     (void);

#endif /* __PANEL_SHELL_H__ */
