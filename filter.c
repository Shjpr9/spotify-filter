#define _GNU_SOURCE
#include "cef_prefix.h"
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#define strncasecmp _strnicmp
#else
#include <dlfcn.h>
#include <pthread.h>
#endif
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static void *(*real_create)(struct cef_request *, void *, void *);
static void (*free_string)(struct cef_string *);
#ifndef _WIN32
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;

static void resolve_symbols(void) {
    real_create = dlsym(RTLD_NEXT, "cef_urlrequest_create");
    free_string = dlsym(RTLD_NEXT, "cef_string_userfree_utf16_free");
    if (!real_create || !free_string)
        fputs("spotify-filter: CEF symbols unavailable; filtering disabled\n", stderr);
}

#endif

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
#ifndef _WIN32
    pthread_once(&symbols_once, resolve_symbols);
#endif
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

#ifdef _WIN32
/* Patch only Spotify's CEF import; no executable instructions or disk files change. */
__declspec(dllexport) DWORD WINAPI initialize_filter(void *unused) {
    (void)unused;
    HMODULE cef = GetModuleHandleW(L"libcef.dll");
    if (!cef) return 0;
    FARPROC create = GetProcAddress(cef, "cef_urlrequest_create");
    FARPROC release = GetProcAddress(cef, "cef_string_userfree_utf16_free");
    if (!create || !release) return 0;
    /* memcpy avoids casts between incompatible Windows function pointer types. */
    _Static_assert(sizeof(create) == sizeof(real_create), "Pointer ABI mismatch");
    memcpy(&real_create, &create, sizeof(create));
    memcpy(&free_string, &release, sizeof(release));
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return 0;
    IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(image + rva);
    for (; imports->Name; ++imports) {
        if (_stricmp((char *)(image + imports->Name), "libcef.dll")) continue;
        IMAGE_THUNK_DATA64 *slot = (IMAGE_THUNK_DATA64 *)(image + imports->FirstThunk);
        for (; slot->u1.Function; ++slot) {
            if (slot->u1.Function != (ULONG_PTR)create) continue;
            DWORD previous, ignored;
            if (!VirtualProtect(&slot->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &previous)) return 0;
            slot->u1.Function = (ULONG_PTR)cef_urlrequest_create;
            if (!VirtualProtect(&slot->u1.Function, sizeof(ULONG_PTR), previous, &ignored)) return 0;
            return 1;
        }
    }
    // shortcut: direct main-executable CEF imports only; revisit if Spotify switches to dynamic lookup.
    return 0;
}
#endif
