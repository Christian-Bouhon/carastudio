/*
 * Copyright (C) 2006-2011 Anders Brander, Anders Kvist and Klaus Post
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * GConf backend replaced by GKeyFile (CaraStudio, 2026).
 * Same public API as the original conf_interface, settings now
 * stored in ~/.config/rawstudio/settings.conf
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include "conf_interface.h"

#define CONF_GROUP "rawstudio"
static GMutex lock;
static GKeyFile *keyfile = NULL;
static gchar *keyfile_path = NULL;

/* Lazy-load the keyfile on first use */
static void conf_ensure_loaded(void)
{
	if (keyfile)
		return;
	keyfile = g_key_file_new();
	keyfile_path = g_build_filename(g_get_user_config_dir(), "rawstudio", "settings.conf", NULL);
	gchar *dir = g_path_get_dirname(keyfile_path);
	g_mkdir_with_parents(dir, 0755);
	g_free(dir);
	if (g_file_test(keyfile_path, G_FILE_TEST_EXISTS))
		g_key_file_load_from_file(keyfile, keyfile_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
}

/* Persist the keyfile to disk */
static void conf_save(void)
{
	if (!keyfile || !keyfile_path)
		return;
	gchar *data = g_key_file_to_data(keyfile, NULL, NULL);
	if (data)
	{
		g_file_set_contents(keyfile_path, data, -1, NULL);
		g_free(data);
	}
}

gboolean
rs_conf_get_boolean(const gchar *name, gboolean *boolean_value)
{
	gboolean ret = FALSE;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	if (g_key_file_has_key(keyfile, CONF_GROUP, name, NULL))
	{
		if (boolean_value)
			*boolean_value = g_key_file_get_boolean(keyfile, CONF_GROUP, name, NULL);
		ret = TRUE;
	}
	g_mutex_unlock(&lock);
	return(ret);
}

gboolean
rs_conf_get_boolean_with_default(const gchar *name, gboolean *boolean_value, gboolean default_value)
{
	gboolean ret = FALSE;
	if (boolean_value)
		*boolean_value = default_value;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	if (g_key_file_has_key(keyfile, CONF_GROUP, name, NULL))
	{
		if (boolean_value)
			*boolean_value = g_key_file_get_boolean(keyfile, CONF_GROUP, name, NULL);
		ret = TRUE;
	}
	g_mutex_unlock(&lock);
	return(ret);
}

gboolean
rs_conf_set_boolean(const gchar *name, gboolean bool_value)
{
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_set_boolean(keyfile, CONF_GROUP, name, bool_value);
	conf_save();
	g_mutex_unlock(&lock);
	return(TRUE);
}

gchar *
rs_conf_get_string(const gchar *name)
{
	gchar *ret = NULL;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	if (g_key_file_has_key(keyfile, CONF_GROUP, name, NULL))
		ret = g_key_file_get_string(keyfile, CONF_GROUP, name, NULL);
	g_mutex_unlock(&lock);
	return(ret);
}

gboolean
rs_conf_set_string(const gchar *name, const gchar *string_value)
{
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_set_string(keyfile, CONF_GROUP, name, string_value ? string_value : "");
	conf_save();
	g_mutex_unlock(&lock);
	return(TRUE);
}

gboolean
rs_conf_get_integer(const gchar *name, gint *integer_value)
{
	gboolean ret = FALSE;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	if (g_key_file_has_key(keyfile, CONF_GROUP, name, NULL))
	{
		if (integer_value)
			*integer_value = g_key_file_get_integer(keyfile, CONF_GROUP, name, NULL);
		ret = TRUE;
	}
	g_mutex_unlock(&lock);
	return(ret);
}

gboolean
rs_conf_set_integer(const gchar *name, const gint integer_value)
{
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_set_integer(keyfile, CONF_GROUP, name, integer_value);
	conf_save();
	g_mutex_unlock(&lock);
	return(TRUE);
}

gboolean
rs_conf_get_color(const gchar *name, GdkColor *color)
{
	gchar *str;
	gboolean ret = FALSE;
	str = rs_conf_get_string(name);
	if (str)
	{
		ret = gdk_color_parse(str, color);
		g_free(str);
	}
	return(ret);
}

gboolean
rs_conf_set_color(const gchar *name, GdkColor *color)
{
	gchar *str;
	gboolean ret = FALSE;
	str = g_strdup_printf("#%02x%02x%02x", color->red>>8, color->green>>8, color->blue>>8);
	if (str)
	{
		ret = rs_conf_set_string(name, str);
		g_free(str);
	}
	return(ret);
}

gboolean
rs_conf_get_double(const gchar *name, gdouble *float_value)
{
	gboolean ret = FALSE;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	if (g_key_file_has_key(keyfile, CONF_GROUP, name, NULL))
	{
		if (float_value)
			*float_value = g_key_file_get_double(keyfile, CONF_GROUP, name, NULL);
		ret = TRUE;
	}
	g_mutex_unlock(&lock);
	return(ret);
}

gboolean
rs_conf_set_double(const gchar *name, const gdouble float_value)
{
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_set_double(keyfile, CONF_GROUP, name, float_value);
	conf_save();
	g_mutex_unlock(&lock);
	return(TRUE);
}

GSList *
rs_conf_get_list_string(const gchar *name)
{
	GSList *list = NULL;
	gchar **array;
	gsize length, i;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	array = g_key_file_get_string_list(keyfile, CONF_GROUP, name, &length, NULL);
	if (array)
	{
		for (i = 0; i < length; i++)
			list = g_slist_append(list, g_strdup(array[i]));
		g_strfreev(array);
	}
	g_mutex_unlock(&lock);
	return(list);
}

gboolean
rs_conf_set_list_string(const gchar *name, GSList *list)
{
	guint length = g_slist_length(list);
	const gchar **array = g_new0(const gchar *, length + 1);
	guint i = 0;
	GSList *l;
	for (l = list; l != NULL; l = l->next)
		array[i++] = (const gchar *)l->data;
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_set_string_list(keyfile, CONF_GROUP, name, array, length);
	conf_save();
	g_mutex_unlock(&lock);
	g_free(array);
	return(TRUE);
}

gboolean
rs_conf_add_string_to_list_string(const gchar *name, gchar *value)
{
	gboolean ret;
	GSList *oldlist = rs_conf_get_list_string(name);
	GSList *newlist = g_slist_append(oldlist, g_strdup(value));
	ret = rs_conf_set_list_string(name, newlist);
	g_slist_free_full(newlist, g_free);
	return(ret);
}

gchar *
rs_conf_get_nth_string_from_list_string(const gchar *name, gint num)
{
	gchar *ret = NULL;
	GSList *list = rs_conf_get_list_string(name);
	if (list)
	{
		gpointer data = g_slist_nth_data(list, num);
		if (data)
			ret = g_strdup((gchar *)data);
		g_slist_free_full(list, g_free);
	}
	return(ret);
}

gboolean
rs_conf_unset(const gchar *name)
{
	g_mutex_lock(&lock);
	conf_ensure_loaded();
	g_key_file_remove_key(keyfile, CONF_GROUP, name, NULL);
	conf_save();
	g_mutex_unlock(&lock);
	return(TRUE);
}
