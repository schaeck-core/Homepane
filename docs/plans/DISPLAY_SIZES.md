# Plan: other display sizes and orientation

Status: findings, not planned yet. Noted 2026-10-16.

Goal: run on other ESP32-P4 panels (e.g. 5" 720 × 1280, 4" 480 × 800) and let the user choose portrait or landscape. A dashboard can choose it itself, so a dashboard generated for the panel (see [HA_FIRMWARE_BUILDER.md](HA_FIRMWARE_BUILDER.md)) loads correctly without touching the settings.

## What works today

- The tile grid follows the screen: columns from the view's `max_columns`, tile height from the rows on screen (setting *Tile rows*, or `homepane.rows` in the dashboard).
- Try other sizes in the simulator: `HOMEPANE_RESOLUTION=720x1280 ./bin/main` (also `1280x720`, `480x800`, `800x480`).
- **720 × 1280 and 1280 × 720:** usable. Portrait needs 3–4 rows (or *Like columns*), otherwise the tiles are very tall. Fonts and widget insides do not scale, but fit.
- **480 × 800 and 800 × 480:** broken. Font sizes, the header and the widgets' insides (weather, thermostat, light, cover) are built for 720 px and overlap or get cut off.

## Dashboard flags

Home Assistant keeps unknown keys in a storage dashboard's config (tested with `lovelace/config/save`). Homepane reads a `homepane` object on the dashboard and on each view:

```json
{
  "homepane": {"rows": "3", "orientation": "portrait"},
  "views": [{"type": "sections", "max_columns": 2, "homepane": {"rows": "cols"}, "sections": []}]
}
```

- `rows`: implemented. Same values as the setting (`"1"`–`"6"`, `"cols"`, `"cols-1"`); the view's wins over the dashboard's, which wins over the panel's setting.
- `orientation` (`"portrait"` / `"landscape"`): only a proposal, not read yet.

## Open for planning

1. **Rotation** (assessed 2026-10-16, not measured on hardware):
   - *In the panel driver:* not possible. The ST7703 is a MIPI-DSI panel in video mode; its driver rejects `swap_xy` and supports only `mirror_y`. Other DSI panels (the 5" 720 × 1280 ones) work the same way.
   - *Native orientation first:* 720 × 1280 and 480 × 800 panels are portrait by nature. Portrait on them needs no rotation at all, only landscape does.
   - *Software rotation* (`lv_display_set_rotation`, esp_lvgl_port `sw_rotate`): every flushed area is transposed by the CPU (`lv_draw_sw_rotate`) into an extra buffer (720 × 720 × 3 B ≈ 1.5 MB in PSRAM, 720 × 1280 ≈ 2.7 MB). Small updates cost little; full-screen work (swipes, scrolling, opening pages) costs the most, and tear-free mode cannot be combined with it.
   - *PPA rotation* (recommended): esp_lvgl_port 2.7 lets the P4's Pixel Processing Accelerator rotate instead of the CPU (`CONFIG_LVGL_PORT_ENABLE_PPA`, with `sw_rotate`). Same buffer, but the CPU stays free for drawing.
   - *Touch:* LVGL 9 rotates pointer input with the display; the GT911 setup stays as it is.
   - *When to apply:* rotation can change at runtime (then rebuild the UI). To allocate the rotation buffer only on rotated panels, read the config before `bsp_display_start()` (mount LittleFS and `loadConfig()` first) and restart on a change. The first frame is drawn before the backlight comes on, so nothing unrotated is visible.
   - To measure: frame time of a view swipe and of scrolling with PPA rotation vs. none.
   - Setting in *Settings > General*, overridden by `homepane.orientation`.
2. **Scaling:** fonts and sizes from `ui_theme.h` relative to the screen (e.g. by the short side), more font sizes in `scripts/gen_fonts.py` (flash budget), widgets that lay out by their tile size instead of fixed pixels.
3. **Header and pages** (settings, detail pages, music player, screensaver) still assume 720 px in places (`720` literals in `app/views` and `app/cpp/ui`).
4. **Board support:** display/touch drivers per board (see the board abstraction in the README's plans).
