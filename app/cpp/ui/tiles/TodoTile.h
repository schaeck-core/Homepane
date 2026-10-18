#ifndef APP_CPP_UI_TILES_TODOTILE_H
#define APP_CPP_UI_TILES_TODOTILE_H

#include "TileWidget.h"

#include <vector>

struct _lv_timer_t;
using lv_timer_t = _lv_timer_t;

namespace app::ui {

// A todo list as a tile: its open items (todo/item/list); tapping one
// checks it off (todo.update_item). Refreshed when the entity changes (its
// state is the number of open items) and every 5 minutes.
class TodoTile final : public TileWidget {
public:
    using TileWidget::TileWidget;
    ~TodoTile() override;

    struct Item {
        std::string uid, summary;
        bool done = false;
    };
    // The open items of a todo/item/list result, in list order (pure; host-tested).
    static std::vector<Item> parse_open_items(const std::string& result_json);

    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;
    void fetch();
    void item_tapped(int index);

protected:
    void build(lv_obj_t* tile) override;
    void update() override;

private:
    std::vector<Item> items_;
    bool fetched_ = false;
    app::core::CancellationSource pending_;
    app::core::CancellationSource completing_;
    lv_timer_t* refresh_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* count_label_ = nullptr;
    lv_obj_t* list_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_TODOTILE_H
