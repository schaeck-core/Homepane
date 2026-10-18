#ifndef PLATFORMS_ESP32P4_MAIN_ESPUPDATEADAPTER_H
#define PLATFORMS_ESP32P4_MAIN_ESPUPDATEADAPTER_H

#include "../../../app/cpp/update/IUpdateAdapter.h"

namespace platform_esp32p4 {

// Firmware updates on the ESP32-P4 (Track U, Phase U1):
// - HTTPS via esp_http_client + the ESP-IDF certificate bundle;
// - the manifest must carry a valid ECDSA P-256 signature
//   (<manifest url>.sig, DER) by the key whose public half is embedded as
//   ota_signing_pubkey.pem; the images are then checked against the
//   SHA-256 values in the signed manifest;
// - the app goes into the inactive OTA slot, the co-processor container
//   into that slot's c6fw_N partition (slave_ota.h); the boot slot is only
//   switched once both are complete and verified.
class EspUpdateAdapter final : public app::update::IUpdateAdapter {
public:
    bool fetch_verified_manifest(const std::string& url, std::string& json, std::string& error) override;
    bool install(const app::update::UpdateManifest& manifest, const std::string& manifest_url, const Progress& progress,
                 std::string& error) override;
    std::string installed_version() const override;
    std::string coprocessor_version() const override;
    void restart() override;
};

} // namespace platform_esp32p4

#endif // PLATFORMS_ESP32P4_MAIN_ESPUPDATEADAPTER_H
