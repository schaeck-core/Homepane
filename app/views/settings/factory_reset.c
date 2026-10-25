#include "factory_reset.h"

#include "settings.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"
#include "../../fonts.h"
#include "../../i18n.h"
#include "../../lucide_icons.h"
#include "../../ui_kit.h"

#include <stdio.h>

#define BAR_W 240
#define SIGN_OUT_TIMEOUT_MS 8000 /* Home Assistant does not answer: go on without it */

typedef enum { STEP_IDLE, STEP_SIGN_OUT, STEP_DELETE, STEP_RESTART } step_t;

static lv_obj_t *s_screen; /* the question, then the progress (top layer) */
static lv_obj_t *s_text;
static lv_obj_t *s_fill;
static lv_timer_t *s_timeout;
static step_t s_step = STEP_IDLE;

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    s_screen = s_text = s_fill = NULL;
}

/* A black page over everything, nothing below gets the touches. */
static lv_obj_t *screen_create(void)
{
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = ui_box(lv_layer_top());
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
    return s_screen;
}

static void anim_width(void *obj, int32_t v) { lv_obj_set_width(obj, v); }

static void show_step(step_t step, tr_key_t text, int percent)
{
    s_step = step;
    if (!s_screen) return;
    lv_label_set_text(s_text, tr(text));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_fill);
    lv_anim_set_exec_cb(&a, anim_width);
    lv_anim_set_values(&a, lv_obj_get_width(s_fill), BAR_W * percent / 100);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ── the steps: each one on its own LVGL timer, out of the caller's stack
 * (the sign-out answer arrives deep in the Home Assistant client) ── */

static void restart_cb(lv_timer_t *t)
{
    (void)t;
    if (app_cpp_restart()) return;
    /* the simulator does not restart: straight into the setup */
    if (s_screen) lv_obj_delete(s_screen);
    s_step = STEP_IDLE;
    settings_close();
    app_cpp_dashboard_release_ui(); /* its widgets go before their objects */
    loadConfig();
    app_cpp_enter_initial_state();
}

static void delete_cb(lv_timer_t *t)
{
    (void)t;
    app_cpp_factory_reset();
    show_step(STEP_RESTART, TR_RESET_STEP_RESTART, 100);
    if (s_fill) lv_obj_set_style_bg_color(s_fill, lv_color_hex(UI_ACCENT), 0);
    lv_timer_t *next = lv_timer_create(restart_cb, 900, NULL);
    lv_timer_set_repeat_count(next, 1);
}

static void signed_out(void)
{
    if (s_step != STEP_SIGN_OUT) return; /* the answer after the timeout */
    if (s_timeout) lv_timer_delete(s_timeout);
    s_timeout = NULL;
    show_step(STEP_DELETE, TR_RESET_STEP_SETTINGS, 70);
    lv_timer_t *next = lv_timer_create(delete_cb, 600, NULL);
    lv_timer_set_repeat_count(next, 1);
}

static void revoke_done(bool ok, void *user)
{
    (void)user;
    if (!ok) printf("[Reset] Could not delete the panel's token in Home Assistant\n");
    signed_out();
}

static void timeout_cb(lv_timer_t *t)
{
    (void)t;
    s_timeout = NULL; /* a one-shot timer deletes itself */
    printf("[Reset] Home Assistant did not answer, resetting without signing out\n");
    signed_out();
}

static void start(void)
{
    lv_obj_t *screen = screen_create();
    lv_obj_t *logo = ui_loader(screen, 44);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -84);

    lv_obj_t *status = ui_box(screen);
    lv_obj_set_size(status, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(status, 16, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 120);
    s_text = ui_label(status, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_t *bar = ui_box(status);
    lv_obj_set_size(bar, BAR_W, 4);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    s_fill = ui_box(bar);
    lv_obj_set_size(s_fill, 0, 4);
    lv_obj_set_style_radius(s_fill, 2, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(UI_TEXT3), 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);

    /* first the panel's token in Home Assistant: left there, pairing again
     * under the same name fails */
    show_step(STEP_SIGN_OUT, TR_RESET_STEP_HA, 30);
    s_timeout = lv_timer_create(timeout_cb, SIGN_OUT_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(s_timeout, 1);
    app_cpp_ha_revoke_token(revoke_done, NULL);
}

/* ── the question ── */

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_screen) lv_obj_delete(s_screen);
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    start();
}

void factory_reset_ask(void)
{
    if (s_step != STEP_IDLE) return; /* already resetting */
    lv_obj_t *screen = screen_create();
    lv_obj_set_style_pad_all(screen, UI_EDGE, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(screen, 20, 0);

    lv_obj_t *body = ui_box(screen);
    lv_obj_set_size(body, LV_PCT(100), 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 20, 0);
    ui_bubble(body, 96, UI_ICON_BUTTON, LI_TRIANGLE_ALERT, UI_LOOK_WARN);
    ui_label(body, UI_FONT_TITLE, UI_TEXT, tr(TR_RESET_ASK_TITLE));
    lv_obj_t *text = ui_label(body, UI_FONT_BODY, UI_TEXT2, tr(TR_RESET_ASK_TEXT));
    lv_obj_set_width(text, 560);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *buttons = ui_box(screen);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, UI_GAP, 0);
    ui_action_button(buttons, tr(TR_RESET_CANCEL), UI_LOOK_INFO, cancel_cb, NULL);
    ui_action_button(buttons, tr(TR_RESET_CONFIRM), UI_LOOK_WARN, confirm_cb, NULL);
}
