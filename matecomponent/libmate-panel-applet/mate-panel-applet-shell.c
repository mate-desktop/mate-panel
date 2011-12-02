/*
 * mate-panel-applet-shell.c: the panel's interface to the applet.
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

#include <config.h>

#include "mate-panel-applet-shell.h"
#include "mate-panel-applet.h"
#include "mate-panel-applet-private.h"

struct _MatePanelAppletShellPrivate {
	MatePanelApplet *applet;
};

static GObjectClass *parent_class = NULL;

static void
impl_MatePanelAppletShell_popup_menu (PortableServer_Servant  servant,
				  CORBA_long   button,
				  CORBA_long   time,
				  CORBA_Environment      *ev)
{
	MatePanelAppletShell *applet_shell;

	applet_shell = MATE_PANEL_APPLET_SHELL (matecomponent_object (servant));

	_mate_panel_applet_popup_menu (applet_shell->priv->applet, button, time);
}

static void
mate_panel_applet_shell_finalize (GObject *object)
{
	MatePanelAppletShell *shell = MATE_PANEL_APPLET_SHELL (object);

	if (shell->priv) {
		g_free (shell->priv);
		shell->priv = NULL;
	}

	parent_class->finalize (object);
}

static void
mate_panel_applet_shell_class_init (MatePanelAppletShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->epv.popup_menu = impl_MatePanelAppletShell_popup_menu;

	object_class->finalize = mate_panel_applet_shell_finalize;

	parent_class = g_type_class_peek_parent (klass);
}

static void
mate_panel_applet_shell_init (MatePanelAppletShell *shell)
{
	shell->priv = g_new0 (MatePanelAppletShellPrivate, 1);

	shell->priv->applet = NULL;
}

MATECOMPONENT_TYPE_FUNC_FULL (MatePanelAppletShell,
		       MATE_Vertigo_MatePanelAppletShell,
		       MATECOMPONENT_OBJECT_TYPE,
		       mate_panel_applet_shell)

void
mate_panel_applet_shell_construct (MatePanelAppletShell *shell,
			      MatePanelApplet      *applet)
{
	shell->priv->applet = applet;
}

MatePanelAppletShell *
mate_panel_applet_shell_new (MatePanelApplet *applet)
{
	MatePanelAppletShell *shell;

	shell = g_object_new (MATE_PANEL_APPLET_SHELL_TYPE, NULL);

	mate_panel_applet_shell_construct (shell, applet);

	return shell;
}
