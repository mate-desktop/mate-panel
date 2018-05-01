#ifndef PANEL_UTIL_H
#define PANEL_UTIL_H

#include <gio/gio.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define		sure_string(s)		((const char *)((s)!=NULL?(s):""))

char *          panel_util_make_exec_uri_for_desktop (const char *exec);

int		panel_find_applet_index	(GtkWidget *widget);

void		panel_push_window_busy	(GtkWidget *window);
void		panel_pop_window_busy	(GtkWidget *window);

gboolean	panel_is_program_in_path (const char *program);

gboolean	panel_is_uri_writable	(const char *uri);
gboolean	panel_uri_exists	(const char *uri);

void            panel_lock_screen                  (GdkScreen    *screen);
void            panel_lock_screen_action           (GdkScreen    *screen,
                                                    const char   *action);
gboolean        panel_lock_screen_action_available (const char   *action);

GIcon *         panel_gicon_from_icon_name (const char *icon_name);
char *          panel_find_icon         (GtkIconTheme  *icon_theme,
					 const char    *icon_name,
					 int            size);
cairo_surface_t *panel_load_icon        (GtkIconTheme  *icon_theme,
					 const char    *icon_name,
					 int            size,
					 int            desired_width,
					 int            desired_height,
					 char         **error_msg);

GFile      *panel_launcher_get_gfile           (const char *location);
char       *panel_launcher_get_uri             (const char *location);
char       *panel_launcher_get_filename        (const char *location);
gboolean    panel_launcher_is_in_personal_path (const char *location);

char *panel_make_full_path   (const char *dir,
			      const char *filename);
char *panel_make_unique_desktop_path_from_name (const char *dir,
						const char *name);
char *panel_make_unique_desktop_uri (const char *dir,
				     const char *source);

GdkPixbuf *panel_util_cairo_rgbdata_to_pixbuf (unsigned char *data,
					       int            width,
					       int            height);

char *guess_icon_from_exec (GtkIconTheme *icon_theme,
			    const gchar  *exec);

const char *panel_util_get_vfs_method_display_name (const char *method);
char *panel_util_get_label_for_uri (const char *text_uri);
char *panel_util_get_icon_for_uri (const char *text_uri);

void panel_util_set_tooltip_text (GtkWidget  *widget,
				  const char *text);

GFile *panel_util_get_file_optional_homedir (const char *location);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_UTIL_H */
