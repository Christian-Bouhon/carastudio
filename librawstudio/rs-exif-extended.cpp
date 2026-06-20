/*
 * CaraStudio — lecteur EXIF étendu (set « top ~50 », photo d'abord).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <exiv2/exiv2.hpp>
#include <string>
#include <set>
#include "rs-exif-extended.h"

#ifndef EXIV2_TEST_VERSION
# define EXIV2_TEST_VERSION(major,minor,patch) \
	( EXIV2_VERSION >= EXIV2_MAKE_VERSION((major),(minor),(patch)) )
#endif

/* Plafond dur du nombre de lignes étendues (la liste blanche est déjà sous
   ce seuil, mais on borne par sécurité). */
#define RS_EXIF_EXTENDED_MAX 50

/* Liste blanche ORDONNÉE, photo d'abord. Les 9 champs déjà montrés par la
   grille de base (appareil, date, objectif, focale, ouverture, vitesse, ISO,
   correction expo, balance des blancs R/V/B) sont volontairement EXCLUS pour
   éviter les doublons. Le libellé passe par gettext (N_()). */
struct ExifField {
	const char *key;   /* clé exiv2 (Exif.Photo.*, Exif.Image.*) */
	const char *label; /* libellé non traduit (marqué N_) */
};

static const ExifField rs_exif_whitelist[] = {
	/* — Prise de vue — */
	{ "Exif.Photo.ExposureProgram",        N_("Programme d'exposition") },
	{ "Exif.Photo.ExposureMode",           N_("Mode d'exposition") },
	{ "Exif.Photo.MeteringMode",           N_("Mode de mesure") },
	{ "Exif.Photo.Flash",                  N_("Flash") },
	{ "Exif.Photo.WhiteBalance",           N_("Mode balance des blancs") },
	{ "Exif.Photo.LightSource",            N_("Source lumineuse") },
	{ "Exif.Photo.FocalLengthIn35mmFilm",  N_("Focale équiv. 24×36") },
	{ "Exif.Photo.MaxApertureValue",       N_("Ouverture maxi objectif") },
	{ "Exif.Photo.SubjectDistance",        N_("Distance sujet") },
	{ "Exif.Photo.SceneCaptureType",       N_("Type de scène") },
	{ "Exif.Photo.DigitalZoomRatio",       N_("Zoom numérique") },
	{ "Exif.Photo.SensitivityType",        N_("Type de sensibilité") },

	/* — Boîtier / optique — */
	{ "Exif.Image.Make",                   N_("Marque (boîtier)") },
	{ "Exif.Photo.LensMake",               N_("Marque objectif") },
	{ "Exif.Photo.LensModel",              N_("Modèle objectif") },
	{ "Exif.Photo.LensSerialNumber",       N_("N° de série objectif") },
	{ "Exif.Photo.BodySerialNumber",       N_("N° de série boîtier") },
	{ "Exif.Image.BodySerialNumber",       N_("N° de série boîtier") },
	{ "Exif.Image.Software",               N_("Logiciel") },

	/* — Image — */
	{ "Exif.Photo.PixelXDimension",        N_("Largeur (px)") },
	{ "Exif.Photo.PixelYDimension",        N_("Hauteur (px)") },
	{ "Exif.Photo.ColorSpace",             N_("Espace colorimétrique") },
	{ "Exif.Image.Orientation",            N_("Orientation") },
	{ "Exif.Photo.Contrast",               N_("Contraste") },
	{ "Exif.Photo.Saturation",             N_("Saturation") },
	{ "Exif.Photo.Sharpness",              N_("Netteté") },
	{ "Exif.Image.XResolution",            N_("Résolution X") },
	{ "Exif.Image.ResolutionUnit",         N_("Unité de résolution") },

	/* — GPS (altitude ; lat/long traités à part avec leur référence) — */
	{ "Exif.GPSInfo.GPSAltitude",          N_("Altitude GPS") },

	/* — Auteur / description — */
	{ "Exif.Image.Artist",                 N_("Auteur") },
	{ "Exif.Image.Copyright",              N_("Copyright") },
	{ "Exif.Image.ImageDescription",       N_("Description") },
	{ "Exif.Photo.UserComment",            N_("Commentaire") },
};

/* Clés couvertes par la grille de base (9 champs) ou structurelles/binaires :
   exclues du vidage générique pour éviter doublons et bruit. */
static const char *rs_exif_generic_skip[] = {
	/* — champs déjà montrés par la grille de base — */
	"Exif.Image.Model", "Exif.Photo.DateTimeOriginal", "Exif.Image.DateTime",
	"Exif.Photo.DateTimeDigitized", "Exif.Photo.FocalLength",
	"Exif.Photo.FNumber", "Exif.Photo.ApertureValue", "Exif.Photo.ExposureTime",
	"Exif.Photo.ShutterSpeedValue", "Exif.Photo.ISOSpeedRatings",
	"Exif.Photo.PhotographicSensitivity", "Exif.Photo.ExposureBiasValue",
	"Exif.Photo.LensInfo", "Exif.Image.ExposureTime", "Exif.Image.FNumber",
	"Exif.Image.ISOSpeedRatings", "Exif.Image.DateTimeOriginal",
	/* — tags structurels TIFF / versions / blobs binaires — */
	"Exif.Image.NewSubfileType", "Exif.Image.SubfileType",
	"Exif.Image.ImageWidth", "Exif.Image.ImageLength", "Exif.Image.BitsPerSample",
	"Exif.Image.Compression", "Exif.Image.PhotometricInterpretation",
	"Exif.Image.FillOrder", "Exif.Image.StripOffsets", "Exif.Image.SamplesPerPixel",
	"Exif.Image.RowsPerStrip", "Exif.Image.StripByteCounts",
	"Exif.Image.PlanarConfiguration", "Exif.Image.YCbCrSubSampling",
	"Exif.Image.YCbCrPositioning", "Exif.Image.YCbCrCoefficients",
	"Exif.Image.ReferenceBlackWhite", "Exif.Image.TileWidth",
	"Exif.Image.TileLength", "Exif.Image.TileOffsets", "Exif.Image.TileByteCounts",
	"Exif.Image.JPEGInterchangeFormat", "Exif.Image.JPEGInterchangeFormatLength",
	"Exif.Image.CFARepeatPatternDim", "Exif.Image.CFAPattern",
	"Exif.Image.ExifTag", "Exif.Image.GPSTag", "Exif.Image.PrintImageMatching",
	"Exif.Image.DNGPrivateData", "Exif.Image.TIFFEPStandardID",
	"Exif.Photo.MakerNote", "Exif.Photo.ExifVersion", "Exif.Photo.FlashpixVersion",
	"Exif.Photo.ComponentsConfiguration", "Exif.Photo.InteroperabilityTag",
	"Exif.Photo.SubSecTime", "Exif.Photo.SubSecTimeOriginal",
	"Exif.Photo.SubSecTimeDigitized", "Exif.Photo.CFAPattern",
};

/* TRUE si la clé doit être ignorée par le vidage générique. */
static gboolean
generic_skip_key(const std::string &key)
{
	for (guint i = 0; i < G_N_ELEMENTS(rs_exif_generic_skip); i++)
		if (key == rs_exif_generic_skip[i])
			return TRUE;
	return FALSE;
}

/* Ajoute une paire (label, value) à *list si value non vide. Renvoie le
   nouveau compteur. */
static gint
append_pair(GList **list, gint count, const gchar *label, const std::string &value)
{
	if (count >= RS_EXIF_EXTENDED_MAX)
		return count;

	gchar *v = g_strstrip(g_strdup(value.c_str()));
	if (!v || !*v)
	{
		g_free(v);
		return count;
	}

	RSExifPair *pair = g_new0(RSExifPair, 1);
	pair->label = g_strdup(label);
	pair->value = v; /* déjà alloué + strippé */
	*list = g_list_prepend(*list, pair);
	return count + 1;
}

/* Valeur interprétée d'une clé, ou chaîne vide si absente. */
static std::string
interpreted(Exiv2::ExifData &exifData, const char *key)
{
	try {
		Exiv2::ExifData::iterator i = exifData.findKey(Exiv2::ExifKey(key));
		if (i != exifData.end())
			return i->print(&exifData);
	} catch (Exiv2::Error &) {
		/* clé invalide : ignorer */
	}
	return std::string();
}

extern "C" GList *
rs_exif_read_extended(const gchar *filename)
{
	GList *list = NULL;
	gint count = 0;

	if (!filename)
		return NULL;

	try {
		auto img = Exiv2::ImageFactory::open(std::string(filename));
		img->readMetadata();
		Exiv2::ExifData &exifData = img->exifData();
		if (exifData.empty())
			return NULL;

		/* Clés déjà affichées : évite les doublons dans la passe générique. */
		std::set<std::string> seen;

		guint n = G_N_ELEMENTS(rs_exif_whitelist);
		for (guint k = 0; k < n && count < RS_EXIF_EXTENDED_MAX; k++)
		{
			std::string v = interpreted(exifData, rs_exif_whitelist[k].key);
			if (!v.empty())
			{
				gint before = count;
				count = append_pair(&list, count,
					_(rs_exif_whitelist[k].label), v);
				if (count > before)
					seen.insert(rs_exif_whitelist[k].key);
			}
		}

		/* GPS latitude / longitude : valeur + référence (N/S, E/W). */
		{
			std::string lat = interpreted(exifData, "Exif.GPSInfo.GPSLatitude");
			std::string latref = interpreted(exifData, "Exif.GPSInfo.GPSLatitudeRef");
			if (!lat.empty())
			{
				if (!latref.empty()) { lat += " "; lat += latref; }
				count = append_pair(&list, count, _("Latitude GPS"), lat);
				seen.insert("Exif.GPSInfo.GPSLatitude");
				seen.insert("Exif.GPSInfo.GPSLatitudeRef");
			}
			std::string lon = interpreted(exifData, "Exif.GPSInfo.GPSLongitude");
			std::string lonref = interpreted(exifData, "Exif.GPSInfo.GPSLongitudeRef");
			if (!lon.empty())
			{
				if (!lonref.empty()) { lon += " "; lon += lonref; }
				count = append_pair(&list, count, _("Longitude GPS"), lon);
				seen.insert("Exif.GPSInfo.GPSLongitude");
				seen.insert("Exif.GPSInfo.GPSLongitudeRef");
			}
		}

		/* Vidage générique : tous les autres tags présents (y compris les
		   Makernotes du boîtier), libellés via exiv2, jusqu'au plafond. On
		   écarte les doublons, la vignette, et les valeurs longues/binaires. */
		for (Exiv2::ExifData::const_iterator i = exifData.begin();
		     i != exifData.end() && count < RS_EXIF_EXTENDED_MAX; ++i)
		{
			try {
				const std::string key = i->key();
				if (seen.count(key) || generic_skip_key(key))
					continue;
				if (i->groupName() == "Thumbnail" || i->groupName() == "Iop")
					continue;

				std::string v = i->print(&exifData);
				/* écarte blobs/tableaux : trop longs pour une ligne d'info */
				if (v.empty() || v.size() > 80)
					continue;

				std::string label = i->tagLabel();
				if (label.empty())
					label = i->tagName();

				gint before = count;
				count = append_pair(&list, count, label.c_str(), v);
				if (count > before)
					seen.insert(key);
			} catch (Exiv2::Error &) {
				/* tag illisible : ignorer */
			}
		}
	} catch (Exiv2::Error &e) {
		g_debug("rs_exif_read_extended('%s'): %s", filename, e.what());
		rs_exif_extended_free(list);
		return NULL;
	}

	/* On a prepend : remettre dans l'ordre de la liste blanche. */
	return g_list_reverse(list);
}

extern "C" void
rs_exif_extended_free(GList *list)
{
	GList *l;
	for (l = list; l != NULL; l = l->next)
	{
		RSExifPair *pair = (RSExifPair *) l->data;
		if (pair)
		{
			g_free(pair->label);
			g_free(pair->value);
			g_free(pair);
		}
	}
	g_list_free(list);
}
