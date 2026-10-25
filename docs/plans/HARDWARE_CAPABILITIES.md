# Plan: Hardware capabilities — detect what is there, hide what is not

Status: steps 1–3 and the settings part of step 5 implemented 2026-10-19 (see *Implemented*); drivers for BOS1921, ENS160/SGP40 and LD6004 open. Noted 2026-10-18, hardware list revised 2026-10-19.

Homepane runs on the stock Waveshare ESP32-P4-WIFI6-Touch-LCD-4B and, later, on boards with extra hardware (sensors, haptics, mmWave, extra microphones, relays). The panel checks at boot which of it is connected. Features that need missing hardware are greyed out in the settings and do not appear anywhere else: not on the dashboard, header or screensaver, and not as MQTT entities in Home Assistant. Today the device shows made-up values instead.

## Current state

- `app/cpp/app_facade.cpp:203` uses `app::sensors::SimulatedSensors g_sensors` on **every** platform, including the device.
- Consumers of it:
  - Header room temperature: `app/views/dashboard.c:154` (`app_cpp_room_temperature()`, shows "–" for `NAN`).
  - Screensaver inside temperature: `app/cpp/ui/Screensaver.cpp:616` (`room_temperature`, `NAN` = none).
  - MQTT: `app/cpp/mqtt/MqttController.cpp` publishes the readings, gestures (`take_gestures`), haptics (`haptic(1)`) and a `simulated_data` flag.
  - Settings: `app/views/settings/general.c` has a haptics switch (`haptic_enabled`), shown regardless of hardware.
- Interface: `app/cpp/sensors/SensorReadings.h` (`ISensorSource`, readings as `std::optional`, `simulated()`).
- Hardware and pins: `docs/hardware/SENSORS.md`. Relays on the 4B bottom board: `docs/plans/RELAYS_AND_FALLBACK.md`.

Effect on a stock 4B today: a fake room temperature in the header and on the screensaver, fake sensor entities in Home Assistant, and a haptics switch without haptics.

## Planned hardware

| Part | Connection | Features | Missing → |
|---|---|---|---|
| BOS1921 + 4 piezo actuators | I²C GPIO7/8, optional IRQ GPIO21 | force / press detection, haptics, piezo tones | haptics, force gestures and piezo tones hidden |
| SHTC3 | I²C GPIO7/8 | temperature, humidity | climate display and its settings hidden |
| VOC / air quality (optional, e.g. ENS160 or SGP40) | I²C GPIO7/8 | VOC / AQI / eCO₂, depending on the sensor | only the values the detected sensor delivers |
| mmWave: HLK-LD2410 (first, for testing) or HLK-LD6004 (later) | UART GPIO2 TX / GPIO3 RX | presence; LD6004 also target count and position | presence / radar settings hidden |
| 2× PDM MEMS front microphones (optional) | GPIO4 PDM CLK, GPIO5 PDM DATA | better far-field audio | nothing changes; the board microphones are used |

Ambient light (auto brightness, lux) is left out for now and comes back as its own step later.

**GPIO7/8 is the BSP's I²C bus** (`BSP_I2C_SDA` / `BSP_I2C_SCL` in `components/esp32_p4_wifi6_touch_lcd_4b/include/bsp/esp32_p4_wifi6_touch_lcd_4b.h`, 400 kHz, `CONFIG_BSP_I2C_NUM=1`), shared with the GT911 touch controller and the ES8311 / ES7210 codecs. The sensors are not a second bus: drivers attach to the BSP's bus handle (`bsp_i2c_get_handle()`), do not create their own.

Addresses (7-bit):

| Address | Part | |
|---|---|---|
| 0x14 / 0x5D | GT911 touch | board |
| 0x18 | ES8311 codec | board |
| 0x40 | ES7210 mic ADC | board |
| 0x44 | BOS1921 | assumed, confirm in the datasheet |
| 0x52 / 0x53 | ENS160 | optional |
| 0x59 | SGP40 | optional |
| 0x70 | SHTC3 | |

No collisions. Note for later: 0x44 is now taken, so an SHT4x or an OPT3001 light sensor (0x44 by default) would need another address.

## Implemented (2026-10-19)

- `app/cpp/hardware/Capabilities.h`: the model (`detected`, haptics, force, temperature, humidity, illuminance, presence, target count / position, `parts`), plus `apply_override()` for the simulator's `HOMEPANE_HW` (`none`, `no-haptics`, `no-climate`, `no-presence`, `ld2410`).
- `ISensorSource::capabilities()`. `SimulatedSensors` pretends the planned hardware (no light sensor) and fills only what its capabilities allow; `NullSensors` has nothing.
- Device: `platforms/esp32p4/main/EspSensors.cpp`, started from `app_cpp_start()`. Probes on its own task (stack in PSRAM): SHTC3 (ID check, read every 5 s without clock stretching, stops after 5 failures), BOS1921 / ENS160 / SGP40 (found and listed in system info, no driver, so no capability yet), LD2410 on UART1 (256000 baud, waits up to 600 ms for a report frame, then keeps reading presence and distance). The simulated sensors are gone from the device.
- MQTT: discovery per capability; entities without hardware get an empty retained config (removed in HA); announced again when the capabilities change (probing finished). `simulated_data` only from the simulator.
- Header: room temperature hidden without a reading. Screensaver: already empty without one.
- Settings → General: haptics and presence rows greyed out with "Not detected" (switch shown off, not clickable; the saved value stays). System info lists the detected parts.
- Host tests in `tests/host/test_mqtt.cpp` (no hardware, hardware found after connecting, LD2410 without positions, override).

Not done yet: BOS1921 driver (haptics, force, tones), ENS160/SGP40 values, LD6004, front microphones, relays.

## Bus load and risks

The shared bus is enough for this, with three things to watch:

- **Bandwidth.** At 400 kHz the bus moves about 40 kB/s. The touch controller is polled (no touch IRQ on the 4B, `BSP_LCD_TOUCH_INT` is NC): roughly one short read per LVGL input period, a few percent of the bus. SHTC3 and ENS160 / SGP40 read a few bytes every second or less, negligible. The codecs are only configured at start. The one part that can use a lot of bus time is the **BOS1921**: polling force data at a high rate, or streaming waveforms sample by sample, can take a large share of the bus and make touch feel sluggish. The chip has two features against that, and the driver uses them:
  - **Waveform synthesizer (WFS):** clicks and simple vibrations are generated inside the chip from stored parameters; the panel only sends a start command. Default for haptic feedback.
  - **1024-sample FIFO:** tones and longer waveforms are sent in bursts that fill the FIFO, not in real time; the bus is free in between. Refill on the FIFO's level interrupt (GPIO21), not by polling.
  Plus: wire the **IRQ on GPIO21** (force events and FIFO level without polling), keep tones short, and measure the touch latency while a tone plays.
- **One stuck part blocks all.** A sensor that holds SDA low stops touch and audio too. Drivers use short timeouts, and the bus driver runs the standard recovery (clock pulses until SDA is released) on a timeout. A part that keeps failing is disabled for the rest of the boot and logged.
- **Electrical.** The add-on board hangs on a cable: keep it short (aim under 20 cm), add no second set of strong pull-ups (the main board already has them; check the values in the schematic), and keep SDA/SCL away from the BOS1921's high-voltage piezo lines. If the bus turns out noisy or slow in practice, the fallback is a second I²C port for the add-on parts on free GPIOs; the P4 has two I²C controllers plus an LP I²C.

The ESP-IDF I²C master driver serialises transfers of devices on the same bus handle, so no extra lock is needed as long as everyone uses the BSP's handle.

## Goal

1. **Before launch (step 1):** no made-up data on the device. Small, self-contained.
2. **Then (steps 2–5):** a capability model filled by probing at boot (plus fixed board facts), read by the UI, the settings and MQTT.

## Step 1: no simulated data on the device

- Device: a sensor source that has nothing (`NullSensors`: every reading empty, `simulated()` false, no gestures, `haptic()` ignored). Simulator: keeps `SimulatedSensors` (optionally only with `HOMEPANE_DEMO=1` or an env flag).
- The choice is made per platform, not with `#ifdef` inside `app/`: the platform hands the source to the facade (follow how other platform adapters are injected, see `docs/ARCHITECTURE.md`).
- Header (`dashboard.c`): hide the temperature label when the value is `NAN`, instead of "–".
- Screensaver: already handles `NAN`; check it hides the inside value cleanly.
- MQTT: announce (discovery) only the entities whose readings exist. With `NullSensors` nothing sensor-related is announced; entities announced before must be removed (empty retained discovery payload), otherwise stale entities stay in Home Assistant.
- Tests: host tests for the MQTT discovery with an empty source and with the simulated one.
- Check: simulator demo screenshot unchanged; firmware build; on the device, header and screensaver show no temperature, HA shows no sensor entities.

## Step 2: capability model

A small value type in `app/`, e.g. `app::hardware::Capabilities`. Platform code fills it; `app/` only reads it.

| Capability | Source | Notes |
|---|---|---|
| `ethernet`, `relays` (count), `rs485`, `speaker`, `microphone` | board file | fixed per board; the 4B has its own microphones |
| `haptics`, `force`, `piezo_tones` | probe: BOS1921 | `force_irq` if GPIO21 is wired |
| `temperature`, `humidity` | probe: SHTC3 | |
| `air_quality` | probe: ENS160 / SGP40 | a set of values, see below |
| `presence`, `target_count`, `target_position` | probe: LD2410 / LD6004 | LD2410: `presence` only |

- Board-fixed capabilities come from the per-board files (decision in `docs/plans/DISPLAY_SIZES.md`: board selected at build time, not a runtime pinout config).
- `air_quality` is not one flag but the list of values the detected sensor really delivers: ENS160 → AQI, TVOC, eCO₂; SGP40 → VOC index only. The UI and MQTT show exactly these.
- Each detected part also records which chip it is (`"SHTC3"`, `"LD2410"`…) for system info and logs.
- The front microphones are not a capability: nothing in the UI depends on them (see step 4).
- Read-only after boot. Available on the UI thread through the facade. In the simulator: all present with `HOMEPANE_DEMO=1`, plus an env override (e.g. `HOMEPANE_HW=no-haptics,no-presence`) to test the missing cases.

## Step 3: detection at boot (device)

- Runs in its own task after the BSP's I²C init, off the UI thread; must not delay the boot noticeably (budget: well under a second in total). The result goes to the UI through `app_cpp_post_to_ui()`. Until it arrives, hardware features count as absent (no flicker from shown to hidden).
- **I²C:** probe each known address, then confirm with an ID read where the chip has one (SHTC3 ID register, ENS160 part ID, SGP40 serial / self-test, BOS1921 chip ID). An ACK alone is not enough.
- **mmWave** (UART GPIO2/3): one driver interface, two protocols. Try the LD2410 first (256000 baud, frames start with `F4 F3 F2 F1`, it reports on its own every ~100 ms; or send *enable configuration* and wait for the ACK), then the LD6004 (protocol and baud rate per its datasheet). The first one that answers with a valid frame wins; none = absent. The LD2410 reports presence, moving / still target and their distance, no target count and no position, so `target_count` and `target_position` stay off with it.
- **No hot-plug.** Detection runs once per boot. A part that fails later is logged; it does not change the capabilities until the next boot.
- Real drivers implement `ISensorSource` (and a haptics / tone interface for the BOS1921) and fill only what was detected (separate task, not part of this plan).

## Step 4: front microphones

No setting, no UI, no capability. Voice always works with the board microphones (ES7210). When the front PDM pair is fitted, the voice pipeline (`platforms/esp32p4/main/EspVoiceAudio.cpp`, `audio_hw.c`) takes its input from it instead, for better far-field audio.

- How the pipeline knows: simplest is a build / board flag for the variant with front microphones. A silent check at boot also works (short PDM capture on a free I2S port, I2S0 is busy with the codecs: constant samples = absent, noise = present; needs a pull-down on GPIO5). Either way the result only picks the audio input.
- The PDM pair needs its own gain and AEC values.
- System info and logs name the input in use, for debugging.

## Step 5: use the capabilities

Two rules:

- **Settings: grey out, with a reason.** A setting whose hardware was not detected stays visible but disabled, with a short hint ("Sensor not detected"). That way the user sees the feature exists and that the panel looked for it. Use the existing disabled-row pattern (`set_enabled()` in `app/views/settings/screensaver.c`), with the hint text in `UI_TEXT3`.
- **Everywhere else: leave it out.** No tile, header value, screensaver value, overlay or MQTT entity for missing hardware. No placeholders.

Exception: board-fixed capabilities a board can never have (relays on a board without relays, RS485) are left out of the settings as well; greying out only makes sense for things that can be connected.

Per feature:

- **Haptics / force / piezo tones (BOS1921):** haptics switch, force gestures and tone options greyed in settings; no haptic calls, no force gestures, tones fall back to the speaker. MQTT: no haptics or gesture entities.
- **Climate (SHTC3):** header and screensaver room temperature, humidity; climate settings greyed. MQTT: no temperature / humidity.
- **Air quality:** only the values from `air_quality`; settings for it greyed when the list is empty.
- **Presence (LD2410 / LD6004):** presence / radar settings greyed (wake on approach, zones…); no presence-driven screensaver wake. Settings that need position (zones) are greyed with the LD2410 as well. MQTT: only the entities the detected sensor fills.
- **Voice:** unchanged; always available on the 4B.
- **Relays / fallback:** consumers of `relays` once `RELAYS_AND_FALLBACK.md` is implemented.
- **MQTT:** discovery driven by capabilities; removal of entities that disappeared (empty retained payload), e.g. after the add-on board was removed or the LD2410 was swapped for an LD6004.
- **System info:** list of detected parts (chip names), so a missing sensor can be diagnosed without logs.

## Rules to keep (from AGENTS.md)

- `app/` stays platform-independent; device code under `platforms/esp32p4`. New `.cpp` files also into `platforms/esp32p4/main/CMakeLists.txt`.
- LVGL only on the UI thread; probing and I/O asynchronous.
- Visible texts through `app/i18n/*.lang` + `scripts/gen_i18n.py`.
- Check: `cmake --build build && cd build && ctest --output-on-failure`, demo screenshot (`SDL_VIDEODRIVER=dummy HOMEPANE_DEMO=1 HOMEPANE_MONITORS=0 HOMEPANE_SCRIPT="wait 3000; shot out.bmp; quit" ./bin/main`), plus one with the hardware override set to "nothing" to check the greyed-out settings; `cd platforms/esp32p4 && idf.py build` (with `HOMEPANE_AUDIO_DEBUG=1`).
- Simulator logs contain the live HA token: write them to a scratch directory, never `/tmp` or the repo.

## Open

- BOS1921: confirm address 0x44 and the chip ID register in the datasheet (probing checks the ACK only so far); WFS parameters for the clicks.
- I²C pull-up values on the main board; cable length to the add-on board.
- ENS160 or SGP40 (or both supported).
- LD6004 protocol and baud rate (when the sensors arrive).
- Front microphones: build flag or silent detection; which I2S port is free for PDM RX next to the codecs.
- Whether GPIO2–5 and GPIO21 are free on the 4B's headers (not used by the BSP; check the main board schematic for other uses). GPIO2–5 are the P4's pad JTAG pins (MTCK, MTDI, MTMS, MTDO); unused as long as JTAG runs over USB, which is the default.
- Later: ambient light sensor and auto brightness (mind the 0x44 collision).

## Order

Step 1 is the one needed before the public launch and can be its own commit. Steps 2–3 (model, detection) next, with the LD2410 as the first real sensor to test against; then 4 and 5 per feature as the drivers arrive, together with the relay work.
