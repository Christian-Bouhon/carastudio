/*
 * CaraStudio — plugin effets artistiques
 *
 * Lumière douce : adapté de ART/RawTherapee ipsoftlight.cc
 *   Copyright 2018 Alberto Griggio <alberto.griggio@gmail.com>
 *   GPL v3+
 *
 * Vignettage artistique : adapté de ART/RawTherapee iptransform.cc
 *   GPL v3+
 *
 * Noir & Blanc : mélangeur de canaux libres (style ART/Lightroom)
 *   Copyright (C) 2026 Carafife — GPL v3+
 *
 * Adaptation C pour pipeline RS_IMAGE16 CaraStudio :
 *   Copyright (C) 2026 Carafife — GPL v3+
 */

#include <rawstudio.h>
#include <rs-image16.h>
#include <math.h>

#define RS_TYPE_EFFECTS rs_effects_get_type()
#define RS_EFFECTS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), RS_TYPE_EFFECTS, RSEffects))

typedef struct {
	RSFilter parent;
	RSSettings *settings;
	gulong settings_signal_id;
	gfloat softlight_strength;
	gfloat art_vignette_strength;
	gfloat art_vignette_feather;
	gfloat art_vignette_roundness;
	gfloat dehaze_strength;
	gfloat dehaze_saturation;
	gboolean bw_enabled;
	gint bw_filter;
	gfloat bw_red;
	gfloat bw_orange;
	gfloat bw_yellow;
	gfloat bw_green;
	gfloat bw_cyan;
	gfloat bw_blue;
	gfloat bw_violet;
	/* Égaliseur de tons par bandes */
	gboolean toneeq_enabled;
	gfloat toneeq_band0;
	gfloat toneeq_band1;
	gfloat toneeq_band2;
	gfloat toneeq_band3;
	gfloat toneeq_band4;
	gfloat toneeq_pivot;
	/* Argentico (négatif argentique) — inversion en espace de travail */
	gboolean argentico_enabled;
	gfloat argentico_green_exp;
	gfloat argentico_red_ratio;
	gfloat argentico_blue_ratio;
	gfloat argentico_exposure;
	gfloat argentico_ref_r;
	gfloat argentico_ref_g;
	gfloat argentico_ref_b;
	/* Correction couleur — roues 3 voies */
	gboolean colorwheels_enabled;
	gfloat cw_shadows_x, cw_shadows_y, cw_shadows_lum;
	gfloat cw_mid_x, cw_mid_y, cw_mid_lum;
	gfloat cw_high_x, cw_high_y, cw_high_lum;
} RSEffects;

typedef struct {
	RSFilterClass parent_class;
} RSEffectsClass;

RS_DEFINE_FILTER(rs_effects, RSEffects)

enum { PROP_0, PROP_SETTINGS };

static void get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec);
static void set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec);
static RSFilterResponse *get_image(RSFilter *filter, const RSFilterRequest *request);
static void settings_changed(RSSettings *settings, RSSettingsMask mask, RSEffects *effects);
static void settings_weak_notify(gpointer data, GObject *where_the_object_was);

/* L'objet RSSettings n'est PAS détenu par le filtre (référence empruntée,
 * propriété de la photo). On suit donc sa destruction par une weak-ref pour
 * remettre e->settings à NULL et éviter de déréférencer un objet libéré au
 * changement de photo (cf. crash effects.c:99). Même schéma que le plugin DCP. */
static void
finalize(GObject *object)
{
	RSEffects *e = RS_EFFECTS(object);

	if (e->settings && e->settings_signal_id)
	{
		g_signal_handler_disconnect(e->settings, e->settings_signal_id);
		g_object_weak_unref(G_OBJECT(e->settings), settings_weak_notify, e);
	}
	e->settings_signal_id = 0;
	e->settings = NULL;
}

static void
rs_effects_class_init(RSEffectsClass *klass)
{
	RSFilterClass *filter_class = RS_FILTER_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->get_property = get_property;
	obj_class->set_property = set_property;
	obj_class->finalize = finalize;
	filter_class->name = "CaraStudio Effects";
	filter_class->get_image = get_image;

	g_object_class_install_property(obj_class, PROP_SETTINGS,
		g_param_spec_object("settings", "settings", "RSSettings",
			RS_TYPE_SETTINGS, G_PARAM_READWRITE));
}

static void
rs_effects_init(RSEffects *effects)
{
	effects->settings = NULL;
	effects->settings_signal_id = 0;
	effects->softlight_strength = 0.0f;
	effects->art_vignette_strength = 0.0f;
	effects->art_vignette_feather = 50.0f;
	effects->art_vignette_roundness = 50.0f;
}

static void
get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec)
{
	RSEffects *e = RS_EFFECTS(obj);
	switch (id) {
	case PROP_SETTINGS: g_value_set_object(val, e->settings); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
	}
}

static void
set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec)
{
	RSEffects *e = RS_EFFECTS(obj);
	switch (id) {
	case PROP_SETTINGS:
		/* Si on réapplique le MÊME objet settings, on se contente de
		 * recharger les valeurs (ne pas déconnecter/reconnecter). */
		if (e->settings && e->settings_signal_id)
		{
			if (e->settings == g_value_get_object(val))
			{
				settings_changed(e->settings, MASK_ALL, e);
				break;
			}
			g_signal_handler_disconnect(e->settings, e->settings_signal_id);
			g_object_weak_unref(G_OBJECT(e->settings), settings_weak_notify, e);
			e->settings_signal_id = 0;
		}
		e->settings = g_value_get_object(val);
		if (e->settings) {
			e->settings_signal_id = g_signal_connect(e->settings,
				"settings-changed", G_CALLBACK(settings_changed), e);
			g_object_weak_ref(G_OBJECT(e->settings), settings_weak_notify, e);
			/* Charge l'état initial depuis les nouveaux réglages. */
			settings_changed(e->settings, MASK_ALL, e);
		}
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
	}
}

static void
settings_weak_notify(gpointer data, GObject *where_the_object_was)
{
	RSEffects *e = RS_EFFECTS(data);
	e->settings = NULL;
	e->settings_signal_id = 0;
}

static void
settings_changed(RSSettings *settings, RSSettingsMask mask, RSEffects *effects)
{
	g_object_get(settings,
		"dehaze-strength",       &effects->dehaze_strength,
		"dehaze-saturation",     &effects->dehaze_saturation,
		"softlight-strength",    &effects->softlight_strength,
		"art-vignette-strength", &effects->art_vignette_strength,
		"art-vignette-feather",  &effects->art_vignette_feather,
		"art-vignette-roundness",&effects->art_vignette_roundness,
		"bw-enabled",            &effects->bw_enabled,
		"bw-filter",             &effects->bw_filter,
		"bw-red",                &effects->bw_red,
		"bw-orange",             &effects->bw_orange,
		"bw-yellow",             &effects->bw_yellow,
		"bw-green",              &effects->bw_green,
		"bw-cyan",               &effects->bw_cyan,
		"bw-blue",               &effects->bw_blue,
		"bw-violet",             &effects->bw_violet,
		"toneeq-enabled",        &effects->toneeq_enabled,
		"toneeq-band0",          &effects->toneeq_band0,
		"toneeq-band1",          &effects->toneeq_band1,
		"toneeq-band2",          &effects->toneeq_band2,
		"toneeq-band3",          &effects->toneeq_band3,
		"toneeq-band4",          &effects->toneeq_band4,
		"toneeq-pivot",          &effects->toneeq_pivot,
		"argentico-enabled",     &effects->argentico_enabled,
		"argentico-green-exp",   &effects->argentico_green_exp,
		"argentico-red-ratio",   &effects->argentico_red_ratio,
		"argentico-blue-ratio",  &effects->argentico_blue_ratio,
		"argentico-exposure",    &effects->argentico_exposure,
		"argentico-ref-r",       &effects->argentico_ref_r,
		"argentico-ref-g",       &effects->argentico_ref_g,
		"argentico-ref-b",       &effects->argentico_ref_b,
		"colorwheels-enabled",   &effects->colorwheels_enabled,
		"cw-shadows-x",          &effects->cw_shadows_x,
		"cw-shadows-y",          &effects->cw_shadows_y,
		"cw-shadows-lum",        &effects->cw_shadows_lum,
		"cw-mid-x",              &effects->cw_mid_x,
		"cw-mid-y",              &effects->cw_mid_y,
		"cw-mid-lum",            &effects->cw_mid_lum,
		"cw-high-x",             &effects->cw_high_x,
		"cw-high-y",             &effects->cw_high_y,
		"cw-high-lum",           &effects->cw_high_lum,
		NULL);
	rs_filter_changed(RS_FILTER(effects), RS_FILTER_CHANGED_PIXELDATA);
}

/* ------------------------------------------------------------------ */
/* Lumière douce — algorithme Pegtop (d'après ART/ipsoftlight.cc)     */
/* ------------------------------------------------------------------ */

static inline float to_linear(float v)
{
	v /= 65535.0f;
	return (v <= 0.04045f) ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

static inline float to_srgb(float v)
{
	v = (v <= 0.0031308f) ? v * 12.92f : 1.055f * powf(v, 1.0f/2.4f) - 0.055f;
	return v * 65535.0f;
}

static void
apply_softlight(RS_IMAGE16 *img, gfloat strength)
{
	if (strength < 0.001f) return;

	const float blend = strength / 100.0f;
	gushort lut[65536];

	for (gint i = 0; i < 65536; i++) {
		float v = to_linear((float)i);
		float v2 = v * v;
		float sl = v2 + 2.0f*v2 - 2.0f*v2*v;   /* Pegtop formula */
		float out = to_srgb(CLAMP(v + blend * (sl - v), 0.0f, 1.0f));
		lut[i] = (gushort) CLAMP((gint)(out + 0.5f), 0, 65535);
	}

	const gint W = img->w;
	const gint H = img->h;
	const gint ps = img->pixelsize;
	const gint rs = img->rowstride;

	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + y * rs;
		for (gint x = 0; x < W; x++) {
			gushort *px = row + x * ps;
			px[0] = lut[px[0]];
			px[1] = lut[px[1]];
			px[2] = lut[px[2]];
		}
	}
}

/* ------------------------------------------------------------------ */
/* Vignettage artistique (d'après ART/iptransform.cc)                 */
/* ------------------------------------------------------------------ */

static void
apply_vignette(RS_IMAGE16 *img, gfloat strength, gfloat feather, gfloat roundness)
{
	if (fabsf(strength) < 0.001f) return;

	const gint W = img->w;
	const gint H = img->h;
	const gint ps = img->pixelsize;
	const gint rs = img->rowstride;

	const float cx = W * 0.5f;
	const float cy = H * 0.5f;
	const float round_f = roundness / 100.0f;
	const float feather_f = MAX(feather / 100.0f, 1e-4f);
	/* strength : négatif = bords sombres, positif = bords clairs */
	const float factor = strength / 6.0f;

	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + y * rs;
		const float dy = (y - cy) / cy;

		for (gint x = 0; x < W; x++) {
			const float dx = (x - cx) / cx;
			float dr = sqrtf(dx*dx + dy*dy);
			/* ellipse → cercle modulé par roundness */
			float rl = (1.0f - round_f) * fmaxf(fabsf(dx), fabsf(dy))
			         + round_f * dr;

			float inner = 1.0f - feather_f;
			float t = (rl - inner) / feather_f;
			t = CLAMP(t, 0.0f, 1.0f);
			/* smoothstep */
			t = t * t * (3.0f - 2.0f * t);

			float mult = 1.0f - factor * t;
			mult = CLAMP(mult, 0.0f, 4.0f);

			gushort *px = row + x * ps;
			for (gint c = 0; c < 3; c++) {
				float v = px[c] * mult;
				px[c] = (gushort) CLAMP((gint)(v + 0.5f), 0, 65535);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Noir & Blanc — mélangeur de canaux DIRECT (façon ART / channel mixer) */
/* ------------------------------------------------------------------ */
/*
 * Deux étages indépendants, comme en photo argentique :
 *
 *  1. FILTRE coloré (global) : calcule la luminance de BASE du gris en
 *     pondérant R/G/B. C'est le « filtre devant l'objectif » : un filtre
 *     rouge éclaircit globalement les rouges et assombrit les bleus. Filtre 0
 *     = luminance Rec.709 neutre. Les poids somment ~1 → pas de dérive.
 *
 *  2. CURSEURS par teinte (ciblés) : chaque curseur ajuste UNIQUEMENT les
 *     pixels de sa couleur, proportionnellement à leur SATURATION. Un pixel
 *     gris (saturation nulle) n'est PAS touché — c'est ce qui distingue ce
 *     comportement du simple mélangeur de canaux qui agissait sur toute
 *     l'image. Ancres de teinte : Rouge 0°, Orange 30°, Jaune 60°, Vert 120°,
 *     Cyan 180°, Bleu 240°, Violet 300° ; interpolation circulaire.
 *
 *     gris = base · (1 + ajust(teinte) · saturation)
 *
 *     Curseur 0–200, neutre 33 :
 *        33  → ajust 0    (aucun effet)
 *        0   → ajust −1.5 (assombrit les pixels de cette teinte jusqu'au noir)
 *        200 → ajust +2.5 (les éclaircit fortement, jusqu'au blanc)
 */

/* Teinte [0;360[ + saturation HSL [0;1] ; entrées r,g,b en [0;1]. */
static inline void
bw_rgb_to_hue_sat(gfloat r, gfloat g, gfloat b, gfloat *hue, gfloat *sat)
{
	gfloat mx = fmaxf(r, fmaxf(g, b));
	gfloat mn = fminf(r, fminf(g, b));
	gfloat d  = mx - mn;

	if (d < 1e-6f) { *hue = 0.0f; *sat = 0.0f; return; }

	gfloat l = 0.5f * (mx + mn);
	*sat = (l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);

	gfloat h;
	if (mx == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
	else if (mx == g) h = (b - r) / d + 2.0f;
	else              h = (r - g) / d + 4.0f;
	*hue = h * 60.0f;
}

static void
apply_bw(RS_IMAGE16 *img,
         gint filter,
         gfloat bw_r, gfloat bw_o, gfloat bw_y,
         gfloat bw_g, gfloat bw_c, gfloat bw_b, gfloat bw_v)
{
	/* Filtre coloré : poids R/G/B de base (somme = 1). */
	static const gfloat filters[9][3] = {
		{ 0.2126f, 0.7152f, 0.0722f }, /* 0 Aucun (Rec.709) */
		{ 0.55f,   0.38f,   0.07f   }, /* 1 Rouge        */
		{ 0.45f,   0.48f,   0.07f   }, /* 2 Rouge-Jaune  */
		{ 0.38f,   0.55f,   0.07f   }, /* 3 Jaune        */
		{ 0.28f,   0.60f,   0.12f   }, /* 4 Vert-Jaune   */
		{ 0.20f,   0.65f,   0.15f   }, /* 5 Vert         */
		{ 0.13f,   0.52f,   0.35f   }, /* 6 Bleu-Vert    */
		{ 0.10f,   0.35f,   0.55f   }, /* 7 Bleu         */
		{ 0.35f,   0.18f,   0.47f   }, /* 8 Violet       */
	};

	/* Mélangeur de canaux N&B style Lightroom/ART, SÉLECTIF par couleur.
	 * Curseurs 0–200, neutre 100. Poids d'un canal au neutre = coeff de
	 * luminance du filtre coloré (aspect naturel). Chaque curseur ajoute un
	 * débattement UNIFORME ±SWING à son canal — identique pour R, V et B —
	 * pour que les trois aient la même autorité (sinon le bleu, coeff ~0.07,
	 * paraît inerte). La normalisation par la somme des poids est essentielle :
	 *   - un pixel GRIS NEUTRE (r≈g≈b) reste inchangé → pas de variation
	 *     globale de luminosité (sinon le curseur agit comme une expo) ;
	 *   - seuls les pixels où une couleur domine se décalent → effet CIBLÉ. */
	#define BW_SWING 0.5f
	(void) bw_o; (void) bw_y; (void) bw_c; (void) bw_v;
	const gint fi = CLAMP(filter, 0, 8);
	const gfloat wR = filters[fi][0] + (bw_r - 100.0f) / 100.0f * BW_SWING;
	const gfloat wG = filters[fi][1] + (bw_g - 100.0f) / 100.0f * BW_SWING;
	const gfloat wB = filters[fi][2] + (bw_b - 100.0f) / 100.0f * BW_SWING;
	gfloat sum = wR + wG + wB;
	if (sum < 0.1f) sum = 0.1f;          /* garde-fou : pas d'inversion de signe */
	const gfloat inv_sum = 1.0f / sum;

	const gint W = img->w, H = img->h, ps = img->pixelsize;

	for (gint y = 0; y < H; y++)
	{
		gushort *p = GET_PIXEL(img, 0, y);
		for (gint x = 0; x < W; x++, p += ps)
		{
			const gfloat r = p[R] / 65535.0f;
			const gfloat g = p[G] / 65535.0f;
			const gfloat b = p[B] / 65535.0f;
			gfloat gray = (r * wR + g * wG + b * wB) * inv_sum * 65535.0f;
			gushort gv = (gushort) CLAMP(gray, 0.0f, 65535.0f);
			p[R] = p[G] = p[B] = gv;
		}
	}
	#undef BW_SWING
}

/* ------------------------------------------------------------------ */
/* Suppression du voile — dark channel prior conservateur             */
/* ------------------------------------------------------------------ */
/*
 * Modèle : I = J·t + A·(1-t)  ⟹  J = (I-A)/t + A
 *
 * Améliorations vs v1 pour éviter noircissement et halos :
 *  • min-filter 3×3 sur le canal sombre → transmission lissée, pas de halos
 *  • omega = 0.7 (conservateur, < 0.95 du papier original)
 *  • t_min = 0.4 → empêche les corrections extrêmes
 *  • strength = facteur de mélange : output = original + blend*(dehazed-original)
 *  • A estimé au 99,9e centile du canal sombre lissé
 */
static void
apply_dehaze(RS_IMAGE16 *img, gfloat strength, gfloat saturation)
{
	if (strength < 0.001f && fabsf(saturation) < 0.001f) return;

	const gint W   = img->w;
	const gint H   = img->h;
	const gint ps  = img->pixelsize;
	const gint rs  = img->rowstride;
	const float blend = strength / 100.0f;

	/* ── Carte du canal sombre (espace linéaire) ── */
	gfloat *dmap = g_new(gfloat, W * H);
	for (gint y = 0; y < H; y++) {
		const gushort *row = img->pixels + y * rs;
		for (gint x = 0; x < W; x++) {
			const gushort *px = row + x * ps;
			dmap[y*W + x] = fminf(to_linear((float)px[0]),
			                      fminf(to_linear((float)px[1]),
			                            to_linear((float)px[2])));
		}
	}

	/* Min-filter 3×3 séparable (H puis V) → supprime les halos de bord */
	gfloat *tmp = g_new(gfloat, W * H);
	for (gint y = 0; y < H; y++)           /* passe horizontale */
		for (gint x = 0; x < W; x++) {
			float m = dmap[y*W + x];
			if (x > 0)   m = fminf(m, dmap[y*W + x-1]);
			if (x < W-1) m = fminf(m, dmap[y*W + x+1]);
			tmp[y*W + x] = m;
		}
	for (gint y = 0; y < H; y++)           /* passe verticale */
		for (gint x = 0; x < W; x++) {
			float m = tmp[y*W + x];
			if (y > 0)   m = fminf(m, tmp[(y-1)*W + x]);
			if (y < H-1) m = fminf(m, tmp[(y+1)*W + x]);
			dmap[y*W + x] = m;
		}
	g_free(tmp);

	/* Lumière atmosphérique A : 99,9e centile du canal sombre lissé */
	guint hist[1024] = {0};
	for (gint i = 0; i < W * H; i++)
		hist[CLAMP((gint)(dmap[i] * 1023.0f), 0, 1023)]++;
	float A = 0.7f;
	glong cnt = 0, thr = MAX(1, (glong)W * H / 1000);
	for (gint i = 1023; i >= 0; i--) {
		cnt += hist[i];
		if (cnt >= thr) { A = (float)i / 1023.0f; break; }
	}
	A = fmaxf(A, 0.35f);    /* plancher : évite sur-correction sur images sombres */

	/* ── Correction par pixel + mélange avec l'original ── */
	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + y * rs;
		for (gint x = 0; x < W; x++) {
			gushort *px = row + x * ps;
			float r = to_linear((float)px[0]);
			float g = to_linear((float)px[1]);
			float b = to_linear((float)px[2]);

			/* Transmission : omega=0.95, t_min=0.20 */
			float t = fmaxf(1.0f - 0.95f * fminf(dmap[y*W+x] / A, 1.0f), 0.20f);

			/* Valeurs sans voile */
			float Jr = (r - A) / t + A;
			float Jg = (g - A) / t + A;
			float Jb = (b - A) / t + A;

			/* Mélange progressif : strength=0 → aucun effet */
			r = r + blend * (Jr - r);
			g = g + blend * (Jg - g);
			b = b + blend * (Jb - b);

			/* Correction de saturation (en lumière linéaire) */
			if (fabsf(saturation) >= 0.001f) {
				float sat_f = 1.0f + saturation / 100.0f;
				float lum = 0.2126f*r + 0.7152f*g + 0.0722f*b;
				r = lum + (r - lum) * sat_f;
				g = lum + (g - lum) * sat_f;
				b = lum + (b - lum) * sat_f;
			}

			px[0] = (gushort)CLAMP((gint)(to_srgb(CLAMP(r,0.f,1.f))+0.5f),0,65535);
			px[1] = (gushort)CLAMP((gint)(to_srgb(CLAMP(g,0.f,1.f))+0.5f),0,65535);
			px[2] = (gushort)CLAMP((gint)(to_srgb(CLAMP(b,0.f,1.f))+0.5f),0,65535);
		}
	}
	g_free(dmap);
}

/* ------------------------------------------------------------------ */
/* Argentico — inversion négatif → positif (d'après ART/filmnegative)  */
/* ------------------------------------------------------------------ */
/*
 * Opère en ESPACE DE TRAVAIL (post-DCP, ProPhoto), comme le film negative
 * d'ART en mode WORKING : là, « canaux égaux = neutre », donc un négatif N&B
 * redevient gris pur et la pioche neutralise réellement le voile d'un négatif
 * couleur. (Avant, en espace capteur brut, la matrice DCP en aval recolorait
 * le gris.) Loi de puissance par canal : sortie = mult · entrée^exp.
 */

static inline gushort
argentico_clip_f(float f)
{
	if (f <= 0.0f) return 0;
	if (f >= 65535.0f) return 65535;
	return (gushort)(f + 0.5f);
}

static guint
argentico_hist_median(const guint *h, guint64 total)
{
	guint64 half = total / 2, acc = 0;
	gint v;
	for (v = 0; v < 65536; v++) {
		acc += h[v];
		if (acc >= half) return (guint)v;
	}
	return 0;
}

/* Niveau moyen cible (médiane → ce gris à exposure=0). L'Exposition le décale ;
 * la pente des verts (greenExp) fait alors du CONTRASTE pur autour de lui. */
#define ARGENTICO_MID 11800.0f   /* ≈ 0.18 × 65535 */

static void
apply_argentico(RS_IMAGE16 *img, gfloat green_exp, gfloat red_ratio, gfloat blue_ratio,
                gfloat exposure, gfloat ref_r, gfloat ref_g, gfloat ref_b)
{
	const gint W = img->w, H = img->h, ps = img->pixelsize, rs = img->rowstride;

	const float rexp = -(green_exp * red_ratio);
	const float gexp = -green_exp;
	const float bexp = -(green_exp * blue_ratio);

	/* Médianes par canal (zone centrale, bordure 20% coupée) : servent toujours
	 * d'ancre de LUMINOSITÉ (niveau moyen). */
	guint *hist = g_new0(guint, 3 * 65536);
	guint *hr = hist, *hg = hist + 65536, *hb = hist + 2 * 65536;
	gint bx = W / 5, by = H / 5;
	gint x0 = bx, x1 = W - bx, y0 = by, y1 = H - by;
	if (x1 <= x0) { x0 = 0; x1 = W; }
	if (y1 <= y0) { y0 = 0; y1 = H; }
	for (gint y = y0; y < y1; y++) {
		gushort *row = img->pixels + y * rs;
		for (gint x = x0; x < x1; x++) {
			gushort *px = row + x * ps;
			hr[px[0]]++; hg[px[1]]++; hb[px[2]]++;
		}
	}
	guint64 total = (guint64)(x1 - x0) * (guint64)(y1 - y0);
	const float med_r = (float)MAX(argentico_hist_median(hr, total), 1u);
	const float med_g = (float)MAX(argentico_hist_median(hg, total), 1u);
	const float med_b = (float)MAX(argentico_hist_median(hb, total), 1u);
	g_free(hist);

	/* Référence de COULEUR : tache piquée si dispo (→ gris exact, neutralise le
	 * voile), sinon médianes. */
	float refin_r, refin_g, refin_b;
	if (ref_g > 0.0f && ref_r > 0.0f && ref_b > 0.0f)
	{
		refin_r = ref_r; refin_g = ref_g; refin_b = ref_b;
	}
	else
	{
		refin_r = med_r; refin_g = med_g; refin_b = med_b;
	}

	/* refout de base arbitraire (s'annule dans le rescale ci-dessous) ; on garde
	 * une valeur saine pour les calculs. */
	const float refout = 65535.0f / 24.0f;
	float rmult = refout / powf(refin_r, rexp);
	float gmult = refout / powf(refin_g, gexp);
	float bmult = refout / powf(refin_b, bexp);

	/* Ancre de luminosité : on rescale (facteur ACHROMATIQUE, donc sans toucher
	 * la couleur) pour que la luminance de la médiane tombe sur ARGENTICO_MID,
	 * décalée par l'Exposition. Résultat : greenExp = contraste pur (la médiane
	 * ne bouge plus), Exposition = luminosité globale. */
	const float Mr = rmult * powf(med_r, rexp);
	const float Mg = gmult * powf(med_g, gexp);
	const float Mb = bmult * powf(med_b, bexp);
	float M = 0.2126f * Mr + 0.7152f * Mg + 0.0722f * Mb;
	if (M < 1e-6f) M = 1e-6f;
	const float target = ARGENTICO_MID * exp2f(exposure);
	float s = target / M;
	s = CLAMP(s, 1e-4f, 1e4f);
	rmult *= s; gmult *= s; bmult *= s;

	gushort *lutr = g_new(gushort, 65536);
	gushort *lutg = g_new(gushort, 65536);
	gushort *lutb = g_new(gushort, 65536);
	for (gint v = 0; v < 65536; v++) {
		lutr[v] = argentico_clip_f(rmult * powf((float)v, rexp));
		lutg[v] = argentico_clip_f(gmult * powf((float)v, gexp));
		lutb[v] = argentico_clip_f(bmult * powf((float)v, bexp));
	}

	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + y * rs;
		for (gint x = 0; x < W; x++) {
			gushort *px = row + x * ps;
			px[0] = lutr[px[0]];
			px[1] = lutg[px[1]];
			px[2] = lutb[px[2]];
		}
	}
	g_free(lutr); g_free(lutg); g_free(lutb);
}

/* ------------------------------------------------------------------ */
/* Égaliseur de tons par bandes (d'après ART/iptoneequalizer.cc,      */
/* lui-même adapté du tone equalizer de darktable, A. Pierre 2018)    */
/* ------------------------------------------------------------------ */
/*
 * 5 bandes utilisateur [-100;100] (noirs/ombres/moyens/clairs/blancs).
 * Chaque bande pilote une plage d'exposition (EV) via 12 gaussiennes
 * centrées de -16 à +6 EV, de σ≈2 EV (partition de l'unité sur [-14;4],
 * d'où correction ≈ 1 quand toutes les bandes sont à zéro).
 *
 * Pour chaque luminance linéaire Y :
 *   luma = log2(Y) - pivot,  borné à [-14;4]
 *   correction = Σ gauss(centre_c, luma) · factor_c / w_sum
 *   RGB_linéaire ×= correction
 *
 * factor_c = 2^(bande/100 · f) convertit la bande en gain (f dépend du
 * signe, comme ART : asymétrie clairs/sombres par bande). La correction
 * ne dépend que de Y → on la précalcule dans une LUT (pas de 12
 * gaussiennes par pixel). Travail en lumière linéaire (to_linear/to_srgb).
 */
static void
apply_toneeq(RS_IMAGE16 *img,
             gfloat b0, gfloat b1, gfloat b2, gfloat b3, gfloat b4,
             gfloat pivot)
{
	static const float centers[12] = {
		-16.f,-14.f,-12.f,-10.f,-8.f,-6.f,-4.f,-2.f,0.f,2.f,4.f,6.f
	};
	#define TE_CONV(v, lo, hi) exp2f((v)/100.f * ((v) < 0.f ? (lo) : (hi)))
	const float factors[12] = {
		TE_CONV(b0, 2.f, 3.f),   /* -16 EV */
		TE_CONV(b0, 2.f, 3.f),   /* -14 EV */
		TE_CONV(b0, 2.f, 3.f),   /* -12 EV */
		TE_CONV(b0, 2.f, 3.f),   /* -10 EV */
		TE_CONV(b0, 2.f, 3.f),   /*  -8 EV */
		TE_CONV(b1, 2.f, 3.f),   /*  -6 EV */
		TE_CONV(b2, 2.5f, 2.5f), /*  -4 EV */
		TE_CONV(b3, 3.f, 2.f),   /*  -2 EV */
		TE_CONV(b4, 3.f, 2.f),   /*   0 EV */
		TE_CONV(b4, 3.f, 2.f),   /*   2 EV */
		TE_CONV(b4, 3.f, 2.f),   /*   4 EV */
		TE_CONV(b4, 3.f, 2.f)    /*   6 EV */
	};
	#undef TE_CONV

	/* Normalisation : somme des gaussiennes évaluée à luma=0 (comme ART) */
	float w_sum = 0.f;
	for (int i = 0; i < 12; i++)
		w_sum += expf(-(centers[i]*centers[i]) / 4.0f);

	/* LUT : correction = f(luminance linéaire ∈ [0;1]) */
	#define TE_LUT 4096
	float lut[TE_LUT];
	const float luma_lo = -14.f, luma_hi = 4.f;
	for (int i = 0; i < TE_LUT; i++) {
		float Y = (float)i / (TE_LUT - 1);
		float luma = log2f(fmaxf(Y, 1e-9f)) - pivot;
		luma = CLAMP(luma, luma_lo, luma_hi);
		float corr = 0.f;
		for (int c = 0; c < 12; c++) {
			float d = luma - centers[c];
			corr += expf(-(d*d) / 4.0f) * factors[c];
		}
		lut[i] = corr / w_sum;
	}

	const gint W = img->w, H = img->h, ps = img->pixelsize, rs = img->rowstride;
	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + y * rs;
		for (gint x = 0; x < W; x++) {
			gushort *px = row + x * ps;
			float r = to_linear((float)px[0]);
			float g = to_linear((float)px[1]);
			float b = to_linear((float)px[2]);
			float Y = 0.2126f*r + 0.7152f*g + 0.0722f*b;
			int idx = (int)(CLAMP(Y, 0.f, 1.f) * (TE_LUT - 1) + 0.5f);
			float corr = lut[idx];
			px[0] = (gushort)CLAMP((gint)(to_srgb(CLAMP(r*corr,0.f,1.f))+0.5f),0,65535);
			px[1] = (gushort)CLAMP((gint)(to_srgb(CLAMP(g*corr,0.f,1.f))+0.5f),0,65535);
			px[2] = (gushort)CLAMP((gint)(to_srgb(CLAMP(b*corr,0.f,1.f))+0.5f),0,65535);
		}
	}
	#undef TE_LUT
}

/* ------------------------------------------------------------------ */
/* Correction couleur — roues 3 voies (ombres / médians / hautes)      */
/* ------------------------------------------------------------------ */
/*
 * Étalonnage coloriste façon lift/gamma/gain. Pour chaque pixel on calcule
 * sa luminance L∈[0,1] et trois poids de zone (base de Bernstein, partition
 * de l'unité, neutre à plat) :
 *   ws=(1-L)²   wm=2L(1-L)   wh=L²   (ws+wm+wh = 1)
 * Chaque roue (x,y) donne un décalage RGB ÉQUILIBRÉ (somme ~0 → chroma pure,
 * pas de dérive de luminance) : shift = radius·[cos θ, cos(θ-120°), cos(θ-240°)].
 * On ajoute la somme pondérée des décalages couleur, puis un décalage de
 * luminance par zone (lift/gamma/gain simplifié = offset additif pondéré). */

/* Convertit une position de roue (x,y ∈ [-1,1]) en décalage RGB équilibré. */
static inline void
cw_shift(gfloat x, gfloat y, gfloat *sr, gfloat *sg, gfloat *sb)
{
	float radius = sqrtf(x*x + y*y);
	if (radius > 1.0f) radius = 1.0f;
	if (radius < 1e-4f) { *sr = *sg = *sb = 0.0f; return; }
	float angle = atan2f(y, x);
	*sr = radius * cosf(angle);
	*sg = radius * cosf(angle - 2.0943951f); /* -120° */
	*sb = radius * cosf(angle - 4.1887902f); /* -240° */
}

static void
apply_colorcorrection(RS_IMAGE16 *img, RSEffects *e)
{
	const float COLOR_STRENGTH = 0.35f; /* amplitude max du décalage couleur */
	const float LUM_STRENGTH   = 0.30f; /* amplitude max du décalage luminance */

	float sr_s, sg_s, sb_s, sr_m, sg_m, sb_m, sr_h, sg_h, sb_h;
	cw_shift(e->cw_shadows_x, e->cw_shadows_y, &sr_s, &sg_s, &sb_s);
	cw_shift(e->cw_mid_x,     e->cw_mid_y,     &sr_m, &sg_m, &sb_m);
	cw_shift(e->cw_high_x,    e->cw_high_y,    &sr_h, &sg_h, &sb_h);

	const float ls = e->cw_shadows_lum, lm = e->cw_mid_lum, lh = e->cw_high_lum;

	const gint W = img->w, H = img->h, ps = img->pixelsize, rs = img->rowstride;
	for (gint yy = 0; yy < H; yy++)
	{
		gushort *row = img->pixels + yy * rs;
		for (gint xx = 0; xx < W; xx++)
		{
			gushort *px = row + xx * ps;
			float rf = px[0] / 65535.0f, gf = px[1] / 65535.0f, bf = px[2] / 65535.0f;
			float L = 0.2126f*rf + 0.7152f*gf + 0.0722f*bf;
			float ws = (1.0f - L) * (1.0f - L);
			float wh = L * L;
			float wm = 2.0f * L * (1.0f - L);

			float dr = ws*sr_s + wm*sr_m + wh*sr_h;
			float dg = ws*sg_s + wm*sg_m + wh*sg_h;
			float db = ws*sb_s + wm*sb_m + wh*sb_h;
			float dl = ws*ls + wm*lm + wh*lh;

			rf += COLOR_STRENGTH*dr + LUM_STRENGTH*dl;
			gf += COLOR_STRENGTH*dg + LUM_STRENGTH*dl;
			bf += COLOR_STRENGTH*db + LUM_STRENGTH*dl;

			px[0] = (gushort) CLAMP((gint)(rf*65535.0f + 0.5f), 0, 65535);
			px[1] = (gushort) CLAMP((gint)(gf*65535.0f + 0.5f), 0, 65535);
			px[2] = (gushort) CLAMP((gint)(bf*65535.0f + 0.5f), 0, 65535);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Entrée principale                                                   */
/* ------------------------------------------------------------------ */

static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSEffects *effects = RS_EFFECTS(filter);
	RSFilterResponse *previous = rs_filter_get_image(filter->previous, request);

	if (!previous) return NULL;

	const gboolean need_ar = effects->argentico_enabled;
	const gboolean need_dh = (effects->dehaze_strength >= 0.001f || fabsf(effects->dehaze_saturation) >= 0.001f);
	const gboolean need_te = effects->toneeq_enabled &&
		(fabsf(effects->toneeq_band0) >= 0.5f || fabsf(effects->toneeq_band1) >= 0.5f ||
		 fabsf(effects->toneeq_band2) >= 0.5f || fabsf(effects->toneeq_band3) >= 0.5f ||
		 fabsf(effects->toneeq_band4) >= 0.5f);
	const gboolean need_bw = effects->bw_enabled;
	const gboolean need_sl = (effects->softlight_strength >= 0.001f);
	const gboolean need_vg = (fabsf(effects->art_vignette_strength) >= 0.001f);
	const gboolean need_cc = effects->colorwheels_enabled &&
		(fabsf(effects->cw_shadows_x) >= 0.001f || fabsf(effects->cw_shadows_y) >= 0.001f ||
		 fabsf(effects->cw_shadows_lum) >= 0.001f ||
		 fabsf(effects->cw_mid_x) >= 0.001f || fabsf(effects->cw_mid_y) >= 0.001f ||
		 fabsf(effects->cw_mid_lum) >= 0.001f ||
		 fabsf(effects->cw_high_x) >= 0.001f || fabsf(effects->cw_high_y) >= 0.001f ||
		 fabsf(effects->cw_high_lum) >= 0.001f);

	if (!need_ar && !need_dh && !need_te && !need_bw && !need_sl && !need_vg && !need_cc)
		return previous;

	RS_IMAGE16 *img = rs_filter_response_get_image(previous);
	if (!img) return previous;

	RSFilterResponse *response = rs_filter_response_clone(previous);
	RS_IMAGE16 *out = rs_image16_copy(img, TRUE);
	rs_filter_response_set_image(response, out);
	/* rs_filter_response_get_image() renvoie une référence : on la relâche
	 * dès que la copie est faite (sinon fuite d'un RS_IMAGE16 par rendu). */
	g_object_unref(img);
	g_object_unref(previous);

	if (need_ar)
		apply_argentico(out, effects->argentico_green_exp, effects->argentico_red_ratio,
		                effects->argentico_blue_ratio, effects->argentico_exposure,
		                effects->argentico_ref_r, effects->argentico_ref_g, effects->argentico_ref_b);

	if (need_dh)
		apply_dehaze(out, effects->dehaze_strength, effects->dehaze_saturation);

	if (need_te)
		apply_toneeq(out, effects->toneeq_band0, effects->toneeq_band1,
		             effects->toneeq_band2, effects->toneeq_band3,
		             effects->toneeq_band4, effects->toneeq_pivot);

	if (need_cc)
		apply_colorcorrection(out, effects);

	if (need_bw)
		apply_bw(out, effects->bw_filter,
		         effects->bw_red, effects->bw_orange, effects->bw_yellow,
		         effects->bw_green, effects->bw_cyan, effects->bw_blue, effects->bw_violet);

	if (need_sl)
		apply_softlight(out, effects->softlight_strength);

	if (need_vg)
		apply_vignette(out, effects->art_vignette_strength,
		               effects->art_vignette_feather,
		               effects->art_vignette_roundness);

	g_object_unref(out);
	return response;
}

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	rs_effects_get_type(G_TYPE_MODULE(plugin));
}
