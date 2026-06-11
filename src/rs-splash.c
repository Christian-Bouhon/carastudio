#include "rs-splash.h"
#include <gtk/gtk.h>
#include <math.h>

/* ── Géométrie ─────────────────────────────────────────────────────────── */
#define SPLASH_W    560
#define LOGO_SIZE   340
#define MARGIN      32    /* espace identique en haut et en bas */
#define TEXT_GAP    30    /* espace entre bas du logo et baseline texte */
#define TEXT_H      34    /* hauteur visuelle approx. du texte à 30pt  */
#define SPLASH_H    (2*MARGIN + LOGO_SIZE + TEXT_GAP + TEXT_H)
#define LOGO_X      ((SPLASH_W - LOGO_SIZE) / 2)
#define LOGO_Y      MARGIN
#define TEXT_Y      (LOGO_Y + LOGO_SIZE + TEXT_GAP)

/* ── Timing (secondes) ─────────────────────────────────────────────────── */
#define T_LOGO_END    0.55
#define T_VEIL_START  0.32
#define T_VEIL_END    0.82
#define T_TEXT_START  0.65
#define T_TEXT_END    1.15
#define T_DONE        3.80   /* fermeture automatique */

#define FPS          60
#define INTERVAL_MS  (1000 / FPS)

typedef struct {
    GtkWidget *window;
    GtkWidget *area;
    GdkPixbuf *logo_pb;
    gint64     start_us;
    guint      timer_id;
} Splash;

static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
static double phase(double t, double s, double e) { return clamp01((t - s) / (e - s)); }
static double ease_out(double p) { double q = 1.0 - p; return 1.0 - q * q * q; }

static gboolean
splash_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    (void)w;
    Splash *s = data;
    double t = (g_get_monotonic_time() - s->start_us) / 1e6;

    /* Fond transparent */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    /* ── Logo (zoom ease-out) ────────────────────────────────────────── */
    double lp = ease_out(phase(t, 0.0, T_LOGO_END));
    if (lp > 0.001 && s->logo_pb) {
        double cx = LOGO_X + LOGO_SIZE / 2.0;
        double cy = LOGO_Y + LOGO_SIZE / 2.0;
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_scale(cr, lp, lp);
        cairo_translate(cr, -LOGO_SIZE / 2.0, -LOGO_SIZE / 2.0);
        double src_w = (double)gdk_pixbuf_get_width(s->logo_pb);
        double sc = (double)LOGO_SIZE / src_w;
        cairo_scale(cr, sc, sc);
        gdk_cairo_set_source_pixbuf(cr, s->logo_pb, 0.0, 0.0);
        cairo_paint_with_alpha(cr, lp);
        cairo_restore(cr);
    }

    /* ── Voile lumineux (balayage gauche → droite) ───────────────────── */
    double vp = ease_out(phase(t, T_VEIL_START, T_VEIL_END));
    if (vp > 0.001) {
        double band = 90.0;
        double vx   = LOGO_X - band + (LOGO_SIZE + band) * vp;
        cairo_pattern_t *pat = cairo_pattern_create_linear(vx, 0, vx + band, 0);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, 1, 1, 1, 0.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.45, 1, 1, 1, 0.38);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 1, 1, 1, 0.0);
        cairo_save(cr);
        cairo_rectangle(cr, LOGO_X, LOGO_Y, LOGO_SIZE, LOGO_SIZE);
        cairo_clip(cr);
        cairo_set_source(cr, pat);
        cairo_rectangle(cr, vx, LOGO_Y, band, LOGO_SIZE);
        cairo_fill(cr);
        cairo_restore(cr);
        cairo_pattern_destroy(pat);
    }

    /* ── Tagline "a raw editor" (zoom ease-out) ──────────────────────── */
    double tp = ease_out(phase(t, T_TEXT_START, T_TEXT_END));
    if (tp > 0.001) {
        const char *tag = "a raw editor";
        cairo_select_font_face(cr, "P052",
                               CAIRO_FONT_SLANT_ITALIC,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 27.0);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, tag, &ext);

        double cx = SPLASH_W / 2.0;
        cairo_save(cr);
        cairo_translate(cr, cx, TEXT_Y);
        cairo_scale(cr, tp, tp);
        cairo_translate(cr, -cx, -TEXT_Y);

        double tx = cx - ext.width / 2.0 - ext.x_bearing;
        cairo_move_to(cr, tx, TEXT_Y);
        cairo_set_source_rgba(cr, 0.90, 0.90, 0.90, tp);
        cairo_show_text(cr, tag);
        cairo_restore(cr);
    }

    return FALSE;
}

static gboolean
splash_tick(gpointer data)
{
    Splash *s = data;
    double t  = (g_get_monotonic_time() - s->start_us) / 1e6;
    gtk_widget_queue_draw(s->area);
    if (t >= T_DONE) {
        gtk_main_quit();
        return FALSE;
    }
    return TRUE;
}

void
rs_splash_show(void)
{
    Splash *s = g_new0(Splash, 1);

    gchar *path = g_build_filename(PACKAGE_DATA_DIR, "icons", "carastudio.png", NULL);
    s->logo_pb  = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);

    s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(s->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(s->window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(s->window), SPLASH_W, SPLASH_H);
    gtk_window_set_position(GTK_WINDOW(s->window), GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s->window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(s->window), TRUE);

    /* Activer le canal alpha pour le fond transparent */
    GdkScreen *screen = gtk_widget_get_screen(s->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual)
        gtk_widget_set_visual(s->window, visual);
    gtk_widget_set_app_paintable(s->window, TRUE);

    s->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s->area, SPLASH_W, SPLASH_H);
    g_signal_connect(s->area, "draw", G_CALLBACK(splash_draw), s);
    gtk_container_add(GTK_CONTAINER(s->window), s->area);

    gtk_widget_show_all(s->window);

    s->start_us = g_get_monotonic_time();
    s->timer_id = g_timeout_add(INTERVAL_MS, splash_tick, s);

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();

    gtk_widget_destroy(s->window);
    if (s->logo_pb)
        g_object_unref(s->logo_pb);
    g_free(s);
}
