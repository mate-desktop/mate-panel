/*
 * Copyright (C) 2017 Colomban Wendling <cwendling@hypra.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SN_FLAT_BUTTON_H
#define SN_FLAT_BUTTON_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_FLAT_BUTTON            (sn_flat_button_get_type ())
#define SN_FLAT_BUTTON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SN_TYPE_FLAT_BUTTON, SnFlatButton))
#define SN_FLAT_BUTTON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SN_TYPE_FLAT_BUTTON, SnFlatButtonClass))
#define SN_IS_FLAT_BUTTON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SN_TYPE_FLAT_BUTTON))
#define SN_IS_FLAT_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SN_TYPE_FLAT_BUTTON))
#define SN_FLAT_BUTTON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SN_TYPE_FLAT_BUTTON, SnFlatButtonClass))

typedef struct _SnFlatButton        SnFlatButton;
typedef struct _SnFlatButtonPrivate SnFlatButtonPrivate;
typedef struct _SnFlatButtonClass   SnFlatButtonClass;

struct _SnFlatButton
{
  GtkButton parent_instance;

  SnFlatButtonPrivate *priv;
};

struct _SnFlatButtonClass
{
  GtkButtonClass parent_class;
};

GType sn_flat_button_get_type (void);
GtkWidget *sn_flat_button_new (void);

#if 0
#ifndef SN_COMPAT_BUTTON_NODRAW_C
/* replace GtkButton */
# define gtk_button_get_type sn_compat_button_nodraw_get_type
#endif
#endif

G_END_DECLS

#endif
