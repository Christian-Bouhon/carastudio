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
	gfloat drc_amount;
	gfloat drc_threshold;
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
	/* Égaliseur de couleurs (color zones) — nœuds (x=teinte, y=valeur) par canal,
	   triés par x, courbe périodique. Max 32 nœuds/canal. */
	gboolean hsl_enabled;
	gfloat hsl_hx[32], hsl_hy[32]; gint hsl_hn;
	gfloat hsl_sx[32], hsl_sy[32]; gint hsl_sn;
	gfloat hsl_lx[32], hsl_ly[32]; gint hsl_ln;
	/* Courbes RVB par canal — LUT 16 bits pré-calculées (NULL = canal identité) */
	gushort *rgb_lut[3];
	gboolean rgb_active;
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

	g_free(e->rgb_lut[0]); e->rgb_lut[0] = NULL;
	g_free(e->rgb_lut[1]); e->rgb_lut[1] = NULL;
	g_free(e->rgb_lut[2]); e->rgb_lut[2] = NULL;
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

/* Parse "x0 y0 x1 y1 …" en nœuds (xs,ys), n = nombre de nœuds. */
static void
hsl_parse_nodes(const gchar *str, gfloat *xs, gfloat *ys, gint *n, gint maxn)
{
	*n = 0;
	if (!str) return;
	gchar **tok = g_strsplit_set(str, " ,", -1);
	gint i, k = 0;
	gfloat buf[2] = {0, 0};
	for (i = 0; tok[i]; i++)
	{
		if (tok[i][0] == '\0') continue;
		buf[k & 1] = (gfloat) g_ascii_strtod(tok[i], NULL);
		k++;
		if (!(k & 1) && *n < maxn) { xs[*n] = buf[0]; ys[*n] = buf[1]; (*n)++; }
	}
	g_strfreev(tok);
}

/* (Re)construit la LUT 16 bits d'un canal (0=R/1=V/2=B) depuis les nœuds de la
 * courbe RVB. Les pixels sont ici en espace sRGB (gamma-encodé) → la courbe
 * s'applique directement, sans correction gamma. Canal en identité → LUT=NULL. */
static void
build_rgb_lut(RSEffects *effects, RSSettings *settings, gint ch)
{
	gfloat *knots = rs_settings_get_rgb_curve_knots(settings, ch);
	gint nknots = rs_settings_get_rgb_curve_nknots(settings, ch);

	gboolean identity = (!knots || nknots < 2 ||
		(nknots == 2 && knots[0] == 0.0f && knots[1] == 0.0f &&
		                knots[2] == 1.0f && knots[3] == 1.0f));
	if (identity)
	{
		g_free(effects->rgb_lut[ch]);
		effects->rgb_lut[ch] = NULL;
		g_free(knots);
		return;
	}

	if (!effects->rgb_lut[ch])
		effects->rgb_lut[ch] = g_new(gushort, 65536);

	RSSpline *spline = rs_spline_new(knots, nknots, NATURAL);
	gfloat *sampled = g_new(gfloat, 65536);
	rs_spline_sample(spline, sampled, 65536);
	g_object_unref(spline);

	gint i;
	for (i = 0; i < 65536; i++)
	{
		gfloat v = sampled[i] * 65535.0f;
		effects->rgb_lut[ch][i] = (v <= 0.0f) ? 0 : (v >= 65535.0f) ? 65535 : (gushort)(v + 0.5f);
	}
	g_free(sampled);
	g_free(knots);
}

static void
settings_changed(RSSettings *settings, RSSettingsMask mask, RSEffects *effects)
{
	g_object_get(settings,
		"dehaze-strength",       &effects->dehaze_strength,
		"drc-amount",            &effects->drc_amount,
		"drc-threshold",         &effects->drc_threshold,
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
		"hsl-enabled",           &effects->hsl_enabled,
		NULL);

	/* Égaliseur de couleurs : parse les 3 courbes (chaînes de 8 bandes). */
	{
		gchar *hs = NULL, *ss = NULL, *ls = NULL;
		g_object_get(settings, "hsl-hue-curve", &hs, "hsl-sat-curve", &ss,
		             "hsl-lum-curve", &ls, NULL);
		hsl_parse_nodes(hs, effects->hsl_hx, effects->hsl_hy, &effects->hsl_hn, 32);
		hsl_parse_nodes(ss, effects->hsl_sx, effects->hsl_sy, &effects->hsl_sn, 32);
		hsl_parse_nodes(ls, effects->hsl_lx, effects->hsl_ly, &effects->hsl_ln, 32);
		g_free(hs); g_free(ss); g_free(ls);
	}

	/* Courbes RVB par canal : (re)construire les LUT + drapeau d'activité. */
	build_rgb_lut(effects, settings, 0);
	build_rgb_lut(effects, settings, 1);
	build_rgb_lut(effects, settings, 2);
	effects->rgb_active = (effects->rgb_lut[0] || effects->rgb_lut[1] || effects->rgb_lut[2]);

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
/* Égaliseur de couleurs (color zones) — 3 courbes f(teinte)          */
/* ------------------------------------------------------------------ */
/*
 * D'après l'égaliseur de couleurs d'ART (rtengine/iphsl.cc) : 3 courbes
 * indexées par la TEINTE du pixel, qui décalent la teinte, multiplient la
 * saturation et la luminance. Ici 8 bandes par canal, interpolation périodique,
 * en espace TSL (HSL). Permet de cibler une couleur (jaune→bleu en teinte,
 * doper les verts en saturation, éclaircir les rouges en luminance). */

static void
cz_rgb2hsl(float r, float g, float b, float *h, float *s, float *l)
{
	float mx = MAX(MAX(r, g), b), mn = MIN(MIN(r, g), b), d = mx - mn;
	*l = (mx + mn) * 0.5f;
	if (d < 1e-6f) { *h = 0; *s = 0; return; }
	*s = (*l > 0.5f) ? d/(2.0f - mx - mn) : d/(mx + mn);
	float hh;
	if (mx == r)      hh = (g - b)/d + (g < b ? 6.0f : 0.0f);
	else if (mx == g) hh = (b - r)/d + 2.0f;
	else              hh = (r - g)/d + 4.0f;
	*h = hh / 6.0f;
}

static inline float
cz_hue2rgb(float p, float q, float t)
{
	if (t < 0) t += 1; if (t > 1) t -= 1;
	if (t < 1.0f/6) return p + (q - p)*6*t;
	if (t < 1.0f/2) return q;
	if (t < 2.0f/3) return p + (q - p)*(2.0f/3 - t)*6;
	return p;
}

static void
cz_hsl2rgb(float h, float s, float l, float *r, float *g, float *b)
{
	if (s < 1e-6f) { *r = *g = *b = l; return; }
	float q = (l < 0.5f) ? l*(1 + s) : l + s - l*s;
	float p = 2*l - q;
	*r = cz_hue2rgb(p, q, h + 1.0f/3);
	*g = cz_hue2rgb(p, q, h);
	*b = cz_hue2rgb(p, q, h - 1.0f/3);
}

/* Interpolation périodique d'une courbe à nœuds (xs triés dans [0,1)) à la
   teinte h∈[0,1). Le segment de bouclage relie le dernier nœud au premier. */
static inline float
cz_interp(const gfloat *xs, const gfloat *ys, gint n, float h)
{
	if (n <= 0) return 0.0f;
	if (n == 1) return ys[0];
	if (h >= xs[0] && h < xs[n-1])
	{
		gint i = 0;
		while (i < n-1 && h >= xs[i+1]) i++;
		float t = (h - xs[i]) / (xs[i+1] - xs[i]);
		return ys[i]*(1.0f - t) + ys[i+1]*t;
	}
	float seg = xs[0] + 1.0f - xs[n-1];
	float pos = (h >= xs[n-1]) ? (h - xs[n-1]) : (h + 1.0f - xs[n-1]);
	float t = (seg > 1e-6f) ? pos / seg : 0.0f;
	return ys[n-1]*(1.0f - t) + ys[0]*t;
}

static void
apply_colorzones(RS_IMAGE16 *img, RSEffects *e)
{
	const float K_HUE = 0.5f; /* décalage teinte max = ±0,5 tour (±180°) */
	const float K_SAT = 1.0f; /* sat *= 1 + val   (val=−1 → gris, +1 → ×2) */
	const float K_LUM = 0.5f; /* lum *= 1 + val·0,5 */
	const gint W = img->w, H = img->h, ps = img->pixelsize, rs = img->rowstride;
	for (gint yy = 0; yy < H; yy++)
	{
		gushort *row = img->pixels + yy * rs;
		for (gint xx = 0; xx < W; xx++)
		{
			gushort *px = row + xx * ps;
			float r = px[0]/65535.0f, g = px[1]/65535.0f, b = px[2]/65535.0f;
			float h, s, l;
			cz_rgb2hsl(r, g, b, &h, &s, &l);
			float dh = cz_interp(e->hsl_hx, e->hsl_hy, e->hsl_hn, h);
			float ds = cz_interp(e->hsl_sx, e->hsl_sy, e->hsl_sn, h);
			float dl = cz_interp(e->hsl_lx, e->hsl_ly, e->hsl_ln, h);
			h += dh * K_HUE; h -= floorf(h);
			s *= (1.0f + ds * K_SAT); s = CLAMP(s, 0.0f, 1.0f);
			l *= (1.0f + dl * K_LUM); l = CLAMP(l, 0.0f, 1.0f);
			cz_hsl2rgb(h, s, l, &r, &g, &b);
			px[0] = (gushort) CLAMP((gint)(r*65535.0f + 0.5f), 0, 65535);
			px[1] = (gushort) CLAMP((gint)(g*65535.0f + 0.5f), 0, 65535);
			px[2] = (gushort) CLAMP((gint)(b*65535.0f + 0.5f), 0, 65535);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Entrée principale                                                   */
/* ------------------------------------------------------------------ */

/* Applique les courbes RVB par canal via les LUT pré-calculées. */
static void
apply_rgb_curves(RS_IMAGE16 *img, RSEffects *e)
{
	const gint W = img->w, H = img->h, ps = img->pixelsize;
	const gushort *lr = e->rgb_lut[0], *lg = e->rgb_lut[1], *lb = e->rgb_lut[2];
	gint y, x;
	for (y = 0; y < H; y++)
	{
		gushort *p = GET_PIXEL(img, 0, y);
		for (x = 0; x < W; x++, p += ps)
		{
			if (lr) p[R] = lr[p[R]];
			if (lg) p[G] = lg[p[G]];
			if (lb) p[B] = lb[p[B]];
		}
	}
}

/* ------------------------------------------------------------------ */
/* Compresseur de plage dynamique (DRC) — tone mapping LOCAL           */
/* ------------------------------------------------------------------ */
/*
 * Décomposition base/détail en domaine LOG (approche Durand-Dorsey
 * simplifiée, base gaussienne large au lieu du bilatéral) :
 *   1. luminance → log
 *   2. base = flou large (grande échelle d'éclairement)
 *   3. détail = log − base  (micro-contraste, textures)
 *   4. on COMPRIME la base vers sa moyenne (réduit l'écart clair/sombre :
 *      remonte les ombres ET retient les hautes lumières = bidirectionnel)
 *   5. on réinjecte le détail intact (éventuellement rehaussé) → local
 *   6. exp() → on remet la luminance, la couleur est préservée (scale RGB)
 *
 * « Ampleur » = force de compression de la base. « Seuil » = rehaussement
 * du micro-contraste (détail). Le rayon du flou est proportionnel à la
 * taille d'image → rendu identique en aperçu et à pleine résolution.
 */

/* Flou « boîte » séparable par somme glissante (O(1) par pixel, rayon libre) */
static void
cs_box_blur_h(const float *src, float *dst, gint W, gint H, gint r)
{
	const float norm = 1.0f / (2 * r + 1);
	for (gint y = 0; y < H; y++) {
		const float *s = src + (gsize)y * W;
		float *d = dst + (gsize)y * W;
		float sum = 0.0f;
		gint x;
		for (x = -r; x <= r; x++) sum += s[CLAMP(x, 0, W - 1)];
		for (x = 0; x < W; x++) {
			d[x] = sum * norm;
			sum += s[CLAMP(x + r + 1, 0, W - 1)] - s[CLAMP(x - r, 0, W - 1)];
		}
	}
}

static void
cs_box_blur_v(const float *src, float *dst, gint W, gint H, gint r)
{
	const float norm = 1.0f / (2 * r + 1);
	for (gint x = 0; x < W; x++) {
		float sum = 0.0f;
		gint y;
		for (y = -r; y <= r; y++) sum += src[(gsize)CLAMP(y, 0, H - 1) * W + x];
		for (y = 0; y < H; y++) {
			dst[(gsize)y * W + x] = sum * norm;
			sum += src[(gsize)CLAMP(y + r + 1, 0, H - 1) * W + x]
			     - src[(gsize)CLAMP(y - r, 0, H - 1) * W + x];
		}
	}
}

static void
apply_drc(RS_IMAGE16 *img, gfloat amount, gfloat threshold)
{
	if (amount < 0.001f) return;

	const gint W  = img->w;
	const gint H  = img->h;
	const gint ps = img->pixelsize;
	const gint rs = img->rowstride;
	const gsize N = (gsize)W * H;

	/* Force de compression de la base (0 → aucun, 1 → très fort) */
	const float strength = (amount / 100.0f) * 0.85f;
	/* Rehaussement du détail piloté par le Seuil (-100→0,5× ; 0→1× ; 300→2,5×) */
	const float detail_gain = 1.0f + (threshold / 100.0f) * 0.5f;

	/* Rayon du flou = échelle relative à l'image (indépendant de la résolution) */
	gint radius = (gint)(0.06f * fminf((float)W, (float)H));
	radius = CLAMP(radius, 8, 512);

	float *loglum = g_new(float, N);   /* log-luminance */
	float *base   = g_new(float, N);   /* base (flou) */
	float *tmp    = g_new(float, N);
	if (!loglum || !base || !tmp) { g_free(loglum); g_free(base); g_free(tmp); return; }

	/* 1) log-luminance + moyenne globale de la base (approx = moyenne du log) */
	double mean = 0.0;
	for (gint y = 0; y < H; y++) {
		const gushort *row = img->pixels + (gsize)y * rs;
		for (gint x = 0; x < W; x++) {
			const gushort *px = row + x * ps;
			float r = to_linear((float)px[0]);
			float g = to_linear((float)px[1]);
			float b = to_linear((float)px[2]);
			float lum = 0.2126f*r + 0.7152f*g + 0.0722f*b;
			float ll = logf(fmaxf(lum, 1e-5f));
			loglum[(gsize)y*W + x] = ll;
			mean += ll;
		}
	}
	mean /= (double)N;

	/* 2) base = flou boîte 3× (H+V) ≈ gaussienne */
	cs_box_blur_h(loglum, base, W, H, radius); cs_box_blur_v(base, tmp, W, H, radius);
	cs_box_blur_h(tmp, base, W, H, radius);    cs_box_blur_v(base, tmp, W, H, radius);
	cs_box_blur_h(tmp, base, W, H, radius);    cs_box_blur_v(base, tmp, W, H, radius);
	/* 'tmp' contient désormais la base lissée */

	/* 3-6) recomposition par pixel */
	const float m = (float)mean;
	for (gint y = 0; y < H; y++) {
		gushort *row = img->pixels + (gsize)y * rs;
		for (gint x = 0; x < W; x++) {
			gushort *px = row + x * ps;
			float b_log = tmp[(gsize)y*W + x];              /* base */
			float d_log = loglum[(gsize)y*W + x] - b_log;   /* détail */

			/* Compression de la base VERS la moyenne (bidirectionnel) */
			float b_comp = m + (b_log - m) * (1.0f - strength);
			/* Réinjection du détail (rehaussé) */
			float l_new = b_comp + d_log * detail_gain;

			float lum_old = expf(loglum[(gsize)y*W + x]);
			float lum_new = expf(l_new);
			float scale = (lum_old > 1e-6f) ? (lum_new / lum_old) : 1.0f;

			float r = to_linear((float)px[0]) * scale;
			float g = to_linear((float)px[1]) * scale;
			float b = to_linear((float)px[2]) * scale;

			px[0] = (gushort)CLAMP((gint)(to_srgb(CLAMP(r,0.f,1.f))+0.5f),0,65535);
			px[1] = (gushort)CLAMP((gint)(to_srgb(CLAMP(g,0.f,1.f))+0.5f),0,65535);
			px[2] = (gushort)CLAMP((gint)(to_srgb(CLAMP(b,0.f,1.f))+0.5f),0,65535);
		}
	}

	g_free(loglum); g_free(base); g_free(tmp);
}

static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSEffects *effects = RS_EFFECTS(filter);
	RSFilterResponse *previous = rs_filter_get_image(filter->previous, request);

	if (!previous) return NULL;

	const gboolean need_ar = effects->argentico_enabled;
	const gboolean need_dh = (effects->dehaze_strength >= 0.001f || fabsf(effects->dehaze_saturation) >= 0.001f);
	const gboolean need_drc = (effects->drc_amount >= 0.001f);
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
	gboolean need_cz = FALSE;
	if (effects->hsl_enabled)
	{
		gint k;
		for (k = 0; k < effects->hsl_hn && !need_cz; k++)
			if (fabsf(effects->hsl_hy[k]) >= 0.001f) need_cz = TRUE;
		for (k = 0; k < effects->hsl_sn && !need_cz; k++)
			if (fabsf(effects->hsl_sy[k]) >= 0.001f) need_cz = TRUE;
		for (k = 0; k < effects->hsl_ln && !need_cz; k++)
			if (fabsf(effects->hsl_ly[k]) >= 0.001f) need_cz = TRUE;
	}

	const gboolean need_rgb = effects->rgb_active;

	if (!need_ar && !need_dh && !need_drc && !need_te && !need_bw && !need_sl && !need_vg && !need_cc && !need_cz && !need_rgb)
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

	if (need_drc)
		apply_drc(out, effects->drc_amount, effects->drc_threshold);

	if (need_te)
		apply_toneeq(out, effects->toneeq_band0, effects->toneeq_band1,
		             effects->toneeq_band2, effects->toneeq_band3,
		             effects->toneeq_band4, effects->toneeq_pivot);

	if (need_cc)
		apply_colorcorrection(out, effects);

	if (need_cz)
		apply_colorzones(out, effects);

	if (need_rgb)
		apply_rgb_curves(out, effects);

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
