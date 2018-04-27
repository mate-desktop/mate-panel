#ifndef DRAWER_H
#define DRAWER_H

#include "panel.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    char          *tooltip;

    PanelToplevel *toplevel;
    GtkWidget     *button;

    gboolean       opened_for_drag;
    guint          close_timeout_id;

    AppletInfo    *info;
} Drawer;


/* API */

void  panel_drawer_create                       (PanelToplevel    *toplevel,
                                                 int               position,
                                                 GIcon            *custom_icon,
                                                 gboolean          use_custom_icon,
                                                 const char       *tooltip);

char *panel_drawer_create_with_id               (const char       *toplevel_id,
                                                 int               position,
                                                 GIcon            *custom_icon,
                                                 gboolean          use_custom_icon,
                                                 const char       *tooltip);

void  drawer_load_from_gsettings                (PanelWidget      *panel_widget,
                                                 gboolean          locked,
                                                 gint              position,
                                                 const char       *id);

void  panel_drawer_set_dnd_enabled              (Drawer           *drawer,
                                                 gboolean          dnd_enabled);

void  drawer_query_deletion                     (Drawer           *drawer);


#ifdef __cplusplus
}
#endif

#endif
