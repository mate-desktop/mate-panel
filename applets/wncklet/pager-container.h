/*
 * Copyright (C) 2022 Alberts Muktupāvels
 * Copyright (C) 2023 Colomban Wendling
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

#ifndef H_PAGER_CONTAINER
#define H_PAGER_CONTAINER

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PAGER_CONTAINER_TYPE (pager_container_get_type ())
#define PAGER_CONTAINER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PAGER_CONTAINER_TYPE, PagerContainer))

typedef struct _PagerContainer PagerContainer;
typedef GtkBinClass PagerContainerClass;

struct _PagerContainer
{
	GtkBin          parent;
	GtkOrientation  orientation;
	int             size;
};

GType       pager_container_get_type        (void);
GtkWidget  *pager_container_new             (GtkWidget     *child,
                                             GtkOrientation orientation);
void        pager_container_set_orientation (PagerContainer *self,
                                             GtkOrientation orientation);

G_END_DECLS

#endif /* guard */
