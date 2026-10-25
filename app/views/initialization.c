#include "views.h"
#include "setup_frame.h"
#include "keyboard_style.h"
#include "../fonts.h"
#include "../ui_kit.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../i18n.h"
#include "../config.h"
#include "../utils.h"
#include "../network.h"

// First-run setup until Home Assistant is paired (setup_frame.h for the
// look): language -> network -> Home Assistant (QR login on the phone, or a
// long-lived token). The dashboard choice follows in setup.c once a token is
// saved (enter_setup_state()).
//
// The QR login: the phone opens HA's authorize page with this panel as the
// OAuth client; HA redirects to the panel's web server (port 8989,
// webserver.c / webserver_esp32.c), which exchanges the code and pairs.
// Errors come back through initialization_pairing_failed().

static void show_language(void);
static void show_network(bool autoskip);
static void show_home_assistant(void);
static void show_token(void);

static lv_obj_t *s_parent;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_qr;
static lv_obj_t *s_url_ta;
static lv_obj_t *s_token_ta;
static lv_obj_t *s_message;

static void reset_handles(void)
{
    s_keyboard = NULL;
    s_qr = NULL;
    s_url_ta = NULL;
    s_token_ta = NULL;
    s_message = NULL;
}

// ── Language ─────────────────────────────────────────────────────────────────

static void language_row_cb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    const char *code = i18n_languages[index].code;
    updateConfig("language", code);
    saveConfig();
    i18n_set_language(code);
    show_language(); // the page in the new language
}

static void language_next_cb(lv_event_t *e)
{
    (void)e;
    // the choice is the default until the user picks another one
    const char *lang = getConfig("language");
    if (!lang || !lang[0]) {
        updateConfig("language", i18n_language());
        saveConfig();
    }
    show_network(true);
}

static void show_language(void)
{
    reset_handles();
    setup_frame_t f = setup_frame_create(s_parent, SETUP_STEP_LANGUAGE, tr(TR_SETUP_WELCOME),
                                         tr(TR_SETUP_WELCOME_HINT), NULL);
    lv_obj_add_flag(f.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(f.content, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < i18n_language_count; i++) {
        const bool selected = strcmp(i18n_languages[i].code, i18n_language()) == 0;
        setup_choice_row(f.content, LI_LANGUAGES, i18n_languages[i].name, NULL, selected, language_row_cb,
                         (void *)(intptr_t)i);
    }
    setup_button(f.footer, NULL, tr(TR_SETUP_CONTINUE), true, language_next_cb, NULL);
}

// ── Network ──────────────────────────────────────────────────────────────────

static void network_back(void) { show_language(); }
static void network_done(void) { show_home_assistant(); }

static void show_network(bool autoskip)
{
    reset_handles();
    network_setup_render(s_parent, network_done, network_back, autoskip);
}

// ── Home Assistant ───────────────────────────────────────────────────────────

/* The QR code holds a short link to the panel, which sends the phone on to
 * Home Assistant's login (/ha in the web server): a short text keeps the
 * modules big enough for dots and the logo in the middle. */
static void update_qr(void)
{
    if (!s_qr) return;
    char url[64];
    snprintf(url, sizeof(url), "http://%s:8989/ha", getLocalIp());
    ui_qr_set(s_qr, url);
}

// "192.0.2.5:8123" -> "http://192.0.2.5:8123"; no trailing slash.
static void save_url(const char *text)
{
    char url[256];
    while (*text == ' ') text++;
    if (strstr(text, "://")) {
        snprintf(url, sizeof(url), "%s", text);
    } else {
        snprintf(url, sizeof(url), "http://%s", text);
    }
    size_t n = strlen(url);
    while (n > 0 && (url[n - 1] == '/' || url[n - 1] == ' ')) url[--n] = '\0';
    updateConfig("haas_url", url);
    saveConfig();
}

static void hide_keyboard(void)
{
    if (!s_keyboard) return;
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void textarea_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (code == LV_EVENT_FOCUSED && s_keyboard) {
        lv_keyboard_set_textarea(s_keyboard, ta);
        lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        hide_keyboard();
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        if (ta == s_url_ta) {
            save_url(lv_textarea_get_text(ta));
            lv_textarea_set_text(ta, getConfig("haas_url"));
            update_qr();
        }
    }
}

static void create_keyboard(void)
{
    s_keyboard = lv_keyboard_create(s_parent);
    ui_style_keyboard(s_keyboard);
    lv_obj_set_size(s_keyboard, LV_PCT(100), LV_PCT(48));
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// Row "label | text field" (88 high, the field fills the rest).
static lv_obj_t *field_row(lv_obj_t *parent, const char *label, const char *text, const char *placeholder)
{
    lv_obj_t *row = ui_box(parent);
    lv_obj_set_size(row, LV_PCT(100), 88);
    lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_pad_hor(row, 20, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_t *name = ui_label(row, UI_FONT_NAME, UI_TEXT, label);
    lv_obj_set_width(name, 110); // the fields of both rows start in line

    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_size(ta, 0, 60);
    lv_obj_set_flex_grow(ta, 1);
    lv_textarea_set_text(ta, text ? text : "");
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(UI_TEXT3), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, lv_color_hex(UI_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 14, 0);
    lv_obj_set_style_pad_hor(ta, 14, 0);
    lv_obj_set_style_pad_ver(ta, 15, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UI_ACCENT), LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, textarea_cb, LV_EVENT_ALL, NULL);
    return ta;
}

static void qr_step(lv_obj_t *parent, const char *number, const char *text)
{
    lv_obj_t *row = ui_box(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *num = ui_box(row);
    lv_obj_set_size(num, 36, 36);
    lv_obj_set_style_radius(num, 18, 0);
    lv_obj_set_style_bg_opa(num, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(num, lv_color_hex(UI_ACCENT_BG), 0);
    lv_obj_center(ui_label(num, UI_FONT_LABEL, UI_ACCENT, number));
    lv_obj_t *l = ui_label(row, UI_FONT_BODY, UI_TEXT, text);
    lv_obj_set_width(l, 0);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}

/* The card with the QR code (the short login link) and the three steps. */
static lv_obj_t *qr_card(lv_obj_t *parent, lv_obj_t **qr)
{
    lv_obj_t *card = ui_box(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 24, 0);

    // light quiet zone around the code: phones read it more reliably
    lv_obj_t *qr_box = ui_box(card);
    lv_obj_set_size(qr_box, 264, 264);
    lv_obj_set_style_radius(qr_box, 16, 0);
    lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(qr_box, lv_color_hex(UI_TEXT), 0);
    *qr = ui_qr(qr_box, 232);
    lv_obj_center(*qr);

    lv_obj_t *steps = ui_box(card);
    lv_obj_set_size(steps, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(steps, 1);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(steps, 18, 0);
    qr_step(steps, "1", tr(TR_SETUP_QR_STEP1));
    qr_step(steps, "2", tr(TR_SETUP_QR_STEP2));
    qr_step(steps, "3", tr(TR_SETUP_QR_STEP3));
    return card;
}

static void ha_back_cb(lv_event_t *e) { (void)e; show_network(false); }
static void ha_token_cb(lv_event_t *e) { (void)e; show_token(); }

static void show_home_assistant(void)
{
    reset_handles();
    setup_frame_t f = setup_frame_create(s_parent, SETUP_STEP_HOME_ASSISTANT, tr(TR_SETUP_HA_TITLE),
                                         tr(TR_SETUP_HA_HINT), ha_back_cb);
    s_url_ta = field_row(f.content, tr(TR_SETUP_ADDRESS), getConfig("haas_url"), "http://homeassistant.local:8123");

    qr_card(f.content, &s_qr);
    update_qr();

    s_message = setup_message(f.content, "", true);
    lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);

    setup_button(f.footer, LI_KEY_ROUND, tr(TR_SETUP_USE_TOKEN), false, ha_token_cb, NULL);
    create_keyboard();
}

void initialization_pairing_failed(const char *detail)
{
    (void)detail; // technical; the log has it
    if (!s_message) return;
    lv_label_set_text(s_message, tr(TR_SETUP_PAIRING_FAILED));
    lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
}

/* Home Assistant already has a long-lived token with this panel's name (an
 * earlier pairing whose token was not deleted): say which one to delete. */
void initialization_pairing_token_exists(const char *token_name)
{
    if (!s_message) return;
    char text[320];
    const char *args[] = {token_name ? token_name : "Homepane"};
    tr_format(text, sizeof(text), TR_SETUP_PAIRING_TOKEN_EXISTS, args, 1);
    lv_label_set_text(s_message, text);
    lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
}

// ── Token ────────────────────────────────────────────────────────────────────

static void token_back_cb(lv_event_t *e) { (void)e; show_home_assistant(); }

static void token_save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_token_ta) return;
    const char *token = lv_textarea_get_text(s_token_ta);
    while (token && *token == ' ') token++;
    if (!token || !token[0]) {
        lv_label_set_text(s_message, tr(TR_SETUP_TOKEN_MISSING));
        lv_obj_remove_flag(s_message, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    save_url(lv_textarea_get_text(s_url_ta));
    updateConfig("haas_token", token);
    saveConfig();
    enter_setup_state(); // dashboard choice (setup.c)
}

static void show_token(void)
{
    reset_handles();
    setup_frame_t f = setup_frame_create(s_parent, SETUP_STEP_HOME_ASSISTANT, tr(TR_SETUP_TOKEN_TITLE),
                                         tr(TR_SETUP_TOKEN_HINT), token_back_cb);
    s_url_ta = field_row(f.content, tr(TR_SETUP_ADDRESS), getConfig("haas_url"), "http://homeassistant.local:8123");
    s_token_ta = field_row(f.content, tr(TR_SETUP_TOKEN), "", "eyJhbGciOi…");
    s_message = setup_message(f.content, "", true);
    lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    setup_button(f.footer, NULL, tr(TR_SETUP_SAVE_CONTINUE), true, token_save_cb, NULL);
    create_keyboard();
}

void initialization_render(lv_obj_t *parent)
{
    s_parent = parent;
    const char *lang = getConfig("language");
    if (!lang || !lang[0]) {
        show_language();
    } else {
        show_network(true);
    }
}

/* Developer builds: the login step's QR card over everything, without
 * touching the configuration (Settings > General). Scanning it opens the
 * panel's /ha, which only answers while the setup's web server runs. */
static void qr_preview_close_cb(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_user_data(e));
}

void initialization_qr_preview(void)
{
    lv_obj_t *overlay = ui_box(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);
    setup_frame_t f = setup_frame_create(overlay, SETUP_STEP_HOME_ASSISTANT, tr(TR_SETUP_HA_TITLE),
                                         tr(TR_SETUP_HA_HINT), NULL);
    lv_obj_t *qr;
    qr_card(f.content, &qr);
    char url[64];
    snprintf(url, sizeof(url), "http://%s:8989/ha", getLocalIp());
    ui_qr_set(qr, url);
    setup_button(f.footer, LI_X, tr(TR_SETUP_PREVIEW_CLOSE), true, qr_preview_close_cb, overlay);
}
