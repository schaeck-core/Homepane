#include "ui_kit.h"

#include "lvgl/src/libs/qrcode/qrcodegen.h"

#include <stdlib.h>
#include <string.h>

uint32_t ui_look_bg(ui_look_t look)
{
    switch (look) {
    case UI_LOOK_ON: return UI_ACCENT_BG;
    case UI_LOOK_WARN: return UI_WARN_BG;
    case UI_LOOK_HEAT: return UI_HEAT_BG;
    case UI_LOOK_COOL: return UI_COOL_BG;
    default: return UI_SURFACE2;
    }
}

uint32_t ui_look_fg(ui_look_t look)
{
    switch (look) {
    case UI_LOOK_ON: return UI_ACCENT;
    case UI_LOOK_INFO: return UI_TEXT;
    case UI_LOOK_WARN: return UI_WARN;
    case UI_LOOK_HEAT: return UI_HEAT;
    case UI_LOOK_COOL: return UI_COOL;
    default: return UI_TEXT3;
    }
}

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}

lv_obj_t *ui_tile(lv_obj_t *parent)
{
    lv_obj_t *t = ui_box(parent);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(t, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x1A1A18), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, UI_R_TILE, 0);
    lv_obj_set_style_pad_all(t, UI_TILE_PAD, 0);
    lv_obj_set_style_pad_row(t, UI_GAP, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT), 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    return t;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text ? text : "");
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

lv_obj_t *ui_label_line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, color, text);
    lv_obj_set_width(l, LV_PCT(100));
    /* one line high: with a content height the label would wrap instead of cutting */
    lv_obj_set_height(l, lv_font_get_line_height(font));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

lv_obj_t *ui_bubble(lv_obj_t *parent, int32_t size, const lv_font_t *icon_font, const char *icon, ui_look_t look)
{
    lv_obj_t *b = ui_box(parent);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *i = ui_label(b, icon_font, UI_TEXT3, icon);
    lv_obj_center(i);
    ui_bubble_set(b, icon, look, 0);
    return b;
}

void ui_bubble_set(lv_obj_t *bubble, const char *icon, ui_look_t look, uint32_t icon_color)
{
    lv_obj_set_style_bg_color(bubble, lv_color_hex(ui_look_bg(look)), 0);
    lv_obj_t *i = lv_obj_get_child(bubble, 0);
    if (!i) return;
    if (icon && strcmp(lv_label_get_text(i), icon) != 0) lv_label_set_text(i, icon);
    lv_obj_set_style_text_color(i, lv_color_hex(icon_color ? icon_color : ui_look_fg(look)), 0);
}

lv_obj_t *ui_button_row(lv_obj_t *tile)
{
    lv_obj_t *row = ui_box(tile);
    lv_obj_set_size(row, LV_PCT(100), UI_BUTTON_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_GAP_LIST, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    return row;
}

lv_obj_t *ui_tile_button(lv_obj_t *row, const char *icon, const char *label, bool primary)
{
    lv_obj_t *b = ui_box(row);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? UI_TEXT : UI_SURFACE2), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? 0xC9C6C0 : UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(primary ? 0x000000 : UI_TEXT), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 8, 0);
    lv_obj_t *i = lv_label_create(b);
    lv_obj_set_style_text_font(i, UI_ICON_BUTTON, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, UI_FONT_NAME, 0);
    ui_tile_button_set(b, icon, label, false);
    return b;
}

void ui_tile_button_set(lv_obj_t *button, const char *icon, const char *label, bool active)
{
    lv_obj_t *i = lv_obj_get_child(button, 0);
    lv_obj_t *l = lv_obj_get_child(button, 1);
    if (icon) lv_label_set_text(i, icon);
    if (label) lv_label_set_text(l, label);
    if (icon) lv_obj_remove_flag(i, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(i, LV_OBJ_FLAG_HIDDEN);
    if (label) lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    if (active) lv_obj_add_state(button, LV_STATE_CHECKED);
    else lv_obj_remove_state(button, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_ACTIVE), LV_STATE_CHECKED);
}

lv_obj_t *ui_toggle(lv_obj_t *parent)
{
    lv_obj_t *s = lv_switch_create(parent);
    lv_obj_set_size(s, 96, 56);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACTIVE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_TEXT3), LV_PART_KNOB);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x000000), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_pad_all(s, -6, LV_PART_KNOB); /* knob 44 in a 56 track */
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, 0, 0);
    return s;
}

/* ── Action button ── */

lv_obj_t *ui_action_button(lv_obj_t *parent, const char *text, ui_look_t look, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, 88);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(look == UI_LOOK_INFO ? UI_SURFACE : ui_look_bg(look)), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(ui_label(b, UI_FONT_NAME, look == UI_LOOK_INFO ? UI_TEXT : ui_look_fg(look), text));
    return b;
}

/* ── Logo ── */

lv_obj_t *ui_logo(lv_obj_t *parent, int32_t pane, bool light_ground)
{
    const int32_t gap = pane * 14 / 44, radius = pane * 12 / 44;
    lv_obj_t *logo = ui_box(parent);
    lv_obj_set_size(logo, 3 * pane + 2 * gap, 3 * pane + 2 * gap);
    for (int i = 0; i < 9; i++) {
        lv_obj_t *p = ui_box(logo);
        lv_obj_set_size(p, pane, pane);
        lv_obj_set_pos(p, (i % 3) * (pane + gap), (i / 3) * (pane + gap));
        lv_obj_set_style_radius(p, radius, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        const uint32_t pane_color = light_ground ? UI_LOGO_PANE_LIGHT : UI_LOGO_PANE;
        const uint32_t lit_color = light_ground ? UI_LOGO_LIT_LIGHT : UI_ACCENT;
        lv_obj_set_style_bg_color(p, lv_color_hex(i == 2 ? lit_color : pane_color), 0);
    }
    return logo;
}

/* ── Loader ── */

/* the lit pane between sand and its dark ground */
static void anim_pulse_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_color(obj, lv_color_mix(lv_color_hex(UI_ACCENT_BG), lv_color_hex(UI_ACCENT), (uint8_t)v), 0);
}

void ui_logo_pulse(lv_obj_t *logo, uint32_t delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lv_obj_get_child(logo, 2));
    lv_anim_set_exec_cb(&a, anim_pulse_cb);
    lv_anim_set_values(&a, 0, 110); /* a light pulse: it stays the lit one */
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* "Atmen · Regen" from the design canvas (artboard Loader, tempo calm): the
 * dark panes stay, single panes breathe up in sand one after another (a
 * fixed pseudo-random order, each one another than the last, its strength
 * 50-100 %), a new one every 520 ms, each breath 1.9 s. First the lit pane
 * alone for 600 ms, as in the logo. One timer per loader mixes each pane's
 * colour (no opacity layers) and redraws only panes that change. */
#define RAIN_INTERVAL 520
#define RAIN_BREATH 1900
#define RAIN_DELAY 600
#define RAIN_TOP 8 /* the cell of the lit pane (top right) */

typedef struct {
    lv_obj_t *logo;
    uint32_t start;
    uint8_t shown[9]; /* each pane's sand share as last drawn */
} rain_t;

/* the canvas's cells, in its order: x, y */
static const uint8_t rain_cells[9][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}, {2, 0}};

static uint32_t rain_hash(uint32_t k, uint32_t s)
{
    uint32_t x = k * 2654435761u + s * 40503u + 12345u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}

static int rain_raw(uint32_t k) { return k == 0 ? RAIN_TOP : (int)(rain_hash(k, 1) % 9); }

/* the cell of drop k: never the same as the one before */
static int rain_cell(uint32_t k)
{
    const int a = rain_raw(k), b = k > 0 ? rain_raw(k - 1) : -1;
    return a == b ? (a + 4) % 9 : a;
}

static float rain_ease(float x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

/* up in the first 40 %, down in the rest */
static float rain_breath(float u) { return u < 0.4f ? rain_ease(u / 0.4f) : 1 - rain_ease((u - 0.4f) / 0.6f); }

static void rain_timer_cb(lv_timer_t *t)
{
    rain_t *r = lv_timer_get_user_data(t);
    float ops[9] = {0};
    const int32_t since = (int32_t)lv_tick_elaps(r->start) - RAIN_DELAY;
    if (since <= 0) {
        ops[RAIN_TOP] = 1;
    } else {
        /* led in by 40 % of a breath: the lit pane is at its peak as the rain starts */
        const int32_t tt = since + RAIN_BREATH * 4 / 10;
        const int32_t k1 = tt / RAIN_INTERVAL;
        const int32_t k0 = tt > RAIN_BREATH ? (tt - RAIN_BREATH) / RAIN_INTERVAL : 0;
        for (int32_t k = k0; k <= k1; k++) {
            const float u = (float)(tt - k * RAIN_INTERVAL) / RAIN_BREATH;
            if (u < 0 || u >= 1) continue;
            const float amp = k > 0 ? 0.5f + 0.5f * (float)(rain_hash((uint32_t)k, 7) % 100) / 100.0f : 1;
            const int i = rain_cell((uint32_t)k);
            const float v = amp * rain_breath(u);
            if (v > ops[i]) ops[i] = v;
        }
    }
    for (int i = 0; i < 9; i++) {
        const uint8_t share = (uint8_t)(ops[i] * 255 + 0.5f);
        if (share == r->shown[i]) continue;
        r->shown[i] = share;
        lv_obj_t *pane = lv_obj_get_child(r->logo, rain_cells[i][1] * 3 + rain_cells[i][0]);
        lv_obj_set_style_bg_color(pane, lv_color_mix(lv_color_hex(UI_ACCENT), lv_color_hex(UI_LOGO_PANE), share), 0);
    }
}

static void rain_delete_cb(lv_event_t *e)
{
    lv_timer_t *t = lv_event_get_user_data(e);
    lv_free(lv_timer_get_user_data(t));
    lv_timer_delete(t);
}

lv_obj_t *ui_loader(lv_obj_t *parent, int32_t pane)
{
    lv_obj_t *logo = ui_logo(parent, pane, false);
    rain_t *r = lv_malloc_zeroed(sizeof(rain_t));
    if (!r) return logo;
    r->logo = logo;
    r->start = lv_tick_get();
    r->shown[RAIN_TOP] = 255; /* ui_logo drew it lit */
    lv_timer_t *t = lv_timer_create(rain_timer_cb, 33, r);
    lv_obj_add_event_cb(logo, rain_delete_cb, LV_EVENT_DELETE, t);
    return logo;
}

/* ── QR code ── */

#define UI_QR_MAX_VERSION 10 /* 57 x 57 modules; a short link needs 3-4 */

static void qr_delete_cb(lv_event_t *e)
{
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(lv_event_get_target(e));
    if (buf) lv_draw_buf_destroy(buf);
}

lv_obj_t *ui_qr(lv_obj_t *parent, int32_t size)
{
    lv_obj_t *qr = lv_canvas_create(parent);
    lv_draw_buf_t *buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_RGB888, LV_STRIDE_AUTO);
    if (buf) {
        lv_canvas_set_draw_buf(qr, buf);
        lv_canvas_fill_bg(qr, lv_color_hex(UI_TEXT), LV_OPA_COVER);
    }
    lv_obj_add_event_cb(qr, qr_delete_cb, LV_EVENT_DELETE, NULL);
    return qr;
}

static void qr_rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t radius, uint32_t color)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    const lv_area_t a = {x, y, x + w - 1, y + w - 1};
    lv_draw_rect(layer, &d, &a);
}

/* in one of the three finder marks (7 x 7 modules in the corners) */
static bool qr_in_finder(int n, int x, int y)
{
    return (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);
}

bool ui_qr_set(lv_obj_t *qr, const char *text)
{
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(qr);
    if (!buf) return false;
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(UI_QR_MAX_VERSION)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(UI_QR_MAX_VERSION)];
    /* high error correction (30 %): the logo covers the middle */
    if (!qrcodegen_encodeText(text, tmp, code, qrcodegen_Ecc_HIGH, 1, UI_QR_MAX_VERSION, qrcodegen_Mask_AUTO, false))
        return false;
    const int n = qrcodegen_getSize(code);
    const int32_t m = (int32_t)buf->header.w / n; /* module size */
    const int32_t o = ((int32_t)buf->header.w - m * n) / 2;
    /* the logo's hole: an odd number of modules, about a fifth of the width
     * (~5 % of the area, well inside what the error correction repairs) */
    int hole = n / 5;
    if (hole % 2 == 0) hole++;
    const int h0 = (n - hole) / 2, h1 = h0 + hole;

    lv_canvas_fill_bg(qr, lv_color_hex(UI_TEXT), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(qr, &layer);
    /* dots, a little smaller than a module */
    const int32_t dot = m * 7 / 8 > 1 ? m * 7 / 8 : m;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!qrcodegen_getModule(code, x, y) || qr_in_finder(n, x, y)) continue;
            if (x >= h0 && x < h1 && y >= h0 && y < h1) continue;
            qr_rect(&layer, o + x * m + (m - dot) / 2, o + y * m + (m - dot) / 2, dot, LV_RADIUS_CIRCLE, UI_BG);
        }
    }
    /* finder marks: rounded ring and core */
    const int corners[3][2] = {{0, 0}, {n - 7, 0}, {0, n - 7}};
    for (int i = 0; i < 3; i++) {
        const int32_t x = o + corners[i][0] * m, y = o + corners[i][1] * m;
        qr_rect(&layer, x, y, 7 * m, 2 * m, UI_BG);
        qr_rect(&layer, x + m, y + m, 5 * m, m * 3 / 2, UI_TEXT);
        qr_rect(&layer, x + 2 * m, y + 2 * m, 3 * m, m, UI_BG);
    }
    /* the logo, half a module inside the hole */
    const int32_t logo = (hole - 1) * m;
    const int32_t pane = logo * 44 / 160, gap = pane * 14 / 44;
    const int32_t lx = o + h0 * m + m / 2, ly = lx;
    for (int i = 0; i < 9; i++) {
        qr_rect(&layer, lx + (i % 3) * (pane + gap), ly + (i / 3) * (pane + gap), pane, pane * 12 / 44,
                i == 2 ? UI_LOGO_LIT_LIGHT : UI_LOGO_PANE_LIGHT);
    }
    lv_canvas_finish_layer(qr, &layer);
    return true;
}

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_toast_timer = NULL; /* auto-deleted after this call */
    if (s_toast) lv_obj_delete(s_toast);
}

static void toast_deleted_cb(lv_event_t *e)
{
    if (s_toast == lv_event_get_target(e)) s_toast = NULL;
}

void ui_toast(const char *text)
{
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) lv_obj_delete(s_toast);
    lv_obj_t *t = ui_box(lv_layer_top());
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(t, 24, 0);
    lv_obj_set_style_pad_ver(t, 14, 0);
    ui_label(t, UI_FONT_BODY, UI_TEXT, text);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_add_event_cb(t, toast_deleted_cb, LV_EVENT_DELETE, NULL);
    s_toast = t;
    s_toast_timer = lv_timer_create(toast_timer_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* ── Gestures ── */

#define DOUBLE_TAP_MS 300

typedef struct {
    ui_gesture_cb_t tap, hold, double_tap;
    void *user;
    lv_timer_t *pending_tap; /* waiting whether a second tap follows */
} gestures_t;

static void pending_tap_cb(lv_timer_t *t)
{
    gestures_t *g = lv_timer_get_user_data(t);
    g->pending_tap = NULL; /* repeat count 1: deleted after this call */
    if (g->tap) g->tap(g->user);
}

static void gestures_event_cb(lv_event_t *e)
{
    gestures_t *g = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:
        if (!g->double_tap) {
            if (g->tap) g->tap(g->user);
        } else if (g->pending_tap) {
            lv_timer_delete(g->pending_tap);
            g->pending_tap = NULL;
            g->double_tap(g->user);
        } else {
            g->pending_tap = lv_timer_create(pending_tap_cb, DOUBLE_TAP_MS, g);
            lv_timer_set_repeat_count(g->pending_tap, 1);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (g->hold) g->hold(g->user);
        break;
    case LV_EVENT_DELETE:
        if (g->pending_tap) lv_timer_delete(g->pending_tap);
        free(g);
        break;
    default: break;
    }
}

void ui_gestures(lv_obj_t *obj, ui_gesture_cb_t tap, ui_gesture_cb_t hold, ui_gesture_cb_t double_tap, void *user)
{
    gestures_t *g = calloc(1, sizeof(*g));
    g->tap = tap;
    g->hold = hold;
    g->double_tap = double_tap;
    g->user = user;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_SHORT_CLICKED, g);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_LONG_PRESSED, g);
    lv_obj_add_event_cb(obj, gestures_event_cb, LV_EVENT_DELETE, g);
}

lv_obj_t *ui_toggle_large(lv_obj_t *parent)
{
    lv_obj_t *s = ui_toggle(parent);
    lv_obj_set_size(s, 112, 64);
    return s;
}

/* ── Detail views ── */

#define UI_DETAIL_FLAG LV_OBJ_FLAG_USER_2 /* marks a detail page's root */

void ui_detail_close_all(void)
{
    lv_obj_t *screen = lv_screen_active();
    for (int32_t i = (int32_t)lv_obj_get_child_count(screen) - 1; i >= 0; --i) {
        lv_obj_t *o = lv_obj_get_child(screen, i);
        if (lv_obj_has_flag(o, UI_DETAIL_FLAG)) lv_obj_delete(o);
    }
}

static void detail_back_cb(lv_event_t *e)
{
    lv_obj_delete_async(lv_event_get_user_data(e));
}

ui_detail_t ui_detail_open(const char *title, const char *subtitle)
{
    ui_detail_t d;
    d.root = ui_box(lv_screen_active());
    lv_obj_add_flag(d.root, UI_DETAIL_FLAG);
    lv_obj_remove_flag(d.root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(d.root, LV_OBJ_FLAG_CLICKABLE); /* nothing below reacts */
    lv_obj_set_size(d.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(d.root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(d.root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(d.root, UI_EDGE, 0);
    lv_obj_set_style_pad_row(d.root, UI_EDGE, 0);
    lv_obj_set_flex_flow(d.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(d.root, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *head = ui_box(d.root);
    lv_obj_set_size(head, LV_PCT(100), 72);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 16, 0);
    lv_obj_t *back = ui_box(head);
    lv_obj_remove_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(back, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(back, UI_ICON_M, UI_TEXT, LI_CHEVRON_LEFT));
    lv_obj_add_event_cb(back, detail_back_cb, LV_EVENT_CLICKED, d.root);

    lv_obj_t *texts = ui_box(head);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, 0);
    d.title = ui_label_line(texts, UI_FONT_TITLE, UI_TEXT, title);
    d.subtitle = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, subtitle ? subtitle : "");
    if (!subtitle) lv_obj_add_flag(d.subtitle, LV_OBJ_FLAG_HIDDEN);

    d.right = ui_box(head);
    lv_obj_set_size(d.right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(d.right, LV_OBJ_FLAG_EVENT_BUBBLE);

    d.content = ui_box(d.root);
    lv_obj_remove_flag(d.content, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_width(d.content, LV_PCT(100));
    lv_obj_set_flex_grow(d.content, 1);
    lv_obj_set_flex_flow(d.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(d.content, UI_EDGE, 0);
    return d;
}

/* ── Segmented control ── */

static void segment_cb(lv_event_t *e)
{
    lv_obj_t *seg = lv_obj_get_parent(lv_event_get_current_target_obj(e));
    const int index = (int)lv_obj_get_index(lv_event_get_current_target_obj(e));
    if (index == ui_segmented_selected(seg)) return;
    ui_segmented_select(seg, index);
    lv_obj_send_event(seg, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_segmented(lv_obj_t *parent, const char *const *labels, int count)
{
    lv_obj_t *seg = ui_box(parent);
    lv_obj_remove_flag(seg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(seg, LV_PCT(100), 64);
    lv_obj_set_style_radius(seg, 22, 0);
    lv_obj_set_style_bg_color(seg, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(seg, 6, 0);
    lv_obj_set_style_pad_column(seg, 6, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < count; ++i) {
        lv_obj_t *b = ui_box(seg);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT2), 0);
        lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT), LV_STATE_CHECKED);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, UI_FONT_NAME, 0);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, segment_cb, LV_EVENT_CLICKED, NULL);
    }
    ui_segmented_select(seg, 0);
    return seg;
}

void ui_segmented_select(lv_obj_t *seg, int index)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(seg); ++i) {
        lv_obj_t *b = lv_obj_get_child(seg, (int32_t)i);
        if ((int)i == index) lv_obj_add_state(b, LV_STATE_CHECKED);
        else lv_obj_remove_state(b, LV_STATE_CHECKED);
    }
}

int ui_segmented_selected(lv_obj_t *seg)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(seg); ++i) {
        if (lv_obj_has_state(lv_obj_get_child(seg, (int32_t)i), LV_STATE_CHECKED)) return (int)i;
    }
    return -1;
}

/* ── Slider row ── */

typedef struct {
    int value;
} slider_row_t;

static void slider_row_event_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    slider_row_t *st = lv_obj_get_user_data(row);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        free(st);
        return;
    }
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_PRESSED) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(row, &a);
    const int32_t w = lv_area_get_width(&a);
    int v = w > 0 ? (int)((p.x - a.x1) * 100 / w) : 0;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    if (v == st->value) return;
    st->value = v;
    lv_obj_set_width(lv_obj_get_child(row, 0), LV_PCT(v));
    lv_obj_send_event(row, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_slider_row(lv_obj_t *parent, const char *icon, const char *label)
{
    lv_obj_t *row = ui_box(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(row, LV_PCT(100), 72);
    lv_obj_set_style_radius(row, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(row, true, 0);
    slider_row_t *st = calloc(1, sizeof(*st));
    lv_obj_set_user_data(row, st);

    lv_obj_t *fill = ui_box(row); /* [0] */
    lv_obj_set_size(fill, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(fill, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);

    lv_obj_t *left = ui_box(row); /* [1] */
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 12, 0);
    if (icon) ui_label(left, UI_ICON_BUTTON, UI_TEXT2, icon);
    ui_label(left, UI_FONT_BODY, UI_TEXT, label);

    lv_obj_t *value = ui_label(row, UI_FONT_NAME, UI_TEXT, ""); /* [2] */
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -24, 0);

    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(row, slider_row_event_cb, LV_EVENT_DELETE, NULL);
    return row;
}

void ui_slider_row_set(lv_obj_t *row, int value, const char *value_text)
{
    slider_row_t *st = lv_obj_get_user_data(row);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    st->value = value;
    lv_obj_set_width(lv_obj_get_child(row, 0), LV_PCT(value));
    lv_label_set_text(lv_obj_get_child(row, 2), value_text ? value_text : "");
}

int ui_slider_row_value(lv_obj_t *row)
{
    slider_row_t *st = lv_obj_get_user_data(row);
    return st ? st->value : 0;
}
