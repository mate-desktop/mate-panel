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

#ifndef APPLET_SIGNALER_H
#define APPLET_SIGNALER_H

#include <glib.h>
#include "applet.h"

G_BEGIN_DECLS

/* Applet Signaler */
/* Singleton Object to handle the world of applets */

#define PANEL_TYPE_APPLET_SIGNALER (mate_panel_applet_signaler_get_type())
#define MATE_PANEL_APPLET_SIGNALER(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), PANEL_TYPE_APPLET_SIGNALER, MatePanelAppletSignaler))
#define PANEL_IS_APPLET_SIGNALER(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), PANEL_TYPE_APPLET_SIGNALER))

#define MATE_PANEL_APPLET_SIGNALER_SIGNAL_APPLET_ADDED    "applet-added"
#define MATE_PANEL_APPLET_SIGNALER_SIGNAL_APPLET_REMOVED  "applet-removed"

typedef struct _MatePanelAppletSignaler MatePanelAppletSignaler;
typedef void (*MatePanelAppletSignalerFunc)(MatePanelAppletSignaler * pas, AppletInfo * info, gpointer data);

GType mate_panel_applet_signaler_get_type (void) G_GNUC_CONST;
MatePanelAppletSignaler * mate_panel_applet_signaler_get_default (void);

void mate_panel_applet_signaler_add_applet    (AppletInfo * applet);
void mate_panel_applet_signaler_remove_applet (AppletInfo * applet);

G_END_DECLS

#endif /* APPLET_SIGNALER_H */
