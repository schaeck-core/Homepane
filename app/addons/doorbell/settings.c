/**
 * @file settings.c
 * @brief Doorbell settings page
 *
 * Picks, from the entities in Home Assistant:
 * - the trigger (event, input_button, button: a press rings; binary_sensor,
 *   input_boolean: off -> on rings),
 * - the camera shown while it rings,
 * - the opener the "hold to open" button calls (lock, button, script, ...),
 * and rings once for a test. The doorbell itself: app/cpp/doorbell.
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "settings.h"
#include "../../views/settings/settings_tab.h"
#include "../../views/settings/settings.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"

/* One picker per setting; ids[] holds the entity ids behind the options
 * (option 0 is "none"). */
typedef struct {
    const char *config_key;
    const char *domains; /* comma separated */
    lv_obj_t *dd;
    char **ids;
    int count;
    char shown[96]; /* the selected name, cut to fit next to the arrow */
} picker_t;

static picker_t s_pickers[] = {
    {"doorbell_trigger", "event,input_button,button,binary_sensor,input_boolean", NULL, NULL, 0, ""},
    {"doorbell_camera", "camera", NULL, NULL, 0, ""},
    {"doorbell_unlock", "lock,button,input_button,script,switch,scene,cover", NULL, NULL, 0, ""},
};
#define PICKER_COUNT ((int)(sizeof(s_pickers) / sizeof(s_pickers[0])))
#define ALL_DOMAINS "event,input_button,button,binary_sensor,input_boolean,camera,lock,script,switch,scene,cover"

static bool in_domains(const char *entity_id, const char *domains)
{
    const char *dot = strchr(entity_id, '.');
    if (!dot) return false;
    const size_t len = (size_t)(dot - entity_id);
    for (const char *p = domains; *p;) {
        const char *end = strchr(p, ',');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == len && strncmp(p, entity_id, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static void show_selected(picker_t *pk) { settings_dropdown_show_cut(pk->dd, pk->shown, sizeof(pk->shown)); }

static void free_picker(picker_t *pk)
{
    for (int i = 0; i < pk->count; i++) free(pk->ids[i]);
    free(pk->ids);
    pk->ids = NULL;
    pk->count = 0;
}

static void fill_picker(picker_t *pk, bool ok)
{
    if (!pk->dd) return;
    free_picker(pk);
    if (!ok) {
        lv_dropdown_set_options(pk->dd, tr(TR_SETTINGS_NOT_CONNECTED));
        return;
    }
    const int n = app_cpp_entity_count();
    pk->ids = calloc((size_t)n + 1, sizeof(char *));
    size_t size = strlen(tr(TR_PRESET_NONE)) + 1;
    for (int i = 0; i < n; i++) {
        if (in_domains(app_cpp_entity_id(i), pk->domains)) size += strlen(app_cpp_entity_name(i)) + 1;
    }
    char *opts = malloc(size);
    if (!pk->ids || !opts) {
        free(opts);
        return;
    }
    strcpy(opts, tr(TR_PRESET_NONE));
    const char *current = getConfig(pk->config_key);
    uint32_t sel = 0;
    for (int i = 0; i < n; i++) {
        const char *id = app_cpp_entity_id(i);
        if (!in_domains(id, pk->domains)) continue;
        strcat(opts, "\n");
        strcat(opts, app_cpp_entity_name(i));
        pk->ids[pk->count++] = strdup(id);
        if (current && strcmp(current, id) == 0) sel = (uint32_t)pk->count;
    }
    lv_dropdown_set_options(pk->dd, opts);
    free(opts);
    lv_dropdown_set_selected(pk->dd, sel);
    lv_obj_remove_state(pk->dd, LV_STATE_DISABLED);
    show_selected(pk);
}

static void entities_loaded_cb(void *user, bool ok)
{
    (void)user;
    for (int i = 0; i < PICKER_COUNT; i++) fill_picker(&s_pickers[i], ok);
}

static void picker_changed_cb(lv_event_t *e)
{
    picker_t *pk = lv_event_get_user_data(e);
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    updateConfig(pk->config_key, sel == 0 || (int)sel > pk->count ? "" : pk->ids[sel - 1]);
    saveConfig();
    show_selected(pk);
    app_cpp_doorbell_apply_settings();
}

static void page_deleted_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < PICKER_COUNT; i++) {
        s_pickers[i].dd = NULL;
        free_picker(&s_pickers[i]);
    }
}

static void test_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_doorbell_test();
}

static void picker_row(lv_obj_t *page, picker_t *pk, const char *icon, const char *title, const char *hint)
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
    lv_obj_t *h = lv_label_create(texts);
    lv_label_set_text(h, hint);
    lv_obj_set_width(h, LV_PCT(100));
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(h, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_TEXT2), 0);

    pk->dd = lv_dropdown_create(row);
    lv_dropdown_set_options(pk->dd, tr(TR_SETTINGS_LOADING));
    lv_obj_set_width(pk->dd, 280);
    lv_obj_add_state(pk->dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(pk->dd, picker_changed_cb, LV_EVENT_VALUE_CHANGED, pk);
}

static lv_obj_t *create_doorbell_settings_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    lv_obj_add_event_cb(page, page_deleted_cb, LV_EVENT_DELETE, NULL);

    picker_row(page, &s_pickers[0], LI_BELL, tr(TR_SETTINGS_DOORBELL_TRIGGER), tr(TR_SETTINGS_DOORBELL_TRIGGER_HINT));
    picker_row(page, &s_pickers[1], LI_CAMERA, tr(TR_SETTINGS_DOORBELL_CAMERA), tr(TR_SETTINGS_DOORBELL_CAMERA_HINT));
    picker_row(page, &s_pickers[2], LI_DOOR_OPEN, tr(TR_SETTINGS_DOORBELL_UNLOCK), tr(TR_SETTINGS_DOORBELL_UNLOCK_HINT));

    /* ring once, as a test */
    lv_obj_t *test = settings_row_create(page);
    lv_obj_add_flag(test, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(test, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_t *ti = lv_label_create(test);
    lv_label_set_text(ti, LI_BELL);
    lv_obj_set_style_text_font(ti, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(ti, lv_color_hex(UI_ACCENT), 0);
    lv_obj_t *tl = lv_label_create(test);
    lv_label_set_text(tl, tr(TR_SETTINGS_DOORBELL_TEST));
    lv_obj_set_style_text_font(tl, UI_FONT_NAME, 0);
    lv_obj_set_style_text_color(tl, lv_color_hex(UI_TEXT), 0);
    lv_obj_add_event_cb(test, test_cb, LV_EVENT_CLICKED, NULL);

    app_cpp_entities_fetch(ALL_DOMAINS, entities_loaded_cb, NULL);
    return page;
}

static const settings_tab_t doorbell_settings_tab = {
    .name = "Doorbell",
    .create_page = create_doorbell_settings_page,
    .name_key = TR_SETTINGS_DOORBELL,
    .summary_key = TR_SETTINGS_DOORBELL_SUMMARY,
    .line_icon = LI_BELL,
};

void addon_doorbell_settings_init(void)
{
    settings_register_tab(&doorbell_settings_tab);
}

const settings_tab_t* addon_doorbell_settings_get_tab(void)
{
    return &doorbell_settings_tab;
}
