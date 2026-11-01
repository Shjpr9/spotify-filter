#ifndef SPOTIFY_FILTER_QUOTE_H
#define SPOTIFY_FILTER_QUOTE_H
#include <wchar.h>

/* Windows CRT: double backslashes before a quote or the closing quote. */
static wchar_t *quote_argument(wchar_t *out, const wchar_t *arg) {
    *out++ = L'"';
    for (;;) {
        size_t slashes = 0;
        while (*arg == L'\\') { ++slashes; ++arg; }
        size_t count = (*arg == L'"' || !*arg) ? slashes * 2 : slashes;
        while (count--) *out++ = L'\\';
        if (!*arg) break;
        if (*arg == L'"') *out++ = L'\\';
        *out++ = *arg++;
    }
    *out++ = L'"';
    *out = 0;
    return out;
}
#endif
