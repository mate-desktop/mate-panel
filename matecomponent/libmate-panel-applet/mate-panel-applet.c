/*
 * mate-panel-applet.c: panel applet writing library.
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

#include <cairo.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <matecomponent/matecomponent-ui-util.h>
#include <matecomponent/matecomponent-main.h>
#include <matecomponent/matecomponent-types.h>
#include <matecomponent/matecomponent-property-bag.h>
#include <matecomponent/matecomponent-item-handler.h>
#include <matecomponent/matecomponent-shlib-factory.h>
#include <matecomponent/matecomponent-property-bag-client.h>
#include <mateconf/mateconf.h>
#include <mateconf/mateconf-client.h>
#include <X11/Xatom.h>

#include "mate-panel-applet.h"
#include "mate-panel-applet-private.h"
#include "mate-panel-applet-shell.h"
#include "mate-panel-applet-marshal.h"
#include "mate-panel-applet-enums.h"

#define MATE_PANEL_APPLET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PANEL_TYPE_APPLET, MatePanelAppletPrivate))

struct _MatePanelAppletPrivate {
	MatePanelAppletShell           *shell;
	MateComponentControl              *control;
	MateComponentPropertyBag          *prop_sack;
	MateComponentItemHandler          *item_handler;
	MateConfClient                *client;

	char                       *iid;
	GClosure                   *closure;
	gboolean                    bound;
	char                       *prefs_key;

	MatePanelAppletFlags            flags;
	MatePanelAppletOrient           orient;
	guint                       size;
	char                       *background;
	GtkWidget                  *background_widget;

	int                         previous_width;
	int                         previous_height;

        int                        *size_hints;
        int                         size_hints_len;

	gboolean                    moving_focus_out;

	gboolean                    locked_down;
};

enum {
        CHANGE_ORIENT,
        CHANGE_SIZE,
        CHANGE_BACKGROUND,
	MOVE_FOCUS_OUT_OF_APPLET,
        SAVE_YOURSELF,
        LAST_SIGNAL
};

static guint mate_panel_applet_signals [LAST_SIGNAL];

#define PROPERTY_ORIENT     "mate-panel-applet-orient"
#define PROPERTY_SIZE       "mate-panel-applet-size"
#define PROPERTY_BACKGROUND "mate-panel-applet-background"
#define PROPERTY_FLAGS      "mate-panel-applet-flags"
#define PROPERTY_SIZE_HINTS "mate-panel-applet-size-hints"
#define PROPERTY_LOCKED_DOWN "mate-panel-applet-locked-down"

enum {
	PROPERTY_ORIENT_IDX,
	PROPERTY_SIZE_IDX,
	PROPERTY_BACKGROUND_IDX,
	PROPERTY_FLAGS_IDX,
	PROPERTY_SIZE_HINTS_IDX,
	PROPERTY_LOCKED_DOWN_IDX
};

G_DEFINE_TYPE (MatePanelApplet, mate_panel_applet, GTK_TYPE_EVENT_BOX)

static void mate_panel_applet_handle_background (MatePanelApplet *applet);
static void mate_panel_applet_setup             (MatePanelApplet *applet);

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
	g_return_if_fail (PANEL_IS_APPLET (applet));

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

	if (applet->priv->prop_sack != NULL)
		matecomponent_pbclient_set_short (MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_FLAGS, flags, NULL);
	else
		applet->priv->flags = flags;
}

void
mate_panel_applet_set_size_hints (MatePanelApplet      *applet,
			     const int        *size_hints,
			     int               n_elements,
			     int               base_size)
{
	CORBA_sequence_CORBA_long *seq;
	CORBA_Environment          env;
	CORBA_any                  any;
	int                        i;

	CORBA_exception_init (&env);

	seq = CORBA_sequence_CORBA_long__alloc ();
	seq->_length = seq->_maximum = n_elements;
	seq->_release = CORBA_TRUE;
	seq->_buffer  = CORBA_sequence_CORBA_long_allocbuf (seq->_length);

	for (i = 0; i < n_elements; i++)
		seq->_buffer [i] = size_hints [i] + base_size;

	any._type    = TC_CORBA_sequence_CORBA_long;
	any._release = CORBA_FALSE;
	any._value   = seq;

	MateComponent_PropertyBag_setValue (MATECOMPONENT_OBJREF (applet->priv->prop_sack),
				     PROPERTY_SIZE_HINTS,
				     &any,
				     &env);

	CORBA_free (seq);

	CORBA_exception_free (&env);
}

guint
mate_panel_applet_get_size (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), 0);

	return applet->priv->size;
}

MatePanelAppletOrient
mate_panel_applet_get_orient (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), 0);

	return applet->priv->orient;
}

gboolean
mate_panel_applet_get_locked_down (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), FALSE);

	return applet->priv->locked_down;
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

void
mate_panel_applet_setup_menu (MatePanelApplet        *applet,
			 const gchar        *xml,
			 const MateComponentUIVerb *verb_list,
			 gpointer            user_data)
{
	MateComponentUIComponent *popup_component;

	g_return_if_fail (PANEL_IS_APPLET (applet));
	g_return_if_fail (xml != NULL && verb_list != NULL);

	popup_component = mate_panel_applet_get_popup_component (applet);

	matecomponent_ui_component_set (popup_component, "/", "<popups/>", NULL);

	matecomponent_ui_component_set_translate (popup_component, "/popups", xml, NULL);

	matecomponent_ui_component_add_verb_list_with_data (popup_component, verb_list, user_data);
}

void
mate_panel_applet_setup_menu_from_file (MatePanelApplet        *applet,
				   const gchar        *opt_datadir,
				   const gchar        *file,
				   const gchar        *opt_app_name,
				   const MateComponentUIVerb *verb_list,
				   gpointer            user_data)
{
	MateComponentUIComponent *popup_component;
	gchar             *app_name = NULL;

	g_return_if_fail (PANEL_IS_APPLET (applet));
	g_return_if_fail (file != NULL && verb_list != NULL);

	if (!opt_datadir)
		opt_datadir = MATE_PANEL_APPLET_DATADIR;

	if (!opt_app_name)
		opt_app_name = app_name = g_strdup_printf ("%lu",
							   (unsigned long) getpid ());

	popup_component = mate_panel_applet_get_popup_component (applet);

	matecomponent_ui_util_set_ui (popup_component, opt_datadir, file, opt_app_name, NULL);

	matecomponent_ui_component_add_verb_list_with_data (popup_component, verb_list, user_data);

	if (app_name)
		g_free (app_name);
}

MateComponentControl *
mate_panel_applet_get_control (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	return applet->priv->control;
}

MateComponentUIComponent *
mate_panel_applet_get_popup_component (MatePanelApplet *applet)
{
	g_return_val_if_fail (PANEL_IS_APPLET (applet), NULL);

	return matecomponent_control_get_popup_ui_component (applet->priv->control);
}

static void
mate_panel_applet_finalize (GObject *object)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (object);

	mate_panel_applet_set_preferences_key (applet, NULL);

	if (applet->priv->client)
		g_object_unref (applet->priv->client);
	applet->priv->client = NULL;

	if (applet->priv->prop_sack)
		matecomponent_object_unref (
			MATECOMPONENT_OBJECT (applet->priv->prop_sack));
	applet->priv->prop_sack = NULL;

	g_free (applet->priv->size_hints);
	g_free (applet->priv->prefs_key);
	g_free (applet->priv->background);
	g_free (applet->priv->iid);

	if (applet->priv->closure)
		g_closure_unref (applet->priv->closure);
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

	if (event->button == 1)
		return TRUE;
	else if (event->button == 3) {
		matecomponent_control_do_popup_full (
				applet->priv->control,
				NULL, NULL,
				(GtkMenuPositionFunc) mate_panel_applet_position_menu,
				applet,
				event->button,
				event->time);
		return TRUE;
	}

	return FALSE;
}

gboolean
_mate_panel_applet_popup_menu (MatePanelApplet *applet,
			  guint button,
			  guint32 time)
{
	matecomponent_control_do_popup_full (applet->priv->control, NULL, NULL,
				      (GtkMenuPositionFunc) mate_panel_applet_position_menu,
				      applet, button, time);
	return TRUE;
}

static gboolean
mate_panel_applet_popup_menu (MatePanelApplet *applet)
{
	return _mate_panel_applet_popup_menu (applet, 3, GDK_CURRENT_TIME);
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

static gboolean
mate_panel_applet_expose (GtkWidget      *widget,
		     GdkEventExpose *event)
{
	GtkAllocation allocation;
	int border_width;
	int focus_width = 0;
	int x, y, width, height;

	g_return_val_if_fail (PANEL_IS_APPLET (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->expose_event (widget, event);

        if (!gtk_widget_has_focus (widget))
		return FALSE;

	gtk_widget_get_allocation (widget, &allocation);

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

	retval = gdk_pixmap_new (window,
				 width, height, -1);

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
mate_panel_applet_get_prop (MateComponentPropertyBag *sack,
                       MateComponentArg         *arg,
		       guint              arg_id,
		       CORBA_Environment *ev,
		       gpointer           user_data)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (user_data);

	switch (arg_id) {
	case PROPERTY_ORIENT_IDX:
		MATECOMPONENT_ARG_SET_SHORT (arg, applet->priv->orient);
		break;
	case PROPERTY_SIZE_IDX:
		MATECOMPONENT_ARG_SET_SHORT (arg, applet->priv->size);
		break;
	case PROPERTY_BACKGROUND_IDX:
		MATECOMPONENT_ARG_SET_STRING (arg, applet->priv->background);
		break;
	case PROPERTY_FLAGS_IDX:
		MATECOMPONENT_ARG_SET_SHORT (arg, applet->priv->flags);
		break;
	case PROPERTY_SIZE_HINTS_IDX: {
		CORBA_sequence_CORBA_long *seq;
		int                        i;

		seq = arg->_value;

		seq->_length  = seq->_maximum = applet->priv->size_hints_len;
		seq->_buffer  = CORBA_sequence_CORBA_long_allocbuf (seq->_length);
		seq->_release = CORBA_TRUE;

		for (i = 0; i < applet->priv->size_hints_len; i++)
			seq->_buffer [i] = applet->priv->size_hints [i];
	}
		break;
	case PROPERTY_LOCKED_DOWN_IDX:
		MATECOMPONENT_ARG_SET_BOOLEAN (arg, applet->priv->locked_down);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
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
mate_panel_applet_set_prop (MateComponentPropertyBag *sack,
		       const MateComponentArg   *arg,
		       guint              arg_id,
		       CORBA_Environment *ev,
		       gpointer           user_data)
{
	MatePanelApplet *applet = MATE_PANEL_APPLET (user_data);

	switch (arg_id) {
	case PROPERTY_ORIENT_IDX: {
		MatePanelAppletOrient orient;

		orient = MATECOMPONENT_ARG_GET_SHORT (arg);

		if (applet->priv->orient != orient) {
			applet->priv->orient = orient;

			g_signal_emit (G_OBJECT (applet),
				       mate_panel_applet_signals [CHANGE_ORIENT],
				       0, orient);
		}
	}
		break;
	case PROPERTY_SIZE_IDX: {
		guint size;

		size = MATECOMPONENT_ARG_GET_SHORT (arg);

		if (applet->priv->size != size) {
			applet->priv->size = size;

			g_signal_emit (G_OBJECT (applet),
                                       mate_panel_applet_signals [CHANGE_SIZE],
                                       0, size);
		}
	}
		break;
	case PROPERTY_BACKGROUND_IDX:
		if (applet->priv->background)
			g_free (applet->priv->background);

		applet->priv->background = g_strdup (MATECOMPONENT_ARG_GET_STRING (arg));

		mate_panel_applet_handle_background (applet);
		break;
	case PROPERTY_FLAGS_IDX:
		applet->priv->flags = MATECOMPONENT_ARG_GET_SHORT (arg);
		break;
	case PROPERTY_SIZE_HINTS_IDX: {
		CORBA_sequence_CORBA_long *seq = arg->_value;
		int                        i;

		applet->priv->size_hints = g_realloc (applet->priv->size_hints,
						      seq->_length * sizeof (int));
		for (i = 0; i < seq->_length; i++)
			applet->priv->size_hints [i] = seq->_buffer [i];

		applet->priv->size_hints_len = seq->_length;;
	}
		break;
	case PROPERTY_LOCKED_DOWN_IDX:
		applet->priv->locked_down = MATECOMPONENT_ARG_GET_BOOLEAN (arg);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static MateComponentPropertyBag *
mate_panel_applet_property_bag (MatePanelApplet *applet)
{
	MateComponentPropertyBag *sack;

	sack = matecomponent_property_bag_new (mate_panel_applet_get_prop,
					mate_panel_applet_set_prop,
					applet);

	matecomponent_property_bag_add (sack,
				 PROPERTY_ORIENT,
				 PROPERTY_ORIENT_IDX,
				 MATECOMPONENT_ARG_SHORT,
				 NULL,
				 "The Applet's containing Panel's orientation",
				 MateComponent_PROPERTY_READABLE | MateComponent_PROPERTY_WRITEABLE);

	matecomponent_property_bag_add (sack,
				 PROPERTY_SIZE,
				 PROPERTY_SIZE_IDX,
				 MATECOMPONENT_ARG_SHORT,
				 NULL,
				 "The Applet's containing Panel's size in pixels",
				 MateComponent_PROPERTY_READABLE | MateComponent_PROPERTY_WRITEABLE);

	matecomponent_property_bag_add (sack,
				 PROPERTY_BACKGROUND,
				 PROPERTY_BACKGROUND_IDX,
				 MATECOMPONENT_ARG_STRING,
				 NULL,
				 "The Applet's containing Panel's background color or pixmap",
				 MateComponent_PROPERTY_READABLE | MateComponent_PROPERTY_WRITEABLE);

	matecomponent_property_bag_add (sack,
				 PROPERTY_FLAGS,
				 PROPERTY_FLAGS_IDX,
				 MATECOMPONENT_ARG_SHORT,
				 NULL,
				 "The Applet's flags",
				 MateComponent_PROPERTY_READABLE);

	matecomponent_property_bag_add (sack,
				 PROPERTY_SIZE_HINTS,
				 PROPERTY_SIZE_HINTS_IDX,
				 TC_CORBA_sequence_CORBA_long,
				 NULL,
				 "Ranges that hint what sizes are acceptable for the applet",
				 MateComponent_PROPERTY_READABLE);

	matecomponent_property_bag_add (sack,
				 PROPERTY_LOCKED_DOWN,
				 PROPERTY_LOCKED_DOWN_IDX,
				 MATECOMPONENT_ARG_BOOLEAN,
				 NULL,
				 "The Applet's containing Panel is locked down",
				 MateComponent_PROPERTY_READABLE | MateComponent_PROPERTY_WRITEABLE);

	return sack;
}

static void
mate_panel_applet_realize (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (mate_panel_applet_parent_class)->realize (widget);

	if (MATE_PANEL_APPLET (widget)->priv->background)
		mate_panel_applet_handle_background (MATE_PANEL_APPLET (widget));
}

static void
mate_panel_applet_control_bound (MateComponentControl *control,
			    MatePanelApplet   *applet)
{
	gboolean ret;

	g_return_if_fail (PANEL_IS_APPLET (applet));
	g_return_if_fail (applet->priv->iid != NULL &&
			  applet->priv->closure != NULL);

	if (applet->priv->bound)
		return;

	matecomponent_closure_invoke (applet->priv->closure,
			       G_TYPE_BOOLEAN, &ret,
			       PANEL_TYPE_APPLET, applet,
			       G_TYPE_STRING, applet->priv->iid,
			       NULL);


	if (!ret) { /* FIXME */
		g_warning ("need to free the control here");

		return;
	}

	applet->priv->bound = TRUE;
}

static MateComponent_Unknown
mate_panel_applet_item_handler_get_object (MateComponentItemHandler *handler,
				      const char        *item_name,
				      gboolean           only_if_exists,
				      gpointer           user_data,
				      CORBA_Environment *ev)
{
	MatePanelApplet *applet = user_data;
	GSList      *options;
	GSList      *l;

	g_return_val_if_fail (PANEL_IS_APPLET (applet), CORBA_OBJECT_NIL);

	options = matecomponent_item_option_parse (item_name);

	for (l = options; l; l = l->next) {
		MateComponentItemOption *option = l->data;

		if (!option->value || !option->value [0])
			continue;

		if (!strcmp (option->key, "prefs_key") && !applet->priv->prefs_key)
			mate_panel_applet_set_preferences_key (applet, option->value);

		else if (!strcmp (option->key, "background"))
			matecomponent_pbclient_set_string (MATECOMPONENT_OBJREF (applet->priv->prop_sack),
						    PROPERTY_BACKGROUND, option->value, NULL);

		else if (!strcmp (option->key, "orient")) {
			if (!strcmp (option->value, "up"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_ORIENT,
					MATE_PANEL_APPLET_ORIENT_UP, NULL);

			else if (!strcmp (option->value, "down"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_ORIENT,
					MATE_PANEL_APPLET_ORIENT_DOWN, NULL);

			else if (!strcmp (option->value, "left"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_ORIENT,
					MATE_PANEL_APPLET_ORIENT_LEFT, NULL);

			else if (!strcmp (option->value, "right"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_ORIENT,
					MATE_PANEL_APPLET_ORIENT_RIGHT, NULL);

		} else if (!strcmp (option->key, "size")) {
			if (!strcmp (option->value, "xx-small"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_XX_SMALL, NULL);

			else if (!strcmp (option->value, "x-small"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_X_SMALL, NULL);

			else if (!strcmp (option->value, "small"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_SMALL, NULL);

			else if (!strcmp (option->value, "medium"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_MEDIUM, NULL);

			else if (!strcmp (option->value, "large"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_LARGE, NULL);

			else if (!strcmp (option->value, "x-large"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_X_LARGE, NULL);

			else if (!strcmp (option->value, "xx-large"))
				matecomponent_pbclient_set_short (
					MATECOMPONENT_OBJREF (applet->priv->prop_sack), PROPERTY_SIZE,
					MATE_Vertigo_PANEL_XX_LARGE, NULL);
		} else if (!strcmp (option->key, "locked_down")) {
			gboolean val = FALSE;
			if (option->value[0] == 'T' ||
			    option->value[0] == 't' ||
			    option->value[0] == 'Y' ||
			    option->value[0] == 'y' ||
			    atoi (option->value) != 0)
				val = TRUE;
			matecomponent_pbclient_set_boolean (MATECOMPONENT_OBJREF (applet->priv->prop_sack),
						     PROPERTY_LOCKED_DOWN, val, NULL);
		}
	}

	matecomponent_item_options_free (options);

	return matecomponent_object_dup_ref (MATECOMPONENT_OBJREF (applet->priv->control), ev);
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

static GObject* mate_panel_applet_constructor(GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
	GObject* obj = G_OBJECT_CLASS(mate_panel_applet_parent_class)->constructor(type, n_construct_properties, construct_properties);

	MatePanelApplet* applet = MATE_PANEL_APPLET(obj);

	/* Voy a renombrar la clase para que se pueda tener compatibilidad con todos
	 * los estilos visuales de GTK2
	 *
	 * Issue #27
	 */
	gtk_widget_set_name(GTK_WIDGET(applet), "PanelApplet");

	mate_panel_applet_setup(applet);

	return obj;
}

static void
mate_panel_applet_class_init (MatePanelAppletClass *klass)
{
	GObjectClass   *gobject_class = (GObjectClass *) klass;
	GtkObjectClass *object_class = (GtkObjectClass *) klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;
	GtkBindingSet *binding_set;

	gobject_class->constructor = mate_panel_applet_constructor;
	klass->move_focus_out_of_applet = mate_panel_applet_move_focus_out_of_applet;

	widget_class->button_press_event = mate_panel_applet_button_press;
	widget_class->size_request = mate_panel_applet_size_request;
	widget_class->size_allocate = mate_panel_applet_size_allocate;
	widget_class->expose_event = mate_panel_applet_expose;
	widget_class->focus = mate_panel_applet_focus;
	widget_class->realize = mate_panel_applet_realize;

	gobject_class->finalize = mate_panel_applet_finalize;

	g_type_class_add_private (klass, sizeof (MatePanelAppletPrivate));

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

static void
mate_panel_applet_init (MatePanelApplet *applet)
{
	applet->priv = MATE_PANEL_APPLET_GET_PRIVATE (applet);

	applet->priv->client = mateconf_client_get_default ();

	applet->priv->bound  = FALSE;
	applet->priv->flags  = MATE_PANEL_APPLET_FLAGS_NONE;
	applet->priv->orient = MATE_PANEL_APPLET_ORIENT_UP;
	applet->priv->size   = MATE_Vertigo_PANEL_MEDIUM;

	applet->priv->moving_focus_out = FALSE;

	gtk_widget_set_events (GTK_WIDGET (applet),
			       GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
}

static void
mate_panel_applet_setup (MatePanelApplet *applet)
{
	MatePanelAppletPrivate *priv;

	priv = applet->priv;

	priv->control = matecomponent_control_new (GTK_WIDGET (applet));

	g_signal_connect (priv->control, "set_frame",
			  G_CALLBACK (mate_panel_applet_control_bound),
			  applet);

	priv->prop_sack = mate_panel_applet_property_bag (applet);

	matecomponent_control_set_properties (
			priv->control, MATECOMPONENT_OBJREF (priv->prop_sack), NULL);

	priv->shell = mate_panel_applet_shell_new (applet);

	matecomponent_object_add_interface (MATECOMPONENT_OBJECT (priv->control),
				     MATECOMPONENT_OBJECT (priv->shell));

	priv->item_handler =
		matecomponent_item_handler_new (
			NULL, mate_panel_applet_item_handler_get_object, applet);

	matecomponent_object_add_interface (MATECOMPONENT_OBJECT (priv->control),
				     MATECOMPONENT_OBJECT (priv->item_handler));

	g_signal_connect (applet, "popup_menu",
			  G_CALLBACK (mate_panel_applet_popup_menu), NULL);
}

GtkWidget* mate_panel_applet_new(void)
{
	MatePanelApplet* applet = g_object_new(PANEL_TYPE_APPLET, NULL);

	return GTK_WIDGET (applet);
}

typedef struct {
	GType     applet_type;
	GClosure *closure;
} MatePanelAppletCallBackData;

static MatePanelAppletCallBackData *
mate_panel_applet_callback_data_new (GType     applet_type,
				GClosure *closure)
{
	MatePanelAppletCallBackData *retval;

	retval = g_new0 (MatePanelAppletCallBackData, 1);

	retval->applet_type = applet_type;
	retval->closure     = closure;

	return retval;
}

static void
mate_panel_applet_callback_data_free (MatePanelAppletCallBackData *data)
{
	g_closure_unref (data->closure);
	g_free (data);
}

static MateComponentObject *
mate_panel_applet_factory_callback (MateComponentGenericFactory    *factory,
			       const char              *iid,
			       MatePanelAppletCallBackData *data)
{
	MatePanelApplet *applet;

	applet = g_object_new (data->applet_type, NULL);

	applet->priv->iid     = g_strdup (iid);
	applet->priv->closure = g_closure_ref (data->closure);

	matecomponent_control_life_instrument (applet->priv->control);

	return MATECOMPONENT_OBJECT (applet->priv->control);
}

static void
mate_panel_applet_all_controls_dead (void)
{
	if (!matecomponent_control_life_get_count())
		matecomponent_main_quit ();
}

int
mate_panel_applet_factory_main_closure (const gchar *iid,
				   GType        applet_type,
				   GClosure    *closure)
{
	int                      retval;
	char                    *display_iid;
	MatePanelAppletCallBackData *data;

	g_return_val_if_fail (iid != NULL, 1);
	g_return_val_if_fail (closure != NULL, 1);

	g_assert (g_type_is_a (applet_type, PANEL_TYPE_APPLET));

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	matecomponent_control_life_set_callback (mate_panel_applet_all_controls_dead);

	closure = matecomponent_closure_store (closure, mate_panel_applet_marshal_BOOLEAN__STRING);

	data = mate_panel_applet_callback_data_new (applet_type, closure);

	display_iid = matecomponent_activation_make_registration_id (
		iid, DisplayString (gdk_display_get_default ()));
	retval = matecomponent_generic_factory_main (
		display_iid,
		(MateComponentFactoryCallback) mate_panel_applet_factory_callback,
		data);
	g_free (display_iid);

	mate_panel_applet_callback_data_free (data);

	return retval;
}

int
mate_panel_applet_factory_main (const gchar                 *iid,
			   GType                        applet_type,
			   MatePanelAppletFactoryCallback   callback,
			   gpointer                     data)
{
	GClosure *closure;

	g_return_val_if_fail (iid != NULL, 1);
	g_return_val_if_fail (callback != NULL, 1);

	closure = g_cclosure_new (G_CALLBACK (callback), data, NULL);

	return mate_panel_applet_factory_main_closure (iid, applet_type, closure);
}

MateComponent_Unknown
mate_panel_applet_shlib_factory_closure (const char         *iid,
				    GType               applet_type,
				    PortableServer_POA  poa,
				    gpointer            impl_ptr,
				    GClosure           *closure,
				    CORBA_Environment  *ev)
{
	MateComponentShlibFactory *factory;

	g_return_val_if_fail (iid != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (closure != NULL, CORBA_OBJECT_NIL);

	g_assert (g_type_is_a (applet_type, PANEL_TYPE_APPLET));

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	closure = matecomponent_closure_store (closure, mate_panel_applet_marshal_BOOLEAN__STRING);

	factory = matecomponent_shlib_factory_new_closure (
			iid, poa, impl_ptr,
			g_cclosure_new (G_CALLBACK (mate_panel_applet_factory_callback),
					mate_panel_applet_callback_data_new (applet_type, closure),
					(GClosureNotify) mate_panel_applet_callback_data_free));

        return CORBA_Object_duplicate (MATECOMPONENT_OBJREF (factory), ev);
}

MateComponent_Unknown
mate_panel_applet_shlib_factory (const char                 *iid,
			    GType                       applet_type,
			    PortableServer_POA          poa,
			    gpointer                    impl_ptr,
			    MatePanelAppletFactoryCallback  callback,
			    gpointer                    user_data,
			    CORBA_Environment          *ev)
{
	g_return_val_if_fail (iid != NULL, CORBA_OBJECT_NIL);
	g_return_val_if_fail (callback != NULL, CORBA_OBJECT_NIL);

	return mate_panel_applet_shlib_factory_closure (
			iid, applet_type, poa, impl_ptr,
			g_cclosure_new (G_CALLBACK (callback),
					user_data, NULL),
			ev);
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
