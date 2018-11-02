
#include "panel-debug-print.h"

static void
_panel_debug_print_style(const char *style)
{
	printf ("\x1b[%sm", style);
}

static void
_panel_debug_print_window_tree_indent (GList *indent)
{
	_panel_debug_print_style ("0;37");
	for (; indent; indent = indent->next) {
		printf (indent->data ? "   \u2502 " : "     ");
	}
}

static void
_panel_debug_print_line_start ()
{
	_panel_debug_print_window_tree_indent (NULL);
	printf ("\u258e");
}

static void
_panel_debug_print_label (const char* label)
{
	_panel_debug_print_line_start ();
	printf ("%s: ", label);
}

static void
_panel_debug_print_bool (const char* label, int value, int default_value, GList *indent)
{
	if (value == default_value)
		return;
	_panel_debug_print_window_tree_indent (indent);
	_panel_debug_print_line_start ();
	if (value) {
		_panel_debug_print_style ("1;32");
		printf ("%s: Yes\n", label);
	} else {
		_panel_debug_print_style ("1;31");
		printf ("%s: No\n", label);
	}
}

static void
_panel_debug_print_g_object_data (GdkWindow *window, const char * key, GList *indent)
{
	void *data = g_object_get_data (G_OBJECT (window), key);

	if (!data)
		return;

	_panel_debug_print_window_tree_indent (indent);
	_panel_debug_print_line_start ();
	_panel_debug_print_style ("1;37");
	printf ("\"%s\"", key);
	_panel_debug_print_style ("0;37");
	printf (": ");
	_panel_debug_print_style ("0;36");
	printf ("%p\n", data);
}

static const char *
_panel_debug_print_gdk_window_type_hint_get_name(GdkWindowTypeHint type)
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
_panel_debug_print_gdk_window_state_print_elem(const char * elem, int show, GList *indent)
{
	if (show) {
		_panel_debug_print_window_tree_indent (indent);
		_panel_debug_print_label ("State");
		_panel_debug_print_style ("1;34");
		printf ("%s\n", elem);
	}
}

static void
_panel_debug_print_gdk_window_state_print(GdkWindowState state, GList *indent)
{
	_panel_debug_print_gdk_window_state_print_elem ("WITHDRAWN", state & GDK_WINDOW_STATE_WITHDRAWN, indent);
	_panel_debug_print_gdk_window_state_print_elem ("ICONIFIED", state & GDK_WINDOW_STATE_ICONIFIED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("MAXIMIZED", state & GDK_WINDOW_STATE_MAXIMIZED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("STICKY", state & GDK_WINDOW_STATE_STICKY, indent);
	_panel_debug_print_gdk_window_state_print_elem ("FULLSCREEN", state & GDK_WINDOW_STATE_FULLSCREEN, indent);
	_panel_debug_print_gdk_window_state_print_elem ("ABOVE", state & GDK_WINDOW_STATE_ABOVE, indent);
	_panel_debug_print_gdk_window_state_print_elem ("BELOW", state & GDK_WINDOW_STATE_BELOW, indent);
	_panel_debug_print_gdk_window_state_print_elem ("FOCUSED", state & GDK_WINDOW_STATE_FOCUSED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("TILED", state & GDK_WINDOW_STATE_TILED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("TOP_TILED", state & GDK_WINDOW_STATE_TOP_TILED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("TOP_RESIZABLE", state & GDK_WINDOW_STATE_TOP_RESIZABLE, indent);
	_panel_debug_print_gdk_window_state_print_elem ("RIGHT_TILED", state & GDK_WINDOW_STATE_RIGHT_TILED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("RIGHT_RESIZABLE", state & GDK_WINDOW_STATE_RIGHT_RESIZABLE, indent);
	_panel_debug_print_gdk_window_state_print_elem ("BOTTOM_TILED", state & GDK_WINDOW_STATE_BOTTOM_TILED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("BOTTOM_RESIZABLE", state & GDK_WINDOW_STATE_BOTTOM_RESIZABLE, indent);
	_panel_debug_print_gdk_window_state_print_elem ("LEFT_TILED", state & GDK_WINDOW_STATE_LEFT_TILED, indent);
	_panel_debug_print_gdk_window_state_print_elem ("LEFT_RESIZABLE", state & GDK_WINDOW_STATE_LEFT_RESIZABLE, indent);
}

static void
_panel_debug_print_window_info (GdkWindow *window, GdkWindow *highlight, GList *indent)
{
	GdkWindowTypeHint window_type;
	const char *window_type_name;
	int width, height;
	int has_native;

	if (window == NULL) {
		_panel_debug_print_bool("Valid Window Object", FALSE, TRUE, indent);
		return;
	}

	window_type = gdk_window_get_type_hint (window);
	window_type_name = _panel_debug_print_gdk_window_type_hint_get_name (window_type);
	width = gdk_window_get_width (window);
	height = gdk_window_get_height (window);
	has_native = gdk_window_has_native (window);

	if (has_native) {

		_panel_debug_print_label ("Address");
		_panel_debug_print_style ("0;36");
		printf ("%p\n", window);
		if (window_type != GDK_WINDOW_TYPE_HINT_NORMAL) {

			_panel_debug_print_window_tree_indent (indent);
			_panel_debug_print_label ("Type");
			_panel_debug_print_style ("1;34");
			printf ("%s\n", window_type_name);
		}
		_panel_debug_print_gdk_window_state_print (gdk_window_get_state (window), indent);
		_panel_debug_print_window_tree_indent (indent);
		_panel_debug_print_label ("Size");
		_panel_debug_print_style ("1;34");
		printf ("%d x %d\n", width, height);
		_panel_debug_print_bool("Has Visual", gdk_window_get_visual (window) != NULL, TRUE, indent);
		_panel_debug_print_bool("Focusable", gdk_window_get_accept_focus (window), TRUE, indent);
		_panel_debug_print_bool("Decorations", gdk_window_get_decorations (window, NULL), FALSE, indent);
	} else {
		_panel_debug_print_label ("Internal Window");
		printf("%dx%d %s\n", width, height, window_type_name);
	}

	_panel_debug_print_g_object_data (window, "wayland_popup_data", indent);
	_panel_debug_print_g_object_data (window, "wayland_popup_attach_widget", indent);
	_panel_debug_print_g_object_data (window, "wayland_layer_surface", indent);
	_panel_debug_print_g_object_data (window, "popup_menu_setup_func", indent);
	_panel_debug_print_g_object_data (window, "tooltip_setup_func", indent);
	_panel_debug_print_g_object_data (window, "custom_tooltip_widget_data", indent);
	_panel_debug_print_bool("Special", window == highlight, FALSE, indent);
}

static void
_panel_debug_print_window_tree_branch (GdkWindow *window, GdkWindow *highlight, GList *indent)
{
	GList *list, *elem, *indent_node;

	_panel_debug_print_window_info (window, highlight, indent);

	if (window) {
			list = gdk_window_get_children (window);

			for (elem = list; elem; elem = elem->next) {
				_panel_debug_print_window_tree_indent (indent);
				printf("%s\u2502 ", elem == list ? "\u2594\u2594\u2594" : "   ");
				printf("\u2581\u2581\u2581\u2581\u2581\u2581\u2581\u2581\n");
				_panel_debug_print_window_tree_indent (indent);
				if (elem->next)
					printf ("   \u251c\u2500");
				else
					printf ("   \u2570\u2500");
				indent_node = g_list_append (NULL, elem->next); // all we care about is if its NULL or not
				indent = g_list_concat(indent, indent_node);
				_panel_debug_print_window_tree_branch (elem->data, highlight, indent);
				indent = g_list_remove_link(indent, indent_node);
				g_list_free_1 (indent_node);
			}

			g_list_free (list);
	}

	_panel_debug_print_style ("0");
	fflush (stdout);
}

void
panel_debug_print_window_tree(GdkWindow *current, const char *code_path, int code_line_num)
{
	GdkWindow *root, *next;
	static GdkWindow* cached_root_window = NULL;

	if (current == NULL)
		current = cached_root_window;

	_panel_debug_print_style("1;35");
	if (code_path) {
		printf("%s", code_path);
		_panel_debug_print_style("0");
		printf(":");
		_panel_debug_print_style("1;37");
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

	_panel_debug_print_window_tree_branch (root, current, 0);
	printf ("\n");
}
