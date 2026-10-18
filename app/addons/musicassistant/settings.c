/**
 * @file settings.c
 * @brief Music Assistant settings: the connection (status, server, user,
 *        password, connect), the room's player, opening the player, the
 *        cover cache on the SD card. The player itself: app/cpp/ui/music.
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "../../ui_kit.h"
#include "settings.h"
#include "addon.h"
#include "../../views/settings/settings_tab.h"
#include "../../views/settings/settings.h"
#include "../../config.h"
#include "../../fonts.h"
#include "../../cpp/app_facade.h"

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_url = NULL;
static lv_obj_t *s_url_hint = NULL;
static lv_obj_t *s_player_dd = NULL;
static lv_obj_t *s_cache_hint = NULL;
static char s_player_shown[96];
static char **s_player_ids = NULL;
static int s_player_count = 0;
static int s_player_key = -1; /* the players listed (a hash), to rebuild on change */

/* a settings row: icon, title and an optional hint below it */
static lv_obj_t *row_with_texts(lv_obj_t *page, const char *icon, const char *title, const char *hint, lv_obj_t **hint_out)
{
    lv_obj_t *row = settings_row_create(page);
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(UI_TEXT2), 0);
    lv_obj_t *texts = lv_obj_create(row);
    lv_obj_remove_style_all(texts);
    lv_obj_remove_flag(texts, LV_OBJ_FLAG_CLICKABLE); /* a tap belongs to the row */
    lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *l = lv_label_create(texts);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_font(l, UI_FONT_NAME, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT), 0);
    if (hint) {
        lv_obj_t *h = lv_label_create(texts);
        lv_label_set_text(h, hint);
        lv_obj_set_width(h, LV_PCT(100));
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(h, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(UI_TEXT2), 0);
        if (hint_out) *hint_out = h;
    }
    return row;
}

static void field_defocused_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target(e);
    updateConfig((const char *)lv_obj_get_user_data(t), lv_textarea_get_text(t));
    saveConfig();
}

static lv_obj_t *text_field(lv_obj_t *row, const char *key, const char *placeholder, int32_t width, bool password)
{
    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_width(ta, width);
    lv_obj_set_style_text_font(ta, UI_FONT_LABEL, 0);
    const char *v = getConfig(key);
    if (v) lv_textarea_set_text(ta, v);
    lv_obj_set_user_data(ta, (void *)key);
    lv_obj_add_event_cb(ta, field_defocused_cb, LV_EVENT_DEFOCUSED, NULL);
    settings_attach_keyboard(ta);
    return ta;
}

/* ── status, players ── */

static void free_players(void)
{
    for (int i = 0; i < s_player_count; i++) free(s_player_ids[i]);
    free(s_player_ids);
    s_player_ids = NULL;
    s_player_count = 0;
}

static void render(void *user)
{
    (void)user;
    if (!s_status) return;
    static const tr_key_t states[] = {TR_SETTINGS_MA_NOT_CONNECTED, TR_SETTINGS_MA_CONNECTING, TR_SETTINGS_MA_CONNECTED,
                                      TR_SETTINGS_MA_AUTH_FAILED};
    const int st = app_cpp_music_status();
    const char *ma_user = app_cpp_music_user();
    if (st == APP_CPP_MUSIC_READY && ma_user[0]) {
        lv_label_set_text_fmt(s_status, tr(TR_SETTINGS_MA_CONNECTED_AS), ma_user);
    } else {
        lv_label_set_text(s_status, tr(states[st >= 0 && st <= 3 ? st : 0]));
    }
    lv_obj_set_style_text_color(s_status, lv_color_hex(st == APP_CPP_MUSIC_READY ? UI_OK
                                                      : st == APP_CPP_MUSIC_AUTH_FAILED ? UI_WARN
                                                                                       : UI_TEXT2), 0);

    /* the players: "automatic" and each player of MA */
    const int n = app_cpp_music_player_count();
    /* rebuilt when the players change (not only their number) */
    int key = n;
    for (int i = 0; i < n; i++) {
        for (const char *c = app_cpp_music_player_id(i); *c; c++) key = key * 31 + (unsigned char)*c;
        for (const char *c = app_cpp_music_player_name(i); *c; c++) key = key * 31 + (unsigned char)*c;
    }
    if (key == s_player_key) return;
    s_player_key = key;
    free_players();
    size_t size = strlen(tr(TR_SETTINGS_MA_AUTOMATIC)) + 1;
    for (int i = 0; i < n; i++) size += strlen(app_cpp_music_player_name(i)) + 1;
    char *opts = malloc(size);
    s_player_ids = calloc((size_t)n + 1, sizeof(char *));
    if (!opts || !s_player_ids) {
        free(opts);
        return;
    }
    strcpy(opts, tr(TR_SETTINGS_MA_AUTOMATIC));
    const char *current = getConfig("ma_player_id");
    uint32_t sel = 0;
    for (int i = 0; i < n; i++) {
        strcat(opts, "\n");
        strcat(opts, app_cpp_music_player_name(i));
        s_player_ids[s_player_count++] = strdup(app_cpp_music_player_id(i));
        if (current && strcmp(current, app_cpp_music_player_id(i)) == 0) sel = (uint32_t)s_player_count;
    }
    lv_dropdown_set_options(s_player_dd, opts);
    free(opts);
    lv_dropdown_set_selected(s_player_dd, sel, LV_ANIM_OFF);
    settings_dropdown_show_cut(s_player_dd, s_player_shown, sizeof(s_player_shown));
}

static void player_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    app_cpp_music_set_player(sel == 0 || (int)sel > s_player_count ? "" : s_player_ids[sel - 1]);
    settings_dropdown_show_cut(s_player_dd, s_player_shown, sizeof(s_player_shown));
}

/* the add-on of HA: the URL follows HA's and is not typed */
static void render_url(void)
{
    if (!s_url) return;
    const bool addon = addon_musicassistant_is_addon();
    const char *url = getConfig("musicassistant_url");
    lv_textarea_set_text(s_url, url ? url : "");
    if (addon) {
        lv_obj_add_state(s_url, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_url, LV_OPA_60, 0);
    } else {
        lv_obj_remove_state(s_url, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_url, LV_OPA_COVER, 0);
    }
    if (s_url_hint) lv_label_set_text(s_url_hint, tr(addon ? TR_SETTINGS_MA_URL_ADDON_HINT : TR_SETTINGS_MA_URL_CUSTOM_HINT));
}

static void install_changed_cb(lv_event_t *e)
{
    const bool addon = lv_dropdown_get_selected(lv_event_get_target(e)) == 0;
    addon_musicassistant_set_addon(addon);
    render_url();
}

static void connect_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
    if (focused) lv_obj_send_event(focused, LV_EVENT_DEFOCUSED, NULL); /* a field still being typed in */
    app_cpp_music_reconnect();
    app_cpp_music_set_listener(render, NULL); /* the new connection */
    s_player_key = -1;
    render(NULL);
}

static void open_cb(lv_event_t *e)
{
    (void)e;
    settings_close();
    app_cpp_music_open();
}

static void render_cache(void)
{
    if (!s_cache_hint) return;
    char n[16];
    snprintf(n, sizeof(n), "%d", app_cpp_music_cache_files());
    const char *args[] = {n};
    char text[96];
    lv_label_set_text(s_cache_hint, tr_format(text, sizeof(text), TR_SETTINGS_MA_CACHE_HINT, args, 1));
}

static void clear_cache_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_music_cache_clear();
    if (s_cache_hint) {
        const char *args[] = {"0"};
        char text[96];
        lv_label_set_text(s_cache_hint, tr_format(text, sizeof(text), TR_SETTINGS_MA_CACHE_HINT, args, 1));
    }
}

static void page_deleted_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_music_set_listener(NULL, NULL);
    s_status = s_player_dd = s_cache_hint = s_url = s_url_hint = NULL;
    s_player_key = -1;
    free_players();
}

static lv_obj_t *small_button(lv_obj_t *row, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = ui_box(row);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_pad_hor(b, 22, 0);
    lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE2), LV_STATE_PRESSED);
    lv_obj_center(ui_label(b, UI_FONT_LABEL, UI_TEXT, text));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *create_musicassistant_settings_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    lv_obj_add_event_cb(page, page_deleted_cb, LV_EVENT_DELETE, NULL);

    /* connection */
    lv_obj_t *row = row_with_texts(page, LI_WIFI, tr(TR_SETTINGS_MA_CONNECTION), "", &s_status);
    small_button(row, tr(TR_SETTINGS_MA_CONNECT), connect_cb);
    row = row_with_texts(page, LI_SERVER, tr(TR_SETTINGS_MA_INSTALL), NULL, NULL);
    lv_obj_t *install = lv_dropdown_create(row);
    lv_dropdown_set_options(install, tr(TR_SETTINGS_MA_INSTALL_OPTIONS));
    lv_obj_set_width(install, 320);
    lv_dropdown_set_selected(install, addon_musicassistant_is_addon() ? 0 : 1, LV_ANIM_OFF);
    lv_obj_add_event_cb(install, install_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    row = row_with_texts(page, LI_GLOBE, tr(TR_SETTINGS_URL), "", &s_url_hint);
    s_url = text_field(row, "musicassistant_url", "ws://musicassistant.local:8095/ws", 300, false);
    if (addon_musicassistant_is_addon()) addon_musicassistant_set_addon(true); /* fills the URL from HA's */
    render_url();
    row = row_with_texts(page, LI_USER, tr(TR_SETTINGS_USERNAME), NULL, NULL);
    text_field(row, "musicassistant_username", tr(TR_SETTINGS_ENTER_USERNAME), 240, false);
    row = row_with_texts(page, LI_LOCK, tr(TR_SETTINGS_PASSWORD), NULL, NULL);
    text_field(row, "musicassistant_password", tr(TR_SETTINGS_ENTER_PASSWORD), 240, true);

    /* the room's player */
    row = row_with_texts(page, LI_SPEAKER, tr(TR_SETTINGS_MA_PLAYER), tr(TR_SETTINGS_MA_PLAYER_HINT), NULL);
    s_player_dd = lv_dropdown_create(row);
    lv_obj_set_width(s_player_dd, 240);
    lv_obj_add_event_cb(s_player_dd, player_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* open the player (also: pull down from the dashboard's top) */
    row = row_with_texts(page, LI_MUSIC, tr(TR_SETTINGS_MA_OPEN), tr(TR_SETTINGS_MA_OPEN_HINT), NULL);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, NULL);

    /* covers on the SD card */
    row = row_with_texts(page, LI_IMAGE, tr(TR_SETTINGS_MA_CACHE), "", &s_cache_hint);
    small_button(row, tr(TR_SETTINGS_MA_CLEAR), clear_cache_cb);
    render_cache();

    s_player_key = -1;
    app_cpp_music_set_listener(render, NULL);
    app_cpp_music_refresh(); /* players added in MA since the panel connected */
    render(NULL);
    return page;
}

static const settings_tab_t musicassistant_settings_tab = {
    .name = "Music Assistant",
    .create_page = create_musicassistant_settings_page,
    .name_key = TR_SETTINGS_MUSICASSISTANT,
    .summary_key = TR_SETTINGS_MUSICASSISTANT_SUMMARY,
    .line_icon = LI_MUSIC,
};

void addon_musicassistant_settings_init(void)
{
    settings_register_tab(&musicassistant_settings_tab);
}

const settings_tab_t* addon_musicassistant_settings_get_tab(void)
{
    return &musicassistant_settings_tab;
}
