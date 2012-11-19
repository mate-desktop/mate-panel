/*
 * panel-ditem-editor.h:
 *
 * Copyright (C) 2004, 2006 Vincent Untz
 *
 * The Mate Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Mate Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#ifndef PANEL_DITEM_EDITOR_H
#define PANEL_DITEM_EDITOR_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_TYPE_DITEM_EDITOR			(panel_ditem_editor_get_type ())
#define PANEL_DITEM_EDITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_DITEM_EDITOR, PanelDItemEditor))
#define PANEL_DITEM_EDITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_DITEM_EDITOR, PanelDItemEditorClass))
#define PANEL_IS_DITEM_EDITOR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_DITEM_EDITOR))
#define PANEL_IS_DITEM_EDITOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_DITEM_EDITOR))
#define PANEL_DITEM_EDITOR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), PANEL_TYPE_DITEM_EDITOR, PanelDItemEditorClass))

typedef struct _PanelDItemEditor      PanelDItemEditor;
typedef struct _PanelDItemEditorClass PanelDItemEditorClass;

typedef struct _PanelDItemEditorPrivate PanelDItemEditorPrivate;

struct _PanelDItemEditorClass
{
	GtkDialogClass parent_class;

	/* File has been saved */
	void (* saved)           (PanelDItemEditor *dialog);

	/* Any information changed */
	void (* changed)         (PanelDItemEditor *dialog);

	/* These more specific signals are provided since they
	 * will likely require a display update */
	/* The name of the item has changed. */
	void (* name_changed)    (PanelDItemEditor *dialog,
				  const char       *name);
	/* The command of the item has changed. */
	void (* command_changed) (PanelDItemEditor *dialog,
				  const char       *command);
	/* The comment of the item has changed. */
	void (* comment_changed) (PanelDItemEditor *dialog,
				  const char       *comment);
	/* The icon in particular has changed. */
	void (* icon_changed)    (PanelDItemEditor *dialog,
				  const char       *icon);

	/* An error is reported. */
	void (* error_reported)  (PanelDItemEditor *dialog,
				  const char       *error);
};

struct _PanelDItemEditor
{
	GtkDialog parent_instance;

	PanelDItemEditorPrivate *priv;
};

typedef char * (*PanelDitemSaveUri) (PanelDItemEditor *dialog, gpointer data);

GType      panel_ditem_editor_get_type (void);

GtkWidget *panel_ditem_editor_new (GtkWindow   *parent,
				   GKeyFile    *key_file,
				   const char  *uri,
				   const char  *title);

GtkWidget *panel_ditem_editor_new_directory (GtkWindow   *parent,
					     GKeyFile    *key_file,
					     const char  *uri,
					     const char  *title);

void panel_ditem_editor_sync_display (PanelDItemEditor *dialog);

GKeyFile *panel_ditem_editor_get_key_file        (PanelDItemEditor *dialog);
GKeyFile *panel_ditem_editor_get_revert_key_file (PanelDItemEditor *dialog);

void panel_ditem_editor_set_uri (PanelDItemEditor *dialog,
				 const char       *uri);

const char* panel_ditem_editor_get_uri(PanelDItemEditor* dialog);

void panel_ditem_register_save_uri_func (PanelDItemEditor  *dialog,
					 PanelDitemSaveUri  save_uri,
					 gpointer           data);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_DITEM_EDITOR_H */
