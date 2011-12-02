#ifndef NOTHING_H
#define NOTHING_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

void         start_screen_check      (void);
void	     start_geginv            (void);
gboolean     panel_dialog_window_event (GtkWidget *window,
					GdkEvent  *event);
int          config_event              (GtkWidget *widget,
					GdkEvent  *event,
					GtkNotebook *nbook);

#ifdef __cplusplus
}
#endif

#endif
