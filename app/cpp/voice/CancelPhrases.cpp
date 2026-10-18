#include "SpeechEndpointer.h"

#include <cctype>
#include <string>

namespace app::voice {

bool is_cancel_phrase(const char* text)
{
    if (!text) return false;
    // Lower-case ASCII, drop punctuation, collapse spaces (Whisper writes
    // "Stopp!", " Abbrechen.", "Vergiss es.").
    std::string s;
    for (const char* p = text; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (std::isalpha(c) || c >= 0x80) s += static_cast<char>(std::tolower(c));
        else if (std::isspace(c) && !s.empty() && s.back() != ' ') s += ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();

    static const char* const kPhrases[] = {
        "stop", "stopp", "halt", "abbrechen", "abbruch", "cancel", "egal", "vergiss es", "vergiss das",
        "nichts", "schon gut", "nein danke", "never mind", "nevermind", "ruhe",
    };
    for (const char* phrase : kPhrases) {
        if (s == phrase) return true;
    }
    return false;
}

} // namespace app::voice
