#include <config.h>
#include <math.h>

#include "wayland-backend.h"

#include "panel-toplevel.h"

#include "wayland-protocols/wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wayland-protocols/xdg-shell-client-protocol.h"

struct zwlr_layer_shell_v1 *layer_shell_global = NULL;
struct xdg_wm_base *xdg_wm_base_global = NULL;
static gboolean wayland_has_initialized = FALSE;
static const char *wayland_popup_data_key = "wayland_popup_data";
static const char *wayland_popup_attach_widget_key = "wayland_popup_attach_widget";
static const char *wayland_layer_surface_key = "wayland_layer_surface";
static const char *menu_setup_func_key = "popup_menu_setup_func";
static const char *tooltip_setup_func_key = "tooltip_setup_func";
static const char *custom_tooltip_widget_key = "custom_tooltip_widget_data";

gboolean
is_using_wayland ()
{
	return GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ());
}

static void
widget_get_pointer_position (GtkWidget *widget, gint *pointer_x, gint *pointer_y)
{
	GdkWindow *window;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkDevice *pointer;

	window = gdk_window_get_toplevel (gtk_widget_get_window (widget));
	display = gdk_window_get_display (window);
	seat = gdk_display_get_default_seat (display);
	pointer = gdk_seat_get_pointer (seat);
	gdk_window_get_device_position (window, pointer, pointer_x, pointer_y, NULL);
}

static void
wl_regitsty_handle_global (void *data,
			   struct wl_registry *registry,
			   uint32_t id,
			   const char *interface,
			   uint32_t version)
{
	// pull out needed globals
	if (strcmp (interface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell_global = wl_registry_bind (registry, id, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp (interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base_global = wl_registry_bind (registry, id, &xdg_wm_base_interface, 1);
	}
}

static void
wl_regitsty_handle_global_remove (void *data,
				  struct wl_registry *registry,
				  uint32_t id)
{
	// who cares
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = wl_regitsty_handle_global,
	.global_remove = wl_regitsty_handle_global_remove,
};

static void
layer_surface_handle_configure (void *data,
				struct zwlr_layer_surface_v1 *surface,
				uint32_t serial,
				uint32_t w,
				uint32_t h)
{
	//width = w;
	//height = h;
	// TODO: resize the GTK window
	//gtk_window_set_default_size (GTK_WINDOW (window), width, height);
	zwlr_layer_surface_v1_ack_configure (surface, serial);
}

static void
layer_surface_handle_closed (void *data,
			    struct zwlr_layer_surface_v1 *surface)
{
	// TODO: close the GTK window and destroy the layer shell surface object
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_handle_configure,
	.closed = layer_surface_handle_closed,
};

static void
xdg_surface_handle_configure (void *data,
			      struct xdg_surface *xdg_surface,
			      uint32_t serial)
{
	xdg_surface_ack_configure (xdg_surface, serial);
}

struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

void
wayland_registry_init ()
{
	GdkDisplay *gdk_display = gdk_display_get_default ();
	g_assert (GDK_IS_WAYLAND_DISPLAY (gdk_display));
	struct wl_display *wl_display = gdk_wayland_display_get_wl_display (gdk_display);
	struct wl_registry *wl_registry = wl_display_get_registry (wl_display);
	wl_registry_add_listener (wl_registry, &wl_registry_listener, NULL);
	wl_display_roundtrip (wl_display);
	if (!layer_shell_global)
		g_warning ("Layer shell global not bound");
	wayland_has_initialized = TRUE;
}

struct _WaylandLayerSurfaceData {
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_surface *wl_surface;
	gint width, height;

	struct xdg_surface *fallback_xdg_surface;
	struct xdg_toplevel *fallback_xdg_toplevel;

	gboolean strut_data_set; // if orientation and exclusive_zone have been set
	PanelOrientation orientation;
	int exclusive_zone;
};

static void
wayland_destroy_layer_surface_data_cb (struct _WaylandLayerSurfaceData *data) {
	if (data->layer_surface)
		zwlr_layer_surface_v1_destroy (data->layer_surface);
	if (data->fallback_xdg_toplevel)
		xdg_toplevel_destroy (data->fallback_xdg_toplevel);
	if (data->fallback_xdg_surface)
		xdg_surface_destroy (data->fallback_xdg_surface);
	free (data);
}

static void
wayland_layer_surface_size_allocate_cb (GtkWidget *widget,
					GdkRectangle *allocation,
					struct _WaylandLayerSurfaceData *data)
{
	if (data->width  != allocation->width ||
	    data->height != allocation->height) {
		data->width  = allocation->width;
		data->height = allocation->height;
		if (data->layer_surface)
			zwlr_layer_surface_v1_set_size (data->layer_surface, data->width, data->height);
		if (data->fallback_xdg_surface)
			xdg_surface_set_window_geometry (data->fallback_xdg_surface, 0, 0, data->width, data->height);
	}
}

void
wayland_realize_panel_toplevel (GtkWidget *widget)
{
	GdkDisplay *gdk_display;
	GdkWindow *window;
	struct _WaylandLayerSurfaceData *data;
	struct wl_display *wl_display;

	g_assert (wayland_has_initialized);

	gdk_display = gdk_window_get_display (gtk_widget_get_window (widget));

	g_assert (GDK_IS_WAYLAND_DISPLAY (gdk_display));
	g_assert (wayland_has_initialized);

	window = gtk_widget_get_window (widget);
	// This will allow anyone who can get hold of the window to make a popup
	g_object_set_data (G_OBJECT (window),
			   menu_setup_func_key,
			   wayland_popup_menu_setup);
	g_object_set_data (G_OBJECT (window),
			   tooltip_setup_func_key,
			   wayland_tooltip_setup);
	gdk_wayland_window_set_use_custom_surface (window);

	data = g_new0 (struct _WaylandLayerSurfaceData, 1);
	data->width = gtk_widget_get_allocated_width (widget);
	data->height = gtk_widget_get_allocated_height (widget);
	data->strut_data_set = FALSE;
	g_object_set_data_full (G_OBJECT (window),
				wayland_layer_surface_key,
				data,
				(GDestroyNotify) wayland_destroy_layer_surface_data_cb);

	data->wl_surface = gdk_wayland_window_get_wl_surface (window);
	g_return_if_fail (data->wl_surface);

	g_signal_connect (widget, "size-allocate", G_CALLBACK (wayland_layer_surface_size_allocate_cb), data);

	//struct wl_output *wl_output = NULL;
	uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
	char *namespace = "mate"; // not sure what this is for

	if (layer_shell_global) {
		data->layer_surface = zwlr_layer_shell_v1_get_layer_surface (layer_shell_global,
									     data->wl_surface,
									     NULL,
									     layer,
									     namespace);
		g_return_if_fail (data->layer_surface);
		zwlr_layer_surface_v1_set_size (data->layer_surface, data->width, data->height);
		zwlr_layer_surface_v1_set_keyboard_interactivity (data->layer_surface, TRUE);
		zwlr_layer_surface_v1_add_listener (data->layer_surface, &layer_surface_listener, data);
	} else {
		g_return_if_fail (xdg_wm_base_global);
		g_warning ("Layer shell protocol not supported, panel will not be placed correctly");
		data->fallback_xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base_global,
									  data->wl_surface);
		data->fallback_xdg_toplevel = xdg_surface_get_toplevel (data->fallback_xdg_surface);
		xdg_surface_add_listener (data->fallback_xdg_surface, &xdg_surface_listener, data);
	}
	wl_surface_commit (data->wl_surface);
	wl_display = gdk_wayland_display_get_wl_display (gdk_display);
	wl_display_roundtrip (wl_display);
}

void
wayland_set_strut (GdkWindow        *panel_window,
		   PanelOrientation  orientation,
		   guint32           strut_size,
		   guint32           strut_start,
		   guint32           strut_end)
{
	struct _WaylandLayerSurfaceData *data;
	gboolean needs_commit = FALSE;

	data = g_object_get_data (G_OBJECT (panel_window), wayland_layer_surface_key);
	g_return_if_fail (data);
	if (!data->layer_surface)
		return;

	if (!data->strut_data_set || data->orientation != orientation) {
		uint32_t anchor;
		switch (orientation) {
		case PANEL_ORIENTATION_LEFT:
			anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
			break;
		case PANEL_ORIENTATION_RIGHT:
			anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
			break;
		case PANEL_ORIENTATION_TOP:
			anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
			break;
		case PANEL_ORIENTATION_BOTTOM:
			anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
			break;
		default:
			g_warning ("Invalid panel orientation %d", orientation);
			anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		}
		zwlr_layer_surface_v1_set_anchor (data->layer_surface, anchor);
		data->orientation = orientation;
		needs_commit = TRUE;
	}

	if (!data->strut_data_set || data->exclusive_zone != strut_size) {
		zwlr_layer_surface_v1_set_exclusive_zone (data->layer_surface, strut_size);
		data->exclusive_zone = strut_size;
		needs_commit = TRUE;
	}

	if (needs_commit) {
		wl_surface_commit (data->wl_surface);
	}

	data->strut_data_set = TRUE;
}

struct _WaylandXdgLayerPopupData {
	struct xdg_surface *xdg_surface;
	struct xdg_popup *xdg_popup;
};

static void
wayland_destroy_popup_data_cb (struct _WaylandXdgLayerPopupData *data) {
	xdg_popup_destroy (data->xdg_popup);
	xdg_surface_destroy (data->xdg_surface);
	free (data);
}

static void
xdg_popup_handle_configure (void *data,
			    struct xdg_popup *xdg_popup,
			    int32_t x,
			    int32_t y,
			    int32_t width,
			    int32_t height)
{
	GtkWidget *menu = data;
	gtk_widget_set_size_request (menu, width, height);
}

static void
xdg_popup_handle_popup_done (void *data,
			     struct xdg_popup *xdg_popup)
{
	GtkWidget *menu = data;
	gtk_widget_unmap (menu);
}

static const struct xdg_popup_listener xdg_popup_listener = {
	.configure = xdg_popup_handle_configure,
	.popup_done = xdg_popup_handle_popup_done,
};

static void
wayland_pop_popup_up_at_positioner (GtkWidget *popup_widget,
				    GtkWidget *attach_widget,
				    struct xdg_positioner *positioner)
{
	GtkRequisition popup_size;
	GdkWindow *popup_window, *attach_window;
	struct _WaylandLayerSurfaceData *layer;
	struct wl_surface *popup_wl_surface;
	struct xdg_surface *popup_xdg_surface;
	struct xdg_popup *popup;
	struct _WaylandXdgLayerPopupData *data;

	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	gtk_widget_get_preferred_size (popup_widget, NULL, &popup_size);
	xdg_positioner_set_size (positioner, popup_size.width, popup_size.height);

	popup_window = gtk_widget_get_window (popup_widget);
	attach_window = gdk_window_get_toplevel (gtk_widget_get_window (attach_widget));

	g_assert (popup_window);
	g_assert (attach_window);

	layer = g_object_get_data (G_OBJECT (attach_window), wayland_layer_surface_key);
	popup_wl_surface = gdk_wayland_window_get_wl_surface (popup_window);
	popup_xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base_global, popup_wl_surface);
	xdg_surface_add_listener (popup_xdg_surface, &xdg_surface_listener, NULL);

	if (layer->layer_surface) {
		popup = xdg_surface_get_popup (popup_xdg_surface, NULL, positioner);
		zwlr_layer_surface_v1_get_popup (layer->layer_surface, popup);
	} else if (layer->fallback_xdg_surface) {
		popup = xdg_surface_get_popup (popup_xdg_surface, layer->fallback_xdg_surface, positioner);
	} else {
		g_assert_not_reached ();
	}
	xdg_popup_add_listener (popup, &xdg_popup_listener, popup_widget);

	data = g_new0 (struct _WaylandXdgLayerPopupData, 1);
	data->xdg_surface = popup_xdg_surface;
	data->xdg_popup = popup;
	g_object_set_data_full (G_OBJECT (popup_widget),
				wayland_popup_data_key,
				data,
				(GDestroyNotify) wayland_destroy_popup_data_cb);

	wl_surface_commit (popup_wl_surface);
	wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_window_get_display (popup_window)));
}

static void
wayland_pop_popup_up_at_widget (GtkWidget *popup_widget,
				GtkWidget *attach_widget,
				enum xdg_positioner_anchor anchor,
				enum xdg_positioner_gravity gravity,
				GdkPoint offset)
{
	struct xdg_positioner *positioner; // Wayland object we're building
	GdkRectangle popup_geom; // Rectangle on the wayland surface which makes up the "logical" window (cuts off boarders and shadows)
	gint popup_width, popup_height; // Size of the Wayland surface
	GdkPoint attach_widget_on_window; // Location of the attach widget on its parent window
	GtkAllocation attach_widget_allocation; // Size of the attach widget
	double popup_anchor_x = 0, popup_anchor_y = 0; // From 0.0 to 1.0, relative to popup surface size, the point on the popup that will be attached
	GdkPoint positioner_offset; // The final calculated offset to be sent to the positioner

	g_return_if_fail (wayland_has_initialized);
	g_return_if_fail (xdg_wm_base_global);

	positioner = xdg_wm_base_create_positioner (xdg_wm_base_global);
	attach_widget = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_widget_key);

	gtk_widget_translate_coordinates (attach_widget, gtk_widget_get_toplevel (attach_widget),
					  0, 0,
					  &attach_widget_on_window.x, &attach_widget_on_window.y);
	gtk_widget_get_allocated_size (attach_widget, &attach_widget_allocation, NULL);
	gdk_window_get_geometry (gtk_widget_get_window (popup_widget),
				 &popup_geom.x, &popup_geom.y,
				 &popup_geom.width, &popup_geom.height);
	popup_width = gdk_window_get_width (gtk_widget_get_window (popup_widget));
	popup_height = gdk_window_get_height (gtk_widget_get_window (popup_widget));
	xdg_positioner_set_anchor_rect (positioner,
					MAX (attach_widget_on_window.x, 0), MAX (attach_widget_on_window.y, 0),
					MAX (attach_widget_allocation.width, 1), MAX (attach_widget_allocation.height, 1));
	switch (gravity) {
	case XDG_POSITIONER_GRAVITY_LEFT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
	case XDG_POSITIONER_GRAVITY_TOP_LEFT:
		popup_anchor_x = 0;
		break;

	case XDG_POSITIONER_GRAVITY_NONE:
	case XDG_POSITIONER_GRAVITY_BOTTOM:
	case XDG_POSITIONER_GRAVITY_TOP:
		popup_anchor_x = 0.5;
		break;

	case XDG_POSITIONER_GRAVITY_RIGHT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
	case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
		popup_anchor_x = 1;
		break;
	}
	switch (gravity) {
	case XDG_POSITIONER_GRAVITY_TOP:
	case XDG_POSITIONER_GRAVITY_TOP_LEFT:
	case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
		popup_anchor_y = 0;
		break;

	case XDG_POSITIONER_GRAVITY_NONE:
	case XDG_POSITIONER_GRAVITY_LEFT:
	case XDG_POSITIONER_GRAVITY_RIGHT:
		popup_anchor_y = 1;
		break;

	case XDG_POSITIONER_GRAVITY_BOTTOM:
	case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
	case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
		popup_anchor_y = 0.5;
		break;
	}
	positioner_offset.x = (popup_width  - popup_geom.width)  * popup_anchor_x - popup_geom.x + offset.x;
	positioner_offset.y = (popup_height - popup_geom.height) * popup_anchor_y - popup_geom.y + offset.y;
	xdg_positioner_set_offset (positioner, positioner_offset.x, positioner_offset.y);
	xdg_positioner_set_anchor (positioner, anchor);
	xdg_positioner_set_gravity (positioner, gravity);
	xdg_positioner_set_constraint_adjustment (positioner,
						  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
						  | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);

	wayland_pop_popup_up_at_positioner (popup_widget, attach_widget, positioner);

	xdg_positioner_destroy (positioner);
}

static void
wayland_popup_realize_cb (GtkWidget *popup_widget, void *_data)
{
	gdk_wayland_window_set_use_custom_surface (gtk_widget_get_window (popup_widget));
}

// This callback overrides the default unmap handler
static void
wayland_popup_unmap_override_cb (GtkWidget *popup_widget, void *_data)
{
	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	// Call the default unmap handler
	GValue args[1] = { G_VALUE_INIT };
	g_value_init (&args[0], G_TYPE_FROM_INSTANCE (popup_widget));
	g_value_set_object (&args[0], popup_widget);
	g_signal_chain_from_overridden (args, NULL);
	g_value_unset (&args[0]);
}

static void
wayland_set_popup_attach_widget (GtkWidget *popup_widget, GtkWidget* attach_widget, GCallback map_event_cb)
{
	GtkWidget *prev_attach_widget;

	popup_widget = gtk_widget_get_toplevel (popup_widget);

	// Get the previous window this popup was attached to
	prev_attach_widget = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_widget_key);

	// If there's not already an attach widget, the callbacks haven't been set up yet either
	if (!prev_attach_widget) {
		// On unmap, we need to destroy the shell surface we create before GTK destroys its wl_surface
		// To do that, we have to override the default unmap signal
		GType popup_type = G_TYPE_FROM_INSTANCE (popup_widget);
		gint unmap_signal_id = g_signal_lookup ("unmap", popup_type);
		GClosure* closure = g_cclosure_new (G_CALLBACK (wayland_popup_unmap_override_cb), NULL, NULL);
		g_signal_override_class_closure (unmap_signal_id, popup_type, closure);

		g_signal_connect (popup_widget, "realize", G_CALLBACK (wayland_popup_realize_cb), NULL);
		g_signal_connect (popup_widget, "map-event", map_event_cb, NULL);
	}

	// if the attached window was null before or has changed, set it to the new value
	if (attach_widget != prev_attach_widget) {
		g_object_set_data (G_OBJECT (popup_widget),
				   wayland_popup_attach_widget_key,
				   attach_widget);
	}
}

static gboolean
wayland_menu_map_event_cb (GtkWidget *popup_widget, GdkEvent *event, void *_data)
{
	GtkWidget *attach_widget;
	PanelToplevel *toplevel;
	enum xdg_positioner_anchor anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
	enum xdg_positioner_gravity gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
	GdkPoint offset = {0, 0};

	attach_widget = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_widget_key);
	g_return_val_if_fail (attach_widget, FALSE);

	toplevel = PANEL_TOPLEVEL (gtk_widget_get_toplevel (attach_widget));

	if (toplevel) {
		switch (panel_toplevel_get_orientation (toplevel)) {
		case PANEL_ORIENTATION_TOP:
			anchor = XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
			gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
			break;
		case PANEL_ORIENTATION_RIGHT:
			anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
			gravity = XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
			break;
		case PANEL_ORIENTATION_BOTTOM:
			anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
			gravity = XDG_POSITIONER_GRAVITY_TOP_RIGHT;
			break;
		case PANEL_ORIENTATION_LEFT:
			anchor = XDG_POSITIONER_ANCHOR_TOP_RIGHT;
			gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
			break;
		}
	} else {
		g_warning ("Failed to find toplevel for popup");
	}

	if (attach_widget == GTK_WIDGET (toplevel)) {
		anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
		widget_get_pointer_position (attach_widget, &offset.x, &offset.y);
	}

	wayland_pop_popup_up_at_widget (popup_widget,
					attach_widget,
					anchor,
					gravity,
					offset);

	return TRUE;
}

void
wayland_popup_menu_setup (GtkWidget *menu, GtkWidget *attach_widget)
{
	wayland_set_popup_attach_widget (menu, attach_widget, G_CALLBACK (wayland_menu_map_event_cb));
}

static gboolean
wayland_tooltip_map_event_cb (GtkWidget *popup_widget, GdkEvent *event, void *_data)
{
	GtkWidget *attach_widget;
	PanelToplevel *toplevel;
	enum xdg_positioner_anchor anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
	enum xdg_positioner_gravity gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
	static const int gap = 6;
	GdkPoint offset = {0, 0};

	attach_widget = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_widget_key);
	g_return_val_if_fail (attach_widget, FALSE);

	toplevel = PANEL_TOPLEVEL (gtk_widget_get_toplevel (attach_widget));

	if (toplevel) {
		switch (panel_toplevel_get_orientation (toplevel)) {
		case PANEL_ORIENTATION_TOP:
			anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
			gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
			offset.y = gap;
			break;
		case PANEL_ORIENTATION_RIGHT:
			anchor = XDG_POSITIONER_ANCHOR_LEFT;
			gravity = XDG_POSITIONER_GRAVITY_LEFT;
			offset.x = -gap;
			break;
		case PANEL_ORIENTATION_BOTTOM:
			anchor = XDG_POSITIONER_ANCHOR_TOP;
			gravity = XDG_POSITIONER_GRAVITY_TOP;
			offset.y = -gap;
			break;
		case PANEL_ORIENTATION_LEFT:
			anchor = XDG_POSITIONER_ANCHOR_RIGHT;
			gravity = XDG_POSITIONER_GRAVITY_RIGHT;
			offset.x = gap;
			break;
		}
	} else {
		g_warning ("Failed to find toplevel for tooltop");
	}

	wayland_pop_popup_up_at_widget (popup_widget,
					attach_widget,
					anchor,
					gravity,
					offset);

	return TRUE;
}

struct _WaylandCustomTooltipData {
	GtkWidget *window; // NOTE: Gtk, NOT Gdk
	GtkWidget *box;
	GtkWidget *label;
};

static void
wayland_custom_tooltip_destroy_cb (struct _WaylandCustomTooltipData *data) {
	gtk_widget_destroy (data->window);
	// gtk should take care of the child widgets
	g_free (data);
}

void
wayland_tooltip_setup (GtkWidget  *widget,
		       gint        x,
		       gint        y,
		       gboolean    keyboard_tip,
		       GtkTooltip *tooltip,
		       void       *_data)
{
	const char *tooltip_text, *tooltip_markup;
	struct _WaylandCustomTooltipData *widget_data;

	tooltip_text = gtk_widget_get_tooltip_text (widget);

	widget_data = g_object_get_data (G_OBJECT (widget), custom_tooltip_widget_key);

	if (!widget_data) {
		GtkStyleContext *context;
		widget_data = g_new0 (struct _WaylandCustomTooltipData, 1);
		widget_data->window = gtk_window_new (GTK_WINDOW_POPUP);
		context = gtk_widget_get_style_context (widget_data->window);
		// TODO: why does this not work?
		gtk_style_context_add_class (context, GTK_STYLE_CLASS_TOOLTIP);
		widget_data->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		widget_data->label = gtk_label_new ("");
		gtk_container_set_border_width (GTK_CONTAINER (widget_data->window), 4);
		gtk_container_add (GTK_CONTAINER (widget_data->box), widget_data->label);
		gtk_container_add (GTK_CONTAINER (widget_data->window), widget_data->box);
		gtk_widget_show_all (widget_data->box);
		gtk_widget_set_tooltip_window (widget, GTK_WINDOW (widget_data->window));
		g_object_set_data_full (G_OBJECT (widget),
					custom_tooltip_widget_key,
					widget_data,
					(GDestroyNotify) wayland_custom_tooltip_destroy_cb);
		wayland_set_popup_attach_widget (widget_data->window,
						 widget,
						 G_CALLBACK (wayland_tooltip_map_event_cb));
	}

	tooltip_text = gtk_widget_get_tooltip_text (widget);
	tooltip_markup = gtk_widget_get_tooltip_markup (widget);
	if (tooltip_markup) {
		gtk_label_set_markup (GTK_LABEL (widget_data->label), tooltip_markup);
	} else {
		gtk_label_set_text (GTK_LABEL (widget_data->label), tooltip_text);
	}
}
