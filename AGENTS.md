# Notes for AI coding assistants

Homepane is a Home Assistant touch panel: C/C++ with LVGL 9, running on an ESP32-P4 (720 × 720 touch screen) and in a desktop simulator. Read [README.md](README.md) for the overview and build steps.

## Before you change the UI

Read [docs/design/DESIGN_GUIDE.md](docs/design/DESIGN_GUIDE.md). It has the design language, the tokens, the building blocks and a step-by-step guide for new dashboard widgets. In short:

- Colors, sizes and fonts come from `app/ui_theme.h`; building blocks from `app/ui_kit.h`. No hex values, no other fonts.
- Icons are Lucide (`LI_*` in `app/lucide_icons.h`). New icons go into `scripts/lucide_icons.txt`, then run `scripts/gen_fonts.py`.
- Every visible text comes from `app/i18n/*.lang` (`tr()` / `trf()`), then run `scripts/gen_i18n.py`.
- Dashboard widgets derive from `TileWidget` (`app/cpp/ui/tiles/`) and are registered in `app/cpp/dashboard/DefaultWidgetRegistration.cpp`.

## Code

- `app/` is platform-independent; `platforms/simulator` and `platforms/esp32p4` are the two platforms. New `.cpp` files must also be added to `platforms/esp32p4/main/CMakeLists.txt` (the simulator picks up tiles and detail pages by itself).
- LVGL is only touched on the UI thread. Network and file work is asynchronous and returns through `app_cpp_post_to_ui()`.
- The device renders in software: keep LVGL object counts low, update labels only when their text changes, avoid shadows, opacity and gradients with more than two stops.
- Write code like the code around it: its naming, comment density and idioms.

## Check your work

- Host tests: `cmake --build build && cd build && ctest --output-on-failure`.
- Screenshots without a device or Home Assistant:
  `SDL_VIDEODRIVER=dummy HOMEPANE_DEMO=1 HOMEPANE_MONITORS=0 HOMEPANE_SCRIPT="wait 3000; shot out.bmp; quit" ./bin/main`.
  Demo data is in `platforms/simulator/demo/`.
- A simulator connected to a real Home Assistant sends real commands. Try controls in demo mode only.
- Firmware: `cd platforms/esp32p4 && idf.py build` (ESP-IDF 5.5.1).
- Never flash the ESP32-C6 co-processor without a way to recover it over USB-UART.
