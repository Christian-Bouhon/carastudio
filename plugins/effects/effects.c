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
	gboolean bw_enabled;
	gint bw_filter;
	gfloat bw_red;
	gfloat bw_orange;
	gfloat bw_yellow;
	gfloat bw_green;
	gfloat bw_cyan;
	gfloat bw_blue;
	gfloat bw_violet;
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

/* Curseur 0–200 (neutre 33) → ajustement, 0 au neutre, fort aux extrêmes. */
static inline gfloat
bw_slider_to_adjust(gfloat s)
{
	if (s >= 33.0f)
		return (s - 33.0f) / (200.0f - 33.0f) * 2.5f;  /* 33→0 ; 200→+2.5 */
	else
		return (s - 33.0f) / 33.0f * 1.5f;             /* 33→0 ; 0→−1.5   */
}

static void
apply_bw(RS_IMAGE16 *img,
         gint filter,
         gfloat bw_r, gfloat bw_o, gfloat bw_y,
         gfloat bw_g, gfloat bw_c, gfloat bw_b, gfloat bw_v)
{
	/* Étage 1 — filtre coloré : poids R/G/B de la luminance de base (somme ~1). */
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
	const gint fi = CLAMP(filter, 0, 8);
	const gfloat fR = filters[fi][0], fG = filters[fi][1], fB = filters[fi][2];

	/* Étage 2 — ajustements par teinte sur 3 ancres PRIMAIRES (Rouge 0°,
	 * Vert 120°, Bleu 240°). Chaque pixel est influencé par ses 2 primaires
	 * les plus proches → un curseur a une large portée (le rouge atteint les
	 * oranges/tons chauds, comme ART), sans zone morte entre ancres.
	 * (orange/jaune/cyan/violet ne sont pas des curseurs exposés.) */
	(void) bw_o; (void) bw_y; (void) bw_c; (void) bw_v;
	const gfloat adjR = bw_slider_to_adjust(bw_r);
	const gfloat adjG = bw_slider_to_adjust(bw_g);
	const gfloat adjB = bw_slider_to_adjust(bw_b);

	const gint W = img->w, H = img->h, ps = img->pixelsize;

	for (gint y = 0; y < H; y++)
	{
		gushort *p = GET_PIXEL(img, 0, y);
		for (gint x = 0; x < W; x++, p += ps)
		{
			const gfloat r = p[R] / 65535.0f;
			const gfloat g = p[G] / 65535.0f;
			const gfloat b = p[B] / 65535.0f;

			/* Luminance de base teintée par le filtre. */
			gfloat base = fR * r + fG * g + fB * b;

			/* Ajustement ciblé par teinte, pondéré par la saturation. */
			gfloat hue, sat;
			bw_rgb_to_hue_sat(r, g, b, &hue, &sat);

			gfloat a = 0.0f;
			if (sat > 1e-4f)
			{
				gfloat lo, hi, t;
				if (hue < 120.0f)       { lo = adjR; hi = adjG; t = hue / 120.0f; }
				else if (hue < 240.0f)  { lo = adjG; hi = adjB; t = (hue - 120.0f) / 120.0f; }
				else                    { lo = adjB; hi = adjR; t = (hue - 240.0f) / 120.0f; }
				a = (lo * (1.0f - t) + hi * t) * sat;
			}

			gfloat factor = 1.0f + a;
			if (factor < 0.0f) factor = 0.0f;

			gfloat gray = base * factor * 65535.0f;
			gushort gv = (gushort) CLAMP(gray, 0.0f, 65535.0f);
			p[R] = p[G] = p[B] = gv;
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

	const gboolean need_bw = effects->bw_enabled;
	const gboolean need_sl = (effects->softlight_strength >= 0.001f);
	const gboolean need_vg = (fabsf(effects->art_vignette_strength) >= 0.001f);

	if (!need_bw && !need_sl && !need_vg)
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
