# Fixes for the lvgl submodule

`cmake/LvglPatches.cmake` applies these to `lvgl/` when the simulator or the
ESP32-P4 build is configured (only patches that are not in yet).

- `0001-ppa-cache-line-size-on-esp-idf-5.patch`: the PPA draw unit
  (`lv_draw_ppa_buf.c`) calls `esp_cache_get_line_size_by_addr()`, which
  ESP-IDF 5.5 does not have. The patch takes the line size from the address
  instead (PSRAM: L2 line, internal RAM: L1 line). Drop it on ESP-IDF 6.

Earlier: `0001-draw-sw-img-free-the-radius-mask.patch` (the radius mask that
`radius_only()` never released) is upstream since v9.6.0.
