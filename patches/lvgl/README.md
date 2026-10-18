# Fixes for the lvgl submodule

`cmake/LvglPatches.cmake` applies these to `lvgl/` when the simulator or the
ESP32-P4 build is configured (only patches that are not in yet).

- `0001-draw-sw-img-free-the-radius-mask.patch`: `radius_only()` in
  `lv_draw_sw_img.c` never released its radius mask. Every image with a
  corner radius, including the layer of a `clip_corner` object, kept one
  entry of the circle cache "in use" for good. Once the cache was full,
  every rounded shape recalculated its mask on every draw. On the panel the
  main page went from ~70 ms to ~175 ms per full redraw over time (for
  example after the screensaver's progress segments had run). Drop this
  patch once the submodule has the fix.
