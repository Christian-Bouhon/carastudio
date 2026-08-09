/*
 * CaraStudio — Sélecteur de thèmes (overlay de palette GTK3)
 *
 * Le thème de base est `theme.css` (anthracite). Chaque thème additionnel est
 * un petit fichier `themes/<cle>.css` qui redéfinit la palette
 * `@define-color cs_*` (et éventuellement quelques règles ciblées), chargé par
 *-dessus à la même priorité APPLICATION. Le thème s'applique à chaud (sans
 * redémarrage), comme darktable.
 */

#ifndef THEME_H
#define THEME_H

#include <glib.h>

/* Nombre de thèmes disponibles. */
extern gint cs_theme_count(void);

/* 0 <= i < cs_theme_count() : clé du thème i (non traduite, stable). */
extern const gchar *cs_theme_key_at(gint i);

/* Nom localisé (traduit) du thème i. */
extern const gchar *cs_theme_name_at(gint i);

/* Clé du thème par défaut (« carafife » : la palette d'origine). */
extern const gchar *cs_theme_default_key(void);

/* Charge le provider de base + applique la conf « ui-theme » au démarrage. */
extern void cs_theme_init(void);

/* Applique le thème `key` immédiatement (restyle GTK à chaud) et enregistre
 * « ui-theme ». Déprécie toute autre key vers le défaut si le fichier manque. */
extern void cs_theme_apply(const gchar *key);

#endif /* THEME_H */
