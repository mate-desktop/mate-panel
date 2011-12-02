/*
 * panel-reset.c
 *
 * Copyright (C) 2010 Perberos <perberos@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANEL_RESET_C__
#define __PANEL_RESET_C__

#include <stdlib.h>
#include "panel-reset.h"

void panel_reset()
{
	/* En teoria, al hacer `mate-panel --reset` se podria correr este comando
	 * para que se reestablesca la configuracion por defecto del panel. O se
	 * borre para que pueda elegir una nueva. Esto ultimo solo si se desarrolla
	 * el dialogo de seleccion de configuracion inicial.
	 *
	 * La configuracion no se borra a travez de los archivos, por que hacer esto
	 * no hace que el demonio de configuracion se actualice. Obligando que se
	 * deba cerrar sesion antes de volver a abrir el panel.
	 * Es por eso que se eliminan las entradas a travez de mate-conf.
	 */
	system("mateconftool-2 --recursive-unset /apps/panel"); // unix like

	/* TODO: send a dbus message to mate-panel, if active, to reload the panel
	 * configuration */
}

#endif /* !__PANEL_RESET_C__ */
