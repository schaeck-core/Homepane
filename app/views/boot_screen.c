#include "boot_screen.h"

#include "../cpp/app_facade.h"
#include "../fonts.h"
#include "../i18n.h"
#include "../ui_kit.h"

#include <stdio.h>

#define LOGO_PANE 44
#define BAR_W 240
#define INTRO_MS 2200 /* the intro and a moment of "Ready": it always plays out */

static lv_obj_t *s_root;     /* on the top layer; NULL when not shown */
static lv_obj_t *s_status;   /* the status column: text and bar */
static lv_obj_t *s_text;
static lv_obj_t *s_bar;
static lv_obj_t *s_fill;
static lv_obj_t *s_button;
static lv_obj_t *s_lit;      /* the logo's lit pane (it pulses while waiting) */
static lv_timer_t *s_poll;   /* Home Assistant connected? */
static boot_step_t s_step;
static bool s_done;          /* finished: later dashboards are no start */
static void (*s_on_button)(void);
static uint32_t s_shown_at;

static void anim_bg_opa(void *obj, int32_t v) { lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0); }
static void anim_text_opa(void *obj, int32_t v) { lv_obj_set_style_text_opa(obj, (lv_opa_t)v, 0); }
static void anim_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }
static void anim_translate_y(void *obj, int32_t v) { lv_obj_set_style_translate_y(obj, v, 0); }
static void anim_width(void *obj, int32_t v) { lv_obj_set_width(obj, v); }

/* One value of one object from `from` to `to`, starting after `delay` ms
 * (the canvas's timeline), easing out. */
static void animate(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t delay, uint32_t time)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, time);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void root_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll) lv_timer_delete(s_poll);
    s_poll = NULL;
    s_root = s_status = s_text = s_bar = s_fill = s_button = s_lit = NULL;
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    if (s_step == BOOT_STEP_HA && app_cpp_ha_connected()) boot_screen_step(BOOT_STEP_DASHBOARD);
}

void boot_screen_show(void)
{
    if (s_root || s_done) return;
    s_root = ui_box(lv_layer_top());
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE); /* nothing below it gets the touches */
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_root, root_delete_cb, LV_EVENT_DELETE, NULL);
    s_shown_at = lv_tick_get();

    /* the logo: the dark panes one by one, the lit one last */
    lv_obj_t *logo = ui_logo(s_root, LOGO_PANE, false);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -84);
    for (int i = 0, n = 0; i < 9; i++) {
        lv_obj_t *pane = lv_obj_get_child(logo, i);
        lv_obj_set_style_bg_opa(pane, LV_OPA_TRANSP, 0);
        if (i == 2) animate(pane, anim_bg_opa, LV_OPA_TRANSP, LV_OPA_COVER, 700, 350);
        else animate(pane, anim_bg_opa, LV_OPA_TRANSP, LV_OPA_COVER, 70 * n++, 160);
    }
    s_lit = lv_obj_get_child(logo, 2);
    ui_logo_pulse(logo, 1800); /* a longer start: the lit pane pulses */

    /* the wordmark rises a little as it appears */
    lv_obj_t *word = ui_label(s_root, font_manrope_700_56, UI_TEXT, "homepane");
    lv_obj_set_style_text_letter_space(word, -2, 0);
    lv_obj_align_to(word, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 36);
    lv_obj_set_style_text_opa(word, LV_OPA_TRANSP, 0);
    animate(word, anim_text_opa, LV_OPA_TRANSP, LV_OPA_COVER, 1000, 500);
    animate(word, anim_translate_y, 8, 0, 1000, 500);

    /* the status: text and progress bar */
    s_status = ui_box(s_root);
    lv_obj_set_size(s_status, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_status, 16, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 196);
    s_text = ui_label(s_status, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    s_bar = ui_box(s_status);
    lv_obj_set_size(s_bar, BAR_W, 4);
    lv_obj_set_style_radius(s_bar, 2, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(s_bar, false, 0);
    s_fill = ui_box(s_bar);
    lv_obj_set_size(s_fill, 0, 4);
    lv_obj_set_style_radius(s_fill, 2, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(UI_TEXT3), 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_status, LV_OPA_TRANSP, 0);
    animate(s_status, anim_opa, LV_OPA_TRANSP, LV_OPA_COVER, 1500, 300);

    app_cpp_update_status_t st;
    app_cpp_update_get_status(&st);
    if (st.installed_version[0]) {
        const char *args[] = {st.installed_version};
        char version[64];
        tr_format(version, sizeof(version), TR_BOOT_VERSION, args, 1);
        lv_obj_t *v = ui_label(s_root, UI_FONT_LABEL, UI_MUTED, version);
        lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -32);
    }
}

void boot_screen_step(boot_step_t step)
{
    if (!s_root) return;
    static const int32_t progress[] = {15, 45, 75}; /* % of the bar per step */
    const tr_key_t text[] = {TR_BOOT_WIFI, TR_BOOT_HA, TR_BOOT_DASHBOARD};
    s_step = step;
    lv_label_set_text(s_text, tr(text[step]));
    lv_anim_delete(s_fill, anim_width);
    animate(s_fill, anim_width, lv_obj_get_width(s_fill), BAR_W * progress[step] / 100, 0, 600);
    if (step == BOOT_STEP_HA && !s_poll) s_poll = lv_timer_create(poll_cb, 200, NULL);
}

static void button_cb(lv_event_t *e)
{
    (void)e;
    void (*cb)(void) = s_on_button;
    boot_screen_hide();
    if (cb) cb();
}

void boot_screen_error(const char *text, const char *button, void (*on_button)(void))
{
    if (!s_root) return;
    if (s_poll) lv_timer_delete(s_poll);
    s_poll = NULL;
    lv_label_set_text(s_text, text);
    lv_obj_set_style_text_color(s_text, lv_color_hex(UI_WARN), 0);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    s_on_button = on_button;
    if (!button || s_button) return;
    s_button = lv_button_create(s_status);
    lv_obj_remove_style_all(s_button);
    lv_obj_set_size(s_button, LV_SIZE_CONTENT, UI_TOUCH_MIN);
    lv_obj_set_style_pad_hor(s_button, 28, 0);
    lv_obj_set_style_radius(s_button, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_color(s_button, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_color(s_button, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_button, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_button, button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = ui_label(s_button, UI_FONT_NAME, UI_TEXT, button);
    lv_obj_center(label);
}

static void finished_cb(lv_timer_t *t)
{
    (void)t;
    boot_screen_hide();
}

void boot_screen_finish(void)
{
    s_done = true;
    if (!s_root) return;
    if (s_poll) lv_timer_delete(s_poll);
    s_poll = NULL;
    lv_label_set_text(s_text, tr(TR_BOOT_READY));
    lv_anim_delete(s_lit, NULL); /* the pulse (or a fade still running): lit */
    lv_obj_set_style_bg_color(s_lit, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_lit, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(UI_ACCENT), 0);
    lv_anim_delete(s_fill, anim_width);
    animate(s_fill, anim_width, lv_obj_get_width(s_fill), BAR_W, 0, 300);
    /* the full bar for a moment, and the intro to its end */
    const uint32_t shown = lv_tick_elaps(s_shown_at);
    const uint32_t wait = shown + 500 < INTRO_MS ? INTRO_MS - shown : 500;
    lv_timer_t *t = lv_timer_create(finished_cb, wait, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void boot_screen_hide(void)
{
    s_done = true;
    if (s_root) lv_obj_delete(s_root); /* root_delete_cb clears the rest */
}

bool boot_screen_active(void)
{
    return !s_done;
}
