/*
 * mate-panel-applets-manager-matecomponent.c
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

#include <config.h>

#include <string.h>
#include <libmatecomponent.h>

#include <mate-panel-applets-manager.h>

#include "mate-panel-applet-frame-matecomponent.h"

#include "mate-panel-applets-manager-matecomponent.h"

G_DEFINE_DYNAMIC_TYPE (MatePanelAppletsManagerMateComponent, mate_panel_applets_manager_matecomponent, PANEL_TYPE_APPLETS_MANAGER);

struct _MatePanelAppletsManagerMateComponentPrivate
{
	GHashTable *applets;
};

static const char applet_requirements [] =
	"has_all (repo_ids, ['IDL:MateComponent/Control:1.0',"
	"		     'IDL:MATE/Vertigo/MatePanelAppletShell:1.0']) && "
	"defined (panel:icon)";

static char *applet_sort_criteria [] = {
	"name",
	NULL
	};

static void
mate_panel_applets_manager_matecomponent_load_applet_infos (MatePanelAppletsManagerMateComponent *manager)
{
	MateComponent_ServerInfoList *applet_list;
	CORBA_Environment      env;
	const char * const    *langs;
	GSList                *langs_gslist;
	int                    i;

	CORBA_exception_init (&env);

	applet_list = matecomponent_activation_query (applet_requirements,
					       applet_sort_criteria,
					       &env);
	if (MATECOMPONENT_EX (&env)) {
		g_warning ("MateComponent query returned exception %s\n",
			   MATECOMPONENT_EX_REPOID (&env));

		CORBA_exception_free (&env);
		CORBA_free (applet_list);

		return;
	}

	CORBA_exception_free (&env);

	langs = g_get_language_names ();

	langs_gslist = NULL;
	for (i = 0; langs[i]; i++)
		langs_gslist = g_slist_prepend (langs_gslist, (char *) langs[i]);

	langs_gslist = g_slist_reverse (langs_gslist);

	for (i = 0; i < applet_list->_length; i++) {
		MateComponent_ServerInfo *info;
		const char *name, *description, *icon;
		MatePanelAppletInfo *applet_info;

		info = &applet_list->_buffer[i];

		name = matecomponent_server_info_prop_lookup (info,
						       "name",
						       langs_gslist);
		description = matecomponent_server_info_prop_lookup (info,
							      "description",
							      langs_gslist);
		icon = matecomponent_server_info_prop_lookup (info,
						       "panel:icon",
						       NULL);

		applet_info = mate_panel_applet_info_new (info->iid, name, description, icon, NULL);

		g_hash_table_insert (manager->priv->applets, g_strdup (info->iid), applet_info);
	}

	g_slist_free (langs_gslist);
	CORBA_free (applet_list);
}

static GList *
mate_panel_applets_manager_matecomponent_get_applets (MatePanelAppletsManager *manager)
{
	MatePanelAppletsManagerMateComponent *matecomponent_manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (manager);

	GHashTableIter iter;
	gpointer       key, value;
	GList         *retval = NULL;

	g_hash_table_iter_init (&iter, matecomponent_manager->priv->applets);
	while (g_hash_table_iter_next (&iter, &key, &value))
		retval = g_list_prepend (retval, value);

	return g_list_reverse (retval);;
}

static gboolean
mate_panel_applets_manager_matecomponent_factory_activate (MatePanelAppletsManager *manager,
					       const gchar         *iid)
{
	MatePanelAppletsManagerMateComponent *matecomponent_manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (manager);
	MatePanelAppletInfo *info;

	/* we let matecomponent deal with that, but we need to return the right value */

	info = g_hash_table_lookup (matecomponent_manager->priv->applets, iid);

	return (info != NULL);
}

static gboolean
mate_panel_applets_manager_matecomponent_factory_deactivate (MatePanelAppletsManager *manager,
						 const gchar         *iid)
{
	MatePanelAppletsManagerMateComponent *matecomponent_manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (manager);
	MatePanelAppletInfo *info;

	/* we let matecomponent deal with that, but we need to return the right value */

	info = g_hash_table_lookup (matecomponent_manager->priv->applets, iid);

	return (info != NULL);
}

static MatePanelAppletInfo *
mate_panel_applets_manager_matecomponent_get_applet_info (MatePanelAppletsManager *manager,
					      const gchar         *iid)
{
	MatePanelAppletsManagerMateComponent *matecomponent_manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (manager);

	return g_hash_table_lookup (matecomponent_manager->priv->applets, iid);
}

static MatePanelAppletInfo *
mate_panel_applets_manager_matecomponent_get_applet_info_from_old_id (MatePanelAppletsManager *manager,
							  const gchar         *iid)
{
	MatePanelAppletsManagerMateComponent *matecomponent_manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (manager);

	return g_hash_table_lookup (matecomponent_manager->priv->applets, iid);
}

static gboolean
mate_panel_applets_manager_matecomponent_load_applet (MatePanelAppletsManager         *manager,
					const gchar                 *iid,
					MatePanelAppletFrameActivating  *frame_act)
{
	return mate_panel_applet_frame_matecomponent_load (iid, frame_act);
}

static void
mate_panel_applets_manager_matecomponent_finalize (GObject *object)
{
	MatePanelAppletsManagerMateComponent *manager = MATE_PANEL_APPLETS_MANAGER_MATECOMPONENT (object);

	if (manager->priv->applets) {
		g_hash_table_destroy (manager->priv->applets);
		manager->priv->applets = NULL;
	}

	G_OBJECT_CLASS (mate_panel_applets_manager_matecomponent_parent_class)->finalize (object);
}

static void
mate_panel_applets_manager_matecomponent_init (MatePanelAppletsManagerMateComponent *manager)
{
	manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
						     PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT,
						     MatePanelAppletsManagerMateComponentPrivate);

	manager->priv->applets = g_hash_table_new_full (g_str_hash,
								 g_str_equal,
								 (GDestroyNotify) g_free,
								 (GDestroyNotify) mate_panel_applet_info_free);

	mate_panel_applets_manager_matecomponent_load_applet_infos (manager);
}

static void
mate_panel_applets_manager_matecomponent_class_finalize (MatePanelAppletsManagerMateComponentClass *class)
{
}

static void
mate_panel_applets_manager_matecomponent_class_init (MatePanelAppletsManagerMateComponentClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);
	MatePanelAppletsManagerClass *manager_class = MATE_PANEL_APPLETS_MANAGER_CLASS (class);

	/* This is a horrible hack: we shouldn't call matecomponent_init() here, but
	 * in g_io_module_load() or even
	 * mate_panel_applets_manager_matecomponent_register(). However, it looks like if
	 * there's no giomodule.cache file, the module gets unloaded, and
	 * matecomponent_init() gets called twice, which makes everythings goes wrong:
	 * matecomponent has been unloaded so believes it has to get initialized, but
	 * the types are already registered in the GType system. And bam. */

	matecomponent_init (NULL, NULL);

	gobject_class->finalize = mate_panel_applets_manager_matecomponent_finalize;

	manager_class->get_applets = mate_panel_applets_manager_matecomponent_get_applets;
	manager_class->factory_activate = mate_panel_applets_manager_matecomponent_factory_activate;
	manager_class->factory_deactivate = mate_panel_applets_manager_matecomponent_factory_deactivate;
	manager_class->get_applet_info = mate_panel_applets_manager_matecomponent_get_applet_info;
	manager_class->get_applet_info_from_old_id = mate_panel_applets_manager_matecomponent_get_applet_info_from_old_id;
	manager_class->load_applet = mate_panel_applets_manager_matecomponent_load_applet;

	g_type_class_add_private (class, sizeof (MatePanelAppletsManagerMateComponentPrivate));
}


void
mate_panel_applets_manager_matecomponent_register (GIOModule *module)
{
	mate_panel_applets_manager_matecomponent_register_type (G_TYPE_MODULE (module));
	g_io_extension_point_implement (MATE_PANEL_APPLETS_MANAGER_EXTENSION_POINT_NAME,
					PANEL_TYPE_APPLETS_MANAGER_MATECOMPONENT,
					"matecomponent",
					10);
}
