/* UI texts in the user's language (app/i18n/<code>.lang, one file per
 * language; scripts/gen_i18n.py turns them into tables). Keys are the
 * TR_* enum of i18n_keys.h, so a missing key is a compile error.
 *
 *   tr(TR_STATE_ON)                          -> "An"
 *   tr_format(buf, sizeof buf, TR_CLIMATE_ROOM, (const char *[]){"21°"}, 1) -> "Raum 21°"
 *
 * Placeholders {0}, {1}, ... take the given strings in that order; each
 * language places them as its grammar needs. */
#ifndef I18N_H
#define I18N_H

#include <stddef.h>

#include "i18n_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *code;           /* "de" */
    const char *name;           /* "Deutsch" */
    const char *decimal;        /* "," */
    const char *const *strings; /* TR__COUNT entries, NULL = fall back to English */
} i18n_language_t;

extern const i18n_language_t i18n_languages[];
extern const int i18n_language_count;

/* Selects the language by code; unknown codes keep English. */
void i18n_set_language(const char *code);
const char *i18n_language(void);
/* The decimal separator of the language ("," or "."). */
char i18n_decimal(void);

const char *tr(tr_key_t key);
/* tr(key) with {n} replaced by args[n]; returns buf. */
const char *tr_format(char *buf, size_t size, tr_key_t key, const char *const *args, int argc);

#ifdef __cplusplus
}

#include <initializer_list>
#include <string>

/* C++: trf(TR_CLIMATE_ROOM, {"21°"}) */
inline std::string trf(tr_key_t key, std::initializer_list<std::string> args)
{
    const char *ptrs[8];
    int n = 0;
    for (const auto &a : args) {
        if (n < 8) ptrs[n++] = a.c_str();
    }
    char buf[256];
    return tr_format(buf, sizeof(buf), key, ptrs, n);
}
#endif

#endif /* I18N_H */
