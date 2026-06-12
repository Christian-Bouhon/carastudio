/*
 * CaraStudio — plugin Argentico (simulation de négatif argentique)
 *
 * Inversion négatif → positif adaptée d'ART/RawTherapee filmnegativeproc.cc
 *   Copyright 2016-2024 the RawTherapee/ART developers — GPL v3+
 * Adaptation C pour pipeline RS_IMAGE16 CaraStudio :
 *   Copyright (C) 2026 Carafife — GPL v3+
 *
 * Position dans le pipeline : inséré tôt (après dématriçage, AVANT la
 * balance des blancs et le profil DCP), sur des données capteur linéaires.
 * Comme il précède un cache "ignore-roi", get_image reçoit l'image entière :
 * la référence (médianes) est donc calculée une fois, de façon cohérente.
 *
 * Algo (par canal) : sortie = mult · entrée^exp
 *   gexp = -greenExp ; rexp = -greenExp·redRatio ; bexp = -greenExp·blueRatio
 *   refIn  = médianes de l'image (base du négatif), bordure 20% coupée
 *   refOut = gris cible à 1/24 du max ; mult = refOut / refIn^exp
 */

#include <rawstudio.h>
#include <rs-image16.h>
#include <math.h>

#define RS_TYPE_ARGENTICO rs_argentico_get_type()
#define RS_ARGENTICO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), RS_TYPE_ARGENTICO, RSArgentico))

typedef struct {
	RSFilter parent;
	RSSettings *settings;
	gulong settings_signal_id;
	gboolean enabled;
	gfloat green_exp;
	gfloat red_ratio;
	gfloat blue_ratio;
	gfloat exposure;
} RSArgentico;

typedef struct {
	RSFilterClass parent_class;
} RSArgenticoClass;

RS_DEFINE_FILTER(rs_argentico, RSArgentico)

enum { PROP_0, PROP_SETTINGS };

static void get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec);
static void set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec);
static RSFilterResponse *get_image(RSFilter *filter, const RSFilterRequest *request);
static void settings_changed(RSSettings *settings, RSSettingsMask mask, RSArgentico *a);
static void settings_weak_notify(gpointer data, GObject *where_the_object_was);

/* Référence empruntée à la photo (non détenue) : weak-ref pour éviter un
 * déréférencement après libération au changement de photo (cf. plugin effects). */
static void
finalize(GObject *object)
{
	RSArgentico *a = RS_ARGENTICO(object);
	if (a->settings && a->settings_signal_id)
	{
		g_signal_handler_disconnect(a->settings, a->settings_signal_id);
		g_object_weak_unref(G_OBJECT(a->settings), settings_weak_notify, a);
	}
	a->settings_signal_id = 0;
	a->settings = NULL;
}

static void
rs_argentico_class_init(RSArgenticoClass *klass)
{
	RSFilterClass *filter_class = RS_FILTER_CLASS(klass);
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->get_property = get_property;
	obj_class->set_property = set_property;
	obj_class->finalize = finalize;
	filter_class->name = "CaraStudio Argentico";
	filter_class->get_image = get_image;

	g_object_class_install_property(obj_class, PROP_SETTINGS,
		g_param_spec_object("settings", "settings", "RSSettings",
			RS_TYPE_SETTINGS, G_PARAM_READWRITE));
}

static void
rs_argentico_init(RSArgentico *a)
{
	a->settings = NULL;
	a->settings_signal_id = 0;
	a->enabled = FALSE;
	a->green_exp = 1.5f;
	a->red_ratio = 1.36f;
	a->blue_ratio = 0.86f;
	a->exposure = 0.0f;
}

static void
get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec)
{
	RSArgentico *a = RS_ARGENTICO(obj);
	switch (id) {
	case PROP_SETTINGS: g_value_set_object(val, a->settings); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
	}
}

static void
set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec)
{
	RSArgentico *a = RS_ARGENTICO(obj);
	switch (id) {
	case PROP_SETTINGS:
		if (a->settings && a->settings_signal_id)
		{
			if (a->settings == g_value_get_object(val))
			{
				settings_changed(a->settings, MASK_ALL, a);
				break;
			}
			g_signal_handler_disconnect(a->settings, a->settings_signal_id);
			g_object_weak_unref(G_OBJECT(a->settings), settings_weak_notify, a);
			a->settings_signal_id = 0;
		}
		a->settings = g_value_get_object(val);
		if (a->settings) {
			a->settings_signal_id = g_signal_connect(a->settings,
				"settings-changed", G_CALLBACK(settings_changed), a);
			g_object_weak_ref(G_OBJECT(a->settings), settings_weak_notify, a);
			settings_changed(a->settings, MASK_ALL, a);
		}
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
	}
}

static void
settings_weak_notify(gpointer data, GObject *where_the_object_was)
{
	RSArgentico *a = RS_ARGENTICO(data);
	a->settings = NULL;
	a->settings_signal_id = 0;
}

static void
settings_changed(RSSettings *settings, RSSettingsMask mask, RSArgentico *a)
{
	gboolean en;
	gfloat ge, rr, br, ex;
	g_object_get(settings,
		"argentico-enabled",   &en,
		"argentico-green-exp", &ge,
		"argentico-red-ratio", &rr,
		"argentico-blue-ratio",&br,
		"argentico-exposure",  &ex,
		NULL);

	/* Argentico précède le cache de dématriçage : on n'invalide la chaîne que
	 * si un de NOS réglages a réellement changé (sinon chaque curseur en aval
	 * recalculerait tout le pipeline). */
	if (en == a->enabled && ge == a->green_exp &&
	    rr == a->red_ratio && br == a->blue_ratio && ex == a->exposure)
		return;

	a->enabled = en;
	a->green_exp = ge;
	a->red_ratio = rr;
	a->blue_ratio = br;
	a->exposure = ex;
	rs_filter_changed(RS_FILTER(a), RS_FILTER_CHANGED_PIXELDATA);
}

/* ------------------------------------------------------------------ */
/* Algo d'inversion                                                    */
/* ------------------------------------------------------------------ */

static inline gushort
clip_f(float f)
{
	/* gère aussi +inf (powf(0, exp<0)) : f >= 65535 → 65535 */
	if (f <= 0.0f) return 0;
	if (f >= 65535.0f) return 65535;
	return (gushort)(f + 0.5f);
}

/* Médiane d'un histogramme cumulé sur 'total' échantillons. */
static guint
hist_median(const guint *h, guint64 total)
{
	guint64 half = total / 2, acc = 0;
	gint v;
	for (v = 0; v < 65536; v++) {
		acc += h[v];
		if (acc >= half) return (guint)v;
	}
	return 0;
}

static void
apply_argentico(RS_IMAGE16 *img, gfloat green_exp, gfloat red_ratio, gfloat blue_ratio, gfloat exposure)
{
	const gint W = img->w;
	const gint H = img->h;
	const gint ps = img->pixelsize;
	const gint rs = img->rowstride;

	const float rexp = -(green_exp * red_ratio);
	const float gexp = -green_exp;
	const float bexp = -(green_exp * blue_ratio);

	/* Médianes par canal sur la zone centrale (bordure 20% coupée) = référence
	 * d'entrée (base du négatif). */
	guint *hist = g_new0(guint, 3 * 65536);
	guint *hr = hist, *hg = hist + 65536, *hb = hist + 2 * 65536;

	gint bx = W / 5, by = H / 5;   /* 20% */
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
	float refin_r = (float)MAX(hist_median(hr, total), 1u);
	float refin_g = (float)MAX(hist_median(hg, total), 1u);
	float refin_b = (float)MAX(hist_median(hb, total), 1u);
	g_free(hist);

	/* refOut = gris à 1/24 du max, décalé par l'exposition (en stops EV) ;
	 * multiplicateurs pour amener refIn → refOut. exposure=0 → comportement
	 * d'origine ; +1 stop = positif deux fois plus clair. */
	const float refout = (65535.0f / 24.0f) * exp2f(exposure);
	const float rmult = refout / powf(refin_r, rexp);
	const float gmult = refout / powf(refin_g, gexp);
	const float bmult = refout / powf(refin_b, bexp);

	/* LUTs par canal (entrée gushort 0..65535). */
	gushort *lutr = g_new(gushort, 65536);
	gushort *lutg = g_new(gushort, 65536);
	gushort *lutb = g_new(gushort, 65536);
	for (gint v = 0; v < 65536; v++) {
		lutr[v] = clip_f(rmult * powf((float)v, rexp));
		lutg[v] = clip_f(gmult * powf((float)v, gexp));
		lutb[v] = clip_f(bmult * powf((float)v, bexp));
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

	g_free(lutr);
	g_free(lutg);
	g_free(lutb);
}

/* ------------------------------------------------------------------ */
/* Entrée principale                                                   */
/* ------------------------------------------------------------------ */

static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSArgentico *a = RS_ARGENTICO(filter);
	RSFilterResponse *previous = rs_filter_get_image(filter->previous, request);

	if (!previous) return NULL;
	if (!a->enabled) return previous;

	RS_IMAGE16 *img = rs_filter_response_get_image(previous);
	if (!img) return previous;

	RSFilterResponse *response = rs_filter_response_clone(previous);
	RS_IMAGE16 *out = rs_image16_copy(img, TRUE);
	rs_filter_response_set_image(response, out);
	g_object_unref(img);
	g_object_unref(previous);

	apply_argentico(out, a->green_exp, a->red_ratio, a->blue_ratio, a->exposure);

	g_object_unref(out);
	return response;
}

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	rs_argentico_get_type(G_TYPE_MODULE(plugin));
}
