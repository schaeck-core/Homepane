#include "HaCommandService.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::home_assistant {

namespace {
struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;
} // namespace

app::core::CancellationSource HaCommandService::call_service(const std::string& domain, const std::string& service,
                                                               const std::string& entity_id,
                                                               const std::string& extra_service_data_json,
                                                               HaClient::ResultCallback callback,
                                                               uint32_t timeout_ms) {
    return send_call(domain, service, entity_id, extra_service_data_json, false, std::move(callback), timeout_ms);
}

app::core::CancellationSource HaCommandService::call_service_with_response(
    const std::string& domain, const std::string& service, const std::string& entity_id,
    const std::string& extra_service_data_json, HaClient::ResultCallback callback, uint32_t timeout_ms) {
    return send_call(domain, service, entity_id, extra_service_data_json, true, std::move(callback), timeout_ms);
}

app::core::CancellationSource HaCommandService::list_todo_items(const std::string& entity_id,
                                                                  HaClient::ResultCallback callback,
                                                                  uint32_t timeout_ms) {
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "type", "todo/item/list");
    cJSON_AddStringToObject(root.get(), "entity_id", entity_id.c_str());
    char* text = cJSON_PrintUnformatted(root.get());
    std::string serialized = text ? text : "";
    if (text) cJSON_free(text);
    return client_.send_command(serialized, std::move(callback), timeout_ms);
}

app::core::CancellationSource HaCommandService::send_call(const std::string& domain, const std::string& service,
                                                            const std::string& entity_id,
                                                            const std::string& extra_service_data_json,
                                                            bool return_response, HaClient::ResultCallback callback,
                                                            uint32_t timeout_ms) {
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "type", "call_service");
    cJSON_AddStringToObject(root.get(), "domain", domain.c_str());
    cJSON_AddStringToObject(root.get(), "service", service.c_str());
    cJSON_AddBoolToObject(root.get(), "return_response", return_response);

    cJSON* service_data = cJSON_CreateObject();
    if (!extra_service_data_json.empty()) {
        CJsonPtr extra(cJSON_Parse(extra_service_data_json.c_str()));
        if (extra && cJSON_IsObject(extra.get())) {
            cJSON* field = nullptr;
            cJSON_ArrayForEach(field, extra.get()) {
                if (field->string) cJSON_AddItemToObject(service_data, field->string, cJSON_Duplicate(field, true));
            }
        }
    }
    cJSON_AddStringToObject(service_data, "entity_id", entity_id.c_str());
    cJSON_AddItemToObject(root.get(), "service_data", service_data);

    char* text = cJSON_PrintUnformatted(root.get());
    std::string serialized = text ? text : "";
    if (text) cJSON_free(text);

    return client_.send_command(serialized, std::move(callback), timeout_ms);
}

} // namespace app::home_assistant
