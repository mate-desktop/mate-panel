/*
 * mate-panel-applets-manager-matecomponent.h
 *
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
 */

#ifndef __MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT_H__
#define __MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT_H__

#include <mate-panel-applets-manager.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT		(mate_panel_applets_manager_matecomponent_get_type ())
#define MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT, MatePanelAppletsManagerMateComponent))
#define MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT, MatePanelAppletsManagerMateComponentClass))
#define PANEL_IS_APPLETS_MANAGER_MATECOMPONENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT))
#define PANEL_IS_APPLETS_MANAGER_MATECOMPONENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT))
#define MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT, MatePanelAppletsManagerMateComponentClass))

typedef struct _MatePanelAppletsManagerMateComponent		MatePanelAppletsManagerMateComponent;
typedef struct _MatePanelAppletsManagerMateComponentClass		MatePanelAppletsManagerMateComponentClass;
typedef struct _MatePanelAppletsManagerMateComponentPrivate	MatePanelAppletsManagerMateComponentPrivate;

struct _MatePanelAppletsManagerMateComponentClass {
	MatePanelAppletsManagerClass parent_class;
};

struct _MatePanelAppletsManagerMateComponent {
	MatePanelAppletsManager parent;

	/*< private > */
	MatePanelAppletsManagerMateComponentPrivate *priv;
};

GType mate_panel_applets_manager_matecomponent_get_type (void);

#ifdef __cplusplus
}
#endif

#endif /* __MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT_H__ */
