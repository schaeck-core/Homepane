# Homepane design guide

This guide covers Homepane's design language and how to build a new widget that matches it. It is written for people **and for AI coding assistants**: give it to your assistant (or let it read this file) before asking it to build a widget. Every new UI element follows it.

- **Target:** ESP32-P4, LVGL 9, 4" IPS, 720 × 720 px, touch only, software rendering.
- **Mockups:** [`canvas/`](canvas/) holds HTML mockups of every screen (`*.dc.html`) with exact sizes, colors and spacing. Open one in a browser when you need a detail this guide does not cover.
- **In code:** design tokens are in [`app/ui_theme.h`](../../app/ui_theme.h), building blocks in [`app/ui_kit.h`](../../app/ui_kit.h), icons in [`app/lucide_icons.h`](../../app/lucide_icons.h).

## Design language

- **Calm, dark and warm.** True black background, slightly warm dark surfaces, off-white text. No blue: the accent is a warm sand tone.
- **One idea per tile.** A tile shows one thing large (an icon, a value, a picture) plus a name and a short status. Details go on a full-screen detail page.
- **Big touch targets.** Everything you can tap is at least 64 × 64 px, and whole rows and tiles are the tap area, not small icons inside them.
- **Line icons, no emoji.** [Lucide](https://lucide.dev/) icons with rounded strokes, in the text colors.
- **Calm motion.** Short slides and fades only where they explain something (a page opening). No decorative animation; it costs frames on the device.

## Colors

Always use the tokens from `app/ui_theme.h` (`lv_color_hex(UI_TEXT)`); never write hex values into a widget.

| Token | Hex | Use |
|---|---|---|
| `UI_BG` | `#000000` | screen background (true black) |
| `UI_SURFACE` | `#141413` | tiles, cards, buttons, rows |
| `UI_SURFACE2` | `#1F1E1C` | icon circles, dividers, slider and arc tracks |
| `UI_ACTIVE` | `#2B2A27` | selected state, slider fill, toggle off |
| `UI_TEXT` | `#ECE9E3` | main text, icons |
| `UI_TEXT2` | `#A39F97` | secondary text, labels, subtitles |
| `UI_TEXT3` | `#8A857D` | tertiary text, chevrons, "–" |
| `UI_ACCENT` | `#D6B98A` | sand accent: toggle on, progress, highlight |
| `UI_ACCENT_BG` | `#2E271C` | background behind sand (icon circle of an "on" tile) |
| `UI_HEAT` / `UI_HEAT_BG` | `#CF9271` / `#33231B` | heating |
| `UI_COOL` / `UI_COOL_BG` | `#94AFC4` / `#1C252C` | cooling |
| `UI_AUTO` | `#C9B79A` | climate auto mode |
| `UI_WARN` / `UI_WARN_BG` | `#D98A73` / `#2A1D18` | alarm, error, attention |
| `UI_OK` | `#8FA88A` | connected, OK (status dot) |
| `UI_BAR`, `UI_MUTED`, `UI_INACTIVE`, `UI_KEY_BG` | | chart bars, placeholder art, inactive dots, keypad special keys |

State colors come in pairs through `ui_look_t` (`app/ui_kit.h`): `UI_LOOK_OFF`, `UI_LOOK_ON` (sand on dark sand), `UI_LOOK_INFO`, `UI_LOOK_WARN`, `UI_LOOK_HEAT`, `UI_LOOK_COOL`. An icon circle (`ui_bubble`) takes a look, so "on" and "off" look the same everywhere.

## Typography

**Manrope** with tabular digits (numbers do not jump when they change). Use the font roles:

| Role | Font | Use |
|---|---|---|
| `UI_FONT_LABEL` | 18 / 500 | status, labels, secondary text |
| `UI_FONT_BODY` | 22 / 500 | running text |
| `UI_FONT_NAME` | 22 / 600 | names, row titles |
| `UI_FONT_TITLE` | 26 / 600 | page titles |
| `UI_FONT_VALUE` | 40 / 600 | measured values |
| `UI_FONT_HUGE` | 90 / 600 | main value (clock, target temperature) |

Other sizes exist in `app/fonts.h` (`font_manrope_600_56`, `_72`, digits-only `_96` and `_160`, `font_manrope_700_44`). A font is a pre-rendered bitmap. You cannot pick an arbitrary size, and a new size means changing `scripts/gen_fonts.py` and regenerating the fonts (it costs flash). Prefer an existing role.

Names are cut with "…" at the tile width (`ui_label_line`), never wrapped into the next element.

## Icons

Lucide line icons as LVGL fonts. Use `LI_*` names from `app/lucide_icons.h` with an icon font role:

| Role | Size | Use |
|---|---|---|
| `UI_ICON_S` | 18 | inline with labels |
| `UI_ICON_BUTTON` | 24 | buttons, settings rows |
| `UI_ICON_M` | 28 | header buttons, settings tiles |
| `UI_ICON_VALUE` | 36 | next to values |
| `UI_ICON_TILE` | 48 | icon in a tile's circle |
| `UI_ICON_L` | 56 | large single icons |

**Adding an icon:** add its Lucide name (as on lucide.dev, e.g. `washing-machine`) to [`scripts/lucide_icons.txt`](../../scripts/lucide_icons.txt), then run `scripts/gen_fonts.py --lv-font-conv <path>` (needs Node with `lv_font_conv` and Python with `fonttools`). It regenerates `app/lucide_icons.h` (`LI_WASHING_MACHINE`) and the `font_lucide_*` files; commit both. An icon that is used but not listed fails to compile, so nothing renders as an empty box.

## Sizes and spacing

| Token | Value | Use |
|---|---|---|
| `UI_EDGE` | 16 | screen edge |
| `UI_GAP` | 12 | grid gap (lists `UI_GAP_LIST` 8) |
| `UI_TILE_W` × `UI_TILE_H` | 221 × 308 | one dashboard tile |
| `UI_TILE_PAD` | 16 | padding inside a tile |
| `UI_R_TILE` | 24 | tile radius |
| `UI_R_CONTROL` | 20 | rows, header buttons, tile buttons |
| `UI_R_KEY` | 22 | keys in a keypad |
| `UI_TOUCH_MIN` | 64 | minimum touch target |
| `UI_BUTTON_H` | 64 | buttons inside tiles |
| `UI_BUBBLE` / `UI_BUBBLE_SMALL` | 112 / 96 | icon circle of a tile (without / with buttons) |
| `UI_HOLD_MS` | 480 | hold gesture |

Pills have radius = height / 2. Primary controls on detail pages are 104–112 px (for example ± on the thermostat).

## The dashboard grid

- Three columns of 221 × 308 tiles with a 12 px gap and a 16 px edge. Every view scrolls vertically, and views sit side by side (swipe).
- Each Home Assistant card becomes **one tile**. Its width follows the card's share of the HA section, rounded to whole columns: 1, 2 or 3 columns (442 or 663 px wide). **The height is always 308.**
- A widget therefore must not assume 221 px. Lay out with flex and percentages and let the tile fill its cell.
- Section titles and `heading` cards become title rows above the tiles.

## Building blocks (`app/ui_kit.h`)

Use these instead of styling raw LVGL objects. They give every widget the same radii, colors and press feedback.

| Function | What it makes |
|---|---|
| `ui_box(parent)` | a plain container without theme styles (transparent, no padding, not scrollable): the base of every layout box |
| `ui_tile(parent)` | a dashboard tile: surface, radius 24, padding 16, column layout, darkens while pressed |
| `ui_label`, `ui_label_line` | text in a font role and color; `_line` cuts with "…" |
| `ui_bubble`, `ui_bubble_set` | round icon circle with a look (off/on/info/warn/heat/cool) |
| `ui_button_row`, `ui_tile_button`, `ui_tile_button_set` | the row of 64 px buttons at the bottom of a tile |
| `ui_toggle`, `ui_toggle_large` | toggle 96 × 56 (sand when on), 112 × 64 in detail headers |
| `ui_detail_open(title, subtitle)` | full-screen detail page: header with back button top left, title, slot on the right, content column |
| `ui_segmented` | segmented control (64 high) |
| `ui_slider_row` | a whole row as a slider: filled share in the "active" surface, label left, value right |
| `ui_toast(text)` | short hint pill at the bottom (~2 s) |
| `ui_gestures(obj, tap, hold, double_tap, user)` | tap, hold (480 ms) and optional double tap |

Tile layouts in the mockups (`canvas/Widgets.dc.html`):
- **Centred tile** (switches, scenes, locks, …): icon circle in the middle, name and status below, optional button row at the bottom.
- **Value tile** (sensors): name on top, large value with unit, small icon, status line.
- **Picture tile** (cover, camera, media): a drawing or picture fills the middle, name above, buttons below.

## Gestures

Tiles take `tap_action`, `hold_action` and `double_tap_action` from the HA dashboard. Without them:

| Tile | Tap | Double tap | Hold |
|---|---|---|---|
| light | on / off | 100 % neutral | detail page |
| switch, outlet | on / off | – | consumption |
| cover | detail; its buttons act directly | – | detail |
| climate | detail | – | detail |
| media player | play / pause | next track | player |
| scene, script | run | – | info |
| lock, gate | hold confirms | – | detail |
| sensor, weather | history | – | history |

- A double tap only exists when configured. Otherwise a tap fires at once.
- Only sliders and arcs take drags; everywhere else a horizontal swipe changes the view.
- Dangerous actions (unlock, open the door, disarm) need a hold, never a single tap.

## Settings pages

- Header 64 high: back button top left (64 × 64, radius 20), title, close button right.
- Overview: tiles with an icon circle, title 22/600 and a status line 18 with an optional status dot.
- Rows 88 high, radius 20, surface, padding 20: icon and title left, value or control right. The whole row is the tap area.
- Section titles 18 in `UI_TEXT2`, without a card.

## Building a new widget

Most widgets show **one Home Assistant entity**. They derive from `TileWidget` ([`app/cpp/ui/tiles/TileWidget.h`](../../app/cpp/ui/tiles/TileWidget.h)), which already provides the tile, the gestures, the entity state and service calls. A small, complete example is [`CoverTile`](../../app/cpp/ui/tiles/CoverTile.cpp).

### 1. The class

```cpp
// app/cpp/ui/tiles/WasherTile.h
#include "TileWidget.h"

namespace app::ui {

// A washing machine: program and remaining time, tap opens the detail page.
class WasherTile final : public TileWidget {
public:
    using TileWidget::TileWidget;
    // Pure logic is static and host-tested (no LVGL).
    static std::string remaining_text(std::optional<double> minutes);

protected:
    void build(lv_obj_t* tile) override; // create the LVGL objects, once
    void update() override;              // show the current state, on every change
    void on_tap() override;

private:
    lv_obj_t* name_ = nullptr;
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* status_ = nullptr;
    std::string shown_status_;
};

} // namespace app::ui
```

### 2. `build()` and `update()`

```cpp
void WasherTile::build(lv_obj_t* tile)
{
    lv_obj_t* body = ui_box(tile);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, UI_GAP, 0);
    // LI_WASHING_MACHINE: "washing-machine" added to scripts/lucide_icons.txt (see Icons)
    bubble_ = ui_bubble(body, UI_BUBBLE, UI_ICON_TILE, LI_WASHING_MACHINE, UI_LOOK_OFF);
    name_ = ui_label_line(body, UI_FONT_NAME, UI_TEXT, "");
    status_ = ui_label_line(body, UI_FONT_LABEL, UI_TEXT2, "");
}

void WasherTile::update()
{
    if (!status_) return;
    lv_label_set_text(name_, name().c_str());
    const bool running = state() == "running";
    ui_bubble_set(bubble_, LI_WASHING_MACHINE, running ? UI_LOOK_ON : UI_LOOK_OFF, 0);
    const std::string text = !available() ? tr(TR_STATE_UNAVAILABLE)
                                          : remaining_text(attr_number("remaining_minutes"));
    if (text != shown_status_) { // set texts only when they change
        shown_status_ = text;
        lv_label_set_text(status_, text.c_str());
    }
}
```

`TileWidget` gives you:
- **Entity data:** `state()`, `attr("name")`, `attr_number("name")`, `loaded()`, `available()`, `seconds_in_state()`, `name()` (without the section title in front).
- **Service calls:** `call("turn_on")` calls the entity's own domain, `call_domain("button", "press")` another one, with extra JSON as the second argument. `expect_state("on")` shows the new state at once, until Home Assistant confirms it (override `on_command_failed()` to react to errors).
- **Formatting:** `format_number(value, decimals)` uses the UI language's decimal separator, `format_duration(seconds)` gives "12 min".

### 3. Register it

In [`app/cpp/dashboard/DefaultWidgetRegistration.cpp`](../../app/cpp/dashboard/DefaultWidgetRegistration.cpp):
- **By entity domain:** add a branch in `tile_for_entity()`. Every card type that shows one entity (`tile`, `entity`, `button`, …) then uses your tile.
- **For a card type of its own** (also custom cards such as `custom:washer-card`): `factory.register_type("custom:washer-card", [instance, &commands](const DashboardCard& card) { ... })`. `card.raw_json` holds the card's full configuration.

Widgets that are not about one entity (clock, charts, several entities) derive from `Widget` ([`app/cpp/ui/Widget.h`](../../app/cpp/ui/Widget.h)) directly. They list what they need in `requirements()` (entities, forecasts, statistics) and receive it in `on_data()`.

### 4. Add it to the builds

- **Simulator:** files in `app/cpp/ui/tiles/` and `app/cpp/ui/details/` are picked up automatically.
- **Firmware:** add the `.cpp` to the source list in [`platforms/esp32p4/main/CMakeLists.txt`](../../platforms/esp32p4/main/CMakeLists.txt) next to the other tiles.

### 5. Texts

No text is written into the code. Add keys to [`app/i18n/en.lang`](../../app/i18n/en.lang) (the reference) and the other languages, for example `washer.remaining = {0} left`. Then run `scripts/gen_i18n.py`. Use them as `tr(TR_WASHER_REMAINING)` or `trf(TR_WASHER_REMAINING, {"12 min"})`. See [`app/i18n/README.md`](../../app/i18n/README.md).

### 6. A detail page (optional)

For more than fits on the tile, open a full-screen page with `ui_detail_open(title, subtitle)` and fill its `content` column (rows, `ui_slider_row`, `ui_segmented`, large buttons). Larger pages are their own class in `app/cpp/ui/details/`. `CoverDetail` is an example: the tile keeps it in a `std::unique_ptr`, opens it in `on_tap()`, forwards state changes and drops it in `shutdown_ui()`.

### 7. Test it

- **Host tests:** put decisions (texts, colors, which icon) into static functions without LVGL, and test them in [`tests/host/test_tiles.cpp`](../../tests/host/test_tiles.cpp). Run them with `ctest` in the build folder.
- **Simulator with demo data:** add the entity to [`platforms/simulator/demo/states.json`](../../platforms/simulator/demo/states.json) (`"washer.demo": {"s": "running", "a": {"friendly_name": "Washer", "remaining_minutes": 42}}`) and a card to [`demo/lovelace.json`](../../platforms/simulator/demo/lovelace.json). Then take a screenshot without a device or Home Assistant:

  ```bash
  SDL_VIDEODRIVER=dummy HOMEPANE_DEMO=1 HOMEPANE_MONITORS=0 \
  HOMEPANE_SCRIPT="wait 3000; shot washer.bmp; click 120 400; wait 800; shot detail.bmp; quit" ./bin/main
  ```

  Compare the screenshot with the mockups in `canvas/`. Demo mode is the place to try buttons. A simulator connected to a real Home Assistant sends real service calls.

## Rules for the device

The panel renders in software, at 24-bit color, on a microcontroller. What is free on a PC costs frames here.

- **Few objects.** Every LVGL object costs memory and drawing time. A tile should need tens of objects, not hundreds. For many repeated elements, draw them yourself or use a picture.
- **Update only what changed.** Compare before `lv_label_set_text`, and don't rebuild children in `update()`.
- **Avoid expensive styles:** opacity on large areas, shadows, blur, and gradients with more than two stops (the firmware supports only two). Don't rotate or zoom pictures live.
- **Never block the UI thread.** All network and file access is asynchronous: service calls through `TileWidget`, pictures through [`ImageLoader`](../../app/cpp/media/ImageLoader.h) (downloads and decodes in the background and caches). LVGL may only be touched on the UI thread. Results from background work come back through the dispatcher (`app_cpp_post_to_ui`).
- **Timers:** only as often as the content changes (a clock: once a second, a countdown: once a second, nothing faster), and always deleted in `shutdown_ui()`.
- **Clean up.** `shutdown_ui()` deletes timers and detail pages. The tile itself is deleted by the dashboard.

## Checklist

- [ ] Colors, sizes and fonts only from `ui_theme.h` tokens, no hex values or magic numbers.
- [ ] Built from `ui_kit` building blocks; touch targets at least 64 × 64.
- [ ] Works at 1, 2 and 3 columns wide (flex/percentages, no fixed 221 px).
- [ ] Lucide icons only (`LI_*`); new icons added to `scripts/lucide_icons.txt` and fonts regenerated.
- [ ] Every text from `app/i18n/*.lang`, in at least English and German.
- [ ] Unavailable and still-loading states look sensible (`available()`, `loaded()`).
- [ ] Dangerous actions need a hold.
- [ ] Pure logic host-tested; screenshot taken in the demo simulator.
- [ ] Added to the firmware's `CMakeLists.txt`; firmware builds.
