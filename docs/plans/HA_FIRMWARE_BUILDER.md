# Plan: Home Assistant integration with firmware builder

Status: idea, not started. Noted 2026-10-16.

A Home Assistant integration or add-on for Homepane that delivers firmware to the panels and, optionally, builds a custom firmware with AI-generated widgets. Goal: users without programming experience can add their own widgets and keep over-the-air updates.

## 1. Firmware source through Home Assistant

- The integration / add-on serves the OTA manifest and the firmware to the panels. Panels without direct internet access still get updates, through Home Assistant.
- **Default:** it links the official firmware (the signed release manifest on GitHub Pages). Nothing to configure.
- The panel's update source becomes configurable (today it is fixed at build time, `app/cpp/update/UpdateSource.h`).

## 2. Custom firmware with its own key

- The user can generate a custom firmware instead of the official one. It is signed with a key of its own (a secret kept in Home Assistant), not the project's key.
- The panel is switched to the custom firmware once (USB, or an OTA step that changes the trusted key with a manifest signed by the current key). From then on, OTA updates come from the custom build instead of the official one.
- Official releases are merged into the custom build, so custom firmware keeps getting fixes: the add-on rebuilds with the user's widgets on top of each new release.
- Open questions: where the build runs (the add-on itself needs ESP-IDF and a few GB; a build service is the alternative), how long a build takes on typical HA hardware, and how the C6 firmware is handled (probably always the official image).

## 3. AI widget builder

- The user enters an OpenAI or Anthropic (Claude) API key in the integration.
- A small chat in Home Assistant: the user describes a widget, the AI writes it following `docs/design/DESIGN_GUIDE.md` and `AGENTS.md` (`TileWidget`, `ui_kit`, i18n, registration).
- The add-on builds the firmware with the new widget, runs the host tests, shows screenshots from the simulator (demo data) for review, and offers the build as an OTA update.
- No programming experience needed; building and signing happen in the background.
- Needed in the firmware: a clean extension point for custom widgets (own folder, registered automatically) so official updates and custom widgets merge without conflicts.

## 4. AI dashboard designer

- The same chat can design and create dashboards for the panel: Sections view, `max_columns`, card sizes and rows chosen for the panel's grid (see "Setting up dashboards" in the README), from the user's areas and entities.
- The result is a starting point the user fine-tunes in Home Assistant.

### Design templates

The user first picks a base design; it steers the AI's prompt. Each template is shown in the UI with a simple picture (a mock-up of the panel), so the user sees roughly what the dashboard will look like before anything is generated.

A template fixes the rough shape, the AI fills it with the user's own entities:
- **Header:** clock and weather (merged card, clock only, weather with forecast, none).
- **Tile layout:** columns and rows per view, big tiles vs. compact tiles, how many per screen.
- **What goes on which view:** the first view holds what is used most, later views the rest.

First ideas:
- **Controls first:** view 1 lights, blinds and heating of the room; view 2 cameras, energy (solar, consumption of outlets), sensors.
- **Room per view:** an overview (clock, weather, alarms, open windows), then one view per area.
- **Status board:** mostly read-only figures (solar yield, consumption, temperatures, presence) with few controls; for hallways.
- **Minimal:** clock and weather large, a handful of scenes / main switches.

Templates are data, not code: a short description for the prompt, layout rules (columns, rows, which card kinds where), the preview picture. New templates need no firmware update.

## Dependencies

- Configurable update source and trusted key on the panel.
- Extension point for custom widgets.
- A way to render previews (the simulator with demo data, `HOMEPANE_SCRIPT` screenshots).
