/*
 * MATE panel launcher module.
 * (C) 1997 The Free Software Foundation
 *
 * Authors: Miguel de Icaza
 *          Federico Mena
 * CORBAized by George Lebl
 * de-CORBAized by George Lebl
 */

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "applet.h"
#include "panel-widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	AppletInfo        *info;
	GtkWidget         *button;

	char              *location;
	GKeyFile          *key_file;

	GtkWidget         *prop_dialog;
	GSList            *error_dialogs;

	gulong             destroy_handler;
} Launcher;

void panel_launcher_create           (PanelToplevel *toplevel,
				      int            position,
				      const char    *location);
void panel_launcher_create_with_id   (const char    *toplevel_id,
				      int            position,
				      const char    *location);
gboolean panel_launcher_create_copy      (PanelToplevel *toplevel,
					  int            position,
					  const char    *location);
void panel_launcher_create_from_info (PanelToplevel *toplevel,
				      int            position,
				      gboolean       exec_info,
				      const char    *exec_or_uri,
				      const char    *name,
				      const char    *comment,
				      const char    *icon);

void		launcher_launch			(Launcher  *launcher,
						 const gchar *action);

void		launcher_properties		(Launcher  *launcher);

void            launcher_load_from_gsettings        (PanelWidget *panel_widget,
													 gboolean     locked,
													 gint         position,
													 const char  *id);

void            panel_launcher_delete           (Launcher *launcher);

void		ask_about_launcher		(const char *file,
						 PanelWidget *panel,
						 int pos,
						 gboolean exactpos);

Launcher *	find_launcher			(const char *path);

void            launcher_properties_destroy     (Launcher *launcher);

void            panel_launcher_set_dnd_enabled  (Launcher *launcher,
						 gboolean  dnd_enabled);


#ifdef __cplusplus
}
#endif

#endif
