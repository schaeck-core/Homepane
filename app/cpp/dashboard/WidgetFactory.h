#ifndef APP_CPP_DASHBOARD_WIDGETFACTORY_H
#define APP_CPP_DASHBOARD_WIDGETFACTORY_H

#include "../ui/Widget.h"
#include "DashboardModel.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace app::dashboard {

// Maps a card's "type" string to a real Widget instance. An unregistered
// type yields UnsupportedWidget rather than failing — C++ migration
// phase 6 DoD: "unbekannter Typ crashfrei". No RTTI/dynamic_cast (ARCHITECTURE.md §15
// Don't) — dispatch is a plain string-keyed map of factory functions.
class WidgetFactory {
public:
    using CreatorFn = std::function<std::unique_ptr<app::ui::Widget>(const DashboardCard&)>;

    void register_type(const std::string& card_type, CreatorFn creator);

    std::unique_ptr<app::ui::Widget> create(const DashboardCard& card) const;

    // Compact tiles (small HA cards, DashboardLayout's CellKind::Compact):
    // one creator for every card type; without it create() is used.
    void register_compact(CreatorFn creator) { compact_ = std::move(creator); }
    std::unique_ptr<app::ui::Widget> create_compact(const DashboardCard& card) const {
        return compact_ ? compact_(card) : create(card);
    }

private:
    std::map<std::string, CreatorFn> creators_;
    CreatorFn compact_;
};

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_WIDGETFACTORY_H
