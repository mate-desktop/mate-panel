/*
 * mate-panel-applet.c: panel applet writing library.
 *
 * Copyright (c) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>
#include <cairo.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#ifdef HAVE_X11
#include <cairo-xlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#include <X11/Xatom.h>
#include "panel-plug-private.h"
#endif

#include "mate-panel-applet.h"
#include "panel-applet-private.h"
#include "mate-panel-applet-factory.h"
#include "mate-panel-applet-marshal.h"
#include "mate-panel-applet-enums.h"

struct _MatePanelAppletPrivate {
	GtkWidget         *plug;
	GDBusConnection   *connection;

	gboolean           out_of_process;

	char              *id;
	GClosure          *closure;
	char              *object_path;
	guint              object_id;
	char              *prefs_path;

	GtkUIManager      *ui_manager;
	GtkActionGroup    *applet_action_group;
	GtkActionGroup    *panel_action_group;

	MatePanelAppletFlags   flags;
	MatePanelAppletOrient  orient;
	guint              size;
	char              *background;

	int                previous_width;
	int                previous_height;

	int               *size_hints;
	int                size_hints_len;

	gboolean           moving_focus_out;

	gboolean           locked;
	gboolean           locked_down;
};

enum {
	CHANGE_ORIENT,
	CHANGE_SIZE,
	CHANGE_BACKGROUND,
	MOVE_FOCUS_OUT_OF_APPLET,
	LAST_SIGNAL
};

static guint mate_panel_applet_signals[LAST_SIGNAL] = { 0 };

enum {
	PROP_0,
	PROP_OUT_OF_PROCESS,
	PROP_ID,
	PROP_CLOSURE,
	PROP_CONNECTION,
	PROP_PREFS_PATH,
	PROP_ORIENT,
	PROP_SIZE,
	PROP_BACKGROUND,
	PROP_FLAGS,
	PROP_SIZE_HINTS,
	PROP_LOCKED,
	PROP_LOCKED_DOWN
};

static void       mate_panel_applet_handle_background   (MatePanelApplet       *applet);
static GtkAction *mate_panel_applet_menu_get_action     (MatePanelApplet       *applet,
						    const gchar       *action);
static void       mate_panel_applet_menu_update_actions (MatePanelApplet       *applet);
static void       mate_panel_applet_menu_cmd_remove     (GtkAction         *action,
						    MatePanelApplet       *applet);
static void       mate_panel_applet_menu_cmd_move       (GtkAction         *action,
						    MatePanelApplet       *applet);
static void       mate_panel_applet_menu_cmd_lock       (GtkAction         *action,
						    MatePanelApplet       *applet);
static void       mate_panel_applet_register_object     (MatePanelApplet       *applet);
void	_mate_panel_applet_apply_css	(GtkWidget* widget, MatePanelAppletBackgroundType type);

static const gchar panel_menu_ui[] =
	"<ui>\n"
	"  <popup name=\"MatePanelAppletPopup\" action=\"PopupAction\">\n"
	"    <placeholder name=\"AppletItems\"/>\n"
	"    <separator/>\n"
	"    <menuitem name=\"RemoveItem\" action=\"Remove\"/>\n"
	"    <menuitem name=\"MoveItem\" action=\"Move\"/>\n"
	"    <separator/>\n"
	"    <menuitem name=\"LockItem\" action=\"Lock\"/>\n"
	"  </popup>\n"
	"</ui>\n";

static const GtkActionEntry menu_entries[] = {
	{ "Remove", "list-remove", N_("_Remove From Panel"),
	  NULL, NULL,
	  G_CALLBACK (mate_panel_applet_menu_cmd_remove) },
	{ "Move", NULL, N_("_Move"),
	  NULL, NULL,
	  G_CALLBACK (mate_panel_applet_menu_cmd_move) }
};

static const GtkToggleActionEntry menu_toggle_entries[] = {
	{ "Lock", NULL, N_("Loc_k To Panel"),
	  NULL, NULL,
	  G_CALLBACK (mate_panel_applet_menu_cmd_lock) }
};

G_DEFINE_TYPE_WITH_PRIVATE (MatePanelApplet, mate_panel_applet, GTK_TYPE_EVENT_BOX)

#define MATE_PANEL_APPLET_INTERFACE   "org.mate.panel.applet.Applet"
#define MATE_PANEL_APPLET_OBJECT_PATH "/org/mate/panel/applet/%s/%d"

char *
mate_panel_applet_get_preferences_path (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	if (!applet->priv->prefs_path)
		return NULL;

	return g_strdup (applet->priv->prefs_path);
}

static void
mate_panel_applet_set_preferences_path (MatePanelApplet *applet,
				  const char  *prefs_path)
{
	if (applet->priv->prefs_path == prefs_path)
		return;

	if (g_strcmp0 (applet->priv->prefs_path, prefs_path) == 0)
		return;

	if (prefs_path) {
		applet->priv->prefs_path = g_strdup (prefs_path);

	}

	g_object_notify (G_OBJECT (applet), "prefs-path");
}

MatePanelAppletFlags
mate_panel_applet_get_flags (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), MATE_PANEL_APPLET_FLAGS_NONE);

	return applet->priv->flags;
}

void
mate_panel_applet_set_flags (MatePanelApplet      *applet,
			MatePanelAppletFlags  flags)
{
	g_return_if_fail (PANEL_IS_APPLET (applet));

	if (applet->priv->flags == flags)
		return;

	applet->priv->flags = flags;

	g_object_notify (G_OBJECT (applet), "flags");

	if (applet->priv->connection) {
		GVariantBuilder  builder;
		GVariantBuilder  invalidated_builder;
		GError          *error = NULL;

		g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
		g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));

		g_variant_builder_add (&builder, "{sv}", "Flags",
				       g_variant_new_uint32 (applet->priv->flags));

		g_dbus_connection_emit_signal (applet->priv->connection,
					       NULL,
					       applet->priv->object_path,
					       "org.freedesktop.DBus.Properties",
					       "PropertiesChanged",
					       g_variant_new ("(sa{sv}as)",
							      MATE_PANEL_APPLET_INTERFACE,
							      &builder,
							      &invalidated_builder),
					       &error);
		if (error) {
			g_printerr ("Failed to send signal PropertiesChanged::Flags: %s\n",
				    error->message);
			g_error_free (error);
		}
		g_variant_builder_clear (&builder);
		g_variant_builder_clear (&invalidated_builder);
	}
}

static void
mate_panel_applet_size_hints_ensure (MatePanelApplet *applet,
				int          new_size)
{
	if (applet->priv->size_hints && applet->priv->size_hints_len < new_size) {
		g_free (applet->priv->size_hints);
		applet->priv->size_hints = g_new (gint, new_size);
	} else if (!applet->priv->size_hints) {
		applet->priv->size_hints = g_new (gint, new_size);
	}
	applet->priv->size_hints_len = new_size;
}

static gboolean
mate_panel_applet_size_hints_changed (MatePanelApplet *applet,
				 const int   *size_hints,
				 int          n_elements,
				 int          base_size)
{
	gint i;

	if (!applet->priv->size_hints)
		return TRUE;

	if (applet->priv->size_hints_len != n_elements)
		return TRUE;

	for (i = 0; i < n_elements; i++) {
		if (size_hints[i] + base_size != applet->priv->size_hints[i])
			return TRUE;
	}

	return FALSE;
}

/**
 * mate_panel_applet_set_size_hints:
 * @applet: applet
 * @size_hints: (array length=n_elements): List of integers
 * @n_elements: Length of @size_hints
 * @base_size: base_size
 */
void
mate_panel_applet_set_size_hints (MatePanelApplet *applet,
			     const int   *size_hints,
			     int          n_elements,
			     int          base_size)
{
	gint i;

	/* Make sure property has really changed to avoid bus traffic */
	if (!mate_panel_applet_size_hints_changed (applet, size_hints, n_elements, base_size))
		return;

	mate_panel_applet_size_hints_ensure (applet, n_elements);
	for (i = 0; i < n_elements; i++)
		applet->priv->size_hints[i] = size_hints[i] + base_size;

	g_object_notify (G_OBJECT (applet), "size-hints");

	if (applet->priv->connection) {
		GVariantBuilder  builder;
		GVariantBuilder  invalidated_builder;
		GVariant       **children;
		GError          *error = NULL;

		g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
		g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));

		children = g_new (GVariant *, applet->priv->size_hints_len);
		for (i = 0; i < n_elements; i++)
			children[i] = g_variant_new_int32 (applet->priv->size_hints[i]);
		g_variant_builder_add (&builder, "{sv}", "SizeHints",
				       g_variant_new_array (G_VARIANT_TYPE_INT32,
							    children, applet->priv->size_hints_len));
		g_free (children);

		g_dbus_connection_emit_signal (applet->priv->connection,
					       NULL,
					       applet->priv->object_path,
					       "org.freedesktop.DBus.Properties",
					       "PropertiesChanged",
					       g_variant_new ("(sa{sv}as)",
							      MATE_PANEL_APPLET_INTERFACE,
							      &builder,
							      &invalidated_builder),
					       &error);
		if (error) {
			g_printerr ("Failed to send signal PropertiesChanged::SizeHints: %s\n",
				    error->message);
			g_error_free (error);
		}
		g_variant_builder_clear (&builder);
		g_variant_builder_clear (&invalidated_builder);
	}
}

guint
mate_panel_applet_get_size (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), 0);

	return applet->priv->size;
}

/* Applets cannot set their size, so API is not public. */
static void
mate_panel_applet_set_size (MatePanelApplet *applet,
		       guint        size)
{
	g_return_if_fail (PANEL_IS_APPLET (applet));

	if (applet->priv->size == size)
		return;

	applet->priv->size = size;
	g_signal_emit (G_OBJECT (applet),
		       mate_panel_applet_signals [CHANGE_SIZE],
		       0, size);

	g_object_notify (G_OBJECT (applet), "size");
}

MatePanelAppletOrient
mate_panel_applet_get_orient (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), 0);

	return applet->priv->orient;
}

/* Applets cannot set their orientation, so API is not public. */
static void
mate_panel_applet_set_orient (MatePanelApplet      *applet,
			 MatePanelAppletOrient orient)
{
	g_return_if_fail (PANEL_IS_APPLET (applet));

	if (applet->priv->orient == orient)
		return;

	applet->priv->orient = orient;
	g_signal_emit (G_OBJECT (applet),
		       mate_panel_applet_signals [CHANGE_ORIENT],
		       0, orient);

	g_object_notify (G_OBJECT (applet), "orient");
}

static void
mate_panel_applet_set_locked (MatePanelApplet *applet,
			 gboolean     locked)
{
	GtkAction *action;

	g_return_if_fail (PANEL_IS_APPLET (applet));

	if (applet->priv->locked == locked)
		return;

	applet->priv->locked = locked;

	action = mate_panel_applet_menu_get_action (applet, "Lock");
	g_signal_handlers_block_by_func (action,
					 mate_panel_applet_menu_cmd_lock,
					 applet);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), locked);
	g_signal_handlers_unblock_by_func (action,
					   mate_panel_applet_menu_cmd_lock,
					   applet);

	mate_panel_applet_menu_update_actions (applet);

	g_object_notify (G_OBJECT (applet), "locked");

	if (applet->priv->connection) {
		GError *error = NULL;

		g_dbus_connection_emit_signal (applet->priv->connection,
					       NULL,
					       applet->priv->object_path,
					       MATE_PANEL_APPLET_INTERFACE,
					       locked ? "Lock" : "Unlock",
					       NULL, &error);
		if (error) {
			g_printerr ("Failed to send signal %s: %s\n",
				    locked ? "Lock" : "Unlock",
				    error->message);
			g_error_free (error);
		}
	}
}

gboolean
mate_panel_applet_get_locked_down (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), FALSE);

	return applet->priv->locked_down;
}

/* Applets cannot set the lockdown state, so API is not public. */
static void
mate_panel_applet_set_locked_down (MatePanelApplet *applet,
			      gboolean     locked_down)
{
	g_return_if_fail (PANEL_IS_APPLET (applet));

	if (applet->priv->locked_down == locked_down)
		return;

	applet->priv->locked_down = locked_down;
	mate_panel_applet_menu_update_actions (applet);

	g_object_notify (G_OBJECT (applet), "locked-down");
}

#ifdef HAVE_X11

static Atom _net_wm_window_type = None;
static Atom _net_wm_window_type_dock = None;
static Atom _net_active_window = None;

static void
mate_panel_applet_init_atoms (Display *xdisplay)
{
	if (_net_wm_window_type == None)
		_net_wm_window_type = XInternAtom (xdisplay,
						   "_NET_WM_WINDOW_TYPE",
						   False);

	if (_net_wm_window_type_dock == None)
		_net_wm_window_type_dock = XInternAtom (xdisplay,
							"_NET_WM_WINDOW_TYPE_DOCK",
							False);

	if (_net_active_window == None)
		_net_active_window = XInternAtom (xdisplay,
						  "_NET_ACTIVE_WINDOW",
						  False);
}

static Window
mate_panel_applet_find_toplevel_dock_window (MatePanelApplet *applet,
					Display	    *xdisplay)
{
	GtkWidget  *toplevel;
	Window	    xwin;
	Window	    root, parent, *child;
	int	    num_children;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (applet));
	if (!gtk_widget_get_realized (toplevel))
		return None;

	xwin = GDK_WINDOW_XID (gtk_widget_get_window (toplevel));

	child = NULL;
	parent = root = None;
	do {
		Atom	type_return;
		Atom	window_type;
		int	format_return;
		gulong	number_return, bytes_after_return;
		guchar *data_return;

		XGetWindowProperty (xdisplay,
				    xwin,
				    _net_wm_window_type,
				    0, 1, False,
				    XA_ATOM,
				    &type_return, &format_return,
				    &number_return,
				    &bytes_after_return,
				    &data_return);

		if (type_return == XA_ATOM) {
			window_type = *(Atom *) data_return;

			XFree (data_return);
			data_return = NULL;

			if (window_type == _net_wm_window_type_dock)
				return xwin;
		}

		if (!XQueryTree (xdisplay,
			   xwin,
			   &root, &parent, &child,
			   (guint *) &num_children)) {
			   return None;
		}

		if (child && num_children > 0)
			XFree (child);

		xwin = parent;

	} while (xwin != None && xwin != root);

	return None;
}

#endif // HAVE_X11

/* This function
 *   1) Gets the window id of the panel that contains the applet
 *	using XQueryTree and XGetWindowProperty to find an ancestor
 *	window with the _NET_WM_WINDOW_TYPE_DOCK window type.
 *   2) Sends a _NET_ACTIVE_WINDOW message to get that panel focused
 */
void
mate_panel_applet_request_focus (MatePanelApplet	 *applet,
			    guint32	  timestamp)
{
#ifdef HAVE_X11
	GdkScreen  *screen;
	GdkWindow  *root;
	GdkDisplay *display;
	Display	   *xdisplay;
	Window	    dock_xwindow;
	Window	    xroot;
	XEvent	    xev;

	if (!GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
		return;

	g_return_if_fail (PANEL_IS_APPLET (applet));

	screen	= gtk_window_get_screen (GTK_WINDOW (applet->priv->plug));
	root	= gdk_screen_get_root_window (screen);
	display = gdk_screen_get_display (screen);

	xdisplay = GDK_DISPLAY_XDISPLAY (display);
	xroot	 = GDK_WINDOW_XID (root);

	mate_panel_applet_init_atoms (xdisplay);

	dock_xwindow = mate_panel_applet_find_toplevel_dock_window (applet, xdisplay);
	if (dock_xwindow == None)
		return;

	xev.xclient.type	 = ClientMessage;
	xev.xclient.serial	 = 0;
	xev.xclient.send_event	 = True;
	xev.xclient.window	 = dock_xwindow;
	xev.xclient.message_type = _net_active_window;
	xev.xclient.format	 = 32;
	xev.xclient.data.l[0]	 = 1; /* requestor type; we're an app, I guess */
	xev.xclient.data.l[1]	 = timestamp;
	xev.xclient.data.l[2]	 = None; /* "currently active window", supposedly */
	xev.xclient.data.l[3]	 = 0;
	xev.xclient.data.l[4]	 = 0;

	XSendEvent (xdisplay,
		    xroot, False,
		    SubstructureRedirectMask | SubstructureNotifyMask,
		    &xev);
#endif
}

static GtkAction *
mate_panel_applet_menu_get_action (MatePanelApplet *applet,
			      const gchar *action)
{
	return gtk_action_group_get_action (applet->priv->panel_action_group, action);
}

static void
mate_panel_applet_menu_update_actions (MatePanelApplet *applet)
{
	gboolean locked = applet->priv->locked;
	gboolean locked_down = applet->priv->locked_down;

	g_object_set (mate_panel_applet_menu_get_action (applet, "Lock"),
		      "visible", !locked_down, NULL);
	g_object_set (mate_panel_applet_menu_get_action (applet, "Move"),
		      "sensitive", !locked,
		      "visible", !locked_down,
		      NULL);
	g_object_set (mate_panel_applet_menu_get_action (applet, "Remove"),
		      "sensitive", !locked,
		      "visible", !locked_down,
		      NULL);
}

static void
mate_panel_applet_menu_cmd_remove (GtkAction   *action,
			      MatePanelApplet *applet)
{
	GError *error = NULL;

	if (!applet->priv->connection)
		return;

	g_dbus_connection_emit_signal (applet->priv->connection,
				       NULL,
				       applet->priv->object_path,
				       MATE_PANEL_APPLET_INTERFACE,
				       "RemoveFromPanel",
				       NULL, &error);
	if (error) {
		g_printerr ("Failed to send signal RemoveFromPanel: %s\n",
			    error->message);
		g_error_free (error);
	}
}

static void
mate_panel_applet_menu_cmd_move (GtkAction   *action,
			    MatePanelApplet *applet)
{
	GError *error = NULL;

	if (!applet->priv->connection)
		return;

	g_dbus_connection_emit_signal (applet->priv->connection,
				       NULL,
				       applet->priv->object_path,
				       MATE_PANEL_APPLET_INTERFACE,
				       "Move",
				       NULL, &error);
	if (error) {
		g_printerr ("Failed to send signal RemoveFromPanel: %s\n",
			    error->message);
		g_error_free (error);
	}
}

static void
mate_panel_applet_menu_cmd_lock (GtkAction   *action,
			    MatePanelApplet *applet)
{
	gboolean locked;

	locked = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	mate_panel_applet_set_locked (applet, locked);
}

void
mate_panel_applet_setup_menu (MatePanelApplet    *applet,
			 const gchar    *xml,
			 GtkActionGroup *applet_action_group)
{
	gchar  *new_xml;
	GError *error = NULL;

	g_return_if_fail (PANEL_IS_APPLET (applet));
	g_return_if_fail (xml != NULL);

	if (applet->priv->applet_action_group)
		return;

	applet->priv->applet_action_group = g_object_ref (applet_action_group);
	gtk_ui_manager_insert_action_group (applet->priv->ui_manager,
					    applet_action_group, 0);

	new_xml = g_strdup_printf ("<ui><popup name=\"MatePanelAppletPopup\" action=\"AppletItems\">"
				   "<placeholder name=\"AppletItems\">%s\n</placeholder>\n"
				   "</popup></ui>\n", xml);
	gtk_ui_manager_add_ui_from_string (applet->priv->ui_manager, new_xml, -1, &error);
	g_free (new_xml);
	gtk_ui_manager_ensure_update (applet->priv->ui_manager);
	if (error) {
		g_warning ("Error merging menus: %s\n", error->message);
		g_error_free (error);
	}
}

void
mate_panel_applet_setup_menu_from_file (MatePanelApplet    *applet,
				   const gchar    *filename,
				   GtkActionGroup *applet_action_group)
{
	gchar  *xml = NULL;
	GError *error = NULL;

	if (g_file_get_contents (filename, &xml, NULL, &error)) {
		mate_panel_applet_setup_menu (applet, xml, applet_action_group);
	} else {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	g_free (xml);
}

/**
 * mate_panel_applet_setup_menu_from_resource:
 * @applet: a #MatePanelApplet.
 * @resource_path: a resource path
 * @action_group: a #GtkActionGroup.
 *
 * Sets up the context menu of @applet. @filename is a resource path to a menu
 * XML file, containing a #GtkUIManager UI definition that describes how to
 * display the menu items. @action_group contains the various #GtkAction that
 * are referenced in @xml.
 *
 * See also the <link linkend="getting-started.context-menu">Context
 * Menu</link> section.
 *
 * Since: 1.20.1
 **/
void
mate_panel_applet_setup_menu_from_resource (MatePanelApplet    *applet,
				       const gchar    *resource_path,
				       GtkActionGroup *action_group)
{
	GBytes *bytes;
	GError *error = NULL;

	bytes = g_resources_lookup_data (resource_path,
					 G_RESOURCE_LOOKUP_FLAGS_NONE,
					 &error);

	if (bytes) {
		mate_panel_applet_setup_menu (applet,
					 g_bytes_get_data (bytes, NULL),
					 action_group);
	} else {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	g_bytes_unref (bytes);
}

static void
mate_panel_applet_finalize (GObject *object)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (object);

	if (applet->priv->connection) {
		if (applet->priv->object_id)
			g_dbus_connection_unregister_object (applet->priv->connection,
							     applet->priv->object_id);
		applet->priv->object_id = 0;
		g_object_unref (applet->priv->connection);
		applet->priv->connection = NULL;
	}

	if (applet->priv->object_path) {
		g_free (applet->priv->object_path);
		applet->priv->object_path = NULL;
	}

	mate_panel_applet_set_preferences_path (applet, NULL);

	if (applet->priv->applet_action_group) {
		g_object_unref (applet->priv->applet_action_group);
		applet->priv->applet_action_group = NULL;
	}

	if (applet->priv->panel_action_group) {
		g_object_unref (applet->priv->panel_action_group);
		applet->priv->panel_action_group = NULL;
	}

	if (applet->priv->ui_manager) {
		g_object_unref (applet->priv->ui_manager);
		applet->priv->ui_manager = NULL;
	}

	g_free (applet->priv->size_hints);
	g_free (applet->priv->prefs_path);
	g_free (applet->priv->background);
	g_free (applet->priv->id);

	/* closure is owned by the factory */
	applet->priv->closure = NULL;

	G_OBJECT_CLASS (mate_panel_applet_parent_class)->finalize (object);
}

static gboolean
container_has_focusable_child (GtkContainer *container)
{
	GtkWidget *child;
	GList *list;
	GList *t;
	gboolean retval = FALSE;

	list = gtk_container_get_children (container);

	for (t = list; t; t = t->next) {
		child = GTK_WIDGET (t->data);
		if (gtk_widget_get_can_focus (child)) {
			retval = TRUE;
			break;
		} else if (GTK_IS_CONTAINER (child)) {
			retval = container_has_focusable_child (GTK_CONTAINER (child));
			if (retval)
				break;
		}
	}
	g_list_free (list);
	return retval;
}

static void
mate_panel_applet_menu_popup (MatePanelApplet *applet,
                              GdkEvent    *event)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (applet->priv->ui_manager,
					  "/MatePanelAppletPopup");

/* Set up theme and transparency support */
	GtkWidget *toplevel = gtk_widget_get_toplevel (menu);
/* Fix any failures of compiz/other wm's to communicate with gtk for transparency */
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(toplevel));
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	gtk_widget_set_visual(GTK_WIDGET(toplevel), visual);
/* Set menu and it's toplevel window to follow panel theme */
	GtkStyleContext *context;
	context = gtk_widget_get_style_context (GTK_WIDGET(toplevel));
	gtk_style_context_add_class(context,"gnome-panel-menu-bar");
	gtk_style_context_add_class(context,"mate-panel-menu-bar");
	GdkGravity widget_anchor = GDK_GRAVITY_NORTH_WEST;
	GdkGravity menu_anchor = GDK_GRAVITY_NORTH_WEST;
	switch (applet->priv->orient) {
	case MATE_PANEL_APPLET_ORIENT_UP:
		menu_anchor = GDK_GRAVITY_SOUTH_WEST;
		break;
	case MATE_PANEL_APPLET_ORIENT_DOWN:
		widget_anchor = GDK_GRAVITY_SOUTH_WEST;
		break;
	case MATE_PANEL_APPLET_ORIENT_LEFT:
		menu_anchor = GDK_GRAVITY_NORTH_EAST;
		break;
	case MATE_PANEL_APPLET_ORIENT_RIGHT:
		widget_anchor = GDK_GRAVITY_NORTH_EAST;
		break;
	}
	gtk_menu_popup_at_widget (GTK_MENU (menu),
	                          GTK_WIDGET (applet),
	                          widget_anchor,
	                          menu_anchor,
	                          event);
}

static gboolean
mate_panel_applet_can_focus (GtkWidget *widget)
{
	/*
	 * A MatePanelApplet widget can focus if it has a tooltip or it does
	 * not have any focusable children.
	 */
	if (gtk_widget_get_has_tooltip (widget))
		return TRUE;

	if (!PANEL_IS_APPLET (widget))
		return FALSE;

	return !container_has_focusable_child (GTK_CONTAINER (widget));
}

/* Taken from libmatecomponentui/matecomponent/matecomponent-plug.c */
static gboolean
mate_panel_applet_button_event (MatePanelApplet      *applet,
			   GdkEventButton *event)
{
#ifdef HAVE_X11
	GtkWidget *widget;
	GdkWindow *window;
	GdkWindow *socket_window;
	XEvent     xevent;
	GdkDisplay *display;

	if (!applet->priv->out_of_process)
		return FALSE;

	widget = applet->priv->plug;

	if (!gtk_widget_is_toplevel (widget))
		return FALSE;

	window = gtk_widget_get_window (widget);
	socket_window = gtk_plug_get_socket_window (GTK_PLUG (widget));

	display = gdk_display_get_default ();

	if (!GDK_IS_X11_DISPLAY (display))
		return FALSE;

	if (event->type == GDK_BUTTON_PRESS) {
		GdkSeat *seat;

		xevent.xbutton.type = ButtonPress;

		seat = gdk_display_get_default_seat (display);

		/* X does an automatic pointer grab on button press
		 * if we have both button press and release events
		 * selected.
		 * We don't want to hog the pointer on our parent.
		 */
		gdk_seat_ungrab (seat);
	} else {
		xevent.xbutton.type = ButtonRelease;
	}

	xevent.xbutton.display     = GDK_WINDOW_XDISPLAY (window);
	xevent.xbutton.window      = GDK_WINDOW_XID (socket_window);
	xevent.xbutton.root        = GDK_WINDOW_XID (gdk_screen_get_root_window
							 (gdk_window_get_screen (window)));
	/*
	 * FIXME: the following might cause
	 *        big problems for non-GTK apps
	 */
	xevent.xbutton.x           = 0;
	xevent.xbutton.y           = 0;
	xevent.xbutton.x_root      = 0;
	xevent.xbutton.y_root      = 0;
	xevent.xbutton.state       = event->state;
	xevent.xbutton.button      = event->button;
	xevent.xbutton.same_screen = TRUE; /* FIXME ? */

	gdk_x11_display_error_trap_push (display);

	XSendEvent (GDK_WINDOW_XDISPLAY (window),
		    GDK_WINDOW_XID (socket_window),
		    False, NoEventMask, &xevent);

	gdk_display_flush (display);
	gdk_x11_display_error_trap_pop_ignored (display);

	return TRUE;
#else
	return FALSE;
#endif
}

static gboolean
mate_panel_applet_button_press (GtkWidget      *widget,
			   GdkEventButton *event)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (widget);

	if (!container_has_focusable_child (GTK_CONTAINER (applet))) {
		if (!gtk_widget_has_focus (widget)) {
			gtk_widget_set_can_focus (widget, TRUE);
			gtk_widget_grab_focus (widget);
		}
	}

	if (event->button == 3) {
		mate_panel_applet_menu_popup (applet, (GdkEvent *) event);

		return TRUE;
	}

	return mate_panel_applet_button_event (applet, event);
}

static gboolean
mate_panel_applet_button_release (GtkWidget      *widget,
			     GdkEventButton *event)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (widget);

	return mate_panel_applet_button_event (applet, event);
}

/*Open the applet context menu only on Menu key
 *Do not open it on Return or some applets won't work
 */
static gboolean
mate_panel_applet_key_press_event (GtkWidget   *widget,
			      GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Menu) {
        mate_panel_applet_menu_popup (MATE_PANEL_APPLET (widget), (GdkEvent *) event);
        return TRUE;
    }
    else
        return FALSE;
}

static void
mate_panel_applet_get_preferred_width (GtkWidget *widget,
				       int       *minimum_width,
				       int       *natural_width)
{
	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->get_preferred_width (widget,
										minimum_width,
										natural_width);

#if !GTK_CHECK_VERSION (3, 23, 0)
	MatePanelApplet *applet = MATE_PANEL_APPLET (widget);
	if (applet->priv->out_of_process) {
		/* Out-of-process applets end up scaled up doubly with GTK 3.22.
		 * For these builds divide by the scale factor to ensure
		 * they are back at their own intended size.
		 */
		gint scale;
		scale = gtk_widget_get_scale_factor (widget);
		*minimum_width /= scale;
		*natural_width /= scale;
	}
#endif
}

static void
mate_panel_applet_get_preferred_height (GtkWidget *widget,
					int       *minimum_height,
					int       *natural_height)
{
	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->get_preferred_height (widget,
										minimum_height,
										natural_height);

#if !GTK_CHECK_VERSION (3, 23, 0)
	MatePanelApplet *applet = MATE_PANEL_APPLET (widget);
	if (applet->priv->out_of_process) {
		gint scale;
		/* Out-of-process applets end up scaled up doubly with GTK 3.22.
		 * For these builds divide by the scale factor to ensure
		 * they are back at their own intended size.
		 */
		scale = gtk_widget_get_scale_factor (widget);
		*minimum_height /= scale;
		*natural_height /= scale;
	}
#endif
}

static GtkSizeRequestMode
mate_panel_applet_get_request_mode (GtkWidget *widget)
{
	/*Do not use GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH
	 *or GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT
	 *to avoid problems with in-process applets
	 *when the panel is not expanded
	 *See https://github.com/mate-desktop/mate-panel/issues/797
	 *and https://github.com/mate-desktop/mate-panel/issues/799
	 *Out of process applets already use GTK_SIZE_REQUEST_CONSTANT_SIZE
	 */
	return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
mate_panel_applet_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
	GtkAllocation  child_allocation;
	GtkBin        *bin;
	GtkWidget     *child;
	int            border_width;
	MatePanelApplet   *applet;

	if (!mate_panel_applet_can_focus (widget)) {
		GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->size_allocate (widget, allocation);
	} else {

		border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

		gtk_widget_set_allocation (widget, allocation);
		bin = GTK_BIN (widget);

		child_allocation.x = 0;
		child_allocation.y = 0;

		child_allocation.width  = MAX (allocation->width  - border_width * 2, 0);
		child_allocation.height = MAX (allocation->height - border_width * 2, 0);

		if (gtk_widget_get_realized (widget))
			gdk_window_move_resize (gtk_widget_get_window (widget),
						allocation->x + border_width,
						allocation->y + border_width,
						child_allocation.width,
						child_allocation.height);

		child = gtk_bin_get_child (bin);
		if (child)
			gtk_widget_size_allocate (child, &child_allocation);
	}

	applet = MATE_PANEL_APPLET (widget);

	if (applet->priv->previous_height != allocation->height ||
	    applet->priv->previous_width  != allocation->width) {
		applet->priv->previous_height = allocation->height;
		applet->priv->previous_width = allocation->width;

		mate_panel_applet_handle_background (applet);
	}
}

static gboolean mate_panel_applet_draw(GtkWidget* widget, cairo_t* cr)
{
	GtkStyleContext *context;
	int border_width;
	gdouble x, y, width, height;

	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->draw(widget, cr);

        if (!gtk_widget_has_focus (widget))
		return FALSE;

	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);

	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	x = 0;
	y = 0;

	width -= 2 * border_width;
	height -= 2 * border_width;

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_save (context);

	cairo_save (cr);
	gtk_render_focus (context, cr, x, y, width, height);
	cairo_restore (cr);

	gtk_style_context_restore (context);

	return FALSE;
}

static gboolean
mate_panel_applet_focus (GtkWidget        *widget,
		    GtkDirectionType  dir)
{
	gboolean ret;
	GtkWidget *previous_focus_child;
	MatePanelApplet *applet;

	g_return_val_if_fail (PANEL_IS_APPLET (widget), FALSE);

	applet = MATE_PANEL_APPLET (widget);
	if (applet->priv->moving_focus_out) {
		/*
		 * Applet will retain focus if there is nothing else on the
		 * panel to get focus
		 */
		applet->priv->moving_focus_out = FALSE;
		return FALSE;
	}

	previous_focus_child = gtk_container_get_focus_child (GTK_CONTAINER (widget));
	if (!previous_focus_child && !gtk_widget_has_focus (widget)) {
		if (gtk_widget_get_has_tooltip (widget)) {
			gtk_widget_set_can_focus (widget, TRUE);
			gtk_widget_grab_focus (widget);
			gtk_widget_set_can_focus (widget, FALSE);
			return TRUE;
		}
	}
	ret = GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->focus (widget, dir);

	if (!ret && !previous_focus_child) {
		if (!gtk_widget_has_focus (widget))  {
			/*
			 * Applet does not have a widget which can focus so set
			 * the focus on the applet unless it already had focus
			 * because it had a tooltip.
			 */
			gtk_widget_set_can_focus (widget, TRUE);
			gtk_widget_grab_focus (widget);
			gtk_widget_set_can_focus (widget, FALSE);
			ret = TRUE;
		}
	}

	return ret;
}

static gboolean
mate_panel_applet_parse_color (const gchar *color_str,
			       GdkRGBA     *color)
{
	g_assert (color_str && color);

	return gdk_rgba_parse (color, color_str);
}

#ifdef HAVE_X11
static gboolean
mate_panel_applet_parse_pixmap_str (const char *str,
			       Window          *xid,
			       int             *x,
			       int             *y)
{
	char **elements;
	char  *tmp;

	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (xid != NULL, FALSE);
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);

	elements = g_strsplit (str, ",", -1);

	if (!elements)
		return FALSE;

	if (!elements [0] || !*elements [0] ||
	    !elements [1] || !*elements [1] ||
	    !elements [2] || !*elements [2])
		goto ERROR_AND_FREE;

	*xid = strtol (elements [0], &tmp, 10);
	if (tmp == elements [0])
		goto ERROR_AND_FREE;

	*x   = strtol (elements [1], &tmp, 10);
	if (tmp == elements [1])
		goto ERROR_AND_FREE;

	*y   = strtol (elements [2], &tmp, 10);
	if (tmp == elements [2])
		goto ERROR_AND_FREE;

	g_strfreev (elements);
	return TRUE;

ERROR_AND_FREE:
	g_strfreev (elements);
	return FALSE;
}

static cairo_surface_t *
mate_panel_applet_create_foreign_surface_for_display (GdkDisplay *display,
                                                      GdkVisual  *visual,
                                                      Window      xid)
{
	Status result = 0;
	Window window;
	gint x, y;
	guint width, height, border, depth;

	gdk_x11_display_error_trap_push (display);
	result = XGetGeometry (GDK_DISPLAY_XDISPLAY (display), xid, &window,
	                       &x, &y, &width, &height, &border, &depth);
	gdk_x11_display_error_trap_pop_ignored (display);

	if (result == 0)
		return NULL;

	return cairo_xlib_surface_create (GDK_DISPLAY_XDISPLAY (display),
	                                  xid, gdk_x11_visual_get_xvisual (visual),
	                                  width, height);
}

static cairo_pattern_t *
mate_panel_applet_get_pattern_from_pixmap (MatePanelApplet *applet,
			 Window           xid,
			 int              x,
			 int              y)
{
	cairo_surface_t *background;
	cairo_surface_t *surface;
	GdkWindow       *window;
	int              width;
	int              height;
	GdkDisplay      *display;
	cairo_t         *cr;
	cairo_pattern_t *pattern;

	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	if (!gtk_widget_get_realized (GTK_WIDGET (applet)))
		return NULL;

	window = gtk_widget_get_window (GTK_WIDGET (applet));
	display = gdk_window_get_display (window);

	background = mate_panel_applet_create_foreign_surface_for_display (display,
									   gdk_window_get_visual (window),
									   xid);

	/* background can be NULL if the user changes the background very fast.
	 * We'll get the next update, so it's not a big deal. */
	if (!background || cairo_surface_status (background) != CAIRO_STATUS_SUCCESS) {
		if (background)
			cairo_surface_destroy (background);
		return NULL;
	}

	width = gdk_window_get_width(window);
	height = gdk_window_get_height(window);
	surface = gdk_window_create_similar_surface (window,
	                            CAIRO_CONTENT_COLOR_ALPHA,
	                            width,
	                            height);
	gdk_x11_display_error_trap_push (display);
	cr = cairo_create (surface);
	cairo_set_source_surface (cr, background, -x, -y);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);
	gdk_x11_display_error_trap_pop_ignored (display);

	cairo_surface_destroy (background);
	pattern = NULL;

	if (cairo_status (cr) == CAIRO_STATUS_SUCCESS) {
		pattern = cairo_pattern_create_for_surface (surface);
	}

	cairo_destroy (cr);
	cairo_surface_destroy (surface);

	return pattern;
}
#endif

static MatePanelAppletBackgroundType
mate_panel_applet_handle_background_string (MatePanelApplet  *applet,
					    GdkRGBA          *color,
					    cairo_pattern_t **pattern)
{
	MatePanelAppletBackgroundType   retval;
	char                          **elements;

	retval = PANEL_NO_BACKGROUND;

	if (!gtk_widget_get_realized (GTK_WIDGET (applet)) || !applet->priv->background)
		return retval;

	elements = g_strsplit (applet->priv->background, ":", -1);

	if (elements [0] && !strcmp (elements [0], "none" )) {
		retval = PANEL_NO_BACKGROUND;

	} else if (elements [0] && !strcmp (elements [0], "color")) {
		g_return_val_if_fail (color != NULL, PANEL_NO_BACKGROUND);

		if (!elements [1] || !mate_panel_applet_parse_color (elements [1], color)) {

			g_warning ("Incomplete '%s' background type received", elements [0]);
			g_strfreev (elements);
			return PANEL_NO_BACKGROUND;
		}

		retval = PANEL_COLOR_BACKGROUND;

	} else if (elements [0] && !strcmp (elements [0], "pixmap")) {
#ifdef HAVE_X11
		if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
			Window pixmap_id;
			int             x, y;

			g_return_val_if_fail (pattern != NULL, PANEL_NO_BACKGROUND);

			if (!mate_panel_applet_parse_pixmap_str (elements [1], &pixmap_id, &x, &y)) {
				g_warning ("Incomplete '%s' background type received: %s",
					elements [0], elements [1]);

				g_strfreev (elements);
				return PANEL_NO_BACKGROUND;
			}

			*pattern = mate_panel_applet_get_pattern_from_pixmap (applet, pixmap_id, x, y);
			if (!*pattern) {
				g_warning ("Failed to get pattern %s", elements [1]);
				g_strfreev (elements);
				return PANEL_NO_BACKGROUND;
			}

			retval = PANEL_PIXMAP_BACKGROUND;
		} else
#endif
		{ // not using X11
			g_warning("Received pixmap background type, which is only supported on X11");
		}
	} else
		g_warning ("Unknown background type received");

	g_strfreev (elements);

	return retval;
}

MatePanelAppletBackgroundType
mate_panel_applet_get_background (MatePanelApplet  *applet,
				  GdkRGBA          *color,
				  cairo_pattern_t **pattern)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), PANEL_NO_BACKGROUND);

	/* initial sanity */
	if (pattern != NULL)
		*pattern = NULL;
	if (color != NULL)
		memset (color, 0, sizeof (GdkRGBA));

	return mate_panel_applet_handle_background_string (applet, color, pattern);
}

static void
mate_panel_applet_set_background_string (MatePanelApplet *applet,
				    const gchar *background)
{
	if (applet->priv->background == background)
		return;

	if (g_strcmp0 (applet->priv->background, background) == 0)
		return;

	if (applet->priv->background)
		g_free (applet->priv->background);
	applet->priv->background = background ? g_strdup (background) : NULL;
	mate_panel_applet_handle_background (applet);

	g_object_notify (G_OBJECT (applet), "background");
}

static void
mate_panel_applet_handle_background (MatePanelApplet *applet)
{
	MatePanelAppletBackgroundType  type;

	GdkRGBA                    color;
	cairo_pattern_t           *pattern;

	type = mate_panel_applet_get_background (applet, &color, &pattern);

	switch (type) {
	case PANEL_NO_BACKGROUND:
		g_signal_emit (G_OBJECT (applet),
			       mate_panel_applet_signals [CHANGE_BACKGROUND],
			       0, PANEL_NO_BACKGROUND, NULL, NULL);
		break;
	case PANEL_COLOR_BACKGROUND:
		g_signal_emit (G_OBJECT (applet),
			       mate_panel_applet_signals [CHANGE_BACKGROUND],
			       0, PANEL_COLOR_BACKGROUND, &color, NULL);
		break;
	case PANEL_PIXMAP_BACKGROUND:
		g_signal_emit (G_OBJECT (applet),
			       mate_panel_applet_signals [CHANGE_BACKGROUND],

			       0, PANEL_PIXMAP_BACKGROUND, NULL, pattern);


		cairo_pattern_destroy (pattern);

		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
mate_panel_applet_realize (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->realize (widget);

	if (MATE_PANEL_APPLET (widget)->priv->background)
		mate_panel_applet_handle_background (MATE_PANEL_APPLET (widget));
}

static void
mate_panel_applet_move_focus_out_of_applet (MatePanelApplet      *applet,
				       GtkDirectionType  dir)
{
	GtkWidget *toplevel;

	applet->priv->moving_focus_out = TRUE;
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (applet));
	g_return_if_fail (toplevel);

	gtk_widget_child_focus (toplevel, dir);
	applet->priv->moving_focus_out = FALSE;
}

static void
mate_panel_applet_change_background(MatePanelApplet *applet,
				    MatePanelAppletBackgroundType type,
				    GdkRGBA* color,
				    cairo_pattern_t *pattern)
{
	GtkStyleContext* context;
	GdkWindow* window;

	if (applet->priv->out_of_process)
		window = gtk_widget_get_window (GTK_WIDGET (applet->priv->plug));
	else
		window = gtk_widget_get_window (GTK_WIDGET (applet));

	gtk_widget_set_app_paintable(GTK_WIDGET(applet),TRUE);

	if (applet->priv->out_of_process)
		_mate_panel_applet_apply_css(GTK_WIDGET(applet->priv->plug),type);

	switch (type) {
		case PANEL_NO_BACKGROUND:
			if (applet->priv->out_of_process){
				pattern = cairo_pattern_create_rgba (0,0,0,0);     /* Using NULL here breaks transparent */
				gdk_window_set_background_pattern(window,pattern); /* backgrounds set by GTK theme */
			}
			break;
		case PANEL_COLOR_BACKGROUND:
			if (applet->priv->out_of_process){
				gdk_window_set_background_rgba(window,color);
				gtk_widget_queue_draw (applet->priv->plug); /*change the bg right away always */
			}
			break;
		case PANEL_PIXMAP_BACKGROUND:
			if (applet->priv->out_of_process){
				gdk_window_set_background_pattern(window,pattern);
				gtk_widget_queue_draw (applet->priv->plug); /*change the bg right away always */
			}
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	if (applet->priv->out_of_process){
		context = gtk_widget_get_style_context (GTK_WIDGET(applet->priv->plug));
		if (applet->priv->orient == MATE_PANEL_APPLET_ORIENT_UP ||
			applet->priv->orient == MATE_PANEL_APPLET_ORIENT_DOWN){
			gtk_style_context_add_class(context,"horizontal");
		}
		else {
			gtk_style_context_add_class(context,"vertical");
		}
	}
}

static void
mate_panel_applet_get_property (GObject    *object,
			   guint       prop_id,
			   GValue     *value,
			   GParamSpec *pspec)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (object);

	switch (prop_id) {
	case PROP_OUT_OF_PROCESS:
		g_value_set_boolean (value, applet->priv->out_of_process);
		break;
	case PROP_ID:
		g_value_set_string (value, applet->priv->id);
		break;
	case PROP_CLOSURE:
		g_value_set_pointer (value, applet->priv->closure);
		break;
	case PROP_CONNECTION:
		g_value_set_object (value, applet->priv->connection);
		break;
	case PROP_PREFS_PATH:
		g_value_set_string (value, applet->priv->prefs_path);
		break;
	case PROP_ORIENT:
		g_value_set_uint (value, applet->priv->orient);
		break;
	case PROP_SIZE:
		g_value_set_uint (value, applet->priv->size);
		break;
	case PROP_BACKGROUND:
		g_value_set_string (value, applet->priv->background);
		break;
	case PROP_FLAGS:
		g_value_set_uint (value, applet->priv->flags);
		break;
	case PROP_SIZE_HINTS: {
		GVariant **children;
		GVariant  *variant;
		gint       i;

		children = g_new (GVariant *, applet->priv->size_hints_len);
		for (i = 0; i < applet->priv->size_hints_len; i++)
			children[i] = g_variant_new_int32 (applet->priv->size_hints[i]);
		variant = g_variant_new_array (G_VARIANT_TYPE_INT32,
					       children, applet->priv->size_hints_len);
		g_free (children);
		g_value_set_pointer (value, variant);
	}
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, applet->priv->locked);
		break;
	case PROP_LOCKED_DOWN:
		g_value_set_boolean (value, applet->priv->locked_down);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
mate_panel_applet_set_property (GObject      *object,
			   guint         prop_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (object);

	switch (prop_id) {
	case PROP_OUT_OF_PROCESS:
		applet->priv->out_of_process = g_value_get_boolean (value);
		break;
	case PROP_ID:
		applet->priv->id = g_value_dup_string (value);
		break;
	case PROP_CLOSURE:
		applet->priv->closure = g_value_get_pointer (value);
		g_closure_set_marshal (applet->priv->closure,
				       mate_panel_applet_marshal_BOOLEAN__STRING);
		break;
	case PROP_CONNECTION:
		applet->priv->connection = g_value_dup_object (value);
		break;
	case PROP_PREFS_PATH:
		mate_panel_applet_set_preferences_path (applet, g_value_get_string (value));
		break;
	case PROP_ORIENT:
		mate_panel_applet_set_orient (applet, g_value_get_uint (value));
		break;
	case PROP_SIZE:
		mate_panel_applet_set_size (applet, g_value_get_uint (value));
		break;
	case PROP_BACKGROUND:
		mate_panel_applet_set_background_string (applet, g_value_get_string (value));
		break;
	case PROP_FLAGS:
		mate_panel_applet_set_flags (applet, g_value_get_uint (value));
		break;
	case PROP_SIZE_HINTS: {
		const int *size_hints;
		gsize      n_elements;

		size_hints = g_variant_get_fixed_array (g_value_get_pointer (value),
							&n_elements, sizeof (gint32));
		mate_panel_applet_set_size_hints (applet, size_hints, n_elements, 0);
	}
		break;
	case PROP_LOCKED:
		mate_panel_applet_set_locked (applet, g_value_get_boolean (value));
		break;
	case PROP_LOCKED_DOWN:
		mate_panel_applet_set_locked_down (applet, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
add_tab_bindings (GtkBindingSet   *binding_set,
		  GdkModifierType  modifiers,
		  GtkDirectionType direction)
{
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
				      "move_focus_out_of_applet", 1,
				      GTK_TYPE_DIRECTION_TYPE, direction);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
				      "move_focus_out_of_applet", 1,
				      GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
mate_panel_applet_setup (MatePanelApplet *applet)
{
	GValue   value = {0, };
	GArray  *params;
	gint     i;
	gboolean ret;

	g_assert (applet->priv->id != NULL &&
		  applet->priv->closure != NULL);

	params = g_array_sized_new (FALSE, TRUE, sizeof (GValue), 2);
	value.g_type = 0;
	g_value_init (&value, G_TYPE_OBJECT);
	g_value_set_object (&value, G_OBJECT (applet));
	g_array_append_val (params, value);

	value.g_type = 0;
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, applet->priv->id);
	g_array_append_val (params, value);

	value.g_type = 0;
	g_value_init (&value, G_TYPE_BOOLEAN);

	g_closure_invoke (applet->priv->closure,
			  &value, params->len,
			  (GValue *) params->data,
			  NULL);

	for (i = 0; i < params->len; i++)
		g_value_unset (&g_array_index (params, GValue, i));
	g_array_free (params, TRUE);

	ret = g_value_get_boolean (&value);
	g_value_unset (&value);

	if (!ret) { /* FIXME */
		g_warning ("need to free the control here");

		return;
	}
}

void _mate_panel_applet_apply_css(GtkWidget* widget, MatePanelAppletBackgroundType type)
{
	GtkStyleContext* context;

	context = gtk_widget_get_style_context (widget);

	switch (type) {
	case PANEL_NO_BACKGROUND:
		gtk_style_context_remove_class (context, "mate-custom-panel-background");
		break;
	case PANEL_COLOR_BACKGROUND:
	case PANEL_PIXMAP_BACKGROUND:
		gtk_style_context_add_class (context, "mate-custom-panel-background");
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

#ifdef HAVE_X11
static void _mate_panel_applet_prepare_css (GtkStyleContext *context)
{
	GtkCssProvider  *provider;

	g_return_if_fail (GDK_IS_X11_DISPLAY (gdk_display_get_default ()));
	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider,
					 "#PanelPlug {\n"
					 " background-repeat: no-repeat;\n" /*disable in gtk theme features */
					 " background-size: cover; "        /*that don't work on panel-toplevel */
					 " }\n"
					 ".mate-custom-panel-background{\n" /*prepare CSS for user set theme */
					 " background-color: rgba (0, 0, 0, 0);\n"
					 " background-image: none;\n"
					 "}",
					 -1, NULL);

	gtk_style_context_add_provider (context,
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}
#endif // HAVE_X11

static void
mate_panel_applet_init (MatePanelApplet *applet)
{
	applet->priv = mate_panel_applet_get_instance_private (applet);

	applet->priv->flags  = MATE_PANEL_APPLET_FLAGS_NONE;
	applet->priv->orient = MATE_PANEL_APPLET_ORIENT_UP;
	applet->priv->size   = 24;

	applet->priv->panel_action_group = gtk_action_group_new ("PanelActions");
	gtk_action_group_set_translation_domain (applet->priv->panel_action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (applet->priv->panel_action_group,
				      menu_entries,
				      G_N_ELEMENTS (menu_entries),
				      applet);
	gtk_action_group_add_toggle_actions (applet->priv->panel_action_group,
					     menu_toggle_entries,
					     G_N_ELEMENTS (menu_toggle_entries),
					     applet);

	applet->priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (applet->priv->ui_manager,
					    applet->priv->panel_action_group, 1);
	gtk_ui_manager_add_ui_from_string (applet->priv->ui_manager,
					   panel_menu_ui, -1, NULL);

	gtk_widget_set_events (GTK_WIDGET (applet),
			       GDK_BUTTON_PRESS_MASK |
			       GDK_BUTTON_RELEASE_MASK);
}

static GObject *
mate_panel_applet_constructor (GType                  type,
                          guint                  n_construct_properties,
                          GObjectConstructParam *construct_properties)
{
	GObject     *object;
	MatePanelApplet *applet;

	object = G_OBJECT_CLASS (mate_panel_applet_parent_class)->constructor (type,
	                                                                  n_construct_properties,
	                                                                  construct_properties);
	applet = MATE_PANEL_APPLET (object);

	if (!applet->priv->out_of_process)
		return object;

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		applet->priv->plug = gtk_plug_new (0);

		GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (applet->priv->plug));
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		gtk_widget_set_visual (GTK_WIDGET (applet->priv->plug), visual);
		GtkStyleContext *context;
		context = gtk_widget_get_style_context (GTK_WIDGET(applet->priv->plug));
		gtk_style_context_add_class (context,"gnome-panel-menu-bar");
		gtk_style_context_add_class (context,"mate-panel-menu-bar");
		gtk_widget_set_name (GTK_WIDGET (applet->priv->plug), "PanelPlug");
		_mate_panel_applet_prepare_css (context);

		g_signal_connect_swapped (G_OBJECT (applet->priv->plug), "embedded",
					  G_CALLBACK (mate_panel_applet_setup),
					  applet);

		gtk_container_add (GTK_CONTAINER (applet->priv->plug), GTK_WIDGET (applet));
	} else
#endif
	{ // not using X11
		g_warning ("Requested construction of an out-of-process applet, which is only possible on X11");
	}

	return object;
}

static void
mate_panel_applet_constructed (GObject* object)
{
	MatePanelApplet* applet = MATE_PANEL_APPLET(object);

	/* Rename the class to have compatibility with all GTK2 themes
	 * https://github.com/perberos/Mate-Desktop-Environment/issues/27
	 */
	gtk_widget_set_name(GTK_WIDGET(applet), "PanelApplet");

	mate_panel_applet_register_object (applet);
}

static void
mate_panel_applet_class_init (MatePanelAppletClass *klass)
{
	GObjectClass   *gobject_class = (GObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;
	GtkBindingSet *binding_set;

	gobject_class->get_property = mate_panel_applet_get_property;
	gobject_class->set_property = mate_panel_applet_set_property;
	gobject_class->constructor = mate_panel_applet_constructor;
	gobject_class->constructed = mate_panel_applet_constructed;
	klass->move_focus_out_of_applet = mate_panel_applet_move_focus_out_of_applet;
	klass->change_background = mate_panel_applet_change_background;
	widget_class->button_press_event = mate_panel_applet_button_press;
	widget_class->button_release_event = mate_panel_applet_button_release;
	widget_class->get_request_mode = mate_panel_applet_get_request_mode;
	widget_class->get_preferred_width = mate_panel_applet_get_preferred_width;
	widget_class->get_preferred_height = mate_panel_applet_get_preferred_height;
	widget_class->draw = mate_panel_applet_draw;
	widget_class->size_allocate = mate_panel_applet_size_allocate;
	widget_class->focus = mate_panel_applet_focus;
	widget_class->realize = mate_panel_applet_realize;
	widget_class->key_press_event = mate_panel_applet_key_press_event;


	gobject_class->finalize = mate_panel_applet_finalize;

	g_object_class_install_property (gobject_class,
	                  PROP_OUT_OF_PROCESS,
	                  g_param_spec_boolean ("out-of-process",
	                               "out-of-process",
	                               "out-of-process",
	                                TRUE,
	                                G_PARAM_CONSTRUCT_ONLY |
	                                G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_ID,
					 g_param_spec_string ("id",
							      "Id",
							      "The Applet identifier",
							      NULL,
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_CLOSURE,
					 g_param_spec_pointer ("closure",
							       "GClosure",
							       "The Applet closure",
							       G_PARAM_CONSTRUCT_ONLY |
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_CONNECTION,
					 g_param_spec_object ("connection",
							      "Connection",
							      "The DBus Connection",
							      G_TYPE_DBUS_CONNECTION,
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_PREFS_PATH,
					 g_param_spec_string ("prefs-path",
							      "PrefsPath",
							      "GSettings Preferences Path",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_ORIENT,
					 g_param_spec_uint ("orient",
							    "Orient",
							    "Panel Applet Orientation",
							    MATE_PANEL_APPLET_ORIENT_FIRST,
							    MATE_PANEL_APPLET_ORIENT_LAST,
							    MATE_PANEL_APPLET_ORIENT_UP,
							    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_SIZE,
					 g_param_spec_uint ("size",
							    "Size",
							    "Panel Applet Size",
							    0, G_MAXUINT, 0,
							    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_BACKGROUND,
					 g_param_spec_string ("background",
							      "Background",
							      "Panel Applet Background",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_FLAGS,
					 g_param_spec_uint ("flags",
							    "Flags",
							    "Panel Applet flags",
							    MATE_PANEL_APPLET_FLAGS_NONE,
							    MATE_PANEL_APPLET_FLAGS_ALL,
							    MATE_PANEL_APPLET_FLAGS_NONE,
							    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_SIZE_HINTS,
					 /* FIXME: value_array? */
					 g_param_spec_pointer ("size-hints",
							       "SizeHints",
							       "Panel Applet Size Hints",
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_LOCKED,
					 g_param_spec_boolean ("locked",
							       "Locked",
							       "Whether Panel Applet is locked",
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_LOCKED_DOWN,
					 g_param_spec_boolean ("locked-down",
							       "LockedDown",
							       "Whether Panel Applet is locked down",
							       FALSE,
							       G_PARAM_READWRITE));

	mate_panel_applet_signals [CHANGE_ORIENT] =
                g_signal_new ("change_orient",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MatePanelAppletClass, change_orient),
                              NULL,
			      NULL,
                              mate_panel_applet_marshal_VOID__UINT,
                              G_TYPE_NONE,
			      1,
			      G_TYPE_UINT);

	mate_panel_applet_signals [CHANGE_SIZE] =
                g_signal_new ("change_size",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MatePanelAppletClass, change_size),
                              NULL,
			      NULL,
                              mate_panel_applet_marshal_VOID__INT,
                              G_TYPE_NONE,
			      1,
			      G_TYPE_INT);

	mate_panel_applet_signals [CHANGE_BACKGROUND] =
                g_signal_new ("change_background",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MatePanelAppletClass, change_background),
                              NULL,
			      NULL,
                              mate_panel_applet_marshal_VOID__ENUM_BOXED_OBJECT,
                              G_TYPE_NONE,
			      3,
			      PANEL_TYPE_MATE_PANEL_APPLET_BACKGROUND_TYPE,
			      GDK_TYPE_RGBA,
			      CAIRO_GOBJECT_TYPE_PATTERN);

	mate_panel_applet_signals [MOVE_FOCUS_OUT_OF_APPLET] =
                g_signal_new ("move_focus_out_of_applet",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                              G_STRUCT_OFFSET (MatePanelAppletClass, move_focus_out_of_applet),
                              NULL,
			      NULL,
                              mate_panel_applet_marshal_VOID__ENUM,
                              G_TYPE_NONE,
			      1,
			      GTK_TYPE_DIRECTION_TYPE);

	binding_set = gtk_binding_set_by_class (gobject_class);
	add_tab_bindings (binding_set, 0, GTK_DIR_TAB_FORWARD);
	add_tab_bindings (binding_set, GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
	add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
	add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);

	gtk_widget_class_set_css_name (widget_class, "PanelApplet");
}

GtkWidget* mate_panel_applet_new(void)
{
	MatePanelApplet* applet = g_object_new(PANEL_TYPE_APPLET, NULL);

	return GTK_WIDGET(applet);
}

static GdkEvent *
button_press_event_new (MatePanelApplet *applet,
                        guint        button,
                        guint        time)
{
  GdkDisplay *display;
  GdkSeat *seat;
  GdkDevice *device;
  GdkEvent *event;

  display = gdk_display_get_default ();
  seat = gdk_display_get_default_seat (display);
  device = gdk_seat_get_pointer (seat);

  event = gdk_event_new (GDK_BUTTON_PRESS);

  event->button.time = time;
  event->button.button = button;

  gdk_event_set_device (event, device);

  return event;
}

static void
method_call_cb (GDBusConnection       *connection,
		const gchar           *sender,
		const gchar           *object_path,
		const gchar           *interface_name,
		const gchar           *method_name,
		GVariant              *parameters,
		GDBusMethodInvocation *invocation,
		gpointer               user_data)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (user_data);
	GdkEvent *event;

	if (g_strcmp0 (method_name, "PopupMenu") == 0) {
		guint button;
		guint time;

		g_variant_get (parameters, "(uu)", &button, &time);

		event = button_press_event_new (applet, button, time);
		mate_panel_applet_menu_popup (applet, event);
		gdk_event_free (event);

		g_dbus_method_invocation_return_value (invocation, NULL);
	}
}

static GVariant *
get_property_cb (GDBusConnection *connection,
		 const gchar     *sender,
		 const gchar     *object_path,
		 const gchar     *interface_name,
		 const gchar     *property_name,
		 GError         **error,
		 gpointer         user_data)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (user_data);
	GVariant    *retval = NULL;

	if (g_strcmp0 (property_name, "PrefsPath") == 0) {
		retval = g_variant_new_string (applet->priv->prefs_path ?
					       applet->priv->prefs_path : "");
	} else if (g_strcmp0 (property_name, "Orient") == 0) {
		retval = g_variant_new_uint32 (applet->priv->orient);
	} else if (g_strcmp0 (property_name, "Size") == 0) {
		retval = g_variant_new_uint32 (applet->priv->size);
	} else if (g_strcmp0 (property_name, "Background") == 0) {
		retval = g_variant_new_string (applet->priv->background ?
					       applet->priv->background : "");
	} else if (g_strcmp0 (property_name, "Flags") == 0) {
		retval = g_variant_new_uint32 (applet->priv->flags);
	} else if (g_strcmp0 (property_name, "SizeHints") == 0) {
		GVariant **children;
		gint       i;

		children = g_new (GVariant *, applet->priv->size_hints_len);
		for (i = 0; i < applet->priv->size_hints_len; i++)
			children[i] = g_variant_new_int32 (applet->priv->size_hints[i]);
		retval = g_variant_new_array (G_VARIANT_TYPE_INT32,
					      children, applet->priv->size_hints_len);
		g_free (children);
	} else if (g_strcmp0 (property_name, "Locked") == 0) {
		retval = g_variant_new_boolean (applet->priv->locked);
	} else if (g_strcmp0 (property_name, "LockedDown") == 0) {
		retval = g_variant_new_boolean (applet->priv->locked_down);
	}

	return retval;
}

static gboolean
set_property_cb (GDBusConnection *connection,
		 const gchar     *sender,
		 const gchar     *object_path,
		 const gchar     *interface_name,
		 const gchar     *property_name,
		 GVariant        *value,
		 GError         **error,
		 gpointer         user_data)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (user_data);

	if (g_strcmp0 (property_name, "PrefsPath") == 0) {
		mate_panel_applet_set_preferences_path (applet, g_variant_get_string (value, NULL));
	} else if (g_strcmp0 (property_name, "Orient") == 0) {
		mate_panel_applet_set_orient (applet, g_variant_get_uint32 (value));
	} else if (g_strcmp0 (property_name, "Size") == 0) {
		mate_panel_applet_set_size (applet, g_variant_get_uint32 (value));
	} else if (g_strcmp0 (property_name, "Background") == 0) {
		mate_panel_applet_set_background_string (applet, g_variant_get_string (value, NULL));
	} else if (g_strcmp0 (property_name, "Flags") == 0) {
		mate_panel_applet_set_flags (applet, g_variant_get_uint32 (value));
	} else if (g_strcmp0 (property_name, "SizeHints") == 0) {
		const int *size_hints;
		gsize      n_elements;

		size_hints = g_variant_get_fixed_array (value, &n_elements, sizeof (gint32));
		mate_panel_applet_set_size_hints (applet, size_hints, n_elements, 0);
	} else if (g_strcmp0 (property_name, "Locked") == 0) {
		mate_panel_applet_set_locked (applet, g_variant_get_boolean (value));
	} else if (g_strcmp0 (property_name, "LockedDown") == 0) {
		mate_panel_applet_set_locked_down (applet, g_variant_get_boolean (value));
	}

	return TRUE;
}

static const gchar introspection_xml[] =
	"<node>"
	  "<interface name='org.mate.panel.applet.Applet'>"
	    "<method name='PopupMenu'>"
	      "<arg name='button' type='u' direction='in'/>"
	      "<arg name='time' type='u' direction='in'/>"
	    "</method>"
	    "<property name='PrefsPath' type='s' access='readwrite'/>"
	    "<property name='Orient' type='u' access='readwrite' />"
	    "<property name='Size' type='u' access='readwrite'/>"
	    "<property name='Background' type='s' access='readwrite'/>"
	    "<property name='Flags' type='u' access='readwrite'/>"
	    "<property name='SizeHints' type='ai' access='readwrite'/>"
	    "<property name='Locked' type='b' access='readwrite'/>"
	    "<property name='LockedDown' type='b' access='readwrite'/>"
	    "<signal name='Move' />"
	    "<signal name='RemoveFromPanel' />"
	    "<signal name='Lock' />"
	    "<signal name='Unlock' />"
	  "</interface>"
	"</node>";

static const GDBusInterfaceVTable interface_vtable = {
	method_call_cb,
	get_property_cb,
	set_property_cb
};

static GDBusNodeInfo *introspection_data = NULL;

static void
mate_panel_applet_register_object (MatePanelApplet *applet)
{
	GError     *error = NULL;
	static gint id = 0;

	if (!introspection_data)
		introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

	applet->priv->object_path = g_strdup_printf (MATE_PANEL_APPLET_OBJECT_PATH, applet->priv->id, id++);
	applet->priv->object_id =
		g_dbus_connection_register_object (applet->priv->connection,
						   applet->priv->object_path,
						   introspection_data->interfaces[0],
						   &interface_vtable,
						   applet, NULL,
						   &error);
	if (!applet->priv->object_id) {
		g_printerr ("Failed to register object %s: %s\n", applet->priv->object_path, error->message);
		g_error_free (error);
	}
}

static void mate_panel_applet_factory_main_finalized(gpointer data, GObject* object)
{
	gtk_main_quit();

	if (introspection_data)
	{
		g_dbus_node_info_unref(introspection_data);
		introspection_data = NULL;
	}
}

#ifdef HAVE_X11
static int (*_x_error_func) (Display *, XErrorEvent *);

static int
_x_error_handler (Display *display, XErrorEvent *error)
{
	if (!error->error_code)
		return 0;

	/* If we got a BadDrawable or a BadWindow, we ignore it for now.
	 * FIXME: We need to somehow distinguish real errors from
	 * X-server-induced errors. Keeping a list of windows for which we
	 * will ignore BadDrawables would be a good idea. */
	if (error->error_code == BadDrawable ||
	    error->error_code == BadWindow)
		return 0;

	return _x_error_func (display, error);
}

/*
 * To do graphical embedding in the X window system, MATE Panel
 * uses the classic foreign-window-reparenting trick. The
 * GtkPlug/GtkSocket widgets are used for this purpose. However,
 * serious robustness problems arise if the GtkSocket end of the
 * connection unexpectedly dies. The X server sends out DestroyNotify
 * events for the descendants of the GtkPlug (i.e., your embedded
 * component's windows) in effectively random order. Furthermore, if
 * you happened to be drawing on any of those windows when the
 * GtkSocket was destroyed (a common state of affairs), an X error
 * will kill your application.
 *
 * To solve this latter problem, MATE Panel sets up its own X error
 * handler which ignores certain X errors that might have been
 * caused by such a scenario. Other X errors get passed to gdk_x_error
 * normally.
 */
static void
_mate_panel_applet_setup_x_error_handler (void)
{
	static gboolean error_handler_setup = FALSE;

	if (error_handler_setup)
		return;

	error_handler_setup = TRUE;

	_x_error_func = XSetErrorHandler (_x_error_handler);
}
#endif

static int
_mate_panel_applet_factory_main_internal (const gchar               *factory_id,
				     gboolean                   out_process,
				     GType                      applet_type,
				     MatePanelAppletFactoryCallback callback,
					 gpointer                   user_data)

{
	MatePanelAppletFactory* factory;
	GClosure* closure;

	g_return_val_if_fail(factory_id != NULL, 1);
	g_return_val_if_fail(callback != NULL, 1);
	g_assert(g_type_is_a(applet_type, PANEL_TYPE_APPLET));


#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		/*Use this both in and out of process as the tray applet always uses GtkSocket
		*to handle GtkStatusIcons whether the tray itself is built in or out of process
		*/
		_mate_panel_applet_setup_x_error_handler();
	} else
#endif
	{ // not using X11
		if (out_process) {
			g_warning("Requested out-of-process applet, which is only supported on X11");
			return 1;
		}
	}

	closure = g_cclosure_new(G_CALLBACK(callback), user_data, NULL);
	factory = mate_panel_applet_factory_new(factory_id, out_process,  applet_type, closure);
	g_closure_unref(closure);

	if (mate_panel_applet_factory_register_service(factory))
	{
		if (out_process)
		{
			g_object_weak_ref(G_OBJECT(factory), mate_panel_applet_factory_main_finalized, NULL);
			gtk_main();
		}

		return 0;
	}

	g_object_unref (factory);

	return 1;
}

/**
 * mate_panel_applet_factory_main:
 * @out_process: boolean, dummy to support applets sending it
 * @factory_id: Factory ID.
 * @applet_type: GType of the applet this factory creates.
 * @callback: (scope call): Callback to be called when a new applet is to be created.
 * @data: (closure): Callback data.
 *
 * Returns: 0 on success, 1 if there is an error.
 */
int
mate_panel_applet_factory_main (const gchar               *factory_id,
               gboolean                   out_process, /*Dummy to support applets w issues with this */
			   GType                      applet_type,
			   MatePanelAppletFactoryCallback callback,
			   gpointer                   user_data)
{
	return _mate_panel_applet_factory_main_internal (factory_id, TRUE, applet_type,
						    callback, user_data);
}

/**
 * mate_panel_applet_factory_setup_in_process: (skip)
 * @factory_id: Factory ID.
 * @applet_type: GType of the applet this factory creates.
 * @callback: (scope call): Callback to be called when a new applet is to be created.
 * @data: (closure): Callback data.
 *
 * Returns: 0 on success, 1 if there is an error.
 */
int
mate_panel_applet_factory_setup_in_process (const gchar               *factory_id,
				       GType                      applet_type,
				       MatePanelAppletFactoryCallback callback,
				       gpointer                   user_data)
{
	return _mate_panel_applet_factory_main_internal (factory_id, FALSE, applet_type,
						    callback, user_data);
}

/**
 * mate_panel_applet_set_background_widget:
 * @applet: a #PanelApplet.
 * @widget: a #GtkWidget.
 *
 * Configure #PanelApplet to automatically draw the background of the applet on
 * @widget. It is generally enough to call this function with @applet as
 * @widget.
 *
 * Deprecated: 3.20: Do not use this API. Since 3.20 this function does nothing.
 **/

void
mate_panel_applet_set_background_widget (MatePanelApplet *applet,
				    GtkWidget   *widget)
{
}

guint32
mate_panel_applet_get_xid (MatePanelApplet *applet,
		      GdkScreen   *screen)
{
	// out_of_process should only be true on X11, so an extra runtime Wayland check is not needed
	if (applet->priv->out_of_process == FALSE)
		return 0;

#ifdef HAVE_X11
	gtk_window_set_screen (GTK_WINDOW (applet->priv->plug), screen);
	gtk_widget_show (applet->priv->plug);

	return gtk_plug_get_id (GTK_PLUG (applet->priv->plug));
#else
	return 0;
#endif
}

const gchar *
mate_panel_applet_get_object_path (MatePanelApplet *applet)
{
	return applet->priv->object_path;
}

G_MODULE_EXPORT GtkWidget *
mate_panel_applet_get_applet_widget (const gchar *factory_id,
                                guint        uid)
{
	GtkWidget *widget;

	widget = mate_panel_applet_factory_get_applet_widget (factory_id, uid);
	if (!widget) {
		return NULL;
	}

	mate_panel_applet_setup (MATE_PANEL_APPLET (widget));

	return widget;
}
