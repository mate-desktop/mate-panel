/*
 * MATE panel drawer module.
 *
 * List static function prototypes separate
 * to prevent errors when compiling panel.c
 *
 * Authors: info@cppsp.de
 */


#ifndef DRAWER_PRIVATE_H
#define DRAWER_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif


/* Internal functions */
/* event handlers */

static void  drawer_click                       (GtkWidget        *widget,
                                                 Drawer           *drawer);

static void  drawer_focus_panel_widget          (Drawer           *drawer,
                                                 GtkDirectionType  direction);

static gboolean  key_press_drawer               (GtkWidget        *widget,
                                                 GdkEventKey      *event,
                                                 Drawer           *drawer);

static gboolean  key_press_drawer_widget        (GtkWidget        *widget,
                                                 GdkEventKey      *event,
                                                 Drawer           *drawer);

    /* drag and drop handlers */

static void  drag_data_get_cb                   (GtkWidget        *widget,
                                                 GdkDragContext   *context,
                                                 GtkSelectionData *selection_data,
                                                 guint             info,
                                                 guint             time,
                                                 Drawer           *drawer);

static gboolean  drag_motion_cb                 (GtkWidget        *widget,
                                                 GdkDragContext   *context,
                                                 int               x,
                                                 int               y,
                                                 guint             time_,
                                                 Drawer           *drawer);

static gboolean  drag_drop_cb                   (GtkWidget        *widget,
                                                 GdkDragContext   *context,
                                                 int               x,
                                                 int               y,
                                                 guint             time_,
                                                 Drawer           *drawer);

static void  drag_data_received_cb              (GtkWidget        *widget,
                                                 GdkDragContext   *context,
                                                 gint              x,
                                                 gint              y,
                                                 GtkSelectionData *selection_data,
                                                 guint             info,
                                                 guint             time_,
                                                 Drawer           *drawer);

static gboolean  close_drawer_in_idle           (gpointer          data);

static void  queue_drawer_close_for_drag        (Drawer           *drawer);

static void  drag_leave_cb                      (GtkWidget        *widget,
                                                 GdkDragContext   *context,
                                                 guint             time_,
                                                 Drawer           *drawer);

    /* load_drawer_applet handlers */

static void  drawer_button_size_allocated       (GtkWidget        *widget,
                                                 GtkAllocation    *alloc,
                                                 Drawer           *drawer);

static gboolean  drawer_changes_enabled         (void);

    /* gsettings handlers */

static void  panel_drawer_custom_icon_changed   (GSettings        *settings,
                                                 gchar            *key,
                                                 Drawer           *drawer);

static void  panel_drawer_tooltip_changed       (GSettings        *settings,
                                                 gchar            *key,
                                                 Drawer           *drawer);

    /* destroy handlers */

static void  toplevel_destroyed                 (GtkWidget        *widget,
                                                 Drawer           *drawer);

static void  destroy_drawer                     (GtkWidget        *widget,
                                                 Drawer           *drawer);

static void  drawer_deletion_response           (GtkWidget        *dialog,
                                                 int               response,
                                                 Drawer           *drawer);

/* end event handlers */

static PanelToplevel *create_drawer_toplevel    (const char       *drawer_id,
                                                 GSettings        *settings);

static void  set_tooltip_and_name               (Drawer           *drawer,
                                                 const char       *tooltip);

static Drawer *create_drawer_applet             (PanelToplevel    *toplevel,
                                                 PanelToplevel    *parent_toplevel,
                                                 const char       *tooltip,
                                                 const char       *custom_icon,
                                                 gboolean          use_custom_icon,
                                                 PanelOrientation  orientation);

static void  panel_drawer_connect_to_gsettings  (Drawer           *drawer);

static void  load_drawer_applet                 (char             *toplevel_id,
                                                 GSettings        *settings,
                                                 const char       *custom_icon,
                                                 gboolean          use_custom_icon,
                                                 const char       *tooltip,
                                                 PanelToplevel    *parent_toplevel,
                                                 gboolean          locked,
                                                 int               pos,
                                                 gboolean          exactpos,
                                                 const char       *id);

static void  panel_drawer_prepare               (const char       *drawer_id,
                                                 GIcon            *custom_icon,
                                                 gboolean          use_custom_icon,
                                                 const char       *tooltip,
                                                 char            **attached_toplevel_id);


#ifdef __cplusplus
}
#endif

#endif /* DRAWER_PRIVATE_H */
