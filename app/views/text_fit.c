#include "text_fit.h"

#include <string.h>

/* Decodes one UTF-8 sequence; returns its length (1 for invalid bytes). */
static int utf8_next(const unsigned char *s, uint32_t *cp)
{
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { *cp = (uint32_t)(s[0] & 0x1F) << 6 | (s[1] & 0x3F); return 2; }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = (uint32_t)(s[0] & 0x0F) << 12 | (uint32_t)(s[1] & 0x3F) << 6 | (s[2] & 0x3F);
        return 3;
    }
    if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *cp = (uint32_t)(s[0] & 0x07) << 18 | (uint32_t)(s[1] & 0x3F) << 12 | (uint32_t)(s[2] & 0x3F) << 6 | (s[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static const char *substitute(uint32_t cp)
{
    switch (cp) {
    case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: return " ";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x2022: case 0x2023: case 0x2043: case 0x25CF: case 0x25E6: return "-";
    case 0x2039: return "<";
    case 0x203A: return ">";
    case 0x20AC: return "EUR";
    case 0x00B0: return " Grad";
    default: return NULL;
    }
}

void text_fit_font(const lv_font_t *font, const char *in, char *out, size_t out_size)
{
    if (!out || !out_size) return;
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)(in ? in : "");
    while (*s && o + 1 < out_size) {
        uint32_t cp;
        const int n = utf8_next(s, &cp);
        lv_font_glyph_dsc_t dsc;
        const bool have = cp == '\n' || cp == ' ' || (cp != 0xFFFD && lv_font_get_glyph_dsc(font, &dsc, cp, 0));
        if (have) {
            if (o + (size_t)n >= out_size) break;
            memcpy(out + o, s, (size_t)n);
            o += (size_t)n;
        } else {
            const char *sub = substitute(cp);
            if (sub) {
                const size_t len = strlen(sub);
                if (o + len >= out_size) break;
                memcpy(out + o, sub, len);
                o += len;
            }
            /* else: dropped (emoji, variation selectors, unknown scripts) */
        }
        s += n;
    }
    out[o] = '\0';
}
