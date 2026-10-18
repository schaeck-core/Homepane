# UI languages

One file per language: `<code>.lang` (`de.lang`, `en.lang`, ...). English is
the reference: every key the code uses is defined there; other languages fall
back to it for keys they do not have.

```
@name = Deutsch          # shown in Settings > General > Language
@decimal = ,             # decimal separator for numbers
@glyphs = 0x400-0x4FF    # optional: extra characters for the text fonts
state.on = An
climate.room = Raum {0}  # {0}, {1}: values from the code, any order
```

Adding a language or text:

1. Add/edit the `.lang` file (copy `en.lang` for a new language).
2. `scripts/gen_i18n.py` regenerates `app/i18n_keys.h` (the `TR_*` keys) and
   `app/generated/i18n_strings.c`; it rejects unknown keys and placeholder
   mismatches. The ctest `i18n_tables_check` fails when they are stale.
3. `scripts/gen_fonts.py` puts every character of all language files and their
   `@glyphs` ranges into the text fonts. Use `@glyphs` for the script a
   language needs for Home Assistant names (e.g. Cyrillic `0x400-0x4FF`). The
   text font (Roboto) covers Latin, Greek and Cyrillic; scripts it lacks (CJK,
   Arabic, ...) need another font first.

In code: `tr(TR_STATE_ON)`, `tr_format(buf, size, TR_CLIMATE_ROOM, args, n)`
(C) or `trf(TR_CLIMATE_ROOM, {"21°"})` (C++). Icons do not depend on the
language and stay in the icon fonts.
