#ifndef APP_CPP_UI_TILES_OPTIONSTILE_H
#define APP_CPP_UI_TILES_OPTIONSTILE_H

#include "../../core/CancellationToken.h"
#include "../../home_assistant/HaCommandService.h"
#include "../Widget.h"

#include <memory>
#include <string>
#include <vector>

struct _lv_timer_t;

namespace app::ui {

// The buttons tile of the design guide: a title and up to three full-width
// buttons. Two sources:
//   - an "entities" card (scenes, scripts, switches): each button runs or
//     toggles its entity and lights up briefly;
//   - an input_select / select entity: each button is an option, the
//     selected one marked.
class OptionsTile final : public Widget {
public:
    static constexpr size_t kMaxButtons = 3;

    // From an "entities" card's raw JSON.
    static std::unique_ptr<OptionsTile> from_entities_card(app::home_assistant::HaInstanceId instance,
                                          app::home_assistant::HaCommandService& commands, const std::string& raw_json);
    // For an input_select / select entity.
    OptionsTile(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService& commands,
                std::string select_entity);

    // ["scene.a", {"entity":"script.b","name":"B"}] -> entity ids + names.
    struct Item {
        std::string entity_id, name;
    };
    static std::vector<Item> parse_entities(const std::string& raw_json, std::string* title);

    std::vector<DataRequirement> requirements() const override;
    void create(lv_obj_t* parent) override;
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

    void button_pressed(size_t index);
    void flash_over();

private:
    OptionsTile(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService& commands);
    void render();
    bool select_mode() const { return !select_entity_.empty(); }

    app::home_assistant::HaInstanceId instance_;
    app::home_assistant::HaCommandService* commands_;
    std::string title_;
    std::vector<Item> items_;   // entities mode
    std::string select_entity_; // select mode
    std::vector<std::string> options_;
    std::string selected_;
    int flashing_ = -1;
    _lv_timer_t* flash_timer_ = nullptr;
    app::core::CancellationSource pending_;

    lv_obj_t* title_label_ = nullptr;
    std::vector<lv_obj_t*> buttons_;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_OPTIONSTILE_H
