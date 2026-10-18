/**
 * @file voice_overlay_b.c
 * @brief Voice assistant overlay, design variant B "Aurora"
 *
 * Geometry and motion follow docs/design/canvas/VoiceAssistantB.dc.html
 * (720x720 design px, center 360/300). Everything animated is rasterized at
 * half resolution (360x200 px, RGB888 work buffer, additive blending), then
 * upscaled 2x (integer bilinear, which also softens it like the design's
 * glow) into a full-resolution RGB565 canvas covering design y 100..500.
 * LVGL only copies that canvas — letting LVGL scale it cost ~100 ms/frame.
 * (The ESP32-P4's PPA did the upscale in ~2 ms, but its blocking call
 * sometimes never returned — presumably DMA contention with the display
 * driver — and froze the LVGL task with the display lock held. The CPU
 * upscale costs ~7-11 ms and is reliable.)
 * Only the area the current and previous frame drew in is upscaled and
 * invalidated. Labels and the button are normal LVGL objects on top.
 */

#include "../cpp/app_facade.h"
#include "voice_overlay_b.h"
#include "voice_overlay.h"
#include "text_fit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "../i18n.h"
#include "../ui_theme.h"
#include "../fonts.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#define NOW_US() ((uint64_t)esp_timer_get_time())
#else
#define NOW_US() ((uint64_t)lv_tick_get() * 1000u)
#endif

#define CW 360          /* canvas width  (half of 720 design px) */
#define CH 200          /* canvas height (design y 100..500)    */
#define Y0 100          /* design y of the canvas top            */
#define DCX 360.0f      /* design center */
#define DCY 300.0f
#define PI_F 3.14159265f

#define TEXT_2 0xA39F97
#define TEXT_1 0xECE9E3
#define WARN   0xD98A73
#define ACTIVE 0x2B2A27

typedef enum { PH_HIDDEN, PH_WAKE, PH_LISTENING, PH_PROCESSING, PH_RESPONDING, PH_DONE, PH_ERROR } phase_t;

/* "Aurora" palette of the design: a, b, c, d, core, end. */
static const uint8_t PAL[6][3] = {
    {0x3D, 0x6B, 0xFF}, {0x7B, 0x4D, 0xFF}, {0xD0, 0x4D, 0xFF},
    {0xFF, 0x7A, 0x8A}, {0xA9, 0xBE, 0xFF}, {0xFF, 0x8A, 0x5C},
};
/* Wave gradients (3 stops at 0 / .5 / 1): listening cyan -> blue -> violet,
 * speaking violet -> magenta -> warm orange, so the two read differently. */
static const uint8_t LISTEN_GRAD[3][3] = {{0x2E, 0xC8, 0xF0}, {0x3D, 0x6B, 0xFF}, {0x7B, 0x4D, 0xFF}};
static const uint8_t SPEAK_GRAD[3][3] = {{0x7B, 0x4D, 0xFF}, {0xD0, 0x4D, 0xFF}, {0xFF, 0x8A, 0x5C}};

static lv_obj_t *s_root, *s_canvas, *s_label, *s_main, *s_clock, *s_button, *s_icon, *s_ring;
static uint8_t *s_buf;          /* half-res RGB888 work buffer (B, G, R) */
static uint32_t s_stride;
static uint16_t *s_out;         /* full-res RGB565 canvas buffer */
static uint32_t s_out_stride;   /* in pixels */
static size_t s_buf_size, s_out_size;
static int s_clock_min = -1;
/* Drawn area (half-res, inclusive) of this and the previous frame. */
typedef struct { int x0, y0, x1, y1; } box_t;
static box_t s_prev_box;
static bool s_prev_valid;
static lv_timer_t *s_timer;
static phase_t s_phase = PH_HIDDEN;
static uint32_t s_phase_start;
static uint32_t s_start_ms;
static uint64_t s_render_us;
static uint64_t s_refr_us, s_refr_start;
static uint64_t s_draw_us, s_up_us;
static uint32_t s_refr_count;
static float s_demo_level = -1;
static float s_level; /* smoothed audio level 0..1 */
static uint32_t s_frames;

/* ── rasterizer (canvas pixels, additive "screen-like" blending) ─────────── */

static inline void add_px(int x, int y, const uint8_t c[3], int a)
{
    if ((unsigned)x >= CW || (unsigned)y >= CH || a <= 0) return;
    uint8_t *p = s_buf + (size_t)y * s_stride + (size_t)x * 3;
    /* LVGL RGB888 is stored B, G, R. */
    int v;
    v = p[2] + c[0] * a / 255; p[2] = v > 255 ? 255 : (uint8_t)v;
    v = p[1] + c[1] * a / 255; p[1] = v > 255 ? 255 : (uint8_t)v;
    v = p[0] + c[2] * a / 255; p[0] = v > 255 ? 255 : (uint8_t)v;
}

/* Adds an already alpha-scaled color (hot loops: no per-pixel division). */
static inline void add_scaled(uint8_t *p, int r, int g, int b)
{
    int v;
    v = p[2] + r; p[2] = v > 255 ? 255 : (uint8_t)v;
    v = p[1] + g; p[1] = v > 255 ? 255 : (uint8_t)v;
    v = p[0] + b; p[0] = v > 255 ? 255 : (uint8_t)v;
}

/* Soft dot at canvas coords (float), radius ~r px. */
static void dot(float fx, float fy, float r, const uint8_t c[3], int a)
{
    const int x0 = (int)floorf(fx - r - 1), x1 = (int)ceilf(fx + r + 1);
    const int y0 = (int)floorf(fy - r - 1), y1 = (int)ceilf(fy + r + 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = x - fx, dy = y - fy;
            const float d = sqrtf(dx * dx + dy * dy);
            float k = 1.0f - (d - r + 0.5f); /* 1 inside, antialiased edge */
            if (k <= 0) continue;
            if (k > 1) k = 1;
            add_px(x, y, c, (int)(a * k));
        }
    }
}

static void lerp_color(const uint8_t a[3], const uint8_t b[3], float t, uint8_t out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = (uint8_t)(a[i] + (b[i] - a[i]) * t);
}

/* Color along a 3-stop wave gradient. */
static void wave_color(float u, const uint8_t grad[3][3], uint8_t out[3])
{
    if (u < 0.5f) lerp_color(grad[0], grad[1], u / 0.5f, out);
    else lerp_color(grad[1], grad[2], (u - 0.5f) / 0.5f, out);
}

/* Ring (stroke) in design coords, gradient a -> b -> d along the diagonal,
 * drawn from angle -90 deg for `frac` of the circle. */
static void ring(float r, float width, float frac, int alpha)
{
    const float cr = r / 2.0f, cx = DCX / 2.0f, cy = (DCY - Y0) / 2.0f;
    const int steps = (int)(2.0f * PI_F * cr * 1.5f * frac) + 1;
    for (int i = 0; i <= steps; ++i) {
        const float ang = -PI_F / 2 + 2.0f * PI_F * frac * i / steps;
        const float x = cx + cr * cosf(ang), y = cy + cr * sinf(ang);
        const float diag = ((x - cx) + (y - cy)) / (4.0f * cr) + 0.5f; /* 0..1 top-left -> bottom-right */
        uint8_t col[3];
        if (diag < 0.6f) lerp_color(PAL[0], PAL[1], diag / 0.6f, col);
        else lerp_color(PAL[1], PAL[3], (diag - 0.6f) / 0.4f, col);
        dot(x, y, width / 4.0f, col, alpha / 3); /* overlapping dots add up */
    }
}

/* One wave layer of the design (wave() in VoiceAssistantB.dc.html). */
static void wave(float x0, float x1, float amp, float k, float ph, float s, int fill_alpha, int line_alpha,
                 const uint8_t grad[3][3], bool white_line, bool glow)
{
    const int c0 = (int)(x0 / 2), c1 = (int)(x1 / 2);
    float prev_top = -1;
    for (int cx = c0; cx <= c1; ++cx) {
        const float x = cx * 2.0f;
        const float u = (x - x0) / (x1 - x0);
        const float e = powf(sinf(PI_F * u), 2.0f);
        const float v = sinf(k * x + ph) * (0.7f + 0.3f * sinf(k * 0.5f * x - ph * 0.6f));
        const float top = (DCY - amp * s * e * v - Y0) / 2.0f;
        const float bot = (DCY + amp * s * e * v * 0.38f - Y0) / 2.0f;
        uint8_t col[3];
        wave_color(u, grad, col);
        if (fill_alpha && (unsigned)cx < CW) {
            /* Span with fractional coverage at both ends (no stair steps);
             * the inner pixels share one pre-scaled color. */
            const float fa = fminf(top, bot), fb = fmaxf(top, bot);
            const int ya = (int)floorf(fa), yb = (int)floorf(fb);
            const int sr = col[0] * fill_alpha >> 8, sg = col[1] * fill_alpha >> 8, sb = col[2] * fill_alpha >> 8;
            for (int y = ya; y <= yb; ++y) {
                if ((unsigned)y >= CH) continue;
                const float cov = fminf(fb, y + 1.0f) - fmaxf(fa, (float)y);
                uint8_t *px = s_buf + (size_t)y * s_stride + (size_t)cx * 3;
                if (cov >= 1.0f) add_scaled(px, sr, sg, sb);
                else if (cov > 0) add_px(cx, y, col, (int)(fill_alpha * cov));
            }
        }
        if (line_alpha) {
            const uint8_t *lc = white_line ? (const uint8_t[]){0xFF, 0xFF, 0xFF} : col;
            /* Segment from the previous column (keeps steep parts closed),
             * a ~1 px antialiased core plus a soft 3 px glow around it. */
            const float a = prev_top < 0 ? top : prev_top;
            const float sa = fminf(a, top), sb = fmaxf(a, top);
            const float reach = glow ? 3.0f : 1.0f;
            for (int y = (int)floorf(sa - reach); y <= (int)ceilf(sb + reach); ++y) {
                const float yc = y + 0.5f;
                const float d = yc < sa ? sa - yc : yc > sb ? yc - sb : 0.0f;
                float k = fmaxf(0.0f, 1.0f - d / 0.9f) + (glow ? 0.28f * fmaxf(0.0f, 1.0f - d / 3.0f) : 0.0f);
                if (k > 0) add_px(cx, y, lc, (int)(line_alpha * fminf(k, 1.0f)));
            }
        }
        prev_top = top;
    }
}

/* Area (half-res) a phase draws in, generous for glow and pulsing. */
static box_t phase_box(void)
{
    const int cx = CW / 2, cy = (int)(DCY - Y0) / 2;
    int rx = 0, ry = 0;
    switch (s_phase) {
    case PH_WAKE: rx = ry = 90; break;
    case PH_LISTENING: rx = 94; ry = 46; break; /* waves only (the ring is an LVGL object) */
    case PH_PROCESSING: rx = ry = 86; break;
    case PH_RESPONDING: rx = CW / 2; ry = 60; break;
    case PH_DONE:
    case PH_ERROR: rx = ry = 80; break;
    default: break;
    }
    box_t b = {cx - rx, cy - ry, cx + rx, cy + ry};
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > CW - 1) b.x1 = CW - 1;
    if (b.y1 > CH - 1) b.y1 = CH - 1;
    return b;
}

static inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

/* 2x bilinear upscale of half-res box `b` into the RGB565 canvas. Memory
 * bound (PSRAM): reads each source row pair once, skips all-black rows. */
static void upscale(box_t b)
{
    for (int y = b.y0; y <= b.y1; ++y) {
        const uint8_t *r0 = s_buf + (size_t)y * s_stride;
        const uint8_t *r1 = s_buf + (size_t)(y + 1 < CH ? y + 1 : y) * s_stride;
        uint16_t *o0 = s_out + (size_t)(2 * y) * s_out_stride;
        uint16_t *o1 = o0 + s_out_stride;
        bool black = true;
        for (int x = b.x0 * 3; x <= b.x1 * 3 + 2 && black; ++x) black = !r0[x] && !r1[x];
        if (black) {
            memset(o0 + 2 * b.x0, 0, (size_t)(b.x1 - b.x0 + 1) * 4);
            memset(o1 + 2 * b.x0, 0, (size_t)(b.x1 - b.x0 + 1) * 4);
            continue;
        }
        for (int x = b.x0; x <= b.x1; ++x) {
            const int xn = x + 1 < CW ? x + 1 : x;
            const uint8_t *a = r0 + 3 * x, *bb = r0 + 3 * xn, *c = r1 + 3 * x, *d = r1 + 3 * xn;
            /* B, G, R bytes */
            o0[2 * x] = rgb565(a[2], a[1], a[0]);
            o0[2 * x + 1] = rgb565((a[2] + bb[2]) >> 1, (a[1] + bb[1]) >> 1, (a[0] + bb[0]) >> 1);
            o1[2 * x] = rgb565((a[2] + c[2]) >> 1, (a[1] + c[1]) >> 1, (a[0] + c[0]) >> 1);
            o1[2 * x + 1] = rgb565((a[2] + bb[2] + c[2] + d[2]) >> 2, (a[1] + bb[1] + c[1] + d[1]) >> 2,
                                   (a[0] + bb[0] + c[0] + d[0]) >> 2);
        }
    }
}

static float clampf(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float ease_out(float x) { x = clampf(x); return 1.0f - (1 - x) * (1 - x) * (1 - x); }

/* ── per-phase drawing ───────────────────────────────────────────────────── */

static void draw_wake(float p)
{
    const float we = ease_out(p * 1.2f);
    /* r4: soft radial glow a/b */
    const float r4 = 170 * we / 2.0f, cx = DCX / 2.0f, cy = (DCY - Y0) / 2.0f;
    for (int y = (int)(cy - r4); y <= (int)(cy + r4); ++y) {
        for (int x = (int)(cx - r4); x <= (int)(cx + r4); ++x) {
            const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (r4 + 0.01f);
            if (d > 1) continue;
            float a = d < 0.45f ? d / 0.45f * 0.55f : d < 0.75f ? 0.55f - (d - 0.45f) / 0.3f * 0.3f : 0.25f * (1 - d) / 0.25f;
            add_px(x, y, d < 0.6f ? PAL[0] : PAL[1], (int)(a * 255));
        }
    }
    ring(146 * we, 1.5f, 1, 64);
    ring(112 * we, 2.0f, 1, 115);
    ring(78 * we, 7.0f, 1, 255);
}

/* Recent low/high of a signal, for mapping it to 0..1 relative to what it
 * currently does. `min_span` keeps silence from being blown up to noise. */
typedef struct {
    float lo, hi;
    bool init;
} range_t;
static range_t s_rng[4]; /* level, low, mid, high */

static float autorange(range_t *r, float v, float min_span)
{
    if (!r->init) {
        r->lo = r->hi = v;
        r->init = true;
    }
    if (v < r->lo) r->lo = v;
    else r->lo += (v - r->lo) * 0.004f; /* floor creeps up (~4 s) */
    if (v > r->hi) r->hi = v;
    else r->hi += (v - r->hi) * 0.015f; /* peak falls back (~2 s) */
    float span = r->hi - r->lo;
    if (span < min_span) span = min_span;
    const float x = (v - r->lo) / span;
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

/* Wave phases, advanced per frame so the speed can follow the voice
 * without the waves jumping. */
static float s_wave_ph[5] = {0, 1.35f, 2.7f, 4.05f, 5.4f};
static float s_band[3]; /* smoothed low / mid / high levels */

/* Each layer follows one band (the voice "plays" the layers differently),
 * all of them the overall level; louder = higher, faster and brighter. */
static void draw_waves(uint32_t dt_ms, bool wide, float level)
{
    const float x0 = wide ? 30 : 176, x1 = wide ? 690 : 544;
    static const float scale[5] = {1, 0.82f, 0.66f, 0.5f, 0.9f};
    static const int fill[5] = {77, 66, 56, 51, 46}; /* .30 .26 .22 .20 .18 */
    static const int band_of[5] = {1, 0, 2, 1, 2};   /* mid, low, high, mid, high */
    const uint8_t(*grad)[3] = wide ? SPEAK_GRAD : LISTEN_GRAD;
    const float bright = 0.75f + 0.5f * level;
    const float speed = 1.0f + 1.4f * level;
    /* The outer frame ring while listening is an LVGL object (s_ring):
     * ~900 soft dots per frame were the most expensive part here. */
    for (int i = 0; i < 5; ++i) {
        const float k = 0.017f + i * 0.0045f - (wide ? 0.004f : 0);
        s_wave_ph[i] += dt_ms * (0.0024f + i * 0.0007f) * speed * (i % 2 ? -1.0f : 1.0f);
        const float band = s_band[band_of[i]];
        /* nearly flat when quiet, clearly moving with each syllable */
        const float amp = wide ? 10 + level * 40 + band * 70 : 5 + level * 30 + band * 75;
        int line = i == 0 ? 242 : i == 1 ? 178 : i == 2 ? 140 : i == 4 ? 90 : 0;
        int f = (int)(fill[i] * bright);
        line = (int)(line * bright);
        if (f > 255) f = 255;
        if (line > 255) line = 255;
        /* glow only on the main and the bright line: cheaper, and calmer */
        wave(x0, x1, amp, k, s_wave_ph[i], scale[i], f, line, grad, i == 4, i == 0 || i == 4);
    }
}

/* Processing: an orb of three point rings turning at different speeds and
 * directions, radii breathing slightly; sizes/opacity vary per point so it
 * looks busier than ~50 dots are. */
static void draw_particles(uint32_t t)
{
    static const struct {
        int count;
        float radius, speed, pulse;
        float size;
    } rings[3] = {
        {11, 70, 0.8f, 6, 2.6f},    /* inner,  +0.8 rad/s */
        {17, 112, -0.45f, 9, 2.2f}, /* middle, -0.45 rad/s */
        {22, 152, 0.25f, 12, 1.9f}, /* outer,  +0.25 rad/s */
    };
    const float ts = t / 1000.0f;
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < rings[r].count; ++i) {
            const float a = (float)i / rings[r].count * 2 * PI_F + ts * rings[r].speed;
            const float rad = rings[r].radius + sinf(ts * 1.6f + i * 0.9f + r) * rings[r].pulse;
            const float k = 0.5f + 0.5f * sinf(ts * 2.1f + i * 1.7f + r * 2.0f); /* twinkle */
            const uint8_t *col = PAL[(i + r) % 3];
            dot((DCX + rad * cosf(a)) / 2, (DCY + rad * sinf(a) - Y0) / 2, (rings[r].size * (0.6f + 0.6f * k)) / 2 + 0.4f,
                col, (int)(120 + 135 * k));
        }
    }
}

static void draw_done(float p, bool error)
{
    const float de = ease_out(p * 1.6f);
    ring(150, 3, error ? 1 : de, error ? 90 : 255);
    if (error) return;
    /* check M312 302 L348 338 L414 266, drawn on */
    static const float pts[3][2] = {{312, 302}, {348, 338}, {414, 266}};
    const float total = 190.0f, drawn = total * ease_out((p - 0.2f) * 2);
    float acc = 0;
    static const uint8_t white[3] = {0xEC, 0xE9, 0xE3};
    for (int sgm = 0; sgm < 2; ++sgm) {
        const float dx = pts[sgm + 1][0] - pts[sgm][0], dy = pts[sgm + 1][1] - pts[sgm][1];
        const float len = sqrtf(dx * dx + dy * dy);
        for (float d = 0; d <= len && acc + d <= drawn; d += 1.0f) {
            dot((pts[sgm][0] + dx * d / len) / 2, (pts[sgm][1] + dy * d / len - Y0) / 2, 1.6f, white, 255);
        }
        acc += len;
    }
}

static void render(void)
{
    const uint32_t now = lv_tick_get();
    const uint32_t t = now - s_start_ms;
    const float p = (now - s_phase_start) / 1000.0f;
    const uint64_t t0 = NOW_US();
    /* Audio level, smoothed asymmetrically: quick to rise with speech, slow
     * to fall, so the waves do not jitter. */
    float target = s_demo_level >= 0 ? s_demo_level
                   : s_phase == PH_RESPONDING ? app_cpp_voice_output_level() : app_cpp_voice_level();
    float bands[3];
    if (s_demo_level >= 0) {
        /* demo: let the bands drift so the layers move differently */
        bands[0] = s_demo_level * (0.6f + 0.4f * sinf(t / 410.0f));
        bands[1] = s_demo_level * (0.6f + 0.4f * sinf(t / 290.0f + 1));
        bands[2] = s_demo_level * (0.6f + 0.4f * sinf(t / 170.0f + 2));
    } else {
        app_cpp_voice_bands(bands);
    }
    /* Auto-ranged: absolute levels say little (the microphone idles at
     * ~0.2 with AGC and room noise, the TTS sits near 1.0 all the time), so
     * each value is mapped between its recent low and high. */
    target = autorange(&s_rng[0], target, 0.25f);
    for (int k = 0; k < 3; ++k) bands[k] = autorange(&s_rng[k + 1], bands[k], 0.15f);
    s_level += (target - s_level) * (target > s_level ? 0.40f : 0.10f);
    for (int k = 0; k < 3; ++k) s_band[k] += (bands[k] - s_band[k]) * (bands[k] > s_band[k] ? 0.45f : 0.12f);
    static uint32_t s_last_ms;
    const uint32_t dt = s_last_ms ? now - s_last_ms : 33;
    s_last_ms = now;
    const box_t cur = phase_box();
    /* Clear what this frame and the previous one cover. */
    box_t u = cur;
    if (s_prev_valid) {
        if (s_prev_box.x0 < u.x0) u.x0 = s_prev_box.x0;
        if (s_prev_box.y0 < u.y0) u.y0 = s_prev_box.y0;
        if (s_prev_box.x1 > u.x1) u.x1 = s_prev_box.x1;
        if (s_prev_box.y1 > u.y1) u.y1 = s_prev_box.y1;
    }
    for (int y = u.y0; y <= u.y1; ++y) memset(s_buf + (size_t)y * s_stride + u.x0 * 3, 0, (size_t)(u.x1 - u.x0 + 1) * 3);
    const uint64_t t_draw = NOW_US();
    switch (s_phase) {
    case PH_WAKE:
        draw_wake(p / 0.4f); /* ~400 ms pulse, then the waves */
        if (p > 0.4f) s_phase = PH_LISTENING;
        break;
    case PH_LISTENING:
        /* The frame ring stays still: resizing it with the level made LVGL
         * redraw its whole 400x400 area every frame. */
        draw_waves(dt > 100 ? 33 : dt, false, s_level);
        break;
    case PH_PROCESSING: draw_particles(t); break;
    case PH_RESPONDING:
        draw_waves(dt > 100 ? 33 : dt, true, s_level);
        break;
    case PH_DONE: draw_done(p / 1.4f, false); break;
    case PH_ERROR: draw_done(1, true); break;
    default: break;
    }
    const uint64_t t_up = NOW_US();
    s_draw_us += t_up - t_draw;
    upscale(u);
    s_up_us += NOW_US() - t_up;
    s_prev_box = cur;
    s_prev_valid = true;
    if (s_phase == PH_LISTENING) lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    s_render_us += NOW_US() - t0;
    if (++s_frames == 300) { /* every ~10 s while shown */
        printf("[VoiceB] rasterizer %u us/frame (draw %u, upscale %u), LVGL refresh %u us x %u in 300 frames\n",
               (unsigned)(s_render_us / s_frames), (unsigned)(s_draw_us / s_frames), (unsigned)(s_up_us / s_frames),
               (unsigned)(s_refr_count ? s_refr_us / s_refr_count : 0), (unsigned)s_refr_count);
        s_render_us = s_refr_us = s_draw_us = s_up_us = 0;
        s_frames = s_refr_count = 0;
    }
    /* lv_obj_invalidate_area() takes screen coordinates, not canvas-relative
     * ones (relative ones left the lower part of the animation frozen). */
    lv_area_t c;
    lv_obj_get_coords(s_canvas, &c);
    lv_area_t a = {c.x1 + 2 * u.x0, c.y1 + 2 * u.y0, c.x1 + 2 * u.x1 + 1, c.y1 + 2 * u.y1 + 1};
    lv_obj_invalidate_area(s_canvas, &a);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_display_trigger_activity(NULL);
    render();
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min != s_clock_min) { /* only redraw the label when it changes */
        s_clock_min = tm.tm_min;
        char clock[16];
        app_cpp_clock_text(clock, sizeof(clock)); /* 24 h or AM/PM (settings) */
        lv_label_set_text(s_clock, clock);
    }
}

/* Display refresh duration (render + flush of the dirty area), to see where
 * frame time goes. */
static void refr_cb(lv_event_t *e)
{
    if (s_phase == PH_HIDDEN) return;
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_start = NOW_US();
    } else if (s_refr_start) {
        s_refr_us += NOW_US() - s_refr_start;
        ++s_refr_count;
        s_refr_start = 0;
    }
}

void voice_overlay_b_init(lv_event_cb_t on_tap)
{
    if (s_root) return;
    lv_display_add_event_cb(lv_display_get_default(), refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), refr_cb, LV_EVENT_REFR_READY, NULL);
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, on_tap, LV_EVENT_CLICKED, NULL);

    s_stride = (uint32_t)CW * 3;
    s_out_stride = CW * 2; /* tightly packed: the PPA writes a plain picture */
    s_buf_size = (size_t)s_stride * CH;
    s_out_size = (size_t)s_out_stride * 2 * CH * 2;
    s_buf = calloc(s_buf_size, 1); /* large: PSRAM on the device */
    s_out = calloc(s_out_size, 1);
    if (!s_out) {
        free(s_buf);
        s_buf = NULL;
    }
    s_canvas = lv_canvas_create(s_root);
    if (s_buf) lv_canvas_set_buffer(s_canvas, s_out, CW * 2, CH * 2, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, Y0);

    s_ring = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(0x6A5CFF), 0);
    lv_obj_set_style_border_opa(s_ring, LV_OPA_50, 0);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ring, 380, 380);
    lv_obj_set_pos(s_ring, (int32_t)DCX - 190, (int32_t)DCY - 190);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    s_clock = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_clock, font_manrope_500_22, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(TEXT_2), 0);
    lv_obj_set_pos(s_clock, 28, 24);

    s_label = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_label, font_manrope_500_22, 0);
    lv_obj_set_style_text_color(s_label, lv_color_hex(TEXT_2), 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, 508);

    s_main = lv_label_create(s_root);
    /* Fixed box above the button: long answers scroll slowly instead of
     * running into it. */
    lv_obj_set_size(s_main, 640, 66);
    lv_label_set_long_mode(s_main, LV_LABEL_LONG_MODE_SCROLL);
    lv_obj_set_style_anim_duration(s_main, lv_anim_speed_clamped(18, 2000, 30000), 0);
    lv_obj_set_style_text_align(s_main, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_main, font_manrope_600_26, 0);
    lv_obj_set_style_text_color(s_main, lv_color_hex(TEXT_1), 0);
    lv_obj_align(s_main, LV_ALIGN_TOP_MID, 0, 545);

    s_button = lv_button_create(s_root);
    lv_obj_remove_style_all(s_button);
    lv_obj_set_size(s_button, 72, 72);
    lv_obj_set_style_radius(s_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_button, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_button, 2, 0);
    lv_obj_align(s_button, LV_ALIGN_TOP_MID, 0, 620);
    lv_obj_add_event_cb(s_button, on_tap, LV_EVENT_CLICKED, NULL);
    s_icon = lv_label_create(s_button);
    lv_obj_set_style_text_font(s_icon, UI_ICON_M, 0);
    lv_label_set_text(s_icon, LI_MIC);
    lv_obj_center(s_icon);

    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void voice_overlay_b_update(const app_cpp_voice_status_t *st)
{
    if (!s_root || !s_buf) return;
    phase_t next = PH_HIDDEN;
    const char *label = "";
    const char *main = "";
    switch (st->state) {
    case APP_CPP_VOICE_LISTENING:
        next = (s_phase == PH_HIDDEN || s_phase == PH_DONE || s_phase == PH_ERROR) ? PH_WAKE
               : (s_phase == PH_WAKE ? PH_WAKE : PH_LISTENING);
        label = tr(TR_VOICE_B_LISTENING);
        break;
    case APP_CPP_VOICE_PROCESSING: next = PH_PROCESSING; label = tr(TR_VOICE_B_THINKING); main = st->transcript; break;
    case APP_CPP_VOICE_SPEAKING: next = PH_RESPONDING; label = tr(TR_VOICE_B_ANSWERING); main = st->response; break;
    case APP_CPP_VOICE_DONE: next = PH_DONE; label = "Erledigt"; main = st->response; break;
    case APP_CPP_VOICE_ERROR: next = PH_ERROR; label = tr(TR_VOICE_B_NOT_UNDERSTOOD); main = st->error; break;
    default: break;
    }
    if (next != s_phase) {
        if ((next == PH_LISTENING || next == PH_WAKE) != (s_phase == PH_LISTENING || s_phase == PH_WAKE)) {
            memset(s_rng, 0, sizeof(s_rng)); /* other audio source: learn its range anew */
        }
        if (s_phase == PH_HIDDEN) {
            s_start_ms = lv_tick_get();
            memset(s_buf, 0, (size_t)s_stride * CH);
            memset(s_out, 0, (size_t)s_out_stride * 2 * CH * 2);
            s_prev_valid = false;
            s_clock_min = -1;
        }
        s_phase = next;
        s_phase_start = lv_tick_get();
    }
    if (next == PH_HIDDEN) {
        if (!lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) voice_overlay_cover_screen(false);
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = NULL;
        }
        return;
    }
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    voice_overlay_cover_screen(true);
    if (!s_timer) s_timer = lv_timer_create(timer_cb, 33, NULL);
    lv_display_trigger_activity(NULL);

    lv_label_set_text(s_label, label);
    /* longer texts in the smaller font (3 lines instead of 2); characters
     * the font lacks (LLM typography, emoji) are substituted or dropped */
    const lv_font_t *font = strlen(main) > 90 ? font_manrope_500_22 : font_manrope_600_26;
    static char fitted[600];
    text_fit_font(font, main, fitted, sizeof(fitted));
    lv_obj_set_style_text_font(s_main, font, 0);
    lv_label_set_text(s_main, fitted);
    lv_obj_set_style_text_color(s_main, lv_color_hex(next == PH_ERROR ? WARN : TEXT_1), 0);
    const bool active = next != PH_DONE && next != PH_ERROR;
    lv_obj_set_style_border_color(s_button, lv_color_hex(active ? 0x7B4DFF : ACTIVE), 0);
    lv_obj_set_style_text_color(s_icon, lv_color_hex(active ? TEXT_1 : 0x8A857D), 0);
    timer_cb(NULL);
}

void voice_overlay_b_demo(app_cpp_voice_state_t state, float level, const char *text)
{
    app_cpp_voice_status_t st;
    memset(&st, 0, sizeof(st));
    st.state = state;
    snprintf(st.transcript, sizeof(st.transcript), "%s", text ? text : "");
    snprintf(st.response, sizeof(st.response), "%s", text ? text : "");
    snprintf(st.error, sizeof(st.error), "%s", text ? text : "");
    s_demo_level = level;
    voice_overlay_b_update(&st);
}
