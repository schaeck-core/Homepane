#include "i18n.h"

#include <string.h>

static const i18n_language_t *s_lang;

static const i18n_language_t *find(const char *code)
{
    for (int i = 0; i < i18n_language_count; ++i) {
        if (code && strcmp(i18n_languages[i].code, code) == 0) return &i18n_languages[i];
    }
    return NULL;
}

static const i18n_language_t *english(void)
{
    static const i18n_language_t *en;
    if (!en) en = find("en");
    return en;
}

void i18n_set_language(const char *code)
{
    const i18n_language_t *l = find(code);
    s_lang = l ? l : english();
}

const char *i18n_language(void)
{
    return s_lang ? s_lang->code : "en";
}

char i18n_decimal(void)
{
    const i18n_language_t *l = s_lang ? s_lang : english();
    return l && l->decimal[0] ? l->decimal[0] : '.';
}

const char *tr(tr_key_t key)
{
    if (key < 0 || key >= TR__COUNT) return "";
    const char *s = s_lang ? s_lang->strings[key] : NULL;
    if (!s && english()) s = english()->strings[key];
    return s ? s : "";
}

const char *tr_format(char *buf, size_t size, tr_key_t key, const char *const *args, int argc)
{
    if (!buf || size == 0) return "";
    const char *src = tr(key);
    size_t out = 0;
    while (*src && out + 1 < size) {
        if (src[0] == '{' && src[1] >= '0' && src[1] <= '9' && src[2] == '}') {
            const int n = src[1] - '0';
            const char *arg = n < argc && args[n] ? args[n] : "";
            while (*arg && out + 1 < size) buf[out++] = *arg++;
            src += 3;
            continue;
        }
        buf[out++] = *src++;
    }
    buf[out] = '\0';
    return buf;
}
