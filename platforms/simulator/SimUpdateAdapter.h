#ifndef PLATFORMS_SIMULATOR_SIMUPDATEADAPTER_H
#define PLATFORMS_SIMULATOR_SIMUPDATEADAPTER_H

#include "../../app/cpp/update/IUpdateAdapter.h"

namespace platform_simulator {

// Desktop stand-in for the firmware update UI: always "offers" version
// 0.1.0.999 (no network, no signature check — the simulator has no flash to
// protect); installing just reports progress. With the environment variable
// SIM_UPDATE_FAIL set, the install fails at 60 %.
class SimUpdateAdapter final : public app::update::IUpdateAdapter {
public:
    bool fetch_verified_manifest(const std::string& url, std::string& json, std::string& error) override;
    bool install(const app::update::UpdateManifest& manifest, const std::string& manifest_url, const Progress& progress,
                 std::string& error) override;
    std::string installed_version() const override { return "0.1.0"; }
    std::string coprocessor_version() const override { return "2.12.3-sb.1"; }
    void restart() override;
};

} // namespace platform_simulator

#endif // PLATFORMS_SIMULATOR_SIMUPDATEADAPTER_H
