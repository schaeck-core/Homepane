#ifndef APP_CPP_UI_DETAILS_ENTITYDETAIL_H
#define APP_CPP_UI_DETAILS_ENTITYDETAIL_H

#include "../../home_assistant/EntityStore.h"
#include "AlarmDetail.h"
#include "ClimateDetail.h"
#include "CoverDetail.h"
#include "LightDetail.h"
#include "MediaDetail.h"

#include <functional>
#include <memory>
#include <string>

namespace app::ui {

// The detail pages' State from an entity: `name` as the tile shows it,
// `state` the tile's (possibly optimistic) state. Shared by the domains'
// own tiles and the compact tile.
LightDetail::State light_detail_state(const app::home_assistant::EntityState& e, const std::string& name,
                                      const std::string& state);
CoverDetail::State cover_detail_state(const app::home_assistant::EntityState& e, const std::string& name,
                                      const std::string& state);
ClimateDetail::State climate_detail_state(const app::home_assistant::EntityState& e, const std::string& name,
                                          const std::string& state);
MediaDetail::State media_detail_state(const app::home_assistant::EntityState& e, const std::string& name,
                                      const std::string& state);
AlarmDetail::State alarm_detail_state(const app::home_assistant::EntityState& e, const std::string& name,
                                      const std::string& state);

// The detail page of an entity's domain, for tiles that show many domains
// (CompactTile): created on the first open, kept current by update()
// while open. The page lives on the screen; reset the owner's pointer in
// shutdown_ui(). UI thread.
class EntityDetail {
public:
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    explicit EntityDetail(Send send);
    ~EntityDetail();
    EntityDetail(const EntityDetail&) = delete;
    EntityDetail& operator=(const EntityDetail&) = delete;

    // light, cover, climate, media_player, alarm_control_panel (pure; host-tested).
    static bool exists_for(const std::string& domain);
    void open(const std::string& domain, const app::home_assistant::EntityState& e, const std::string& name,
              const std::string& state);
    // Refreshes the page if it is open.
    void update(const app::home_assistant::EntityState& e, const std::string& name, const std::string& state);

private:
    Send send_;
    std::unique_ptr<LightDetail> light_;
    std::unique_ptr<CoverDetail> cover_;
    std::unique_ptr<ClimateDetail> climate_;
    std::unique_ptr<MediaDetail> media_;
    std::unique_ptr<AlarmDetail> alarm_;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_ENTITYDETAIL_H
