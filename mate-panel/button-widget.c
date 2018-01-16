#include <config.h>
#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

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

#define BUTTON_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), BUTTON_TYPE_WIDGET, ButtonWidgetPrivate))

struct _ButtonWidgetPrivate {
	GtkIconTheme     *icon_theme;
	GdkPixbuf        *pixbuf;
	GdkPixbuf        *pixbuf_hc;

	char             *filename;

	PanelOrientation  orientation;

	int               size;

	guint             activatable   : 1;
	guint             ignore_leave  : 1;
	guint             arrow         : 1;
	guint             dnd_highlight : 1;
};

static void button_widget_icon_theme_changed (ButtonWidget *button);
static void button_widget_reload_pixbuf (ButtonWidget *button);

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

G_DEFINE_TYPE (ButtonWidget, button_widget, GTK_TYPE_BUTTON)

/* colorshift a pixbuf */
static void
do_colorshift (GdkPixbuf *dest, GdkPixbuf *src, int shift)
{
	gint i, j;
	gint width, height, has_alpha, srcrowstride, destrowstride;
	guchar *target_pixels;
	guchar *original_pixels;
	guchar *pixsrc;
	guchar *pixdest;
	int val;
	guchar r,g,b;

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	srcrowstride = gdk_pixbuf_get_rowstride (src);
	destrowstride = gdk_pixbuf_get_rowstride (dest);
	target_pixels = gdk_pixbuf_get_pixels (dest);
	original_pixels = gdk_pixbuf_get_pixels (src);

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

static GdkPixbuf *
make_hc_pixbuf (GdkPixbuf *pb)
{
	GdkPixbuf *new;
	
	if (!pb)
		return NULL;

	new = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (pb),
			      gdk_pixbuf_get_has_alpha (pb),
			      gdk_pixbuf_get_bits_per_sample (pb),
			      gdk_pixbuf_get_width (pb),
			      gdk_pixbuf_get_height (pb));
	do_colorshift (new, pb, 30);

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

	button_widget_reload_pixbuf (BUTTON_WIDGET (widget));
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
button_widget_unset_pixbufs (ButtonWidget *button)
{
	if (button->priv->pixbuf)
		g_object_unref (button->priv->pixbuf);
	button->priv->pixbuf = NULL;

	if (button->priv->pixbuf_hc)
		g_object_unref (button->priv->pixbuf_hc);
	button->priv->pixbuf_hc = NULL;
}

static void
button_widget_reload_pixbuf (ButtonWidget *button)
{
	button_widget_unset_pixbufs (button);

	if (button->priv->size <= 1 || button->priv->icon_theme == NULL)
		return;

	if (button->priv->filename != NULL &&
	    button->priv->filename [0] != '\0') {
		char *error = NULL;

		button->priv->pixbuf =
			panel_load_icon (button->priv->icon_theme,
					 button->priv->filename,
					 button->priv->size,
					 button->priv->orientation & PANEL_VERTICAL_MASK   ? button->priv->size : -1,
					 button->priv->orientation & PANEL_HORIZONTAL_MASK ? button->priv->size : -1,
					 &error);
		if (error) {
			//FIXME: this is not rendered at button->priv->size
			GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
			button->priv->pixbuf = gtk_icon_theme_load_icon (icon_theme,
							       "image-missing",
							       GTK_ICON_SIZE_BUTTON,
							       GTK_ICON_LOOKUP_FORCE_SVG | GTK_ICON_LOOKUP_USE_BUILTIN,
							       NULL);
			g_free (error);
		}
	}

	button->priv->pixbuf_hc = make_hc_pixbuf (button->priv->pixbuf);

	gtk_widget_queue_resize (GTK_WIDGET (button));
}

static void
button_widget_icon_theme_changed (ButtonWidget *button)
{
	if (button->priv->filename != NULL)
		button_widget_reload_pixbuf (button);
}

static void
button_widget_finalize (GObject *object)
{
	ButtonWidget *button = (ButtonWidget *) object;

	button_widget_unset_pixbufs (button);

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
	double scale;

	scale = (orientation & PANEL_HORIZONTAL_MASK ? button_height : button_width) / 48.0;

	*size = 12 * scale;
	*angle = 0;

	switch (orientation) {
	case PANEL_ORIENTATION_TOP:
		*x     = scale * 3;
		*y     = scale * (48 - 13);
		*angle = G_PI;
		retval = GTK_ARROW_DOWN;
		break;
	case PANEL_ORIENTATION_BOTTOM:
		*x     = scale * (48 - 13);
		*y     = scale * 1;
		*angle = 0;
		retval = GTK_ARROW_UP;
		break;
	case PANEL_ORIENTATION_LEFT:
		*x     = scale * (48 - 13);
		*y     = scale * 3;
		*angle = G_PI / 2;
		retval = GTK_ARROW_RIGHT;
		break;
	case PANEL_ORIENTATION_RIGHT:
		*x     = scale * 1;
		*y     = scale * 3;
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
	GdkPixbuf *pb = NULL;
  
	g_return_val_if_fail (BUTTON_IS_WIDGET (widget), FALSE);

	button_widget = BUTTON_WIDGET (widget);

	if (!button_widget->priv->pixbuf_hc && !button_widget->priv->pixbuf)
		return FALSE;

	state_flags = gtk_widget_get_state_flags (widget);
	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);

	/* offset for pressed buttons */
	off = (button_widget->priv->activatable &&
		(state_flags & GTK_STATE_FLAG_PRELIGHT) && (state_flags & GTK_STATE_FLAG_ACTIVE)) ?
		BUTTON_WIDGET_DISPLACEMENT * height / 48.0 : 0;

	if (!button_widget->priv->activatable) {
		pb = gdk_pixbuf_copy (button_widget->priv->pixbuf);
		gdk_pixbuf_saturate_and_pixelate (button_widget->priv->pixbuf,
						  pb,
						  0.8,
						  TRUE);
	} else if (panel_global_config_get_highlight_when_over () && 
		   (state_flags & GTK_STATE_FLAG_PRELIGHT || gtk_widget_has_focus (widget)))
		pb = g_object_ref (button_widget->priv->pixbuf_hc);
	else
		pb = g_object_ref (button_widget->priv->pixbuf);

	g_assert (pb != NULL);

	w = gdk_pixbuf_get_width (pb);
	h = gdk_pixbuf_get_height (pb);
	x = off + (width - w)/2;
	y = off + (height - h)/2;

	cairo_save (cr);
	gdk_cairo_set_source_pixbuf (cr, pb, x, y);
	cairo_paint (cr);
	cairo_restore (cr);

	g_object_unref (pb);

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
#if !GTK_CHECK_VERSION (3, 20, 0)
		gint focus_pad;
#endif

		gtk_style_context_save (context);
		gtk_style_context_set_state (context, state_flags);

#if !GTK_CHECK_VERSION (3, 20, 0)
		gtk_widget_style_get (widget,
				      "focus-padding", &focus_pad,
				      NULL);
		x = focus_pad;
		y = focus_pad;
		w = width - 2 * focus_pad;
		h = height - 2 * focus_pad;
#endif

		cairo_save (cr);
#if GTK_CHECK_VERSION (3, 20, 0)
		gtk_render_focus (context, cr, 0, 0, width, height);
#else
		gtk_render_focus (context, cr, x, y, w, h);
#endif
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

	if (button_widget->priv->orientation & PANEL_HORIZONTAL_MASK)
		size = gtk_widget_get_allocated_height (parent);
	else
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
	else
		size = gtk_widget_get_allocated_width (parent);

	*minimal_height = *natural_height = size;
}

static void
button_widget_size_allocate (GtkWidget     *widget,
			     GtkAllocation *allocation)
{
	ButtonWidget *button_widget = BUTTON_WIDGET (widget);
	int           size;

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

		button_widget_reload_pixbuf (button_widget);
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
	button->priv = BUTTON_WIDGET_GET_PRIVATE (button);

	button->priv->icon_theme = NULL;
	button->priv->pixbuf     = NULL;
	button->priv->pixbuf_hc  = NULL;

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

	g_type_class_add_private (klass, sizeof (ButtonWidgetPrivate));
	  
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

	button_widget_reload_pixbuf (button);

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

GdkPixbuf *
button_widget_get_pixbuf (ButtonWidget *button)
{
	g_return_val_if_fail (BUTTON_IS_WIDGET (button), NULL);

	if (!button->priv->pixbuf)
		return NULL;

	return g_object_ref (button->priv->pixbuf);
}
