/*
 * panel-a11y.h: panel accessibility support module
 *
 * Copyright (C) 2002, 2003 Sun Microsystems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __PANEL_A11Y_H__
#define __PANEL_A11Y_H__

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean panel_a11y_get_is_a11y_enabled          (GtkWidget  *widget);
void     panel_a11y_set_atk_name_desc            (GtkWidget  *widget,
						  const char *name,
						  const char *desc);
void     panel_a11y_set_atk_relation             (GtkWidget  *widget,
						  GtkLabel   *label);
GType    panel_a11y_query_accessible_parent_type (GType       type,
						  GTypeInfo  *type_info);

#ifdef __cplusplus
}
#endif

#endif /* __PANEL_A11Y_H__ */
