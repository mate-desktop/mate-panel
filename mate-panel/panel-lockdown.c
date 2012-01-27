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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Matt Keenan  <matt.keenan@sun.com>
 *      Mark McLoughlin  <mark@skynet.ie>
 */

#include <config.h>

#include "panel-lockdown.h"

#include <string.h>
#include "panel-mateconf.h"

#define N_LISTENERS 7

#define PANEL_GLOBAL_LOCKDOWN_DIR    "/apps/panel/global"
#define DESKTOP_MATE_LOCKDOWN_DIR   "/desktop/mate/lockdown"
#define PANEL_GLOBAL_LOCKED_DOWN_KEY PANEL_GLOBAL_LOCKDOWN_DIR  "/locked_down"
#define DISABLE_COMMAND_LINE_KEY     DESKTOP_MATE_LOCKDOWN_DIR "/disable_command_line"
#define DISABLE_LOCK_SCREEN_KEY      DESKTOP_MATE_LOCKDOWN_DIR  "/disable_lock_screen"
#define DISABLE_LOG_OUT_KEY          PANEL_GLOBAL_LOCKDOWN_DIR  "/disable_log_out"
#define DISABLE_FORCE_QUIT_KEY       PANEL_GLOBAL_LOCKDOWN_DIR  "/disable_force_quit"
#define DISABLE_PLACES_MENU_KEY      PANEL_GLOBAL_LOCKDOWN_DIR  "/disable_places_menu"
#define DISABLED_APPLETS_KEY         PANEL_GLOBAL_LOCKDOWN_DIR  "/disabled_applets"

typedef struct {
        guint   initialized : 1;

        guint   locked_down : 1;
        guint   disable_command_line : 1;
        guint   disable_lock_screen : 1;
        guint   disable_log_out : 1;
        guint   disable_force_quit : 1;
        guint   disable_places_menu : 1;

        GSList *disabled_applets;

        guint   listeners [N_LISTENERS];

        GSList *closures;
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
locked_down_notify (MateConfClient   *client,
                    guint          cnxn_id,
                    MateConfEntry    *entry,
                    PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->locked_down = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_command_line_notify (MateConfClient   *client,
                             guint          cnxn_id,
                             MateConfEntry    *entry,
                             PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->disable_command_line = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_lock_screen_notify (MateConfClient   *client,
                            guint          cnxn_id,
                            MateConfEntry    *entry,
                            PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->disable_lock_screen = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_log_out_notify (MateConfClient   *client,
                        guint          cnxn_id,
                        MateConfEntry    *entry,
                        PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->disable_log_out = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

static void
disable_force_quit_notify (MateConfClient   *client,
                           guint          cnxn_id,
                           MateConfEntry    *entry,
                           PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->disable_force_quit = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

 static void
 disable_places_menu_notify (MateConfClient   *client,
                            guint          cnxn_id,
                            MateConfEntry    *entry,
                            PanelLockdown *lockdown)
{
        if (!entry->value || entry->value->type != MATECONF_VALUE_BOOL)
                return;

        lockdown->disable_places_menu = mateconf_value_get_bool (entry->value);

        panel_lockdown_invoke_closures (lockdown);
}

static void
disabled_applets_notify (MateConfClient   *client,
                         guint          cnxn_id,
                         MateConfEntry    *entry,
                         PanelLockdown *lockdown)
{
        GSList *l;

        if (!entry->value || entry->value->type != MATECONF_VALUE_LIST ||
            mateconf_value_get_list_type (entry->value) != MATECONF_VALUE_STRING)
                return;

        for (l = lockdown->disabled_applets; l; l = l->next)
                g_free (l->data);
        g_slist_free (lockdown->disabled_applets);
        lockdown->disabled_applets = NULL;

        for (l = mateconf_value_get_list (entry->value); l; l = l->next) {
                const char *iid = mateconf_value_get_string (l->data);

                lockdown->disabled_applets =
                        g_slist_prepend (lockdown->disabled_applets,
                                         g_strdup (iid));
        }

        panel_lockdown_invoke_closures (lockdown);
}

static gboolean
panel_lockdown_load_bool (PanelLockdown         *lockdown,
                          MateConfClient           *client,
                          const char            *key,
                          MateConfClientNotifyFunc  notify_func,
                          int                    listener)
{
        GError   *error = NULL;
        gboolean  retval;

        retval = mateconf_client_get_bool (client, key, &error);
        if (error) {
                g_warning ("Error getting value of '%s': %s\n",
                           key, error->message);
                retval = FALSE;
        }

        lockdown->listeners [listener] =
                mateconf_client_notify_add (client,
                                         key,
                                         notify_func,
                                         lockdown,
                                         NULL, NULL);

        return retval;
}

static GSList *
panel_lockdown_load_disabled_applets (PanelLockdown *lockdown,
                                      MateConfClient   *client,
                                      int            listener)
{
        GSList *retval;

        retval = mateconf_client_get_list (client,
                                        DISABLED_APPLETS_KEY,
                                        MATECONF_VALUE_STRING,
                                        NULL);

        lockdown->listeners [listener] =
                mateconf_client_notify_add (client,
                                         DISABLED_APPLETS_KEY,
                                         (MateConfClientNotifyFunc) disabled_applets_notify,
                                         lockdown,
                                         NULL, NULL);

        return retval;
}

void
panel_lockdown_init (void)
{
        MateConfClient *client;
        int          i = 0;

        client = panel_mateconf_get_client ();

        mateconf_client_add_dir (client,
                              DESKTOP_MATE_LOCKDOWN_DIR,
                              MATECONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);

        mateconf_client_add_dir (client,
                              PANEL_GLOBAL_LOCKDOWN_DIR,
                              MATECONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);

        panel_lockdown.locked_down =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          PANEL_GLOBAL_LOCKED_DOWN_KEY,
                                          (MateConfClientNotifyFunc) locked_down_notify,
                                          i++);

        panel_lockdown.disable_command_line =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          DISABLE_COMMAND_LINE_KEY,
                                          (MateConfClientNotifyFunc) disable_command_line_notify,
                                          i++);
        
        panel_lockdown.disable_lock_screen =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          DISABLE_LOCK_SCREEN_KEY,
                                          (MateConfClientNotifyFunc) disable_lock_screen_notify,
                                          i++);

        panel_lockdown.disable_log_out =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          DISABLE_LOG_OUT_KEY,
                                          (MateConfClientNotifyFunc) disable_log_out_notify,
                                          i++);

        panel_lockdown.disable_force_quit =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          DISABLE_FORCE_QUIT_KEY,
                                          (MateConfClientNotifyFunc) disable_force_quit_notify,
                                          i++);

        panel_lockdown.disable_places_menu =
                panel_lockdown_load_bool (&panel_lockdown,
                                          client,
                                          DISABLE_PLACES_MENU_KEY,
                                          (MateConfClientNotifyFunc) disable_places_menu_notify,
                                          i++);
        panel_lockdown.disabled_applets =
                panel_lockdown_load_disabled_applets (&panel_lockdown,
                                                      client,
                                                      i++);

        g_assert (i == N_LISTENERS);

        panel_lockdown.initialized = TRUE;
}

void
panel_lockdown_finalize (void)
{
        MateConfClient *client;
        GSList      *l;
        int          i;

        g_assert (panel_lockdown.initialized != FALSE);

        client = panel_mateconf_get_client ();

        for (l = panel_lockdown.disabled_applets; l; l = l->next)
                g_free (l->data);
        g_slist_free (panel_lockdown.disabled_applets);
        panel_lockdown.disabled_applets = NULL;

        for (i = 0; i < N_LISTENERS; i++) {
                if (panel_lockdown.listeners [i])
                        mateconf_client_notify_remove (client,
                                                    panel_lockdown.listeners [i]);
                panel_lockdown.listeners [i] = 0;
        }

        mateconf_client_remove_dir (client,
                                 PANEL_GLOBAL_LOCKDOWN_DIR,
                                 NULL);

        mateconf_client_remove_dir (client,
                                 DESKTOP_MATE_LOCKDOWN_DIR,
                                 NULL);

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
panel_lockdown_get_disable_places_menu (void)
{
        g_assert (panel_lockdown.initialized != FALSE);

        return panel_lockdown.disable_places_menu;
}

gboolean
panel_lockdown_is_applet_disabled (const char *iid)
{
        GSList *l;

        g_assert (panel_lockdown.initialized != FALSE);

        for (l = panel_lockdown.disabled_applets; l; l = l->next)
                if (!strcmp (l->data, iid))
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
