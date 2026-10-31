#define _GNU_SOURCE
#include "cef_prefix.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void *(*real_create)(struct cef_request *, void *, void *);
static void (*free_string)(struct cef_string *);
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;

static void resolve_symbols(void) {
    real_create = dlsym(RTLD_NEXT, "cef_urlrequest_create");
    free_string = dlsym(RTLD_NEXT, "cef_string_userfree_utf16_free");
    if (!real_create || !free_string)
        fputs("spotify-filter: CEF symbols unavailable; filtering disabled\n", stderr);
}

static int is_ad_url(const char *url, const char **path) {
    if (strncasecmp(url, "https://", 8)) return 0;
    const char *host = url + 8;
    *path = strchr(host, '/');
    if (!*path) return 0;
    size_t length = (size_t)(*path - host);
    if (length >= 4 && !strncmp(host + length - 4, ":443", 4)) length -= 4;
    const char *gateway = "spclient.wg.spotify.com";
    const char *suffix = "-spclient.spotify.com";
    size_t suffix_length = strlen(suffix);
    int allowed_host = length == strlen(gateway) && !strncasecmp(host, gateway, length);
    if (!allowed_host && length > suffix_length &&
        !strncasecmp(host + length - suffix_length, suffix, suffix_length)) {
        allowed_host = 1;
        for (size_t i = 0; i < length - suffix_length; ++i) {
            unsigned char c = (unsigned char)host[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-')) allowed_host = 0;
        }
    }
    return allowed_host && (!strncmp(*path, "/ads/", 5) || !strncmp(*path, "/ad-logic/", 10));
}

void *cef_urlrequest_create(struct cef_request *request, void *client, void *context) {
    pthread_once(&symbols_once, resolve_symbols);
    if (!real_create) { errno = ENOSYS; return NULL; }
    if (free_string && request && request->size >= sizeof(*request) && request->get_url) {
        struct cef_string *value = request->get_url(request);
        if (value) {
            char url[8193];
            size_t i = 0;
            // shortcut: unusual/oversized URLs pass through; extend only with observed examples.
            if (value->str && value->length < sizeof(url)) {
                for (; i < value->length; ++i) {
                    if (!value->str[i] || value->str[i] > 127) break;
                    url[i] = (char)value->str[i];
                }
            }
            int complete = value->str && value->length < sizeof(url) && i == value->length;
            free_string(value);
            if (complete) {
                url[i] = '\0';
                const char *path = NULL;
                if (is_ad_url(url, &path)) {
                    fprintf(stderr, "spotify-filter: blocked %.*s\n", (int)strcspn(path, "?#"), path);
                    return NULL;
                }
            }
        }
    }
    return real_create(request, client, context);
}
