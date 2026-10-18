#ifndef APP_CPP_UPDATE_IUPDATEADAPTER_H
#define APP_CPP_UPDATE_IUPDATEADAPTER_H

#include "UpdateManifest.h"

#include <functional>
#include <string>

namespace app::update {

// Per-platform firmware update shim. ESP32-P4: HTTPS download, ECDSA
// signature check, A/B slot + c6fw partition writes (EspUpdateAdapter);
// simulator: fake. Blocking calls are only ever made from a background
// task (UpdateController uses IBackgroundRunner), never the UI thread.
class IUpdateAdapter {
public:
    virtual ~IUpdateAdapter() = default;

    // Downloads the manifest and its detached signature and verifies the
    // signature. Blocking. On success `json` holds the exact signed bytes.
    virtual bool fetch_verified_manifest(const std::string& url, std::string& json, std::string& error) = 0;

    using Progress = std::function<void(int percent)>;

    // Downloads and verifies (SHA-256) the app image and, if present, the
    // co-processor image into the inactive slot, then selects that slot
    // for the next boot. Does not restart. Blocking.
    virtual bool install(const UpdateManifest& manifest, const std::string& manifest_url, const Progress& progress,
                         std::string& error) = 0;

    virtual std::string installed_version() const = 0;
    virtual std::string coprocessor_version() const = 0; // "" if unknown

    virtual void restart() = 0;
};

} // namespace app::update

#endif // APP_CPP_UPDATE_IUPDATEADAPTER_H
