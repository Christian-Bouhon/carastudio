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
	gfloat softlight_strength;
	gfloat art_vignette_strength;
	gfloat art_vignette_feather;
	gfloat art_vignette_roundness;
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

static void
rs_effects_class_init(RSEffectsClass *klass)
{
	RSFilterClass *filter_class = RS_FILTER_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->get_property = get_property;
	obj_class->set_property = set_property;
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
		if (e->settings)
			g_signal_handlers_disconnect_by_func(e->settings, settings_changed, e);
		e->settings = g_value_get_object(val);
		if (e->settings) {
			g_object_get(e->settings,
				"softlight-strength",    &e->softlight_strength,
				"art-vignette-strength", &e->art_vignette_strength,
				"art-vignette-feather",  &e->art_vignette_feather,
				"art-vignette-roundness",&e->art_vignette_roundness,
				NULL);
			g_signal_connect(e->settings, "settings-changed",
				G_CALLBACK(settings_changed), e);
		}
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
	}
}

static void
settings_changed(RSSettings *settings, RSSettingsMask mask, RSEffects *effects)
{
	g_object_get(settings,
		"softlight-strength",    &effects->softlight_strength,
		"art-vignette-strength", &effects->art_vignette_strength,
		"art-vignette-feather",  &effects->art_vignette_feather,
		"art-vignette-roundness",&effects->art_vignette_roundness,
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
/* Entrée principale                                                   */
/* ------------------------------------------------------------------ */

static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSEffects *effects = RS_EFFECTS(filter);
	RSFilterResponse *previous = rs_filter_get_image(filter->previous, request);

	if (!previous) return NULL;

	if (effects->softlight_strength < 0.001f &&
	    fabsf(effects->art_vignette_strength) < 0.001f)
		return previous;

	RS_IMAGE16 *img = rs_filter_response_get_image(previous);
	if (!img) return previous;

	RSFilterResponse *response = rs_filter_response_clone(previous);
	RS_IMAGE16 *out = rs_image16_copy(img, TRUE);
	rs_filter_response_set_image(response, out);
	g_object_unref(previous);

	apply_softlight(out, effects->softlight_strength);
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
