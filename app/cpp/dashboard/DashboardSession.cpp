#include "DashboardSession.h"

#include "DashboardLayout.h"
#include "lvgl/lvgl.h"
#include "../../fonts.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <variant>

namespace app::dashboard {

using app::core::LogLevel;
using app::home_assistant::EntitySubscriptionCoordinator;
using app::home_assistant::QueryCoordinator;
using app::ui::DataUpdate;
using app::ui::EntityRequirement;
using app::ui::ForecastRequirement;
using app::ui::StatisticsRequirement;

DashboardSession::DashboardSession(DashboardModel model, const WidgetFactory& factory,
                                    app::core::DashboardGeneration generation,
                                    EntitySubscriptionCoordinator* coordinator, QueryCoordinator* query_coordinator,
                                    app::core::ILogger* logger)
    : model_(std::move(model)), factory_(factory), generation_(generation), coordinator_(coordinator),
      query_coordinator_(query_coordinator), logger_(logger) {}

// ── Swiping with still pictures ──
//
// A full redraw of a busy page takes ~150 ms on the ESP32-P4 (software
// rendering, ~300 objects), so a swipe that redraws the pages live runs at
// ~7-14 frames/s. Instead, while nobody touches the panel, each page near
// the shown one is drawn once into a picture (lv_snapshot); a swipe hides
// the pages' contents and moves the pictures (a plain copy per frame), and
// shows the live pages again when it ends. A page whose picture is not
// current (it changed since) is swiped live rather than shown outdated.
struct DashboardSession::SwipeShots {
    static constexpr uint32_t kTickMs = 400;
    static constexpr uint32_t kIdleMs = 1200;      // no touch for this long before a picture is taken
    static constexpr uint32_t kMinAgeMs = 5000;    // a page at most every 5 s (e.g. a camera tile)
    static constexpr uint32_t kNeighbourMs = 60000; // pages off screen are not seen changing

    struct Shot {
        lv_obj_t* page = nullptr;
        lv_obj_t* image = nullptr;
        lv_draw_buf_t* buf = nullptr;
        bool stale = true;
        uint32_t taken_at = 0;
        int32_t scroll_y = 0;
        std::vector<lv_obj_t*> hidden; // contents hidden during a swipe
    };

    lv_obj_t* root = nullptr;
    app::core::ILogger* logger = nullptr;
    std::vector<Shot> shots;
    lv_timer_t* timer = nullptr;
    lv_timer_t* settle = nullptr; // the live pages back once the scroll has settled
    int32_t last_x = 0;
    bool swiping = false;
    bool taking = false;

    SwipeShots(lv_obj_t* root_obj, app::core::ILogger* log) : root(root_obj), logger(log)
    {
        for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
            Shot s;
            s.page = lv_obj_get_child(root, static_cast<int32_t>(i));
            s.image = lv_image_create(s.page);
            lv_obj_add_flag(s.image, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s.image, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_remove_flag(s.image, LV_OBJ_FLAG_CLICKABLE);
            shots.push_back(std::move(s));
        }
        lv_obj_add_event_cb(root, scroll_cb, LV_EVENT_SCROLL_BEGIN, this);
        lv_obj_add_event_cb(root, scroll_cb, LV_EVENT_SCROLL_END, this);
        lv_display_add_event_cb(lv_obj_get_display(root), invalidate_cb, LV_EVENT_INVALIDATE_AREA, this);
        timer = lv_timer_create(tick_cb, kTickMs, this);
    }

    // in the UI context, before the pages are deleted
    void shutdown()
    {
        if (timer) lv_timer_delete(timer);
        timer = nullptr;
        if (settle) lv_timer_delete(settle);
        settle = nullptr;
        if (root) {
            lv_display_remove_event_cb_with_user_data(lv_obj_get_display(root), invalidate_cb, this);
            lv_obj_remove_event_cb_with_user_data(root, scroll_cb, this);
        }
        for (auto& s : shots) {
            if (s.image) lv_image_set_src(s.image, nullptr);
            drop(s);
        }
        shots.clear();
        root = nullptr;
    }

    static void drop(Shot& s)
    {
        if (!s.buf) return;
        lv_image_cache_drop(s.buf);
        lv_draw_buf_destroy(s.buf);
        s.buf = nullptr;
    }

    int shown_index() const
    {
        const int32_t w = lv_obj_get_width(root);
        if (w <= 0 || shots.empty()) return 0;
        const int i = static_cast<int>((lv_obj_get_scroll_x(root) + w / 2) / w);
        return std::max(0, std::min(i, static_cast<int>(shots.size()) - 1));
    }

    // Something on the shown page was redrawn: its picture is out of date.
    static void invalidate_cb(lv_event_t* e)
    {
        auto* self = static_cast<SwipeShots*>(lv_event_get_user_data(e));
        if (self->taking || self->swiping || self->shots.empty()) return;
        const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(e));
        Shot& s = self->shots[static_cast<size_t>(self->shown_index())];
        lv_area_t page;
        lv_obj_get_coords(s.page, &page);
        if (area && area->x1 <= page.x2 && area->x2 >= page.x1 && area->y1 <= page.y2 && area->y2 >= page.y1) s.stale = true;
    }

    bool needs_picture(const Shot& s, bool shown, uint32_t now) const
    {
        if (!s.buf) return true;
        const uint32_t age = now - s.taken_at;
        if (age < kMinAgeMs) return false;
        return shown ? s.stale || lv_obj_get_scroll_y(s.page) != s.scroll_y : age >= kNeighbourMs;
    }

    static void tick_cb(lv_timer_t* t)
    {
        auto* self = static_cast<SwipeShots*>(lv_timer_get_user_data(t));
        if (self->swiping || !self->root || lv_obj_get_screen(self->root) != lv_screen_active()) return;
        if (lv_display_get_inactive_time(lv_obj_get_display(self->root)) < kIdleMs) return;
        // not under the screensaver or another layer: nothing of it is seen
        for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); ++i) {
            if (!lv_obj_has_flag(lv_obj_get_child(lv_layer_top(), static_cast<int32_t>(i)), LV_OBJ_FLAG_HIDDEN)) return;
        }
        const uint32_t now = lv_tick_get();
        const int shown = self->shown_index();
        // one picture per tick: the shown page first, then its neighbours
        for (int i : {shown, shown + 1, shown - 1}) {
            if (i < 0 || i >= static_cast<int>(self->shots.size())) continue;
            Shot& s = self->shots[static_cast<size_t>(i)];
            if (!self->needs_picture(s, i == shown, now)) continue;
            self->take(s, now);
            return;
        }
    }

    void take(Shot& s, uint32_t now)
    {
        taking = true;
        lv_draw_buf_t* buf = lv_snapshot_take(s.page, lv_display_get_color_format(lv_obj_get_display(root)));
        taking = false;
        if (!buf) return; // no memory: the page is swiped live
        drop(s);
        s.buf = buf;
        s.stale = false;
        s.taken_at = now;
        s.scroll_y = lv_obj_get_scroll_y(s.page);
        lv_image_set_src(s.image, buf);
    }

    static void scroll_cb(lv_event_t* e)
    {
        auto* self = static_cast<SwipeShots*>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) self->begin();
        else self->end();
    }

    void begin()
    {
        if (settle) { // the snap after the finger lifted: same swipe
            lv_timer_delete(settle);
            settle = nullptr;
        }
        if (swiping) return;
        swiping = true;
        int pictures = 0;
        for (auto& s : shots) {
            if (!s.buf || s.stale || lv_obj_get_scroll_y(s.page) != s.scroll_y) continue; // live
            ++pictures;
            for (uint32_t i = 0; i < lv_obj_get_child_count(s.page); ++i) {
                lv_obj_t* c = lv_obj_get_child(s.page, static_cast<int32_t>(i));
                if (c == s.image || lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
                lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
                s.hidden.push_back(c);
            }
            lv_obj_set_pos(s.image, 0, s.scroll_y);
            lv_obj_remove_flag(s.image, LV_OBJ_FLAG_HIDDEN);
        }
        if (logger) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "swipe: %d of %d pages as pictures", pictures, static_cast<int>(shots.size()));
            logger->log(LogLevel::Info, msg);
        }
    }

    // A lifted finger ends one scroll; the coasting and the snap to the page
    // follow. The pictures stay until the view rests exactly on a page.
    void end()
    {
        if (!swiping || settle) return;
        last_x = lv_obj_get_scroll_x(root);
        settle = lv_timer_create(settle_cb, 60, this);
    }

    static void settle_cb(lv_timer_t* t)
    {
        auto* self = static_cast<SwipeShots*>(lv_timer_get_user_data(t));
        const int32_t x = lv_obj_get_scroll_x(self->root);
        const int32_t w = lv_obj_get_width(self->root);
        const bool resting = x == self->last_x && w > 0 && x % w == 0;
        self->last_x = x;
        lv_indev_t* indev = lv_indev_active();
        if (!resting || (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)) return;
        lv_timer_delete(t);
        self->settle = nullptr;
        self->restore();
    }

    void restore()
    {
        if (!swiping) return;
        swiping = false;
        for (auto& s : shots) {
            for (lv_obj_t* c : s.hidden) lv_obj_remove_flag(c, LV_OBJ_FLAG_HIDDEN);
            s.hidden.clear();
            lv_obj_add_flag(s.image, LV_OBJ_FLAG_HIDDEN);
        }
    }
};

DashboardSession::~DashboardSession() {
    // §9: destructors must not touch LVGL — shutdown() is the only place
    // that does, and it must already have run by the time we get here.
    if (!shut_down_ && root_ && logger_) {
        logger_->log(LogLevel::Error,
                     "DashboardSession destroyed without calling shutdown() first — LVGL objects leaked");
    }
}

namespace {
// Tile grid (docs/design/DESIGN_GUIDE.md): gap 12; columns from the view,
// tile height from the rows on screen (DashboardLayout.h).
constexpr int32_t kGap = 12;
constexpr uint32_t kSurface = 0x141413;
constexpr uint32_t kSurface2 = 0x1F1E1C;
constexpr int32_t kClockWidth = 296;
constexpr uint32_t kText2 = 0xA39F97;

std::string g_rows_setting = "2";

// Pixel geometry of one view: half columns across, slots down.
struct Geometry {
    int32_t col_w = 0, half_w = 0, slot_h = 0;
    int32_t x(int half) const { return (half / 2) * (col_w + kGap) + (half % 2) * (half_w + kGap); }
    int32_t width(int half, int halves) const {
        const int last = half + halves - 1;
        const int32_t end = last % 2 ? (last / 2) * (col_w + kGap) + col_w : x(last) + half_w;
        return end - x(half);
    }
    int32_t y(int slot) const { return slot * (slot_h + kGap); }
    int32_t height(int slots) const { return slots * slot_h + (slots - 1) * kGap; }
};
} // namespace

void DashboardSession::set_rows_setting(const std::string& setting) { g_rows_setting = setting; }

void DashboardSession::create(lv_obj_t* parent) {
    // Views side by side (swipe between them), each scrolling vertically.
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(root_, 0, 0);
    lv_obj_set_scroll_snap_x(root_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(root_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_update_layout(parent);
    const int32_t width = lv_obj_get_content_width(parent);
    const int32_t height = lv_obj_get_content_height(parent);

    auto create_card = [&](const DashboardCard& card, lv_obj_t* cell_parent, bool compact) -> lv_obj_t* {
        std::unique_ptr<app::ui::Widget> widget = compact ? factory_.create_compact(card) : factory_.create(card);
        widget->create(cell_parent);
        wire_requirements(*widget);
        lv_obj_t* root = widget->root();
        widgets_.push_back(std::move(widget));
        return root;
    };
    auto make_cell = [](lv_obj_t* row_obj, const Geometry& g, const GridCell& c, int halves) {
        lv_obj_t* cell = lv_obj_create(row_obj);
        lv_obj_remove_style_all(cell);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(cell, g.x(c.x), g.y(c.y));
        lv_obj_set_size(cell, g.width(c.x, halves), g.height(c.h));
        return cell;
    };

    for (const auto& view : model_.views) {
        const int columns = view_columns(view);
        // the view's own setting, the dashboard's, then the panel's
        const std::string& rows_setting = !view.rows.empty()     ? view.rows
                                          : !model_.rows.empty() ? model_.rows
                                                                 : g_rows_setting;
        const int rows_shown = rows_on_screen(rows_setting, columns);
        Geometry g;
        g.col_w = (width - (columns - 1) * kGap) / columns;
        g.half_w = (g.col_w - kGap) / 2;
        const int32_t tile_h = (height - (rows_shown - 1) * kGap - kGap) / rows_shown;
        g.slot_h = (tile_h - (kSlotsPerTile - 1) * kGap) / kSlotsPerTile;

        lv_obj_t* page = lv_obj_create(root_);
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(page, kGap, 0);
        lv_obj_set_style_pad_bottom(page, kGap, 0);
        lv_obj_set_scroll_dir(page, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

        const std::vector<GridRow> rows = layout_grid(view);
        // Snapping to the rows only while each fits the screen; a taller one
        // could not be scrolled through otherwise.
        bool snap = true;
        for (const GridRow& row : rows) snap = snap && g.height(row.height) <= height;
        lv_obj_set_scroll_snap_y(page, snap ? LV_SCROLL_SNAP_START : LV_SCROLL_SNAP_NONE);

        for (const GridRow& row : rows) {
            lv_obj_t* row_obj = lv_obj_create(page);
            lv_obj_remove_style_all(row_obj);
            lv_obj_clear_flag(row_obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row_obj, LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_set_size(row_obj, LV_PCT(100), g.height(row.height));
            for (size_t i = 0; i < row.cells.size(); ++i) {
                const GridCell& c = row.cells[i];
                if (c.kind == CellKind::Title) {
                    // Section title: 18, text 2, no card, at the slot's bottom.
                    lv_obj_t* cell = make_cell(row_obj, g, c, c.w);
                    lv_obj_t* label = lv_label_create(cell);
                    lv_obj_set_width(label, LV_PCT(100));
                    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
                    lv_obj_set_style_text_font(label, font_manrope_500_18, 0);
                    lv_obj_set_style_text_color(label, lv_color_hex(kText2), 0);
                    lv_obj_set_style_pad_left(label, 4, 0);
                    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, -4);
                    lv_label_set_text(label, c.title.c_str());
                    continue;
                }
                // Clock followed by a weather card: one card, clock left,
                // divider, weather right (design guide, Main artboard).
                const GridCell* next = i + 1 < row.cells.size() ? &row.cells[i + 1] : nullptr;
                const bool merge = next && c.kind == CellKind::Tile && next->kind == CellKind::Tile &&
                                   c.card->type == "clock" && next->card && next->card->type == "weather-forecast" &&
                                   next->y == c.y && next->x == c.x + c.w && next->w >= 4;
                lv_obj_t* cell = make_cell(row_obj, g, c, merge ? c.w + next->w : c.w);
                // Tile surface for widgets that do not draw their own yet.
                lv_obj_set_style_bg_color(cell, lv_color_hex(kSurface), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(cell, c.kind == CellKind::Compact ? 20 : 24, 0);
                lv_obj_set_style_text_color(cell, lv_color_hex(0xECE9E3), 0);
                if (!merge) {
                    create_card(*c.card, cell, c.kind == CellKind::Compact);
                    continue;
                }
                const int32_t cell_h = g.height(c.h);
                lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
                lv_obj_t* clock_area = lv_obj_create(cell);
                lv_obj_remove_style_all(clock_area);
                lv_obj_clear_flag(clock_area, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_size(clock_area, std::max(g.width(c.x, c.w), kClockWidth), LV_PCT(100));
                lv_obj_t* divider = lv_obj_create(cell);
                lv_obj_remove_style_all(divider);
                lv_obj_set_size(divider, 2, cell_h - 40);
                lv_obj_set_style_align(divider, LV_ALIGN_LEFT_MID, 0);
                lv_obj_set_style_margin_top(divider, 20, 0);
                lv_obj_set_style_bg_color(divider, lv_color_hex(kSurface2), 0);
                lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
                lv_obj_t* weather_area = lv_obj_create(cell);
                lv_obj_remove_style_all(weather_area);
                lv_obj_clear_flag(weather_area, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_height(weather_area, LV_PCT(100));
                lv_obj_set_flex_grow(weather_area, 1);
                create_card(*c.card, clock_area, false);
                create_card(*next->card, weather_area, false);
                ++i;
            }
        }
    }
    if (model_.views.size() > 1) swipe_ = std::make_unique<SwipeShots>(root_, logger_);
}

int DashboardSession::current_view() const {
    if (!root_) return 0;
    const int32_t w = lv_obj_get_width(root_);
    if (w <= 0) return 0;
    const int index = static_cast<int>((lv_obj_get_scroll_x(root_) + w / 2) / w);
    return std::max(0, std::min(index, static_cast<int>(model_.views.size()) - 1));
}

bool DashboardSession::at_top() const {
    if (!root_) return false;
    lv_obj_t* page = lv_obj_get_child(root_, current_view());
    return page && lv_obj_get_scroll_y(page) <= 0;
}

void DashboardSession::show_view(int index, bool animate) {
    if (!root_ || index < 0 || index >= static_cast<int>(model_.views.size())) return;
    lv_obj_t* page = lv_obj_get_child(root_, index);
    if (!page) return;
    if (!animate) lv_obj_update_layout(root_); // positions are needed to jump there
    lv_obj_scroll_to_view(page, animate ? LV_ANIM_ON : LV_ANIM_OFF);
}

void DashboardSession::show_home() {
    if (!root_) return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root_); ++i) {
        lv_obj_scroll_to_y(lv_obj_get_child(root_, static_cast<int32_t>(i)), 0, LV_ANIM_OFF);
    }
    show_view(0, false);
}

void DashboardSession::wire_requirements(app::ui::Widget& widget) {
    std::vector<app::ui::DataRequirement> reqs = widget.requirements();
    for (size_t i = 0; i < reqs.size(); ++i) {
        app::ui::Widget* w = &widget;
        size_t requirement_index = i;

        if (const auto* entity_req = std::get_if<EntityRequirement>(&reqs[i])) {
            if (!coordinator_) continue;
            auto handle = coordinator_->observe(
                entity_req->entity_id, [w, requirement_index](const app::home_assistant::EntityState& state) {
                    DataUpdate update;
                    update.requirement_index = requirement_index;
                    update.entity_state = &state;
                    w->on_data(update);
                });
            // §6.2: deliver the current (possibly still `loading`) state
            // now that create() has fully returned — never inline from
            // observe().
            coordinator_->deliver_current(entity_req->entity_id, handle);
            subscriptions_.push_back(std::move(handle));
        } else if (const auto* forecast_req = std::get_if<ForecastRequirement>(&reqs[i])) {
            if (!query_coordinator_) continue;
            auto handle = query_coordinator_->observe_forecast(
                forecast_req->entity_id, forecast_req->forecast_type,
                [w, requirement_index](const app::home_assistant::ForecastState& state) {
                    DataUpdate update;
                    update.requirement_index = requirement_index;
                    update.forecast_state = &state;
                    w->on_data(update);
                });
            query_coordinator_->deliver_current_forecast(forecast_req->entity_id, forecast_req->forecast_type,
                                                           handle);
            subscriptions_.push_back(std::move(handle));
        } else if (const auto* statistics_req = std::get_if<StatisticsRequirement>(&reqs[i])) {
            if (!query_coordinator_) continue;
            auto handle = query_coordinator_->observe_statistics(
                statistics_req->query, statistics_req->refresh_interval_ms,
                [w, requirement_index](const app::home_assistant::StatisticsResult& result) {
                    DataUpdate update;
                    update.requirement_index = requirement_index;
                    update.statistics_result = &result;
                    w->on_data(update);
                });
            query_coordinator_->deliver_current_statistics(statistics_req->query, handle);
            subscriptions_.push_back(std::move(handle));
        }
    }
}

void DashboardSession::shutdown() {
    if (shut_down_) return;
    shut_down_ = true;

    // §4 Shutdown-Reihenfolge: cancel entity/query subscriptions first (no
    // more on_data() calls can arrive for widgets about to be torn down),
    // then each widget's own UI resources (timers, etc.), then the LVGL
    // subtree (which nulls every widget's root_ via LV_EVENT_DELETE), and
    // only then release the C++ objects.
    subscriptions_.clear();
    if (swipe_) swipe_->shutdown();
    for (auto& w : widgets_) {
        w->shutdown_ui();
    }
    if (root_) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
    widgets_.clear();
}

} // namespace app::dashboard
