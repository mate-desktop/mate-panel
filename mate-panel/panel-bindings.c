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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>

#include "panel-bindings.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libmate-desktop/mate-gsettings.h>

#include "panel-schemas.h"
#include "panel-profile.h"
#ifdef HAVE_X11
#include "xstuff.h"
#include "panel-xutils.h"
#endif

#define DEFAULT_MOUSE_MODIFIER GDK_MOD1_MASK

typedef struct {
	char            *key;
	char            *signal;
	guint            keyval;
	GdkModifierType  modifiers;
} PanelBinding;

static gboolean initialised = FALSE;
static GSettings *marco_settings = NULL;
static GSettings *marco_keybindings_settings = NULL;

static PanelBinding bindings [] = {
	{ MARCO_ACTIVATE_WINDOW_MENU_KEY, "popup-panel-menu", 0, 0 },
	{ MARCO_TOGGLE_MAXIMIZED_KEY,     "toggle-expand",    0, 0 },
	{ MARCO_MAXIMIZE_KEY,             "expand",           0, 0 },
	{ MARCO_UNMAXIMIZE_KEY,           "unexpand",         0, 0 },
	{ MARCO_TOGGLE_SHADED_KEY,        "toggle-hidden",    0, 0 },
	{ MARCO_BEGIN_MOVE_KEY,           "begin-move",       0, 0 },
	{ MARCO_BEGIN_RESIZE_KEY,         "begin-resize",     0, 0 },
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
panel_binding_changed (GSettings *settings,
					   gchar *key,
					   PanelBinding *binding)
{
	if (binding->keyval)
		panel_binding_clear_entry (binding, NULL);

	binding->keyval    = 0;
	binding->modifiers = 0;

	gchar *value = g_settings_get_string (settings, key);
	panel_binding_set_from_string (binding, value);
	g_free (value);

	if (!binding->keyval)
		return;

	panel_binding_set_entry (binding, NULL);
}

static void
panel_binding_watch (PanelBinding *binding,
		     const char   *key)
{
	gchar *signal_name;
	signal_name = g_strdup_printf ("changed::%s", key);
	g_signal_connect (marco_keybindings_settings,
					  signal_name,
					  G_CALLBACK (panel_binding_changed),
					  binding);
	g_free (signal_name);
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
panel_bindings_mouse_modifier_changed (GSettings *settings,
									   gchar *key,
									   gpointer user_data)
{
	gchar *value = g_settings_get_string (settings, key);
	panel_bindings_mouse_modifier_set_from_string (value);
	g_free (value);
}

static void
panel_bindings_initialise (void)
{
	int          i;
	char        *str;

	if (initialised)
		return;

	if (!mate_gsettings_schema_exists (MARCO_SCHEMA)) {
		initialised = TRUE;
		return;
	}

	marco_settings = g_settings_new (MARCO_SCHEMA);
	marco_keybindings_settings = g_settings_new (MARCO_KEYBINDINGS_SCHEMA);

	for (i = 0; i < G_N_ELEMENTS (bindings); i++) {
		str = g_settings_get_string (marco_keybindings_settings, bindings [i].key);
		panel_binding_set_from_string (&bindings [i], str);
		panel_binding_watch (&bindings [i], bindings [i].key);
		g_free (str);
	}

	/* mouse button modifier */

	g_signal_connect (marco_settings,
					  "changed::" MARCO_MOUSE_BUTTON_MODIFIER_KEY,
					  G_CALLBACK (panel_bindings_mouse_modifier_changed),
					  NULL);

	str = g_settings_get_string (marco_settings, MARCO_MOUSE_BUTTON_MODIFIER_KEY);
	panel_bindings_mouse_modifier_set_from_string (str);
	g_free (str);

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
#ifdef HAVE_X11
	if (!is_using_x11 ())
		return 0;

	g_assert (mouse_button_modifier_keymask != 0);

	if (!initialised)
		panel_bindings_initialise ();

	return panel_get_real_modifier_mask (mouse_button_modifier_keymask);
#else
	return 0;
#endif
}
