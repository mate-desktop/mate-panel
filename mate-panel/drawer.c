/*
 * MATE panel drawer module.
 * (C) 1997 The Free Software Foundation
 *
 * Authors: Miguel de Icaza
 *          Federico Mena
 *          George Lebl
 */


#include <config.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#include "drawer.h"
#include "drawer-private.h"

#include "applet.h"
#include "button-widget.h"
#include "panel-config-global.h"
#include "panel-profile.h"
#include "panel-util.h"
#include "panel-globals.h"
#include "panel-lockdown.h"
#include "panel-icon-names.h"
#include "panel-schemas.h"


/* Internal functions */
/* event handlers */


static void
drawer_click (GtkWidget *widget,
              Drawer    *drawer)
{
    if (!panel_toplevel_get_is_hidden (drawer->toplevel))
        panel_toplevel_hide (drawer->toplevel, FALSE, -1);
    else
        panel_toplevel_unhide (drawer->toplevel);
}

static void
drawer_focus_panel_widget (Drawer           *drawer,
                           GtkDirectionType  direction)
{
    PanelWidget *panel_widget;

    panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

    gtk_window_present (GTK_WINDOW (drawer->toplevel));
    gtk_container_set_focus_child (GTK_CONTAINER (panel_widget), NULL);
    gtk_widget_child_focus (GTK_WIDGET (panel_widget), direction);
}

static gboolean
key_press_drawer (GtkWidget   *widget,
                  GdkEventKey *event,
                  Drawer      *drawer)
{
    gboolean retval = TRUE;
    GtkOrientation orient;

    if (event->state & gtk_accelerator_get_default_mod_mask ())
        return FALSE;

    orient = PANEL_WIDGET (gtk_widget_get_parent (drawer->button))->orient;

    switch (event->keyval) {
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        if (orient == GTK_ORIENTATION_HORIZONTAL) {
            if (!panel_toplevel_get_is_hidden (drawer->toplevel))
                drawer_focus_panel_widget (drawer, GTK_DIR_TAB_BACKWARD);
        } else {
            /* let default focus movement happen */
            retval = FALSE;
        }
        break;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
        if (orient == GTK_ORIENTATION_VERTICAL) {
            if (!panel_toplevel_get_is_hidden (drawer->toplevel))
                drawer_focus_panel_widget (drawer, GTK_DIR_TAB_BACKWARD);
        } else {
            /* let default focus movement happen */
            retval = FALSE;
        }
        break;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
        if (orient == GTK_ORIENTATION_HORIZONTAL) {
            if (!panel_toplevel_get_is_hidden (drawer->toplevel))
                drawer_focus_panel_widget (drawer, GTK_DIR_TAB_FORWARD);
        } else {
            /* let default focus movement happen */
            retval = FALSE;
        }
        break;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
        if (orient == GTK_ORIENTATION_VERTICAL) {
            if (!panel_toplevel_get_is_hidden (drawer->toplevel))
                drawer_focus_panel_widget (drawer, GTK_DIR_TAB_FORWARD);
        } else {
            /* let default focus movement happen */
            retval = FALSE;
        }
        break;
    case GDK_KEY_Escape:
        panel_toplevel_hide (drawer->toplevel, FALSE, -1);
        break;
    default:
        retval = FALSE;
        break;
    }

    return retval;
}

/*
 * This function implements Esc moving focus from the drawer to the drawer
 * icon and closing the drawer and Shift+Esc moving focus from the drawer
 * to the drawer icon without closing the drawer when focus is in the drawer.
 */
static gboolean
key_press_drawer_widget (GtkWidget   *widget,
                         GdkEventKey *event,
                         Drawer      *drawer)
{
    PanelWidget *panel_widget;

    if (event->keyval != GDK_KEY_Escape)
        return FALSE;

    panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

    gtk_window_present (GTK_WINDOW (panel_widget->toplevel));

    if ((event->state & gtk_accelerator_get_default_mod_mask ()) == GDK_SHIFT_MASK ||
        panel_toplevel_get_is_hidden (drawer->toplevel))
            return TRUE;

    panel_toplevel_hide (drawer->toplevel, FALSE, -1);

    return TRUE;
}

    /* drag and drop handlers */

static void
drag_data_get_cb (GtkWidget          *widget,
                  GdkDragContext     *context,
                  GtkSelectionData   *selection_data,
                  guint               info,
                  guint               time,
                  Drawer             *drawer)
{
    char *foo;

    foo = g_strdup_printf ("DRAWER:%d", panel_find_applet_index (widget));

    gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data), 8, (guchar *) foo, strlen (foo));

    g_free (foo);
}

static gboolean
drag_motion_cb (GtkWidget          *widget,
                GdkDragContext     *context,
                int                 x,
                int                 y,
                guint               time_,
                Drawer             *drawer)
{
    PanelWidget *panel_widget;
    guint        info = 0;

    if (!panel_check_dnd_target_data (widget, context, &info, NULL))
        return FALSE;

    panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

    if (!panel_check_drop_forbidden (panel_widget, context, info, time_))
        return FALSE;

    if (drawer->close_timeout_id)
        g_source_remove (drawer->close_timeout_id);

    drawer->close_timeout_id = 0;

    button_widget_set_dnd_highlight (BUTTON_WIDGET (widget), TRUE);

    if (panel_toplevel_get_is_hidden (drawer->toplevel)) {
        panel_toplevel_unhide (drawer->toplevel);
        drawer->opened_for_drag = TRUE;
    }

    return TRUE;
}

static gboolean
drag_drop_cb (GtkWidget      *widget,
              GdkDragContext *context,
              int             x,
              int             y,
              guint           time_,
              Drawer         *drawer)
{
    GdkAtom atom = NULL;

    if (!panel_check_dnd_target_data (widget, context, NULL, &atom))
        return FALSE;

    gtk_drag_get_data (widget, context, atom, time_);

    return TRUE;
}

static void
drag_data_received_cb (GtkWidget          *widget,
                       GdkDragContext     *context,
                       gint                x,
                       gint                y,
                       GtkSelectionData   *selection_data,
                       guint               info,
                       guint               time_,
                       Drawer             *drawer)
{
    PanelWidget *panel_widget;

    if (!panel_check_dnd_target_data (widget, context, &info, NULL)) {
        gtk_drag_finish (context, FALSE, FALSE, time_);
        return;
    }

    panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

    panel_receive_dnd_data (panel_widget, info, -1, selection_data, context, time_);
}

static gboolean
close_drawer_in_idle (gpointer data)
{
    Drawer *drawer = (Drawer *) data;

    drawer->close_timeout_id = 0;

    if (drawer->opened_for_drag) {
        panel_toplevel_hide (drawer->toplevel, FALSE, -1);
        drawer->opened_for_drag = FALSE;
    }

    return FALSE;
}

static void
queue_drawer_close_for_drag (Drawer *drawer)
{
    if (!drawer->close_timeout_id)
        drawer->close_timeout_id = g_timeout_add_seconds (1, close_drawer_in_idle, drawer);
}

static void
drag_leave_cb (GtkWidget      *widget,
               GdkDragContext *context,
               guint           time_,
               Drawer         *drawer)
{
    queue_drawer_close_for_drag (drawer);

    button_widget_set_dnd_highlight (BUTTON_WIDGET (widget), FALSE);
}

    /* load_drawer_applet handlers */

static void
drawer_button_size_allocated (GtkWidget     *widget,
                              GtkAllocation *alloc,
                              Drawer        *drawer)
{
    if (!gtk_widget_get_realized (widget))
        return;

    gtk_widget_queue_resize (GTK_WIDGET (drawer->toplevel));

    g_object_set_data (G_OBJECT (widget), "allocated", GINT_TO_POINTER (TRUE));
}

static gboolean
drawer_changes_enabled (void)
{
    return !panel_lockdown_get_locked_down ();
}

    /* gsettings handlers */

static void
panel_drawer_custom_icon_changed (GSettings *settings,
                                  gchar     *key,
                                  Drawer    *drawer)
{
    g_return_if_fail (drawer != NULL);
    g_return_if_fail (drawer->button != NULL);

    gboolean use_custom_icon = g_settings_get_boolean (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY);
    char *custom_icon = g_settings_get_string (settings, PANEL_OBJECT_CUSTOM_ICON_KEY);

    if (use_custom_icon && custom_icon != NULL && custom_icon [0] != '\0') {
        button_widget_set_icon_name (BUTTON_WIDGET (drawer->button), custom_icon);
    } else {
        button_widget_set_icon_name (BUTTON_WIDGET (drawer->button), PANEL_ICON_DRAWER);
    }

    g_free (custom_icon);
}

static void
panel_drawer_tooltip_changed (GSettings *settings,
                              gchar     *key,
                              Drawer    *drawer)
{
    gchar *tooltip = g_settings_get_string (settings, key);
    set_tooltip_and_name (drawer, tooltip);
    g_free (tooltip);
}

    /* destroy handlers */

static void
toplevel_destroyed (GtkWidget *widget,
                    Drawer    *drawer)
{
    drawer->toplevel = NULL;

    if (drawer->button) {
        gtk_widget_destroy (drawer->button);
        drawer->button = NULL;
    }
}

static void
destroy_drawer (GtkWidget *widget,
                Drawer    *drawer)
{
    if (drawer->toplevel) {
        gtk_widget_destroy (GTK_WIDGET (drawer->toplevel));
        drawer->toplevel = NULL;
    }

    if (drawer->close_timeout_id) {
        g_source_remove (drawer->close_timeout_id);
        drawer->close_timeout_id = 0;
    }
}

static void
drawer_deletion_response (GtkWidget   *dialog,
                          int          response,
                          Drawer      *drawer)
{
    if (response == GTK_RESPONSE_OK)
        panel_profile_delete_object (drawer->info);

    gtk_widget_destroy (dialog);
}

/* end event handlers */

static PanelToplevel *
create_drawer_toplevel (const char *drawer_id,
                        GSettings  *settings)
{
    PanelToplevel *toplevel;
    char          *toplevel_id;

    toplevel_id = panel_profile_find_new_id (PANEL_GSETTINGS_TOPLEVELS);
    toplevel = panel_profile_load_toplevel (toplevel_id);

    if (!toplevel) {
        g_free (toplevel_id);
        return NULL;
    }

    g_settings_set_string (settings, PANEL_OBJECT_ATTACHED_TOPLEVEL_ID_KEY, toplevel_id);
    g_free (toplevel_id);

    panel_profile_set_toplevel_enable_buttons (toplevel, TRUE);
    panel_profile_set_toplevel_enable_arrows (toplevel, TRUE);

    return toplevel;
}

static void
set_tooltip_and_name (Drawer     *drawer,
                      const char *tooltip)
{
    g_return_if_fail (drawer != NULL);
    g_return_if_fail (drawer->toplevel != NULL);

    if (tooltip != NULL && tooltip [0] != '\0') {
        panel_toplevel_set_name (drawer->toplevel, tooltip);
        panel_util_set_tooltip_text (drawer->button, tooltip);
    }
}

static Drawer *
create_drawer_applet (PanelToplevel    *toplevel,
                      PanelToplevel    *parent_toplevel,
                      const char       *tooltip,
                      const char       *custom_icon,
                      gboolean          use_custom_icon,
                      PanelOrientation  orientation)
{
    Drawer *drawer;
    AtkObject *atk_obj;

    drawer = g_new0 (Drawer, 1);

    drawer->toplevel = toplevel;

    if (!use_custom_icon || !custom_icon || !custom_icon [0]) {
        drawer->button = button_widget_new (PANEL_ICON_DRAWER, TRUE, orientation);
    } else {
        drawer->button = button_widget_new (custom_icon, TRUE, orientation);
    }

    if (!drawer->button) {
        g_free (drawer);
        return NULL;
    }

    atk_obj = gtk_widget_get_accessible (drawer->button);
    atk_object_set_name (atk_obj, _("Drawer"));

    set_tooltip_and_name (drawer, tooltip);

    g_signal_connect (drawer->button, "clicked", G_CALLBACK (drawer_click), drawer);
    g_signal_connect (drawer->button, "key_press_event", G_CALLBACK (key_press_drawer), drawer);
    g_signal_connect (drawer->toplevel, "key_press_event", G_CALLBACK (key_press_drawer_widget), drawer);


    gtk_drag_dest_set (drawer->button, 0, NULL, 0, 0);

    g_signal_connect (drawer->button, "drag_data_get", G_CALLBACK (drag_data_get_cb), drawer);
    g_signal_connect (drawer->button, "drag_motion", G_CALLBACK (drag_motion_cb), drawer);
    g_signal_connect (drawer->button, "drag_drop", G_CALLBACK (drag_drop_cb), drawer);
    g_signal_connect (drawer->button, "drag_data_received", G_CALLBACK (drag_data_received_cb), drawer);
    g_signal_connect (drawer->button, "drag_leave", G_CALLBACK (drag_leave_cb), drawer);


    g_signal_connect (drawer->button, "destroy", G_CALLBACK (destroy_drawer), drawer);
    g_signal_connect (drawer->toplevel, "destroy", G_CALLBACK (toplevel_destroyed), drawer);

    gtk_widget_show (drawer->button);

    panel_toplevel_attach_to_widget (drawer->toplevel, parent_toplevel, GTK_WIDGET (drawer->button));

    return drawer;
}

static void
panel_drawer_connect_to_gsettings (Drawer *drawer)
{
    g_signal_connect (drawer->info->settings,
                      "changed::" PANEL_OBJECT_USE_CUSTOM_ICON_KEY,
                      G_CALLBACK (panel_drawer_custom_icon_changed),
                      drawer);

    g_signal_connect (drawer->info->settings,
                      "changed::" PANEL_OBJECT_CUSTOM_ICON_KEY,
                      G_CALLBACK (panel_drawer_custom_icon_changed),
                      drawer);

    g_signal_connect (drawer->info->settings,
                      "changed::" PANEL_OBJECT_TOOLTIP_KEY,
                      G_CALLBACK (panel_drawer_tooltip_changed),
                      drawer);
}

static void
load_drawer_applet (char          *toplevel_id,
                    GSettings     *settings,
                    const char    *custom_icon,
                    gboolean       use_custom_icon,
                    const char    *tooltip,
                    PanelToplevel *parent_toplevel,
                    gboolean       locked,
                    int            pos,
                    gboolean       exactpos,
                    const char    *id)
{
    PanelOrientation  orientation;
    PanelToplevel    *toplevel = NULL;
    Drawer           *drawer = NULL;
    PanelWidget      *panel_widget;

    orientation = panel_toplevel_get_orientation (parent_toplevel);

    if (toplevel_id)
        toplevel = panel_profile_get_toplevel_by_id (toplevel_id);

    if (!toplevel)
        toplevel = create_drawer_toplevel (id, settings);

    if (toplevel) {
        panel_toplevel_hide (toplevel, FALSE, -1);
        drawer = create_drawer_applet (toplevel,
                                       parent_toplevel,
                                       tooltip,
                                       custom_icon,
                                       use_custom_icon,
                                       orientation);
    }

    if (!drawer)
        return;

    panel_widget = panel_toplevel_get_panel_widget (parent_toplevel);

    drawer->info = mate_panel_applet_register (drawer->button, drawer,
                                          (GDestroyNotify) g_free,
                                          panel_widget,
                                          locked, pos, exactpos,
                                          PANEL_OBJECT_DRAWER, id);

    if (!drawer->info) {
        gtk_widget_destroy (GTK_WIDGET (toplevel));
        return;
    }

    g_signal_connect_after (drawer->button, "size_allocate", G_CALLBACK (drawer_button_size_allocated), drawer);

    panel_widget_add_forbidden (panel_toplevel_get_panel_widget (drawer->toplevel));
    panel_widget_set_applet_expandable (panel_widget, GTK_WIDGET (drawer->button), FALSE, TRUE);
    panel_widget_set_applet_size_constrained (panel_widget, GTK_WIDGET (drawer->button), TRUE);

    mate_panel_applet_add_callback (drawer->info,
                               "add",
                               "list-add",
                               _("_Add to Drawer..."),
                               drawer_changes_enabled);

    mate_panel_applet_add_callback (drawer->info,
                               "properties",
                               "document-properties",
                               _("_Properties"),
                               drawer_changes_enabled);

    mate_panel_applet_add_callback (drawer->info,
                               "help",
                               "help-browser",
                               _("_Help"),
                               NULL);

    panel_drawer_connect_to_gsettings (drawer);
}

static void
panel_drawer_prepare (const char  *drawer_id,
                      GIcon       *custom_icon,
                      gboolean     use_custom_icon,
                      const char  *tooltip,
                      char       **attached_toplevel_id)
{
    GSettings *settings;
    char *path;

    path = g_strdup_printf ("%s%s/", PANEL_OBJECT_PATH, drawer_id);
    settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);
    g_free (path);

    if (tooltip) {
        g_settings_set_string (settings, PANEL_OBJECT_TOOLTIP_KEY, tooltip);
    }

    g_settings_set_boolean (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY, use_custom_icon);

    if (custom_icon) {
	gchar *icon_name;
	icon_name = g_icon_to_string(custom_icon);
	g_settings_set_string (settings, PANEL_OBJECT_CUSTOM_ICON_KEY, icon_name);
	g_free(icon_name);
    }

    if (attached_toplevel_id) {
        char *toplevel_id;
        char *toplevel_path;
        GSettings *toplevel_settings;

        toplevel_id = panel_profile_find_new_id (PANEL_GSETTINGS_TOPLEVELS);

        toplevel_path = g_strdup_printf (PANEL_TOPLEVEL_PATH "%s/", toplevel_id);

        toplevel_settings = g_settings_new_with_path (PANEL_TOPLEVEL_SCHEMA, toplevel_path);

        g_settings_set_string (settings, PANEL_OBJECT_ATTACHED_TOPLEVEL_ID_KEY, toplevel_id);
        g_settings_set_boolean (toplevel_settings, PANEL_TOPLEVEL_ENABLE_BUTTONS_KEY, TRUE);
        g_settings_set_boolean (toplevel_settings, PANEL_TOPLEVEL_ENABLE_ARROWS_KEY, TRUE);

        *attached_toplevel_id = toplevel_id;

        g_object_unref (toplevel_settings);
        g_free (toplevel_path);
    }
    g_object_unref (settings);
}

/* API */

void
panel_drawer_create (PanelToplevel *toplevel,
                     int            position,
                     GIcon         *custom_icon,
                     gboolean       use_custom_icon,
                     const char    *tooltip)
{
    char *id;

    id = panel_profile_prepare_object (PANEL_OBJECT_DRAWER, toplevel, position, FALSE);

    panel_drawer_prepare (id, custom_icon, use_custom_icon, tooltip, NULL);

    panel_profile_add_to_list (PANEL_GSETTINGS_OBJECTS, id);

    g_free (id);
}

char *
panel_drawer_create_with_id (const char    *toplevel_id,
                             int            position,
                             GIcon         *custom_icon,
                             gboolean       use_custom_icon,
                             const char    *tooltip)
{
    char *id;
    char *attached_toplevel_id = NULL;

    id = panel_profile_prepare_object_with_id (PANEL_OBJECT_DRAWER, toplevel_id, position, FALSE);

    panel_drawer_prepare (id, custom_icon, use_custom_icon, tooltip, &attached_toplevel_id);

    panel_profile_add_to_list (PANEL_GSETTINGS_OBJECTS, id);

    g_free (id);

    return attached_toplevel_id;
}

void
drawer_load_from_gsettings (PanelWidget *panel_widget,
                            gboolean     locked,
                            gint         position,
                            const char  *id)
{
    gboolean     use_custom_icon;
    char        *toplevel_id;
    char        *custom_icon;
    char        *tooltip;
    gchar       *path;
    GSettings   *settings;

    g_return_if_fail (panel_widget != NULL);
    g_return_if_fail (id != NULL);

    path = g_strdup_printf ("%s%s/", PANEL_OBJECT_PATH, id);
    settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);
    g_free (path);

    toplevel_id = g_settings_get_string (settings, PANEL_OBJECT_ATTACHED_TOPLEVEL_ID_KEY);

    panel_profile_load_toplevel (toplevel_id);

    use_custom_icon = g_settings_get_boolean (settings, PANEL_OBJECT_USE_CUSTOM_ICON_KEY);
    custom_icon = g_settings_get_string (settings, PANEL_OBJECT_CUSTOM_ICON_KEY);

    tooltip = g_settings_get_string (settings, PANEL_OBJECT_TOOLTIP_KEY);

    load_drawer_applet (toplevel_id,
                        settings,
                        custom_icon,
                        use_custom_icon,
                        tooltip,
                        panel_widget->toplevel,
                        locked,
                        position,
                        TRUE,
                        id);

    g_free (toplevel_id);
    g_free (custom_icon);
    g_free (tooltip);
}

void
panel_drawer_set_dnd_enabled (Drawer   *drawer,
                              gboolean  dnd_enabled)
{
    if (dnd_enabled) {
        static GtkTargetEntry dnd_targets[] = {
            { "application/x-mate-panel-applet-internal", 0, 0 }
        };

        gtk_widget_set_has_window (drawer->button, TRUE);
        gtk_drag_source_set (drawer->button,
                             GDK_BUTTON1_MASK,
                             dnd_targets, 1,
                             GDK_ACTION_MOVE);
        //FIXME: we're forgetting the use_custom_icon case, here
        gtk_drag_source_set_icon_name (drawer->button, button_widget_get_icon_name (BUTTON_WIDGET (drawer->button)));

        gtk_widget_set_has_window (drawer->button, FALSE);

    } else
        gtk_drag_source_unset (drawer->button);
}

void
drawer_query_deletion (Drawer *drawer)
{
    GtkWidget *dialog;

     if (drawer->toplevel) {
        PanelWidget *panel_widget;

        panel_widget = panel_toplevel_get_panel_widget (drawer->toplevel);

        if (!panel_global_config_get_confirm_panel_remove () ||
            !g_list_length (panel_widget->applet_list)) {
                panel_profile_delete_object (drawer->info);
                return;
        }

        dialog = panel_deletion_dialog (drawer->toplevel);

        g_signal_connect (dialog, "response", G_CALLBACK (drawer_deletion_response), drawer);

        g_signal_connect_object (drawer->toplevel, "destroy", G_CALLBACK (gtk_widget_destroy), dialog, G_CONNECT_SWAPPED);

        gtk_widget_show_all (dialog);
    }
}
