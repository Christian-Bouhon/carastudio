/*
 * CaraStudio — styles nommés (presets). Voir rs-style.h.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 */

#include <config.h>
#include <rawstudio.h>
#include <glib/gstdio.h>
#include <libxml/parser.h>
#include <libxml/xmlwriter.h>
#include <stdlib.h>
#include <string.h>
#include "rs-photo.h"   /* RS_PHOTO, requis par rs-cache.h */
#include "rs-style.h"
#include "rs-cache.h"

#define STYLE_EXT ".carastyle"
#define STYLE_FORMAT_VERSION 1

gchar *
rs_style_get_dir(void)
{
	gchar *dir = g_build_filename(g_get_user_config_dir(), "carastudio", "styles", NULL);
	g_mkdir_with_parents(dir, 0755);
	return dir;
}

/* Convertit un nom affiché en nom de fichier sûr (seul '/' pose problème sous
 * Linux ; accents et espaces sont acceptés tels quels). */
static gchar *
style_sanitize(const gchar *name)
{
	gchar *s = g_strdup(name);
	gchar *p;
	g_strstrip(s);
	for (p = s; *p; p++)
		if (*p == G_DIR_SEPARATOR || *p == '/')
			*p = '-';
	return s;
}

static gchar *
style_path_for(const gchar *name)
{
	gchar *dir  = rs_style_get_dir();
	gchar *safe = style_sanitize(name);
	gchar *file = g_strconcat(safe, STYLE_EXT, NULL);
	gchar *path = g_build_filename(dir, file, NULL);
	g_free(dir); g_free(safe); g_free(file);
	return path;
}

static gint
style_name_cmp(gconstpointer a, gconstpointer b)
{
	return g_ascii_strcasecmp((const gchar *) a, (const gchar *) b);
}

gboolean
rs_style_exists(const gchar *name)
{
	if (!name || !*name)
		return FALSE;
	gchar *path = style_path_for(name);
	gboolean ex = g_file_test(path, G_FILE_TEST_IS_REGULAR);
	g_free(path);
	return ex;
}

gboolean
rs_style_save(const gchar *name, RSSettings *settings, RSStyleGroups groups)
{
	g_return_val_if_fail(name && *name, FALSE);
	g_return_val_if_fail(RS_IS_SETTINGS(settings), FALSE);

	gchar *path = style_path_for(name);
	xmlTextWriterPtr writer = xmlNewTextWriterFilename(path, 0);
	if (!writer)
	{
		g_free(path);
		return FALSE;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
	xmlTextWriterStartElement(writer, BAD_CAST "carastudio-style");
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "version", "%d", STYLE_FORMAT_VERSION);
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "cacheversion", "%d", CACHEVERSION);
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "name", "%s", name);
	xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "groups", "%" G_GUINT64_FORMAT, (guint64) groups);

	/* Snapshot COMPLET des réglages (MASK_ALL). Le profil DCP n'y figure pas :
	 * il vit sur la photo, pas dans RSSettings — un style ne le transporte donc
	 * jamais. */
	xmlTextWriterStartElement(writer, BAD_CAST "settings");
	rs_cache_save_settings(settings, MASK_ALL, writer);
	xmlTextWriterEndElement(writer); /* settings */

	xmlTextWriterEndElement(writer); /* carastudio-style */
	int ret = xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);
	g_free(path);
	return (ret >= 0);
}

gboolean
rs_style_load(const gchar *name, RSSettings *out_settings, RSStyleGroups *out_groups)
{
	g_return_val_if_fail(name && *name, FALSE);
	g_return_val_if_fail(RS_IS_SETTINGS(out_settings), FALSE);

	gchar *path = style_path_for(name);
	xmlDocPtr doc = xmlParseFile(path);
	g_free(path);
	if (!doc)
		return FALSE;

	xmlNodePtr root = xmlDocGetRootElement(doc);
	if (!root || xmlStrcmp(root->name, BAD_CAST "carastudio-style"))
	{
		xmlFreeDoc(doc);
		return FALSE;
	}

	gint cacheversion = CACHEVERSION;
	xmlChar *cv = xmlGetProp(root, BAD_CAST "cacheversion");
	if (cv) { cacheversion = atoi((gchar *) cv); xmlFree(cv); }

	if (out_groups)
	{
		*out_groups = STYLE_ALL;
		xmlChar *g = xmlGetProp(root, BAD_CAST "groups");
		if (g) { *out_groups = (RSStyleGroups) g_ascii_strtoull((gchar *) g, NULL, 10); xmlFree(g); }
	}

	gboolean found = FALSE;
	xmlNodePtr cur = root->xmlChildrenNode;
	while (cur)
	{
		if (!xmlStrcmp(cur->name, BAD_CAST "settings"))
		{
			rs_cache_load_setting(out_settings, doc, cur->xmlChildrenNode, cacheversion);
			found = TRUE;
			break;
		}
		cur = cur->next;
	}
	xmlFreeDoc(doc);
	return found;
}

gboolean
rs_style_delete(const gchar *name)
{
	g_return_val_if_fail(name && *name, FALSE);
	gchar *path = style_path_for(name);
	gboolean ok = (g_unlink(path) == 0);
	g_free(path);
	return ok;
}

gboolean
rs_style_rename(const gchar *oldname, const gchar *newname)
{
	g_return_val_if_fail(oldname && *oldname, FALSE);
	g_return_val_if_fail(newname && *newname, FALSE);
	if (rs_style_exists(newname))
		return FALSE;

	/* Recharge → réenregistre sous le nouveau nom (met à jour l'attribut name)
	 * → supprime l'ancien. Plus robuste qu'un simple g_rename du fichier. */
	RSSettings *s = rs_settings_new();
	RSStyleGroups groups = STYLE_ALL;
	gboolean ok = rs_style_load(oldname, s, &groups);
	if (ok) ok = rs_style_save(newname, s, groups);
	if (ok) rs_style_delete(oldname);
	g_object_unref(s);
	return ok;
}

GList *
rs_style_list(void)
{
	GList *names = NULL;
	gchar *dir = rs_style_get_dir();
	GDir *d = g_dir_open(dir, 0, NULL);
	if (d)
	{
		const gchar *entry;
		while ((entry = g_dir_read_name(d)))
		{
			if (!g_str_has_suffix(entry, STYLE_EXT))
				continue;

			gchar *path = g_build_filename(dir, entry, NULL);
			/* Nom affiché = attribut "name" du fichier ; repli = nom de fichier
			 * sans l'extension. */
			gchar *display = NULL;
			xmlDocPtr doc = xmlParseFile(path);
			if (doc)
			{
				xmlNodePtr root = xmlDocGetRootElement(doc);
				if (root && !xmlStrcmp(root->name, BAD_CAST "carastudio-style"))
				{
					xmlChar *n = xmlGetProp(root, BAD_CAST "name");
					if (n) { display = g_strdup((gchar *) n); xmlFree(n); }
				}
				xmlFreeDoc(doc);
			}
			if (!display)
				display = g_strndup(entry, strlen(entry) - strlen(STYLE_EXT));

			names = g_list_prepend(names, display);
			g_free(path);
		}
		g_dir_close(d);
	}
	g_free(dir);
	return g_list_sort(names, style_name_cmp);
}
