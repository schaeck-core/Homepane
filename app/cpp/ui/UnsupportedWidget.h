#ifndef APP_CPP_UI_UNSUPPORTEDWIDGET_H
#define APP_CPP_UI_UNSUPPORTEDWIDGET_H

#include "Widget.h"

#include <string>

namespace app::ui {

// Visible, crash-free placeholder for a card type not yet migrated to
// C++ (C++ migration phase 6 DoD). Deliberately owns no
// requirements and does nothing on on_data().
class UnsupportedWidget final : public Widget {
public:
    explicit UnsupportedWidget(std::string card_type) : card_type_(std::move(card_type)) {}

    void create(lv_obj_t* parent) override;

private:
    std::string card_type_;
};

} // namespace app::ui

#endif // APP_CPP_UI_UNSUPPORTEDWIDGET_H
