# Architecture

How Homepane's code is organised and the rules it follows. For building and running see the [README](../README.md); for UI work the [design guide](design/DESIGN_GUIDE.md); for AI assistants [AGENTS.md](../AGENTS.md).

Homepane is C and C++17 on LVGL 9. The same application code runs on the ESP32-P4 (ESP-IDF) and in the desktop simulator (SDL); only thin platform adapters differ.

## 1. Rules

1. **One state per entity.** Every Home Assistant entity has exactly one canonical state in the `EntityStore`. Widgets do not keep their own copy.
2. **Transports move bytes.** WebSocket and MQTT code transports and correlates messages. It owns no LVGL objects and no widget state, and never calls widgets.
3. **Protocol code normalises.** Home Assistant's initial snapshot and later deltas are turned into one form before the store sees them. Widgets never see `a`/`c`/`+`, request ids or subscription ids.
4. **Subscriptions are handles.** Consumers observe the store through move-only handles. The store knows no widgets and no LVGL.
5. **Widgets declare what they need**: entity state, a weather forecast or statistics are different kinds of requirements.
6. **LVGL only on the UI thread.** Network and FreeRTOS callbacks post owned data into a bounded UI queue.
7. **A dashboard session owns its widgets** (`std::vector<std::unique_ptr<Widget>>`). On a switch, subscriptions and timers stop first, then widgets and the LVGL tree go.
8. **One codebase.** The simulator and the ESP32-P4 build the same application, core, Home Assistant and UI code. Only transport, file system, time, tasks, memory metrics and hardware are platform adapters.
9. **No exceptions, no RTTI.** Errors are explicit (status, optional value, result). ESP-IDF builds with both disabled.
10. **Restarts are not a strategy.** Resources have budgets and deterministic cleanup; caches evict, queues are bounded.
11. **No update without rollback.** An OTA image is only provisional until it confirms itself; otherwise the bootloader returns to the previous one.
12. **MQTT is a transport of its own**, independent of the Home Assistant WebSocket, with the same ownership and threading rules.

## 2. Modules

```text
app/                      platform-independent application
  cpp/
    application/          Application, DashboardController, SetupController, PairingController,
                          AppStateController, WifiController, UpdateController
    core/                 UiDispatcher, CancellationToken, ConnectionEpoch, DashboardGeneration, clocks, logging
    net/                  IWebSocketTransport, WebSocketConnection, BackoffPolicy, ConnectionManager,
                          IHttpFetcher, IWifiAdapter, Improv serial
    home_assistant/       HaClient, HaProtocolDecoder, EntityStore, EntitySubscriptionCoordinator,
                          QueryCoordinator, HaCommandService, DeviceLink
    dashboard/            DashboardParser, DashboardModel, DashboardLayout, WidgetFactory, DashboardSession
    ui/                   Widget, tiles/, details/, music/, overlays (doorbell, screensaver)
    music_assistant/      MaClient, MaProtocolDecoder, MaController, MaPlayerStore
    media/                ImageLoader, ImageDecoder, FileImageCache
    mqtt/                 MqttController, HA discovery
    update/               update manifest, IUpdateAdapter
    voice/, sensors/, doorbell/, eventcalendar/, settings/
    app_facade.h/.cpp     the C ABI between the C views and the C++ application
  views/, addons/         screens and settings in C (being replaced piece by piece)
platforms/
  simulator/              SDL, libwebsockets transport, demo Home Assistant, script runner
  esp32p4/                ESP-IDF project: board support, esp_websocket_client / esp-mqtt / esp_http_client
                          adapters, OTA adapter, C6 co-processor update, Improv
```

Dependencies point one way:

```text
LVGL C API <- ui/widgets <- dashboard/application -> home_assistant -> net interfaces
                                    |                       |
                                    v                       v
                               UiDispatcher          platform transports
```

Nothing points from a transport or the store to a widget or to LVGL.

Responsibilities:

- **`Application`** and the facade are the composition root: they build the platform adapters, controllers, clients and the dispatcher. There is no free-floating singleton collection.
- **`WebSocketConnection`**: connection state machine, send queue, frame reassembly, request correlation, epoch, backoff with jitter. Data leaves it as owned messages. `ConnectionManager` can own several named connections and refuses duplicates unless asked to replace one.
- **`HaProtocolDecoder`** validates and normalises `auth_*`, `result`, `event` and `subscribe_entities` snapshots and deltas into typed messages.
- **`HaClient`**: authentication, commands with timeouts, reconnect hooks. No widget references.
- **`EntityStore`** owns `entity_id -> EntityRecord`, merges snapshots and deltas and notifies observers.
- **`EntitySubscriptionCoordinator`** deduplicates network subscriptions: many local observers of one entity mean at most one Home Assistant subscription per connection and epoch.
- **`QueryCoordinator`**: long-lived streams and one-shot or periodic queries that are not entity state (weather forecast, statistics).
- **`HaCommandService`**: service calls for widgets.
- **`DashboardParser`** turns Lovelace JSON into an owned `DashboardModel` without touching LVGL; **`DashboardLayout`** places it on the tile grid (pure, host-tested).
- **`WidgetFactory`** creates a `std::unique_ptr<Widget>` per registered card type; unknown types become a visible unsupported widget.
- **`DashboardSession`** owns the model, the widgets, their subscription handles, timers and the LVGL root. A monotonic `DashboardGeneration` keeps late results of an old session from touching a new one.
- **`UiDispatcher`**: a bounded queue; producers post small owned commands, the UI thread drains it.
- Overlays (doorbell, screensaver, music player, detail pages) live above the dashboard session and survive dashboard switches.

## 3. Ownership and lifetimes

| Object | Owner | Rule |
|---|---|---|
| `Application`, controllers | platform entry / facade | lifetime of the firmware |
| `WebSocketConnection` | its client (`HaClient`, `MaClient`) or `ConnectionManager` | until stopped or replaced; events carry the epoch |
| `EntityRecord` | `EntityStore` | may outlive dashboard sessions; keyed per Home Assistant instance |
| network subscription | `EntitySubscriptionCoordinator` | counted per entity and epoch |
| local observation | move-only `SubscriptionHandle` | RAII; releasing it is idempotent |
| widgets | `DashboardSession` | from a successful parse until the session shuts down |
| a widget's `lv_obj_t*` | the LVGL tree | not owned by the widget; cleared on `LV_EVENT_DELETE` |
| `cJSON*` from a message | the parse scope | never stored; needed values are copied |
| UI queue command | the queue, by value | until drained or dropped by policy |

A dashboard session shuts down in this order:

1. Mark the generation as stopping; accept no new commands.
2. Cancel entity and query handles and timers.
3. Ignore queued commands of the old generation.
4. Delete the LVGL root on the UI thread (`LV_EVENT_DELETE` clears raw pointers).
5. Destroy the widgets, then the model.

The `EntityStore` may outlive a session; its observers never own one. Observers are addressed through handles and tokens, not raw widget pointers.

## 4. Entity store

- A record exists from the first local need (`loading`), so widgets can be built before the first snapshot.
- A snapshot replaces the state; a delta is applied field by field. Removed attributes are explicit.
- `unknown` and `unavailable` are Home Assistant states and keep their string. `stale` is local: after a disconnect the last value stays visible, marked as outdated.
- Only real changes raise the version and notify observers.
- Updates and reads happen serially on the UI thread, so records need no locks.
- Attributes are owned values, never borrowed `cJSON*`.

## 5. Data requirements and subscriptions

### 5.1 What a widget declares

After construction and before its first data, a widget returns its `requirements()`: entity state, a forecast or a statistics query (`std::variant` of small structs). It then gets `on_data()` on the UI thread.

### 5.2 Subscription handles

- `observe()` returns a move-only handle; destroying or cancelling it is idempotent.
- The first value is not delivered inside `observe()` (the widget may be half built) but afterwards, through the queue or `deliver_current()`.
- Notification never holds an iterator across a callback, so a callback may drop its own handle.
- Several widgets of one entity have separate handles on the same record.
- Widgets may keep derived UI state (a formatted label), not a second copy of the entity.

### 5.3 Kinds of widgets

- Light, switch, cover, climate: entity state for display; actions through `HaCommandService`; optimistic display only with an explicit pending state that the server confirms or rolls back.
- Sensor: entity state, `loading` first.
- Weather: entity state plus its own forecast stream, combined in the widget.
- Statistics: a periodic query in the `QueryCoordinator`, keyed by entities, period and types; its timer belongs to the query.
- Clock: no subscription, a local timer.

## 6. Connections: authentication, reconnect, resubscribe

```text
stopped -> connecting -> transport_connected -> authenticating -> ready
              ^                                     |
              |                                     v
       backoff_wait <- disconnected/degraded <- closing
```

1. `connect()` raises the connection epoch; replies of an older epoch cannot reach current handlers.
2. The transport reassembles fragmented frames on both platforms, with a maximum message size.
3. `auth_ok` makes the client ready; `auth_invalid` is not retried endlessly.
4. Requests have a timeout and cancellation; a one-shot handler ends exactly once (result, timeout, disconnect or cancel).
5. On a disconnect the records of that connection become `stale` and keep their last state.
6. Reconnects use exponential backoff with jitter and a cap.
7. After a new `auth_ok` the coordinators subscribe again to what is still needed; old subscription ids are dropped.
8. The first new snapshot replaces the stale state.
9. Duplicate named connections are refused or replaced explicitly; loading a dashboard reuses the existing connection.
10. Several connections (Home Assistant, Music Assistant, MQTT) have separate id spaces, queues and reconnect states. Music Assistant has its own protocol (`message_id` strings, `auth` / `auth/login` as commands).

## 7. Dashboard and widget lifecycle

1. `DashboardController::render()` fetches the Lovelace configuration through `HaClient` (and again when Home Assistant reports `lovelace_updated` for that dashboard).
2. The reply is parsed into an owned `DashboardModel`; errors name the JSON path. Card fields only one widget needs stay in the card's raw JSON for that widget to read.
3. A new session is built on the UI thread; the old one is shut down first.
4. The factory creates one widget per card. Constructors take configuration only: no network, no LVGL.
5. Requirements are collected; the coordinators create `loading` records and deduplicate subscriptions.
6. `create(parent)` builds a valid loading state first; then observers start and the current state is delivered.
7. Events that arrive meanwhile land in the store and reach the widgets through `deliver_current()`.
8. Updates are incremental: widgets keep their child objects and update them, instead of rebuilding.
9. Late replies (dashboard, statistics, pictures) carry the generation and are dropped when it is old.

## 8. C, C++ and LVGL

- C++ sources are `.cpp`; LVGL and C headers have `extern "C"` guards. The C views talk to the C++ application through `app_facade.h` (a C ABI).
- LVGL callbacks are static trampolines that forward to the object in the user data. No capturing lambdas as C callbacks.
- An object lives until its callbacks are removed. `LV_EVENT_DELETE` clears pointers and never frees twice.
- Destructors do not call LVGL. UI resources are released in `shutdown_ui()` on the UI thread; destructors only free C++ resources.
- Exceptions never cross the C boundary (and are disabled anyway).

## 9. Threading and the UI queue

- Network tasks collect frames, check little, and post owned messages; they never lock LVGL.
- Protocol handling and store updates run when the UI thread drains the queue.
- The UI thread drains the queue, runs observers and updates LVGL. On the ESP32-P4 the main loop holds the LVGL lock while it polls the application, so the app is effectively single-threaded, like in the simulator.
- Background work (HTTP, decoding, flash) runs through `IBackgroundRunner` and returns through `app_cpp_post_to_ui()`.

Queue rules:

- a fixed capacity; no unbounded allocation in an event storm;
- entity updates are coalesced to the newest version; control, auth and disconnect are never dropped (`critical`);
- overflow is counted; payloads are owned;
- latency, high-water mark and drops are measured.

## 10. Strings and memory

- `std::string` for every owned string. `std::string_view` only as a short-lived parameter, never stored.
- No `strdup`/`free` chains in C++; C library results get an RAII deleter.
- `std::unique_ptr` for ownership; `std::shared_ptr` only where shared lifetime is real (decoded pictures). Observers use handles, not `shared_ptr<Widget>`.
- On the ESP32-P4 internal RAM (~400 KB) is the scarce kind: Wi-Fi's DMA buffers need it, and the panel can crash when its largest free block falls below ~16 KB. So `malloc()` prefers PSRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`), LVGL moves to PSRAM below 80 KB of free internal RAM (`LvMem.c`), and task stacks that never touch flash live in PSRAM. Internal RAM stays for DMA, stacks that need it and Wi-Fi.
- Caches have budgets: decoded pictures in a PSRAM LRU, cover files on the SD card with a file limit.
- Measure after changes: the debug build's `/sys/mem`, or the MQTT diagnostics (internal free, lowest, largest block).
- Never log tokens or passwords.

## 11. Build

- One application source list for both platforms; platform differences only at adapter boundaries, not as `#ifdef ESP_PLATFORM` spread through application classes (`app_facade.cpp` is where adapters are chosen).
- C++17. The simulator uses libwebsockets, curl and SDL; the ESP32-P4 uses `esp_websocket_client`, `esp_http_client` and esp-mqtt. Both pass the same contract tests.
- Generated fonts and LVGL stay C. Third-party code is not compiled as C++.
- New `.cpp` files: the simulator picks up tiles and detail pages by itself; everything else is added to `CMakeLists.txt` and `platforms/esp32p4/main/CMakeLists.txt`.

## 12. Tests

- Host tests (`tests/host/`, `ctest`): protocol decoders, store, coordinators, parser and layout, WebSocket fragmentation and reconnect, dispatcher overflow, controllers, widgets with headless LVGL, MQTT, update manifests, Improv.
- Fixtures are recorded from real servers, not guessed.
- Every widget change is tested with two instances of the same entity, a dashboard switch and a reconnect.
- On the device: memory before and after dashboard loads, switches and reconnects; long runs must show no steady loss and no shrinking largest free block.

## 13. Firmware updates and MQTT

### 13.1 A/B updates

The flash has two app slots (`ota_0`, `ota_1`) and, next to each, a slot for the matching ESP32-C6 image (`c6fw_0`, `c6fw_1`). An update downloads into the inactive slot, checks SHA-256, and only then switches the boot slot.

### 13.2 Rollback

With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` a new image starts as pending. It confirms itself only after a real frame was rendered; a crash before that makes the bootloader return to the previous image.

### 13.3 Signed manifests

The panel only installs from a manifest (`firmware-manifest.json`) whose ECDSA P-256 signature matches the public key in `platforms/esp32p4/main/ota_signing_pubkey.pem`. The private key is a CI secret (`OTA_SIGNING_KEY`). The manifest names the version, the minimum version to update from, the image and the C6 image with their hashes. No downgrades.

### 13.4 MQTT

MQTT is a sibling of the WebSocket transport (`IMqttTransport`; ESP: esp-mqtt). The panel registers itself as one Home Assistant device by discovery (`homeassistant/<component>/<node_id>/<object_id>/config`, retained). The `node_id` comes from the MAC and never changes. Availability uses a last will (`homepane/<node_id>/status`). Discovery is sent again when Home Assistant restarts. Entities: sensors (presence, temperature, ...), diagnostics (Wi-Fi, uptime, memory, last restart reason), a restart button and a display switch. Values are sent on change, with thresholds and minimum intervals. The MQTT and WebSocket paths do not depend on each other.

### 13.5 The ESP32-C6 co-processor

Wi-Fi runs on an ESP32-C6 over SDIO (ESP-Hosted). Its firmware is updated by the P4 from the `c6fw` slot of the running app, only when newer. The host's SDIO receive mode must match the C6 firmware's (streaming today); changing it is a C6 firmware change. See `platforms/esp32c6_coprocessor/README.md`.

## 14. Later features

These shape today's interfaces even where they are not built yet:

- **Music Assistant** has its own client and protocol, not Home Assistant entities. Login with username and password; the 30-day session token renews itself. Search results and queues are bounded, paged and cancellable. Covers use a RAM LRU and an SD card cache; downloads and decoding happen off the UI thread.
- **Notifications, doorbell, camera**: overlays above the dashboard with priority and timeout. Cameras send MJPEG or single JPEGs at a panel-sized resolution; frames are bounded (drop the oldest).
- **Voice**: audio behind platform interfaces with bounded ring buffers. Wake word on the panel, speech recognition and answers on the server. With several panels, one wins a session (arbitration by confidence and level); the session carries the panel's area so "lights off" means this room.

## 15. Do and don't

Do:

- Read the code you change; documents describe intent, the code the current state.
- Make small vertical changes with tests.
- Delete replaced C code in the change that migrates its last user.
- Document ownership in headers; prefer move-only handles and explicit cancel.
- Test widget changes with duplicate instances, dashboard switches and reconnects.

Don't:

- Rename `.c` files to `.cpp` wholesale.
- Put widget pointers or LVGL objects in the store, or network details in widgets.
- Use subscription ids as widget, entity or query identity.
- Store borrowed `cJSON` strings or `string_view`s across callbacks.
- Call LVGL from network or worker tasks, not even under a lock.
- Add unbounded queues, caches, result lists, history or retries.
- Add global singleton or hash-map layers instead of real ownership.
- Let exceptions cross the C ABI or build a factory on RTTI.
- Keep replaced code "just in case".
