#ifndef BUTTON_WIDGET_H
#define BUTTON_WIDGET_H

#include <gtk/gtk.h>
#include "panel-enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_TYPE_WIDGET		(button_widget_get_type ())
#define BUTTON_WIDGET(object)          	(G_TYPE_CHECK_INSTANCE_CAST ((object), BUTTON_TYPE_WIDGET, ButtonWidget))
#define BUTTON_WIDGET_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass, BUTTON_TYPE_WIDGET, ButtonWidgetClass))
#define BUTTON_IS_WIDGET(object)    	(G_TYPE_CHECK_INSTANCE_TYPE ((object), BUTTON_TYPE_WIDGET))
#define BUTTON_IS_WIDGET_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), BUTTON_TYPE_WIDGET))

typedef struct _ButtonWidget		ButtonWidget;
typedef struct _ButtonWidgetClass	ButtonWidgetClass;
typedef struct _ButtonWidgetPrivate	ButtonWidgetPrivate;

struct _ButtonWidget {
	GtkButton            parent;

	ButtonWidgetPrivate *priv;
};

struct _ButtonWidgetClass {
	GtkButtonClass parent_class;
};

GType            button_widget_get_type          (void) G_GNUC_CONST;
GtkWidget *      button_widget_new               (const char       *pixmap,
						  gboolean          arrow,
						  PanelOrientation  orientation);
void             button_widget_set_activatable   (ButtonWidget     *button,
						  gboolean          activatable);
gboolean         button_widget_get_activatable   (ButtonWidget     *button);
void             button_widget_set_icon_name     (ButtonWidget     *button,
						  const char       *icon_name);
const char *     button_widget_get_icon_name     (ButtonWidget     *button);
void             button_widget_set_orientation   (ButtonWidget     *button,
						  PanelOrientation  orientation);
PanelOrientation button_widget_get_orientation   (ButtonWidget     *button);
void             button_widget_set_has_arrow     (ButtonWidget     *button,
						  gboolean          has_arrow);
gboolean         button_widget_get_has_arrow     (ButtonWidget     *button);
void             button_widget_set_dnd_highlight (ButtonWidget     *button,
						  gboolean          dnd_highlight);
gboolean         button_widget_get_dnd_highlight (ButtonWidget     *button);
void             button_widget_set_ignore_leave  (ButtonWidget     *button,
						  gboolean          ignore_leave);
gboolean         button_widget_get_ignore_leave  (ButtonWidget     *button);
GtkIconTheme    *button_widget_get_icon_theme    (ButtonWidget     *button);
cairo_surface_t *button_widget_get_surface       (ButtonWidget     *button);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_WIDGET_H__ */
