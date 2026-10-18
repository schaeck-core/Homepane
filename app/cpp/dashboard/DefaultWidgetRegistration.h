#ifndef APP_CPP_DASHBOARD_DEFAULTWIDGETREGISTRATION_H
#define APP_CPP_DASHBOARD_DEFAULTWIDGETREGISTRATION_H

#include "../home_assistant/EntityStore.h"
#include "../home_assistant/HaCommandService.h"
#include "WidgetFactory.h"

namespace app::dashboard {

// Registers every card type migrated to C++ so far (grows with each
// phase: clock in Phase 6, "tile" for the sensor domain in Phase 7,
// "light" in Phase 8, ...) into `factory`. Everything else keeps falling
// back to UnsupportedWidget. Kept separate from WidgetFactory itself so
// the factory stays generic and does not need to depend on concrete
// widget types (C++ migration phase 6/7/8).
void register_default_widgets(WidgetFactory& factory, app::home_assistant::HaInstanceId instance,
                               app::home_assistant::HaCommandService& commands);

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_DEFAULTWIDGETREGISTRATION_H
