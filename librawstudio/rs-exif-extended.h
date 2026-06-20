/*
 * CaraStudio — lecteur EXIF étendu (set « top ~50 », photo d'abord).
 *
 * Relit un fichier (RAW ou JPEG/TIFF) avec exiv2 et renvoie une liste
 * ordonnée de paires (libellé, valeur déjà interprétée). Indépendant du
 * cache métadonnées et de la struct RSMetadata : lecture à la volée pour
 * l'onglet « Infos ».
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#ifndef RS_EXIF_EXTENDED_H
#define RS_EXIF_EXTENDED_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	gchar *label; /* libellé localisé (ex. « Programme d'exposition ») */
	gchar *value; /* valeur interprétée par exiv2 (ex. « Priorité ouverture ») */
} RSExifPair;

/**
 * Lit les EXIF étendus d'un fichier et renvoie une GList* de RSExifPair*
 * (ordre photo d'abord, plafonnée). À libérer avec rs_exif_extended_free().
 * Renvoie NULL si le fichier n'a pas de métadonnées lisibles.
 */
extern GList *rs_exif_read_extended(const gchar *filename);

/** Libère la liste renvoyée par rs_exif_read_extended(). */
extern void rs_exif_extended_free(GList *list);

G_END_DECLS

#endif /* RS_EXIF_EXTENDED_H */
