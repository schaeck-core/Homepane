#ifndef APP_CPP_HOME_ASSISTANT_HACOMMANDSERVICE_H
#define APP_CPP_HOME_ASSISTANT_HACOMMANDSERVICE_H

#include "../core/CancellationToken.h"
#include "HaClient.h"

#include <string>

namespace app::home_assistant {

// Thin, domain-shaped wrapper around HaClient::send_command() for HA
// `call_service` calls (CPP_MIGRATION_PLAN.md §6.3: "Aktionen als
// HaCommandService"). Widgets build no JSON of their own; they never see
// the "id" field or raw send_command() plumbing.
class HaCommandService {
public:
    explicit HaCommandService(HaClient& client) : client_(client) {}

    // `extra_service_data_json` is an owned JSON object fragment merged
    // into "service_data" alongside entity_id (e.g. {"brightness":255}),
    // or empty for none. Same one-shot-callback contract as
    // HaClient::send_command(): fires exactly once, on Result, timeout,
    // disconnect, or via the returned CancellationSource.
    app::core::CancellationSource call_service(const std::string& domain, const std::string& service,
                                                const std::string& entity_id,
                                                const std::string& extra_service_data_json,
                                                HaClient::ResultCallback callback, uint32_t timeout_ms = 10000);

    // The same with "return_response": the Result's "response" carries the
    // service's data (e.g. calendar.get_events).
    app::core::CancellationSource call_service_with_response(const std::string& domain, const std::string& service,
                                                              const std::string& entity_id,
                                                              const std::string& extra_service_data_json,
                                                              HaClient::ResultCallback callback,
                                                              uint32_t timeout_ms = 10000);

    // todo/item/list: the Result is {"items": [{uid, summary, status, due?}, ...]}.
    app::core::CancellationSource list_todo_items(const std::string& entity_id, HaClient::ResultCallback callback,
                                                   uint32_t timeout_ms = 10000);

private:
    app::core::CancellationSource send_call(const std::string& domain, const std::string& service,
                                             const std::string& entity_id, const std::string& extra_service_data_json,
                                             bool return_response, HaClient::ResultCallback callback,
                                             uint32_t timeout_ms);

    HaClient& client_;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_HACOMMANDSERVICE_H
