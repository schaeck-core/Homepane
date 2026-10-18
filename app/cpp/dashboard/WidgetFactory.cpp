#include "WidgetFactory.h"

#include "../ui/UnsupportedWidget.h"

namespace app::dashboard {

void WidgetFactory::register_type(const std::string& card_type, CreatorFn creator) {
    creators_[card_type] = std::move(creator);
}

std::unique_ptr<app::ui::Widget> WidgetFactory::create(const DashboardCard& card) const {
    auto it = creators_.find(card.type);
    if (it != creators_.end()) return it->second(card);
    return std::make_unique<app::ui::UnsupportedWidget>(card.type);
}

} // namespace app::dashboard
