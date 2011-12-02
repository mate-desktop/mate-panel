#ifndef PANEL_H
#define PANEL_H

#include <gtk/gtk.h>
#include "panel-toplevel.h"
#include "panel-widget.h"
#include "applet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _PanelData PanelData;
struct _PanelData {
	GtkWidget *panel;
	GtkWidget *menu;
	int insertion_pos;
	guint deactivate_idle;
};

void orientation_change(AppletInfo *info, PanelWidget *panel);
void size_change(AppletInfo *info, PanelWidget *panel);
void back_change(AppletInfo *info, PanelWidget *panel);

PanelData *panel_setup (PanelToplevel *toplevel);

GdkScreen *panel_screen_from_panel_widget  (PanelWidget *panel);

gboolean panel_is_applet_right_stick (GtkWidget *applet);


gboolean panel_check_dnd_target_data (GtkWidget      *widget,
				      GdkDragContext *context,
				      guint          *ret_info,
				      GdkAtom        *ret_atom);

void panel_receive_dnd_data (PanelWidget      *panel,
			     guint             info,
			     int               pos,
			     GtkSelectionData *selection_data,
			     GdkDragContext   *context,
			     guint             time_);

gboolean panel_check_drop_forbidden (PanelWidget    *panel,
				     GdkDragContext *context,
				     guint           info,
				     guint           time_);

void panel_delete (PanelToplevel *toplevel);

GtkWidget  *panel_deletion_dialog  (PanelToplevel *toplevel);

#ifdef __cplusplus
}
#endif

#endif
