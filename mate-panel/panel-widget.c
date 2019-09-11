/* Mate panel: panel widget
 * (C) 1997,1998,1999,2000 the Free Software Foundation
 * (C) 2000 Eazel, Inc.
 *
 * Authors:  George Lebl
 */
#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#ifdef HAVE_X11
#include <gtk/gtkx.h> /* for GTK_IS_SOCKET */
#endif

#include <libpanel-util/panel-list.h>

#include "applet.h"
#include "panel-widget.h"
#include "button-widget.h"
#include "panel.h"
#include "panel-util.h"
#include "panel-marshal.h"
#include "panel-typebuiltins.h"
#include "panel-applet-frame.h"
#include "panel-globals.h"
#include "panel-profile.h"
#include "panel-lockdown.h"

#define MOVE_INCREMENT 1

typedef enum {
	PANEL_SWITCH_MOVE = 0,
	PANEL_FREE_MOVE,
	PANEL_PUSH_MOVE
} PanelMovementType;

G_DEFINE_TYPE (PanelWidget, panel_widget, GTK_TYPE_FIXED);

enum {
	SIZE_CHANGE_SIGNAL,
	BACK_CHANGE_SIGNAL,
	APPLET_MOVE_SIGNAL,
	APPLET_ADDED_SIGNAL,
	APPLET_REMOVED_SIGNAL,
	PUSH_MOVE_SIGNAL,
	SWITCH_MOVE_SIGNAL,
	FREE_MOVE_SIGNAL,
	TAB_MOVE_SIGNAL,
	END_MOVE_SIGNAL,
	POPUP_PANEL_MENU_SIGNAL,
	LAST_SIGNAL
};

static guint panel_widget_signals [LAST_SIGNAL] = {0};

/*define for some debug output*/
#undef PANEL_WIDGET_DEBUG

static gboolean mate_panel_applet_in_drag = FALSE;
static GtkWidget *saved_focus_widget = NULL;

static void panel_widget_get_preferred_size (GtkWidget	  *widget,
					     GtkRequisition *minimum_size,
					     GtkRequisition *natural_size);
static void panel_widget_get_preferred_width (GtkWidget *widget,
					      gint *minimum_width,
					      gint *natural_width);
static void panel_widget_get_preferred_height (GtkWidget *widget,
					       gint *minimum_height,
					       gint *natural_height);
static void panel_widget_size_allocate  (GtkWidget        *widget,
					 GtkAllocation    *allocation);
static void panel_widget_cadd           (GtkContainer     *container,
					 GtkWidget        *widget);
static void panel_widget_cremove        (GtkContainer     *container,
					 GtkWidget        *widget);
static void panel_widget_dispose        (GObject *obj);
static void panel_widget_finalize       (GObject          *obj);

static void panel_widget_push_move_applet   (PanelWidget      *panel,
                                             GtkDirectionType  dir);
static void panel_widget_switch_move_applet (PanelWidget      *panel,
                                             GtkDirectionType  dir);
static void panel_widget_free_move_applet   (PanelWidget      *panel,
                                             GtkDirectionType  dir);
static void panel_widget_tab_move           (PanelWidget      *panel,
                                             gboolean          next);
static void panel_widget_end_move           (PanelWidget      *panel);
static gboolean panel_widget_real_focus     (GtkWidget        *widget,
                                             GtkDirectionType  direction);

static gboolean panel_widget_push_applet_right (PanelWidget *panel,
						GList       *list,
						int          push);
static gboolean panel_widget_push_applet_left  (PanelWidget *panel,
						GList       *list,
						int          push);

/************************
 convenience functions
 ************************/
static int
applet_data_compare (AppletData *ad1, AppletData *ad2)
{
	return ad1->pos - ad2->pos;
}

static void
emit_applet_moved (PanelWidget *panel_widget,
		   AppletData  *applet)
{
	/* we always want to queue a draw after moving, so do it here instead
	* of after the signal emission in all callers */
	gtk_widget_queue_draw (applet->applet);
	g_signal_emit (panel_widget,
		       panel_widget_signals [APPLET_MOVE_SIGNAL], 0,
		       applet->applet);
}

/************************
 widget core
 ************************/

static void
add_tab_bindings (GtkBindingSet    *binding_set,
   	          GdkModifierType   modifiers,
		  gboolean          next)
{
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
				      "tab_move", 1,
				      G_TYPE_BOOLEAN, next);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
				      "tab_move", 1,
				      G_TYPE_BOOLEAN, next);
}

static void
add_move_bindings (GtkBindingSet    *binding_set,
		   GdkModifierType   modifiers,
		   const gchar      *name)
{
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Up, modifiers,
                                      name, 1,
                                      GTK_TYPE_DIRECTION_TYPE, GTK_DIR_UP);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Down, modifiers,
                                      name, 1,
                                      GTK_TYPE_DIRECTION_TYPE, GTK_DIR_DOWN);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Left, modifiers,
                                      name, 1,
                                      GTK_TYPE_DIRECTION_TYPE, GTK_DIR_LEFT);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Right, modifiers,
                                      name, 1,
                                      GTK_TYPE_DIRECTION_TYPE, GTK_DIR_RIGHT);
}

static void
add_all_move_bindings (PanelWidget *panel)
{
	GtkWidgetClass *class;
	GtkBindingSet *binding_set;

	class = GTK_WIDGET_GET_CLASS (panel);

	binding_set = gtk_binding_set_by_class (class);

	add_move_bindings (binding_set, GDK_SHIFT_MASK, "push_move");
	add_move_bindings (binding_set, GDK_CONTROL_MASK, "switch_move");
	add_move_bindings (binding_set, GDK_MOD1_MASK, "free_move");
	add_move_bindings (binding_set, 0, "free_move");

	add_tab_bindings (binding_set, 0, TRUE);
	add_tab_bindings (binding_set, GDK_SHIFT_MASK, FALSE);

	gtk_binding_entry_add_signal (binding_set,
                                      GDK_KEY_Escape, 0,
                                      "end_move", 0);
	gtk_binding_entry_add_signal (binding_set,
                                      GDK_KEY_KP_Enter, 0,
                                      "end_move", 0);
	gtk_binding_entry_add_signal (binding_set,
                                      GDK_KEY_Return, 0,
                                      "end_move", 0);
	gtk_binding_entry_add_signal (binding_set,
                                      GDK_KEY_KP_Space, 0,
                                      "end_move", 0);
	gtk_binding_entry_add_signal (binding_set,
                                      GDK_KEY_space, 0,
                                      "end_move", 0);

#ifdef HAVE_X11
	GtkWidget *focus_widget;

	focus_widget = gtk_window_get_focus (GTK_WINDOW (panel->toplevel));
	// will always be false when not on X
	if (GTK_IS_SOCKET (focus_widget)) {
		/*
		 * If the focus widget is a GtkSocket, i.e. the
		 * focus is in an applet in another process then
		 * key bindings do not work. We get around this by
		 * by setting the focus to the PanelWidget for the
		 * duration of the move.
		 */
		gtk_widget_set_can_focus (GTK_WIDGET (panel), TRUE);
		gtk_widget_grab_focus (GTK_WIDGET (panel));
		saved_focus_widget = focus_widget;
	}
#endif // HAVE_X11
}

static void
panel_widget_force_grab_focus (GtkWidget *widget)
{
	gboolean can_focus = gtk_widget_get_can_focus (widget);
	/*
	 * This follows what gtk_socket_claim_focus() does
	 */
	if (!can_focus)
		gtk_widget_set_can_focus (widget, TRUE);
	gtk_widget_grab_focus (widget);
	if (!can_focus)
		gtk_widget_set_can_focus (widget, FALSE);
}

static void
panel_widget_reset_saved_focus (PanelWidget *panel)
{
	if (saved_focus_widget) {
		gtk_widget_set_can_focus (GTK_WIDGET (panel), FALSE);
		panel_widget_force_grab_focus (saved_focus_widget);
		saved_focus_widget = NULL;
	}
}

static void
remove_tab_bindings (GtkBindingSet    *binding_set,
		     GdkModifierType   modifiers,
		     gboolean          next)
{
	gtk_binding_entry_remove (binding_set, GDK_KEY_Tab, modifiers);
	gtk_binding_entry_remove (binding_set, GDK_KEY_KP_Tab, modifiers);
}

static void
remove_move_bindings (GtkBindingSet    *binding_set,
		      GdkModifierType   modifiers)
{
	gtk_binding_entry_remove (binding_set, GDK_KEY_Up, modifiers);
	gtk_binding_entry_remove (binding_set, GDK_KEY_Down, modifiers);
	gtk_binding_entry_remove (binding_set, GDK_KEY_Left, modifiers);
	gtk_binding_entry_remove (binding_set, GDK_KEY_Right, modifiers);
}

static void
remove_all_move_bindings (PanelWidget *panel)
{
	GtkWidgetClass *class;
	GtkBindingSet *binding_set;

	class = GTK_WIDGET_GET_CLASS (panel);

	binding_set = gtk_binding_set_by_class (class);

	panel_widget_reset_saved_focus (panel);

	remove_move_bindings (binding_set, GDK_SHIFT_MASK);
	remove_move_bindings (binding_set, GDK_CONTROL_MASK);
	remove_move_bindings (binding_set, GDK_MOD1_MASK);
	remove_move_bindings (binding_set, 0);
	remove_tab_bindings (binding_set, 0, TRUE);
	remove_tab_bindings (binding_set, GDK_SHIFT_MASK, FALSE);

	gtk_binding_entry_remove (binding_set, GDK_KEY_Escape, 0);
	gtk_binding_entry_remove (binding_set, GDK_KEY_KP_Enter, 0);
	gtk_binding_entry_remove (binding_set, GDK_KEY_Return, 0);
	gtk_binding_entry_remove (binding_set, GDK_KEY_KP_Space, 0);
	gtk_binding_entry_remove (binding_set, GDK_KEY_space, 0);
}

static void
panel_widget_class_init (PanelWidgetClass *class)
{
	GObjectClass *object_class = (GObjectClass*) class;
	GtkWidgetClass *widget_class = (GtkWidgetClass*) class;
	GtkContainerClass *container_class = (GtkContainerClass*) class;

	panel_widget_signals[SIZE_CHANGE_SIGNAL] =
                g_signal_new ("size_change",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PanelWidgetClass, size_change),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

	panel_widget_signals[BACK_CHANGE_SIGNAL] =
                g_signal_new ("back_change",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PanelWidgetClass, back_change),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

	panel_widget_signals[APPLET_MOVE_SIGNAL] =
                g_signal_new ("applet_move",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PanelWidgetClass, applet_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);

	panel_widget_signals[APPLET_ADDED_SIGNAL] =
                g_signal_new ("applet_added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PanelWidgetClass, applet_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);

	panel_widget_signals[APPLET_REMOVED_SIGNAL] =
                g_signal_new ("applet_removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (PanelWidgetClass, applet_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);

	panel_widget_signals[PUSH_MOVE_SIGNAL] =
		g_signal_new ("push_move",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                              G_STRUCT_OFFSET (PanelWidgetClass, push_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__ENUM,
                              G_TYPE_NONE,
                              1,
                              GTK_TYPE_DIRECTION_TYPE);

	panel_widget_signals[SWITCH_MOVE_SIGNAL] =
                g_signal_new ("switch_move",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                              G_STRUCT_OFFSET (PanelWidgetClass, switch_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__ENUM,
                              G_TYPE_NONE,
                              1,
                              GTK_TYPE_DIRECTION_TYPE);

	panel_widget_signals[FREE_MOVE_SIGNAL] =
                g_signal_new ("free_move",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                              G_STRUCT_OFFSET (PanelWidgetClass, free_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__ENUM,
                              G_TYPE_NONE,
                              1,
                              GTK_TYPE_DIRECTION_TYPE);

	panel_widget_signals[TAB_MOVE_SIGNAL] =
                g_signal_new ("tab_move",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (PanelWidgetClass, tab_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);

	panel_widget_signals[END_MOVE_SIGNAL] =
		g_signal_new ("end_move",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                              G_STRUCT_OFFSET (PanelWidgetClass, end_move),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

	class->size_change = NULL;
	class->back_change = NULL;
	class->applet_move = NULL;
	class->applet_added = NULL;
	class->applet_removed = NULL;
	class->push_move = panel_widget_push_move_applet;
	class->switch_move = panel_widget_switch_move_applet;
	class->free_move = panel_widget_free_move_applet;
	class->tab_move = panel_widget_tab_move;
	class->end_move = panel_widget_end_move;

	object_class->dispose = panel_widget_dispose;
	object_class->finalize = panel_widget_finalize;

	widget_class->get_preferred_width = panel_widget_get_preferred_width;
	widget_class->get_preferred_height = panel_widget_get_preferred_height;
	widget_class->size_allocate = panel_widget_size_allocate;

	gtk_widget_class_set_css_name (widget_class, "PanelWidget");

	widget_class->focus = panel_widget_real_focus;
	container_class->add = panel_widget_cadd;
	container_class->remove = panel_widget_cremove;
}

static void
remove_panel_from_forbidden(PanelWidget *panel, PanelWidget *r)
{
	GSList *list;
	GtkWidget *parent_panel;

	g_return_if_fail(PANEL_IS_WIDGET(panel));
	g_return_if_fail(PANEL_IS_WIDGET(r));

	if(!panel->master_widget)
		return;

	list = g_object_get_data (G_OBJECT(panel->master_widget),
				  MATE_PANEL_APPLET_FORBIDDEN_PANELS);
	if(list) {
		list = g_slist_remove(list,r);
		g_object_set_data (G_OBJECT(panel->master_widget),
				   MATE_PANEL_APPLET_FORBIDDEN_PANELS,
				   list);
	}
	parent_panel = gtk_widget_get_parent (panel->master_widget);
	if (parent_panel)
		remove_panel_from_forbidden(PANEL_WIDGET(parent_panel), r);
}

static void
add_panel_to_forbidden(PanelWidget *panel, PanelWidget *r)
{
	GSList *list;
	GtkWidget *parent_panel;

	g_return_if_fail(PANEL_IS_WIDGET(panel));
	g_return_if_fail(PANEL_IS_WIDGET(r));

	if(!panel->master_widget)
		return;

	list = g_object_get_data (G_OBJECT(panel->master_widget),
				  MATE_PANEL_APPLET_FORBIDDEN_PANELS);
	if(g_slist_find(list,r)==NULL) {
		list = g_slist_prepend(list,r);

		g_object_set_data (G_OBJECT(panel->master_widget),
				   MATE_PANEL_APPLET_FORBIDDEN_PANELS,
				   list);
	}
	parent_panel = gtk_widget_get_parent (panel->master_widget);
	if (parent_panel)
		add_panel_to_forbidden(PANEL_WIDGET(parent_panel), r);
}

static void
run_up_forbidden(PanelWidget *panel,
		 void (*runfunc)(PanelWidget *,PanelWidget *))
{
	GList *list;

	g_return_if_fail(PANEL_IS_WIDGET(panel));

	for(list = panel->applet_list;list!=NULL;list = g_list_next(list)) {
		AppletData *ad = list->data;
		PanelWidget *p =
			g_object_get_data (G_OBJECT(ad->applet),
					   MATE_PANEL_APPLET_ASSOC_PANEL_KEY);
		if(p)
			run_up_forbidden(p,runfunc);
	}
	(*runfunc)(panel,panel);
}

static void
panel_widget_reset_focus (GtkContainer *container,
                          GtkWidget    *widget)
{
	PanelWidget *panel = PANEL_WIDGET (container);

	if (gtk_container_get_focus_child (container) == widget) {
		GList *children;

		children = gtk_container_get_children (container);

		/* More than one element on the list */
		if (children && children->next) {
			GList *l;

			/* There are still object on the panel */
			for (l = children; l; l = l->next) {
				GtkWidget *child_widget;

				child_widget = l->data;
				if (child_widget == widget)
					break;
			}
			if (l) {
				GtkWidget *next_widget;

				if (l->next)
					next_widget = l->next->data;
				else
					next_widget = l->prev->data;

				gtk_widget_child_focus (next_widget,
						        GTK_DIR_TAB_FORWARD);
			}
		} else
			panel_widget_focus (panel);

		g_list_free (children);
	}
}

static void
panel_widget_cadd (GtkContainer *container,
		   GtkWidget    *widget)
{
	PanelWidget *p;

	g_return_if_fail (PANEL_IS_WIDGET (container));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	panel_widget_add (PANEL_WIDGET (container), widget, FALSE, 0, FALSE);

	p = g_object_get_data (G_OBJECT(widget),
			       MATE_PANEL_APPLET_ASSOC_PANEL_KEY);
	if (p) {
		panel_toplevel_attach_to_widget (p->toplevel,
						 PANEL_WIDGET (container)->toplevel,
						 widget);
		run_up_forbidden (p, add_panel_to_forbidden);
	}
}

static void
panel_widget_cremove (GtkContainer *container, GtkWidget *widget)
{
	AppletData *ad;
	PanelWidget *p;
	PanelWidget *panel;

	g_return_if_fail (PANEL_IS_WIDGET (container));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	panel = PANEL_WIDGET (container);

	ad = g_object_get_data (G_OBJECT (widget), MATE_PANEL_APPLET_DATA);
	p = g_object_get_data (G_OBJECT (widget),
				 MATE_PANEL_APPLET_ASSOC_PANEL_KEY);

	if (p != NULL) {
		panel_toplevel_detach (p->toplevel);
		run_up_forbidden (p, remove_panel_from_forbidden);
	}

	panel_widget_reset_focus (container, widget);

	if(panel->currently_dragged_applet == ad)
		panel_widget_applet_drag_end(panel);

	g_object_ref (widget);
	if (GTK_CONTAINER_CLASS (panel_widget_parent_class)->remove)
		(* GTK_CONTAINER_CLASS (panel_widget_parent_class)->remove) (container,
								widget);
	if (ad)
		panel->applet_list = g_list_remove (panel->applet_list, ad);

	g_signal_emit (G_OBJECT (container),
		       panel_widget_signals[APPLET_REMOVED_SIGNAL],
		       0, widget);
	g_object_unref (widget);
}


/*get the list item of the data on the position pos*/
static GList *
get_applet_list_pos (PanelWidget *panel,
		     int          pos)
{
	GList *l;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel), NULL);

	for (l = panel->applet_list; l; l = l->next) {
		AppletData *ad = l->data;

		if (ad->pos <= pos) {
		       if (ad->pos + ad->cells > pos)
			       return l;
		} else
			return NULL;
	}

	return NULL;
}

/*tells us if an applet is "stuck" on the right side*/
int
panel_widget_is_applet_stuck (PanelWidget *panel_widget,
			      GtkWidget   *widget)
{
	AppletData *applet;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel_widget), FALSE);
	g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

	applet = g_object_get_data (G_OBJECT (widget), MATE_PANEL_APPLET_DATA);
	if (applet) {
		GList *applet_list, *l;
		int    end_pos = -1;

		applet_list = g_list_find (panel_widget->applet_list, applet);

		for (l = applet_list; l; l = l->next) {
			applet = l->data;

			if (end_pos != -1 && applet->pos != end_pos)
				break;

			end_pos = applet->pos + applet->cells;
			if (end_pos >= panel_widget->size)
				return TRUE;
		}
	}

	return FALSE;
}

static int
get_size_from_hints (AppletData *ad, int cells)
{
	int i;

	for (i = 0; i < ad->size_hints_len; i += 2) {
		if (cells > ad->size_hints[i]) {
			/* Clip to top */
			cells = ad->size_hints[i];
			break;
		}
		if (cells <= ad->size_hints[i] &&
		    cells >= ad->size_hints[i+1]) {
			/* Keep cell size */
			break;
		}
	}

	return MAX (cells, ad->min_cells);
}

static void
panel_widget_jump_applet_right (PanelWidget *panel,
				GList       *list,
				GList       *next,
				int          pos)
{
	AppletData *ad;
	AppletData *nad = NULL;

	ad = list->data;
	if (next)
		nad = next->data;

	if (pos >= panel->size)
		return;

	if (!nad || nad->constrained >= pos + ad->min_cells)
		goto jump_right;

	if (!panel_widget_push_applet_right (panel, next, pos + ad->min_cells - nad->constrained)) {
		panel_widget_jump_applet_right (panel,
						list,
						next->next,
						nad->constrained + nad->min_cells);
		return;
	}

 jump_right:
	ad->pos = ad->constrained = pos;
	panel->applet_list = g_list_remove_link (panel->applet_list, list);
	panel->applet_list = panel_g_list_insert_before (panel->applet_list, next, list);
	gtk_widget_queue_resize (GTK_WIDGET (panel));
	emit_applet_moved (panel, ad);
}

static void
panel_widget_switch_applet_right (PanelWidget *panel,
				  GList       *list)
{
	AppletData *ad;
	AppletData *nad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (ad->constrained + ad->min_cells >= panel->size)
		return;

	if (list->next)
		nad = list->next->data;

	if (!nad || nad->constrained >= ad->constrained + ad->min_cells + MOVE_INCREMENT) {
		ad->pos = ad->constrained += MOVE_INCREMENT;
		gtk_widget_queue_resize (GTK_WIDGET (panel));
		emit_applet_moved (panel, ad);
		return;
	}

	if (nad->locked) {
		panel_widget_jump_applet_right (panel,
						list,
						list->next->next,
						nad->constrained + nad->min_cells);
		return;
	}

	nad->constrained = nad->pos = ad->constrained;
	ad->constrained = ad->pos = ad->constrained + nad->min_cells;
	panel->applet_list = panel_g_list_swap_next (panel->applet_list, list);

	gtk_widget_queue_resize (GTK_WIDGET (panel));

	emit_applet_moved (panel, ad);
	emit_applet_moved (panel, nad);
}

static void
panel_widget_jump_applet_left (PanelWidget *panel,
			       GList       *list,
			       GList       *prev,
			       int          pos)
{
	AppletData *ad;
	AppletData *pad = NULL;

	ad = list->data;
	if (prev)
		pad = prev->data;

	if (pos < 0)
		return;

	if (!pad || pad->constrained + pad->min_cells <= pos)
		goto jump_left;

	if (!panel_widget_push_applet_left (panel, prev, pad->constrained + pad->min_cells - pos)) {
		panel_widget_jump_applet_left (panel,
					       list,
					       prev->prev,
					       pad->constrained - ad->min_cells);
		return;
	}

 jump_left:
	ad->pos = ad->constrained = pos;
	panel->applet_list = g_list_remove_link (panel->applet_list, list);
	panel->applet_list = panel_g_list_insert_after (panel->applet_list, prev, list);
	gtk_widget_queue_resize (GTK_WIDGET (panel));
	emit_applet_moved (panel, ad);
}

static void
panel_widget_switch_applet_left (PanelWidget *panel,
				 GList       *list)
{
	AppletData *ad;
	AppletData *pad = NULL;

	ad = list->data;
	if (ad->constrained <= 0)
		return;

	if (list->prev)
		pad = list->prev->data;

	if (!pad || pad->constrained + pad->min_cells <= ad->constrained - MOVE_INCREMENT) {
		ad->pos = ad->constrained -= MOVE_INCREMENT;
		gtk_widget_queue_resize (GTK_WIDGET (panel));
		emit_applet_moved (panel, ad);
		return;
	}

	if (pad->locked) {
		panel_widget_jump_applet_left (panel,
					       list,
					       list->prev->prev,
					       pad->constrained - ad->min_cells);
		return;
	}

	ad->constrained = ad->pos = pad->constrained;
	pad->constrained = pad->pos = ad->constrained + ad->min_cells;
	panel->applet_list = panel_g_list_swap_prev (panel->applet_list, list);

	gtk_widget_queue_resize (GTK_WIDGET (panel));

	emit_applet_moved (panel, ad);
	emit_applet_moved (panel, pad);
}

static gboolean
panel_widget_try_push_right (PanelWidget *panel,
			     GList       *list,
			     int          push)
{
	AppletData *ad;
	AppletData *nad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (list->next)
		nad = list->next->data;

	if (ad->locked)
		return FALSE;

	if (ad->constrained + ad->min_cells + push >= panel->size)
		return FALSE;

	if (!nad || nad->constrained >= ad->constrained + ad->min_cells + push)
		return TRUE;

	return panel_widget_try_push_right (panel, list->next, push);
}

static int
panel_widget_get_right_jump_pos (PanelWidget *panel,
				 AppletData  *ad,
				 GList       *next,
				 int          pos)
{
	AppletData *nad = NULL;

	if (next)
		nad = next->data;

	if (!nad || nad->constrained >= pos + ad->min_cells)
		return pos;

	if (panel_widget_try_push_right (panel, next, pos + ad->min_cells - nad->constrained))
		return pos;

	return panel_widget_get_right_jump_pos (panel,
						ad,
						next->next,
						nad->constrained + nad->min_cells);
}

static int
panel_widget_get_right_switch_pos (PanelWidget *panel,
				   GList       *list)
{
	AppletData *ad;
	AppletData *nad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (list->next)
		nad = list->next->data;

	if (!nad || nad->constrained >= ad->constrained + ad->min_cells + MOVE_INCREMENT)
		return ad->constrained + MOVE_INCREMENT;

	if (nad->locked)
		return panel_widget_get_right_jump_pos (panel,
							ad,
							list->next->next,
							nad->constrained + nad->min_cells);

	return nad->constrained + nad->min_cells - ad->cells;
}

static gboolean
panel_widget_try_push_left (PanelWidget *panel,
			    GList       *list,
			    int          push)
{
	AppletData *ad;
	AppletData *pad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (list->prev)
		pad = list->prev->data;

	if (ad->locked)
		return FALSE;

	if (ad->constrained - push < 0)
		return FALSE;

	if (!pad || pad->constrained + pad->min_cells <= ad->constrained - push)
		return TRUE;

	return panel_widget_try_push_left (panel, list->prev, push);
}

static int
panel_widget_get_left_jump_pos (PanelWidget *panel,
				AppletData  *ad,
				GList       *prev,
				int          pos)
{
	AppletData *pad = NULL;

	if (prev)
		pad = prev->data;

	if (!pad || pad->constrained + pad->min_cells <= pos)
		return pos;

	if (panel_widget_try_push_left (panel, prev, pad->constrained + pad->min_cells - pos))
		return pos;

	return panel_widget_get_left_jump_pos (panel,
					       ad,
					       prev->prev,
					       pad->constrained - ad->min_cells);
}

static int
panel_widget_get_left_switch_pos (PanelWidget *panel,
				  GList       *list)
{
	AppletData *ad;
	AppletData *pad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (list->prev)
		pad = list->prev->data;

	if (!pad || pad->constrained + pad->min_cells <= ad->constrained - MOVE_INCREMENT)
		return ad->constrained - MOVE_INCREMENT;

	if (pad->locked)
		return panel_widget_get_left_jump_pos (panel,
						       ad,
						       list->prev->prev,
						       pad->constrained - ad->min_cells);

	return pad->constrained;
}

static void
panel_widget_switch_move (PanelWidget *panel,
			  AppletData  *ad,
			  int          moveby)
{
	GList *list;
	int    finalpos;
	int    pos;

	g_return_if_fail (ad != NULL);
	g_return_if_fail (PANEL_IS_WIDGET (panel));

	if (moveby == 0)
		return;

	list = g_list_find (panel->applet_list, ad);
	g_return_if_fail (list != NULL);

	finalpos = ad->constrained + moveby;

	if (ad->constrained < finalpos) {
		AppletData *pad;

		if (list->prev) {
			pad = list->prev->data;
			if (pad->expand_major)
				gtk_widget_queue_resize (GTK_WIDGET (panel));
		}

		while (ad->constrained < finalpos) {
			pos = panel_widget_get_right_switch_pos (panel, list);

			if (abs (pos - finalpos) >= abs (ad->constrained - finalpos) ||
			    pos + ad->min_cells > panel->size)
				break;

			panel_widget_switch_applet_right (panel, list);
		}

		if (list->prev) {
			pad = list->prev->data;
			if (pad->expand_major)
				gtk_widget_queue_resize (GTK_WIDGET (panel));
		}
	} else {
		AppletData *nad;

		if (list->next) {
			nad = list->next->data;
			if (nad->expand_major)
				gtk_widget_queue_resize (GTK_WIDGET (panel));
		}

		while (ad->constrained > finalpos) {
			pos = panel_widget_get_left_switch_pos (panel, list);

			if (abs (pos - finalpos) >= abs (ad->constrained - finalpos) || pos < 0)
				break;

			panel_widget_switch_applet_left (panel, list);
		}

	}
}

static int
panel_widget_push_applet_right (PanelWidget *panel,
				GList       *list,
				int          push)
{
	AppletData *ad;
	AppletData *nad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (ad->constrained + ad->min_cells + push >= panel->size)
		return FALSE;

	if (ad->locked)
		return FALSE;

	if (list->next)
		nad = list->next->data;

	if (!nad || nad->constrained >= ad->constrained + ad->min_cells + push) {
		ad->pos = ad->constrained += push;
		gtk_widget_queue_resize (GTK_WIDGET (panel));
		emit_applet_moved (panel, ad);
		return TRUE;
	}

	g_assert (list->next != NULL);

	if (!panel_widget_push_applet_right (panel, list->next, push))
		return FALSE;

	ad->pos = ad->constrained += push;;
	gtk_widget_queue_resize (GTK_WIDGET (panel));
	emit_applet_moved (panel, ad);

	return TRUE;
}

static int
panel_widget_push_applet_left (PanelWidget *panel,
			       GList       *list,
			       int          push)
{
	AppletData *ad;
	AppletData *pad = NULL;

	g_assert (list != NULL);

	ad = list->data;
	if (ad->constrained - push < 0)
		return FALSE;

	if (ad->locked)
		return FALSE;

	if (list->prev)
		pad = list->prev->data;

	if (!pad || pad->constrained + pad->min_cells <= ad->constrained - push) {
		ad->pos = ad->constrained -= push;
		gtk_widget_queue_resize (GTK_WIDGET (panel));
		emit_applet_moved (panel, ad);
		return TRUE;
	}

	g_assert (list->prev != NULL);

	if (!panel_widget_push_applet_left (panel, list->prev, push))
		return FALSE;

	ad->pos = ad->constrained -= push;
	gtk_widget_queue_resize (GTK_WIDGET (panel));
	emit_applet_moved (panel, ad);

	return TRUE;
}

static void
panel_widget_push_move (PanelWidget *panel,
			AppletData  *ad,
			int          moveby)
{
	AppletData *pad;
	int finalpos;
	GList *list;

	g_return_if_fail (ad != NULL);
	g_return_if_fail (PANEL_IS_WIDGET (panel));

	if (moveby == 0)
		return;

	list = g_list_find (panel->applet_list, ad);
	g_return_if_fail (list != NULL);

	finalpos = ad->constrained + moveby;

	if (ad->constrained < finalpos) {
		while (ad->constrained < finalpos)
			if (!panel_widget_push_applet_right (panel, list, 1))
				break;

                if (list->prev) {
			pad = list->prev->data;
			if (pad->expand_major)
				gtk_widget_queue_resize (GTK_WIDGET (panel));
		}
	} else {
                while (ad->constrained > finalpos)
			if (!panel_widget_push_applet_left (panel, list, 1))
				break;
	}
}


/*this is a special function and may fail if called improperly, it works
only under special circumstance when we know there is nothing from
old_size to panel->size*/
static void
panel_widget_right_stick(PanelWidget *panel,int old_size)
{
	int i,pos;
	GList *list,*prev;
	AppletData *ad;

	g_return_if_fail(PANEL_IS_WIDGET(panel));
	g_return_if_fail(old_size>=0);

	if(old_size>=panel->size ||
	   panel->packed)
	   	return;

	list = get_applet_list_pos(panel,old_size-1);

	if(!list)
		return;

	pos = panel->size-1;

	ad = list->data;
	do {
		i = ad->pos;
		ad->pos = ad->constrained = pos--;
		ad->cells = 1;
		prev = list;
		list = g_list_previous(list);
		if(!list)
			break;
		ad = list->data;
	} while(ad->pos + ad->cells == i);

	for (list = prev; list; list = list->next)
		emit_applet_moved (panel, list->data);
}

static void
panel_widget_get_preferred_size(GtkWidget	     *widget,
				GtkRequisition *minimum_size,
				GtkRequisition *natural_size)
{
	PanelWidget *panel;
	GList *list;
	GList *ad_with_hints;
	gboolean dont_fill;
	gint scale;

	g_return_if_fail(PANEL_IS_WIDGET(widget));
	g_return_if_fail(minimum_size != NULL);

	panel = PANEL_WIDGET(widget);

	if (panel->orient == GTK_ORIENTATION_HORIZONTAL) {
		minimum_size->width = 0;
		minimum_size->height = panel->sz;
	} else {
		minimum_size->height = 0;
		minimum_size->width = panel->sz;
	}
	natural_size->width = minimum_size->width;
	natural_size->height = minimum_size->height;

	ad_with_hints = NULL;
	scale = gtk_widget_get_scale_factor(widget);

	for (list = panel->applet_list; list!=NULL; list = g_list_next(list)) {
		AppletData *ad = list->data;
		GtkRequisition child_min_size;
		GtkRequisition child_natural_size;
		gtk_widget_get_preferred_size(ad->applet,
		                              &child_min_size,
		                              &child_natural_size);

		if (panel->orient == GTK_ORIENTATION_HORIZONTAL) {
			if (minimum_size->height < child_min_size.height &&
			    !ad->size_constrained)
				minimum_size->height = child_min_size.height;
			if (natural_size->height < child_natural_size.height &&
			    !ad->size_constrained)
				natural_size->height = child_natural_size.height;

			if (panel->packed && ad->expand_major && ad->size_hints)
				ad_with_hints = g_list_prepend (ad_with_hints,
								ad);

			else if (panel->packed)
			{
				/* Just because everything is bigger when scaled up doesn't mean
				 * that the applets need any less room when the panel is packed. */
				minimum_size->width += child_min_size.width * scale;
				natural_size->width += child_natural_size.width * scale;
			}
		} else {
			if (minimum_size->width < child_min_size.width &&
			    !ad->size_constrained)
				minimum_size->width = child_min_size.width;
			if (natural_size->width < child_min_size.width &&
			    !ad->size_constrained)
				natural_size->width = child_min_size.width;

			if (panel->packed && ad->expand_major && ad->size_hints)
				ad_with_hints = g_list_prepend (ad_with_hints,
								ad);

			else if (panel->packed)
			{
				/* Just because everything is bigger when scaled up doesn't mean
				 * that the applets need any less room when the panel is packed. */
				minimum_size->height += child_min_size.height * scale;
				natural_size->height += child_natural_size.height * scale;
			}
		}
	}

	panel->nb_applets_size_hints = 0;
	if (panel->applets_hints != NULL)
		g_free (panel->applets_hints);
	panel->applets_hints = NULL;
	if (panel->applets_using_hint != NULL)
		g_free (panel->applets_using_hint);
	panel->applets_using_hint = NULL;

	if (panel->packed) {
		/* put the list in the correct order: this is important
		 * since we'll use this order in the size_allocate() */
		ad_with_hints = g_list_reverse (ad_with_hints);

		panel->nb_applets_size_hints = g_list_length (ad_with_hints);
		if (panel->nb_applets_size_hints > 0) {
			int i;
			panel->applets_hints = g_new0 (AppletSizeHints, panel->nb_applets_size_hints);

			i = 0;
			for (list = ad_with_hints;
			     list != NULL;
			     list = g_list_next (list)) {
				AppletData *ad = list->data;

				panel->applets_hints[i].hints = ad->size_hints;
				panel->applets_hints[i].len = ad->size_hints_len;
				i++;
			}

			panel->applets_using_hint = g_new0 (AppletSizeHintsAlloc, panel->nb_applets_size_hints);
		}
	}

	dont_fill = panel->packed && panel->nb_applets_size_hints != 0;

	if (panel->orient == GTK_ORIENTATION_HORIZONTAL) {
		if (minimum_size->width < 12 && !dont_fill)
			minimum_size->width = 12;
		if (minimum_size->height < 12)
			minimum_size->height = 12;
		if (natural_size->width < 12 && !dont_fill)
			natural_size->width = 12;
		if (natural_size->height < 12)
			natural_size->height = 12;
	} else {
		if (minimum_size->width < 12)
			minimum_size->width = 12;
		if (minimum_size->height < 12 && !dont_fill)
			minimum_size->height = 12;
		if (natural_size->width < 12)
			natural_size->width = 12;
		if (natural_size->height < 12 && !dont_fill)
			natural_size->height = 12;
	}
}

static void
panel_widget_get_preferred_width(GtkWidget *widget,
				 gint	   *minimum_width,
				 gint	   *natural_width)
{
	GtkRequisition req_min, req_natural;
	panel_widget_get_preferred_size(widget, &req_min, &req_natural);
	*minimum_width = req_min.width;
	*natural_width = req_natural.width;
}

static void
panel_widget_get_preferred_height(GtkWidget *widget,
				  gint	    *minimum_height,
				  gint	    *natural_height)
{
	GtkRequisition req_min, req_natural;
	panel_widget_get_preferred_size(widget, &req_min, &req_natural);
	*minimum_height = req_min.height;
	*natural_height = req_natural.height;
}

static void
queue_resize_on_all_applets(PanelWidget *panel)
{
	GList *li;
	for(li = panel->applet_list; li != NULL;
	    li = g_list_next(li)) {
		AppletData *ad = li->data;
		gtk_widget_queue_resize (ad->applet);
	}
}

static void
panel_widget_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	PanelWidget *panel;
	GList *list;
	int i;
	int old_size;
	gboolean ltr;

	g_return_if_fail(PANEL_IS_WIDGET(widget));
	g_return_if_fail(allocation!=NULL);

	panel = PANEL_WIDGET(widget);

	old_size = panel->size;
	ltr = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR;

	gtk_widget_set_allocation (widget, allocation);
	if (gtk_widget_get_realized (widget))
		gdk_window_move_resize (gtk_widget_get_window (widget),
					allocation->x,
					allocation->y,
					allocation->width,
					allocation->height);

	if(panel->orient == GTK_ORIENTATION_HORIZONTAL)
		panel->size = allocation->width;
	else
		panel->size = allocation->height;
	if(old_size<panel->size)
		panel_widget_right_stick(panel,old_size);

	if (panel->packed) {
		/* we're assuming the order is the same as the one that was
		 * in size_request() */
		int applet_using_hint_index = 0;

		i = 0;
		for(list = panel->applet_list;
		    list!=NULL;
		    list = g_list_next(list)) {
			AppletData *ad = list->data;
			GtkAllocation challoc;
			GtkRequisition chreq;
			gtk_widget_get_preferred_size (ad->applet, &chreq, NULL);

			ad->constrained = i;

			challoc.width = chreq.width;
			challoc.height = chreq.height;
			if(panel->orient == GTK_ORIENTATION_HORIZONTAL) {
				if (ad->expand_minor)
					challoc.height = allocation->height;

				if (ad->expand_major && ad->size_hints) {
					int width = panel->applets_using_hint[applet_using_hint_index].size;
					applet_using_hint_index++;
					challoc.width = MIN (width, allocation->width - i);
				}

				ad->cells = challoc.width;
				challoc.x = ltr ? ad->constrained : panel->size - ad->constrained - challoc.width;
				challoc.y = allocation->height / 2 - challoc.height / 2;
			} else {
				if (ad->expand_minor)
					challoc.width = allocation->width;

				if (ad->expand_major && ad->size_hints) {
					int height = panel->applets_using_hint[applet_using_hint_index].size;
					applet_using_hint_index++;
					challoc.height = MIN (height, allocation->height - i);
				}

				ad->cells = challoc.height;
				challoc.x = allocation->width / 2 - challoc.width / 2;
				challoc.y = ad->constrained;
			}
			ad->min_cells  = ad->cells;
			gtk_widget_size_allocate(ad->applet,&challoc);
			i += ad->cells;
		}

		/* EEEEK, there might be not enough room and we don't handle
		 * it: all the applets at the right well be unusable */

	} else { /*not packed*/

		/* First make sure there's enough room on the left */
		i = 0;
		for (list = panel->applet_list;
		     list != NULL;
		     list = g_list_next (list)) {
			AppletData *ad = list->data;
			GtkRequisition chreq;

			gtk_widget_get_preferred_size (ad->applet, &chreq, NULL);

			if (!ad->expand_major || !ad->size_hints) {
				if(panel->orient == GTK_ORIENTATION_HORIZONTAL)
					ad->cells = chreq.width;
				else
					ad->cells = chreq.height;

				ad->min_cells = ad->cells;
			} else {
				ad->cells = ad->size_hints [ad->size_hints_len - 1];
				ad->min_cells = ad->size_hints [ad->size_hints_len - 1];
			}

			ad->constrained = ad->pos;

			if (ad->constrained < i)
				ad->constrained = i;

			i = ad->constrained + ad->cells;
		}

		/* Now expand from the right */
		i = panel->size;
		for(list = g_list_last(panel->applet_list);
		    list!=NULL;
		    list = g_list_previous(list)) {
			AppletData *ad = list->data;
			int cells;

			if (ad->constrained + ad->min_cells > i)
				ad->constrained = MAX (i - ad->min_cells, 0);

			if (ad->expand_major) {
				cells = (i - ad->constrained) - 1;

				if (ad->size_hints)
					cells = get_size_from_hints (ad, cells);
				cells = MAX (cells, ad->min_cells);
				cells = MIN (cells, panel->size);

				ad->cells = cells;
			}

			i = ad->constrained;
		}

		/* EEEEK, there's not enough room, so shift applets even
		 * at the expense of perhaps running out of room on the
		 * right if there is no free space in the middle */
		if(i < 0) {
			i = 0;
			for(list = panel->applet_list;
			    list!=NULL;
			    list = g_list_next(list)) {
				AppletData *ad = list->data;

				if (ad->constrained < i)
					ad->constrained = i;

				i = ad->constrained + ad->cells;
			}
		}

		for(list = panel->applet_list;
		    list!=NULL;
		    list = g_list_next(list)) {
			AppletData *ad = list->data;
			GtkAllocation challoc;
			GtkRequisition chreq;
			gtk_widget_get_preferred_size (ad->applet, &chreq, NULL);

			challoc.width = chreq.width;
			challoc.height = chreq.height;
			if(panel->orient == GTK_ORIENTATION_HORIZONTAL) {
				challoc.width = ad->cells;
				if (ad->expand_minor) {
					challoc.height = allocation->height;
				}
				challoc.x = ltr ? ad->constrained : panel->size - ad->constrained - challoc.width;
				challoc.y = allocation->height / 2 - challoc.height / 2;
			} else {
				challoc.height = ad->cells;
				if (ad->expand_minor) {
					challoc.width = allocation->width;
				}
				challoc.x = allocation->width / 2 - challoc.width / 2;
				challoc.y = ad->constrained;
			}

			challoc.width = MAX(challoc.width, 1);
			challoc.height = MAX(challoc.height, 1);

			gtk_widget_size_allocate(ad->applet,&challoc);
		}
	}

	gtk_widget_queue_resize(widget);
}

gboolean
panel_widget_is_cursor(PanelWidget *panel, int overlap)
{
	GtkWidget     *widget;
	GtkAllocation allocation;
	GdkDevice      *device;
	int           x,y;
	int           w,h;

	g_return_val_if_fail(PANEL_IS_WIDGET(panel),FALSE);

	widget = panel->drop_widget;

	if(!widget ||
	   !GTK_IS_WIDGET(widget) ||
	   !gtk_widget_get_visible(widget))
		return FALSE;

	device = gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (widget)));
	gdk_window_get_device_position(gtk_widget_get_window (widget), device, &x, &y, NULL);

	gtk_widget_get_allocation (widget, &allocation);
	w = allocation.width;
	h = allocation.height;

	if((x+overlap)>=0 &&
	   (x-overlap)<=w &&
	   (y+overlap)>=0 &&
	   (y-overlap)<=h)
		return TRUE;
	return FALSE;
}

static void
panel_widget_finalize (GObject *obj)
{
	PanelWidget *panel;

	g_return_if_fail (PANEL_IS_WIDGET (obj));

	panel = PANEL_WIDGET (obj);

	if (panel->applets_hints != NULL)
		g_free (panel->applets_hints);
	panel->applets_hints = NULL;
	if (panel->applets_using_hint != NULL)
		g_free (panel->applets_using_hint);
	panel->applets_using_hint = NULL;


	G_OBJECT_CLASS (panel_widget_parent_class)->finalize (obj);
}

static void
panel_widget_open_dialog_destroyed (PanelWidget *panel_widget,
				    GtkWidget   *dialog)
{
	g_return_if_fail (panel_widget->open_dialogs != NULL);

	panel_widget->open_dialogs = g_slist_remove (panel_widget->open_dialogs, dialog);
}

static void
panel_widget_destroy_open_dialogs (PanelWidget *panel_widget)
{
	GSList *l, *list;

	list = panel_widget->open_dialogs;
	panel_widget->open_dialogs = NULL;

	for (l = list; l; l = l->next) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (l->data),
				G_CALLBACK (panel_widget_open_dialog_destroyed),
				panel_widget);
		gtk_widget_destroy (l->data);
	}
	g_slist_free (list);

}

static void
panel_widget_dispose (GObject *obj)
{
	PanelWidget *panel;

	g_return_if_fail (PANEL_IS_WIDGET (obj));

	panel = PANEL_WIDGET (obj);

	panels = g_slist_remove (panels, panel);

	panel_widget_destroy_open_dialogs (panel);

	if (panel->master_widget != NULL) {
		g_object_set_data (G_OBJECT (panel->master_widget),
				   MATE_PANEL_APPLET_ASSOC_PANEL_KEY,
				   NULL);
		g_object_remove_weak_pointer (G_OBJECT (panel->master_widget),
					      (gpointer *) &panel->master_widget);
		panel->master_widget = NULL;
	}

	G_OBJECT_CLASS (panel_widget_parent_class)->dispose (obj);
}

static void
panel_widget_init (PanelWidget *panel)
{
	GtkWidget *widget = (GtkWidget *) panel;

	gtk_widget_set_events (
		widget,
		gtk_widget_get_events (widget) | GDK_BUTTON_RELEASE_MASK);

	panel->packed        = FALSE;
	panel->orient        = GTK_ORIENTATION_HORIZONTAL;
	panel->size          = 0;
	panel->applet_list   = NULL;
	panel->master_widget = NULL;
	panel->drop_widget   = widget;
	panel->open_dialogs  = NULL;

	panel->nb_applets_size_hints = 0;
	panel->applets_hints = NULL;
	panel->applets_using_hint = NULL;

	panels = g_slist_append (panels, panel);
}

GtkWidget *
panel_widget_new (PanelToplevel  *toplevel,
		  gboolean        packed,
		  GtkOrientation  orient,
		  int             sz)
{
	PanelWidget *panel;

	panel = g_object_new (PANEL_TYPE_WIDGET, NULL);

	gtk_widget_set_has_window (GTK_WIDGET (panel), TRUE);
	gtk_widget_set_can_focus (GTK_WIDGET (panel), TRUE);

	panel->orient = orient;
	panel->sz = sz;

	panel->packed = packed;
	panel->size = 0;

	panel->toplevel    = toplevel;
	panel->drop_widget = GTK_WIDGET (toplevel);

	return GTK_WIDGET (panel);
}

static guint moving_timeout = 0;
static gboolean been_moved = FALSE;
static gboolean repeat_if_outside = FALSE;

static gboolean
panel_widget_applet_drag_start_no_grab (PanelWidget *panel,
					GtkWidget *applet,
					int drag_off)
{
	AppletData *ad;
	AppletInfo *info;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel), FALSE);
	g_return_val_if_fail (GTK_IS_WIDGET (panel), FALSE);

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	g_return_val_if_fail (ad != NULL, FALSE);

	if (ad->locked)
		return FALSE;

	/* Check if we can actually move this object in the
	   configuration */
	info = g_object_get_data (G_OBJECT (applet), "applet_info");
	if (info != NULL &&
	    ! mate_panel_applet_can_freely_move (info))
		return FALSE;

	if (moving_timeout != 0) {
		g_source_remove (moving_timeout);
		moving_timeout = 0;
		been_moved = FALSE;
	}

#ifdef PANEL_WIDGET_DEBUG
	g_message("Starting drag on a %s at %p\n",
		  g_type_name(G_TYPE_FROM_INSTANCE (applet)), applet);
#endif
	panel->currently_dragged_applet = ad;
	if (drag_off == PW_DRAG_OFF_CURSOR)
		ad->drag_off = panel_widget_get_cursorloc (panel) - ad->constrained;
	else if (drag_off == PW_DRAG_OFF_CENTER)
		ad->drag_off = ad->cells / 2;
	else
		ad->drag_off = drag_off;

	add_all_move_bindings (panel);

	mate_panel_applet_in_drag = TRUE;

	return TRUE;
}


static void
panel_widget_applet_drag_end_no_grab (PanelWidget *panel)
{
	g_return_if_fail (panel != NULL);
	g_return_if_fail (PANEL_IS_WIDGET (panel));

#ifdef PANEL_WIDGET_DEBUG
	g_message("Ending drag\n");
#endif
	panel->currently_dragged_applet = NULL;
	mate_panel_applet_in_drag = FALSE;

	remove_all_move_bindings (panel);
	if (moving_timeout != 0) {
		g_source_remove (moving_timeout);
		moving_timeout = 0;
		been_moved = FALSE;
	}
}

void
panel_widget_applet_drag_start (PanelWidget *panel,
				GtkWidget   *applet,
				int          drag_off,
				guint32      time_)
{
	GdkWindow *window;

	g_return_if_fail (PANEL_IS_WIDGET (panel));
	g_return_if_fail (GTK_IS_WIDGET (applet));

#ifdef PANEL_WIDGET_DEBUG
	g_message("Starting drag [grabbed] on a %s at %p\n",
		  g_type_name(G_TYPE_FROM_INSTANCE(applet)), applet);
#endif

	if (!panel_widget_applet_drag_start_no_grab (panel, applet, drag_off))
		return;

	panel_toplevel_push_autohide_disabler (panel->toplevel);

	gtk_grab_add (applet);

	window = gtk_widget_get_window (applet);
	if (window) {
		GdkGrabStatus  status;
		GdkCursor     *fleur_cursor;
		GdkDisplay *display;
		GdkSeat *seat;

		fleur_cursor = gdk_cursor_new_for_display (gdk_display_get_default (),
		                                           GDK_FLEUR);

		display = gdk_window_get_display (window);
		seat = gdk_display_get_default_seat (display);

		status = gdk_seat_grab (seat, window, GDK_SEAT_CAPABILITY_POINTER,
		                        FALSE, fleur_cursor, NULL, NULL, NULL);

		g_object_unref (fleur_cursor);
		gdk_display_flush (display);

		if (status != GDK_GRAB_SUCCESS) {
			g_warning (G_STRLOC ": failed to grab pointer (errorcode: %d)",
				   status);
			panel_widget_applet_drag_end (panel);
		}
	}
}

void
panel_widget_applet_drag_end (PanelWidget *panel)
{
	GdkDisplay *display;
	GdkSeat *seat;

	g_return_if_fail (PANEL_IS_WIDGET (panel));

	if (panel->currently_dragged_applet == NULL)
		return;

	display = gtk_widget_get_display (GTK_WIDGET (panel));
	seat = gdk_display_get_default_seat (display);

	gdk_seat_ungrab (seat);

	gtk_grab_remove (panel->currently_dragged_applet->applet);
	panel_widget_applet_drag_end_no_grab (panel);
	panel_toplevel_pop_autohide_disabler (panel->toplevel);
	gdk_display_flush (display);
}

/*get pos of the cursor location in panel coordinates*/
int
panel_widget_get_cursorloc (PanelWidget *panel)
{
	GdkDevice      *device;
	int x, y;
	gboolean rtl;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel), -1);

	device = gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (GTK_WIDGET(panel))));
	gdk_window_get_device_position(gtk_widget_get_window (GTK_WIDGET(panel)), device, &x, &y, NULL);
	rtl = gtk_widget_get_direction (GTK_WIDGET (panel)) == GTK_TEXT_DIR_RTL;

	if (panel->orient == GTK_ORIENTATION_HORIZONTAL)
		return (rtl ? panel->size - x : x);
	else
		return y;
}

/*calculates the value to move the applet by*/
static int
panel_widget_get_moveby (PanelWidget *panel, int pos, int offset)
{
	g_return_val_if_fail (PANEL_IS_WIDGET (panel), -1);

	return panel_widget_get_cursorloc (panel) - offset - pos;
}

static GList *
walk_up_to (int pos, GList *list)
{
	AppletData *ad;

	g_return_val_if_fail (list != NULL, NULL);

	ad = list->data;

	if (ad->constrained <= pos &&
	    ad->constrained + ad->cells > pos)
		return list;
	while (list->next != NULL &&
	       ad->constrained + ad->cells <= pos) {
		list = list->next;
		ad = list->data;
	}
	while (list->prev != NULL &&
	       ad->constrained > pos) {
		list = list->prev;
		ad = list->data;
	}
	return list;
}

static GtkWidget *
is_in_applet (int pos, AppletData *ad)
{
	g_return_val_if_fail (ad != NULL, NULL);

	if (ad->constrained <= pos &&
	    ad->constrained + ad->cells > pos)
		return ad->applet;
	return NULL;
}

static int
panel_widget_get_free_spot (PanelWidget *panel,
			    AppletData  *ad,
			    int          place)
{
	int i, e;
	int start;
	int right = -1, left = -1;
	GList *list;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel), -1);
	g_return_val_if_fail (ad != NULL, -1);

	if (ad->constrained >= panel->size)
		return -1;

	if (panel->applet_list == NULL) {
		if (place + ad->min_cells > panel->size)
			return panel->size-ad->min_cells;
		else
			return place;
	}

	list = panel->applet_list;

	start = place - ad->drag_off;
	if (start < 0)
		start = 0;
	for (e = 0, i = start; i < panel->size; i++) {
		GtkWidget *applet;
		list = walk_up_to (i, list);
		applet = is_in_applet (i, list->data);
		if (applet == NULL ||
		    applet == ad->applet) {
			e++;
			if (e >= ad->min_cells) {
				right = i - e + 1;
				break;
			}
		} else {
			e = 0;
		}
	}

	start = place + ad->drag_off;
	if (start >= panel->size)
		start = panel->size - 1;
	for (e = 0, i = start; i >= 0; i--) {
		GtkWidget *applet;
		list = walk_up_to (i, list);
		applet = is_in_applet (i, list->data);
		if (applet == NULL ||
		    applet == ad->applet) {
			e++;
			if (e >= ad->min_cells) {
				left = i;
				break;
			}
		} else {
			e=0;
		}
	}

	start = place - ad->drag_off;

	if (left == -1) {
		if (right == -1)
			return -1;
		else
			return right;
	} else {
		if (right == -1)
			return left;
		else
			return abs (left - start) > abs (right - start) ?
				right : left;
	}
}

static void
panel_widget_nice_move (PanelWidget *panel,
			AppletData  *ad,
			int          pos)
{
	g_return_if_fail (PANEL_IS_WIDGET (panel));
	g_return_if_fail (ad != NULL);

	pos = panel_widget_get_free_spot (panel, ad, pos);
	if (pos < 0 || pos == ad->pos)
		return;

	ad->pos = ad->constrained = pos;

	panel->applet_list =
		panel_g_list_resort_item (panel->applet_list, ad,
					  (GCompareFunc)applet_data_compare);

	gtk_widget_queue_resize (GTK_WIDGET (panel));

	emit_applet_moved (panel, ad);
}

/* schedule to run the below function */
static void schedule_try_move (PanelWidget *panel, gboolean repeater);

/*find the cursor position and move the applet to that position*/
static void
panel_widget_applet_move_to_cursor (PanelWidget *panel)
{
	int moveby;
	int pos;
	int movement;
	GtkWidget *applet;
	GdkDevice      *device;
	GSList *forb;
	GdkModifierType mods;
	AppletData *ad;

	g_return_if_fail(PANEL_IS_WIDGET(panel));

	if (panel->currently_dragged_applet == NULL)
		return;

	ad = panel->currently_dragged_applet;

	pos = ad->constrained;

	applet = ad->applet;
	g_assert(GTK_IS_WIDGET(applet));
	forb = g_object_get_data (G_OBJECT(applet),
				  MATE_PANEL_APPLET_FORBIDDEN_PANELS);

	if(!panel_widget_is_cursor(panel,10)) {
		GSList *list;

		for(list=panels;
		    list!=NULL;
		    list=g_slist_next(list)) {
			PanelWidget *new_panel =
				PANEL_WIDGET(list->data);

			if (panel != new_panel &&
			    panel_widget_is_cursor (new_panel,10) &&
			    panel_screen_from_panel_widget (panel) ==
			    panel_screen_from_panel_widget (new_panel) &&
			    !g_slist_find (forb, new_panel) &&
			    !panel_lockdown_get_locked_down ()) {
				pos = panel_widget_get_moveby (new_panel, 0, ad->drag_off);

				if (pos < 0) pos = 0;

				panel_widget_applet_drag_end (panel);

				/*disable reentrancy into this function*/
				if (!panel_widget_reparent (panel, new_panel, applet, pos)) {
					panel_widget_applet_drag_start (
						panel, applet, ad->drag_off, GDK_CURRENT_TIME);
					continue;
				}

				panel_widget_applet_drag_start (
					new_panel, applet, ad->drag_off, GDK_CURRENT_TIME);
				schedule_try_move (new_panel, TRUE);

				return;
			}
		}
	}

	device = gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (GTK_WIDGET (panel))));
	gdk_window_get_device_position (gtk_widget_get_window (GTK_WIDGET(panel)), device, NULL, NULL, &mods);

	movement = PANEL_SWITCH_MOVE;

	if (panel->packed) {
		movement = PANEL_SWITCH_MOVE;
	} else {
		if (mods & GDK_CONTROL_MASK)
			movement = PANEL_SWITCH_MOVE;
		else if (mods & GDK_SHIFT_MASK)
			movement = PANEL_PUSH_MOVE;
		else if (mods & GDK_MOD1_MASK)
			movement = PANEL_FREE_MOVE;
	}

	switch (movement) {
	case PANEL_SWITCH_MOVE:
		moveby = panel_widget_get_moveby (panel, pos, ad->drag_off);
		panel_widget_switch_move (panel, ad, moveby);
		break;
	case PANEL_FREE_MOVE:
		panel_widget_nice_move (panel, ad, panel_widget_get_cursorloc (panel));
		break;
	case PANEL_PUSH_MOVE:
		moveby = panel_widget_get_moveby (panel, pos, ad->drag_off);
		panel_widget_push_move (panel, ad, moveby);
		break;
	}
}

static int
move_timeout_handler(gpointer data)
{
	PanelWidget   *panel = data;
	GdkDevice      *device;

	g_return_val_if_fail(PANEL_IS_WIDGET(data),FALSE);

	if(been_moved &&
	   panel->currently_dragged_applet) {
		panel_widget_applet_move_to_cursor(panel);
		been_moved = FALSE;
		return TRUE;
	}
	been_moved = FALSE;

	if(panel->currently_dragged_applet && repeat_if_outside) {
		GtkWidget     *widget;
		GtkAllocation  allocation;
		int            x,y;
		int            w,h;

		widget = panel->currently_dragged_applet->applet;

		device = gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (widget)));
		gdk_window_get_device_position (gtk_widget_get_window (widget), device, &x, &y, NULL);

		gtk_widget_get_allocation (widget, &allocation);
		w = allocation.width;
		h = allocation.height;

		/* if NOT inside return TRUE, this means we will be
		 * kept inside the timeout until we hit the damn widget
		 * or the drag ends */
		if(!(x>=0 && x<=w && y>=0 && y<=h))
			return TRUE;
	}

	moving_timeout = 0;

	return FALSE;
}

static void
schedule_try_move(PanelWidget *panel, gboolean repeater)
{
	if (!panel->currently_dragged_applet)
		return;
	repeat_if_outside = repeater;
	if(moving_timeout == 0) {
		been_moved = FALSE;
		panel_widget_applet_move_to_cursor(panel);
		moving_timeout =
			g_timeout_add (50, move_timeout_handler, panel);
	} else
		been_moved = TRUE;
}

static gboolean
panel_widget_applet_button_press_event (GtkWidget      *widget,
					GdkEventButton *event)
{
	GtkWidget   *parent;
	PanelWidget *panel;
	guint32      event_time;

	parent = gtk_widget_get_parent (widget);

	g_return_val_if_fail (PANEL_IS_WIDGET (parent), FALSE);

	panel = PANEL_WIDGET (parent);

	/* don't propagate this event */
	if (panel->currently_dragged_applet) {
		g_signal_stop_emission (G_OBJECT (widget),
					g_signal_lookup ("button-press-event",
							 G_OBJECT_TYPE (widget)),
					0);
		return TRUE;
	}

	/* Begin drag if the middle mouse button is pressed, unless the panel
	 * is locked down or a grab is active (meaning a menu is open) */
	if (panel_lockdown_get_locked_down () || event->button != 2 ||
	    gtk_grab_get_current() != NULL)
		return FALSE;

	/* time on sent events seems to be bogus */
	event_time = event->time;
	if (event->send_event)
		event_time = GDK_CURRENT_TIME;

	panel_widget_applet_drag_start (panel, widget, PW_DRAG_OFF_CURSOR, event_time);

	return TRUE;
}

static gboolean
panel_widget_applet_button_release_event (GtkWidget      *widget,
					  GdkEventButton *event)
{
	GtkWidget   *parent;
	PanelWidget *panel;

	parent = gtk_widget_get_parent (widget);

	g_return_val_if_fail (PANEL_IS_WIDGET (parent), FALSE);

	panel = PANEL_WIDGET (parent);

	/* don't propagate this event */
	if (panel->currently_dragged_applet) {
		g_signal_stop_emission (G_OBJECT (widget),
					g_signal_lookup ("button-release-event",
							 G_OBJECT_TYPE (widget)),
					0);
		panel_widget_applet_drag_end (panel);
		return TRUE;
	}

	return FALSE;
}

static gboolean
panel_widget_applet_motion_notify_event (GtkWidget *widget,
					 GdkEvent  *event)
{
	GtkWidget   *parent;
	PanelWidget *panel;

	parent = gtk_widget_get_parent (widget);

	g_return_val_if_fail (PANEL_IS_WIDGET (parent), FALSE);

	if (gdk_event_get_screen (event) != gtk_widget_get_screen (widget))
		return FALSE;

	panel = PANEL_WIDGET (parent);

	schedule_try_move (panel, FALSE);

	return FALSE;
}

static gboolean
panel_widget_applet_key_press_event (GtkWidget   *widget,
				     GdkEventKey *event)
{
	GtkWidget   *parent;
	PanelWidget *panel;

	parent = gtk_widget_get_parent (widget);

	g_return_val_if_fail (PANEL_IS_WIDGET (parent), FALSE);

	panel = PANEL_WIDGET (parent);

	if (!mate_panel_applet_in_drag)
		return FALSE;

	return gtk_bindings_activate (G_OBJECT (panel),
				      ((GdkEventKey *)event)->keyval,
				      ((GdkEventKey *)event)->state);
}

static int
panel_sub_event_handler(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	g_return_val_if_fail(GTK_IS_WIDGET(widget),FALSE);
	g_return_val_if_fail(event!=NULL,FALSE);

	switch (event->type) {
		/*pass these to the parent!*/
		case GDK_BUTTON_PRESS:
		case GDK_BUTTON_RELEASE:
		case GDK_MOTION_NOTIFY: {
			GdkEventButton *bevent = (GdkEventButton *)event;

			if (bevent->button != 1 || mate_panel_applet_in_drag)
				return gtk_widget_event (data, event);

			}
			break;
		case GDK_KEY_PRESS:
			if (mate_panel_applet_in_drag)
				return gtk_widget_event(data, event);
			break;
		default:
			break;
	}

	return FALSE;
}


static void
bind_applet_events(GtkWidget *widget, gpointer data)
{
	g_return_if_fail(GTK_IS_WIDGET(widget));

	/* XXX: This is more or less a hack.  We need to be able to
	 * capture events over applets so that we can drag them with
	 * the mouse and such.  So we need to force the applet's
	 * widgets to recursively send the events back to their parent
	 * until the event gets to the applet wrapper (the
	 * GtkEventBox) for processing by us.
	 */

	if (gtk_widget_get_has_window (widget))
		g_signal_connect (G_OBJECT(widget), "event",
				  G_CALLBACK (panel_sub_event_handler),
				  data);

	if (GTK_IS_CONTAINER(widget))
		gtk_container_foreach (GTK_CONTAINER (widget),
				       bind_applet_events, data);
}

static void
panel_widget_applet_destroy (GtkWidget *applet, gpointer data)
{
	AppletData *ad;
	GtkWidget  *parent;

	g_return_if_fail (GTK_IS_WIDGET (applet));

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	g_object_set_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA, NULL);

	parent = gtk_widget_get_parent (applet);
	/*if it wasn't yet removed*/
	if(parent) {
		PanelWidget *panel = PANEL_WIDGET (parent);

		if (panel->currently_dragged_applet == ad)
			panel_widget_applet_drag_end (panel);

		panel->applet_list = g_list_remove (panel->applet_list,ad);
	}

	g_free (ad->size_hints);

	g_free (ad);
}

static void
bind_top_applet_events (GtkWidget *widget)
{
	g_return_if_fail(GTK_IS_WIDGET(widget));

	g_signal_connect (G_OBJECT(widget), "destroy",
			  G_CALLBACK (panel_widget_applet_destroy),
			  NULL);

	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (panel_widget_applet_button_press_event),
			  NULL);

	g_signal_connect (widget, "button-release-event",
			  G_CALLBACK (panel_widget_applet_button_release_event),
			  NULL);
	g_signal_connect (widget, "motion-notify-event",
			  G_CALLBACK (panel_widget_applet_motion_notify_event),
			  NULL);
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (panel_widget_applet_key_press_event),
			  NULL);

	/* XXX: This is more or less a hack.  We need to be able to
	 * capture events over applets so that we can drag them with
	 * the mouse and such.  So we need to force the applet's
	 * widgets to recursively send the events back to their parent
	 * until the event gets to the applet wrapper (the
	 * GtkEventBox) for processing by us.
	 */

	if (GTK_IS_CONTAINER(widget))
		gtk_container_foreach (GTK_CONTAINER (widget),
				       bind_applet_events, widget);
}

static int
panel_widget_find_empty_pos(PanelWidget *panel, int pos)
{
	int i;
	int right=-1,left=-1;
	GList *list;

	g_return_val_if_fail(PANEL_IS_WIDGET(panel),-1);

	if(pos>=panel->size)
		pos = panel->size-1;

	if (pos <= 0)
		pos = 0;

	if(!panel->applet_list)
		return pos;

	list = panel->applet_list;

	for (i = pos; i < panel->size; i++) {
		list = walk_up_to (i, list);
		if ( ! is_in_applet (i, list->data)) {
			right = i;
			break;
		}
	}

	for(i = pos; i >= 0; i--) {
		list = walk_up_to (i, list);
		if ( ! is_in_applet (i, list->data)) {
			left = i;
			break;
		}
	}

	if (left == -1) {
		if (right == -1)
			return -1;
		else
			return right;
	} else {
		if (right == -1)
			return left;
		else
			return abs (left - pos) > abs (right - pos) ?
				right : left;
	}
}

void
panel_widget_add_forbidden (PanelWidget *panel)
{
	g_return_if_fail (panel != NULL);
	g_return_if_fail (PANEL_IS_WIDGET (panel));

	add_panel_to_forbidden (panel, panel);
}

int
panel_widget_add (PanelWidget *panel,
		  GtkWidget   *applet,
		  gboolean     locked,
		  int          pos,
		  gboolean     insert_at_pos)
{
	AppletData *ad = NULL;

	g_return_val_if_fail (PANEL_IS_WIDGET (panel), -1);
	g_return_val_if_fail (GTK_IS_WIDGET (applet), -1);

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);

	if (ad != NULL)
		pos = ad->pos;

	if (!insert_at_pos || pos < 0) {
		if (panel->packed) {
			if (get_applet_list_pos (panel, pos))
				/*this is a slight hack so that this applet
				  is inserted AFTER an applet with this pos
				  number*/
				pos++;
		} else {
			int newpos = panel_widget_find_empty_pos (panel, pos);
			if (newpos >= 0)
				pos = newpos;
			else if (get_applet_list_pos (panel, pos))
				/*this is a slight hack so that this applet
				  is inserted AFTER an applet with this pos
				  number*/
				pos++;
		}
	}

	if(pos==-1) return -1;

	if (ad == NULL) {
		ad = g_new (AppletData, 1);
		ad->applet = applet;
		ad->cells = 1;
		ad->min_cells = 1;
		ad->pos = pos;
		ad->constrained = pos;
		ad->drag_off = 0;
		ad->size_constrained = FALSE;
		ad->expand_major = FALSE;
		ad->expand_minor = FALSE;
		ad->locked = locked;
		ad->size_hints = NULL;
		g_object_set_data (G_OBJECT (applet),
				   MATE_PANEL_APPLET_DATA, ad);

		/*this is a completely new applet, which was not yet bound*/
		bind_top_applet_events (applet);
	}

	panel->applet_list =
		g_list_insert_sorted(panel->applet_list,ad,
				     (GCompareFunc)applet_data_compare);

	/*this will get done right on size allocate!*/
	if(panel->orient == GTK_ORIENTATION_HORIZONTAL)
		gtk_fixed_put(GTK_FIXED(panel),applet,
			      pos,0);
	else
		gtk_fixed_put(GTK_FIXED(panel),applet,
			      0,pos);


	gtk_widget_queue_resize(GTK_WIDGET(panel));

	g_signal_emit (G_OBJECT(panel),
		       panel_widget_signals[APPLET_ADDED_SIGNAL],
		       0, applet);

	/*NOTE: forbidden list is not updated on addition, use the
	function above for the panel*/

	return pos;
}

gboolean
panel_widget_reparent (PanelWidget *old_panel,
		       PanelWidget *new_panel,
		       GtkWidget *applet,
		       int pos)
{
	AppletData *ad;
	GtkWidget *focus_widget = NULL;
	AppletInfo* info;
	GdkDisplay *display;

	g_return_val_if_fail(PANEL_IS_WIDGET(old_panel), FALSE);
	g_return_val_if_fail(PANEL_IS_WIDGET(new_panel), FALSE);
	g_return_val_if_fail(GTK_IS_WIDGET(applet), FALSE);
	g_return_val_if_fail(pos>=0, FALSE);

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	g_return_val_if_fail(ad!=NULL, FALSE);

	/* Don't try and reparent to an explicitly hidden panel,
	 * very confusing for the user ...
	 */
	if (panel_toplevel_get_is_hidden (new_panel->toplevel))
		return FALSE;

	info = g_object_get_data (G_OBJECT (ad->applet), "applet_info");

	ad->pos = ad->constrained = panel_widget_get_free_spot (new_panel, ad, pos);
	if (ad->pos == -1)
		ad->pos = ad->constrained = 0;

	gtk_widget_queue_resize (GTK_WIDGET (new_panel));
	gtk_widget_queue_resize (GTK_WIDGET (old_panel));

	panel_widget_reset_saved_focus (old_panel);
	if (gtk_container_get_focus_child (GTK_CONTAINER (old_panel)) == applet)
		focus_widget = gtk_window_get_focus (GTK_WINDOW (old_panel->toplevel));

	/* Do not touch until GTK+4
	 * or until we figure out how to properly
	 * reimplement gtk_widget_reparent.
	 * https://github.com/mate-desktop/mate-panel/issues/504
	 */
	gtk_widget_reparent (applet, GTK_WIDGET (new_panel));

	if (info && info->type == PANEL_OBJECT_APPLET)
		mate_panel_applet_frame_set_panel (MATE_PANEL_APPLET_FRAME (ad->applet), new_panel);

	if (gtk_widget_get_can_focus (GTK_WIDGET (new_panel)))
		gtk_widget_set_can_focus (GTK_WIDGET (new_panel), FALSE);
	if (focus_widget) {
		panel_widget_force_grab_focus (focus_widget);
	} else {
		gboolean return_val;

		g_signal_emit_by_name (applet, "focus",
				       GTK_DIR_TAB_FORWARD,
				       &return_val);
	}
 	gtk_window_present (GTK_WINDOW (new_panel->toplevel));

	display = gtk_widget_get_display (GTK_WIDGET (new_panel));
	gdk_display_flush (display);

	emit_applet_moved (new_panel, ad);

	return TRUE;
}

void
panel_widget_set_packed (PanelWidget *panel_widget,
			 gboolean     packed)
{
	panel_widget->packed = packed;

	gtk_widget_queue_resize (GTK_WIDGET (panel_widget));
}

void
panel_widget_set_orientation (PanelWidget    *panel_widget,
			      GtkOrientation  orientation)
{
	panel_widget->orient = orientation;

	gtk_widget_queue_resize (GTK_WIDGET (panel_widget));
}

void
panel_widget_set_size (PanelWidget *panel_widget,
		       int          size)
{
	g_return_if_fail (PANEL_IS_WIDGET (panel_widget));

	if (size == panel_widget->sz)
		return;

	panel_widget->sz = size;

	queue_resize_on_all_applets (panel_widget);

	g_signal_emit (panel_widget, panel_widget_signals [SIZE_CHANGE_SIGNAL], 0);

	gtk_widget_queue_resize (GTK_WIDGET (panel_widget));
}

void
panel_widget_emit_background_changed (PanelWidget *panel)
{
	g_signal_emit (panel, panel_widget_signals [BACK_CHANGE_SIGNAL], 0);
}

static void
panel_widget_push_move_applet (PanelWidget     *panel,
                               GtkDirectionType dir)
{
	AppletData *applet;
	int         increment = 0;

	applet = panel->currently_dragged_applet;
	g_return_if_fail (applet);

	switch (dir) {
	case GTK_DIR_LEFT:
	case GTK_DIR_UP:
		increment = -MOVE_INCREMENT;
		break;
	case GTK_DIR_RIGHT:
	case GTK_DIR_DOWN:
		increment = MOVE_INCREMENT;
		break;
	default:
		return;
	}

	panel_widget_push_move (panel, applet, increment);
}

static void
panel_widget_switch_move_applet (PanelWidget      *panel,
                                 GtkDirectionType  dir)
{
	AppletData *applet;
	GList      *list;

	applet = panel->currently_dragged_applet;
	g_return_if_fail (applet != NULL);

	list = g_list_find (panel->applet_list, applet);
	g_return_if_fail (list != NULL);

	switch (dir) {
	case GTK_DIR_LEFT:
	case GTK_DIR_UP:
		panel_widget_switch_applet_left (panel, list);
		break;
	case GTK_DIR_RIGHT:
	case GTK_DIR_DOWN:
		panel_widget_switch_applet_right (panel, list);
		break;
	default:
		return;
	}
}

static void
panel_widget_free_move_applet (PanelWidget      *panel,
                               GtkDirectionType  dir)
{
	AppletData *ad;
	gint        increment = MOVE_INCREMENT;

	ad = panel->currently_dragged_applet;

	g_return_if_fail (ad);

	switch (dir) {
	case GTK_DIR_LEFT:
	case GTK_DIR_UP:
		increment = -increment;
		break;
	case GTK_DIR_RIGHT:
	case GTK_DIR_DOWN:
		break;
	default:
		return;
	}

	panel_widget_nice_move (panel, ad, increment + ad->constrained + ad->drag_off);
}

static void
panel_widget_tab_move (PanelWidget *panel,
                       gboolean     next)
{
	PanelWidget *new_panel = NULL;
	PanelWidget *previous_panel = NULL;
	AppletData  *ad;
	GSList      *l;

	ad = panel->currently_dragged_applet;

	if (!ad)
		return;

	for (l = panels; l; l = l->next) {
		PanelWidget *panel_in_list = l->data;

		if (panel_in_list == panel) {
			if (next) {
				if (l->next)
					new_panel = l->next->data;
				else
					new_panel = ((GSList *)panels)->data;

			} else {
				if (previous_panel)
					new_panel = previous_panel;
				else
					continue;
			}
			break;
		} else {
			if (!next)
				previous_panel = panel_in_list;
		}
	}

	g_return_if_fail (l != NULL);

	if (!new_panel && previous_panel)
		new_panel = previous_panel;

	if (new_panel &&
	    (new_panel != panel) &&
	    !panel_lockdown_get_locked_down ())
		panel_widget_reparent (panel, new_panel, ad->applet, 0);
}

static void
panel_widget_end_move (PanelWidget *panel)
{
	panel_widget_applet_drag_end (panel);
}

static gboolean
panel_widget_real_focus (GtkWidget        *widget,
                         GtkDirectionType  direction)
{
	if (gtk_widget_get_can_focus (widget) && gtk_container_get_children (GTK_CONTAINER (widget))) {
		gtk_widget_set_can_focus (widget, FALSE);
	}
	return GTK_WIDGET_CLASS (panel_widget_parent_class)->focus (widget, direction);
}

void
panel_widget_focus (PanelWidget *panel_widget)
{
	if (panel_toplevel_get_is_attached (panel_widget->toplevel))
		return;

	/*
         * Set the focus back on the panel; we unset the focus child so that
	 * the next time focus is inside the panel we do not remember the
	 * previously focused child. We also need to set GTK_CAN_FOCUS flag
	 * on the panel as it is unset when this function is called.
	 */
	gtk_container_set_focus_child (GTK_CONTAINER (panel_widget), NULL);
	gtk_widget_set_can_focus (GTK_WIDGET (panel_widget), TRUE);
	gtk_widget_grab_focus (GTK_WIDGET (panel_widget));
}


PanelOrientation
panel_widget_get_applet_orientation (PanelWidget *panel)
{
	g_return_val_if_fail (PANEL_IS_WIDGET (panel), PANEL_ORIENTATION_TOP);
	g_return_val_if_fail (PANEL_IS_TOPLEVEL (panel->toplevel), PANEL_ORIENTATION_TOP);

	return panel_toplevel_get_orientation (panel->toplevel);
}

void
panel_widget_set_applet_size_constrained (PanelWidget *panel,
					  GtkWidget   *applet,
					  gboolean     size_constrained)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return;

	size_constrained = size_constrained != FALSE;

	if (ad->size_constrained == size_constrained)
		return;

	ad->size_constrained = size_constrained;

	gtk_widget_queue_resize (GTK_WIDGET (panel));
}

void
panel_widget_set_applet_expandable (PanelWidget *panel,
				    GtkWidget   *applet,
				    gboolean     major,
				    gboolean     minor)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return;

	major = major != FALSE;
	minor = minor != FALSE;

	if (ad->expand_major == major && ad->expand_minor == minor)
		return;

	ad->expand_major = major;
	ad->expand_minor = minor;

	gtk_widget_queue_resize (GTK_WIDGET (panel));
}

void
panel_widget_set_applet_size_hints (PanelWidget *panel,
				    GtkWidget   *applet,
				    int         *size_hints,
				    int          size_hints_len)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return;

	g_free (ad->size_hints);

	if (size_hints_len > 0 && (size_hints_len % 2 == 0)) {
		ad->size_hints     = size_hints;
		ad->size_hints_len = size_hints_len;
	} else {
		g_free (size_hints);
		ad->size_hints = NULL;
	}

	gtk_widget_queue_resize (GTK_WIDGET (panel));
}

void
panel_widget_set_applet_locked (PanelWidget *panel,
				GtkWidget   *applet,
				gboolean     locked)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return;

	ad->locked = locked;
}

gboolean
panel_widget_get_applet_locked (PanelWidget *panel,
				GtkWidget   *applet)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return FALSE;

	return ad->locked;
}

gboolean
panel_widget_toggle_applet_locked (PanelWidget *panel,
				   GtkWidget   *applet)
{
	AppletData *ad;

	ad = g_object_get_data (G_OBJECT (applet), MATE_PANEL_APPLET_DATA);
	if (!ad)
		return FALSE;

	return ad->locked = !ad->locked;
}

gboolean
mate_panel_applet_is_in_drag (void)
{
	return mate_panel_applet_in_drag;
}

void
panel_widget_register_open_dialog (PanelWidget *panel,
				   GtkWidget   *dialog)
{
	/* the window is for a panel, so it should be shown in the taskbar. See
	 * HIG: An alert should not appear in the panel window list unless it
	 * is, or may be, the only window shown by an application. */
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);

	panel->open_dialogs = g_slist_append (panel->open_dialogs,
					      dialog);

	g_signal_connect_object (dialog, "destroy",
				 G_CALLBACK (panel_widget_open_dialog_destroyed),
				 panel,
				 G_CONNECT_SWAPPED);
}
