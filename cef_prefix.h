#ifndef SPOTIFY_FILTER_CEF_PREFIX_H
#define SPOTIFY_FILTER_CEF_PREFIX_H
#include <stddef.h>
#include <stdint.h>

/* Stable prefix of CEF's public reference-counted request API on Linux x86_64. */
struct cef_string {
    uint16_t *str;
    size_t length;
    void (*dtor)(uint16_t *);
};
struct cef_request {
    size_t size;
    void (*base_methods[4])(void);
    int (*is_read_only)(void *);
    struct cef_string *(*get_url)(struct cef_request *);
};
_Static_assert(sizeof(void *) == 8, "Requires 64-bit CEF");
_Static_assert(offsetof(struct cef_request, get_url) == 48, "CEF ABI mismatch");
#endif
