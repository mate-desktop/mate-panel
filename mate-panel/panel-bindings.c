/*
 * panel-bindings.c: panel keybindings support module
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
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

#include "panel-bindings.h"

#include <string.h>
#include <glib/gi18n.h>

#include "panel-mateconf.h"
#include "panel-profile.h"
#include "panel-xutils.h"

#define BINDINGS_PREFIX    "/apps/marco/window_keybindings"
#define MOUSE_MODIFIER_DIR "/apps/marco/general"
#define MOUSE_MODIFIER_KEY "/apps/marco/general/mouse_button_modifier"
#define DEFAULT_MOUSE_MODIFIER GDK_MOD1_MASK

typedef struct {
	char            *key;
	char            *signal;
	guint            keyval;
	GdkModifierType  modifiers;
} PanelBinding;

static gboolean initialised = FALSE;

static PanelBinding bindings [] = {
	{ "activate_window_menu", "popup-panel-menu", 0, 0 },
	{ "toggle_maximized",     "toggle-expand",    0, 0 },
	{ "maximize",             "expand",           0, 0 },
	{ "unmaximize",           "unexpand",         0, 0 },
	{ "toggle_shaded",        "toggle-hidden",    0, 0 },
	{ "begin_move",           "begin-move",       0, 0 },
	{ "begin_resize",         "begin-resize",     0, 0 },
};

static guint mouse_button_modifier_keymask = DEFAULT_MOUSE_MODIFIER;

static void
panel_binding_set_from_string (PanelBinding *binding,
			       const char   *str)
{
	g_assert (binding->keyval == 0);
	g_assert (binding->modifiers == 0);

	if (!str || !str [0] || !strcmp (str, "disabled")) {
		binding->keyval = 0;
		binding->modifiers = 0;
		return;
	}

	gtk_accelerator_parse (str, &binding->keyval, &binding->modifiers);
	if (binding->keyval == 0 && binding->modifiers == 0) {
		g_warning ("Unable to parse binding '%s'\n", str);
		return;
	}
}

static inline GtkBindingSet *
get_binding_set (GtkBindingSet *binding_set)
{
	if (!binding_set) {
		PanelToplevelClass *toplevel_class;

		toplevel_class = g_type_class_peek (PANEL_TYPE_TOPLEVEL);
		if (!toplevel_class)
			return NULL;

		g_assert (PANEL_IS_TOPLEVEL_CLASS (toplevel_class));

		binding_set = gtk_binding_set_by_class (toplevel_class);
	}

	return binding_set;
}

static void
panel_binding_clear_entry (PanelBinding  *binding,
			   GtkBindingSet *binding_set)
{
	binding_set = get_binding_set (binding_set);

        gtk_binding_entry_remove (binding_set, binding->keyval, binding->modifiers);
}

static void
panel_binding_set_entry (PanelBinding  *binding,
			 GtkBindingSet *binding_set)
{
	binding_set = get_binding_set (binding_set);

        gtk_binding_entry_add_signal (binding_set,	
				      binding->keyval,
				      binding->modifiers,
				      binding->signal,
				      0);
}

static void
panel_binding_changed (MateConfClient  *client,
		       guint         cnxn_id,
		       MateConfEntry   *entry,
		       PanelBinding *binding)
{
	MateConfValue *value;

	if (binding->keyval)
		panel_binding_clear_entry (binding, NULL);

	binding->keyval    = 0;
	binding->modifiers = 0;

	value = mateconf_entry_get_value (entry);

	if (!value || value->type != MATECONF_VALUE_STRING)
		return;

	panel_binding_set_from_string (binding, mateconf_value_get_string (value));

	if (!binding->keyval)
		return;

	panel_binding_set_entry (binding, NULL);
}

static void
panel_binding_watch (PanelBinding *binding,
		     const char   *key)
{
	GError *error = NULL;

	mateconf_client_notify_add (panel_mateconf_get_client (), key,
				(MateConfClientNotifyFunc) panel_binding_changed,
				binding, NULL, &error);
	if (error) {
		g_warning ("Error watching mateconf key '%s': %s",
			   key, error->message);
		g_error_free (error);
	}
}

static void
panel_bindings_mouse_modifier_set_from_string (const char *str)
{
	guint modifier_keysym;
	guint modifier_keymask;

	gtk_accelerator_parse (str, &modifier_keysym, &modifier_keymask);

	if (modifier_keysym == 0 && modifier_keymask == 0) {
		g_warning ("Unable to parse mouse modifier '%s'\n", str);
		return;
	}

	if (modifier_keymask)
		mouse_button_modifier_keymask = modifier_keymask;
	else
		mouse_button_modifier_keymask = DEFAULT_MOUSE_MODIFIER;
}

static void
panel_bindings_mouse_modifier_changed (MateConfClient  *client,
				       guint         cnxn_id,
				       MateConfEntry   *entry,
				       gpointer      user_data)
{
	MateConfValue *value;
	const char *str;

	value = mateconf_entry_get_value (entry);

	if (!value || value->type != MATECONF_VALUE_STRING)
		return;

	str = mateconf_value_get_string (value);
	panel_bindings_mouse_modifier_set_from_string (str);
}

static void
panel_bindings_initialise (void)
{
	MateConfClient *client;
	GError      *error;
	int          i;
	char        *str;

	if (initialised)
		return;

	client = panel_mateconf_get_client ();

	error = NULL;
	mateconf_client_add_dir (client, BINDINGS_PREFIX,
			      MATECONF_CLIENT_PRELOAD_ONELEVEL, &error);
	if (error) {
		g_warning ("Error loading mateconf directory '%s': %s",
			   BINDINGS_PREFIX, error->message),
		g_error_free (error);
	}

	for (i = 0; i < G_N_ELEMENTS (bindings); i++) {
		const char *key;

		key = panel_mateconf_sprintf ("%s/%s", BINDINGS_PREFIX, bindings [i].key);

		error = NULL;
		str = mateconf_client_get_string (client, key, &error);
		if (error) {
			g_warning ("Error getting value for '%s': %s",
				   key, error->message);
			g_error_free (error);
			continue;
		}

		panel_binding_set_from_string (&bindings [i], str);
		panel_binding_watch (&bindings [i], key);

		g_free (str);
	}

	/* mouse button modifier */
	error = NULL;
	mateconf_client_add_dir (client, MOUSE_MODIFIER_DIR,
			      MATECONF_CLIENT_PRELOAD_NONE, &error);
	if (error) {
		g_warning ("Error loading mateconf directory '%s': %s",
			   MOUSE_MODIFIER_DIR, error->message),
		g_error_free (error);
	}

	error = NULL;
	mateconf_client_notify_add (client, MOUSE_MODIFIER_KEY,
				 panel_bindings_mouse_modifier_changed,
				 NULL, NULL, &error);
	if (error) {
		g_warning ("Error watching mateconf key '%s': %s",
			   MOUSE_MODIFIER_KEY, error->message);
		g_error_free (error);
	}

	error = NULL;
	str = mateconf_client_get_string (client, MOUSE_MODIFIER_KEY, &error);
	if (error) {
		g_warning ("Error getting value for '%s': %s",
			   MOUSE_MODIFIER_KEY, error->message);
		g_error_free (error);
	} else {
		panel_bindings_mouse_modifier_set_from_string (str);
		g_free (str);
	}

	initialised = TRUE;
}

void
panel_bindings_set_entries (GtkBindingSet *binding_set)
{
	int i;

	if (!initialised)
		panel_bindings_initialise ();

	for (i = 0; i < G_N_ELEMENTS (bindings); i++) {
		if (!bindings [i].keyval)
			continue;

		panel_binding_set_entry (&bindings [i], binding_set);
	}
}

guint
panel_bindings_get_mouse_button_modifier_keymask (void)
{
	g_assert (mouse_button_modifier_keymask != 0);

	if (!initialised)
		panel_bindings_initialise ();

	return panel_get_real_modifier_mask (mouse_button_modifier_keymask);
}
