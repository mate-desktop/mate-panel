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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <mateconf/mateconf.h>
#include <mateconf/mateconf-client.h>
#include <X11/Xatom.h>

#include "mate-panel-applet.h"
#include "mate-panel-applet-factory.h"
#include "mate-panel-applet-marshal.h"
#include "mate-panel-applet-enums.h"

#define MATE_PANEL_APPLET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PANEL_TYPE_APPLET, MatePanelAppletPrivate))

struct _MatePanelAppletPrivate {
	GtkWidget         *plug;
	GtkWidget         *applet;
	MateConfClient       *client;
	GDBusConnection   *connection;

	char              *id;
	GClosure          *closure;
	char              *object_path;
	guint              object_id;
	char              *prefs_key;

	GtkUIManager      *ui_manager;
	GtkActionGroup    *applet_action_group;
	GtkActionGroup    *panel_action_group;

	MatePanelAppletFlags   flags;
	MatePanelAppletOrient  orient;
	guint              size;
	char              *background;
	GtkWidget         *background_widget;

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
	SAVE_YOURSELF,
	LAST_SIGNAL
};

static guint mate_panel_applet_signals[LAST_SIGNAL];

enum {
	PROP_0,
	PROP_ID,
	PROP_CLOSURE,
	PROP_CONNECTION,
	PROP_PREFS_KEY,
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
	{ "Remove", GTK_STOCK_REMOVE, N_("_Remove From Panel"),
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

G_DEFINE_TYPE (MatePanelApplet, mate_panel_applet, GTK_TYPE_EVENT_BOX)

#define MATE_PANEL_APPLET_INTERFACE   "org.mate.panel.applet.Applet"
#define MATE_PANEL_APPLET_OBJECT_PATH "/org/mate/panel/applet/%s/%d"

static void
mate_panel_applet_associate_schemas_in_dir (MateConfClient  *client,
				       const gchar  *prefs_key,
				       const gchar  *schema_dir,
				       GError      **error)
{
	GSList *list, *l;

	list = mateconf_client_all_entries (client, schema_dir, error);

	if (*error != NULL)
		return;

	for (l = list; l; l = l->next) {
		MateConfEntry  *entry = l->data;
		const gchar *schema_key;
		MateConfEntry  *applet_entry;
		const gchar *applet_schema_key;
		gchar       *key;
		gchar       *tmp;

		schema_key = mateconf_entry_get_key (entry);
		tmp = g_path_get_basename (schema_key);

		if (strchr (tmp, '-'))
			g_warning ("Applet key '%s' contains a hyphen. Please "
				   "use underscores in mateconf keys\n", tmp);

		key = g_strdup_printf ("%s/%s", prefs_key, tmp);
		g_free (tmp);

		/* Associating a schema is potentially expensive, so let's try
		 * to avoid this by doing it only when needed. So we check if
		 * the key is already correctly associated. */

		applet_entry = mateconf_client_get_entry (client, key,
						       NULL, TRUE, NULL);
		if (applet_entry)
			applet_schema_key = mateconf_entry_get_schema_name (applet_entry);
		else
			applet_schema_key = NULL;

		if (g_strcmp0 (schema_key, applet_schema_key) != 0) {
			mateconf_engine_associate_schema (client->engine,
						       key, schema_key, error);

			if (applet_entry == NULL ||
			    mateconf_entry_get_value (applet_entry) == NULL ||
			    mateconf_entry_get_is_default (applet_entry)) {
				/* unset the key: mateconf_client_get_entry()
				 * brought an invalid entry in the client
				 * cache, and we want to fix this */
				mateconf_client_unset (client, key, NULL);
			}
		}

		g_free (key);

		if (applet_entry)
			mateconf_entry_unref (applet_entry);
		mateconf_entry_unref (entry);

		if (*error) {
			g_slist_free (list);
			return;
		}
	}

	g_slist_free (list);

	list = mateconf_client_all_dirs (client, schema_dir, error);

	for (l = list; l; l = l->next) {
		gchar *subdir = l->data;
		gchar *prefs_subdir;
		gchar *schema_subdir;
		gchar *tmp;

		tmp = g_path_get_basename (subdir);

		prefs_subdir  = g_strdup_printf ("%s/%s", prefs_key, tmp);
		schema_subdir = g_strdup_printf ("%s/%s", schema_dir, tmp);

		mate_panel_applet_associate_schemas_in_dir (client,
						       prefs_subdir,
						       schema_subdir,
						       error);

		g_free (prefs_subdir);
		g_free (schema_subdir);
		g_free (subdir);
		g_free (tmp);

		if (*error) {
			g_slist_free (list);
			return;
		}
	}

	g_slist_free (list);
}

void
mate_panel_applet_add_preferences (MatePanelApplet  *applet,
			      const gchar  *schema_dir,
			      GError      **opt_error)
{
	GError **error = NULL;
	GError  *our_error = NULL;

	g_return_if_fail (PANEL_IS_APPLET (applet));
	g_return_if_fail (schema_dir != NULL);

	if (!applet->priv->prefs_key)
		return;

	if (opt_error)
		error = opt_error;
	else
		error = &our_error;

	mate_panel_applet_associate_schemas_in_dir (applet->priv->client,
					       applet->priv->prefs_key,
					       schema_dir,
					       error);

	if (!opt_error && our_error) {
		g_warning (G_STRLOC ": failed to add preferences from '%s' : '%s'",
			   schema_dir, our_error->message);
		g_error_free (our_error);
	}
}

char *
mate_panel_applet_get_preferences_key (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	if (!applet->priv->prefs_key)
		return NULL;

	return g_strdup (applet->priv->prefs_key);
}

static void
mate_panel_applet_set_preferences_key (MatePanelApplet *applet,
				  const char  *prefs_key)
{
	if (applet->priv->prefs_key == prefs_key)
		return;

	if (g_strcmp0 (applet->priv->prefs_key, prefs_key) == 0)
		return;

	if (applet->priv->prefs_key) {
		mateconf_client_remove_dir (applet->priv->client,
					 applet->priv->prefs_key,
					 NULL);

		g_free (applet->priv->prefs_key);
		applet->priv->prefs_key = NULL;
	}

	if (prefs_key) {
		applet->priv->prefs_key = g_strdup (prefs_key);

		mateconf_client_add_dir (applet->priv->client,
				      applet->priv->prefs_key,
				      MATECONF_CLIENT_PRELOAD_RECURSIVE,
				      NULL);
	}

	g_object_notify (G_OBJECT (applet), "prefs-key");
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
		GVariantBuilder *builder;
		GVariantBuilder *invalidated_builder;
		GError          *error = NULL;

		builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
		invalidated_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));

		g_variant_builder_add (builder, "{sv}", "Flags",
				       g_variant_new_uint32 (applet->priv->flags));

		g_dbus_connection_emit_signal (applet->priv->connection,
					       NULL,
					       applet->priv->object_path,
					       "org.freedesktop.DBus.Properties",
					       "PropertiesChanged",
					       g_variant_new ("(sa{sv}as)",
							      MATE_PANEL_APPLET_INTERFACE,
							      builder,
							      invalidated_builder),
					       &error);
		if (error) {
			g_printerr ("Failed to send signal PropertiesChanged::Flags: %s\n",
				    error->message);
			g_error_free (error);
		}
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
		GVariantBuilder *builder;
		GVariantBuilder *invalidated_builder;
		GVariant       **children;
		GError          *error = NULL;

		builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
		invalidated_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));

		children = g_new (GVariant *, applet->priv->size_hints_len);
		for (i = 0; i < n_elements; i++)
			children[i] = g_variant_new_int32 (applet->priv->size_hints[i]);
		g_variant_builder_add (builder, "{sv}", "SizeHints",
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
							      builder,
							      invalidated_builder),
					       &error);
		if (error) {
			g_printerr ("Failed to send signal PropertiesChanged::SizeHints: %s\n",
				    error->message);
			g_error_free (error);
		}
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

#if 0
/* Locked should not be public API: it's not useful for applet writers to know
 * if the applet is locked (as opposed to locked_down). */
static gboolean
mate_panel_applet_get_locked (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), FALSE);

	return applet->priv->locked;
}
#endif

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
	GdkScreen  *screen;
	GdkWindow  *root;
	GdkDisplay *display;
	Display	   *xdisplay;
	Window	    dock_xwindow;
	Window	    xroot;
	XEvent	    xev;

	g_return_if_fail (PANEL_IS_APPLET (applet));

	screen	= gtk_widget_get_screen (GTK_WIDGET (applet));
	root	= gdk_screen_get_root_window (screen);
	display = gdk_screen_get_display (screen);

	xdisplay = GDK_DISPLAY_XDISPLAY (display);
	xroot	 = GDK_WINDOW_XWINDOW (root);

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

	mate_panel_applet_set_preferences_key (applet, NULL);

	if (applet->priv->client)
		g_object_unref (applet->priv->client);
	applet->priv->client = NULL;

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
	g_free (applet->priv->prefs_key);
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
mate_panel_applet_position_menu (GtkMenu   *menu,
			    int       *x,
			    int       *y,
			    gboolean  *push_in,
			    GtkWidget *widget)
{
	MatePanelApplet    *applet;
	GtkAllocation   allocation;
	GtkRequisition  requisition;
	GdkScreen      *screen;
	int             menu_x = 0;
	int             menu_y = 0;
	int             pointer_x;
	int             pointer_y;

	g_return_if_fail (PANEL_IS_APPLET (widget));

	applet = MATE_PANEL_APPLET (widget);

	screen = gtk_widget_get_screen (widget);

	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
	gdk_window_get_origin (gtk_widget_get_window (widget),
			       &menu_x, &menu_y);
	gtk_widget_get_pointer (widget, &pointer_x, &pointer_y);

	gtk_widget_get_allocation (widget, &allocation);

	menu_x += allocation.x;
	menu_y += allocation.y;

	if (applet->priv->orient == MATE_PANEL_APPLET_ORIENT_UP ||
	    applet->priv->orient == MATE_PANEL_APPLET_ORIENT_DOWN) {
		if (gtk_widget_get_direction (GTK_WIDGET (menu)) != GTK_TEXT_DIR_RTL) {
			if (pointer_x < allocation.width &&
			    requisition.width < pointer_x)
				menu_x += MIN (pointer_x,
					       allocation.width - requisition.width);
		} else {
			menu_x += allocation.width - requisition.width;
			if (pointer_x > 0 && pointer_x < allocation.width &&
			    pointer_x < allocation.width - requisition.width) {
				menu_x -= MIN (allocation.width - pointer_x,
					       allocation.width - requisition.width);
			}
		}
		menu_x = MIN (menu_x, gdk_screen_get_width (screen) - requisition.width);

		if (menu_y > gdk_screen_get_height (screen) / 2)
			menu_y -= requisition.height;
		else
			menu_y += allocation.height;
	} else  {
		if (pointer_y < allocation.height &&
		    requisition.height < pointer_y)
			menu_y += MIN (pointer_y, allocation.height - requisition.height);
		menu_y = MIN (menu_y, gdk_screen_get_height (screen) - requisition.height);

		if (menu_x > gdk_screen_get_width (screen) / 2)
			menu_x -= requisition.width;
		else
			menu_x += allocation.width;

	}

	*x = menu_x;
	*y = menu_y;
	*push_in = TRUE;
}

static void
mate_panel_applet_menu_popup (MatePanelApplet *applet,
			 guint        button,
			 guint32      time)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (applet->priv->ui_manager,
					  "/MatePanelAppletPopup");
	gtk_menu_popup (GTK_MENU (menu),
			NULL, NULL,
			(GtkMenuPositionFunc) mate_panel_applet_position_menu,
			applet,
			button, time);
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
mate_panel_applet_button_event (GtkWidget      *widget,
			   GdkEventButton *event)
{
	GdkWindow *window;
	GdkWindow *socket_window;
	XEvent     xevent;

	if (!gtk_widget_is_toplevel (widget))
		return FALSE;

	window = gtk_widget_get_window (widget);
	socket_window = gtk_plug_get_socket_window (GTK_PLUG (widget));

	if (event->type == GDK_BUTTON_PRESS) {
		xevent.xbutton.type = ButtonPress;

		/* X does an automatic pointer grab on button press
		 * if we have both button press and release events
		 * selected.
		 * We don't want to hog the pointer on our parent.
		 */
		gdk_display_pointer_ungrab
			(gtk_widget_get_display (widget),
			 GDK_CURRENT_TIME);
	} else {
		xevent.xbutton.type = ButtonRelease;
	}

	xevent.xbutton.display     = GDK_WINDOW_XDISPLAY (window);
	xevent.xbutton.window      = GDK_WINDOW_XWINDOW (socket_window);
	xevent.xbutton.root        = GDK_WINDOW_XWINDOW (gdk_screen_get_root_window
							 (gdk_drawable_get_screen (window)));
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

	gdk_error_trap_push ();

	XSendEvent (GDK_WINDOW_XDISPLAY (window),
		    GDK_WINDOW_XWINDOW (socket_window),
		    False, NoEventMask, &xevent);

	gdk_flush ();
	gdk_error_trap_pop ();

	return TRUE;
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
		mate_panel_applet_menu_popup (applet, event->button, event->time);

		return TRUE;
	}

	return mate_panel_applet_button_event (applet->priv->plug, event);
}

static gboolean
mate_panel_applet_button_release (GtkWidget      *widget,
			     GdkEventButton *event)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (widget);

	return mate_panel_applet_button_event (applet->priv->plug, event);
}

static gboolean
mate_panel_applet_popup_menu (GtkWidget *widget)
{
	mate_panel_applet_menu_popup (MATE_PANEL_APPLET (widget), 3, GDK_CURRENT_TIME);

	return TRUE;
}

static void
mate_panel_applet_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	int focus_width = 0;

	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->size_request (widget,
								    requisition);

	if (!mate_panel_applet_can_focus (widget))
		return;

	/*
	 * We are deliberately ignoring focus-padding here to
	 * save valuable panel real estate.
	 */
	gtk_widget_style_get (widget,
			      "focus-line-width", &focus_width,
			      NULL);

	requisition->width  += 2 * focus_width;
	requisition->height += 2 * focus_width;
}

static void
mate_panel_applet_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
	GtkAllocation  child_allocation;
	GtkBin        *bin;
	GtkWidget     *child;
	int            border_width;
	int            focus_width = 0;
	MatePanelApplet   *applet;

	if (!mate_panel_applet_can_focus (widget)) {
		GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->size_allocate (widget, allocation);
	} else {
		/*
		 * We are deliberately ignoring focus-padding here to
		 * save valuable panel real estate.
		 */
		gtk_widget_style_get (widget,
				      "focus-line-width", &focus_width,
				      NULL);

		border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

		gtk_widget_set_allocation (widget, allocation);
		bin = GTK_BIN (widget);

		child_allocation.x = focus_width;
		child_allocation.y = focus_width;

		child_allocation.width  = MAX (allocation->width  - border_width * 2, 0);
		child_allocation.height = MAX (allocation->height - border_width * 2, 0);

		if (gtk_widget_get_realized (widget))
			gdk_window_move_resize (gtk_widget_get_window (widget),
						allocation->x + border_width,
						allocation->y + border_width,
						child_allocation.width,
						child_allocation.height);

		child_allocation.width  = MAX (child_allocation.width  - 2 * focus_width, 0);
		child_allocation.height = MAX (child_allocation.height - 2 * focus_width, 0);

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

static gboolean mate_panel_applet_expose(GtkWidget* widget, GdkEventExpose* event)
{
	GtkAllocation allocation;
	int border_width;
	int focus_width = 0;
	int x, y, width, height;

	g_return_val_if_fail (PANEL_IS_APPLET (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->expose_event(widget, event);

        if (!gtk_widget_has_focus (widget))
		return FALSE;

	gtk_widget_get_allocation(widget, &allocation);

	/*
	 * We are deliberately ignoring focus-padding here to
	 * save valuable panel real estate.
	 */
	gtk_widget_style_get (widget,
		"focus-line-width", &focus_width,
		NULL);

	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));

	x = allocation.x;
	y = allocation.y;

	width  = allocation.width  - 2 * border_width;
	height = allocation.height - 2 * border_width;

	gtk_paint_focus (gtk_widget_get_style (widget),
			 gtk_widget_get_window (widget),
			 gtk_widget_get_state (widget),
			 &event->area, widget, "mate_panel_applet",
			 x, y, width, height);

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
			  GdkColor    *color)
{
	int r, g, b;

	g_assert (color_str && color);

	if (sscanf (color_str, "%4x%4x%4x", &r, &g, &b) != 3)
		return FALSE;

	color->red   = r;
	color->green = g;
	color->blue  = b;

	return TRUE;
}

static gboolean
mate_panel_applet_parse_pixmap_str (const char *str,
			       GdkNativeWindow *xid,
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

static GdkPixmap *
mate_panel_applet_get_pixmap (MatePanelApplet     *applet,
			 GdkNativeWindow  xid,
			 int              x,
			 int              y)
{
	gboolean         display_grabbed;
	GdkPixmap       *pixmap;
	GdkDisplay      *display;
	GdkPixmap       *retval;
	GdkWindow       *window;
	int              width;
	int              height;
	cairo_t         *cr;
	cairo_pattern_t *pattern;

	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	if (!gtk_widget_get_realized (GTK_WIDGET (applet)))
		return NULL;

	display = gdk_display_get_default ();
	display_grabbed = FALSE;

	window = gtk_widget_get_window (GTK_WIDGET (applet));

	pixmap = gdk_pixmap_lookup_for_display (display, xid);
	if (pixmap)
		g_object_ref (pixmap);
	else {
		display_grabbed = TRUE;
		gdk_x11_display_grab (display);
		pixmap = gdk_pixmap_foreign_new_for_display (display, xid);
	}

	/* This can happen if the user changes the background very fast.
	 * We'll get the next update, so it's not a big deal. */
	if (pixmap == NULL) {
		if (display_grabbed)
			gdk_x11_display_ungrab (display);
		return NULL;
	}

	#if GTK_CHECK_VERSION(3, 0, 0)
		width = gdk_window_get_width(window);
		height = gdk_window_get_height(window);
	#else
		gdk_drawable_get_size(GDK_DRAWABLE(window), &width, &height);
	#endif

	retval = gdk_pixmap_new (window, width, height, -1);

	/* the pixmap has no colormap, and we need one */
	gdk_drawable_set_colormap (GDK_DRAWABLE (pixmap),
				   gdk_drawable_get_colormap (window));

	cr = gdk_cairo_create (GDK_DRAWABLE (retval));
	gdk_cairo_set_source_pixmap (cr, pixmap, -x, -y);
	pattern = cairo_get_source (cr);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);

	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	cairo_destroy (cr);

	g_object_unref (pixmap);

	if (display_grabbed)
		gdk_x11_display_ungrab (display);

	return retval;
}

static MatePanelAppletBackgroundType
mate_panel_applet_handle_background_string (MatePanelApplet  *applet,
				       GdkColor     *color,
				       GdkPixmap   **pixmap)
{
	MatePanelAppletBackgroundType   retval;
	char                      **elements;

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
		GdkNativeWindow pixmap_id;
		int             x, y;

		g_return_val_if_fail (pixmap != NULL, PANEL_NO_BACKGROUND);

		if (!mate_panel_applet_parse_pixmap_str (elements [1], &pixmap_id, &x, &y)) {
			g_warning ("Incomplete '%s' background type received: %s",
				   elements [0], elements [1]);

			g_strfreev (elements);
			return PANEL_NO_BACKGROUND;
		}

		*pixmap = mate_panel_applet_get_pixmap (applet, pixmap_id, x, y);
		if (!*pixmap) {
			g_warning ("Failed to get pixmap %s", elements [1]);
			g_strfreev (elements);
			return PANEL_NO_BACKGROUND;
		}

		retval = PANEL_PIXMAP_BACKGROUND;
	} else
		g_warning ("Unknown background type received");

	g_strfreev (elements);

	return retval;
}

MatePanelAppletBackgroundType
mate_panel_applet_get_background (MatePanelApplet *applet,
			     GdkColor *color,
			     GdkPixmap **pixmap)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), PANEL_NO_BACKGROUND);

	/* initial sanity */
	if (pixmap != NULL)
		*pixmap = NULL;
	if (color != NULL)
		memset (color, 0, sizeof (GdkColor));

	return mate_panel_applet_handle_background_string (applet, color, pixmap);
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
mate_panel_applet_update_background_for_widget (GtkWidget                 *widget,
					   MatePanelAppletBackgroundType  type,
					   GdkColor                  *color,
					   GdkPixmap                 *pixmap)
{
	GtkRcStyle *rc_style;
	GtkStyle   *style;

	/* reset style */
	gtk_widget_set_style (widget, NULL);
	rc_style = gtk_rc_style_new ();
	gtk_widget_modify_style (widget, rc_style);
	g_object_unref (rc_style);

	switch (type) {
	case PANEL_NO_BACKGROUND:
		break;
	case PANEL_COLOR_BACKGROUND:
		gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, color);
		break;
	case PANEL_PIXMAP_BACKGROUND:
		style = gtk_style_copy (gtk_widget_get_style (widget));
		if (style->bg_pixmap[GTK_STATE_NORMAL])
			g_object_unref (style->bg_pixmap[GTK_STATE_NORMAL]);
		style->bg_pixmap[GTK_STATE_NORMAL] = g_object_ref (pixmap);
		gtk_widget_set_style (widget, style);
		g_object_unref (style);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
mate_panel_applet_handle_background (MatePanelApplet *applet)
{
	MatePanelAppletBackgroundType  type;
	GdkColor                   color;
	GdkPixmap                 *pixmap;

	type = mate_panel_applet_get_background (applet, &color, &pixmap);

	if (applet->priv->background_widget)
		mate_panel_applet_update_background_for_widget (applet->priv->background_widget,
							   type, &color, pixmap);

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
			       0, PANEL_PIXMAP_BACKGROUND, NULL, pixmap);

		g_object_unref (pixmap);
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
mate_panel_applet_get_property (GObject    *object,
			   guint       prop_id,
			   GValue     *value,
			   GParamSpec *pspec)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (object);

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, applet->priv->id);
		break;
	case PROP_CLOSURE:
		g_value_set_pointer (value, applet->priv->closure);
		break;
	case PROP_CONNECTION:
		g_value_set_object (value, applet->priv->connection);
		break;
	case PROP_PREFS_KEY:
		g_value_set_string (value, applet->priv->prefs_key);
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
	case PROP_PREFS_KEY:
		mate_panel_applet_set_preferences_key (applet, g_value_get_string (value));
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
	gtk_binding_entry_add_signal (binding_set, GDK_Tab, modifiers,
				      "move_focus_out_of_applet", 1,
				      GTK_TYPE_DIRECTION_TYPE, direction);
	gtk_binding_entry_add_signal (binding_set, GDK_KP_Tab, modifiers,
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

static void
mate_panel_applet_init (MatePanelApplet *applet)
{
	applet->priv = MATE_PANEL_APPLET_GET_PRIVATE (applet);

	applet->priv->flags  = MATE_PANEL_APPLET_FLAGS_NONE;
	applet->priv->orient = MATE_PANEL_APPLET_ORIENT_UP;
	applet->priv->size   = 24;

	applet->priv->client = mateconf_client_get_default ();

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




	applet->priv->plug = gtk_plug_new (0);
	g_signal_connect_swapped (G_OBJECT (applet->priv->plug), "embedded",
				  G_CALLBACK (mate_panel_applet_setup),
				  applet);

	gtk_widget_set_events (GTK_WIDGET (applet),
			       GDK_BUTTON_PRESS_MASK |
			       GDK_BUTTON_RELEASE_MASK);

	gtk_container_add (GTK_CONTAINER (applet->priv->plug), GTK_WIDGET (applet));
}

static void mate_panel_applet_constructed(GObject* object)
{
	MatePanelApplet* applet = MATE_PANEL_APPLET(object);

	/* Voy a renombrar la clase para que se pueda tener compatibilidad con todos
	 * los estilos visuales de GTK2
	 *
	 * Issue #27
	 */
	gtk_widget_set_name(GTK_WIDGET(applet), "PanelApplet");

	mate_panel_applet_register_object (applet);
}

static void
mate_panel_applet_class_init (MatePanelAppletClass *klass)
{
	GObjectClass   *gobject_class = (GObjectClass *) klass;
	GtkObjectClass *object_class = (GtkObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;
	GtkBindingSet *binding_set;

	gobject_class->get_property = mate_panel_applet_get_property;
	gobject_class->set_property = mate_panel_applet_set_property;
	gobject_class->constructed = mate_panel_applet_constructed;
	klass->move_focus_out_of_applet = mate_panel_applet_move_focus_out_of_applet;

	widget_class->button_press_event = mate_panel_applet_button_press;
	widget_class->button_release_event = mate_panel_applet_button_release;
	widget_class->size_request = mate_panel_applet_size_request;
	widget_class->size_allocate = mate_panel_applet_size_allocate;
	widget_class->expose_event = mate_panel_applet_expose;
	widget_class->focus = mate_panel_applet_focus;
	widget_class->realize = mate_panel_applet_realize;
	widget_class->popup_menu = mate_panel_applet_popup_menu;

	gobject_class->finalize = mate_panel_applet_finalize;

	g_type_class_add_private (klass, sizeof (MatePanelAppletPrivate));

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
					 PROP_PREFS_KEY,
					 g_param_spec_string ("prefs-key",
							      "PrefsKey",
							      "MateConf Preferences Key",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_ORIENT,
					 g_param_spec_uint ("orient",
							    "Orient",
							    "Panel Applet Orientation",
							    0, G_MAXUINT, 0, /* FIXME */
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
							    0, G_MAXUINT, 0, /* FIXME */
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
			      GDK_TYPE_COLOR,
			      GDK_TYPE_PIXMAP);

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

	binding_set = gtk_binding_set_by_class (object_class);
	add_tab_bindings (binding_set, 0, GTK_DIR_TAB_FORWARD);
	add_tab_bindings (binding_set, GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
	add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
	add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
}

GtkWidget* mate_panel_applet_new(void)
{
	MatePanelApplet* applet = g_object_new(PANEL_TYPE_APPLET, NULL);

	return GTK_WIDGET(applet);
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

	if (g_strcmp0 (method_name, "PopupMenu") == 0) {
		guint button;
		guint time;

		g_variant_get (parameters, "(uu)", &button, &time);
		mate_panel_applet_menu_popup (applet, button, time);

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

	if (g_strcmp0 (property_name, "PrefsKey") == 0) {
		retval = g_variant_new_string (applet->priv->prefs_key ?
					       applet->priv->prefs_key : "");
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

	if (g_strcmp0 (property_name, "PrefsKey") == 0) {
		mate_panel_applet_set_preferences_key (applet, g_variant_get_string (value, NULL));
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
	    "<property name='PrefsKey' type='s' access='readwrite'/>"
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

int mate_panel_applet_factory_main(const gchar* factory_id, gboolean out_process, GType applet_type, MatePanelAppletFactoryCallback callback, gpointer user_data)
{
	MatePanelAppletFactory* factory;
	GClosure* closure;

	g_return_val_if_fail(factory_id != NULL, 1);
	g_return_val_if_fail(callback != NULL, 1);
	g_assert(g_type_is_a(applet_type, PANEL_TYPE_APPLET));

	if (out_process)
	{
		_mate_panel_applet_setup_x_error_handler();
	}

	closure = g_cclosure_new(G_CALLBACK(callback), user_data, NULL);
	factory = mate_panel_applet_factory_new(factory_id, applet_type, closure);
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

void
mate_panel_applet_set_background_widget (MatePanelApplet *applet,
				    GtkWidget   *widget)
{
	applet->priv->background_widget = widget;

	if (widget) {
		MatePanelAppletBackgroundType  type;
		GdkColor                   color;
		GdkPixmap                 *pixmap;

		type = mate_panel_applet_get_background (applet, &color, &pixmap);
		mate_panel_applet_update_background_for_widget (widget, type,
							   &color, pixmap);
		if (type == PANEL_PIXMAP_BACKGROUND)
			g_object_unref (pixmap);
	}
}

guint32
mate_panel_applet_get_xid (MatePanelApplet *applet,
		      GdkScreen   *screen)
{
	gtk_window_set_screen (GTK_WINDOW (applet->priv->plug), screen);
	gtk_widget_show (applet->priv->plug);

	return gtk_plug_get_id (GTK_PLUG (applet->priv->plug));
}

const gchar *
mate_panel_applet_get_object_path (MatePanelApplet *applet)
{
	return applet->priv->object_path;
}
