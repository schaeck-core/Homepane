#!/usr/bin/env python3
"""Derive the static Manrope fonts gen_fonts.py renders from the variable
font in resources/fonts/Manrope (Google Fonts, OFL).

- one instance per weight the design guide uses (500 text, 600 titles/values,
  700 the screensaver's event title)
- tabular figures frozen into the character map: lv_font_conv applies no
  OpenType features, and the guide wants tabular digits (a clock or a
  counting value must not shift sideways)

Usage: scripts/make_manrope.py   (needs fontTools: pip install fonttools)
Then run scripts/gen_fonts.py and commit resources/fonts/Manrope/static/.
"""
import os

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, 'resources', 'fonts', 'Manrope')
WEIGHTS = {500: 'Medium', 600: 'SemiBold', 700: 'Bold'}


def tabular_map(font):
    """glyph name -> its 'tnum' substitute (single substitutions only)."""
    gsub = font['GSUB'].table
    out = {}
    for rec in gsub.FeatureList.FeatureRecord:
        if rec.FeatureTag != 'tnum':
            continue
        for index in rec.Feature.LookupListIndex:
            lookup = gsub.LookupList.Lookup[index]
            for sub in lookup.SubTable:
                if lookup.LookupType == 7:  # extension
                    sub = sub.ExtSubTable
                if hasattr(sub, 'mapping'):
                    out.update(sub.mapping)
    return out


def main():
    os.makedirs(os.path.join(DIR, 'static'), exist_ok=True)
    for weight, style in WEIGHTS.items():
        font = TTFont(os.path.join(DIR, 'Manrope-VariableFont_wght.ttf'))
        font = instantiateVariableFont(font, {'wght': weight})
        tnum = tabular_map(font)
        frozen = 0
        for table in font['cmap'].tables:
            for cp, name in list(table.cmap.items()):
                if name in tnum:
                    table.cmap[cp] = tnum[name]
                    frozen += 1
        path = os.path.join(DIR, 'static', f'Manrope-{style}.ttf')
        font.save(path)
        print(f'{os.path.relpath(path, ROOT)}: wght {weight}, {frozen} cmap entries tabular')


if __name__ == '__main__':
    main()
