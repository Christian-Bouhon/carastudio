/*
 * CaraStudio — plugin load-libraw
 *
 * Décodeur RAW universel basé sur LibRaw 0.20+.
 * Remplace load-rawspeed (fork Klaus Post 2016, abandonné, ~400 boîtiers).
 * LibRaw supporte 1000+ boîtiers et est activement maintenu.
 *
 * Priorité 10 > rawspeed (priorité 5) → LibRaw est essayé en premier.
 * rawspeed reste en fallback pour les éventuels formats non gérés.
 *
 * Licence : GPL-2.0-or-later (comme le reste de CaraStudio)
 */

#include <rawstudio.h>
#include <libraw/libraw.h>
#include <string.h>

/* Priorité supérieure à rawspeed (5) pour être essayé en premier */
#define LIBRAW_PRIORITY 10

/* ------------------------------------------------------------------ */
/* Chargement de l'image RAW                                           */
/* ------------------------------------------------------------------ */

static RSFilterResponse *
load_libraw_file(const gchar *filename)
{
	RSFilterResponse *response;
	RS_IMAGE16 *image = NULL;
	libraw_data_t *raw;
	int ret;

	rs_io_lock();
	raw = libraw_init(0);
	if (!raw) {
		rs_io_unlock();
		g_warning("load-libraw: libraw_init() échoué pour %s", filename);
		return rs_filter_response_new();
	}

	ret = libraw_open_file(raw, filename);
	if (ret != LIBRAW_SUCCESS) {
		libraw_close(raw);
		rs_io_unlock();
		/* Pas un fichier RAW connu → retour silencieux, rawspeed tentera */
		return rs_filter_response_new();
	}

	ret = libraw_unpack(raw);
	rs_io_unlock();

	if (ret != LIBRAW_SUCCESS) {
		g_warning("load-libraw: décompression échouée: %s — %s",
		          filename, libraw_strerror(ret));
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/*
	 * Fujifilm X-Trans : filters == 9 (pattern 6x6, non-Bayer).
	 * Sigma Foveon    : colors == 4 (capteur multicouche).
	 * Ces formats nécessitent un dématriçage spécialisé non présent
	 * dans CaraStudio. On laisse rawspeed tenter en fallback.
	 */
	if (raw->idata.filters == 9 || raw->idata.colors > 3) {
		libraw_close(raw);
		return rs_filter_response_new();
	}

	guint width       = (guint)raw->sizes.width;
	guint height      = (guint)raw->sizes.height;
	guint raw_width   = (guint)raw->sizes.raw_width;
	guint left_margin = (guint)raw->sizes.left_margin;
	guint top_margin  = (guint)raw->sizes.top_margin;
	uint16_t *raw_pixels = raw->rawdata.raw_image;

	if (!raw_pixels || width == 0 || height == 0) {
		g_warning("load-libraw: données pixels vides pour %s", filename);
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/* Image Bayer 16 bits, 1 canal, 1 short par pixel */
	image = rs_image16_new(width, height, 1, 1);
	if (!image) {
		libraw_close(raw);
		return rs_filter_response_new();
	}

	/*
	 * Pattern CFA au format dcraw 32 bits — directement compatible
	 * avec RS_IMAGE16.filters, utilisé par le filtre de dématriçage.
	 */
	image->filters = (guint)raw->idata.filters;

	/* Copie des pixels Bayer depuis le buffer LibRaw */
	guint y;
	for (y = 0; y < height; y++) {
		gushort *dst        = GET_PIXEL(image, 0, y);
		const uint16_t *src = raw_pixels
		                      + (top_margin + y) * raw_width
		                      + left_margin;
		memcpy(dst, src, width * sizeof(gushort));
	}

	libraw_close(raw);

	response = rs_filter_response_new();
	rs_filter_response_set_image(response, image);
	rs_filter_response_set_width(response, (gint)width);
	rs_filter_response_set_height(response, (gint)height);
	g_object_unref(image);

	return response;
}

/* ------------------------------------------------------------------ */
/* Enregistrement des formats                                          */
/* ------------------------------------------------------------------ */

static void
reg(const gchar *ext, const gchar *desc)
{
	rs_filetype_register_loader(ext, desc, load_libraw_file,
	                            LIBRAW_PRIORITY, RS_LOADER_FLAGS_RAW);
}

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	/* Sony Alpha / NEX / ZV */
	reg(".arw", "Sony RAW (LibRaw)");
	reg(".srf", "Sony RAW (LibRaw)");
	reg(".sr2", "Sony RAW (LibRaw)");

	/* Canon EOS / PowerShot */
	reg(".cr2", "Canon RAW (LibRaw)");
	reg(".cr3", "Canon RAW CR3 (LibRaw)");   /* EOS R — non supporté rawspeed */
	reg(".crw", "Canon RAW CRW (LibRaw)");

	/* Nikon */
	reg(".nef", "Nikon RAW (LibRaw)");
	reg(".nrw", "Nikon RAW (LibRaw)");

	/* Olympus / OM System */
	reg(".orf", "Olympus RAW (LibRaw)");

	/* Pentax / Ricoh */
	reg(".pef", "Pentax RAW (LibRaw)");
	reg(".ptx", "Pentax RAW (LibRaw)");

	/* Panasonic / Leica (partagent le format RW2) */
	reg(".rw2", "Panasonic RAW (LibRaw)");

	/* Fujifilm — le dématriçage X-Trans est filtré dans le loader */
	reg(".raf", "Fujifilm RAW (LibRaw)");

	/* Adobe Digital Negative */
	reg(".dng", "Digital Negative (LibRaw)");

	/* Hasselblad */
	reg(".3fr", "Hasselblad RAW (LibRaw)");
	reg(".fff", "Hasselblad RAW (LibRaw)");

	/* Minolta / Konica Minolta */
	reg(".mrw", "Minolta RAW (LibRaw)");

	/* Leica */
	reg(".rwl", "Leica RAW (LibRaw)");
	reg(".raw", "RAW générique (LibRaw)");

	/* Samsung */
	reg(".srw", "Samsung RAW (LibRaw)");

	/* Sigma (Bayer uniquement, Foveon filtré) */
	reg(".x3f", "Sigma RAW (LibRaw)");

	/* Epson */
	reg(".erf", "Epson RAW (LibRaw)");

	/* Kodak */
	reg(".kdc", "Kodak RAW (LibRaw)");
	reg(".dcs", "Kodak RAW (LibRaw)");
	reg(".dcr", "Kodak RAW (LibRaw)");

	/* Phase One / Mamiya / Leaf */
	reg(".iiq", "Phase One RAW (LibRaw)");
	reg(".mef", "Mamiya RAW (LibRaw)");
	reg(".mos", "Leaf RAW (LibRaw)");
}
