/* ESP32 shim: redirect <cjson/cJSON.h> to the ESP-IDF json component header.
 *
 * Bug fixed here: a plain `#include "cJSON.h"` re-resolves to this very
 * file on a case-insensitive filesystem (macOS default) — quoted-include
 * lookup checks this file's own directory first, and "cJSON.h" matches
 * "cJSON.h" regardless of case, so it matches itself. `#pragma once` then
 * silently swallows that self-include, leaving no real declarations
 * behind (found by C++ migration phase 4: the first C++ file to
 * actually exercise this shim on ESP32-P4). `#include_next` continues the
 * search *after* this file's directory, reaching the real ESP-IDF header
 * instead of looping back here. */
#pragma once
#include_next "cJSON.h"
