#ifndef __PANEL_DEBUG_PRINT_H__
#define __PANEL_DEBUG_PRINT_H__

#include <gtk/gtk.h>
#include <gdk/gdk.h>

void panel_debug_print_window_tree (GdkWindow *current, const char *code_path, int code_line_num);
#define PANEL_DEBUG_PRINT_WINDOW_TREE (window) panel_debug_print_window_tree

#endif /* __PANEL_DEBUG_PRINT_H__ */
