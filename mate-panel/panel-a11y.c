/*
 * panel-a11y.c: panel accessibility support module
 *
 * Copyright (C) 2002, 2003 Sun Microsystems Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "panel-a11y.h"

gboolean
panel_a11y_get_is_a11y_enabled (GtkWidget *widget)
{
	static gboolean initialised  = FALSE;
	static gboolean a11y_enabled = FALSE;

	if (!initialised) {
		a11y_enabled = GTK_IS_ACCESSIBLE (gtk_widget_get_accessible (widget));
		initialised = TRUE;
	}

	return a11y_enabled;
}

void
panel_a11y_set_atk_name_desc (GtkWidget  *widget,
			      const char *name,
			      const char *desc)
{
	AtkObject *aobj;

	g_return_if_fail (GTK_IS_WIDGET (widget));

	if (!panel_a11y_get_is_a11y_enabled (widget))
		return;

	aobj = gtk_widget_get_accessible (widget);

	if (name)
		atk_object_set_name (aobj, name);

	if (desc)
		atk_object_set_description (aobj, desc);
}

/**
 * panel_a11y_set_atk_relation
 * @widget : The Gtk widget which is labelled by @label
 * @label : The label for the @widget.
 *
 * Description : This function establishes atk relation
 * between a gtk widget and a label.
 */
void
panel_a11y_set_atk_relation (GtkWidget *widget,
			     GtkLabel  *label)
{
	AtkObject      *aobject;
	AtkRelationSet *relation_set;
	AtkRelation    *relation;
	AtkObject      *targets [1];

	g_return_if_fail (GTK_IS_WIDGET(widget));
	g_return_if_fail (GTK_IS_LABEL(label));

	if (!panel_a11y_get_is_a11y_enabled (widget))
		return;

	aobject = gtk_widget_get_accessible (widget);

	gtk_label_set_mnemonic_widget (label, widget);

	targets [0] = gtk_widget_get_accessible (GTK_WIDGET (label));

	relation_set = atk_object_ref_relation_set (aobject);

	relation = atk_relation_new (targets, 1, ATK_RELATION_LABELLED_BY);
	atk_relation_set_add (relation_set, relation);
	g_object_unref (relation);
}

/**
 * panel_a11y_query_accessible_parent_type
 * @type: widget type
 * @type_info: accessible object type info to complete
 *
 * Standard hack which figures out the #GType of the accessible
 * object for @type's parent type. Also, fills out the class_size
 * and instance_size of @type_info to match the size of the parent
 * type.
 *
 * Basically, this is the hack to use if you want to derive from
 * an accessible object implementation in gail.
 *
 * Returns: the #GType of @type's parent's accessible peer
 */
GType
panel_a11y_query_accessible_parent_type (GType      type,
					 GTypeInfo *type_info)
{
	AtkObjectFactory *factory;
	GType             parent_type;
	GType             accessible_parent_type;

	g_return_val_if_fail (G_TYPE_IS_OBJECT (type), G_TYPE_INVALID);

	parent_type = g_type_parent (type);

	factory = atk_registry_get_factory (atk_get_default_registry (), parent_type);

	accessible_parent_type = atk_object_factory_get_accessible_type (factory);

	if (type_info) {
		GTypeQuery query;

		g_type_query (accessible_parent_type, &query);

		type_info->class_size    = query.class_size;
		type_info->instance_size = query.instance_size;
	}

	return atk_object_factory_get_accessible_type (factory);
}
