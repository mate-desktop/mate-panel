/*
 * panel-keyfile.c: GKeyFile extensions
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Based on code from panel-util.c (there was no copyright header at the time)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <string.h>
#include <sys/stat.h>

#include <glib.h>
#include <gio/gio.h>

#include "panel-keyfile.h"

#define KEYFILE_TRUSTED_SHEBANG "#!/usr/bin/env xdg-open\n"

GKeyFile *
panel_key_file_new_desktop (void)
{
	GKeyFile *retval;

	retval = g_key_file_new ();

	//FIXME? g_key_file_set_string (retval, G_KEY_FILE_DESKTOP_GROUP, "Name", _("No Name"));
	g_key_file_set_string (retval, G_KEY_FILE_DESKTOP_GROUP, "Version", "1.0");

	return retval;
}

static void
_panel_key_file_make_executable (const gchar *path)
{
	GFile     *file;
	GFileInfo *info;
	guint32    current_perms;
	guint32    new_perms;

	file = g_file_new_for_path (path);

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_TYPE","
				  G_FILE_ATTRIBUTE_UNIX_MODE,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  NULL,
				  NULL);

	if (info == NULL) {
		g_warning ("Cannot mark %s executable", path);
		g_object_unref (file);
		return;
	}

	if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_UNIX_MODE)) {
		current_perms = g_file_info_get_attribute_uint32 (info,
								  G_FILE_ATTRIBUTE_UNIX_MODE);
		new_perms = current_perms | S_IXGRP | S_IXUSR | S_IXOTH;
		if ((current_perms != new_perms) &&
		    !g_file_set_attribute_uint32 (file,
			    			  G_FILE_ATTRIBUTE_UNIX_MODE,
						  new_perms,
						  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						  NULL, NULL))
			g_warning ("Cannot mark %s executable", path);
	}

	g_object_unref (info);
	g_object_unref (file);
}

//FIXME: kill this when bug #309224 is fixed
gboolean
panel_key_file_to_file (GKeyFile     *keyfile,
			const gchar  *file,
			GError      **error)
{
	gchar   *filename;
	GError  *write_error;
	gchar   *data;
	gsize    length;
	gboolean res;

	g_return_val_if_fail (keyfile != NULL, FALSE);
	g_return_val_if_fail (file != NULL, FALSE);

	write_error = NULL;
	data = g_key_file_to_data (keyfile, &length, &write_error);
	if (write_error) {
		g_propagate_error (error, write_error);
		return FALSE;
	}

	if (!g_path_is_absolute (file))
		filename = g_filename_from_uri (file, NULL, &write_error);
	else
		filename = g_filename_from_utf8 (file, -1, NULL, NULL,
						 &write_error);

	if (write_error) {
		g_propagate_error (error, write_error);
		g_free (data);
		return FALSE;
	}

	if (!g_str_has_prefix (data, "#!")) {
		gchar *new_data;
		gsize  new_length;

		new_length = length + strlen (KEYFILE_TRUSTED_SHEBANG);
		new_data = g_malloc (new_length + 1);

		g_strlcpy (new_data, KEYFILE_TRUSTED_SHEBANG, new_length + 1);
		g_strlcat (new_data, data, new_length + 1);

		g_free (data);
		data = new_data;
		length = new_length;
	}

	res = g_file_set_contents (filename, data, length, &write_error);

	if (write_error) {
		g_propagate_error (error, write_error);
		g_free (data);
		g_free (filename);
		return FALSE;
	}

	g_free (data);

	_panel_key_file_make_executable (filename);
	g_free (filename);

	return res;
}

gboolean
panel_key_file_load_from_uri (GKeyFile       *keyfile,
			      const gchar    *uri,
			      GKeyFileFlags   flags,
			      GError        **error)
{
	char     *scheme;
	gboolean  is_local;
	gboolean  result;

	g_return_val_if_fail (keyfile != NULL, FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	scheme = g_uri_parse_scheme (uri);
	is_local = (scheme == NULL) || !g_ascii_strcasecmp (scheme, "file");
	g_free (scheme);

	if (is_local) {
		char *path;

		if (g_path_is_absolute (uri))
			path = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
		else
			path = g_filename_from_uri (uri, NULL, NULL);
		result = g_key_file_load_from_file (keyfile, path,
						    flags, error);
		g_free (path);
	} else {
		GFile   *file;
		char	*contents;
		gsize    size;
		gboolean ret;

		file = g_file_new_for_uri (uri);
		ret = g_file_load_contents (file, NULL, &contents, &size,
					    NULL, NULL);
		g_object_unref (file);

		if (!ret)
			return FALSE;

		result = g_key_file_load_from_data (keyfile, contents, size,
						    flags, error);

		g_free (contents);
	}

	return result;
}

gboolean
panel_key_file_copy_and_mark_trusted (const char  *source_path,
				      const char  *target_path,
				      GError     **error)
{
	GKeyFile *key_file;
	gboolean  res = FALSE;

	key_file = g_key_file_new ();
	res = g_key_file_load_from_file (key_file, source_path,
					 G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
					 error);
	if (!res) {
		g_key_file_free (key_file);
		return FALSE;
	}

	res = panel_key_file_to_file (key_file, target_path, error);

	g_key_file_free (key_file);

	return res;
}

gboolean
panel_key_file_get_boolean (GKeyFile    *keyfile,
			    const gchar *key,
			    gboolean     default_value)
{
	GError   *error;
	gboolean  retval;

	error = NULL;
	retval = g_key_file_get_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP, key, &error);
	if (error != NULL) {
		retval = default_value;
		g_error_free (error);
	}

	return retval;
}

void
panel_key_file_set_locale_string (GKeyFile    *keyfile,
				  const gchar *key,
				  const gchar *value)
{
	const char         *locale;
	const char * const *langs_pointer;
	int                 i;

	locale = NULL;
	langs_pointer = g_get_language_names ();
	for (i = 0; langs_pointer[i] != NULL; i++) {
		/* find first without encoding  */
		if (strchr (langs_pointer[i], '.') == NULL) {
			locale = langs_pointer[i];
			break;
		}
	}

	if (locale)
		g_key_file_set_locale_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
					      key, locale, value);
	else
		g_key_file_set_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
				       key, value);
}

void
panel_key_file_remove_locale_key (GKeyFile    *keyfile,
				  const gchar *key)
{
	const char * const *langs_pointer;
	int                 i;
	char               *locale_key;

	locale_key = NULL;
	langs_pointer = g_get_language_names ();
	for (i = 0; langs_pointer[i] != NULL; i++) {
		/* find first without encoding  */
		if (strchr (langs_pointer[i], '.') == NULL) {
			locale_key = g_strdup_printf ("%s[%s]",
						      key, langs_pointer[i]);
			if (g_key_file_has_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
						locale_key, NULL))
				break;

			g_free (locale_key);
			locale_key = NULL;
		}
	}

	if (locale_key) {
		g_key_file_remove_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
				       locale_key, NULL);
		g_free (locale_key);
	} else
		g_key_file_remove_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
				       key, NULL);
}

void
panel_key_file_remove_all_locale_key (GKeyFile    *keyfile,
				      const gchar *key)
{
	char **keys;
	int    key_len;
	int    i;

	if (!key)
		return;

	keys = g_key_file_get_keys (keyfile, G_KEY_FILE_DESKTOP_GROUP, NULL, NULL);
	if (!keys)
		return;

	key_len = strlen (key);

	for (i = 0; keys[i] != NULL; i++) {
		int len;

		if (strncmp (keys[i], key, key_len))
			continue;

		len = strlen (keys[i]);
		if (len == key_len ||
		    (len > key_len && keys[i][key_len] == '['))
			g_key_file_remove_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
					       keys[i], NULL);
	}

	g_strfreev (keys);
}

void
panel_key_file_ensure_C_key (GKeyFile   *keyfile,
			     const char *key)
{
	char *C_value;
	char *buffer;

	/* Make sure we set the "C" locale strings to the terms we set here.
	 * This is so that if the user logs into another locale they get their
	 * own description there rather then empty. It is not the C locale
	 * however, but the user created this entry herself so it's OK */
	C_value = panel_key_file_get_string (keyfile, key);
	if (C_value == NULL || C_value [0] == '\0') {
		buffer = panel_key_file_get_locale_string (keyfile, key);
		if (buffer) {
			panel_key_file_set_string (keyfile, key, buffer);
			g_free (buffer);
		}
	}
	g_free (C_value);
}
