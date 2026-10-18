/**
 * @file screensaver.c
 * @brief Screensaver settings: when it starts; when the screen goes off
 *        (never before the screensaver); the HA Eventcalendar (on/off,
 *        greyed out with a hint when the add-on is not installed; its
 *        events can be looked through any time); the background picture
 *        (built in, or one from HA's media); show it now. The screensaver
 *        itself: app/cpp/ui/Screensaver.
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "../../ui_kit.h"
#include "screensaver.h"
#include "settings.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"

static const int AFTER_S[] = {0, 30, 60, 120, 300, 600};
/* screen off: TR_SETTINGS_SCREENSAVER_OFF_OPTIONS order; 0 = never */
static const int OFF_S[] = {0, 30, 60, 120, 300, 600, 1800, 3600};
#define OFF_COUNT ((int)(sizeof(OFF_S) / sizeof(OFF_S[0])))

static lv_obj_t *s_off_dd = NULL;
static int s_off_shown[OFF_COUNT]; /* the dropdown's entries: indexes into OFF_S */
static int s_off_shown_count = 0;
static lv_obj_t *s_events_switch = NULL;
static lv_obj_t *s_events_hint = NULL;
static lv_obj_t *s_browse_row = NULL;
static lv_obj_t *s_picture_dd = NULL;
static lv_obj_t *s_picture_hint = NULL;
static lv_obj_t *s_folder_row = NULL;
static lv_obj_t *s_folder_hint = NULL;
static bool s_folder_busy = false;
static char s_picture_shown[96];
static char **s_picture_ids = NULL;
static int s_picture_count = 0;

static bool events_on(void)
{
    const char *v = getConfig("eventcalendar_enabled");
    return !(v && strcmp(v, "0") == 0);
}

static void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (!obj) return;
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
        lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
        lv_obj_set_style_opa(obj, LV_OPA_50, 0);
    }
}

static void update_browse_row(void)
{
    set_enabled(s_browse_row, events_on() && app_cpp_event_count() > 0 && s_events_switch &&
                                  !lv_obj_has_state(s_events_switch, LV_STATE_DISABLED));
}

/* ── when ── */

static int config_int(const char *key, int fallback)
{
    const char *v = getConfig(key);
    return v && v[0] ? atoi(v) : fallback;
}

/* The screen-off choices: never, or not shorter than the screensaver's time
 * (it would never be seen otherwise). */
static void fill_off_dropdown(void)
{
    if (!s_off_dd) return;
    const int saver = config_int("screensaver_after", 120);
    const int off = config_int("screen_timeout", 600);
    char labels[160];
    snprintf(labels, sizeof(labels), "%s", tr(TR_SETTINGS_SCREENSAVER_OFF_OPTIONS));
    char opts[160] = "";
    s_off_shown_count = 0;
    uint32_t sel = 0;
    char *save = NULL;
    int i = 0;
    for (char *label = strtok_r(labels, "\n", &save); label && i < OFF_COUNT; label = strtok_r(NULL, "\n", &save), i++) {
        if (OFF_S[i] != 0 && OFF_S[i] < saver) continue;
        /* the stored time, or the next longer one shown (older, shorter settings) */
        if (off > 0 && sel == 0 && OFF_S[i] >= off) sel = (uint32_t)s_off_shown_count;
        if (s_off_shown_count) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        strncat(opts, label, sizeof(opts) - strlen(opts) - 1);
        s_off_shown[s_off_shown_count++] = i;
    }
    if (off > 0 && sel == 0 && s_off_shown_count > 1) sel = (uint32_t)s_off_shown_count - 1;
    lv_dropdown_set_options(s_off_dd, opts);
    lv_dropdown_set_selected(s_off_dd, sel, LV_ANIM_OFF);
}

static void after_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    char v[8];
    const int after = AFTER_S[sel < 6 ? sel : 3];
    snprintf(v, sizeof(v), "%d", after);
    updateConfig("screensaver_after", v);
    /* the screen goes off with the screensaver at the earliest */
    const int off = config_int("screen_timeout", 600);
    if (off > 0 && off < after) updateConfig("screen_timeout", v);
    saveConfig();
    fill_off_dropdown();
}

static void off_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if ((int)sel >= s_off_shown_count) return;
    char v[8];
    snprintf(v, sizeof(v), "%d", OFF_S[s_off_shown[sel]]);
    updateConfig("screen_timeout", v);
    saveConfig();
}

/* ── the event calendar ── */

static void addon_found_cb(void *user, bool found, const char *entity)
{
    (void)user;
    if (!s_events_switch) return;
    if (found) {
        const char *chosen = getConfig("eventcalendar_entity");
        if (!chosen || strcmp(chosen, entity) != 0) {
            updateConfig("eventcalendar_entity", entity);
            saveConfig();
            app_cpp_screensaver_apply_settings();
        }
        lv_label_set_text(s_events_hint, tr(TR_SETTINGS_SCREENSAVER_EVENTS_HINT));
        set_enabled(s_events_switch, true);
    } else {
        lv_label_set_text(s_events_hint, tr(TR_SETTINGS_SCREENSAVER_NO_ADDON));
        lv_obj_set_style_text_color(s_events_hint, lv_color_hex(UI_WARN), 0);
        lv_obj_remove_state(s_events_switch, LV_STATE_CHECKED);
        set_enabled(s_events_switch, false);
    }
    update_browse_row();
}

static void events_switched_cb(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    updateConfig("eventcalendar_enabled", on ? "1" : "0");
    saveConfig();
    app_cpp_screensaver_apply_settings();
    update_browse_row();
}

static void event_row_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    app_cpp_screensaver_show_event(app_cpp_event_id(i));
}

/* the events, any time: a page with one row per event; a tap shows it */
static void browse_cb(lv_event_t *e)
{
    (void)e;
    if (app_cpp_event_count() == 0) return;
    ui_detail_t d = ui_detail_open(tr(TR_SETTINGS_SCREENSAVER_BROWSE), NULL);
    lv_obj_add_flag(d.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(d.content, LV_DIR_VER);
    lv_obj_set_style_pad_row(d.content, UI_GAP_LIST, 0);
    for (int i = 0; i < app_cpp_event_count(); i++) {
        lv_obj_t *row = ui_box(d.content);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), 88);
        lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, 20, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 16, 0);
        lv_obj_t *texts = ui_box(row);
        lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 2, 0);
        ui_label_line(texts, UI_FONT_NAME, UI_TEXT, app_cpp_event_title(i));
        ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, app_cpp_event_when(i));
        ui_label(row, UI_ICON_BUTTON, UI_TEXT3, LI_CHEVRON_RIGHT);
        lv_obj_add_event_cb(row, event_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

/* ── the background picture ── */

static void free_pictures(void)
{
    for (int i = 0; i < s_picture_count; i++) free(s_picture_ids[i]);
    free(s_picture_ids);
    s_picture_ids = NULL;
    s_picture_count = 0;
}

static void pictures_loaded_cb(void *user, bool ok)
{
    (void)user;
    if (!s_picture_dd) return;
    free_pictures();
    const int n = ok ? app_cpp_media_picture_count() : 0;
    size_t size = strlen(tr(TR_SETTINGS_SCREENSAVER_BUILTIN)) + 1;
    for (int i = 0; i < n; i++) size += strlen(app_cpp_media_picture_title(i)) + 1;
    char *opts = malloc(size);
    s_picture_ids = calloc((size_t)n + 1, sizeof(char *));
    if (!opts || !s_picture_ids) {
        free(opts);
        return;
    }
    strcpy(opts, tr(TR_SETTINGS_SCREENSAVER_BUILTIN));
    const char *current = getConfig("screensaver_picture");
    uint32_t sel = 0;
    for (int i = 0; i < n; i++) {
        strcat(opts, "\n");
        strcat(opts, app_cpp_media_picture_title(i));
        s_picture_ids[s_picture_count++] = strdup(app_cpp_media_picture_id(i));
        if (current && strcmp(current, app_cpp_media_picture_id(i)) == 0) sel = (uint32_t)s_picture_count;
    }
    lv_dropdown_set_options(s_picture_dd, opts);
    free(opts);
    lv_dropdown_set_selected(s_picture_dd, sel, LV_ANIM_OFF);
    lv_obj_remove_state(s_picture_dd, LV_STATE_DISABLED);
    settings_dropdown_show_cut(s_picture_dd, s_picture_shown, sizeof(s_picture_shown));
    /* no folder "Homepane" yet: offer to create it */
    const bool found = ok && app_cpp_media_folder_found();
    if (s_picture_hint) {
        lv_label_set_text(s_picture_hint,
                          tr(found ? TR_SETTINGS_SCREENSAVER_PICTURE_HINT_FOLDER : TR_SETTINGS_SCREENSAVER_PICTURE_HINT));
    }
    if (s_folder_row) {
        if (ok && !found) lv_obj_remove_flag(s_folder_row, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_folder_row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void folder_created_cb(void *user, int status)
{
    (void)user;
    s_folder_busy = false;
    if (status == 200) {
        app_cpp_media_pictures_fetch(pictures_loaded_cb, NULL); /* hides the row */
        return;
    }
    if (!s_folder_hint) return;
    if (status == 401 || status == 403) {
        lv_label_set_text(s_folder_hint, tr(TR_SETTINGS_SCREENSAVER_FOLDER_DENIED));
    } else {
        char code[16];
        snprintf(code, sizeof(code), "%d", status);
        const char *args[] = {status ? code : "–"};
        char text[128];
        lv_label_set_text(s_folder_hint, tr_format(text, sizeof(text), TR_SETTINGS_SCREENSAVER_FOLDER_FAILED, args, 1));
    }
    lv_obj_set_style_text_color(s_folder_hint, lv_color_hex(UI_WARN), 0);
}

static void create_folder_cb(lv_event_t *e)
{
    (void)e;
    if (s_folder_busy) return;
    s_folder_busy = true;
    lv_label_set_text(s_folder_hint, tr(TR_SETTINGS_SCREENSAVER_FOLDER_BUSY));
    lv_obj_set_style_text_color(s_folder_hint, lv_color_hex(UI_TEXT2), 0);
    app_cpp_media_folder_create(folder_created_cb, NULL);
}

static void picture_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    updateConfig("screensaver_picture", sel == 0 || (int)sel > s_picture_count ? "" : s_picture_ids[sel - 1]);
    saveConfig();
    settings_dropdown_show_cut(s_picture_dd, s_picture_shown, sizeof(s_picture_shown));
}

/* ── page ── */

static void page_deleted_cb(lv_event_t *e)
{
    (void)e;
    s_off_dd = s_events_switch = s_events_hint = s_browse_row = s_picture_dd = NULL;
    s_picture_hint = s_folder_row = s_folder_hint = NULL;
    free_pictures();
}

static void show_now_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_screensaver_show();
}

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

static lv_obj_t *create_screensaver_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    lv_obj_add_event_cb(page, page_deleted_cb, LV_EVENT_DELETE, NULL);

    /* when */
    lv_obj_t *row = row_with_texts(page, LI_TIMER, tr(TR_SETTINGS_SCREENSAVER_AFTER), NULL, NULL);
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, tr(TR_SETTINGS_SCREENSAVER_AFTER_OPTIONS)); /* AFTER_S order */
    const char *after = getConfig("screensaver_after");
    const int a = after && after[0] ? atoi(after) : 120;
    /* the stored time, or the next longer one listed (older values) */
    uint32_t sel = 5;
    for (uint32_t i = 6; i-- > 0;) {
        if (AFTER_S[i] >= a && (a > 0 || i == 0)) sel = i;
    }
    lv_dropdown_set_selected(dd, sel, LV_ANIM_OFF);
    lv_obj_add_event_cb(dd, after_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* screen off (not before the screensaver) */
    row = row_with_texts(page, LI_MOON, tr(TR_SETTINGS_SCREENSAVER_OFF), tr(TR_SETTINGS_SCREENSAVER_OFF_HINT), NULL);
    s_off_dd = lv_dropdown_create(row);
    fill_off_dropdown();
    lv_obj_add_event_cb(s_off_dd, off_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* the event calendar: on/off (greyed out until the add-on is found) */
    row = row_with_texts(page, LI_CALENDAR, tr(TR_SETTINGS_SCREENSAVER_EVENTS), tr(TR_SETTINGS_LOADING), &s_events_hint);
    s_events_switch = lv_switch_create(row);
    if (events_on()) lv_obj_add_state(s_events_switch, LV_STATE_CHECKED);
    set_enabled(s_events_switch, false);
    lv_obj_add_event_cb(s_events_switch, events_switched_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* its events, any time */
    s_browse_row = row_with_texts(page, LI_LIST, tr(TR_SETTINGS_SCREENSAVER_BROWSE), NULL, NULL);
    lv_obj_add_flag(s_browse_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_browse_row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_t *chev = lv_label_create(s_browse_row);
    lv_label_set_text(chev, LI_CHEVRON_RIGHT);
    lv_obj_set_style_text_font(chev, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(chev, lv_color_hex(UI_TEXT3), 0);
    lv_obj_add_event_cb(s_browse_row, browse_cb, LV_EVENT_CLICKED, NULL);
    set_enabled(s_browse_row, false);

    /* the background picture */
    row = row_with_texts(page, LI_IMAGE, tr(TR_SETTINGS_SCREENSAVER_PICTURE), tr(TR_SETTINGS_SCREENSAVER_PICTURE_HINT),
                         &s_picture_hint);
    s_picture_dd = lv_dropdown_create(row);
    lv_dropdown_set_options(s_picture_dd, tr(TR_SETTINGS_LOADING));
    lv_obj_set_width(s_picture_dd, 240);
    lv_obj_add_state(s_picture_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_picture_dd, picture_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* create the folder "Homepane" in HA's media (shown when missing) */
    s_folder_row = row_with_texts(page, LI_FOLDER_PLUS, tr(TR_SETTINGS_SCREENSAVER_FOLDER),
                                  tr(TR_SETTINGS_SCREENSAVER_FOLDER_HINT), &s_folder_hint);
    lv_obj_add_flag(s_folder_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_folder_row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_folder_row, create_folder_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_folder_row, LV_OBJ_FLAG_HIDDEN);

    /* show it now */
    row = row_with_texts(page, LI_SPARKLES, tr(TR_SETTINGS_SCREENSAVER_SHOW), NULL, NULL);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, show_now_cb, LV_EVENT_CLICKED, NULL);

    /* is the add-on there? (its display sensor carries "api_version") */
    app_cpp_eventcalendar_detect(addon_found_cb, NULL);
    app_cpp_media_pictures_fetch(pictures_loaded_cb, NULL);
    return page;
}

static const settings_tab_t screensaver_tab = {
    .name = "Screensaver",
    .create_page = create_screensaver_page,
    .name_key = TR_SETTINGS_SCREENSAVER,
    .summary_key = TR_SETTINGS_SCREENSAVER_SUMMARY,
    .line_icon = LI_SPARKLES,
};

void settings_screensaver_init(void)
{
    settings_register_tab(&screensaver_tab);
}
