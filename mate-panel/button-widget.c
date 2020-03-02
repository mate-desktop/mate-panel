#include <config.h>
#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "button-widget.h"
#include "panel-widget.h"
#include "panel-types.h"
#include "panel-util.h"
#include "panel-config-global.h"
#include "panel-marshal.h"
#include "panel-typebuiltins.h"
#include "panel-globals.h"
#include "panel-enums.h"
#include "panel-enums-gsettings.h"

struct _ButtonWidgetPrivate {
	GtkIconTheme     *icon_theme;
	cairo_surface_t  *surface;
	cairo_surface_t  *surface_hc;

	char             *filename;

	PanelOrientation  orientation;

	int               size;

	guint             activatable   : 1;
	guint             ignore_leave  : 1;
	guint             arrow         : 1;
	guint             dnd_highlight : 1;
};

static void button_widget_icon_theme_changed (ButtonWidget *button);
static void button_widget_reload_surface (ButtonWidget *button);

enum {
	PROP_0,
	PROP_ACTIVATABLE,
	PROP_IGNORE_LEAVE,
	PROP_HAS_ARROW,
	PROP_DND_HIGHLIGHT,
	PROP_ORIENTATION,
	PROP_ICON_NAME
};

#define BUTTON_WIDGET_DISPLACEMENT 2

G_DEFINE_TYPE_WITH_PRIVATE (ButtonWidget, button_widget, GTK_TYPE_BUTTON)

/* colorshift a surface */
static void
do_colorshift (cairo_surface_t *dest, cairo_surface_t *src, int shift)
{
	gint i, j;
	gint width, height, has_alpha, srcrowstride, destrowstride;
	guchar *target_pixels;
	guchar *original_pixels;
	guchar *pixsrc;
	guchar *pixdest;
	int val;
	guchar r,g,b;

	has_alpha = cairo_surface_get_content (src) != CAIRO_CONTENT_COLOR;
	width = cairo_image_surface_get_width (src);
	height = cairo_image_surface_get_height (src);
	srcrowstride = cairo_image_surface_get_stride (src);
	destrowstride = cairo_image_surface_get_stride (dest);
	original_pixels = cairo_image_surface_get_data (src);
	target_pixels = cairo_image_surface_get_data (dest);

	for (i = 0; i < height; i++) {
		pixdest = target_pixels + i*destrowstride;
		pixsrc = original_pixels + i*srcrowstride;
		for (j = 0; j < width; j++) {
			r = *(pixsrc++);
			g = *(pixsrc++);
			b = *(pixsrc++);
			val = r + shift;
			*(pixdest++) = CLAMP(val, 0, 255);
			val = g + shift;
			*(pixdest++) = CLAMP(val, 0, 255);
			val = b + shift;
			*(pixdest++) = CLAMP(val, 0, 255);
			if (has_alpha)
				*(pixdest++) = *(pixsrc++);
		}
	}
}

static cairo_surface_t *
make_hc_surface (cairo_surface_t *surface)
{
	cairo_t *cr;
	cairo_surface_t *new;

	if (!surface)
		return NULL;

	new = cairo_surface_create_similar (surface,
			                    cairo_surface_get_content (surface),
			                    cairo_image_surface_get_width (surface),
			                    cairo_image_surface_get_height (surface));

	do_colorshift (new, surface, 30);

	cr = cairo_create (new);
	cairo_set_operator (cr, CAIRO_OPERATOR_DEST_IN);
	cairo_mask_surface (cr, surface, 0, 0);
	cairo_destroy (cr);

	return new;
}

static void
button_widget_realize(GtkWidget *widget)
{
	gtk_widget_add_events (widget, GDK_POINTER_MOTION_MASK |
			       GDK_POINTER_MOTION_HINT_MASK |
			       GDK_KEY_PRESS_MASK);

	GTK_WIDGET_CLASS (button_widget_parent_class)->realize (widget);

	BUTTON_WIDGET (widget)->priv->icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
	g_signal_connect_object (BUTTON_WIDGET (widget)->priv->icon_theme,
				 "changed",
				 G_CALLBACK (button_widget_icon_theme_changed),
				 widget,
				 G_CONNECT_SWAPPED);

	button_widget_reload_surface (BUTTON_WIDGET (widget));
}

static void
button_widget_unrealize (GtkWidget *widget)
{
	g_signal_handlers_disconnect_by_func (BUTTON_WIDGET (widget)->priv->icon_theme,
					      G_CALLBACK (button_widget_icon_theme_changed),
					      widget);

	GTK_WIDGET_CLASS (button_widget_parent_class)->unrealize (widget);
}

static void
button_widget_unset_surfaces (ButtonWidget *button)
{
	if (button->priv->surface)
		cairo_surface_destroy (button->priv->surface);
	button->priv->surface = NULL;

	if (button->priv->surface_hc)
		cairo_surface_destroy (button->priv->surface_hc);
	button->priv->surface_hc = NULL;
}

static void
button_widget_reload_surface (ButtonWidget *button)
{
	button_widget_unset_surfaces (button);

	if (button->priv->size <= 1 || button->priv->icon_theme == NULL)
		return;

	if (button->priv->filename != NULL &&
	    button->priv->filename [0] != '\0') {
		gint scale;
		char *error = NULL;

		scale = gtk_widget_get_scale_factor (GTK_WIDGET (button));

		button->priv->surface =
			panel_load_icon (button->priv->icon_theme,
					 button->priv->filename,
					 button->priv->size * scale,
					 (button->priv->orientation & PANEL_VERTICAL_MASK)   ? button->priv->size * scale : -1,
					 (button->priv->orientation & PANEL_HORIZONTAL_MASK) ? button->priv->size * scale: -1,
					 &error);
		if (error) {
			//FIXME: this is not rendered at button->priv->size
			GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
			button->priv->surface = gtk_icon_theme_load_surface (icon_theme,
							       "image-missing",
							       GTK_ICON_SIZE_BUTTON,
							       scale,
							       NULL,
							       GTK_ICON_LOOKUP_FORCE_SVG | GTK_ICON_LOOKUP_USE_BUILTIN,
							       NULL);
			g_free (error);
		}
	}

	button->priv->surface_hc = make_hc_surface (button->priv->surface);

	gtk_widget_queue_resize (GTK_WIDGET (button));
}

static void
button_widget_icon_theme_changed (ButtonWidget *button)
{
	if (button->priv->filename != NULL)
		button_widget_reload_surface (button);
}

static void
button_widget_finalize (GObject *object)
{
	ButtonWidget *button = (ButtonWidget *) object;

	button_widget_unset_surfaces (button);

	g_free (button->priv->filename);
	button->priv->filename = NULL;

	G_OBJECT_CLASS (button_widget_parent_class)->finalize (object);
}

static void
button_widget_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
	ButtonWidget *button;

	g_return_if_fail (BUTTON_IS_WIDGET (object));

	button = BUTTON_WIDGET (object);

	switch (prop_id) {
	case PROP_ACTIVATABLE:
		g_value_set_boolean (value, button->priv->activatable);
		break;
	case PROP_IGNORE_LEAVE:
		g_value_set_boolean (value, button->priv->ignore_leave);
		break;
	case PROP_HAS_ARROW:
		g_value_set_boolean (value, button->priv->arrow);
		break;
	case PROP_DND_HIGHLIGHT:
		g_value_set_boolean (value, button->priv->dnd_highlight);
		break;
	case PROP_ORIENTATION:
		g_value_set_enum (value, button->priv->orientation);
		break;
	case PROP_ICON_NAME:
		g_value_set_string (value, button->priv->filename);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
button_widget_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	ButtonWidget *button;

	g_return_if_fail (BUTTON_IS_WIDGET (object));

	button = BUTTON_WIDGET (object);

	switch (prop_id) {
	case PROP_ACTIVATABLE:
		button_widget_set_activatable (button, g_value_get_boolean (value));
		break;
	case PROP_IGNORE_LEAVE:
		button_widget_set_ignore_leave (button, g_value_get_boolean (value));
		break;
	case PROP_HAS_ARROW:
		button_widget_set_has_arrow (button, g_value_get_boolean (value));
		break;
	case PROP_DND_HIGHLIGHT:
		button_widget_set_dnd_highlight (button, g_value_get_boolean (value));
		break;
	case PROP_ORIENTATION:
		button_widget_set_orientation (button, g_value_get_enum (value));
		break;
	case PROP_ICON_NAME:
		button_widget_set_icon_name (button, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GtkArrowType
calc_arrow (PanelOrientation  orientation,
	    int               button_width,
	    int               button_height,
	    int              *x,
	    int              *y,
	    gdouble          *angle,
	    gdouble          *size)
{
	GtkArrowType retval = GTK_ARROW_UP;

	if (orientation & PANEL_HORIZONTAL_MASK) {
		if (button_width > 50)
			button_width = 50;
	} else {
		if (button_height > 50)
			button_height = 50;
	}

	*size = ((orientation & PANEL_HORIZONTAL_MASK) ? button_width : button_height) / 2;
	*angle = 0;

	switch (orientation) {
	case PANEL_ORIENTATION_TOP:
		*x     = (button_width - (*size)) / 2;
		*y     = button_height * .99 - (*size) / (3/2);	// 3/2 is the approximate ratio of GTK arrows
		*angle = G_PI;
		retval = GTK_ARROW_DOWN;
		break;
	case PANEL_ORIENTATION_BOTTOM:
		*x     = (button_width - (*size)) / 2;
		*y     = button_height * .01;
		*angle = 0;
		retval = GTK_ARROW_UP;
		break;
	case PANEL_ORIENTATION_LEFT:
		*x     = button_width * .99 - (*size) / (3/2);	// 3/2 is the approximate ratio of GTK arrows
		*y     = (button_height - (*size)) / 2;
		*angle = G_PI / 2;
		retval = GTK_ARROW_RIGHT;
		break;
	case PANEL_ORIENTATION_RIGHT:
		*x     = button_width * .01;
		*y     = (button_height - (*size)) / 2;
		*angle = 3 * (G_PI / 2);
		retval = GTK_ARROW_LEFT;
		break;
	}

	return retval;
}

static gboolean
button_widget_draw (GtkWidget *widget,
		    cairo_t *cr)
{
	ButtonWidget *button_widget;
	int width;
	int height;
	GtkStyleContext *context;
	GtkStateFlags state_flags;
	int off;
	int x, y, w, h;
	int scale;

	g_return_val_if_fail (BUTTON_IS_WIDGET (widget), FALSE);

	button_widget = BUTTON_WIDGET (widget);

	if (!button_widget->priv->surface_hc && !button_widget->priv->surface)
		return FALSE;

	state_flags = gtk_widget_get_state_flags (widget);
	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);
	scale = gtk_widget_get_scale_factor (widget);

	/* offset for pressed buttons */
	off = (button_widget->priv->activatable &&
		(state_flags & GTK_STATE_FLAG_PRELIGHT) && (state_flags & GTK_STATE_FLAG_ACTIVE)) ?
		BUTTON_WIDGET_DISPLACEMENT * height / 48.0 : 0;

	w = cairo_image_surface_get_width (button_widget->priv->surface) / scale;
	h = cairo_image_surface_get_height (button_widget->priv->surface) / scale;
	x = off + (width - w) / 2;
	y = off + (height - h) / 2;

	cairo_save (cr);

	if (!button_widget->priv->activatable) {
		cairo_set_source_surface (cr, button_widget->priv->surface, x, y);
		cairo_mask_surface (cr, button_widget->priv->surface, x, y);
		cairo_set_operator (cr, CAIRO_OPERATOR_HSL_SATURATION);
		cairo_set_source_rgba (cr, 0, 0, 0, 0.2);
	} else if (panel_global_config_get_highlight_when_over () &&
		   (state_flags & GTK_STATE_FLAG_PRELIGHT || gtk_widget_has_focus (widget))) {
		cairo_set_source_surface (cr, button_widget->priv->surface_hc, x, y);
	} else {
		cairo_set_source_surface (cr, button_widget->priv->surface, x, y);
	}

	cairo_paint (cr);
	cairo_restore (cr);

	context = gtk_widget_get_style_context (widget);

	if (button_widget->priv->arrow) {
		gdouble angle, size;
		gtk_style_context_save (context);
		gtk_style_context_set_state (context, state_flags);

		calc_arrow (button_widget->priv->orientation,
					 width, height,
					 &x, &y,
					 &angle, &size);

		cairo_save (cr);
		gtk_render_arrow (context, cr, angle, x, y, size);
		cairo_restore (cr);

		gtk_style_context_restore (context);
	}

	if (button_widget->priv->dnd_highlight) {

		cairo_save (cr);
		cairo_set_line_width (cr, 1);
		cairo_set_source_rgb (cr, 0., 0., 0.);
		cairo_rectangle (cr, 0.5, 0.5, width - 1, height - 1);
		cairo_stroke (cr);
		cairo_restore (cr);
	}

	if (gtk_widget_has_focus (widget)) {
		gtk_style_context_save (context);
		gtk_style_context_set_state (context, state_flags);

		cairo_save (cr);
		gtk_render_focus (context, cr, 0, 0, width, height);
		cairo_restore (cr);

		gtk_style_context_restore (context);
	}

	return FALSE;
}

static void
button_widget_get_preferred_width (GtkWidget *widget,
				   gint *minimal_width,
				   gint *natural_width)
{
 	ButtonWidget *button_widget = BUTTON_WIDGET (widget);
	GtkWidget *parent;
	int size;

	parent = gtk_widget_get_parent (widget);

	if (button_widget->priv->orientation & PANEL_HORIZONTAL_MASK){
		size = gtk_widget_get_allocated_height (parent);

		/* should get this value (50) from gsettings, user defined value in properties of the panel (max_icon_size) OR use 48*/
		if ( size > 50 )
			size = 50;

	} else
		size = gtk_widget_get_allocated_width (parent);

	*minimal_width = *natural_width = size;
}

static void
button_widget_get_preferred_height (GtkWidget *widget,
				    gint *minimal_height,
				    gint *natural_height)
{
	ButtonWidget *button_widget = BUTTON_WIDGET (widget);
	GtkWidget *parent;
	int size;

	parent = gtk_widget_get_parent (widget);

	if (button_widget->priv->orientation & PANEL_HORIZONTAL_MASK)
		size = gtk_widget_get_allocated_height (parent);
	else {
		size = gtk_widget_get_allocated_width (parent);

		/* should get this value (50) from gsettings, user defined value in properties of the panel (max_icon_size) OR use 48*/
		if ( size > 50 )
			size = 50;

	}

	*minimal_height = *natural_height = size;
}

static void
button_widget_size_allocate (GtkWidget     *widget,
			     GtkAllocation *allocation)
{
	ButtonWidget *button_widget = BUTTON_WIDGET (widget);
	int           size;

	/* should get this value (50) from gsettings, user defined value in properties of the panel (max_icon_size) OR use 48?*/
	if (button_widget->priv->orientation & PANEL_HORIZONTAL_MASK) {
		if ( allocation->height > 50 ) {
			allocation->width = 50;
		}
	} else {
		if ( allocation->width > 50 ) {
			allocation->height = 50;
		}
	}

	GTK_WIDGET_CLASS (button_widget_parent_class)->size_allocate (widget, allocation);

	if (button_widget->priv->orientation & PANEL_HORIZONTAL_MASK)
		size = allocation->height;
	else
		size = allocation->width;

	if (size < 22)
		size = 16;
	else if (size < 24)
		size = 22;
	else if (size < 32)
		size = 24;
	else if (size < 48)
		size = 32;
	else
		size = 48;

	if (button_widget->priv->size != size) {
		button_widget->priv->size = size;

		button_widget_reload_surface (button_widget);
	}
}

static void
button_widget_activate (GtkButton *button)
{
	ButtonWidget *button_widget = BUTTON_WIDGET (button);

	if (!button_widget->priv->activatable)
		return;

	if (GTK_BUTTON_CLASS (button_widget_parent_class)->activate)
		GTK_BUTTON_CLASS (button_widget_parent_class)->activate (button);
}

static gboolean
button_widget_button_press (GtkWidget *widget, GdkEventButton *event)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	if (event->button == 1 && BUTTON_WIDGET (widget)->priv->activatable &&
	/* we don't want to have two/three "click" events for double/triple
	 * clicks. FIXME: this is only a workaround, waiting for bug 159101 */
	    event->type == GDK_BUTTON_PRESS)
		return GTK_WIDGET_CLASS (button_widget_parent_class)->button_press_event (widget, event);

	return FALSE;
}

static gboolean
button_widget_enter_notify (GtkWidget *widget, GdkEventCrossing *event)
{
	gboolean in_button;

	g_return_val_if_fail (BUTTON_IS_WIDGET (widget), FALSE);

	GtkStateFlags state_flags = gtk_widget_get_state_flags (widget);
	in_button = state_flags & GTK_STATE_FLAG_PRELIGHT;

	GTK_WIDGET_CLASS (button_widget_parent_class)->enter_notify_event (widget, event);

	state_flags = gtk_widget_get_state_flags (widget);
	if (in_button != (state_flags & GTK_STATE_FLAG_PRELIGHT) &&
	    panel_global_config_get_highlight_when_over ())
		gtk_widget_queue_draw (widget);

	return FALSE;
}

static gboolean
button_widget_leave_notify (GtkWidget *widget, GdkEventCrossing *event)
{
	gboolean in_button;

	g_return_val_if_fail (BUTTON_IS_WIDGET (widget), FALSE);

	GtkStateFlags state_flags = gtk_widget_get_state_flags (widget);
	in_button = state_flags & GTK_STATE_FLAG_PRELIGHT;

	GTK_WIDGET_CLASS (button_widget_parent_class)->leave_notify_event (widget, event);

	state_flags = gtk_widget_get_state_flags (widget);
	if (in_button != (state_flags & GTK_STATE_FLAG_PRELIGHT) &&
	    panel_global_config_get_highlight_when_over ())
		gtk_widget_queue_draw (widget);

	return FALSE;
}

static void
button_widget_init (ButtonWidget *button)
{
	button->priv = button_widget_get_instance_private (button);

	button->priv->icon_theme = NULL;
	button->priv->surface    = NULL;
	button->priv->surface_hc = NULL;

	button->priv->filename   = NULL;

	button->priv->orientation = PANEL_ORIENTATION_TOP;

	button->priv->size = 0;

	button->priv->activatable   = FALSE;
	button->priv->ignore_leave  = FALSE;
	button->priv->arrow         = FALSE;
	button->priv->dnd_highlight = FALSE;
}

static void
button_widget_class_init (ButtonWidgetClass *klass)
{
	GObjectClass *gobject_class   = (GObjectClass   *) klass;
	GtkWidgetClass *widget_class  = (GtkWidgetClass *) klass;
	GtkButtonClass *button_class  = (GtkButtonClass *) klass;

	gobject_class->finalize     = button_widget_finalize;
	gobject_class->get_property = button_widget_get_property;
	gobject_class->set_property = button_widget_set_property;

	widget_class->realize            = button_widget_realize;
	widget_class->unrealize          = button_widget_unrealize;
	widget_class->size_allocate      = button_widget_size_allocate;
	widget_class->get_preferred_width = button_widget_get_preferred_width;
	widget_class->get_preferred_height = button_widget_get_preferred_height;
	widget_class->draw               = button_widget_draw;
	widget_class->button_press_event = button_widget_button_press;
	widget_class->enter_notify_event = button_widget_enter_notify;
	widget_class->leave_notify_event = button_widget_leave_notify;

	button_class->activate = button_widget_activate;

	g_object_class_install_property (
			gobject_class,
			PROP_ACTIVATABLE,
			g_param_spec_boolean ("activatable",
					      "Activatable",
					      "Whether the button is activatable",
					      TRUE,
					      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (
			gobject_class,
			PROP_IGNORE_LEAVE,
			g_param_spec_boolean ("ignore-leave",
					      "Ignore leaving to not unhighlight the icon",
					      "Whether or not to unhighlight the icon when the cursor leaves it",
					      FALSE,
					      G_PARAM_READWRITE));

	g_object_class_install_property (
			gobject_class,
			PROP_HAS_ARROW,
			g_param_spec_boolean ("has-arrow",
					      "Has Arrow",
					      "Whether or not to draw an arrow indicator",
					      FALSE,
					      G_PARAM_READWRITE));

	g_object_class_install_property (
			gobject_class,
			PROP_DND_HIGHLIGHT,
			g_param_spec_boolean ("dnd-highlight",
					      "Drag and drop Highlight",
					      "Whether or not to highlight the icon during drag and drop",
					      FALSE,
					      G_PARAM_READWRITE));

	g_object_class_install_property (
			gobject_class,
			PROP_ORIENTATION,
			g_param_spec_enum ("orientation",
					   "Orientation",
					   "The ButtonWidget orientation",
					   PANEL_TYPE_ORIENTATION,
					   PANEL_ORIENTATION_TOP,
					   G_PARAM_READWRITE));

	g_object_class_install_property (
			gobject_class,
			PROP_ICON_NAME,
			g_param_spec_string ("icon-name",
					     "Icon Name",
					     "The desired icon for the ButtonWidget",
					     NULL,
					     G_PARAM_READWRITE));
}

GtkWidget *
button_widget_new (const char       *filename,
		   gboolean          arrow,
		   PanelOrientation  orientation)
{
	GtkWidget *retval;

	retval = g_object_new (
			BUTTON_TYPE_WIDGET,
			"has-arrow", arrow,
			"orientation", orientation,
			"icon-name", filename,
			NULL);

	return retval;
}

void
button_widget_set_activatable (ButtonWidget *button,
			       gboolean      activatable)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	activatable = activatable != FALSE;

	if (button->priv->activatable != activatable) {
		button->priv->activatable = activatable;

		if (gtk_widget_is_drawable (GTK_WIDGET (button)))
			gtk_widget_queue_draw (GTK_WIDGET (button));

		g_object_notify (G_OBJECT (button), "activatable");
	}
}

gboolean
button_widget_get_activatable (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), FALSE);

	return button->priv->activatable;
}

void
button_widget_set_icon_name (ButtonWidget *button,
			     const char   *icon_name)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	if (button->priv->filename && icon_name &&
	    !strcmp (button->priv->filename, icon_name))
		return;

	if (button->priv->filename)
		g_free (button->priv->filename);
	button->priv->filename = g_strdup (icon_name);

	button_widget_reload_surface (button);

	g_object_notify (G_OBJECT (button), "icon-name");
}

const char *
button_widget_get_icon_name (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), NULL);

	return button->priv->filename;
}

void
button_widget_set_orientation (ButtonWidget     *button,
			       PanelOrientation  orientation)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	if (button->priv->orientation == orientation)
		return;

	button->priv->orientation = orientation;

	/* Force a re-scale */
	button->priv->size = -1;

	gtk_widget_queue_resize (GTK_WIDGET (button));

	g_object_notify (G_OBJECT (button), "orientation");
}

PanelOrientation
button_widget_get_orientation (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), 0);

	return button->priv->orientation;
}

void
button_widget_set_has_arrow (ButtonWidget *button,
			     gboolean      has_arrow)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	has_arrow = has_arrow != FALSE;

	if (button->priv->arrow == has_arrow)
		return;

	button->priv->arrow = has_arrow;

	gtk_widget_queue_draw (GTK_WIDGET (button));

	g_object_notify (G_OBJECT (button), "has-arrow");
}

gboolean
button_widget_get_has_arrow (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), FALSE);

	return button->priv->arrow;
}

void
button_widget_set_dnd_highlight (ButtonWidget *button,
				 gboolean      dnd_highlight)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	dnd_highlight = dnd_highlight != FALSE;

	if (button->priv->dnd_highlight == dnd_highlight)
		return;

	button->priv->dnd_highlight = dnd_highlight;

	gtk_widget_queue_draw (GTK_WIDGET (button));

	g_object_notify (G_OBJECT (button), "dnd-highlight");
}

gboolean
button_widget_get_dnd_highlight (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), FALSE);

	return button->priv->dnd_highlight;
}

void
button_widget_set_ignore_leave (ButtonWidget *button,
				gboolean      ignore_leave)
{
	g_return_if_fail (BUTTON_IS_WIDGET (button));

	ignore_leave = ignore_leave != FALSE;

	if (button->priv->ignore_leave == ignore_leave)
		return;

	button->priv->ignore_leave = ignore_leave;

	gtk_widget_queue_draw (GTK_WIDGET (button));

	g_object_notify (G_OBJECT (button), "ignore-leave");
}

gboolean
button_widget_get_ignore_leave (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), FALSE);

	return button->priv->ignore_leave;
}

GtkIconTheme *
button_widget_get_icon_theme (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), NULL);

	return button->priv->icon_theme;
}

cairo_surface_t *
button_widget_get_surface (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), NULL);

	if (!button->priv->surface)
		return NULL;

	return cairo_surface_reference (button->priv->surface);
}
