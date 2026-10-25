# Plan: Relay outputs, offline fallback and hardware capabilities

Status: idea, not started. Noted 2026-10-18.

The Waveshare 4B's bottom board has two relays. Homepane drives them as potential-free outputs: a push button for a distribution cabinet (KNX-style, the cabinet decides what is switched), a door opener, or a switch for a lamp or a fan. When Home Assistant is unreachable, the panel falls back to a simple screen with these outputs, so the light still works without Home Assistant.

## 1. Hardware (Waveshare 4B bottom board)

From the [bottom board schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B/86_Panel_Bottom_Board.pdf):

- **Chain per channel:** `RELAY1` / `RELAY2` from the main board → SS8050 → PC817 optocoupler → SS8050 → relay HLS8L-DC5V-S-C. The coil runs on an isolated 5 V supply (B0505 DC-DC).
- **Output:** two terminals per relay (L0/L1, L2/L3 on P3), a single potential-free contact.
- **Reset:** 10 kΩ pull-downs on the driver transistors. While the P4 resets (reboot, OTA, watchdog), the relays drop out; they never pull in by accident.
- **Relay ratings** (HLS8L, form C): 10 A 120 VAC / 24 VDC, 7 A 240 VAC; 100,000 operations at rated load, 10 ms operate, 5 ms release.
- **Supply:** 12–24 V on the bus terminals. RS485 is isolated as well (THVD1406).

Open:
- Which P4 GPIOs drive `RELAY1` and `RELAY2` (main board schematic) and their level during boot.
- Whether the relay contact is wired as normally open or normally closed.
- Load limits for LED lamps: LED drivers draw a high inrush current when switched on, which can weld small relay contacts. Ordinary lamps are fine; for larger LED installations the docs recommend a contactor or an impulse relay in the cabinet.
- The load current flows through the board: pluggable terminal block (P3) → PCB traces → relay contact. The board has milled isolation slots between the relay/terminal area and the low-voltage side, which suggests it is meant for mains. Still open: Waveshare's rated load for the outputs, and the rating of the terminal block.

## 2. Output channels

Each relay is a channel with its own settings (*Settings → Outputs*):

| Setting | |
|---|---|
| Name | e.g. "Hallway light", "Front door" |
| Mode | **Pulse:** pulls in for a set time (default 200 ms), for a cabinet input, an impulse relay or a door opener. **Switch:** keeps its state, for a lamp, a fan or a pump. |
| Show in fallback | yes / no |
| State after power loss | Switch mode only: off, on, last state |

- **Home Assistant:** each enabled channel appears through MQTT discovery (the existing `MqttController`), as a `button` in pulse mode and as a `switch` in switch mode. The panel publishes the switch state retained, also right after reconnecting, so changes made offline reach Home Assistant.
- **Local first:** when a dashboard card shows the panel's own channel, a tap switches the relay directly and reports to Home Assistant afterwards. No round trip through Home Assistant, the same feel online and offline.
- **Haptics:** a tap on a channel triggers a click when the panel has a haptic driver (own board, see 5).
- Switch mode and reboots: the relays drop out during every reset (see 1). A lamp held by the relay goes off for a moment on reboot and OTA. The docs recommend pulse mode with an impulse relay for lights.

## 3. Offline fallback

- **Enter:** Home Assistant has been unreachable for about 30 seconds (short drops do not count), or it is unreachable at boot.
- **Screen:** one or two large buttons for the channels marked *show in fallback*, the clock and a quiet status line ("Home Assistant not reachable, reconnecting…"). Without configured channels it is still a better offline screen than an error message.
- **Return:** the existing reconnect logic of the HA client keeps trying; once the connection is ready, the panel goes back to the dashboard on the same view.
- Pulse mode works the same offline: the cabinet keeps the state, the panel needs no feedback.

## 4. Door opener

The doorbell overlay's *hold to open* can pulse a channel directly instead of calling a Home Assistant service (option per doorbell). The door then opens even when Home Assistant is down.

## 5. Hardware capabilities

Relays and the fallback are only offered when the board has relays (the Waveshare 4B declares two). The capability model is planned in `HARDWARE_CAPABILITIES.md`.

## 6. Later: RS485

A cabinet connection over RS485 (Modbus or an own protocol): many channels over one cable, with state feedback. Same channel model as above, a second kind of output next to the relays.

## Dependencies

- Board capability description (per-board files, see `DISPLAY_SIZES.md`).
- GPIOs of `RELAY1` / `RELAY2`.
- Haptics only with own hardware.
