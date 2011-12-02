/*
 * mate-panel-applet-mateconf.h: panel applet preferences handling.
 *
 * Copyright (C) 2001-2003 Sun Microsystems, Inc.
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
 *     Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __MATE_PANEL_APPLET_MATECONF_H__
#define __MATE_PANEL_APPLET_MATECONF_H__

#include <glib.h>
#include <mateconf/mateconf-value.h>

#include <mate-panel-applet.h>

#ifdef __cplusplus
extern "C" {
#endif

gchar       *mate_panel_applet_mateconf_get_full_key (MatePanelApplet     *applet,
					      const gchar     *key);

void         mate_panel_applet_mateconf_set_bool     (MatePanelApplet     *applet,
					      const gchar     *key,
					      gboolean         the_bool,
					      GError         **opt_error);
void         mate_panel_applet_mateconf_set_int      (MatePanelApplet     *applet,
					      const gchar     *key,
					      gint             the_int,
					      GError         **opt_error);
void         mate_panel_applet_mateconf_set_string   (MatePanelApplet     *applet,
					      const gchar     *key,
					      const gchar     *the_string,
					      GError         **opt_error);
void         mate_panel_applet_mateconf_set_float    (MatePanelApplet     *applet,
					      const gchar     *key,
					      gdouble          the_float,
					      GError         **opt_error);
void         mate_panel_applet_mateconf_set_list     (MatePanelApplet     *applet,
					      const gchar     *key,
					      MateConfValueType   list_type,
					      GSList          *list,
					      GError         **opt_error);
void         mate_panel_applet_mateconf_set_value    (MatePanelApplet     *applet,
					      const gchar     *key,
					      MateConfValue      *value,
					      GError         **opt_error);

gboolean     mate_panel_applet_mateconf_get_bool     (MatePanelApplet     *applet,
					      const gchar     *key,
					      GError         **opt_error);
gint         mate_panel_applet_mateconf_get_int      (MatePanelApplet     *applet,
					      const gchar     *key,
					      GError         **opt_error);
gchar       *mate_panel_applet_mateconf_get_string   (MatePanelApplet     *applet,
					      const gchar     *key,
					      GError         **opt_error);
gdouble      mate_panel_applet_mateconf_get_float    (MatePanelApplet     *applet,
					      const gchar     *key,
					      GError         **opt_error);
GSList      *mate_panel_applet_mateconf_get_list     (MatePanelApplet     *applet,
					      const gchar     *key,
					      MateConfValueType   list_type,
					      GError         **opt_error);
MateConfValue  *mate_panel_applet_mateconf_get_value    (MatePanelApplet     *applet,
					      const gchar     *key,
					      GError         **opt_error);

#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLET_MATECONF_H__ */
