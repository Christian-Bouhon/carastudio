/*
 * CaraStudio — styles nommés (presets).
 *
 * Un « style » est un jeu de réglages enregistré sur disque sous un nom, que
 * l'on peut réappliquer à une ou plusieurs photos (à la manière des profils
 * partiels d'ART). Le fichier .carastyle contient TOUS les réglages (snapshot
 * complet, réutilisant la sérialisation de rs-cache) ; les GROUPES mémorisés
 * (RSStyleGroups) indiquent ce que le style est censé porter, appliqué via
 * rs_settings_copy_partial() — jamais le profil DCP, qui reste propre à chaque
 * photo.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 */

#ifndef RS_STYLE_H
#define RS_STYLE_H

#include <rawstudio.h>

/* Répertoire des styles (~/.config/carastudio/styles), créé si absent.
 * À libérer avec g_free(). */
extern gchar *rs_style_get_dir(void);

/* Liste triée (insensible à la casse) des noms de styles disponibles.
 * GList de gchar* — libérer avec g_list_free_full(list, g_free). */
extern GList *rs_style_list(void);

/* Un style porte-t-il déjà ce nom ? */
extern gboolean rs_style_exists(const gchar *name);

/* Enregistre les réglages sous un style nommé (écrase si le nom existe).
 * @param name    Nom affiché du style
 * @param settings Réglages à mémoriser (snapshot complet)
 * @param groups  Groupes STYLE_* que le style porte (appliqués par défaut)
 * @return TRUE si l'écriture a réussi. */
extern gboolean rs_style_save(const gchar *name, RSSettings *settings, RSStyleGroups groups);

/* Charge un style dans *out_settings (RSSettings déjà alloué appartenant à
 * l'appelant) et renseigne *out_groups (peut être NULL). @return TRUE si OK. */
extern gboolean rs_style_load(const gchar *name, RSSettings *out_settings, RSStyleGroups *out_groups);

/* Supprime / renomme un style. @return TRUE si OK. */
extern gboolean rs_style_delete(const gchar *name);
extern gboolean rs_style_rename(const gchar *oldname, const gchar *newname);

#endif /* RS_STYLE_H */
