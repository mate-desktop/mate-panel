/* Mate panel: panel widget
 * (C) 1997-1998 the Free Software Foundation
 *
 * Authors:  George Lebl
 */
/* This widget, although slightly written as a general purpose widget, it
   has MANY interdependencies, which makes it almost impossible to use in
   anything else but the panel, what it would need is some serious
   cleaning up*/
#ifndef PANEL_WIDGET_H
#define PANEL_WIDGET_H


#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "button-widget.h"
#include "panel-types.h"
#include "panel-background.h"
#include "panel-toplevel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_WIDGET \
	(panel_widget_get_type())
#define PANEL_WIDGET(object) \
	(G_TYPE_CHECK_INSTANCE_CAST((object), PANEL_TYPE_WIDGET, PanelWidget))
#define PANEL_WIDGET_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), PANEL_TYPE_WIDGET, PanelWidgetClass))
#define PANEL_IS_WIDGET(object) \
	(G_TYPE_CHECK_INSTANCE_TYPE((object), PANEL_TYPE_WIDGET))

#define PANEL_MINIMUM_WIDTH 12

#define MATE_PANEL_APPLET_ASSOC_PANEL_KEY "mate_panel_applet_assoc_panel_key"
#define MATE_PANEL_APPLET_FORBIDDEN_PANELS "mate_panel_applet_forbidden_panels"
#define MATE_PANEL_APPLET_DATA "mate_panel_applet_data"

#ifndef TYPEDEF_PANEL_WIDGET
typedef struct _PanelWidget		PanelWidget;
#define TYPEDEF_PANEL_WIDGET
#endif /* TYPEDEF_PANEL_WIDGET */

typedef struct _PanelWidgetClass	PanelWidgetClass;

typedef struct _AppletRecord		AppletRecord;
typedef struct _AppletData		AppletData;
typedef struct _DNDRecord		DNDRecord;

typedef struct _AppletSizeHints		AppletSizeHints;
typedef struct _AppletSizeHintsAlloc	AppletSizeHintsAlloc;

struct _AppletSizeHints {
	int *hints;
	int  len;
};

struct _AppletSizeHintsAlloc {
	int index;
	int size;
};

struct _AppletData
{
	GtkWidget *	applet;
	int		pos;
	int             constrained;
	int		cells;
	int             min_cells;

	int		drag_off; /* offset on the applet where drag
				     was started */

	/* Valid size ranges for expanded applets */
	int *           size_hints;
	int             size_hints_len;

	guint           size_constrained : 1;
	guint           expand_major : 1;
	guint           expand_minor : 1;
	guint           locked : 1;

};

struct _PanelWidget
{
	GtkFixed        fixed;

	GList          *applet_list;

	GSList         *open_dialogs;

	int             size;
	GtkOrientation  orient;
	int             sz;

	AppletData     *currently_dragged_applet;

	PanelBackground background;

	GtkWidget      *master_widget;

	GtkWidget      *drop_widget;     /* widget that the panel checks for
	                                  * the cursor on drops usually the
	                                  * panel widget itself
	                                  */

	PanelToplevel  *toplevel;

	GdkEventKey    *key_event;

	/* helpers to get a good size in packed panels with applets using
	 * size hints */
	int                   nb_applets_size_hints;
	AppletSizeHints      *applets_hints;
	AppletSizeHintsAlloc *applets_using_hint;

	guint           packed : 1;
};

struct _PanelWidgetClass
{
	GtkFixedClass parent_class;

	void (* size_change) (PanelWidget *panel);
	void (* back_change) (PanelWidget *panel);
	void (* applet_move) (PanelWidget *panel,
			      GtkWidget *applet);
	void (* applet_added) (PanelWidget *panel,
			       GtkWidget *applet);
	void (* applet_removed) (PanelWidget *panel,
				 GtkWidget *applet);
	void (* push_move) (PanelWidget		*panel,
                            GtkDirectionType	 dir);
	void (* switch_move) (PanelWidget	*panel,
                              GtkDirectionType	 dir);
	void (* free_move) (PanelWidget		*panel,
                            GtkDirectionType	 dir);
	void (* tab_move) (PanelWidget	*panel,
                           gboolean	 next);
	void (* end_move) (PanelWidget	*panel);
};

GType		panel_widget_get_type		(void) G_GNUC_CONST;

GtkWidget *	panel_widget_new		(PanelToplevel  *toplevel,
						 gboolean        packed,
						 GtkOrientation  orient,
						 int             sz);
/*add an applet to the panel, preferably at position pos, if insert_at_pos
  is on, we REALLY want to insert at the pos given by pos*/
int		panel_widget_add		(PanelWidget *panel,
						 GtkWidget   *applet,
						 gboolean     locked,
						 int          pos,
						 gboolean     insert_at_pos);

/*needs to be called for drawers after add*/
void		panel_widget_add_forbidden	(PanelWidget *panel);

/*move applet to a different panel*/
int		panel_widget_reparent		(PanelWidget *old_panel,
						 PanelWidget *new_panel,
						 GtkWidget *applet,
						 int pos);

/* use these for drag_off for special cases */
#define PW_DRAG_OFF_CURSOR -1
#define PW_DRAG_OFF_CENTER -2

/*drag*/
gboolean        mate_panel_applet_is_in_drag         (void);
void		panel_widget_applet_drag_start	(PanelWidget *panel,
						 GtkWidget   *applet,
						 int          drag_off,
						 guint32      time_);
void		panel_widget_applet_drag_end	(PanelWidget *panel);

void            panel_widget_set_packed         (PanelWidget    *panel_widget,
						 gboolean        packed);
void            panel_widget_set_orientation    (PanelWidget    *panel_widget,
						 GtkOrientation  orientation);
void            panel_widget_set_size           (PanelWidget    *panel_widget,
						 int             size);

/*draw EVERYTHING (meaning icons)*/
void		panel_widget_draw_all		(PanelWidget *panel,
						 GdkRectangle *area);
/*draw just one icon (applet has to be an icon of course)*/
void		panel_widget_draw_icon		(PanelWidget *panel,
						 ButtonWidget *applet);


/*tells us if an applet is "stuck" on the right side*/
int		panel_widget_is_applet_stuck	(PanelWidget *panel,
						 GtkWidget *applet);
/*get pos of the cursor location in panel coordinates*/
int		panel_widget_get_cursorloc	(PanelWidget *panel);

/*needed for other panel types*/
gboolean	panel_widget_is_cursor		(PanelWidget *panel,
						 int overlap);
/* set the focus on the panel */
void            panel_widget_focus              (PanelWidget *panel);

PanelOrientation panel_widget_get_applet_orientation (PanelWidget *panel);

void     panel_widget_emit_background_changed (PanelWidget *panel);

void     panel_widget_set_applet_size_constrained (PanelWidget *panel,
						   GtkWidget   *applet,
						   gboolean     size_constrained);
void     panel_widget_set_applet_expandable       (PanelWidget *panel,
						   GtkWidget   *applet,
						   gboolean     major,
						   gboolean     minor);
void     panel_widget_set_applet_size_hints       (PanelWidget *panel,
						   GtkWidget   *applet,
						   int         *size_hints,
						   int          size_hints_len);

void     panel_widget_set_applet_locked           (PanelWidget *panel,
						   GtkWidget   *applet,
						   gboolean     locked);
gboolean panel_widget_get_applet_locked           (PanelWidget *panel,
						   GtkWidget   *applet);
gboolean panel_widget_toggle_applet_locked        (PanelWidget *panel,
						   GtkWidget   *applet);

void     panel_widget_register_open_dialog        (PanelWidget *panel,
						   GtkWidget   *dialog);
#ifdef __cplusplus
}
#endif

#endif /* PANEL_WIDGET_H */
