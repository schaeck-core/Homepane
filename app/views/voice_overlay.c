/**
 * @file voice_overlay.c
 * @brief Voice assistant: push-to-talk button + state overlay
 *
 * Follows docs/design/DESIGN_GUIDE.md and the voice design variant A
 * (docs/design/canvas/VoiceAssistant.dc.html), reduced to what the test
 * needs: a sand circle that grows with the microphone level while
 * listening, a ring while processing, transcript and response text.
 */

#include "voice_overlay.h"
#include "voice_overlay_b.h"
#include "text_fit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "../i18n.h"
#include "../config.h"
#include "../ui_theme.h"
#include "../cpp/app_facade.h"
#include "../fonts.h"

#define DG_BLACK     0x000000
#define DG_SURFACE   0x141413
#define DG_ACTIVE    0x2B2A27
#define DG_SAND      0xD6B98A
#define DG_TEXT      0xECE9E3
#define DG_TEXT_2    0xA39F97
#define DG_WARN      0xD98A73

#define CORE_D       128 /* core circle, as in the design (r 64) */

static lv_obj_t *s_button;
static lv_obj_t *s_overlay;
static lv_obj_t *s_halo;
static lv_obj_t *s_core;
static lv_obj_t *s_core_icon;
static lv_obj_t *s_spinner;
static lv_obj_t *s_label;
static lv_obj_t *s_transcript;
static lv_obj_t *s_response;
static lv_timer_t *s_anim_timer;

static void button_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_voice_button();
}

static void overlay_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_voice_status_t st;
    app_cpp_voice_get_status(&st);
    /* Tap while listening = "done talking"; otherwise close. */
    if (st.state == APP_CPP_VOICE_LISTENING) app_cpp_voice_button();
    else app_cpp_voice_cancel();
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    lv_display_trigger_activity(NULL); /* long questions: keep the screen on */
    float level = app_cpp_voice_level();
    int32_t halo = CORE_D + 40 + (int32_t)(level * 160.0f);
    lv_obj_set_size(s_halo, halo, halo);
    lv_obj_align(s_halo, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_bg_opa(s_halo, (lv_opa_t)(25 + level * 40), 0);
}

static lv_obj_t *circle(lv_obj_t *parent, int32_t d, uint32_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, d, d);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, -60);
    return c;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, 600);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

static void build_overlay(void)
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(DG_BLACK), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_cb, LV_EVENT_CLICKED, NULL);

    s_label = text(s_overlay, font_manrope_500_22, DG_TEXT_2, 48);

    s_halo = circle(s_overlay, CORE_D + 40, DG_SAND);
    lv_obj_set_style_bg_opa(s_halo, LV_OPA_20, 0);

    s_spinner = lv_spinner_create(s_overlay);
    lv_obj_set_size(s_spinner, 216, 216);
    lv_obj_align(s_spinner, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(DG_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(DG_SAND), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);
    lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_CLICKABLE);

    s_core = circle(s_overlay, CORE_D, DG_SAND);
    s_core_icon = lv_label_create(s_core);
    lv_obj_set_style_text_font(s_core_icon, UI_ICON_VALUE, 0);
    lv_obj_set_style_text_color(s_core_icon, lv_color_hex(DG_BLACK), 0);
    lv_label_set_text(s_core_icon, LI_MIC);
    lv_obj_center(s_core_icon);

    s_transcript = text(s_overlay, font_manrope_600_26, DG_TEXT, 480);
    s_response = text(s_overlay, font_manrope_500_22, DG_TEXT_2, 556);
    /* Fixed boxes: long texts scroll slowly instead of growing; the answer
     * keeps its place on the overlay. */
    lv_obj_set_size(s_transcript, 600, 66);
    lv_obj_set_size(s_response, 520, 80);
    lv_obj_t *boxes[] = {s_transcript, s_response};
    for (int i = 0; i < 2; ++i) {
        lv_label_set_long_mode(boxes[i], LV_LABEL_LONG_MODE_SCROLL);
        lv_obj_set_style_anim_duration(boxes[i], lv_anim_speed_clamped(18, 2000, 30000), 0);
    }

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *s_covered_screen;

/* The app may replace (and delete) the screen while the overlay is up. */
static void covered_screen_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_covered_screen) s_covered_screen = NULL;
}

void voice_overlay_cover_screen(bool cover)
{
    lv_obj_t *active = lv_screen_active();
    if (cover) {
        if (s_covered_screen == active) return;
        voice_overlay_cover_screen(false);
        s_covered_screen = active;
        /* Not LV_OBJ_FLAG_HIDDEN: un-hiding a screen dereferences its (NULL)
         * parent in this LVGL version. A transparent layered opacity makes
         * the refresh skip the screen and all its children just the same. */
        lv_obj_set_style_opa_layered(active, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(active, covered_screen_deleted_cb, LV_EVENT_DELETE, NULL);
    } else if (s_covered_screen) {
        lv_obj_remove_event_cb(s_covered_screen, covered_screen_deleted_cb);
        lv_obj_set_style_opa_layered(s_covered_screen, LV_OPA_COVER, 0);
        s_covered_screen = NULL;
    }
}

static bool use_variant_b(void)
{
    const char *ui = getConfig("voice_ui");
    return ui && strcmp(ui, "b") == 0;
}

static void refresh(void *user)
{
    (void)user;
    app_cpp_voice_status_t st;
    app_cpp_voice_get_status(&st);

    if (use_variant_b()) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        if (s_anim_timer) {
            lv_timer_delete(s_anim_timer);
            s_anim_timer = NULL;
        }
        voice_overlay_b_update(&st);
        return;
    }
    {
        /* variant A: make sure B is hidden */
        app_cpp_voice_status_t idle = {0};
        idle.state = APP_CPP_VOICE_IDLE;
        voice_overlay_b_update(&idle);
    }

    const bool active = st.state != APP_CPP_VOICE_IDLE;
    /* Counts as user activity: wakes the screen from power save (the main
     * loop wakes on inactivity < timeout) and keeps it on while the
     * assistant is active — e.g. after the wake word on a dark screen. */
    if (active) lv_display_trigger_activity(NULL);
    if (active) lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    voice_overlay_cover_screen(active);

    const bool listening = st.state == APP_CPP_VOICE_LISTENING;
    if (listening && !s_anim_timer) s_anim_timer = lv_timer_create(anim_cb, 33, NULL);
    if (!listening && s_anim_timer) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
    }
    if (listening) lv_obj_remove_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
    if (st.state == APP_CPP_VOICE_PROCESSING) lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    const char *label = "";
    uint32_t core = DG_SAND;
    switch (st.state) {
    case APP_CPP_VOICE_LISTENING: label = tr(TR_VOICE_LISTENING); break;
    case APP_CPP_VOICE_PROCESSING: label = tr(TR_VOICE_PROCESSING); break;
    case APP_CPP_VOICE_SPEAKING: label = tr(TR_VOICE_ANSWER); break;
    case APP_CPP_VOICE_DONE: label = tr(TR_VOICE_DONE); break;
    case APP_CPP_VOICE_ERROR: label = tr(TR_STATE_ERROR); core = DG_ACTIVE; break;
    default: break;
    }
    lv_label_set_text(s_label, label);
    lv_obj_set_style_bg_color(s_core, lv_color_hex(core), 0);
    lv_obj_set_style_text_color(s_core_icon, lv_color_hex(st.state == APP_CPP_VOICE_ERROR ? DG_TEXT : DG_BLACK), 0);

    /* texts from HA/the LLM: substitute or drop characters the fonts lack */
    static char fitted[600];
    text_fit_font(font_manrope_600_26, st.transcript, fitted, sizeof(fitted));
    lv_label_set_text(s_transcript, fitted);
    const bool err = st.state == APP_CPP_VOICE_ERROR;
    lv_obj_set_style_text_color(s_response, lv_color_hex(err ? DG_WARN : DG_TEXT_2), 0);
    text_fit_font(font_manrope_500_22, err ? st.error : st.response, fitted, sizeof(fitted));
    lv_label_set_text(s_response, fitted);
}

static bool config_on(const char *key)
{
    const char *v = getConfig(key);
    return v && strcmp(v, "1") == 0;
}

void voice_overlay_apply_settings(void)
{
    const bool enabled = config_on("voice_enabled") && config_on("microphone_enabled");
    const char *wake = getConfig("voice_wake_word");
    if (!wake || !wake[0]) wake = "okay_nabu"; /* default */
    if (strcmp(wake, "none") == 0) wake = "";
    if (!app_cpp_voice_configure(enabled, wake) && enabled && wake[0]) {
        printf("[Voice] wake word '%s' unavailable; button only\n", wake);
    }
    const char *sound = getConfig("voice_sound");
    app_cpp_voice_set_sound(sound && sound[0] ? sound : "chime");
    if (enabled) { /* do not power up the audio codecs for a disabled assistant */
        const char *vol = getConfig("voice_volume");
        app_cpp_voice_set_volume(vol && vol[0] ? atoi(vol) : 90);
    }
    if (s_button) {
        if (enabled) lv_obj_remove_flag(s_button, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_button, LV_OBJ_FLAG_HIDDEN);
    }
}

void voice_overlay_init(void)
{
    if (s_button) return;
    build_overlay();
    voice_overlay_b_init(overlay_cb);

    /* The push-to-talk button lives in the dashboard header (dashboard.c);
     * a floating button covered tiles and detail pages. */
    app_cpp_voice_set_listener(refresh, NULL);
    voice_overlay_apply_settings();
}
