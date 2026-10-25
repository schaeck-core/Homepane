#!/usr/bin/env python3
"""Regenerate the bitmap fonts in app/generated/ in one pass.

1. The Lucide line icons listed in scripts/lucide_icons.txt (font_lucide_*,
   plus app/lucide_icons.h with their LI_* names).
2. The text fonts (Manrope), with every character of the UI languages
   (app/i18n, see language_ranges()).

Usage: scripts/gen_fonts.py [--lv-font-conv PATH]
       (needs Node + lv_font_conv, and fontTools: pip install fonttools)
Then commit the changed app/generated/*.c. An icon the code uses but the
list lacks fails to compile (no LI_* name).
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'app', 'generated')
# static instances with tabular digits, derived by scripts/make_manrope.py
MANROPE = os.path.join(ROOT, 'resources', 'fonts', 'Manrope', 'static')
# Glyphs Manrope lacks (rare Latin Extended-A letters, some dashes, primes)
# come from Roboto in the same font file.
ROBOTO = os.path.join(ROOT, 'resources', 'fonts', 'Roboto', 'static')
LUCIDE = os.path.join(ROOT, 'resources', 'fonts', 'lucide')
LUCIDE_SIZES = (18, 24, 28, 36, 48, 56)
# Lucide sits in the BMP private use area; the fonts move it to plane 15
# (U+F0000 + offset), clear of anything else in the private use area.
LUCIDE_PLANE = 0xF0000

# Text: Basic Latin, Latin-1, Latin Extended-A (German and other European
# languages; HA entity names).
LATIN = '0x20-0x7E,0xA0-0xFF,0x100-0x17F'
# Typography LLM answers and HA texts use: dashes, quotes, ellipsis, bullet,
# per mille, primes, angle quotes, euro, trademark, minus, approx, not-equal,
# less/greater-equal (no arrows; lv_font_conv refuses ranges a
# font has no glyph in). Only for the reading sizes (<= 40 px): at 56-90 px
# every glyph costs several KB, and those sizes show clock/numbers.
TYPOGRAPHY = ('0x2010-0x2027,0x2030,0x2032,0x2033,0x2039,0x203A,0x20AC,0x2122,'
              '0x2212,0x2248,0x2260,0x2264,0x2265')


def language_ranges():
    """Code points the UI languages need beyond LATIN/TYPOGRAPHY: every
    character in app/i18n/*.lang texts plus each file's @glyphs ranges (for
    a script the language's Home Assistant names use, e.g. Cyrillic
    0x400-0x4FF). The text font (Manrope) must contain them."""
    import glob
    covered = set()
    for part in (LATIN + ',' + TYPOGRAPHY).split(','):
        lo, _, hi = part.partition('-')
        covered.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    extra, ranges = set(), []
    for path in sorted(glob.glob(os.path.join(ROOT, 'app', 'i18n', '*.lang'))):
        with open(path, encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#') or '=' not in line:
                    continue
                key, value = (x.strip() for x in line.split('=', 1))
                if key == '@glyphs':
                    ranges += [r.strip() for r in value.split(',') if r.strip()]
                elif not key.startswith('@') or key == '@name':  # the name shows in the language list
                    extra.update(ord(ch) for ch in value if ord(ch) not in covered and ord(ch) >= 0x20)
    return ','.join(ranges + [f'0x{cp:X}' for cp in sorted(extra)])


def convert(tool, font, size, ranges, name, fallback=None):
    """Render `ranges` from `font`; with a fallback font, code points `font`
    lacks are taken from it (lv_font_conv refuses ranges with no glyph)."""
    out = os.path.join(GEN, (name[:-2] if name.endswith('_c') else name) + '.c')
    # paths relative to ROOT (the working directory): lv_font_conv writes its
    # command line into the generated file
    font = os.path.relpath(font, ROOT)
    fallback = fallback and os.path.relpath(fallback, ROOT)
    out = os.path.relpath(out, ROOT)
    sources = ['--font', font, '-r', ranges]
    if fallback:
        own, missing = split_ranges(font, ranges)
        sources = ['--font', font, '-r', own]
        if missing:
            have_fb, lost = split_ranges(fallback, missing)
            if have_fb:
                sources += ['--font', fallback, '-r', have_fb]
            if lost:
                print(f'    no glyph in either font: {lost}')
    cmd = [tool, *sources, '--size', str(size), '--format', 'lvgl', '--bpp', '4', '--no-compress',
           '--no-prefilter', '--no-kerning', '--output', out, '--force-fast-kern-format',
           '--lv-font-name', name, '--lv-include', 'lvgl/lvgl.h']
    print(f'  {os.path.basename(out)} ({size}px)')
    subprocess.run(cmd, check=True, cwd=ROOT)


def split_ranges(font, ranges):
    """(code points in `font`, code points not in it) of an lv_font_conv
    range list, each again as a range list."""
    from fontTools.ttLib import TTFont
    cmap = TTFont(os.path.join(ROOT, font)).getBestCmap()
    cps = set()
    for part in ranges.split(','):
        lo, _, hi = part.partition('-')
        cps.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    return (to_ranges(cp for cp in cps if cp in cmap), to_ranges(cp for cp in cps if cp not in cmap))


def to_ranges(cps):
    out, run = [], None
    for cp in sorted(cps):
        if run and cp == run[1] + 1:
            run[1] = cp
        else:
            if run:
                out.append(run)
            run = [cp, cp]
    if run:
        out.append(run)
    return ','.join(f'0x{a:X}' if a == b else f'0x{a:X}-0x{b:X}' for a, b in out)


def lucide(tool):
    import json
    with open(os.path.join(LUCIDE, 'codepoints.json'), encoding='utf-8') as f:
        codepoints = json.load(f)
    names = []
    with open(os.path.join(ROOT, 'scripts', 'lucide_icons.txt'), encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and line not in names:
                names.append(line)
    unknown = [n for n in names if n not in codepoints]
    if unknown:
        sys.exit('unknown Lucide icons: ' + ', '.join(unknown))
    mapped = {n: LUCIDE_PLANE + codepoints[n] - 0xE000 for n in names}
    ranges = ','.join(f'0x{codepoints[n]:X}=>0x{mapped[n]:X}' for n in names)
    print('Lucide (line icons):')
    for size in LUCIDE_SIZES:
        convert(tool, os.path.join(LUCIDE, 'lucide.ttf'), size, ranges, f'font_lucide_{size}_c')
    header = os.path.join(ROOT, 'app', 'lucide_icons.h')
    with open(header, 'w', encoding='utf-8') as f:
        f.write('/* Generated by scripts/gen_fonts.py from scripts/lucide_icons.txt: do not edit.\n'
                ' * Lucide line icons (ISC, resources/fonts/lucide) for font_lucide_* fonts. */\n'
                '#ifndef LUCIDE_ICONS_H\n#define LUCIDE_ICONS_H\n\n')
        for n in sorted(names):
            macro = 'LI_' + n.upper().replace('-', '_')
            f.write(f'#define {macro} "\\U{mapped[n]:08X}" /* {n} */\n')
        f.write('\n#endif /* LUCIDE_ICONS_H */\n')
    print(f'  {os.path.relpath(header, ROOT)} ({len(names)} icons)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lv-font-conv', default=shutil.which('lv_font_conv') or 'lv_font_conv')
    args = ap.parse_args()

    lucide(args.lv_font_conv)

    lang = language_ranges()
    text = LATIN + ',' + TYPOGRAPHY + (',' + lang if lang else '')
    print('Manrope (text):' + (f' + languages ({lang})' if lang else ''))
    # design guide: 500 for text, 600 for names, titles and values
    medium = os.path.join(MANROPE, 'Manrope-Medium.ttf')
    semibold = os.path.join(MANROPE, 'Manrope-SemiBold.ttf')
    roboto_medium = os.path.join(ROBOTO, 'Roboto-Medium.ttf')
    for size in (18, 22):
        convert(args.lv_font_conv, medium, size, text, f'font_manrope_500_{size}_c', roboto_medium)
    for size in (22, 26, 32, 40):
        convert(args.lv_font_conv, semibold, size, text, f'font_manrope_600_{size}_c', roboto_medium)
    for size in (56, 72, 90):
        convert(args.lv_font_conv, semibold, size, LATIN, f'font_manrope_600_{size}_c', roboto_medium)
    # The dashboard clock: digits and colon only (a few KB at 96 px).
    convert(args.lv_font_conv, semibold, 96, '0x30-0x3A', 'font_manrope_600_96_c')
    # The screensaver's big clock over its own picture (160 px, digits and colon)
    convert(args.lv_font_conv, semibold, 160, '0x30-0x3A', 'font_manrope_600_160_c')
    # The screensaver's event title (700, 44 px)
    bold = os.path.join(MANROPE, 'Manrope-Bold.ttf')
    convert(args.lv_font_conv, bold, 44, text, 'font_manrope_700_44_c', roboto_medium)
    # The wordmark on the start screen (700, 56 px): the letters of "homepane" only
    convert(args.lv_font_conv, bold, 56, '0x61,0x65,0x68,0x6D,0x6E,0x6F,0x70', 'font_manrope_700_56_c')

if __name__ == '__main__':
    main()
