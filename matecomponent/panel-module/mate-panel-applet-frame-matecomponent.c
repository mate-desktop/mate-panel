/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * mate-panel-applet-frame-matecomponent.c: panel side container for applets
 *
 * Copyright (C) 2001 - 2003 Sun Microsystems, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include <string.h>

#include <libmatecomponentui.h>

#include <mate-panel-applet-frame.h>
#include <mate-panel-applets-manager.h>

#include "MATE_Panel.h"

#include "mate-panel-applet-frame-matecomponent.h"

G_DEFINE_TYPE (MatePanelAppletFrameMateComponent,
	       mate_panel_applet_frame_matecomponent,
	       PANEL_TYPE_APPLET_FRAME)

struct _MatePanelAppletFrameMateComponentPrivate
{
	MATE_Vertigo_MatePanelAppletShell  applet_shell;
	CORBA_Object                    control;
	MateComponent_PropertyBag              property_bag;
	MateComponentUIComponent              *ui_component;
};

#define PROPERTY_ORIENT      "mate-panel-applet-orient"
#define PROPERTY_SIZE        "mate-panel-applet-size"
#define PROPERTY_BACKGROUND  "mate-panel-applet-background"
#define PROPERTY_FLAGS       "mate-panel-applet-flags"
#define PROPERTY_SIZE_HINTS  "mate-panel-applet-size-hints"
#define PROPERTY_LOCKED_DOWN "mate-panel-applet-locked-down"

typedef enum {
       PANEL_SIZE_XX_SMALL = MATE_Vertigo_PANEL_XX_SMALL,
       PANEL_SIZE_X_SMALL  = MATE_Vertigo_PANEL_X_SMALL,
       PANEL_SIZE_SMALL    = MATE_Vertigo_PANEL_SMALL,
       PANEL_SIZE_MEDIUM   = MATE_Vertigo_PANEL_MEDIUM,
       PANEL_SIZE_LARGE    = MATE_Vertigo_PANEL_LARGE,
       PANEL_SIZE_X_LARGE  = MATE_Vertigo_PANEL_X_LARGE,
       PANEL_SIZE_XX_LARGE = MATE_Vertigo_PANEL_XX_LARGE
} PanelSize;

/* Keep in sync with mate-panel-applet.h. Uggh. */
typedef enum {
	APPLET_FLAGS_NONE   = 0,
	APPLET_EXPAND_MAJOR = 1 << 0,
	APPLET_EXPAND_MINOR = 1 << 1,
	APPLET_HAS_HANDLE   = 1 << 2
} MatePanelAppletFlags;

GQuark
mate_panel_applet_frame_matecomponent_error_quark (void)
{
        static GQuark ret = 0;

        if (ret == 0) {
                ret = g_quark_from_static_string ("mate_panel_applet_frame_matecomponent_error");
        }

        return ret;
}

static guint
get_mate_panel_applet_orient (PanelOrientation orientation)
{
	/* For some reason libmate-panel-applet and panel use a different logic for
	 * orientation, so we need to convert it. We should fix this. */
	switch (orientation) {
	case PANEL_ORIENTATION_TOP:
		return 1;
	case PANEL_ORIENTATION_BOTTOM:
		return 0;
	case PANEL_ORIENTATION_LEFT:
		return 3;
	case PANEL_ORIENTATION_RIGHT:
		return 2;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
mate_panel_applet_frame_matecomponent_update_flags (MatePanelAppletFrame *frame,
					const CORBA_any  *any)
{
	int      flags;
	gboolean major;
	gboolean minor;
	gboolean has_handle;

	flags = MATECOMPONENT_ARG_GET_SHORT (any);

	major = (flags & APPLET_EXPAND_MAJOR) != 0;
	minor = (flags & APPLET_EXPAND_MINOR) != 0;
	has_handle = (flags & APPLET_HAS_HANDLE) != 0;

	_mate_panel_applet_frame_update_flags (frame, major, minor, has_handle);
}

static void
mate_panel_applet_frame_matecomponent_update_size_hints (MatePanelAppletFrame *frame,
					     const CORBA_any  *any)
{
	CORBA_sequence_CORBA_long *seq;

	seq = any->_value;

	_mate_panel_applet_frame_update_size_hints (frame, seq->_buffer, seq->_length);
}

static void
mate_panel_applet_frame_matecomponent_init_properties (MatePanelAppletFrame *frame)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);
	CORBA_any *any;

	any = matecomponent_pbclient_get_value (matecomponent_frame->priv->property_bag,
					 PROPERTY_FLAGS,
					 MATECOMPONENT_ARG_SHORT,
					 NULL);
	if (any) {
		mate_panel_applet_frame_matecomponent_update_flags (frame, any);
		CORBA_free (any);
	}

	any = matecomponent_pbclient_get_value (matecomponent_frame->priv->property_bag,
					 PROPERTY_SIZE_HINTS,
					 TC_CORBA_sequence_CORBA_long,
					 NULL);
	if (any) {
		mate_panel_applet_frame_matecomponent_update_size_hints (frame, any);
		CORBA_free (any);
	}
}

static void
mate_panel_applet_frame_matecomponent_sync_menu_state (MatePanelAppletFrame *frame,
					   gboolean          movable,
					   gboolean          removable,
					   gboolean          lockable,
					   gboolean          locked,
					   gboolean          locked_down)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/LockAppletToPanel",
				      "state",
				      locked ? "1" : "0",
				      NULL);

	/* First sensitivity */
	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/LockAppletToPanel",
				      "sensitive",
				      lockable ? "1" : "0",
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/RemoveAppletFromPanel",
				      "sensitive",
				      (locked && !lockable) ? "0" : (removable ? "1" : "0"),
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/MoveApplet",
				      "sensitive",
				      locked ? "0" : (movable ? "1" : "0"),
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/LockAppletToPanel",
				      "hidden",
				      locked_down ? "1" : "0",
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/LockSeparator",
				      "hidden",
				      locked_down ? "1" : "0",
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/RemoveAppletFromPanel",
				      "hidden",
				      locked_down ? "1" : "0",
				      NULL);

	matecomponent_ui_component_set_prop (matecomponent_frame->priv->ui_component,
				      "/commands/MoveApplet",
				      "hidden",
				      locked_down ? "1" : "0",
				      NULL);
}

static void
mate_panel_applet_frame_matecomponent_popup_menu (MatePanelAppletFrame *frame,
				      guint             button,
				      guint32           timestamp)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);
	CORBA_Environment env;

	CORBA_exception_init (&env);

	MATE_Vertigo_MatePanelAppletShell_popup_menu (matecomponent_frame->priv->applet_shell,
						   button, timestamp, &env);
	if (MATECOMPONENT_EX (&env))
		g_warning ("Exception from popup_menu '%s'\n", env._id);

	CORBA_exception_free (&env);
}

static void
mate_panel_applet_frame_matecomponent_change_orientation (MatePanelAppletFrame *frame,
					      PanelOrientation  orientation)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);
	CORBA_unsigned_short orient = 0;

	switch (orientation) {
	case PANEL_ORIENTATION_TOP:
		orient = MATE_Vertigo_PANEL_ORIENT_DOWN;
		break;
	case PANEL_ORIENTATION_BOTTOM:
		orient = MATE_Vertigo_PANEL_ORIENT_UP;
		break;
	case PANEL_ORIENTATION_LEFT:
		orient = MATE_Vertigo_PANEL_ORIENT_RIGHT;
		break;
	case PANEL_ORIENTATION_RIGHT:
		orient = MATE_Vertigo_PANEL_ORIENT_LEFT;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	matecomponent_pbclient_set_short (matecomponent_frame->priv->property_bag,
				   PROPERTY_ORIENT,
				   orient,
				   NULL);

	gtk_widget_queue_resize (GTK_WIDGET (frame));
}

static void
mate_panel_applet_frame_matecomponent_change_size (MatePanelAppletFrame *frame,
				       guint             size)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);

	/* Normalise the size to the constants defined in the IDL. */
	size = size <= PANEL_SIZE_XX_SMALL ? PANEL_SIZE_XX_SMALL :
	       size <= PANEL_SIZE_X_SMALL  ? PANEL_SIZE_X_SMALL  :
	       size <= PANEL_SIZE_SMALL    ? PANEL_SIZE_SMALL    :
	       size <= PANEL_SIZE_MEDIUM   ? PANEL_SIZE_MEDIUM   :
	       size <= PANEL_SIZE_LARGE    ? PANEL_SIZE_LARGE    :
	       size <= PANEL_SIZE_X_LARGE  ? PANEL_SIZE_X_LARGE  : PANEL_SIZE_XX_LARGE;

	matecomponent_pbclient_set_short (matecomponent_frame->priv->property_bag,
				   PROPERTY_SIZE,
				   size,
				   NULL);
}

static void
mate_panel_applet_frame_matecomponent_change_background (MatePanelAppletFrame    *frame,
					     PanelBackgroundType  type)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (frame);
	char *bg_str;

	bg_str = _mate_panel_applet_frame_get_background_string (
			frame, PANEL_WIDGET (GTK_WIDGET (frame)->parent), type);

	if (bg_str != NULL) {
		matecomponent_pbclient_set_string (matecomponent_frame->priv->property_bag,
					    PROPERTY_BACKGROUND,
					    bg_str, NULL);

		g_free (bg_str);
	}
}

static void
mate_panel_applet_frame_matecomponent_applet_broken (MateCORBAConnection  *cnx,
					 MatePanelAppletFrame *frame)
{
	_mate_panel_applet_frame_applet_broken (frame);
}

static void
popup_handle_remove (MateComponentUIComponent *uic,
		     MatePanelAppletFrame  *frame,
		     const gchar       *verbname)
{
	_mate_panel_applet_frame_applet_remove (frame);
}

static void
popup_handle_move (MateComponentUIComponent *uic,
		   MatePanelAppletFrame  *frame,
		   const gchar       *verbname)
{
	_mate_panel_applet_frame_applet_move (frame);
}

static void
listener_popup_handle_lock (MateComponentUIComponent            *uic,
			    const char                   *path,
			    MateComponent_UIComponent_EventType  type,
			    const char                   *state,
			    gpointer                      data)
{
	MatePanelAppletFrame *frame;
	gboolean          locked;

	g_assert (!strcmp (path, "LockAppletToPanel"));

	if (type != MateComponent_UIComponent_STATE_CHANGED)
		return;

	if (!state)
		return;

	frame = (MatePanelAppletFrame *) data;
	locked = (strcmp (state, "1") == 0);

	_mate_panel_applet_frame_applet_lock (frame, locked);

	mate_panel_applet_frame_sync_menu_state (frame);
}

static MateComponentUIVerb popup_verbs [] = {
        MATECOMPONENT_UI_UNSAFE_VERB ("RemoveAppletFromPanel", popup_handle_remove),
        MATECOMPONENT_UI_UNSAFE_VERB ("MoveApplet",            popup_handle_move),

        MATECOMPONENT_UI_VERB_END
};


static void
mate_panel_applet_frame_matecomponent_finalize (GObject *object)
{
	MatePanelAppletFrameMateComponent *frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (object);

	if (frame->priv->control) {
		/* do this before unref'ing every matecomponent stuff since it looks
		 * like we can receive some events when unref'ing them */
		MateCORBA_small_unlisten_for_broken (frame->priv->control,
						 G_CALLBACK (mate_panel_applet_frame_matecomponent_applet_broken));
		matecomponent_object_release_unref (frame->priv->control, NULL);
		frame->priv->control = CORBA_OBJECT_NIL;
	}

	if (frame->priv->property_bag)
		matecomponent_object_release_unref (
			frame->priv->property_bag, NULL);

	if (frame->priv->applet_shell)
		matecomponent_object_release_unref (
			frame->priv->applet_shell, NULL);

	if (frame->priv->ui_component)
		matecomponent_object_unref (
			MATECOMPONENT_OBJECT (frame->priv->ui_component));

	G_OBJECT_CLASS (mate_panel_applet_frame_matecomponent_parent_class)->finalize (object);
}

static void
mate_panel_applet_frame_matecomponent_init (MatePanelAppletFrameMateComponent *frame)
{
	GtkWidget *container;

	frame->priv = G_TYPE_INSTANCE_GET_PRIVATE (frame,
						   PANEL_TYPE_APPLET_FRAME_MATECOMPONENT,
						   MatePanelAppletFrameMateComponentPrivate);

	frame->priv->applet_shell = CORBA_OBJECT_NIL;
	frame->priv->control      = CORBA_OBJECT_NIL;
	frame->priv->property_bag = CORBA_OBJECT_NIL;
	frame->priv->ui_component = NULL;
}

static void
mate_panel_applet_frame_matecomponent_class_init (MatePanelAppletFrameMateComponentClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);
	MatePanelAppletFrameClass *frame_class = MATE_PANEL_APPLET_FRAME_CLASS (class);

	gobject_class->finalize = mate_panel_applet_frame_matecomponent_finalize;

	frame_class->init_properties = mate_panel_applet_frame_matecomponent_init_properties;
	frame_class->sync_menu_state = mate_panel_applet_frame_matecomponent_sync_menu_state;
	frame_class->popup_menu = mate_panel_applet_frame_matecomponent_popup_menu;
	frame_class->change_orientation = mate_panel_applet_frame_matecomponent_change_orientation;
	frame_class->change_size = mate_panel_applet_frame_matecomponent_change_size;
	frame_class->change_background = mate_panel_applet_frame_matecomponent_change_background;

	g_type_class_add_private (class, sizeof (MatePanelAppletFrameMateComponentPrivate));
}

static MATE_Vertigo_MatePanelAppletShell
mate_panel_applet_frame_get_applet_shell (MateComponent_Control control)
{
	CORBA_Environment              env;
	MATE_Vertigo_MatePanelAppletShell retval;

	CORBA_exception_init (&env);

	retval = MateComponent_Unknown_queryInterface (control,
						"IDL:MATE/Vertigo/MatePanelAppletShell:1.0",
						&env);
	if (MATECOMPONENT_EX (&env)) {
		g_warning ("Unable to obtain AppletShell interface from control\n");

		retval = CORBA_OBJECT_NIL;
	}

	CORBA_exception_free (&env);

	return retval;
}

static const char* mate_panel_applet_frame_get_orient_string(MatePanelAppletFrame* frame, MatePanelAppletFrameActivating* frame_act)
{
	PanelOrientation orientation;
	const char* retval = NULL;

	orientation = mate_panel_applet_frame_activating_get_orientation(frame_act);

	switch (orientation)
	{
		case PANEL_ORIENTATION_TOP:
			retval = "down";
			break;
		case PANEL_ORIENTATION_BOTTOM:
			retval = "up";
			break;
		case PANEL_ORIENTATION_LEFT:
			retval = "right";
			break;
		case PANEL_ORIENTATION_RIGHT:
			retval = "left";
			break;
		default:
			g_assert_not_reached();
			break;
	}

	return retval;
}

static const char* mate_panel_applet_frame_get_size_string (MatePanelAppletFrame* frame, MatePanelAppletFrameActivating* frame_act)
{
	const char* retval = NULL;
	guint32 size;

	size = mate_panel_applet_frame_activating_get_size(frame_act);

	if (size <= PANEL_SIZE_XX_SMALL)
	{
		retval = "xx-small";
	}
	else if (size <= PANEL_SIZE_X_SMALL)
	{
		retval = "x-small";
	}
	else if (size <= PANEL_SIZE_SMALL)
	{
		retval = "small";
	}
	else if (size <= PANEL_SIZE_MEDIUM)
	{
		retval = "medium";
	}
	else if (size <= PANEL_SIZE_LARGE)
	{
		retval = "large";
	}
	else if (size <= PANEL_SIZE_X_LARGE)
	{
		retval = "x-large";
	}
	else
	{
		retval = "xx-large";
	}

	return retval;
}

static char *
mate_panel_applet_frame_construct_item (MatePanelAppletFrame           *frame,
				   MatePanelAppletFrameActivating *frame_act)
{
	char *retval;
	char *conf_path = NULL;
	char *bg_str = NULL;
	gboolean locked_down;

	conf_path = mate_panel_applet_frame_activating_get_conf_path (frame_act);
	//FIXME vuntz
#if 0
	bg_str = _mate_panel_applet_frame_get_background_string (
				frame, panel, panel->background.type);
#endif

	if (bg_str == NULL)
		bg_str = g_strdup ("");

	locked_down = mate_panel_applet_frame_activating_get_locked_down (frame_act);

	retval = g_strdup_printf (
			"prefs_key=%s;"
			"background=%s;orient=%s;size=%s;locked_down=%s",
			conf_path, bg_str,
			mate_panel_applet_frame_get_orient_string (frame, frame_act),
			mate_panel_applet_frame_get_size_string (frame, frame_act),
			locked_down ? "true" : "false");

	g_free (conf_path);
	g_free (bg_str);

	return retval;
}

static void
mate_panel_applet_frame_event_listener (MateComponentListener    *listener,
				   const char        *event,
				   const CORBA_any   *any,
				   CORBA_Environment *ev,
				   MatePanelAppletFrame  *frame)
{
	if (!strcmp (event, "MateComponent/Property:change:" PROPERTY_FLAGS))
		mate_panel_applet_frame_matecomponent_update_flags (frame, any);

	else if (!strcmp (event, "MateComponent/Property:change:" PROPERTY_SIZE_HINTS))
		mate_panel_applet_frame_matecomponent_update_size_hints (frame, any);
}

static void
mate_panel_applet_frame_matecomponent_activated (CORBA_Object  object,
				     const char   *error_reason,
				     gpointer      data)
{
	MatePanelAppletFrameActivating *frame_act;
	MatePanelAppletFrameMateComponent *matecomponent_frame;
	MatePanelAppletFrame   *frame;
	GtkWidget          *widget;
	MateComponentControlFrame *control_frame;
	MateComponent_Control      control;
	MateComponent_ItemContainer container;
	CORBA_Environment   corba_ev;
	AppletInfo         *info;
	char               *error;
	char               *item_name;
	GError             *gerror = NULL;

	widget = NULL;
	matecomponent_frame = MATE_PANEL_APPLET_FRAME_MATECOMPONENT (data);
	frame = MATE_PANEL_APPLET_FRAME (data);
	frame_act = g_object_get_data (G_OBJECT (frame), "mate-panel-applet-frame-activating");
	g_object_set_data (G_OBJECT (frame), "mate-panel-applet-frame-activating", NULL);

	/* according to the source of matecomponent control == NULL && no
	   exception can happen, so handle it */
	if (error_reason != NULL || object == CORBA_OBJECT_NIL) {
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, error_reason);
		goto error_out;
	}

	CORBA_exception_init (&corba_ev);

	item_name = mate_panel_applet_frame_construct_item (frame,
						       frame_act);

	matecomponent_frame->priv->control = CORBA_OBJECT_NIL;
	container = MateComponent_Unknown_queryInterface (object,
						   "IDL:MateComponent/ItemContainer:1.0",
						   &corba_ev);
	if (!MATECOMPONENT_EX (&corba_ev) && container != CORBA_OBJECT_NIL) {
		MateComponent_Unknown containee;

		containee = MateComponent_ItemContainer_getObjectByName (container,
								  item_name,
								  TRUE,
								  &corba_ev);
		matecomponent_object_release_unref (container, NULL);

		if (!MATECOMPONENT_EX (&corba_ev) && containee != CORBA_OBJECT_NIL) {
			matecomponent_frame->priv->control =
				MateComponent_Unknown_queryInterface (containee,
							       "IDL:MateComponent/Control:1.0",
							       &corba_ev);

			matecomponent_object_release_unref (containee, NULL);
		}
	}
	g_free (item_name);

	if (matecomponent_frame->priv->control == CORBA_OBJECT_NIL) {
		error = matecomponent_exception_get_text (&corba_ev);
		gerror = g_error_new (mate_panel_applet_frame_matecomponent_error_quark (), 0, "failed to get MateComponent/Control interface:\n", error);
		CORBA_exception_free (&corba_ev);
		matecomponent_object_release_unref (object, NULL);
		g_free (error);
		goto error_out;
	}

	widget = matecomponent_widget_new_control_from_objref (matecomponent_frame->priv->control,
							CORBA_OBJECT_NIL);

	CORBA_exception_free (&corba_ev);
	matecomponent_object_release_unref (object, NULL);

	if (!widget) {
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, "no widget created");
		goto error_out;
	}

	control_frame = matecomponent_widget_get_control_frame (MATECOMPONENT_WIDGET (widget));
	if (control_frame == NULL) {
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, "cannot get control frame");
		goto error_out;
	}

	matecomponent_frame->priv->property_bag =
		matecomponent_control_frame_get_control_property_bag (control_frame,
							       &corba_ev);
	if (matecomponent_frame->priv->property_bag == NULL || MATECOMPONENT_EX (&corba_ev)) {
		error = matecomponent_exception_get_text (&corba_ev);
		CORBA_exception_free (&corba_ev);
		gerror = g_error_new (mate_panel_applet_frame_matecomponent_error_quark (), 0, "cannot get property bag frame:\n%s", error);
		g_free (error);
		goto error_out;
	}

	matecomponent_event_source_client_add_listener (matecomponent_frame->priv->property_bag,
						 (MateComponentListenerCallbackFn) mate_panel_applet_frame_event_listener,
						 "MateComponent/Property:change:mate-panel-applet",
						 NULL,
						 frame);

	matecomponent_frame->priv->ui_component =
		matecomponent_control_frame_get_popup_component (control_frame,
							  &corba_ev);
	if (matecomponent_frame->priv->ui_component == NULL || MATECOMPONENT_EX (&corba_ev)) {
		error = matecomponent_exception_get_text (&corba_ev);
		CORBA_exception_free (&corba_ev);
		gerror = g_error_new (mate_panel_applet_frame_matecomponent_error_quark (), 0, "cannot get popup component:\n%s", error);
		g_free (error);
		goto error_out;
	}

	matecomponent_ui_util_set_ui (matecomponent_frame->priv->ui_component, DATADIR,
			       "MATE_Panel_Popup.xml", "panel", NULL);

	matecomponent_ui_component_add_listener (matecomponent_frame->priv->ui_component,
					  "LockAppletToPanel",
					  listener_popup_handle_lock,
					  frame);

	matecomponent_ui_component_add_verb_list_with_data (
		matecomponent_frame->priv->ui_component, popup_verbs, frame);

	control = matecomponent_control_frame_get_control (control_frame);
	if (!control) {
		CORBA_exception_free (&corba_ev);
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, "cannot get control");
		goto error_out;
	}

	matecomponent_frame->priv->applet_shell = mate_panel_applet_frame_get_applet_shell (control);
	if (matecomponent_frame->priv->applet_shell == CORBA_OBJECT_NIL) {
		CORBA_exception_free (&corba_ev);
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, "cannot get applet shell");
		goto error_out;
	}

	CORBA_exception_free (&corba_ev);

	MateCORBA_small_listen_for_broken (object,
				       G_CALLBACK (mate_panel_applet_frame_matecomponent_applet_broken),
				       frame);

	gtk_container_add (GTK_CONTAINER (frame), widget);

	goto out;

error_out:
	if (widget)
		g_object_unref (widget);
	if (!gerror)
		gerror = g_error_new_literal (mate_panel_applet_frame_matecomponent_error_quark (), 0, "unknown error");

out:
	_mate_panel_applet_frame_activated (frame, frame_act, gerror);
}

gboolean
mate_panel_applet_frame_matecomponent_load (const gchar                 *iid,
			        MatePanelAppletFrameActivating  *frame_act)
{
	MatePanelAppletFrameMateComponent *matecomponent_frame;
	MatePanelAppletFrame       *frame;
	CORBA_Environment       ev;

	g_return_val_if_fail (iid != NULL, FALSE);
	g_return_val_if_fail (frame_act != NULL, FALSE);

	if (!mate_panel_applets_manager_factory_activate (iid))
		return FALSE;

	matecomponent_frame = g_object_new (PANEL_TYPE_APPLET_FRAME_MATECOMPONENT, NULL);
	frame = MATE_PANEL_APPLET_FRAME (matecomponent_frame);
	_mate_panel_applet_frame_set_iid (frame, iid);

	g_object_set_data (G_OBJECT (frame), "mate-panel-applet-frame-activating", frame_act);

	CORBA_exception_init (&ev);

	matecomponent_activation_activate_from_id_async ((gchar *) iid, 0,
						  (MateComponentActivationCallback) mate_panel_applet_frame_matecomponent_activated,
						  frame, &ev);

	CORBA_exception_free (&ev);

	return TRUE;
}
