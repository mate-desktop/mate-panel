/*
 * mate-panel-applet.h: panel applet writing API.
 *
 * Copyright (C) 2001 Sun Microsystems, Inc.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *     Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __MATE_PANEL_APPLET_H__
#define __MATE_PANEL_APPLET_H__

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <cairo.h>
#include <cairo-gobject.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	MATE_PANEL_APPLET_ORIENT_UP,
	MATE_PANEL_APPLET_ORIENT_DOWN,
	MATE_PANEL_APPLET_ORIENT_LEFT,
	MATE_PANEL_APPLET_ORIENT_RIGHT
#define MATE_PANEL_APPLET_ORIENT_FIRST MATE_PANEL_APPLET_ORIENT_UP
#define MATE_PANEL_APPLET_ORIENT_LAST  MATE_PANEL_APPLET_ORIENT_RIGHT
} MatePanelAppletOrient;

#define PANEL_TYPE_APPLET              (mate_panel_applet_get_type ())
#define MATE_PANEL_APPLET(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PANEL_TYPE_APPLET, MatePanelApplet))
#define MATE_PANEL_APPLET_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PANEL_TYPE_APPLET, MatePanelAppletClass))
#define PANEL_IS_APPLET(o)             (G_TYPE_CHECK_INSTANCE_TYPE ((o), PANEL_TYPE_APPLET))
#define PANEL_IS_APPLET_CLASS(k)       (G_TYPE_CHECK_CLASS_TYPE ((k), PANEL_TYPE_APPLET))
#define MATE_PANEL_APPLET_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PANEL_TYPE_APPLET, MatePanelAppletClass))

typedef enum {
	PANEL_NO_BACKGROUND,
	PANEL_COLOR_BACKGROUND,
	PANEL_PIXMAP_BACKGROUND
} MatePanelAppletBackgroundType;

typedef enum {
	MATE_PANEL_APPLET_FLAGS_NONE   = 0,
	MATE_PANEL_APPLET_EXPAND_MAJOR = 1 << 0,
	MATE_PANEL_APPLET_EXPAND_MINOR = 1 << 1,
	MATE_PANEL_APPLET_HAS_HANDLE   = 1 << 2
#define MATE_PANEL_APPLET_FLAGS_ALL (MATE_PANEL_APPLET_EXPAND_MAJOR|MATE_PANEL_APPLET_EXPAND_MINOR|MATE_PANEL_APPLET_HAS_HANDLE)
} MatePanelAppletFlags;

typedef struct _MatePanelApplet        MatePanelApplet;
typedef struct _MatePanelAppletClass   MatePanelAppletClass;
typedef struct _MatePanelAppletPrivate MatePanelAppletPrivate;

typedef gboolean (*MatePanelAppletFactoryCallback) (MatePanelApplet* applet, const gchar *iid, gpointer user_data);

struct _MatePanelApplet {
	GtkEventBox event_box;

	MatePanelAppletPrivate* priv;
};

struct _MatePanelAppletClass {
	GtkEventBoxClass event_box_class;

	void (*change_orient) (MatePanelApplet* applet, MatePanelAppletOrient orient);

	void (*change_size) (MatePanelApplet* applet, guint size);

	void (*change_background) (MatePanelApplet *applet, MatePanelAppletBackgroundType type, GdkRGBA* color, cairo_pattern_t *pattern);

	void (*move_focus_out_of_applet) (MatePanelApplet* frame, GtkDirectionType direction);
};

GType mate_panel_applet_get_type(void) G_GNUC_CONST;

GtkWidget* mate_panel_applet_new(void);

MatePanelAppletOrient mate_panel_applet_get_orient(MatePanelApplet* applet);
guint mate_panel_applet_get_size(MatePanelApplet* applet);
MatePanelAppletBackgroundType mate_panel_applet_get_background (MatePanelApplet *applet, /* return values */ GdkRGBA* color, cairo_pattern_t** pattern);
void mate_panel_applet_set_background_widget(MatePanelApplet* applet, GtkWidget* widget);

gchar* mate_panel_applet_get_preferences_path(MatePanelApplet* applet);

MatePanelAppletFlags mate_panel_applet_get_flags(MatePanelApplet* applet);
void mate_panel_applet_set_flags(MatePanelApplet* applet, MatePanelAppletFlags flags);

void mate_panel_applet_set_size_hints(MatePanelApplet* applet, const int* size_hints, int n_elements, int base_size);

gboolean mate_panel_applet_get_locked_down(MatePanelApplet* applet);

// Does nothing when not on X11
void mate_panel_applet_request_focus(MatePanelApplet* applet, guint32 timestamp);

void mate_panel_applet_setup_menu(MatePanelApplet* applet, const gchar* xml, GtkActionGroup* action_group);
void mate_panel_applet_setup_menu_from_file(MatePanelApplet* applet, const gchar* filename, GtkActionGroup* action_group);
void mate_panel_applet_setup_menu_from_resource (MatePanelApplet    *applet,
                                                 const gchar        *resource_path,
                                                 GtkActionGroup     *action_group);

int mate_panel_applet_factory_main(const gchar* factory_id,gboolean  out_process, GType applet_type, MatePanelAppletFactoryCallback callback, gpointer data);

int  mate_panel_applet_factory_setup_in_process (const gchar               *factory_id,
							  GType                      applet_type,
							  MatePanelAppletFactoryCallback callback,
							  gpointer                   data);


/*
 * These macros are getting a bit unwieldy.
 *
 * Things to define for these:
 *	+ required if Native Language Support is enabled (ENABLE_NLS):
 *                   GETTEXT_PACKAGE and MATELOCALEDIR
 */

#if !defined(ENABLE_NLS)
	#define _MATE_PANEL_APPLET_SETUP_GETTEXT(call_textdomain) \
		do { } while (0)
#else /* defined(ENABLE_NLS) */
	#include <libintl.h>
	#define _MATE_PANEL_APPLET_SETUP_GETTEXT(call_textdomain) \
	do { \
		bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR); \
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8"); \
		if (call_textdomain) \
			textdomain (GETTEXT_PACKAGE); \
	} while (0)
#endif /* !defined(ENABLE_NLS) */

#define MATE_PANEL_APPLET_OUT_PROCESS_FACTORY(factory_id, type, name, callback, data) \
int main(int argc, char* argv[]) \
{ \
	GOptionContext* context; \
	GError* error; \
	int retval; \
	 \
	_MATE_PANEL_APPLET_SETUP_GETTEXT (TRUE); \
	 \
	context = g_option_context_new(""); \
	g_option_context_add_group (context, gtk_get_option_group(TRUE)); \
	 \
	error = NULL; \
	if (!g_option_context_parse (context, &argc, &argv, &error)) \
	{ \
		if (error) \
		{ \
			g_printerr ("Cannot parse arguments: %s.\n", error->message); \
			g_error_free (error); \
		} \
		else \
		{ \
			g_printerr ("Cannot parse arguments.\n"); \
		} \
		g_option_context_free (context); \
		return 1; \
	} \
	 \
	gtk_init (&argc, &argv); \
	 \
	retval = mate_panel_applet_factory_main (factory_id,TRUE, type, callback, data); \
	g_option_context_free (context); \
	 \
	return retval; \
}

#define MATE_PANEL_APPLET_IN_PROCESS_FACTORY(factory_id, type, descr, callback, data) \
gboolean _mate_panel_applet_shlib_factory (void);	\
G_MODULE_EXPORT gint _mate_panel_applet_shlib_factory(void) \
{ \
	_MATE_PANEL_APPLET_SETUP_GETTEXT(FALSE); \
return mate_panel_applet_factory_setup_in_process (factory_id, type,                 \
                                               callback, data);  \
}

#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLET_H__ */
