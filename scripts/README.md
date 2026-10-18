Scripts for font glyph extraction and conversion

- extract_glyphs.py: Scans source files for icon macros (ICON_*, LV_SYMBOL_*) and accumulates unique UTF-8 glyphs for subset font generation.
- Usage: Run locally to print a glyph list string suitable for lv_font_conv's --symbols input.

This is optional scaffolding; current build still uses runtime tiny_ttf fonts.
