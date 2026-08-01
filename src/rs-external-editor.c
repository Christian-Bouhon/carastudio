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
#include <gtk/gtk.h>
#include <unistd.h>
#include <stdlib.h>
#include "gettext.h"
#include "application.h"
#include "rs-photo.h"
#include "conf_interface.h"
#include "gtk-interface.h" /* rawstudio_window (parent des dialogues) */


static gchar **find_gimp_command(void);
static gchar **prompt_for_gimp_command(void);
static gchar **external_editor_environ(void);

gboolean
rs_external_editor_gimp(RS_PHOTO *photo, RSFilter *prior_to_resample, guint snapshot)
{
	RSOutput *output = NULL;
	g_assert(RS_IS_PHOTO(photo));

	/* GIMP disponible ? On cherche AVANT de rendre, quel que soit le mode
	 * d'installation (paquet distro ou Flatpak). Le fichier est passé en
	 * ARGUMENT → toutes les versions 2.x/3.x l'ouvrent, pas de test de version. */
	gchar **gimp_cmd = find_gimp_command();
	if (!gimp_cmd)
		/* Auto-détection en échec (typique sous AppImage, où l'environnement
		 * nettoyé masque le GIMP hôte) : on demande une seule fois à l'utilisateur
		 * de pointer l'exécutable, on le mémorise (CONF_GIMP_COMMAND) et on relance.
		 * Les fois suivantes, find_gimp_command() lit directement ce chemin. */
		gimp_cmd = prompt_for_gimp_command();
	if (!gimp_cmd) {
		g_warning("GIMP introuvable (ni dans le PATH, ni en Flatpak org.gimp.GIMP)");
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
	/* argv = préfixe de commande GIMP (« gimp », ou « flatpak run org.gimp.GIMP »)
	 * suivi du fichier PNG. */
	guint pfx = g_strv_length(gimp_cmd);
	gchar **argv = g_new0(gchar *, pfx + 2);
	guint k;
	for (k = 0; k < pfx; k++)
		argv[k] = gimp_cmd[k];            /* pointeurs partagés, libérés via gimp_cmd */
	argv[pfx] = filename->str;
	argv[pfx + 1] = NULL;

	/* Environnement NETTOYÉ : sous AppImage, ne pas transmettre à GIMP notre
	 * LD_LIBRARY_PATH / modules bundlés, sinon il charge nos libs (versions
	 * incompatibles) et plante (issue #12, sickboy). */
	gchar **child_env = external_editor_environ();
	GError *error = NULL;
	gboolean launched = g_spawn_async(NULL, argv, child_env,
		G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL, NULL, NULL, &error);
	g_free(argv);                 /* tableau seul ; éléments = gimp_cmd + filename */
	g_strfreev(child_env);
	g_strfreev(gimp_cmd);

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

/* Environnement à transmettre à un programme EXTERNE (GIMP). Sous AppImage, on
 * RETIRE les variables injectées par le runtime qui, héritées par GIMP, le
 * feraient charger NOS bibliothèques bundlées (versions incompatibles → plantage,
 * issue #12). Hors AppImage (APPDIR non défini), l'environnement est inchangé.
 * L'appelant libère avec g_strfreev(). */
static gchar **
external_editor_environ(void)
{
	gchar **env = g_get_environ();
	if (g_getenv("APPDIR"))   /* on tourne dans une AppImage */
	{
		static const gchar * const strip[] = {
			"LD_LIBRARY_PATH", "LD_PRELOAD",
			"GTK_PATH", "GTK_IM_MODULE_FILE", "GTK_EXE_PREFIX", "GTK_DATA_PREFIX",
			"GDK_PIXBUF_MODULE_FILE", "GDK_PIXBUF_MODULEDIR",
			"GIO_MODULE_DIR", "GSETTINGS_SCHEMA_DIR",
			"FONTCONFIG_FILE", "FONTCONFIG_PATH",
			"GI_TYPELIB_PATH", "PANGO_LIBDIR", NULL };
		gint i;
		for (i = 0; strip[i]; i++)
			env = g_environ_unsetenv(env, strip[i]);
	}
	return env;
}

/* Trouve comment lancer GIMP, quel que soit le mode d'installation, et renvoie un
 * argv NULL-terminé (préfixe de commande, à libérer par g_strfreev) ou NULL si
 * GIMP est absent :
 *   - paquet distro : { "<chemin>/gimp", NULL }   (gimp-2.10 / gimp en repli)
 *   - Flatpak       : { "<chemin>/flatpak", "run", "org.gimp.GIMP", NULL }
 * Les binaires hôte (flatpak) sont testés avec l'environnement nettoyé, sinon la
 * détection elle-même échouerait sous AppImage. */
static gchar **
find_gimp_command(void)
{
	/* Chemin explicitement configuré par l'utilisateur (voir
	 * prompt_for_gimp_command) : PRIORITAIRE sur l'auto-détection. S'il n'est plus
	 * exécutable (GIMP déplacé/désinstallé), on l'ignore et on retombe sur la
	 * détection automatique. */
	gchar *conf = rs_conf_get_string(CONF_GIMP_COMMAND);
	if (conf)
	{
		if (conf[0] && g_file_test(conf, G_FILE_TEST_IS_EXECUTABLE))
		{
			gchar **argv = g_new0(gchar *, 2);
			argv[0] = conf;   /* chemin absolu, transféré à l'appelant */
			return argv;
		}
		g_free(conf);
	}

	const gchar *candidates[] = { "gimp", "gimp-2.10", "gimp-3.0", NULL };
	gint i;
	for (i = 0; candidates[i]; i++)
	{
		gchar *path = g_find_program_in_path(candidates[i]);
		if (path)
		{
			gchar **argv = g_new0(gchar *, 2);
			argv[0] = path;   /* chemin absolu, déjà alloué */
			return argv;
		}
	}

	/* Repli Flatpak : flatpak présent ET org.gimp.GIMP installé. */
	gchar *fp = g_find_program_in_path("flatpak");
	if (fp)
	{
		gchar *check_argv[] = { fp, "info", "org.gimp.GIMP", NULL };
		gchar **env = external_editor_environ();
		gint status = -1;
		gboolean ok = g_spawn_sync(NULL, check_argv, env,
			G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			NULL, NULL, NULL, NULL, &status, NULL);
		g_strfreev(env);
		if (ok && status == 0)
		{
			gchar **argv = g_new0(gchar *, 4);
			argv[0] = fp;                        /* chemin flatpak (alloué) */
			argv[1] = g_strdup("run");
			argv[2] = g_strdup("org.gimp.GIMP");
			return argv;
		}
		g_free(fp);
	}

	return NULL;
}

/* Demande à l'utilisateur de pointer l'exécutable GIMP via un sélecteur de
 * fichier, mémorise le choix dans CONF_GIMP_COMMAND et renvoie l'argv
 * correspondant ({ "<chemin>", NULL }, à libérer par g_strfreev), ou NULL si
 * l'utilisateur annule / choisit un fichier non exécutable. N'est appelée qu'en
 * dernier recours, quand l'auto-détection a échoué. */
static gchar **
prompt_for_gimp_command(void)
{
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		_("GIMP introuvable — indiquez l'exécutable GIMP"),
		rawstudio_window,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		_("Annuler"), GTK_RESPONSE_CANCEL,
		_("Valider"), GTK_RESPONSE_ACCEPT,
		NULL);

	/* Les exécutables vivent le plus souvent dans /usr/bin ; on y ouvre par défaut
	 * sans l'imposer (l'utilisateur peut viser un GIMP AppImage, /opt, etc.). */
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), "/usr/bin");

	gchar **result = NULL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (path && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
		{
			rs_conf_set_string(CONF_GIMP_COMMAND, path);
			result = g_new0(gchar *, 2);
			result[0] = path;   /* chemin absolu, transféré à l'appelant */
		}
		else
		{
			GtkWidget *err = gtk_message_dialog_new(
				rawstudio_window,
				GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
				_("« %s » n'est pas un exécutable. Sélectionnez le programme GIMP."),
				path ? path : "");
			gtk_dialog_run(GTK_DIALOG(err));
			gtk_widget_destroy(err);
			g_free(path);
		}
	}
	gtk_widget_destroy(dialog);
	return result;
}

