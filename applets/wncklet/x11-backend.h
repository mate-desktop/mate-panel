/* Wncklet applet X11 backend */

#ifndef _WNCKLET_APPLET_X11_BACKEND_H_
#define _WNCKLET_APPLET_X11_BACKEND_H_

#include "tasklist-backend.h"

G_BEGIN_DECLS

/* Returns NULL unless the default GdkDisplay is an X11 one, so that the
 * caller can pick a backend by display type rather than at compile time. */
const TasklistBackend *x11_tasklist_backend (void);

G_END_DECLS

#endif /* _WNCKLET_APPLET_X11_BACKEND_H_ */
