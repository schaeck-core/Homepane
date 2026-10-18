#include "PanelListParser.h"

#include <cjson/cJSON.h>

#include <string>

namespace app::application {

std::vector<PanelOption> PanelListParser::parse(const std::string& get_panels_result_json) {
    std::vector<PanelOption> options;

    cJSON* root = cJSON_Parse(get_panels_result_json.c_str());
    if (!root) return options;

    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, root) {
        const cJSON* config = cJSON_GetObjectItemCaseSensitive(entry, "config");
        if (!cJSON_IsObject(config)) continue;
        // Matches the legacy app/views/setup.c filter exactly: only
        // panels with a "config.mode" are real Lovelace dashboards (the
        // built-in "config"/"developer-tools"/... panels have a
        // "config" key too, but it never has "mode").
        if (!cJSON_GetObjectItemCaseSensitive(config, "mode")) continue;

        const cJSON* title = cJSON_GetObjectItemCaseSensitive(entry, "title");
        const cJSON* url_path = cJSON_GetObjectItemCaseSensitive(entry, "url_path");
        if (!cJSON_IsString(url_path)) continue;
        // HA's default dashboard has no title ("Overview" in the frontend):
        // empty here, the UI names it
        const bool is_default = std::string(url_path->valuestring) == "lovelace";
        if (!cJSON_IsString(title) && !is_default) continue;

        options.push_back(PanelOption{cJSON_IsString(title) ? title->valuestring : "", url_path->valuestring});
    }

    cJSON_Delete(root);
    return options;
}

} // namespace app::application
