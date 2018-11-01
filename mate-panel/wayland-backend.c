#include <config.h>
#include <math.h>

#include "wayland-backend.h"

#include "panel-toplevel.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct zwlr_layer_shell_v1 *layer_shell_global = NULL;
struct xdg_wm_base *xdg_wm_base_global = NULL;
static gboolean wayland_has_initialized = FALSE;
static const char *wayland_popup_data_key = "wayland_popup_data";
static const char *wayland_popup_attach_widget_key = "wayland_popup_attach_widget";
static const char *wayland_layer_surface_key = "wayland_layer_surface";
static const char *wayland_pointer_position_key = "wayland_pointer_position";
static const char *menu_setup_func_key = "popup_menu_setup_func";
static const char *tooltip_setup_func_key = "tooltip_setup_func";

static void
debug_print_style(const char *style)
{
	printf ("\x1b[%sm", style);
}

static void
debug_print_window_tree_indent (GList *indent)
{
	debug_print_style ("0;37");
	for (; indent; indent = indent->next) {
		printf (indent->data ? "   \u2502 " : "     ");
	}
}

static void
debug_print_line_start ()
{
	debug_print_window_tree_indent (NULL);
	printf ("\u258e");
}

static void
debug_print_label (const char* label)
{
	debug_print_line_start ();
	printf ("%s: ", label);
}

static void
debug_print_bool (const char* label, int value, int default_value, GList *indent)
{
	if (value == default_value)
		return;
	debug_print_window_tree_indent (indent);
	debug_print_line_start ();
	if (value) {
		debug_print_style ("1;32");
		printf ("%s: Yes\n", label);
	} else {
		debug_print_style ("1;31");
		printf ("%s: No\n", label);
	}
}

static void
debug_print_g_object_data (GdkWindow *window, const char * key, GList *indent)
{
	void *data = g_object_get_data (G_OBJECT (window), key);

	if (!data)
		return;

	debug_print_window_tree_indent (indent);
	debug_print_line_start ();
	debug_print_style ("1;37");
	printf ("\"%s\"", key);
	debug_print_style ("0;37");
	printf (": ");
	debug_print_style ("0;36");
	printf ("%p\n", data);
}

static const char *
debug_print_gdk_window_type_hint_get_name(GdkWindowTypeHint type)
{
	switch (type) {
		case GDK_WINDOW_TYPE_HINT_NORMAL: return "NORMAL";
		case GDK_WINDOW_TYPE_HINT_DIALOG: return "DIALOG";
		case GDK_WINDOW_TYPE_HINT_MENU: return "MENU";
		case GDK_WINDOW_TYPE_HINT_TOOLBAR: return "TOOLBAR";
		case GDK_WINDOW_TYPE_HINT_SPLASHSCREEN: return "SPLASHSCREEN";
		case GDK_WINDOW_TYPE_HINT_UTILITY: return "UTILITY";
		case GDK_WINDOW_TYPE_HINT_DOCK: return "DOCK";
		case GDK_WINDOW_TYPE_HINT_DESKTOP: return "DESKTOP";
		case GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU: return "DROPDOWN_MENU";
		case GDK_WINDOW_TYPE_HINT_POPUP_MENU: return "POPUP_MENU";
		case GDK_WINDOW_TYPE_HINT_TOOLTIP: return "TOOLTIP";
		case GDK_WINDOW_TYPE_HINT_NOTIFICATION: return "NOTIFICATION";
		case GDK_WINDOW_TYPE_HINT_COMBO: return "COMBO";
		case GDK_WINDOW_TYPE_HINT_DND: return "DND";
		default: return "[UNKNOWN]";
	}
}

static void
debug_print_gdk_window_state_print_elem(const char * elem, int show, GList *indent)
{
	if (show) {
		debug_print_window_tree_indent (indent);
		debug_print_label ("State");
		debug_print_style ("1;34");
		printf ("%s\n", elem);
	}
}

static void
debug_print_gdk_window_state_print(GdkWindowState state, GList *indent)
{
	debug_print_gdk_window_state_print_elem ("WITHDRAWN", state & GDK_WINDOW_STATE_WITHDRAWN, indent);
	debug_print_gdk_window_state_print_elem ("ICONIFIED", state & GDK_WINDOW_STATE_ICONIFIED, indent);
	debug_print_gdk_window_state_print_elem ("MAXIMIZED", state & GDK_WINDOW_STATE_MAXIMIZED, indent);
	debug_print_gdk_window_state_print_elem ("STICKY", state & GDK_WINDOW_STATE_STICKY, indent);
	debug_print_gdk_window_state_print_elem ("FULLSCREEN", state & GDK_WINDOW_STATE_FULLSCREEN, indent);
	debug_print_gdk_window_state_print_elem ("ABOVE", state & GDK_WINDOW_STATE_ABOVE, indent);
	debug_print_gdk_window_state_print_elem ("BELOW", state & GDK_WINDOW_STATE_BELOW, indent);
	debug_print_gdk_window_state_print_elem ("FOCUSED", state & GDK_WINDOW_STATE_FOCUSED, indent);
	debug_print_gdk_window_state_print_elem ("TILED", state & GDK_WINDOW_STATE_TILED, indent);
	debug_print_gdk_window_state_print_elem ("TOP_TILED", state & GDK_WINDOW_STATE_TOP_TILED, indent);
	debug_print_gdk_window_state_print_elem ("TOP_RESIZABLE", state & GDK_WINDOW_STATE_TOP_RESIZABLE, indent);
	debug_print_gdk_window_state_print_elem ("RIGHT_TILED", state & GDK_WINDOW_STATE_RIGHT_TILED, indent);
	debug_print_gdk_window_state_print_elem ("RIGHT_RESIZABLE", state & GDK_WINDOW_STATE_RIGHT_RESIZABLE, indent);
	debug_print_gdk_window_state_print_elem ("BOTTOM_TILED", state & GDK_WINDOW_STATE_BOTTOM_TILED, indent);
	debug_print_gdk_window_state_print_elem ("BOTTOM_RESIZABLE", state & GDK_WINDOW_STATE_BOTTOM_RESIZABLE, indent);
	debug_print_gdk_window_state_print_elem ("LEFT_TILED", state & GDK_WINDOW_STATE_LEFT_TILED, indent);
	debug_print_gdk_window_state_print_elem ("LEFT_RESIZABLE", state & GDK_WINDOW_STATE_LEFT_RESIZABLE, indent);
}

static void
debug_print_window_info (GdkWindow *window, GdkWindow *highlight, GList *indent)
{
	GdkWindowTypeHint window_type;
	const char *window_type_name;
	int width, height;
	int has_native;

	if (window == NULL) {
		debug_print_bool("Valid Window Object", FALSE, TRUE, indent);
		return;
	}

	window_type = gdk_window_get_type_hint (window);
	window_type_name = debug_print_gdk_window_type_hint_get_name (window_type);
	width = gdk_window_get_width (window);
	height = gdk_window_get_height (window);
	has_native = gdk_window_has_native (window);

	if (has_native) {

		debug_print_label ("Address");
		debug_print_style ("0;36");
		printf ("%p\n", window);
		if (window_type != GDK_WINDOW_TYPE_HINT_NORMAL) {

			debug_print_window_tree_indent (indent);
			debug_print_label ("Type");
			debug_print_style ("1;34");
			printf ("%s\n", window_type_name);
		}
		debug_print_gdk_window_state_print (gdk_window_get_state (window), indent);
		debug_print_window_tree_indent (indent);
		debug_print_label ("Size");
		debug_print_style ("1;34");
		printf ("%d x %d\n", width, height);
		debug_print_bool("Has Visual", gdk_window_get_visual (window) != NULL, TRUE, indent);
		debug_print_bool("Focusable", gdk_window_get_accept_focus (window), TRUE, indent);
		debug_print_bool("Decorations", gdk_window_get_decorations (window, NULL), FALSE, indent);
	} else {
		debug_print_label ("Internal Window");
		printf("%dx%d %s\n", width, height, window_type_name);
	}

	debug_print_g_object_data (window, wayland_popup_data_key, indent);
	debug_print_g_object_data (window, wayland_popup_attach_widget_key, indent);
	debug_print_g_object_data (window, wayland_layer_surface_key, indent);
	debug_print_g_object_data (window, wayland_pointer_position_key, indent);
	debug_print_g_object_data (window, menu_setup_func_key, indent);
	debug_print_g_object_data (window, tooltip_setup_func_key, indent);
	debug_print_bool("Special", window == highlight, FALSE, indent);
}

static void
debuug_print_window_tree_branch (GdkWindow *window, GdkWindow *highlight, GList *indent)
{
	GList *list, *elem, *indent_node;

	debug_print_window_info (window, highlight, indent);

	if (window) {
			list = gdk_window_get_children (window);

			for (elem = list; elem; elem = elem->next) {
				debug_print_window_tree_indent (indent);
				printf("%s\u2502 ", elem == list ? "\u2594\u2594\u2594" : "   ");
				printf("\u2581\u2581\u2581\u2581\u2581\u2581\u2581\u2581\n");
				debug_print_window_tree_indent (indent);
				if (elem->next)
					printf ("   \u251c\u2500");
				else
					printf ("   \u2570\u2500");
				indent_node = g_list_append (NULL, elem->next); // all we care about is if its NULL or not
				indent = g_list_concat(indent, indent_node);
				debuug_print_window_tree_branch (elem->data, highlight, indent);
				indent = g_list_remove_link(indent, indent_node);
				g_list_free_1 (indent_node);
			}

			g_list_free (list);
	}

	debug_print_style ("0");
	fflush (stdout);
}

void
debug_print_window_tree(GdkWindow *current, const char *code_path, int code_line_num)
{
	GdkWindow *root, *next;
	static GdkWindow* cached_root_window = NULL;

	if (current == NULL)
		current = cached_root_window;

	debug_print_style("1;35");
	if (code_path) {
		printf("%s", code_path);
		debug_print_style("0");
		printf(":");
		debug_print_style("1;37");
		printf("%d\n", code_line_num);
	} else {
		printf("[UNKNOWN CALLER]\n");
	}

	root = next = current;
	while (next) {
		root = next;
		next = gdk_window_get_parent (next);
	}

	cached_root_window = root;

	debuug_print_window_tree_branch (root, current, 0);
	printf ("\n");
}

gboolean
is_using_wayland ()
{
	return GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ());
}

/*
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
	gdk_window_get_device_position(window, pointer, pointer_x, pointer_y, NULL);
}
*/

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
	//gtk_window_set_default_size(GTK_WINDOW(window), width, height);
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
		g_warning("Layer shell global not bound");
	wayland_has_initialized = TRUE;
}

// struct wl_output *
// get_primary_wl_output (GdkDisplay *gdk_display)
// {
// 	GdkMonitor *gdk_monitor = gdk_display_get_primary_monitor (gdk_display);
//
// 	if (gdk_monitor == NULL && gdk_display_get_n_monitors (gdk_display) > 0)
// 		gdk_monitor = gdk_display_get_monitor (gdk_display, 0);
//
// 	if (gdk_monitor)
// 		return gdk_wayland_monitor_get_wl_output (gdk_monitor);
// 	else
// 		return NULL;
// }

struct _WaylandLayerSurfaceData {
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_surface *wl_surface;
	gint width, height;

	gboolean strut_data_set; // if orientation and exclusive_zone have been set
	PanelOrientation orientation;
	int exclusive_zone;
};

static void
wayland_destroy_layer_surface_data_cb (struct _WaylandLayerSurfaceData *data) {
	if (data->layer_surface)
		zwlr_layer_surface_v1_destroy (data->layer_surface);
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
		zwlr_layer_surface_v1_set_size (data->layer_surface, data->width, data->height);
	}
}

void
wayland_realize_panel_toplevel (GtkWidget *widget)
{
	GdkDisplay *gdk_display;
	GdkWindow *window;
	struct _WaylandLayerSurfaceData *data;
	struct wl_display *wl_display;

	g_assert(wayland_has_initialized);

	gdk_display = gdk_window_get_display (gtk_widget_get_window (widget));

	g_assert (GDK_IS_WAYLAND_DISPLAY (gdk_display));
	g_assert (wayland_has_initialized);

	if (!layer_shell_global) {
		g_warning ("Layer shell protocol not supported");
		return;
	}

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
	g_object_set_data_full(G_OBJECT (window),
			       wayland_layer_surface_key,
			       data,
			       (GDestroyNotify) wayland_destroy_layer_surface_data_cb);

	data->wl_surface = gdk_wayland_window_get_wl_surface (window);
	g_return_if_fail (data->wl_surface);

	g_signal_connect (widget, "size-allocate", G_CALLBACK (wayland_layer_surface_size_allocate_cb), data);

	//struct wl_output *wl_output = NULL;
	uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
	char *namespace = "mate"; // not sure what this is for

	data->layer_surface = zwlr_layer_shell_v1_get_layer_surface (layer_shell_global,
								     data->wl_surface,
								     NULL,
								     layer,
								     namespace);
	g_return_if_fail (data->layer_surface);
	zwlr_layer_surface_v1_set_size (data->layer_surface, data->width, data->height);
	zwlr_layer_surface_v1_set_keyboard_interactivity (data->layer_surface, TRUE);
	zwlr_layer_surface_v1_add_listener (data->layer_surface, &layer_surface_listener, data);
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
	xdg_surface_destroy (data->xdg_surface);
	xdg_popup_destroy (data->xdg_popup);
	free (data);
}

static void
xdg_surface_handle_configure (void *data,
			      struct xdg_surface *xdg_surface,
			      uint32_t serial)
{
	xdg_surface_ack_configure (xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

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
	gtk_widget_unmap(menu);
}

static const struct xdg_popup_listener xdg_popup_listener = {
	.configure = xdg_popup_handle_configure,
	.popup_done = xdg_popup_handle_popup_done,
};

static void
wayland_pop_popup_up_at_positioner (GtkWidget *attach_widget,
				    GtkWidget *popup_widget,
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

	popup = xdg_surface_get_popup (popup_xdg_surface, NULL, positioner);
	xdg_popup_add_listener (popup, &xdg_popup_listener, popup_widget);
// 	xdg_surface_set_window_geometry(popup_xdg_surface, geom_x, geom_y, geom_width, geom_height);
	zwlr_layer_surface_v1_get_popup (layer->layer_surface, popup);

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
wayland_pop_popup_up_at_widget (GtkWidget *attach_widget,
				GtkWidget *popup_widget,
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

	gtk_widget_translate_coordinates(attach_widget, gtk_widget_get_toplevel(attach_widget),
					 0, 0,
					 &attach_widget_on_window.x, &attach_widget_on_window.y);
	gtk_widget_get_allocated_size(attach_widget, &attach_widget_allocation, NULL);
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

	wayland_pop_popup_up_at_positioner (attach_widget, popup_widget, positioner);

	xdg_positioner_destroy (positioner);
}

static void
wayland_context_menu_realize_cb (GtkWidget *popup_widget, void *_data)
{
	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	gdk_wayland_window_set_use_custom_surface (gtk_widget_get_window (popup_widget));
}

static gboolean
wayland_context_menu_unmap_cb (GtkWidget *popup_widget, void *_data)
{
	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	return TRUE;
}

static gboolean
wayland_menu_map_event_cb (GtkWidget *popup_widget, GdkEvent *event, void *_data)
{
	GtkWidget *attach_widget;
	PanelToplevel *toplevel;
	enum xdg_positioner_anchor anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
	enum xdg_positioner_gravity gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;

	// GdkPoint *pointer_on_attach_widget;
	// gint pointer_x, pointer_y;

	// GdkPoint *pointer_on_attach_widget = g_object_get_data (G_OBJECT (attach_widget), wayland_pointer_position_key);
	// widget_get_pointer_position (attach_widget, &pointer_x, &pointer_y);

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

	wayland_pop_popup_up_at_widget (attach_widget,
					popup_widget,
					anchor,
					gravity,
					(GdkPoint){0, 0});

	return TRUE;
}

static void
wayland_set_popup_attach_widget(GtkWidget *popup_widget, GtkWidget* attach_widget, GCallback map_event_cb)
{
	GtkWidget *prev_attach_widget;

	// Get the previous window this popup was attached to
	prev_attach_widget = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_widget_key);

	// If there's not already an attach widget, the callbacks haven't been set up yet either'
	if (!prev_attach_widget) {
		g_signal_connect (popup_widget, "realize", G_CALLBACK (wayland_context_menu_realize_cb), NULL);
		g_signal_connect (popup_widget, "map-event", map_event_cb, NULL);
		g_signal_connect (popup_widget, "unmap", G_CALLBACK (wayland_context_menu_unmap_cb), NULL);
	}

	// if the attached window was null before or has changed, set it to the new value
	if (attach_widget != prev_attach_widget) {
		g_object_set_data (G_OBJECT (popup_widget),
				   wayland_popup_attach_widget_key,
				   attach_widget);
	}
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

	wayland_pop_popup_up_at_widget (attach_widget,
					popup_widget,
					anchor,
					gravity,
					offset);

	return TRUE;
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
	GtkWidget *tooltip_window_widget; // NOTE: Gtk, NOT Gdk
	GtkWidget *box;
	GtkWidget *label;
	GdkPoint *pointer_point_pointer;

	tooltip_text = gtk_widget_get_tooltip_text (widget);
	tooltip_window_widget = gtk_window_new (GTK_WINDOW_POPUP);
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new (tooltip_text);
	tooltip_markup = gtk_widget_get_tooltip_markup (widget);
	if (tooltip_markup)
		gtk_label_set_markup (GTK_LABEL (label), tooltip_markup);
	gtk_container_add (GTK_CONTAINER (box), label);
	gtk_container_add (GTK_CONTAINER (tooltip_window_widget), box);
	gtk_widget_show_all (box);
	// TODO: make the tooltip look nice
	gtk_widget_set_tooltip_window (widget, GTK_WINDOW (tooltip_window_widget));
	// TODO: is tooltip_window_widget now owned by GTK, or do we need to destroy it?
	pointer_point_pointer = g_new0 (GdkPoint, 1);
	pointer_point_pointer->x = x;
	pointer_point_pointer->y = y;
	g_object_set_data_full (G_OBJECT (widget), wayland_pointer_position_key, pointer_point_pointer, g_free);
	wayland_set_popup_attach_widget (tooltip_window_widget, widget, G_CALLBACK (wayland_tooltip_map_event_cb));
}

