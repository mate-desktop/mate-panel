/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004 Sun Microsystems, Inc.
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
 *      Matt Keenan  <matt.keenan@sun.com>
 *      Mark McLoughlin  <mark@skynet.ie>
 */

#include <config.h>

#include "panel-lockdown.h"

#include <string.h>
#include <gio/gio.h>
#include "panel-schemas.h"

typedef struct {
        guint   initialized : 1;

        guint   locked_down : 1;
        guint   disable_command_line : 1;
        guint   disable_lock_screen : 1;
        guint   disable_log_out : 1;
        guint   disable_force_quit : 1;

        gchar **disabled_applets;

        GSList *closures;

        GSettings *panel_settings;
        GSettings *lockdown_settings;
} PanelLockdown;

static PanelLockdown panel_lockdown = { 0, };


static inline void
panel_lockdown_invoke_closures (PanelLockdown *lockdown)
{
        GSList *l;

        for (l = lockdown->closures; l; l = l->next)
                g_closure_invoke (l->data, NULL, 0, NULL, NULL);
}

static void
locked_down_notify (GSettings     *settings,
                    gchar         *key,
                    PanelLockdown *lockdown)
{
        lockdown->locked_down = g_settings_get_boolean (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_command_line_notify (GSettings     *settings,
                             gchar         *key,
                             PanelLockdown *lockdown)
{
        lockdown->disable_command_line = g_settings_get_boolean (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_lock_screen_notify (GSettings     *settings,
                            gchar         *key,
                            PanelLockdown *lockdown)
{
        lockdown->disable_lock_screen = g_settings_get_boolean (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_log_out_notify (GSettings     *settings,
                        gchar         *key,
                        PanelLockdown *lockdown)
{
        lockdown->disable_log_out = g_settings_get_boolean (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_force_quit_notify (GSettings     *settings,
                           gchar         *key,
                           PanelLockdown *lockdown)
{
        lockdown->disable_force_quit = g_settings_get_boolean (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static void
disabled_applets_notify (GSettings     *settings,
                         gchar         *key,
                         PanelLockdown *lockdown)
{
        g_strfreev (lockdown->disabled_applets);
        lockdown->disabled_applets = g_settings_get_strv (settings, key);
        panel_lockdown_invoke_closures (lockdown);
}

static gboolean
panel_lockdown_load_bool (PanelLockdown         *lockdown,
                          GSettings             *settings,
                          const char            *key,
                          GCallback              notify_func)
{
        gboolean  retval;
        gchar *signal_name;

        retval = g_settings_get_boolean (settings, key);

        signal_name = g_strdup_printf ("changed::%s", key);

        g_signal_connect (settings,
                          signal_name,
                          G_CALLBACK (notify_func),
                          lockdown);

        g_free (signal_name);

        return retval;
}

static gchar **
panel_lockdown_load_disabled_applets (PanelLockdown *lockdown,
                                      GSettings     *settings)
{
        gchar **retval;

        retval = g_settings_get_strv (settings,
                                      PANEL_DISABLED_APPLETS_KEY);

        g_signal_connect (settings,
                          "changed::" PANEL_DISABLED_APPLETS_KEY,
                          G_CALLBACK (disabled_applets_notify),
                          lockdown);

        return retval;
}

void
panel_lockdown_init (void)
{
        panel_lockdown.panel_settings = g_settings_new (PANEL_SCHEMA);
        panel_lockdown.lockdown_settings = g_settings_new (LOCKDOWN_SCHEMA);

        panel_lockdown.locked_down =
                panel_lockdown_load_bool (&panel_lockdown,
                                          panel_lockdown.panel_settings,
                                          PANEL_LOCKED_DOWN_KEY,
                                          G_CALLBACK (locked_down_notify));

        panel_lockdown.disable_command_line =
                panel_lockdown_load_bool (&panel_lockdown,
                                          panel_lockdown.lockdown_settings,
                                          LOCKDOWN_DISABLE_COMMAND_LINE_KEY,
                                          G_CALLBACK (disable_command_line_notify));

        panel_lockdown.disable_lock_screen =
                panel_lockdown_load_bool (&panel_lockdown,
                                          panel_lockdown.lockdown_settings,
                                          LOCKDOWN_DISABLE_LOCK_SCREEN_KEY,
                                          G_CALLBACK (disable_lock_screen_notify));

        panel_lockdown.disable_log_out =
                panel_lockdown_load_bool (&panel_lockdown,
                                          panel_lockdown.lockdown_settings,
                                          LOCKDOWN_DISABLE_LOG_OUT_KEY,
                                          G_CALLBACK (disable_log_out_notify));

        panel_lockdown.disable_force_quit =
                panel_lockdown_load_bool (&panel_lockdown,
                                          panel_lockdown.panel_settings,
                                          PANEL_DISABLE_FORCE_QUIT_KEY,
                                          G_CALLBACK (disable_force_quit_notify));

        panel_lockdown.disabled_applets =
                panel_lockdown_load_disabled_applets (&panel_lockdown,
                                                      panel_lockdown.panel_settings);

        panel_lockdown.initialized = TRUE;
}

void
panel_lockdown_finalize (void)
{
        GSList *l;

        g_assert (panel_lockdown.initialized != FALSE);

        if (panel_lockdown.disabled_applets) {
                g_strfreev (panel_lockdown.disabled_applets);
                panel_lockdown.disabled_applets = NULL;
        }

        if (panel_lockdown.panel_settings) {
                g_object_unref (panel_lockdown.panel_settings);
                panel_lockdown.panel_settings = NULL;
        }
        if (panel_lockdown.lockdown_settings) {
                g_object_unref (panel_lockdown.lockdown_settings);
                panel_lockdown.lockdown_settings = NULL;
        }

        for (l = panel_lockdown.closures; l; l = l->next)
                g_closure_unref (l->data);
        g_slist_free (panel_lockdown.closures);
        panel_lockdown.closures = NULL;

        panel_lockdown.initialized = FALSE;
}

gboolean
panel_lockdown_get_locked_down (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.locked_down;
}

gboolean
panel_lockdown_get_disable_command_line (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.disable_command_line;
}

gboolean
panel_lockdown_get_disable_lock_screen (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.disable_lock_screen;
}

gboolean
panel_lockdown_get_disable_log_out (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.disable_log_out;
}

gboolean
panel_lockdown_get_disable_force_quit (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.disable_force_quit;
}

gboolean
panel_lockdown_is_applet_disabled (const char *iid)
{
        gint i;

        g_assert (panel_lockdown.initialized != FALSE);

        if (panel_lockdown.disabled_applets)
                for (i = 0; panel_lockdown.disabled_applets[i]; i++)
                        if (!strcmp (panel_lockdown.disabled_applets[i], iid))
                                return TRUE;

        return FALSE;
}

static GClosure *
panel_lockdown_notify_find (GSList    *closures,
                            GCallback  callback_func,
                            gpointer   user_data)
{
        GSList *l;

        for (l = closures; l; l = l->next) {
                GCClosure *cclosure = l->data;
                GClosure  *closure  = l->data;

                if (closure->data == user_data &&
                    cclosure->callback == callback_func)
                        return closure;
        }

        return NULL;
}

static void
marshal_user_data (GClosure     *closure,
                   GValue       *return_value,
                   guint         n_param_values,
                   const GValue *param_values,
                   gpointer      invocation_hint,
                   gpointer      marshal_data)
{
        GCClosure *cclosure = (GCClosure*) closure;

        g_return_if_fail (cclosure->callback != NULL);
        g_return_if_fail (n_param_values == 0);

        ((void (*) (gpointer *))cclosure->callback) (closure->data);
}

void
panel_lockdown_notify_add (GCallback callback_func,
                           gpointer  user_data)
{
        GClosure *closure;

        g_assert (panel_lockdown_notify_find (panel_lockdown.closures,
                                              callback_func,
                                              user_data) == NULL);

        closure = g_cclosure_new (callback_func, user_data, NULL);
        g_closure_set_marshal (closure, marshal_user_data);

        panel_lockdown.closures = g_slist_append (panel_lockdown.closures,
                                                  closure);
}

void
panel_lockdown_notify_remove (GCallback callback_func,
                              gpointer  user_data)
{
        GClosure *closure;

        closure = panel_lockdown_notify_find (panel_lockdown.closures,
                                              callback_func,
                                              user_data);

        g_assert (closure != NULL);

        panel_lockdown.closures = g_slist_remove (panel_lockdown.closures,
                                                  closure);

        g_closure_unref (closure);
}
