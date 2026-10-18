#ifndef APP_CPP_HOME_ASSISTANT_HAPROTOCOLDECODER_H
#define APP_CPP_HOME_ASSISTANT_HAPROTOCOLDECODER_H

#include "HaTypes.h"

#include <string>

namespace app::home_assistant {

// Pure, stateless decoder: raw HA WebSocket JSON text in, a typed
// DecodedMessage out. Never exposes a cJSON pointer across this boundary
// (CPP_MIGRATION_PLAN.md §11 Don't: "Keine geborgten cJSON-Strings...
// über Callbackgrenzen speichern"). Internally uses cJSON only as a
// parsing detail, fully cleaned up before returning.
class HaProtocolDecoder {
public:
    static DecodedMessage decode(const std::string& raw_json);
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_HAPROTOCOLDECODER_H
