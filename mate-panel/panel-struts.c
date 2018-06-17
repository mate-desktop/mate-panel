/*
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2003,2004 Rob Adams
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

#include <config.h>
#include <gdk/gdkx.h>

#include "panel-struts.h"

#include "panel-multiscreen.h"
#include "panel-xutils.h"


typedef struct {
        PanelToplevel    *toplevel;

	GdkScreen        *screen;
	int               monitor;

        PanelOrientation  orientation;
	GdkRectangle      geometry;
        int               strut_size;
        int               strut_start;
        int               strut_end;

	GdkRectangle      allocated_geometry;
        int               allocated_strut_size;
        int               allocated_strut_start;
        int               allocated_strut_end;
} PanelStrut;


static GSList *panel_struts_list = NULL;


static inline PanelStrut *
panel_struts_find_strut (PanelToplevel *toplevel)
{
	GSList *l;

	for (l = panel_struts_list; l; l = l->next) {
		PanelStrut *strut = l->data;

		if (strut->toplevel == toplevel)
			break;
	}

	return l ? l->data : NULL;
}

static void
panel_struts_get_monitor_geometry (GdkScreen *screen,
				   int        monitor,
				   int       *x,
				   int       *y,
				   int       *width,
				   int       *height)
{
        *x      = panel_multiscreen_x      (screen, monitor);
        *y      = panel_multiscreen_y      (screen, monitor);
        *width  = panel_multiscreen_width  (screen, monitor);
        *height = panel_multiscreen_height (screen, monitor);
}

static PanelStrut *
panel_struts_intersect (GSList       *struts,
			GdkRectangle *geometry,
			int           skip)
{
	GSList *l;
	int     i;

	i = 0;
	for (l = struts; l; l = l->next) {
		PanelStrut *strut = l->data;
		int         x1, y1, x2, y2;

		x1 = MAX (strut->allocated_geometry.x, geometry->x);
		y1 = MAX (strut->allocated_geometry.y, geometry->y);

		x2 = MIN (strut->allocated_geometry.x + strut->allocated_geometry.width,
			  geometry->x + geometry->width);
		y2 = MIN (strut->allocated_geometry.y + strut->allocated_geometry.height,
			  geometry->y + geometry->height);

		if (x2 - x1 > 0 && y2 - y1 > 0 && ++i > skip)
			break;
	}

	return l ? l->data : NULL;
}

static int
panel_struts_allocation_overlapped (PanelStrut   *strut,
				    PanelStrut   *overlap,
				    GdkRectangle *geometry,
				    gboolean     *moved_down,
				    int           skip)
{
	int overlap_x1, overlap_y1, overlap_x2, overlap_y2;

	overlap_x1 = overlap->allocated_geometry.x;
	overlap_y1 = overlap->allocated_geometry.y;
	overlap_x2 = overlap->allocated_geometry.x + overlap->allocated_geometry.width;
	overlap_y2 = overlap->allocated_geometry.y + overlap->allocated_geometry.height;

	if (strut->orientation == overlap->orientation) {
		int old_x, old_y;

		old_x = geometry->x;
		old_y = geometry->y;

		switch (strut->orientation) {
		case PANEL_ORIENTATION_TOP:
			geometry->y = overlap_y2;
			strut->allocated_strut_size += geometry->y - old_y;
			break;
		case PANEL_ORIENTATION_BOTTOM:
			geometry->y = overlap_y1 - geometry->height;
			strut->allocated_strut_size += old_y - geometry->y;
			break;
		case PANEL_ORIENTATION_LEFT:
			geometry->x = overlap_x2;
			strut->allocated_strut_size += geometry->x - old_x;
			break;
		case PANEL_ORIENTATION_RIGHT:
			geometry->x = overlap_x1 - geometry->width;
			strut->allocated_strut_size += old_x - geometry->x;
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	} else {
		if (strut->orientation & PANEL_HORIZONTAL_MASK ||
		    overlap->orientation & PANEL_VERTICAL_MASK)
			return ++skip;

		switch (overlap->orientation) {
		case PANEL_ORIENTATION_TOP:
			geometry->y = overlap_y2;
			*moved_down = TRUE;
			break;
		case PANEL_ORIENTATION_BOTTOM:
			if (!*moved_down)
				geometry->y = overlap_y1 - geometry->height;
			else if (overlap_y1 > geometry->y)
				geometry->height = overlap_y1 - geometry->y;
			else
				return ++skip;
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		strut->allocated_strut_start = geometry->y;
		strut->allocated_strut_end   = geometry->y + geometry->height - 1;
	}

	return skip;
}

static gboolean
panel_struts_allocate_struts (PanelToplevel *toplevel,
			      GdkScreen     *screen,
			      int            monitor)
{
	GSList   *allocated = NULL;
	GSList   *l;
	gboolean  toplevel_changed = FALSE;

	for (l = panel_struts_list; l; l = l->next) {
		PanelStrut   *strut = l->data;
		PanelStrut   *overlap;
		GdkRectangle  geometry;
		int           monitor_x, monitor_y;
		int           monitor_width, monitor_height;
		gboolean      moved_down;
		int           skip;

		if (strut->screen != screen || strut->monitor != monitor)
			continue;

		panel_struts_get_monitor_geometry (strut->screen, strut->monitor,
						   &monitor_x, &monitor_y,
						   &monitor_width, &monitor_height);

		strut->allocated_strut_size  = strut->strut_size;
		strut->allocated_strut_start = strut->strut_start;
		strut->allocated_strut_end   = strut->strut_end;

		geometry = strut->geometry;

		moved_down = FALSE;
		skip = 0;
		while ((overlap = panel_struts_intersect (allocated, &geometry, skip)))
			skip = panel_struts_allocation_overlapped (
				strut, overlap, &geometry, &moved_down, skip);

		if (strut->orientation & PANEL_VERTICAL_MASK) {
			if (geometry.y < monitor_y) {
				geometry.height = geometry.y + geometry.height - monitor_y;
				geometry.y      = monitor_y;
			}

			if (geometry.y + geometry.height > monitor_y + monitor_height)
				geometry.height = monitor_y + monitor_height - geometry.y;
		}

		if (strut->allocated_geometry.x      != geometry.x     ||
		    strut->allocated_geometry.y      != geometry.y     ||
		    strut->allocated_geometry.width  != geometry.width ||
		    strut->allocated_geometry.height != geometry.height) {
			strut->allocated_geometry = geometry;

			if (strut->toplevel == toplevel)
				toplevel_changed = TRUE;
			else
				gtk_widget_queue_resize (GTK_WIDGET (strut->toplevel));
		}

		allocated = g_slist_append (allocated, strut);
	}

	g_slist_free (allocated);

	return toplevel_changed;
}

void
panel_struts_set_window_hint (PanelToplevel *toplevel)
{
	GtkWidget       *widget;
	PanelStrut      *strut;
	int              strut_size;
	int              monitor_x, monitor_y, monitor_width, monitor_height;
	int              screen_width, screen_height;
	int              leftmost, rightmost, topmost, bottommost;
	int              scale;
	PanelOrientation strut_orientation;
	int              strut_start, strut_end;
	const char      *wm_name;
	gboolean         wm_compatible;

	widget = GTK_WIDGET (toplevel);

	if (!gtk_widget_get_realized (widget))
		return;

	if (!(strut = panel_struts_find_strut (toplevel))) {
		panel_struts_unset_window_hint (toplevel);
		return;
	}

	scale = gtk_widget_get_scale_factor (widget);
	strut_size = strut->allocated_strut_size;

	screen_width  = WidthOfScreen (gdk_x11_screen_get_xscreen (strut->screen)) / scale;
	screen_height = HeightOfScreen (gdk_x11_screen_get_xscreen (strut->screen)) / scale;

	panel_struts_get_monitor_geometry (strut->screen,
					   strut->monitor,
					   &monitor_x,
					   &monitor_y,
					   &monitor_width,
					   &monitor_height);

        panel_multiscreen_is_at_visible_extreme (strut->screen,
                                                 strut->monitor,
                                                 &leftmost,
                                                 &rightmost,
                                                 &topmost,
                                                 &bottommost);

	/* HACK: Here we purposefully break freedesktop wm-spec standard for _NET_WM_STRUT_PARTIAL
	 * for multi-monitor setups. Since _NET_WM_STRUT_PARTIAL only allow struts along the edges
	 * of the screen (rather than each monitor), panels positioned on the inside edge of a
	 * monitor (e.g. top panel on the bottom-most monitor, left panel on the right-most
	 * monitor, etc), cannot have proper struts. We "fix" this by setting a partial strut that
	 * is orthogonal to the "correct" one, and pivot the dimensions for it.
	 *
	 * For example: a top panel defaults to having a strut that is 24px high, a start
	 * coordinate of 0px, and an end coordinate of 1920px. If this panel is on the bottom-most
	 * monitor, and thus no longer at the edge of the screen, we instead "fix" it here by
	 * applying struts as though it was a *left* panel, that is 1920px wide, with a start
	 * coordinate of 1080px (top monitor height) and an end coordinate of (1080px + 24px).
	 *
	 * This hack only works properly for expanded panels, or panels that start at either edge
	 * of the monitor, since otherwise they require floating struts, which are not really a
	 * thing. This last part we just ignore and let the strut exist snapped to the far edge of
	 * the monitor, which only mildly affects edge snapping of non-maximized windows against
	 * the floating panel.
	 *
	 * This hack will be removed once _NET_WM_STRUT_AREA is implemented.
	 */
	strut_start = strut->allocated_strut_start;
	strut_end = strut->allocated_strut_end;
	strut_orientation = strut->orientation;

	/* Only "Metacity" and "Metacity (Marco)" are compatible with this workaround, so we set a
	 * flag to make sure that other window managers get the standard behavior.
	 */
	wm_name = gdk_x11_screen_get_window_manager_name (strut->screen);
	wm_compatible = g_str_has_prefix (wm_name, "Metacity");

	switch (strut->orientation) {
	case PANEL_ORIENTATION_TOP:
		if (!topmost) {
			if (wm_compatible) {
				strut_orientation = strut_start ? PANEL_ORIENTATION_RIGHT : PANEL_ORIENTATION_LEFT;
				strut_start = monitor_y;
				strut_end = (strut_size / scale) + strut_start - 1;
				strut_size = (1 + strut->allocated_strut_end - strut->allocated_strut_start) * scale;
			} else {
				strut_size = 0;
			}
		} else if (monitor_y > 0) {
			strut_size += monitor_y;
		}
		break;
	case PANEL_ORIENTATION_BOTTOM:
		if (!bottommost) {
			if (wm_compatible) {
				strut_orientation = strut_start ? PANEL_ORIENTATION_RIGHT : PANEL_ORIENTATION_LEFT;
				strut_end = monitor_height - 1;
				strut_start = strut_end - (strut_size / scale) + 1;
				strut_size = (1 + strut->allocated_strut_end - strut->allocated_strut_start) * scale;
			} else {
				strut_size = 0;
			}
		} else if (monitor_y + monitor_height < screen_height) {
			strut_size += screen_height - (monitor_y + monitor_height);
		}
		break;
	case PANEL_ORIENTATION_LEFT:
		if (!leftmost) {
			if (wm_compatible) {
				strut_orientation = strut_start ? PANEL_ORIENTATION_BOTTOM : PANEL_ORIENTATION_TOP;
				strut_start = monitor_x;
				strut_end = (strut_size / scale) + strut_start - 1;
				strut_size = (1 + strut->allocated_strut_end - strut->allocated_strut_start) * scale;
			} else {
				strut_size = 0;
			}
		} else if (monitor_x > 0) {
			strut_size += monitor_x;
		}
		break;
	case PANEL_ORIENTATION_RIGHT:
		if (!rightmost) {
			if (wm_compatible) {
				strut_orientation = strut_start ? PANEL_ORIENTATION_BOTTOM : PANEL_ORIENTATION_TOP;
				strut_end = monitor_width - 1;
				strut_start = strut_end - (strut_size / scale) + 1;
				strut_size = (1 + strut->allocated_strut_end - strut->allocated_strut_start) * scale;
			} else {
				strut_size = 0;
			}
		} else if (monitor_x + monitor_width < screen_width) {
			strut_size += screen_width - (monitor_x + monitor_width);
		}
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	panel_xutils_set_strut (gtk_widget_get_window (widget),
				strut_orientation,
				strut_size,
				strut_start * scale,
				strut_end * scale);
}

void
panel_struts_unset_window_hint (PanelToplevel *toplevel)
{
	if (!gtk_widget_get_realized (GTK_WIDGET (toplevel)))
		return;

	panel_xutils_set_strut (gtk_widget_get_window (GTK_WIDGET (toplevel)), 0, 0, 0, 0);
}

static inline int
orientation_to_order (PanelOrientation orientation)
{
        switch (orientation) {
        case PANEL_ORIENTATION_TOP:
                return 1;
        case PANEL_ORIENTATION_BOTTOM:
                return 2;
        case PANEL_ORIENTATION_LEFT:
                return 3;
        case PANEL_ORIENTATION_RIGHT:
                return 4;
        default:
                g_assert_not_reached ();
                return -1;
        }
}

static inline int
get_toplevel_depth (PanelToplevel *toplevel)
{
	int depth = 0;

	while ((toplevel = panel_toplevel_get_attach_toplevel (toplevel)))
		depth++;

	return depth;
}

/* Sort in order of
 *   1) screen
 *   2) monitor
 *   3) depth (for drawers)
 *   4) top, bottom, left, right
 *   5) strut_start ascending
 *   6) strut_end descending
 */
static int
panel_struts_compare (const PanelStrut *s1,
		      const PanelStrut *s2)
{
	int s1_depth;
	int s2_depth;

	if (s1->screen != s2->screen)
		return gdk_x11_screen_get_screen_number (s1->screen) -
			gdk_x11_screen_get_screen_number (s2->screen);

	if (s1->monitor != s2->monitor)
		return s1->monitor - s2->monitor;

	s1_depth = get_toplevel_depth (s1->toplevel);
	s2_depth = get_toplevel_depth (s2->toplevel);
	if (s1_depth != s2_depth)
		return s2_depth - s1_depth;

        if (s1->orientation != s2->orientation)
                return orientation_to_order (s1->orientation) -
			orientation_to_order (s2->orientation);

        if (s1->strut_start != s2->strut_start)
                return s1->strut_start - s2->strut_start;

        if (s1->strut_end != s2->strut_end)
                return s2->strut_end - s1->strut_end;

        return 0;
}

gboolean
panel_struts_register_strut (PanelToplevel    *toplevel,
			     GdkScreen        *screen,
			     int               monitor,
			     PanelOrientation  orientation,
			     int               strut_size,
			     int               strut_start,
			     int               strut_end,
			     gint              scale)
{
	PanelStrut *strut;
	gboolean    new_strut = FALSE;
	int         monitor_x, monitor_y, monitor_width, monitor_height;

	if (!(strut = panel_struts_find_strut (toplevel))) {
		strut = g_new0 (PanelStrut, 1);
		new_strut = TRUE;

	} else if (strut->toplevel    == toplevel    &&
		   strut->orientation == orientation &&
		   strut->screen      == screen      &&
		   strut->monitor     == monitor     &&
		   strut->strut_size  == strut_size  &&
		   strut->strut_start == strut_start &&
		   strut->strut_end   == strut_end)
		return FALSE;

	strut->toplevel    = toplevel;
	strut->orientation = orientation;
	strut->screen      = screen;
	strut->monitor     = monitor;
	strut->strut_size  = strut_size;
	strut->strut_start = strut_start;
	strut->strut_end   = strut_end;

	panel_struts_get_monitor_geometry (screen, monitor,
					   &monitor_x, &monitor_y,
					   &monitor_width, &monitor_height);

	switch (strut->orientation) {
	case PANEL_ORIENTATION_TOP:
		strut->geometry.x      = strut->strut_start;
		strut->geometry.y      = monitor_y;
		strut->geometry.width  = strut->strut_end - strut->strut_start + 1;
		strut->geometry.height = strut->strut_size / scale;
		if (scale > 1)
			strut->geometry.width -= (strut->strut_size / scale);
		break;
	case PANEL_ORIENTATION_BOTTOM:
		strut->geometry.x      = strut->strut_start;
		strut->geometry.y      = monitor_y + monitor_height - strut->strut_size;
		strut->geometry.width  = strut->strut_end - strut->strut_start + 1;
		strut->geometry.height = strut->strut_size / scale;
		if (scale > 1)
			strut->geometry.width -= (strut->strut_size / scale);
		break;
	case PANEL_ORIENTATION_LEFT:
		strut->geometry.x      = monitor_x;
		strut->geometry.y      = strut->strut_start;
		strut->geometry.width  = strut->strut_size / scale;
		strut->geometry.height = strut->strut_end - strut->strut_start + 1;
		if (scale > 1)
			strut->geometry.height -= (strut->strut_size / scale);
		break;
	case PANEL_ORIENTATION_RIGHT:
		strut->geometry.x      = monitor_x + monitor_width - strut->strut_size;
		strut->geometry.y      = strut->strut_start;
		strut->geometry.width  = strut->strut_size / scale;
		strut->geometry.height = strut->strut_end - strut->strut_start + 1;
		if (scale > 1)
			strut->geometry.height -= (strut->strut_size / scale);
		break;
	}

	if (new_strut)
		panel_struts_list = g_slist_append (panel_struts_list, strut);

	panel_struts_list = g_slist_sort (panel_struts_list,
					  (GCompareFunc) panel_struts_compare);

	return panel_struts_allocate_struts (toplevel, screen, monitor);
}

void
panel_struts_unregister_strut (PanelToplevel *toplevel)
{
	PanelStrut *strut;
	GdkScreen  *screen;
	int         monitor;

	if (!(strut = panel_struts_find_strut (toplevel)))
		return;

	screen  = strut->screen;
	monitor = strut->monitor;

	panel_struts_list = g_slist_remove (panel_struts_list, strut);
	g_free (strut);

	panel_struts_allocate_struts (toplevel, screen, monitor);
}

gboolean
panel_struts_update_toplevel_geometry (PanelToplevel *toplevel,
				       int           *x,
				       int           *y,
				       int           *width,
				       int           *height)
{
	PanelStrut *strut;

	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);

	if (!(strut = panel_struts_find_strut (toplevel)))
		return FALSE;

	*x += strut->allocated_geometry.x - strut->geometry.x;
	*y += strut->allocated_geometry.y - strut->geometry.y;

	if (width != NULL && *width != -1)
		*width  += strut->allocated_geometry.width  - strut->geometry.width;
	if (height != NULL && *height != -1)
		*height += strut->allocated_geometry.height - strut->geometry.height;

	return TRUE;
}
