#ifndef APP_CPP_MUSIC_ASSISTANT_MAPROTOCOLDECODER_H
#define APP_CPP_MUSIC_ASSISTANT_MAPROTOCOLDECODER_H

#include "MaTypes.h"

#include <string>

namespace app::music_assistant {

// Pure, stateless decoder: raw MA WebSocket JSON text in, a typed
// MaDecodedMessage out. Never exposes a cJSON pointer across this
// boundary (same contract as HaProtocolDecoder — CPP_MIGRATION_PLAN.md
// §11 Don't: "Keine geborgten cJSON-Strings... über Callbackgrenzen
// speichern"). Internally uses cJSON only as a parsing detail, fully
// cleaned up before returning.
class MaProtocolDecoder {
public:
    static MaDecodedMessage decode(const std::string& raw_json);
};

} // namespace app::music_assistant

#endif // APP_CPP_MUSIC_ASSISTANT_MAPROTOCOLDECODER_H
