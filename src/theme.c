/*
 * CaraStudio — Sélecteur de thèmes GTK3 (overlay de palette).
 *
 * Le thème de base `theme.css` (anthracite) et l'overlay `themes/<cle>.css`
 * sont CONCATÉNÉS dans un unique GtkCssProvider : la base référence les
 * @define-color et l'overlay les redéfinit dans le même provider (les
 * définitions ultérieures priment), ce qui garantit la résolution — deux
 * providers distincts à même priorité ne se voient pas en GTK3.
 *
 * Un provider est TOUJOURS en place (jamais d'état sans thème) : si l'overlay
 * demandé est introuvable, on retombe sur la base.
 */

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <config.h>

#include "application.h"
#include "theme.h"
#include "rs-utils.h"
#include "conf_interface.h"
#include "gettext.h"

typedef struct {
	const gchar *key;
	const gchar *name;
} CSTHEME;

static const CSTHEME cs_themes[] = {
	{ "carafife",   N_("CaraStudio") },        /* défaut : palette d'origine (overlay carafife.css) */
	{ "anthracite", N_("Studio Anthracite") }, /* = base theme.css */
	{ "clair",      N_("Studio Clair") },
	{ "ambre",      N_("Studio Ambre") },
};

static GtkCssProvider *provider = NULL;

gint
cs_theme_count(void)
{
	return G_N_ELEMENTS(cs_themes);
}

const gchar *
cs_theme_key_at(gint i)
{
	return (i >= 0 && i < (gint) G_N_ELEMENTS(cs_themes)) ? cs_themes[i].key : NULL;
}

const gchar *
cs_theme_name_at(gint i)
{
	return (i >= 0 && i < (gint) G_N_ELEMENTS(cs_themes)) ? _(cs_themes[i].name) : NULL;
}

const gchar *
cs_theme_default_key(void)
{
	return cs_themes[0].key;
}

/* Retourne la clé du thème actif : valeur de « ui-theme » si connue, sinon le
 * défaut. Libère le retour. */
static gchar *
cs_theme_current_key(void)
{
	gchar *key = rs_conf_get_string("ui-theme");

	if (!key || !*key)
	{
		g_free(key);
		return g_strdup(cs_theme_default_key());
	}

	gint i;
	for (i = 0; i < cs_theme_count(); i++)
		if (g_str_equal(key, cs_theme_key_at(i)))
			return key;

	g_free(key);
	return g_strdup(cs_theme_default_key());
}

/* Lève l'ancien provider du screen et le libère. */
static void
cs_theme_unload(void)
{
	if (provider)
	{
		gtk_style_context_remove_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(provider));
		g_object_unref(provider);
		provider = NULL;
	}
}

/* Charge base + overlay et pose le provider. `cleaned` est une clé déjà validée
 * de la table : on charge toujours la base `theme.css`, puis l'overlay
 * `themes/<cle>.css` s'il existe (y compris pour le thème par défaut).
 * Retourne en sortie la clé réellement appliquée. Toujours mets un provider en
 * place sauf si même la base est illisible. */
static gboolean
cs_theme_build_and_load(const gchar *cleaned, gchar **effective)
{
	GError *err = NULL;
	gchar *base_path, *overlay_path;
	gchar *base = NULL, *overlay = NULL, *css = NULL;
	gsize len;
	gboolean ret = FALSE;

	*effective = g_strdup(cleaned);

	/* Lit la base. */
	base_path = g_build_filename(rs_reloc(PACKAGE_DATA_DIR), PACKAGE, "theme.css", NULL);
	if (!g_file_get_contents(base_path, &base, &len, &err))
	{
		g_warning("CaraStudio: lecture du thème de base échouée: %s", err ? err->message : "?");
		g_clear_error(&err);
		g_free(base_path);
		return FALSE;
	}
	g_free(base_path);

	/* Concatène l'overlay de la clé demandée s'il existe. */
	{
		gchar *overlay_name = g_strconcat(cleaned, ".css", NULL);
		overlay_path = g_build_filename(rs_reloc(PACKAGE_DATA_DIR), PACKAGE, "themes", overlay_name, NULL);
		g_free(overlay_name);
		if (g_file_test(overlay_path, G_FILE_TEST_EXISTS))
		{
			if (g_file_get_contents(overlay_path, &overlay, &len, &err))
				css = g_strdup_printf("%s\n\n%s", base, overlay);
			else
			{
				g_warning("CaraStudio: lecture de l'overlay « %s » échouée: %s", cleaned, err ? err->message : "?");
				g_clear_error(&err);
			}
		}
		else
		{
			/* Thème « base » (ex. anthracite) : pas d'overlay, on utilise theme.css seul. */
		}
		g_free(overlay_path);
	}
	g_free(overlay);

	if (!css)
		css = g_strdup(base);
	g_free(base);

	provider = gtk_css_provider_new();
	if (gtk_css_provider_load_from_data(provider, css, -1, &err))
	{
		gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		ret = TRUE;
	}
	else
	{
		g_warning("CaraStudio: chargement CSS échoué: %s", err ? err->message : "?");
		g_clear_error(&err);
		g_object_unref(provider);
		provider = NULL;
	}
	g_free(css);

	return ret;
}

void
cs_theme_init(void)
{
	cs_theme_apply(cs_theme_current_key());
}

void
cs_theme_apply(const gchar *key)
{
	gchar *cleaned, *effective = NULL;
	gboolean known = FALSE;
	gint i;

	cs_theme_unload();

	if (!key || !*key)
		cleaned = g_strdup(cs_theme_default_key());
	else
		cleaned = g_strdup(key);

	/* Sécurité : on n'accepte que les clés de la table (pas d'arbitrage de
	 * chemin vers d'autres fichiers). */
	for (i = 0; i < cs_theme_count(); i++)
		if (g_str_equal(cleaned, cs_theme_key_at(i)))
			known = TRUE;

	if (!known)
	{
		g_free(cleaned);
		cleaned = g_strdup(cs_theme_default_key());
	}

	if (cs_theme_build_and_load(cleaned, &effective))
		rs_conf_set_string("ui-theme", effective);
	else
		rs_conf_set_string("ui-theme", cs_theme_default_key());

	g_free(effective);
	g_free(cleaned);
}