/*
 * mate-panel-applet-shell.h: the panel's interface to the applet.
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

#ifndef __MATE_PANEL_APPLET_SHELL_H__
#define __MATE_PANEL_APPLET_SHELL_H__

#include <matecomponent/matecomponent-object.h>

#include <mate-panel-applet.h>
#include <MATE_Panel.h>

#define MATE_PANEL_APPLET_SHELL_TYPE        (mate_panel_applet_shell_get_type ())
#define MATE_PANEL_APPLET_SHELL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), MATE_PANEL_APPLET_SHELL_TYPE, MatePanelAppletShell))
#define MATE_PANEL_APPLET_SHELL_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST    ((k), MATE_PANEL_APPLET_SHELL_TYPE, MatePanelAppletShellClass))
#define PANEL_IS_APPLET_SHELL(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), MATE_PANEL_APPLET_SHELL_TYPE))
#define PANEL_IS_APPLET_SHELL_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE    ((k), MATE_PANEL_APPLET_SHELL_TYPE))

typedef struct _MatePanelAppletShellPrivate MatePanelAppletShellPrivate;

typedef struct {
	MateComponentObject             base;

	MatePanelAppletShellPrivate *priv;
} MatePanelAppletShell;

typedef struct {
	MateComponentObjectClass                       base_class;

	POA_MATE_Vertigo_MatePanelAppletShell__epv epv;
} MatePanelAppletShellClass;


GType             mate_panel_applet_shell_get_type  (void) G_GNUC_CONST;

void              mate_panel_applet_shell_construct (MatePanelAppletShell *shell,
						MatePanelApplet      *applet);

MatePanelAppletShell *mate_panel_applet_shell_new       (MatePanelApplet      *applet);


#endif /* MATE_PANEL_APPLET_SHELL_H */
