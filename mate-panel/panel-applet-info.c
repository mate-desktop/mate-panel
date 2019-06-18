/*
 * panel-applet-info.c
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 * Copyright (C) 2010 Vincent Untz <vuntz@gnome.org>
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
 */

#include <config.h>

#include "panel-applet-info.h"

struct _MatePanelAppletInfo {
	gchar  *iid;

	gchar  *name;
	gchar  *comment;
	gchar  *icon;

	gchar **old_ids;
	gboolean x11_supported;
	gboolean wayland_supported;
};

MatePanelAppletInfo *
mate_panel_applet_info_new (const gchar  *iid,
			    const gchar  *name,
			    const gchar  *comment,
			    const gchar  *icon,
			    const gchar **old_ids,
			    gboolean      x11_supported,
			    gboolean      wayland_supported)
{
	MatePanelAppletInfo *info;

	info = g_slice_new0 (MatePanelAppletInfo);

	info->iid = g_strdup (iid);
	info->name = g_strdup (name);
	info->comment = g_strdup (comment);
	info->icon = g_strdup (icon);

	/* MateComponent compatibility */
	if (old_ids != NULL) {
		int i, len;

		len = g_strv_length ((gchar **) old_ids);
		if (len > 0) {
			info->old_ids = g_new0 (gchar *, len + 1);
			for (i = 0; i < len; i++)
				info->old_ids[i] = g_strdup (old_ids[i]);
		}
	}

	info->x11_supported = x11_supported;
	info->wayland_supported = wayland_supported;

	if (!x11_supported && !wayland_supported)
		g_warning ("Applet %s has no supported platforms", name);

	return info;
}

void
mate_panel_applet_info_free (MatePanelAppletInfo *info)
{
	if (!info)
		return;

	g_free (info->iid);
	g_free (info->name);
	g_free (info->comment);
	g_free (info->icon);
	g_strfreev (info->old_ids);

	g_slice_free (MatePanelAppletInfo, info);
}

const gchar *
mate_panel_applet_info_get_iid (MatePanelAppletInfo *info)
{
	return info->iid;
}

const gchar *
mate_panel_applet_info_get_name (MatePanelAppletInfo *info)
{
	return info->name;
}

const gchar *
mate_panel_applet_info_get_description (MatePanelAppletInfo *info)
{
	return info->comment;
}

const gchar *
mate_panel_applet_info_get_icon (MatePanelAppletInfo *info)
{
	return info->icon;
}

const gchar * const *
mate_panel_applet_info_get_old_ids (MatePanelAppletInfo *info)
{
	return (const gchar * const *) info->old_ids;
}

gboolean
mate_panel_applet_info_get_x11_supported (MatePanelAppletInfo *info)
{
	return info->x11_supported;
}

gboolean
mate_panel_applet_info_get_wayland_supported (MatePanelAppletInfo *info)
{
	return info->wayland_supported;
}
