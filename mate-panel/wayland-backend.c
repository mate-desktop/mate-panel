#include <config.h>

#include "wayland-backend.h"
#include "panel-toplevel.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct zwlr_layer_shell_v1 *layer_shell_global = NULL;
struct xdg_wm_base *xdg_wm_base_global = NULL;
static gboolean wayland_has_initialized = FALSE;
static const char *wayland_popup_data_key = "wayland_popup_data";
static const char *wayland_popup_attach_window_key = "wayland_popup_attach_window";
static const char *wayland_layer_surface_key = "wayland_layer_surface";
static const char *menu_setup_func_key = "popup_menu_setup_func";

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
	int has_layeer_surface;
	int width, height;
	int has_native;

	window_type = gdk_window_get_type_hint (window);
	window_type_name = debug_print_gdk_window_type_hint_get_name (window_type);
	has_layeer_surface = g_object_get_data(G_OBJECT (window), wayland_layer_surface_key) != NULL;
	width = gdk_window_get_width (window);
	height = gdk_window_get_height (window);
	has_native = gdk_window_has_native (window);

	if (has_native) {

		debug_print_label ("Address");
		debug_print_style ("1;33");
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

	debug_print_bool("Layer Surface", has_layeer_surface, FALSE, indent);
	debug_print_bool("Special", window == highlight, FALSE, indent);
}

static void
debuug_print_window_tree_branch (GdkWindow *window, GdkWindow *highlight, GList *indent)
{
	GList *list, *elem, *indent_node;

	debug_print_window_info (window, highlight, indent);

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

	debug_print_style ("0");
	fflush (stdout);
}

void
debug_print_window_tree(GdkWindow *current, const char *code_path, int code_line_num)
{
	GdkWindow *root, *next;

	debug_print_style("1;35");
	printf("%s", code_path);
	debug_print_style("0");
	printf(":");
	debug_print_style("1;37");
	printf("%d\n", code_line_num);

	root = next = current;
	while (next) {
		root = next;
		next = gdk_window_get_parent (next);
	}

	debuug_print_window_tree_branch (root, current, 0);
	printf ("\n");
}

gboolean
is_using_wayland ()
{
	return GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ());
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

void
wayland_realize_panel_toplevel (GtkWidget *widget)
{
	GdkDisplay *gdk_display;
	GdkWindow *window;
	struct wl_surface *wl_surface;
	struct zwlr_layer_surface_v1 *layer_surface;
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
	g_object_set_data(G_OBJECT (window),
			  menu_setup_func_key,
			  wayland_menu_setup);
	gdk_wayland_window_set_use_custom_surface (window);
	wl_surface = gdk_wayland_window_get_wl_surface (window);
	g_assert (wl_surface);

	//struct wl_output *wl_output = NULL;
	uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
	char *namespace = "mate"; // not sure what this is for

	layer_surface = zwlr_layer_shell_v1_get_layer_surface (layer_shell_global,
							       wl_surface,
							       NULL,
							       layer,
							       namespace);
	g_assert (layer_surface);
	g_object_set_data_full(G_OBJECT (window),
			       wayland_layer_surface_key,
			       layer_surface,
			       (GDestroyNotify) zwlr_layer_surface_v1_destroy);
	// GdkRectangle rect;
	// gdk_monitor_get_geometry(gdk_monitor, &rect);
	gint width = 0, height = 0;
	gtk_window_get_size (GTK_WINDOW (widget), &width, &height);
	zwlr_layer_surface_v1_set_size (layer_surface, width, height);
	zwlr_layer_surface_v1_set_anchor (layer_surface,
					  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	//zwlr_layer_surface_v1_set_exclusive_zone (layer_surface, exclusive_zone);
	//zwlr_layer_surface_v1_set_margin (layer_surface, margin_top, margin_right, margin_bottom, margin_left);
	zwlr_layer_surface_v1_set_keyboard_interactivity (layer_surface, FALSE);
	zwlr_layer_surface_v1_set_exclusive_zone (layer_surface, 200);
	zwlr_layer_surface_v1_add_listener (layer_surface, &layer_surface_listener, NULL);
	wl_surface_commit (wl_surface);
	wl_display = gdk_wayland_display_get_wl_display (gdk_display);
	wl_display_roundtrip (wl_display);
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
wayland_setup_positioner (struct xdg_positioner *positioner, GdkWindow *panel_window, GtkWidget *popup, gint offset_x, gint offset_y)
{
	GtkRequisition popup_size;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkDevice *pointer;
	gint pointer_x, pointer_y;

	gtk_widget_get_preferred_size (popup, NULL, &popup_size);
	xdg_positioner_set_size (positioner, popup_size.width, popup_size.height);
	display = gdk_window_get_display (panel_window);
	seat = gdk_display_get_default_seat (display);
	pointer = gdk_seat_get_pointer (seat);
	gdk_window_get_device_position(panel_window, pointer, &pointer_x, &pointer_y, NULL);
	xdg_positioner_set_anchor_rect (positioner, 0, 0, pointer_x, pointer_y);
	xdg_positioner_set_offset(positioner, offset_x, offset_y);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
	xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_TOP_RIGHT);
	xdg_positioner_set_constraint_adjustment(positioner, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
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
wayland_context_menu_map_event_cb (GtkWidget *popup_widget, GdkEvent *event, void *_data)
{
	struct zwlr_layer_surface_v1 *layer_surface;
	struct xdg_surface *popup_xdg_surface;
	struct xdg_positioner *positioner;
	struct xdg_popup *popup;
	GdkWindow *popup_window, *attach_window;
	struct wl_surface *popup_wl_surface;
	gint geom_x, geom_y, geom_width, geom_height;
	struct _WaylandXdgLayerPopupData *data;

	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	popup_window = gtk_widget_get_window (popup_widget);
	attach_window = g_object_get_data (G_OBJECT (popup_widget), wayland_popup_attach_window_key);

	g_assert (wayland_has_initialized);
	g_assert (xdg_wm_base_global);
	g_assert (popup_window);
	g_assert (attach_window);

	layer_surface = g_object_get_data (G_OBJECT (attach_window), wayland_layer_surface_key);
	popup_wl_surface = gdk_wayland_window_get_wl_surface (popup_window);

	g_assert (layer_surface);
	g_assert (popup_wl_surface);

	popup_xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base_global, popup_wl_surface);
	xdg_surface_add_listener(popup_xdg_surface, &xdg_surface_listener, NULL);
	positioner = xdg_wm_base_create_positioner (xdg_wm_base_global);
	gdk_window_get_geometry(popup_window, &geom_x, &geom_y, &geom_width, &geom_height);
	wayland_setup_positioner (positioner, attach_window, popup_widget, -geom_x, -geom_y);
	popup = xdg_surface_get_popup (popup_xdg_surface, NULL, positioner);
	xdg_positioner_destroy (positioner);
	xdg_popup_add_listener(popup, &xdg_popup_listener, popup_widget);
// 	xdg_surface_set_window_geometry(popup_xdg_surface, geom_x, geom_y, geom_width, geom_height);
	zwlr_layer_surface_v1_get_popup (layer_surface, popup);

	data = g_new0 (struct _WaylandXdgLayerPopupData, 1);
	data->xdg_surface = popup_xdg_surface;
	data->xdg_popup = popup;
	g_object_set_data_full (G_OBJECT (popup_widget),
				wayland_popup_data_key,
				data,
				(GDestroyNotify) wayland_destroy_popup_data_cb);

	wl_surface_commit (popup_wl_surface);
	wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_window_get_display (popup_window)));

	return TRUE;
}

static gboolean
wayland_context_menu_unmap_cb (GtkWidget *popup_widget, void *_data)
{
	g_object_set_data (G_OBJECT (popup_widget),
			   wayland_popup_data_key,
			   NULL);

	return TRUE;
}

void
wayland_menu_setup (GtkWidget *menu, GdkWindow *attach_window)
{
	GdkWindow *prev_attach_window;

	g_assert (attach_window);

	// Get the previous window this menu was attached to
	prev_attach_window = g_object_get_data (G_OBJECT (menu), wayland_popup_attach_window_key);

	// If there's not already an attached window, the callbacks haven't been set up yet either'
	if (!prev_attach_window) {
		g_signal_connect (menu, "realize", G_CALLBACK (wayland_context_menu_realize_cb), NULL);
		g_signal_connect (menu, "map-event", G_CALLBACK (wayland_context_menu_map_event_cb), NULL);
		g_signal_connect (menu, "unmap", G_CALLBACK (wayland_context_menu_unmap_cb), NULL);
	}

	// if the attached window was null before or has changed, set it to the new value
	if (attach_window != prev_attach_window) {
		g_object_set_data (G_OBJECT (menu),
				   wayland_popup_attach_window_key,
				   attach_window);
	}
}

