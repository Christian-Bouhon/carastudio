/*
 * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>,
 * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "application.h"
#include "rs-photo.h"


static gboolean rs_has_gimp(gint major, gint minor, gint micro);

gboolean
rs_external_editor_gimp(RS_PHOTO *photo, RSFilter *prior_to_resample, guint snapshot)
{
	RSOutput *output = NULL;
	g_assert(RS_IS_PHOTO(photo));

	// We need at least GIMP 2.4.0 to export photo
	if (!rs_has_gimp(2,4,0)) {
		return FALSE;
	}

	GString *filename;

	gchar* org_name = g_path_get_basename(photo->filename);
	gchar* org_name_noext = g_utf8_strchr(org_name, -1, '.');

	/* Terminate string there */
	if (NULL != org_name_noext)
		org_name_noext[0] = 0;

	filename = g_string_new("");
        g_string_printf(filename, "%s/%s-rawstudio_%.0f.png",g_get_tmp_dir(), org_name, g_random_double()*10000);

	g_free(org_name);

	/* Chaîne de rendu COMPLÈTE, identique à l'export normal (rs_photo_save).
	   L'ancienne chaîne sautait RSCrop, RSResample et RSEffects → ni l'orientation,
	   ni le recadrage, ni les effets CaraStudio (courbes, argentico…) n'étaient
	   appliqués : une photo portrait partait en paysage dans GIMP. RSCrop applique
	   l'orientation/le recadrage via rs_photo_apply_to_filters. */
	RSFilter *ftransform_input = rs_filter_new("RSColorspaceTransform", prior_to_resample);
	RSFilter *fdcp = rs_filter_new("RSDcp", ftransform_input);
	RSFilter *frotate = rs_filter_new("RSRotate", fdcp);       /* orientation + angle */
	RSFilter *fcrop = rs_filter_new("RSCrop", frotate);        /* recadrage (après rotation) */
	RSFilter *fresample = rs_filter_new("RSResample", fcrop);
	RSFilter *fdenoise = rs_filter_new("RSDenoise", fresample);
	RSFilter *feffects = rs_filter_new("RSEffects", fdenoise);
	RSFilter *ftransform_display = rs_filter_new("RSColorspaceTransform", feffects);
	RSFilter *fend = ftransform_display;

	GList *filters = g_list_append(NULL, fend);
	rs_photo_apply_to_filters(photo, filters, snapshot);
	g_list_free(filters);

	output = rs_output_new("RSPngfile");
	g_object_set(output, "filename", filename->str, NULL);
	g_object_set(output, "save16bit", FALSE, NULL);
	g_object_set(output, "copy-metadata", TRUE, NULL);
	rs_output_execute(output, fend);
	g_object_unref(output);
	g_object_unref(ftransform_input);
	g_object_unref(fdcp);
	g_object_unref(frotate);
	g_object_unref(fcrop);
	g_object_unref(fresample);
	g_object_unref(fdenoise);
	g_object_unref(feffects);
	g_object_unref(ftransform_display);

	/* Ouvre le PNG rendu dans GIMP en le passant en ARGUMENT — méthode robuste,
	   valable pour toutes les versions de GIMP (2.x comme 3.x). L'ancien mécanisme
	   passait par D-Bus (org.gimp.GIMP.UI / OpenAsNew), interface qui a changé : GIMP
	   s'ouvrait alors SANS la photo. Avec « gimp <fichier> », si GIMP tourne déjà il
	   ouvre le fichier dans l'instance existante ; sinon il démarre et l'ouvre.
	   Lancement ASYNCHRONE (on ne bloque pas l'interface). Le fichier temporaire est
	   laissé dans /tmp — GIMP le lit ; le système nettoie /tmp. */
	gchar *argv[] = { "gimp", filename->str, NULL };
	GError *error = NULL;
	gboolean launched = g_spawn_async(NULL, argv, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL, NULL, NULL, &error);

	if (!launched)
	{
		g_warning("Impossible de lancer GIMP : %s", error ? error->message : "erreur inconnue");
		g_clear_error(&error);
		g_unlink(filename->str);
		g_string_free(filename, TRUE);
		return FALSE;
	}

	g_string_free(filename, TRUE);
	return TRUE;
}

static gboolean
rs_has_gimp(gint major, gint minor, gint micro) {
	FILE *fp;
	char line[128];
	int _major, _minor, _micro;
	gboolean retval = FALSE;

	fp = popen("gimp -v","r");
	if (fgets( line, sizeof line, fp) == NULL)
	{
		g_warning("fgets returned: %d\n", retval);
		return FALSE;
	}
	pclose(fp);

	GRegex *regex;
	gchar **tokens;
	
	regex = g_regex_new(".*([0-9])\x2E([0-9]+)\x2E([0-9]+).*", 0, 0, NULL);
	tokens = g_regex_split(regex, line, 0);
	g_regex_unref(regex);

	if (tokens[1])
		_major = atoi(tokens[1]);
	else
	{
		g_strfreev(tokens);
		return FALSE;
	}

	if (_major > major) {
		retval = TRUE;
	} else if (_major == major) {

		if (tokens[2])
			_minor = atoi(tokens[2]);
		else
		{
			g_strfreev(tokens);
			return FALSE;
		}

		if (_minor > minor) {
			retval = TRUE;
		} else if (_minor == minor) {
	
			if (tokens[3])
				_micro = atoi(tokens[3]);
			else
			{
				g_strfreev(tokens);
				return FALSE;
			}

			if (_micro >= micro) {
				retval = TRUE;
			}
		}
	}
	g_strfreev(tokens);

	return retval;
}

