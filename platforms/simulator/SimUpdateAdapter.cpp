#include "SimUpdateAdapter.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace platform_simulator {

bool SimUpdateAdapter::fetch_verified_manifest(const std::string& url, std::string& json, std::string& error)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    (void)error;
    (void)url;
    const std::string sha(64, '0');
    json = R"({"version":"0.1.0.999","min_supported_version":"0.1.0","path":"firmware/homepane.bin","sha256":")" +
           sha + R"("})";
    return true;
}

bool SimUpdateAdapter::install(const app::update::UpdateManifest&, const std::string&,
                               const Progress& progress, std::string& error)
{
    for (int p = 0; p <= 100; p += 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (std::getenv("SIM_UPDATE_FAIL") && p == 60) {
            error = "SHA-256 mismatch (simulated)";
            return false;
        }
        progress(p);
    }
    return true;
}

void SimUpdateAdapter::restart() { std::printf("[SimUpdateAdapter] restart requested (ignored in the simulator)\n"); }

} // namespace platform_simulator
