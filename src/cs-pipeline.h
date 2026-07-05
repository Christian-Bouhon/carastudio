/* Charte des 5 étapes du pipeline de traitement de CaraStudio (ordre RÉEL).
 * Index 0..4 = étapes A..E. Source UNIQUE des couleurs/lettres, partagée par :
 * le popover « Pipeline » (barre du haut), les badges sur les en-têtes de
 * modules, et (à venir) la section Pipeline de l'aide.
 *   A Géométrie · B Développement RAW (DCP) · C Réduction de bruit ·
 *   D Effets · E Sortie
 */
#ifndef CS_PIPELINE_H
#define CS_PIPELINE_H

#include <glib.h>

#define CS_STAGE_COUNT 5

static inline const char *
cs_stage_letter(gint stage)
{
	static const char * const L[CS_STAGE_COUNT] = { "A", "B", "C", "D", "E" };
	return (stage >= 0 && stage < CS_STAGE_COUNT) ? L[stage] : "";
}

static inline const char *
cs_stage_color(gint stage)
{
	static const char * const C[CS_STAGE_COUNT] = {
		"#7a8290", "#3d7fc4", "#2fa39a", "#9a6fc0", "#c8922e"
	};
	return (stage >= 0 && stage < CS_STAGE_COUNT) ? C[stage] : "#888888";
}

/* Titre de section précédé d'un badge « lettre+numéro » d'étape (ex. D3), rappelant
 * la position dans le pipeline : lettre = étape (couleur), numéro = ordre fin dans
 * l'étape (num<=0 → lettre seule). L'en-tête (gui_box) rend déjà le markup ; le
 * titre est « markup-ready » (pas de ré-échappement). À libérer (g_free). */
static inline gchar *
cs_stage_title(gint stage, gint num, const gchar *title)
{
	if (stage < 0 || stage >= CS_STAGE_COUNT)
		return g_strdup(title);
	/* Badge discret : police plus petite (size="small"), non gras, padding réduit
	   → le badge reste un préfixe secondaire, le nom de l'outil garde la vedette. */
	if (num > 0)
		return g_strdup_printf(
			"<span background=\"%s\" foreground=\"#ffffff\" size=\"x-small\"> %s%d </span> %s",
			cs_stage_color(stage), cs_stage_letter(stage), num, title);
	return g_strdup_printf(
		"<span background=\"%s\" foreground=\"#ffffff\" size=\"x-small\"> %s </span> %s",
		cs_stage_color(stage), cs_stage_letter(stage), title);
}

/* Badge d'étape SEUL (markup Pango, sans titre) — pour composer une étiquette
   où le badge précède d'autres widgets (ex. onglet : badge + icône + nom). */
static inline gchar *
cs_stage_badge(gint stage, gint num)
{
	if (stage < 0 || stage >= CS_STAGE_COUNT)
		return g_strdup("");
	if (num > 0)
		return g_strdup_printf(
			"<span background=\"%s\" foreground=\"#ffffff\" size=\"x-small\"> %s%d </span>",
			cs_stage_color(stage), cs_stage_letter(stage), num);
	return g_strdup_printf(
		"<span background=\"%s\" foreground=\"#ffffff\" size=\"x-small\"> %s </span>",
		cs_stage_color(stage), cs_stage_letter(stage));
}

#endif /* CS_PIPELINE_H */
