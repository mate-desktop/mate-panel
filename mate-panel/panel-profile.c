/*
 * panel-profile.c:
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

#include <config.h>
#include <math.h>

#include "panel-profile.h"
#include "panel-layout.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#include <libpanel-util/panel-list.h>
#include <libmate-desktop/mate-dconf.h>
#include <libmate-desktop/mate-gsettings.h>

#include "applet.h"
#include "panel.h"
#include "panel-widget.h"
#include "panel-util.h"
#include "panel-multimonitor.h"
#include "panel-toplevel.h"
#include "panel-lockdown.h"
#include "panel-schemas.h"

typedef struct {
	GdkScreen       *screen;
	int              monitor;
	int              size;
	int              x;
	int              x_right;
	gboolean         x_centered;
	int              y;
	int              y_bottom;
	gboolean         y_centered;
	PanelOrientation orientation;

	guint screen_changed : 1;
	guint monitor_changed : 1;
	guint size_changed : 1;
	guint x_changed : 1;
	guint x_right_changed : 1;
	guint x_centered_changed : 1;
	guint y_changed : 1;
	guint y_bottom_changed : 1;
	guint y_centered_changed : 1;
	guint orientation_changed : 1;
} ToplevelLocationChange;

typedef const char *(*PanelProfileGetIdFunc)   (gpointer           object);
typedef gboolean    (*PanelProfileOnLoadQueue) (const char        *id);
typedef void        (*PanelProfileLoadFunc)    (const char        *id);
typedef void        (*PanelProfileDestroyFunc) (const char        *id);

static GSettings *profile_settings = NULL;

static GQuark toplevel_id_quark = 0;
#if 0
static GQuark queued_changes_quark = 0;
#endif
static GQuark commit_timeout_quark = 0;

static void panel_profile_object_id_list_update (gchar **objects);
static void panel_profile_ensure_toplevel_per_screen (void);

static void
panel_profile_set_toplevel_id (PanelToplevel *toplevel,
			       const char    *id)
{
	if (!toplevel_id_quark)
		toplevel_id_quark = g_quark_from_static_string ("panel-toplevel-id");

	g_object_set_qdata_full (G_OBJECT (toplevel),
				 toplevel_id_quark,
				 g_strdup (id),
				 g_free);
}

const char *
panel_profile_get_toplevel_id (PanelToplevel *toplevel)
{
	if (!toplevel_id_quark)
		return NULL;

	return g_object_get_qdata (G_OBJECT (toplevel), toplevel_id_quark);
}

PanelToplevel *
panel_profile_get_toplevel_by_id (const char *toplevel_id)
{
	GSList *toplevels, *l;

	if (!toplevel_id || !toplevel_id [0])
		return NULL;

	toplevels = panel_toplevel_list_toplevels ();
	for (l = toplevels; l; l = l->next)
		if (!strcmp (panel_profile_get_toplevel_id (l->data), toplevel_id))
			return l->data;

	return NULL;
}

char *
panel_profile_find_new_id (PanelGSettingsKeyType type)
{
	gchar      **existing_ids;
	char        *retval = NULL;
	char        *prefix = NULL;
	char        *dir = NULL;
	int          i;
	int          j;

	switch (type) {
		case PANEL_GSETTINGS_TOPLEVELS:
			prefix = PANEL_TOPLEVEL_DEFAULT_PREFIX;
			dir = PANEL_TOPLEVEL_PATH;
			break;
		case PANEL_GSETTINGS_OBJECTS:
			prefix = PANEL_OBJECT_DEFAULT_PREFIX;
			dir = PANEL_OBJECT_PATH;
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	existing_ids = mate_dconf_list_subdirs (dir, TRUE);

	for (i = 0; !retval; i++) {
		retval = g_strdup_printf ("%s-%d", prefix, i);

		for (j = 0; existing_ids[j] != NULL; j++) {
			if (g_strcmp0 (existing_ids[j], retval) == 0) {
				g_free (retval);
				retval = NULL;
				break;
			}
		}
	}
	if (existing_ids)
		g_strfreev (existing_ids);

	g_assert (retval != NULL);

	return retval;
}

static void
panel_profile_remove_commit_timeout (guint timeout)
{
	g_source_remove (timeout);
}

static void
panel_profile_set_commit_timeout (PanelToplevel *toplevel,
				  guint          timeout)
{
	GDestroyNotify destroy_notify;

	if (!commit_timeout_quark)
		commit_timeout_quark = g_quark_from_static_string ("panel-queued-timeout");

	if (timeout)
		destroy_notify = (GDestroyNotify) panel_profile_remove_commit_timeout;
	else
		destroy_notify = NULL;

	g_object_set_qdata_full (G_OBJECT (toplevel),
				 commit_timeout_quark,
				 GUINT_TO_POINTER (timeout),
				 destroy_notify);
}

static guint
panel_profile_get_commit_timeout (GObject *object)
{
	if (!commit_timeout_quark)
		return 0;

	return GPOINTER_TO_UINT (g_object_get_qdata (object, commit_timeout_quark));
}

gboolean
panel_profile_key_is_writable (PanelToplevel *toplevel, gchar *key) {
	return g_settings_is_writable (toplevel->settings, key);
}

gboolean
panel_profile_background_key_is_writable (PanelToplevel *toplevel, gchar *key) {
	return g_settings_is_writable (toplevel->background_settings, key);
}

void
panel_profile_set_background_type (PanelToplevel       *toplevel,
				   PanelBackgroundType  background_type)
{
	g_settings_set_enum (toplevel->background_settings,
						 "type",
						 background_type);
}

PanelBackgroundType
panel_profile_get_background_type (PanelToplevel *toplevel)
{
	PanelBackgroundType background_type;
	background_type = g_settings_get_enum (toplevel->background_settings,
						 "type");
	return background_type;
}

void
panel_profile_set_background_color (PanelToplevel *toplevel,
				    GdkRGBA       *color)
{
	panel_profile_set_background_gdk_rgba (toplevel, color);
}

void
panel_profile_get_background_color (PanelToplevel *toplevel,
				    GdkRGBA       *color)
{
	panel_profile_get_background_gdk_rgba (toplevel, color);
}

void
panel_profile_set_background_gdk_rgba (PanelToplevel *toplevel,
					GdkRGBA      *color)
{
	char *color_str;

	color_str = gdk_rgba_to_string (color);

	g_settings_set_string (toplevel->background_settings, "color", color_str);

	g_free (color_str);
}

void
panel_profile_get_background_gdk_rgba (PanelToplevel *toplevel,
					GdkRGBA      *color)
{
	char *color_str;

	color_str = g_settings_get_string (toplevel->background_settings, "color");
	if (!color_str || !gdk_rgba_parse (color, color_str)) {
		color->red   = 0.;
		color->green = 0.;
		color->blue  = 0.;
		color->alpha  = 1.;
	}

	g_free (color_str);
}

void
panel_profile_set_background_opacity (PanelToplevel *toplevel,
				      guint16        opacity)
{
	GdkRGBA color;
	panel_profile_get_background_color (toplevel, &color);
	color.alpha = opacity / 65535.0;
	panel_profile_set_background_color (toplevel, &color);
}

guint16
panel_profile_get_background_opacity (PanelToplevel *toplevel)
{
	GdkRGBA color;
	panel_profile_get_background_color (toplevel, &color);
	return (guint16) round (color.alpha * 65535);
}

void
panel_profile_set_background_image (PanelToplevel *toplevel,
				    const char    *image)
{
	if (image && image [0])
		g_settings_set_string (toplevel->background_settings, "image", image);
	else
		g_settings_reset (toplevel->background_settings, "image");
}

char *
panel_profile_get_background_image (PanelToplevel *toplevel)
{
	return g_settings_get_string (toplevel->background_settings, "image");
}

void
panel_profile_set_toplevel_name (PanelToplevel *toplevel,
				 const char    *name)
{
	if (name && name [0])
		g_settings_set_string (toplevel->settings, "name", name);
	else
		g_settings_reset (toplevel->settings, "name");
}

char *
panel_profile_get_toplevel_name (PanelToplevel *toplevel)
{
	return g_settings_get_string (toplevel->settings, "name");
}

void
panel_profile_set_toplevel_orientation (PanelToplevel    *toplevel,
					PanelOrientation  orientation)
{
	g_settings_set_enum (toplevel->settings, "orientation", orientation);
}

PanelOrientation
panel_profile_get_toplevel_orientation (PanelToplevel *toplevel)
{
	PanelOrientation orientation;
	orientation = g_settings_get_enum (toplevel->settings, "orientation");
	return orientation;
}

#define TOPLEVEL_GET_SET_FUNCS(k, p, t, s, a)                     \
	void                                                          \
	panel_profile_set_##p##_##s (PanelToplevel *toplevel, a s)    \
	{                                                             \
		g_settings_set_##t (toplevel->settings, k, s);            \
	}                                                             \
	a                                                             \
	panel_profile_get_##p##_##s (PanelToplevel *toplevel)         \
	{                                                             \
		a retval;                                                 \
		retval = g_settings_get_##t (toplevel->settings, k);      \
		return retval;                                            \
	}

TOPLEVEL_GET_SET_FUNCS ("size",               toplevel,   int,     size,           int)
TOPLEVEL_GET_SET_FUNCS ("expand",             toplevel,   boolean, expand,         gboolean)
TOPLEVEL_GET_SET_FUNCS ("auto-hide",          toplevel,   boolean, auto_hide,      gboolean)
TOPLEVEL_GET_SET_FUNCS ("enable-buttons",     toplevel,   boolean, enable_buttons, gboolean)
TOPLEVEL_GET_SET_FUNCS ("enable-arrows",      toplevel,   boolean, enable_arrows,  gboolean)

#define TOPLEVEL_GET_SET_BG_FUNCS(k, p, t, s, a)                     \
	void                                                          \
	panel_profile_set_##p##_##s (PanelToplevel *toplevel, a s)    \
	{                                                             \
		g_settings_set_##t (toplevel->background_settings, k, s);            \
	}                                                             \
	a                                                             \
	panel_profile_get_##p##_##s (PanelToplevel *toplevel)         \
	{                                                             \
		a retval;                                                 \
		retval = g_settings_get_##t (toplevel->background_settings, k);      \
		return retval;                                            \
	}

TOPLEVEL_GET_SET_BG_FUNCS ("fit",     background, boolean, fit,            gboolean)
TOPLEVEL_GET_SET_BG_FUNCS ("stretch", background, boolean, stretch,        gboolean)
TOPLEVEL_GET_SET_BG_FUNCS ("rotate",  background, boolean, rotate,         gboolean)

GSettings*
panel_profile_get_attached_object_settings (PanelToplevel *toplevel)
{
	GtkWidget  *attach_widget;
	const char *id;
	char *path;
	GSettings *settings;

	attach_widget = panel_toplevel_get_attach_widget (toplevel);

	id = mate_panel_applet_get_id_by_widget (attach_widget);

	if (!id)
		return NULL;

	path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);
	settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);
	g_free (path);

	return settings;
}

void
panel_profile_set_attached_custom_icon (PanelToplevel *toplevel,
					const char    *custom_icon)
{
	GSettings *settings;
	settings = panel_profile_get_attached_object_settings (toplevel);

	g_settings_set_boolean (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY, custom_icon != NULL);
	g_settings_set_string (settings, PANEL_OBJECT_CUSTOM_ICON_KEY, sure_string (custom_icon));

	g_object_unref (settings);
}

char *
panel_profile_get_attached_custom_icon (PanelToplevel *toplevel)
{
	gchar *custom_icon = NULL;
	if (panel_toplevel_get_is_attached (toplevel))
	{
		GSettings *settings;
		settings = panel_profile_get_attached_object_settings (toplevel);

		if (!g_settings_get_boolean (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY))
		{
			g_object_unref (settings);
			return NULL;
		}

		custom_icon = g_settings_get_string (settings, PANEL_OBJECT_CUSTOM_ICON_KEY);
		g_object_unref (settings);
	}
	return custom_icon;
}

gboolean
panel_profile_is_writable_attached_custom_icon (PanelToplevel *toplevel)
{
	gboolean is_writable = FALSE;
	if (panel_toplevel_get_is_attached (toplevel))
	{
		GSettings *settings;
		settings = panel_profile_get_attached_object_settings (toplevel);

		is_writable = g_settings_is_writable (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY) &&
					  g_settings_is_writable (settings, PANEL_OBJECT_CUSTOM_ICON_KEY);

		g_object_unref (settings);
	}
	return is_writable;
}

void
panel_profile_set_attached_tooltip (PanelToplevel *toplevel,
				    const char    *tooltip)
{
	GSettings *settings;
	settings = panel_profile_get_attached_object_settings (toplevel);
	g_settings_set_string (settings, PANEL_OBJECT_TOOLTIP_KEY, tooltip);
	g_object_unref (settings);
}

char *
panel_profile_get_attached_tooltip (PanelToplevel *toplevel)
{
	gchar *tooltip = NULL;
	if (panel_toplevel_get_is_attached (toplevel))
	{
		GSettings *settings;
		settings = panel_profile_get_attached_object_settings (toplevel);
		tooltip = g_settings_get_string (settings, PANEL_OBJECT_TOOLTIP_KEY);
		g_object_unref (settings);
	}
	return tooltip;
}

gboolean
panel_profile_is_writable_attached_tooltip (PanelToplevel *toplevel)
{
	gboolean is_writable = FALSE;
	if (panel_toplevel_get_is_attached (toplevel))
	{
		GSettings *settings;
		settings = panel_profile_get_attached_object_settings (toplevel);
		is_writable = g_settings_is_writable (settings, PANEL_OBJECT_TOOLTIP_KEY);
		g_object_unref (settings);
	}
	return is_writable;
}

static void
get_background_color (PanelToplevel *toplevel,
					  GdkRGBA       *color)
{
	char       *color_str;
	color_str = g_settings_get_string (toplevel->background_settings, "color");
	if (!color_str || !gdk_rgba_parse (color, color_str)) {
		color->red   = 0;
		color->green = 0;
		color->blue  = 0;
		color->alpha  = 1;
	}

	g_free (color_str);
}

static char *
get_background_image (PanelToplevel *toplevel,
		      gboolean     *fit,
		      gboolean     *stretch,
		      gboolean     *rotate)
{
	char *image;
	image = g_settings_get_string (toplevel->background_settings, "image");
	*fit = g_settings_get_boolean (toplevel->background_settings, "fit");
	*stretch = g_settings_get_boolean (toplevel->background_settings, "stretch");
	*rotate = g_settings_get_boolean (toplevel->background_settings, "rotate");
	return image;
}

static void
panel_profile_load_background (PanelToplevel *toplevel)
{
	PanelWidget         *panel_widget;
	PanelBackground     *background;
	PanelBackgroundType  background_type;
	GdkRGBA              color;
	char                *image;
	gboolean             fit;
	gboolean             stretch;
	gboolean             rotate;

	panel_widget = panel_toplevel_get_panel_widget (toplevel);

	background = &panel_widget->toplevel->background;
	background_type = panel_profile_get_background_type (toplevel);

	get_background_color (toplevel, &color);

	image = get_background_image (toplevel, &fit, &stretch, &rotate);

	panel_background_set (background,
			      background_type,
			      &color,
			      image,
			      fit,
			      stretch,
			      rotate);

	g_free (image);
}

static gboolean
panel_profile_commit_toplevel_changes (PanelToplevel *toplevel)
{
	if (g_settings_get_has_unapplied (toplevel->queued_settings))
		g_settings_apply (toplevel->queued_settings);

	panel_profile_set_commit_timeout (toplevel, 0);

	return FALSE;
}

static void
panel_profile_queue_toplevel_location_change (PanelToplevel          *toplevel,
					      ToplevelLocationChange *change)
{
	guint commit_timeout;

	g_settings_delay (toplevel->queued_settings);

#ifdef HAVE_X11
	if (change->screen_changed &&
	    GDK_IS_X11_SCREEN (change->screen)) {
		g_settings_set_int (toplevel->queued_settings,
							"screen",
							gdk_x11_screen_get_screen_number (change->screen));
	}
#endif

	if (change->monitor_changed)
		g_settings_set_int (toplevel->queued_settings,
							"monitor",
							change->monitor);

	if (change->size_changed)
		g_settings_set_int (toplevel->queued_settings,
									 "size",
									 change->size);

	if (change->orientation_changed)
		g_settings_set_enum (toplevel->queued_settings,
										"orientation",
										change->orientation);

	if (change->x_changed)
		g_settings_set_int (toplevel->queued_settings,
							"x",
							change->x);

	if (change->x_right_changed)
		g_settings_set_int (toplevel->queued_settings,
							"x-right",
							change->x_right);

	if (change->x_centered_changed)
		g_settings_set_boolean (toplevel->queued_settings,
								"x-centered",
								change->x_centered);

	if (change->y_changed)
		g_settings_set_int (toplevel->queued_settings,
							"y",
							change->y);

	if (change->y_bottom_changed)
		g_settings_set_int (toplevel->queued_settings,
							"y-bottom",
							change->y_bottom);

	if (change->y_centered_changed)
		g_settings_set_boolean (toplevel->queued_settings,
								"y-centered",
								change->y_centered);

	commit_timeout = panel_profile_get_commit_timeout (G_OBJECT (toplevel));
	if (!commit_timeout) {
		commit_timeout =
			g_timeout_add (500,
				       (GSourceFunc) panel_profile_commit_toplevel_changes,
				       toplevel);
		panel_profile_set_commit_timeout (toplevel, commit_timeout);
	}
}

#define TOPLEVEL_LOCATION_CHANGED_HANDLER(c)                                      \
	static void                                                               \
	panel_profile_toplevel_##c##_changed (PanelToplevel *toplevel)            \
	{                                                                         \
		ToplevelLocationChange change = { NULL };                           \
		change.c##_changed = TRUE;                                        \
		change.c = panel_toplevel_get_##c (toplevel);                     \
		panel_profile_queue_toplevel_location_change (toplevel, &change); \
	}

TOPLEVEL_LOCATION_CHANGED_HANDLER(monitor)
TOPLEVEL_LOCATION_CHANGED_HANDLER(size)
TOPLEVEL_LOCATION_CHANGED_HANDLER(orientation)
TOPLEVEL_LOCATION_CHANGED_HANDLER(x_centered)
TOPLEVEL_LOCATION_CHANGED_HANDLER(y_centered)

#define TOPLEVEL_POSITION_CHANGED_HANDLER(c)                                      \
	static void                                                               \
	panel_profile_toplevel_##c##_changed (PanelToplevel *toplevel)            \
	{                                                                         \
		ToplevelLocationChange change = { NULL };                           \
		int                    x, y, x_right, y_bottom;                   \
		change.c##_changed = TRUE;                                        \
		panel_toplevel_get_position (toplevel,                            \
					     &x, &x_right,                        \
					     &y, &y_bottom);                      \
		change.c = c;                                                     \
		panel_profile_queue_toplevel_location_change (toplevel, &change); \
	}

TOPLEVEL_POSITION_CHANGED_HANDLER(x)
TOPLEVEL_POSITION_CHANGED_HANDLER(x_right)
TOPLEVEL_POSITION_CHANGED_HANDLER(y)
TOPLEVEL_POSITION_CHANGED_HANDLER(y_bottom)

static void
panel_profile_toplevel_screen_changed (PanelToplevel *toplevel)
{
	ToplevelLocationChange change = { NULL };

	change.screen_changed = TRUE;
	change.screen = gtk_window_get_screen (GTK_WINDOW (toplevel));

	panel_profile_queue_toplevel_location_change (toplevel, &change);
}

static void
panel_profile_connect_to_toplevel (PanelToplevel *toplevel)
{
	g_signal_connect (toplevel, "notify::screen",
			  G_CALLBACK (panel_profile_toplevel_screen_changed), NULL);
	g_signal_connect (toplevel, "notify::monitor",
			  G_CALLBACK (panel_profile_toplevel_monitor_changed), NULL);
	g_signal_connect (toplevel, "notify::size",
			  G_CALLBACK (panel_profile_toplevel_size_changed), NULL);
	g_signal_connect (toplevel, "notify::x",
			  G_CALLBACK (panel_profile_toplevel_x_changed), NULL);
	g_signal_connect (toplevel, "notify::x-right",
			  G_CALLBACK (panel_profile_toplevel_x_right_changed), NULL);
	g_signal_connect (toplevel, "notify::x-centered",
			  G_CALLBACK (panel_profile_toplevel_x_centered_changed), NULL);
	g_signal_connect (toplevel, "notify::y",
			  G_CALLBACK (panel_profile_toplevel_y_changed), NULL);
	g_signal_connect (toplevel, "notify::y-bottom",
			  G_CALLBACK (panel_profile_toplevel_y_bottom_changed), NULL);
	g_signal_connect (toplevel, "notify::y-centered",
			  G_CALLBACK (panel_profile_toplevel_y_centered_changed), NULL);
	g_signal_connect (toplevel, "notify::orientation",
			  G_CALLBACK (panel_profile_toplevel_orientation_changed), NULL);
}

static void
panel_profile_toplevel_change_notify (GSettings *settings,
									  gchar *key,
									  PanelToplevel *toplevel)
{
	if (toplevel == NULL || !PANEL_IS_TOPLEVEL (toplevel))
		return;

#define UPDATE_STRING(k, n)                                                     \
		if (!strcmp (key, k)) {                                                 \
			gchar *value = g_settings_get_string (settings, key);               \
			panel_toplevel_set_##n (toplevel, value);                           \
			g_free (value);                                                     \
		}

#define UPDATE_ENUM(k, n)                                                       \
		if (!strcmp (key, k)) {                                                 \
			panel_toplevel_set_##n (toplevel,                                   \
									g_settings_get_enum (settings, key));       \
		}

#define UPDATE_INT(k, n)                                                        \
		if (!strcmp (key, k)) {                                                 \
			panel_toplevel_set_##n (toplevel,                                   \
									g_settings_get_int (settings, key));        \
		}

#define UPDATE_BOOL(k, n)                                                       \
		if (!strcmp (key, k)) {                                                 \
			panel_toplevel_set_##n (toplevel,                                   \
									g_settings_get_boolean (settings, key));    \
		}

#define UPDATE_POS(k, n, n2)                                                    \
		if (!strcmp (key, k)) {                                                 \
			int x, x_right, y, y_bottom;                            \
			panel_toplevel_get_position (toplevel, &x, &x_right,    \
						     &y, &y_bottom);                        \
			panel_toplevel_set_##n (                                \
				toplevel,                                           \
				g_settings_get_int (settings, key),                 \
				n2,                                                 \
				panel_toplevel_get_##n##_centered (toplevel));      \
		}

#define UPDATE_POS2(k, n, n2)                                       \
		if (!strcmp (key, k)) {                                     \
			int x, x_right, y, y_bottom;                            \
			panel_toplevel_get_position (toplevel, &x, &x_right,    \
						     &y, &y_bottom);                        \
			panel_toplevel_set_##n (                                \
				toplevel,                                           \
				n,                                                  \
				g_settings_get_int (settings, key),                 \
				panel_toplevel_get_##n##_centered (toplevel));      \
		}

#define UPDATE_CENTERED(k, n, n2)                                   \
		if (!strcmp (key, k)) {                                     \
			int x, x_right, y, y_bottom;                            \
			panel_toplevel_get_position (toplevel, &x, &x_right,    \
						     &y, &y_bottom);                        \
			panel_toplevel_set_##n (                                \
				toplevel, n, n2,                                    \
				g_settings_get_boolean (settings, key));            \
		}

	if (!strcmp (key, "screen")) {
		GdkScreen *screen;
		screen = gdk_display_get_default_screen (gdk_display_get_default ());

		if (screen)
			gtk_window_set_screen (GTK_WINDOW (toplevel), screen);
		else
			/* Make sure to set the key back to an actual
			 * available screen so it will get loaded on
			 * next startup.
			 */
			panel_profile_toplevel_screen_changed (toplevel);
	}
	else UPDATE_INT ("monitor", monitor)
	else UPDATE_STRING ("name", name)
	else UPDATE_BOOL ("expand", expand)
	else UPDATE_ENUM ("orientation", orientation)
	else UPDATE_INT ("size", size)
	else UPDATE_POS ("x", x, x_right)
	else UPDATE_POS ("y", y, y_bottom)
	else UPDATE_POS2 ("x-right", x, x_right)
	else UPDATE_POS2 ("y-bottom", y, y_bottom)
	else UPDATE_CENTERED ("x-centered", x, x_right)
	else UPDATE_CENTERED ("y-centered", y, y_bottom)
	else UPDATE_BOOL ("auto-hide", auto_hide)
	else UPDATE_BOOL ("enable-animations", animate)
	else UPDATE_BOOL ("enable-buttons", enable_buttons)
	else UPDATE_BOOL ("enable-arrows", enable_arrows)
	else UPDATE_INT ("hide-delay", hide_delay)
	else UPDATE_INT ("unhide-delay", unhide_delay)
	else UPDATE_INT ("auto-hide-size", auto_hide_size)
	else UPDATE_ENUM ("animation-speed", animation_speed)
}

static void
panel_profile_background_change_notify (GSettings *settings,
										gchar *key,
										PanelToplevel *toplevel)
{
	PanelWidget     *panel_widget;
	PanelBackground *background;

	if (toplevel == NULL || !PANEL_IS_TOPLEVEL (toplevel))
		return;

	panel_widget = panel_toplevel_get_panel_widget (toplevel);
	if (panel_widget == NULL)
		return;

	background = &panel_widget->toplevel->background;

	if (!strcmp (key, "type")) {
		PanelBackgroundType  background_type;
		background_type = g_settings_get_enum (settings, key);
		panel_background_set_type (background, background_type);
		panel_toplevel_update_edges (toplevel);
	} else if (!strcmp (key, "color")) {
		GdkRGBA color;
		gchar *str;
		str = g_settings_get_string (settings, key);
		if (gdk_rgba_parse (&color, str))
			panel_background_set_color (background, &color);
		g_free (str);
	} else if (!strcmp (key, "image")) {
		gchar *value = g_settings_get_string (settings, key);
		panel_background_set_image (background, value);
		g_free (value);
	} else if (!strcmp (key, "fit")) {
		panel_background_set_fit (background,
					  g_settings_get_boolean (settings, key));
	} else if (!strcmp (key, "stretch")) {
		panel_background_set_stretch (background,
					      g_settings_get_boolean (settings, key));
	} else if (!strcmp (key, "rotate")) {
		panel_background_set_rotate (background,
					     g_settings_get_boolean (settings, key));
	}
}

static const char *
key_from_type (PanelGSettingsKeyType type) {
	if (type == PANEL_GSETTINGS_TOPLEVELS)
		return PANEL_TOPLEVEL_ID_LIST_KEY;
	else if (type == PANEL_GSETTINGS_OBJECTS)
		return PANEL_OBJECT_ID_LIST_KEY;

	g_assert_not_reached ();
	return NULL;
}

void
panel_profile_add_to_list (PanelGSettingsKeyType  type,
						   const char        *id)
{
	char *new_id = id ? g_strdup (id) : panel_profile_find_new_id (type);

    if (new_id != NULL) {
		mate_gsettings_append_strv (profile_settings,
		                            key_from_type (type),
		                            new_id);
		g_free (new_id);
    }
}

void
panel_profile_remove_from_list (PanelGSettingsKeyType  type,
								const char        *id)
{
	mate_gsettings_remove_all_from_strv (profile_settings,
	                                     key_from_type (type),
	                                     id);
}

static gboolean
panel_profile_id_list_is_writable (PanelGSettingsKeyType type)
{
	return g_settings_is_writable (profile_settings, key_from_type (type));
}

gboolean
panel_profile_id_lists_are_writable (void)
{
  return
    panel_profile_id_list_is_writable (PANEL_GSETTINGS_TOPLEVELS) &&
    panel_profile_id_list_is_writable (PANEL_GSETTINGS_OBJECTS);
}

static gboolean
panel_profile_find_empty_spot (GdkScreen *screen,
			       PanelOrientation *orientation,
			       int *monitor)
{
	GSList *li;
	int i;
	int *filled_spots;
	gboolean found_a_spot = FALSE;

	*monitor = 0;
	*orientation = PANEL_ORIENTATION_TOP;

	filled_spots = g_new0 (int, panel_multimonitor_monitors ());

	for (li = panel_toplevel_list_toplevels (); li != NULL; li = li->next) {
		PanelToplevel *toplevel = li->data;
		GdkScreen *toplevel_screen = gtk_window_get_screen (GTK_WINDOW (toplevel));
		int toplevel_monitor = panel_toplevel_get_monitor (toplevel);

		if (toplevel_screen != screen ||
		    panel_toplevel_get_is_attached (toplevel) ||
		    toplevel_monitor < 0)
			continue;

		filled_spots[toplevel_monitor] |= panel_toplevel_get_orientation (toplevel);
	}

	for (i = 0; i < panel_multimonitor_monitors (); i++) {
		/* These are ordered based on "priority" of the
		   orientation when picking it */
		if ( ! (filled_spots[i] & PANEL_ORIENTATION_TOP)) {
			*orientation = PANEL_ORIENTATION_TOP;
			*monitor = i;
			found_a_spot = TRUE;
			break;
		} else if ( ! (filled_spots[i] & PANEL_ORIENTATION_BOTTOM)) {
			*orientation = PANEL_ORIENTATION_BOTTOM;
			*monitor = i;
			found_a_spot = TRUE;
			break;
		} else if ( ! (filled_spots[i] & PANEL_ORIENTATION_RIGHT)) {
			*orientation = PANEL_ORIENTATION_RIGHT;
			*monitor = i;
			found_a_spot = TRUE;
			break;
		} else if ( ! (filled_spots[i] & PANEL_ORIENTATION_LEFT)) {
			*orientation = PANEL_ORIENTATION_LEFT;
			*monitor = i;
			found_a_spot = TRUE;
			break;
		}
	}

	g_free (filled_spots);

	return found_a_spot;
}

void
panel_profile_create_toplevel (GdkScreen *screen)
{
	char            *id;
	char            *path;
	PanelOrientation orientation;
	int              monitor;
	GSettings       *settings;
	int              screen_number;

	g_return_if_fail (screen != NULL);

	id = panel_profile_find_new_id (PANEL_GSETTINGS_TOPLEVELS);

	path = g_strdup_printf (PANEL_TOPLEVEL_PATH "%s/", id);

	settings = g_settings_new_with_path (PANEL_TOPLEVEL_SCHEMA, path);
	g_free (path);

	screen_number = 0;
#ifdef HAVE_X11
	if (GDK_IS_X11_SCREEN (screen)) {
		screen_number = gdk_x11_screen_get_screen_number (screen);
	}
#endif // HAVE_X11

	g_settings_set_int (settings, PANEL_TOPLEVEL_SCREEN_KEY, screen_number);

	if (panel_profile_find_empty_spot (screen, &orientation, &monitor)) {
		g_settings_set_int (settings, PANEL_TOPLEVEL_MONITOR_KEY, monitor);
		g_settings_set_enum (settings, PANEL_TOPLEVEL_ORIENTATION_KEY, orientation);
	}

	panel_profile_add_to_list (PANEL_GSETTINGS_TOPLEVELS, id);

	g_object_unref (settings);
	g_free (id);
}

static void
panel_profile_delete_toplevel_objects (const char *toplevel_id)
{
	gchar   **list;
	GArray   *newlist;
	int       i;

	list = g_settings_get_strv (profile_settings, PANEL_OBJECT_ID_LIST_KEY);
	newlist = g_array_new (TRUE, TRUE, sizeof (gchar *));

	for (i = 0; list[i]; i++) {
		char *path;
		char *parent_toplevel_id;
		GSettings *settings;

		path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", list[i]);
		settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);
		parent_toplevel_id = g_settings_get_string (settings, PANEL_OBJECT_TOPLEVEL_ID_KEY);
		g_free (path);
		g_object_unref (settings);

		if (parent_toplevel_id && !strcmp (toplevel_id, parent_toplevel_id)) {
			g_free (parent_toplevel_id);
			continue;
		}

		newlist = g_array_append_val (newlist, list[i]);

		g_free (parent_toplevel_id);
	}

	g_settings_set_strv (profile_settings, PANEL_OBJECT_ID_LIST_KEY, (const gchar **) newlist->data);
	g_array_free (newlist, TRUE);
	g_strfreev (list);
}

void
panel_profile_delete_toplevel (PanelToplevel *toplevel)
{
	const char *toplevel_id;

	toplevel_id = panel_profile_get_toplevel_id (toplevel);

	panel_profile_delete_toplevel_objects (toplevel_id);

	panel_profile_remove_from_list (PANEL_GSETTINGS_TOPLEVELS, toplevel_id);
}

PanelToplevel *
panel_profile_load_toplevel (const char *toplevel_id)
{
	PanelToplevel *toplevel;
	GdkScreen     *screen;
	char          *toplevel_path;
	char          *toplevel_background_path;

	if (!toplevel_id || !toplevel_id [0])
		return NULL;

	toplevel_path = g_strdup_printf ("%s%s/", PANEL_TOPLEVEL_PATH, toplevel_id);

	screen = gdk_display_get_default_screen (gdk_display_get_default ());

	if (screen == NULL) {
		g_free (toplevel_path);
		return NULL;
	}

	toplevel = g_object_new (PANEL_TYPE_TOPLEVEL,
				 "screen", screen,
				 NULL);

	panel_toplevel_set_settings_path (toplevel, toplevel_path);
	toplevel->settings = g_settings_new_with_path (PANEL_TOPLEVEL_SCHEMA, toplevel_path);
	toplevel->queued_settings = g_settings_new_with_path (PANEL_TOPLEVEL_SCHEMA, toplevel_path);

	toplevel_background_path = g_strdup_printf ("%sbackground/", toplevel_path);
	toplevel->background_settings = g_settings_new_with_path (PANEL_TOPLEVEL_BACKGROUND_SCHEMA, toplevel_background_path);

#define GET_INT(k, fn)                                              \
	{                                                               \
		int val;                                                    \
		val = g_settings_get_int (toplevel->settings, k);     \
		panel_toplevel_set_##fn (toplevel, val);                    \
	}

#define GET_BOOL(k, fn)                                                \
	{                                                                  \
		gboolean val;                                                  \
		val = g_settings_get_boolean (toplevel->settings, k);    \
		panel_toplevel_set_##fn (toplevel, val);                       \
	}

#define GET_STRING(k, fn)                                           \
	{                                                               \
		char *val;                                                  \
		val = g_settings_get_string (toplevel->settings, k);        \
		panel_toplevel_set_##fn (toplevel, val);                     \
		g_free (val);                                               \
	}

#define GET_ENUM(k, fn)                                           \
	{                                                               \
		int val;                                                  \
		val = g_settings_get_enum (toplevel->settings, k);        \
		panel_toplevel_set_##fn (toplevel, val);                     \
	}

	GET_STRING ("name", name);
	GET_INT ("monitor", monitor);
	GET_BOOL ("expand", expand);
	GET_ENUM ("orientation", orientation);
	GET_INT ("size", size);
	GET_BOOL ("auto-hide", auto_hide);
	GET_BOOL ("enable-animations", animate);
	GET_BOOL ("enable-buttons", enable_buttons);
	GET_BOOL ("enable-arrows", enable_arrows);
	GET_INT ("hide-delay", hide_delay);
	GET_INT ("unhide-delay", unhide_delay);
	GET_INT ("auto-hide-size", auto_hide_size);
	GET_ENUM ("animation-speed", animation_speed);

#define GET_POSITION(a, b, c, fn)                                          \
	{                                                                      \
		gboolean centered;                                                 \
		int      position;                                                 \
		int      position2;                                                \
		centered = g_settings_get_boolean (toplevel->settings, c);   \
		position = g_settings_get_int (toplevel->settings, a);       \
		position2 = g_settings_get_int (toplevel->settings, b);      \
		panel_toplevel_set_##fn (toplevel, position, position2, centered); \
	}

	GET_POSITION ("x", "x-right", "x-centered", x);
	GET_POSITION ("y", "y-bottom", "y-centered", y);

	panel_profile_load_background (toplevel);

	panel_profile_set_toplevel_id (toplevel, toplevel_id);

	panel_profile_connect_to_toplevel (toplevel);

	g_signal_connect (toplevel->settings,
					  "changed",
					  G_CALLBACK (panel_profile_toplevel_change_notify),
					  toplevel);
	g_signal_connect (toplevel->background_settings,
					  "changed",
					  G_CALLBACK (panel_profile_background_change_notify),
					  toplevel);

	g_free (toplevel_path);
	g_free (toplevel_background_path);

	panel_setup (toplevel);

	return toplevel;
}

static void
panel_profile_load_and_show_toplevel (char *toplevel_id)
{
	PanelToplevel  *toplevel;
	gchar         **objects;
	gboolean        loading_queued_applets;

	toplevel = panel_profile_load_toplevel (toplevel_id);
	if (!toplevel)
		return;

	gtk_widget_show (GTK_WIDGET (toplevel));

	loading_queued_applets = FALSE;

	/* reload list of objects to get those that might be on the new
	 * toplevel */
	GSettings *panel_settings;
	panel_settings = g_settings_new (PANEL_SCHEMA);
	objects = g_settings_get_strv (panel_settings, PANEL_OBJECT_ID_LIST_KEY);

	if (objects) {
		panel_profile_object_id_list_update (objects);
		loading_queued_applets = TRUE;
	}

	if (!loading_queued_applets)
		mate_panel_applet_load_queued_applets (FALSE);

	g_strfreev (objects);
	g_object_unref (panel_settings);
}

static void
panel_profile_load_and_show_toplevel_startup (const char *toplevel_id)
{
	PanelToplevel *toplevel;
	toplevel = panel_profile_load_toplevel (toplevel_id);
	if (toplevel)
		gtk_widget_show (GTK_WIDGET (toplevel));
}

static void
panel_profile_destroy_toplevel (const char *id)
{
	PanelToplevel *toplevel;

	if (!(toplevel = panel_profile_get_toplevel_by_id (id)))
		return;

	gtk_widget_destroy (GTK_WIDGET (toplevel));
}

char *
panel_profile_prepare_object_with_id (PanelObjectType  object_type,
				      const char      *toplevel_id,
				      int              position,
				      gboolean         right_stick)
{
	PanelGSettingsKeyType  key_type;
	char              *id;
	char              *settings_path;
	GSettings         *settings;

	key_type = PANEL_GSETTINGS_OBJECTS;
	id = panel_profile_find_new_id (key_type);

	settings_path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);

	settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, settings_path);

	g_settings_set_enum (settings, PANEL_OBJECT_TYPE_KEY, object_type);
	g_settings_set_string (settings, PANEL_OBJECT_TOPLEVEL_ID_KEY, toplevel_id);
	g_settings_set_int (settings, PANEL_OBJECT_POSITION_KEY, position);
	g_settings_set_boolean (settings, PANEL_OBJECT_PANEL_RIGHT_STICK_KEY, right_stick);

	/* Force writing the settings in order to reserve the object ID *now*,
	 * so that a later call to panel_profile_find_new_id() won't find the same
	 * one. */
	g_settings_sync ();

	g_free (settings_path);
	g_object_unref (settings);

	return id;
}

char *
panel_profile_prepare_object (PanelObjectType  object_type,
			      PanelToplevel   *toplevel,
			      int              position,
			      gboolean         right_stick)
{
	return panel_profile_prepare_object_with_id (object_type,
						     panel_profile_get_toplevel_id (toplevel),
						     position,
						     right_stick);
}

void
panel_profile_delete_object (AppletInfo *applet_info)
{
	PanelGSettingsKeyType  type;
	const char        *id;

	type = PANEL_GSETTINGS_OBJECTS;
	id = mate_panel_applet_get_id (applet_info);

	panel_profile_remove_from_list (type, id);
}

static void
panel_profile_load_object (char *id)
{
	PanelObjectType  object_type;
	char            *object_path;
	char            *toplevel_id;
	int              position;
	gboolean         right_stick;
	gboolean         locked;
	GSettings       *settings;

	object_path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);
	settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, object_path);

	object_type = g_settings_get_enum (settings, PANEL_OBJECT_TYPE_KEY);
	position = g_settings_get_int (settings, PANEL_OBJECT_POSITION_KEY);
	toplevel_id = g_settings_get_string (settings, PANEL_OBJECT_TOPLEVEL_ID_KEY);
	right_stick = g_settings_get_boolean (settings, PANEL_OBJECT_PANEL_RIGHT_STICK_KEY);
	locked = g_settings_get_boolean (settings, PANEL_OBJECT_LOCKED_KEY);

	mate_panel_applet_queue_applet_to_load (id,
					   object_type,
					   toplevel_id,
					   position,
					   right_stick,
					   locked);

	g_free (toplevel_id);
	g_free (object_path);
	g_object_unref (settings);
}

static void
panel_profile_destroy_object (const char *id)
{
	AppletInfo *info;

	info = mate_panel_applet_get_by_id (id);

	mate_panel_applet_clean (info);
}

static void
panel_profile_delete_dir (PanelGSettingsKeyType  type,
						  const char            *id)
{
	gchar *dir = NULL;

	switch (type) {
		case PANEL_GSETTINGS_TOPLEVELS:
			dir = g_strdup_printf (PANEL_TOPLEVEL_PATH "%s/", id);
			break;
		case PANEL_GSETTINGS_OBJECTS:
			dir = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	if (type == PANEL_GSETTINGS_TOPLEVELS) {
		gchar *subdir;
		subdir = g_strdup_printf (PANEL_TOPLEVEL_PATH "%s/background/", id);
		mate_dconf_recursive_reset (subdir, NULL);
		g_free (subdir);
	}
	else if (type == PANEL_GSETTINGS_OBJECTS) {
		gchar *subdir;
		subdir = g_strdup_printf (PANEL_TOPLEVEL_PATH "%s/prefs/", id);
		mate_dconf_recursive_reset (subdir, NULL);
		g_free (subdir);
	}

	if (dir != NULL) {
		mate_dconf_recursive_reset (dir, NULL);
		g_free (dir);
	}
}

static gboolean
panel_profile_object_exists (GSList                *list,
			     const char            *id,
			     PanelProfileGetIdFunc  get_id_func)
{
	GSList *l;

	if (!list || !id)
		return FALSE;

	for (l = list; l; l = l->next) {
		const char *check_id;

		check_id = get_id_func (l->data);
		g_assert (check_id != NULL);

		if (!strcmp (check_id, id))
			return TRUE;
	}

	return FALSE;
}

static gboolean
panel_profile_id_exists (GSList     *id_list,
			 const char *id)
{
	GSList *l;

	if (!id_list || !id)
		return FALSE;

	for (l = id_list; l; l = l->next) {
		const char *check_id = l->data;

		if (!strcmp (id, check_id))
			return TRUE;
	}

	return FALSE;
}

static void
panel_profile_load_added_ids (GSList                 *list,
							  GSList                 *id_list,
							  PanelProfileGetIdFunc   get_id_func,
							  PanelProfileLoadFunc    load_handler,
							  PanelProfileOnLoadQueue on_load_queue)
{
	GSList *added_ids = NULL;
	GSList *l;

	for (l = id_list; l; l = l->next) {
		const char *id = l->data;

		if (!panel_profile_object_exists (list, id, get_id_func) &&
		    (on_load_queue == NULL || !on_load_queue (id)))
			added_ids = g_slist_prepend (added_ids, g_strdup (id));
	}

	for (l = added_ids; l; l = l->next) {
		char *id;
		id = (char *) l->data;

		if (id && id[0])
			load_handler (id);

		g_free (l->data);
		l->data = NULL;
	}

	g_slist_free (added_ids);
}

static void
panel_profile_delete_removed_ids (PanelGSettingsKeyType    type,
								  GSList                  *list,
								  GSList                  *id_list,
								  PanelProfileGetIdFunc    get_id_func,
								  PanelProfileDestroyFunc  destroy_handler)
{
	GSList *removed_ids = NULL;
	GSList *l;

	for (l = list; l; l = l->next) {
		const char *id;

		id = get_id_func (l->data);

		if (!panel_profile_id_exists (id_list, id))
			removed_ids = g_slist_prepend (removed_ids, g_strdup (id));
	}

	for (l = removed_ids; l; l = l->next) {
		const char *id = l->data;

		panel_profile_delete_dir (type, id);
		destroy_handler (id);

		g_free (l->data);
		l->data = NULL;
	}
	g_slist_free (removed_ids);
}

static gboolean
load_default_layout_idle (gpointer unused) {
	if (g_slist_length (panel_toplevel_list_toplevels ()) != 0) {
		/* some toplevels are not destroyed yet, waiting */
		return TRUE;
	}

	/* load the default layout and stop this handler */
	panel_profile_ensure_toplevel_per_screen ();
	return FALSE;
}

static void
panel_profile_toplevel_id_list_notify (GSettings *settings,
									   gchar *key,
									   gpointer   user_data)
{
	GSList     *l, *existing_toplevels;
	GSList     *toplevel_ids;
	gchar     **toplevel_ids_strv;

	toplevel_ids_strv = g_settings_get_strv (settings, key);

	toplevel_ids = mate_gsettings_strv_to_gslist ((const gchar **) toplevel_ids_strv);
	toplevel_ids = panel_g_slist_make_unique (toplevel_ids,
						  (GCompareFunc) g_strcmp0,
						  TRUE);
	g_strfreev (toplevel_ids_strv);

	existing_toplevels = NULL;
	for (l = panel_toplevel_list_toplevels (); l; l = l->next) {
		PanelToplevel *toplevel = l->data;

		/* Attached toplevels aren't on the id list */
		if (panel_toplevel_get_is_attached (toplevel))
			continue;

		existing_toplevels = g_slist_prepend (existing_toplevels, toplevel);
	}

	panel_profile_load_added_ids (existing_toplevels,
								  toplevel_ids,
								  (PanelProfileGetIdFunc) panel_profile_get_toplevel_id,
								  (PanelProfileLoadFunc) panel_profile_load_and_show_toplevel,
								  (PanelProfileOnLoadQueue) NULL);

	panel_profile_delete_removed_ids (PANEL_GSETTINGS_TOPLEVELS,
									  existing_toplevels,
									  toplevel_ids,
									  (PanelProfileGetIdFunc) panel_profile_get_toplevel_id,
									  (PanelProfileDestroyFunc) panel_profile_destroy_toplevel);

	/* if there are no panels, reset layout to default */
	if (g_slist_length (toplevel_ids) == 0)
		g_idle_add (load_default_layout_idle, NULL);

	g_slist_free (existing_toplevels);
	g_slist_free_full (toplevel_ids, g_free);
}

static void
panel_profile_object_id_list_update (gchar **objects)
{
	GSList *existing_applets;
	GSList *sublist = NULL, *l;
	GSList *object_ids;

	object_ids = mate_gsettings_strv_to_gslist ((const gchar **) objects);
	object_ids = panel_g_slist_make_unique (object_ids,
						(GCompareFunc) g_strcmp0,
						TRUE);

	existing_applets = mate_panel_applet_list_applets ();

	for (l = existing_applets; l; l = l->next) {
		AppletInfo *info = l->data;
		sublist = g_slist_prepend (sublist, info);
	}

	panel_profile_load_added_ids (sublist,
								  object_ids,
								  (PanelProfileGetIdFunc) mate_panel_applet_get_id,
								  (PanelProfileLoadFunc) panel_profile_load_object,
								  (PanelProfileOnLoadQueue) mate_panel_applet_on_load_queue);

	panel_profile_delete_removed_ids (PANEL_GSETTINGS_OBJECTS,
									  sublist,
									  object_ids,
									  (PanelProfileGetIdFunc) mate_panel_applet_get_id,
									  (PanelProfileDestroyFunc) panel_profile_destroy_object);

	g_slist_free (sublist);
	g_slist_free_full (object_ids, g_free);

	mate_panel_applet_load_queued_applets (FALSE);
}

static void
panel_profile_object_id_list_notify (GSettings *settings,
									 gchar *key,
									 gpointer data)
{
	gchar **objects;
	objects = g_settings_get_strv (settings, key);
	panel_profile_object_id_list_update (objects);
	g_strfreev (objects);
}

static void
panel_profile_load_list (GSettings              *settings,
						 PanelGSettingsKeyType   type,
						 PanelProfileLoadFunc    load_handler,
						 GCallback               notify_handler)
{

	const gchar *key = key_from_type (type);
	gchar  *changed_signal;
	gchar **list;
	gint    i;

	changed_signal = g_strdup_printf ("changed::%s", key);
	g_signal_connect (settings, changed_signal, G_CALLBACK (notify_handler), NULL);
	g_free (changed_signal);

	list = g_settings_get_strv (settings, key);

	for (i = 0; list[i]; i++) {
		load_handler (list[i]);
	}

	if (list)
		g_strfreev (list);
}

static void
panel_profile_ensure_toplevel_per_screen ()
{
	GSList     *toplevels;
	GSList     *empty_screens = NULL;
	GSList     *l;
	GdkDisplay *display;
	GdkScreen  *screen;

	toplevels = panel_toplevel_list_toplevels ();

	display = gdk_display_get_default ();

	screen = gdk_display_get_default_screen (display);

	for (l = toplevels; l; l = l->next)
		if (gtk_window_get_screen (l->data) == screen)
			break;

	if (!l)
		empty_screens = g_slist_prepend (empty_screens, screen);

	for (l = empty_screens; l; l = l->next)
		panel_layout_apply_default_from_gkeyfile (l->data);

	g_slist_free (empty_screens);
}

void
panel_profile_settings_load (void)
{
	profile_settings = g_settings_new ("org.mate.panel");
}

void
panel_profile_load (void)
{
	panel_profile_settings_load();

	panel_profile_load_list (profile_settings,
				 PANEL_GSETTINGS_TOPLEVELS,
				 (PanelProfileLoadFunc)panel_profile_load_and_show_toplevel_startup,
				 G_CALLBACK (panel_profile_toplevel_id_list_notify));
	panel_profile_load_list (profile_settings,
				 PANEL_GSETTINGS_OBJECTS,
				 (PanelProfileLoadFunc)panel_profile_load_object,
				 G_CALLBACK (panel_profile_object_id_list_notify));

	panel_profile_ensure_toplevel_per_screen ();

	mate_panel_applet_load_queued_applets (TRUE);
}

static gboolean
get_program_listing_setting (const char *key)
{
	gboolean retval;
	retval = g_settings_get_boolean (profile_settings, key);
	return retval;
}

gboolean
panel_profile_get_show_program_list (void)
{
	return get_program_listing_setting ("show-program-list");
}

gboolean
panel_profile_get_enable_program_list (void)
{
	return get_program_listing_setting ("enable-program-list");
}

gboolean
panel_profile_get_enable_autocompletion (void)
{
	return get_program_listing_setting ("enable-autocompletion");
}

void
panel_profile_set_show_program_list (gboolean show_program_list)
{
	g_settings_set_boolean (profile_settings, "show-program-list", show_program_list);
}

gboolean
panel_profile_is_writable_show_program_list (void)
{
	return g_settings_is_writable (profile_settings, "show-program-list");
}

gboolean
panel_profile_can_be_moved_freely (PanelToplevel *toplevel)
{
	if (panel_lockdown_get_locked_down () ||
	    !g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_ORIENTATION_KEY))
		return FALSE;

	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_SCREEN_KEY))
		return FALSE;

	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_MONITOR_KEY))
		return FALSE;

	/* For expanded panels we don't really have to check
	   x and y */
	if (panel_toplevel_get_expand (toplevel))
		return TRUE;

	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_X_KEY))
		return FALSE;
	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_X_RIGHT_KEY))
		return FALSE;
	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_X_CENTERED_KEY))
		return FALSE;

	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_Y_KEY))
		return FALSE;
	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_Y_BOTTOM_KEY))
		return FALSE;
	if (!g_settings_is_writable (toplevel->settings, PANEL_TOPLEVEL_Y_CENTERED_KEY))
		return FALSE;

	return TRUE;
}
