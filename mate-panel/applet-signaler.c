/*
 * Fast User Switch Applet: status-manager.c
 * 
 * Copyright (C) 2008 Canonical, Ltd.
 * Authors:
 *   Ted Gould <ted@canonical.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "applet-signaler.h"

#define MATE_PANEL_APPLET_SIGNALER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), PANEL_TYPE_APPLET_SIGNALER, MatePanelAppletSignalerClass))
#define PANEL_IS_APPLET_SIGNALER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), PANEL_TYPE_APPLET_SIGNALER))
#define MATE_PANEL_APPLET_SIGNALER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS((object), PANEL_TYPE_APPLET_SIGNALER, MatePanelAppletSignalerClass))

/* Signals */
enum {
	APPLET_ADDED,
	APPLET_REMOVED,
	LAST_SIGNAL
};

struct _MatePanelAppletSignaler
{
	GObject parent;
};

typedef struct _MatePanelAppletSignalerClass
{
	GObjectClass parent_class;

	MatePanelAppletSignalerFunc applet_added;
	MatePanelAppletSignalerFunc applet_removed;
}
MatePanelAppletSignalerClass;

guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (MatePanelAppletSignaler, mate_panel_applet_signaler, G_TYPE_OBJECT);

static void
mate_panel_applet_signaler_finalize (GObject * obj)
{
	return;
}

static void
mate_panel_applet_signaler_class_init (MatePanelAppletSignalerClass * class)
{
	GObjectClass * gobj;
	gobj = G_OBJECT_CLASS(class);

	gobj->finalize = mate_panel_applet_signaler_finalize;

	/* Signals */
	signals[APPLET_ADDED] = g_signal_new (MATE_PANEL_APPLET_SIGNALER_SIGNAL_APPLET_ADDED,
	                             G_TYPE_FROM_CLASS(class),
	                             G_SIGNAL_RUN_LAST,
	                             G_STRUCT_OFFSET(MatePanelAppletSignalerClass, applet_added),
	                             NULL, NULL, /* accumulator */
	                             g_cclosure_marshal_VOID__POINTER,
	                             G_TYPE_NONE, /* Return value */
	                             1, G_TYPE_POINTER); /* Parameters */

	signals[APPLET_REMOVED] = g_signal_new (MATE_PANEL_APPLET_SIGNALER_SIGNAL_APPLET_REMOVED,
	                             G_TYPE_FROM_CLASS(class),
	                             G_SIGNAL_RUN_LAST,
	                             G_STRUCT_OFFSET(MatePanelAppletSignalerClass, applet_removed),
	                             NULL, NULL, /* accumulator */
	                             g_cclosure_marshal_VOID__POINTER,
	                             G_TYPE_NONE, /* Return value */
	                             1, G_TYPE_POINTER); /* Parameters */

	return;
}

static void
mate_panel_applet_signaler_init (MatePanelAppletSignaler * pas)
{
	return;
}

static MatePanelAppletSignaler * default_signaler = NULL;

MatePanelAppletSignaler *
mate_panel_applet_signaler_get_default (void)
{
	if (default_signaler == NULL) {
		default_signaler = g_object_new(PANEL_TYPE_APPLET_SIGNALER, NULL);
		g_object_add_weak_pointer(G_OBJECT(default_signaler),
		                          (gpointer *)&default_signaler);
	}

	return default_signaler;
}

static guint applet_count = 0;

void
mate_panel_applet_signaler_add_applet (AppletInfo * applet)
{
	/* Ensure that one is created */
	MatePanelAppletSignaler * signaler = mate_panel_applet_signaler_get_default();

	g_signal_emit(signaler, signals[APPLET_ADDED], NULL, applet);

	return;
}

void
mate_panel_applet_signaler_remove_applet (AppletInfo * applet)
{
	/* Ensure that one is created */
	MatePanelAppletSignaler * signaler = mate_panel_applet_signaler_get_default();

	g_signal_emit(signaler, signals[APPLET_REMOVED], NULL, applet);

	return;
}

