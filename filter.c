#define _GNU_SOURCE
#include "cef_prefix.h"
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <delayimp.h>
#include <wchar.h>
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
#ifdef _WIN32
static INIT_ONCE symbols_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK resolve_symbols(PINIT_ONCE once, void *parameter, void **context) {
    (void)once;
    (void)parameter;
    (void)context;
    HMODULE cef = GetModuleHandleW(L"libcef.dll");
    if (!cef) return FALSE;
    FARPROC create = GetProcAddress(cef, "cef_urlrequest_create");
    FARPROC release = GetProcAddress(cef, "cef_string_userfree_utf16_free");
    if (!create || !release) return FALSE;
    _Static_assert(sizeof(create) == sizeof(real_create), "Pointer ABI mismatch");
    memcpy(&real_create, &create, sizeof(create));
    memcpy(&free_string, &release, sizeof(release));
    return TRUE;
}
#else
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
#ifdef _WIN32
    if (!InitOnceExecuteOnce(&symbols_once, resolve_symbols, NULL, NULL)) {
        errno = ENOSYS;
        return NULL;
    }
#else
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
static int patch_thunks(BYTE *image, DWORD names_rva, DWORD slots_rva,
                        const char *symbol, ULONG_PTR replacement) {
    if (!names_rva || !slots_rva) return 0;
    IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(image + names_rva);
    IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(image + slots_rva);
    for (; names->u1.AddressOfData; ++names, ++slots) {
        if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
        IMAGE_IMPORT_BY_NAME *entry = (IMAGE_IMPORT_BY_NAME *)(image + names->u1.AddressOfData);
        if (strcmp((char *)entry->Name, symbol)) continue;
        if (slots->u1.Function == replacement) return 1;
        DWORD previous, ignored;
        if (!VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &previous)) return -1;
        InterlockedExchangePointer((void *volatile *)&slots->u1.Function, (void *)replacement);
        if (!VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), previous, &ignored)) return -1;
        return 1;
    }
    return 0;
}

static int patch_import(HMODULE module, const char *dll, const char *symbol, ULONG_PTR replacement) {
    BYTE *image = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (rva) {
        IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(image + rva);
        for (; imports->Name; ++imports) {
            if (_stricmp((char *)(image + imports->Name), dll)) continue;
            int result = patch_thunks(image, imports->OriginalFirstThunk, imports->FirstThunk, symbol, replacement);
            if (result) return result;
        }
    }
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (rva) {
        const ImgDelayDescr *imports = (const ImgDelayDescr *)(image + rva);
        for (; imports->rvaDLLName; ++imports) {
            if (!(imports->grAttrs & dlattrRva) || _stricmp((char *)(image + imports->rvaDLLName), dll)) continue;
            int result = patch_thunks(image, imports->rvaINT, imports->rvaIAT, symbol, replacement);
            if (result) return result;
        }
    }
    return 0;
}

static HMODULE WINAPI filtered_load_library(const wchar_t *path, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW(path, file, flags);
    DWORD error = GetLastError();
    /* Inspection mappings must remain untouched and must not initialize code. */
    DWORD inspection = DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE |
                       LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (module && !(flags & inspection)) {
        wchar_t filename[32768];
        DWORD length = GetModuleFileNameW(module, filename, 32768);
        const wchar_t *name = length && length < 32768 ? wcsrchr(filename, L'\\') : NULL;
        if (name && !_wcsicmp(name + 1, L"Spotify.dll") &&
                patch_import(module, "libcef.dll", "cef_urlrequest_create", (ULONG_PTR)cef_urlrequest_create) != 1) {
            fputs("spotify-filter: Spotify.dll has no supported CEF request import; startup stopped.\n", stderr);
            FreeLibrary(module);
            SetLastError(ERROR_PROC_NOT_FOUND);
            return NULL;
        }
    }
    SetLastError(error);
    return module;
}

__declspec(dllexport) DWORD WINAPI initialize_filter(void *unused) {
    (void)unused;
    HMODULE main = GetModuleHandleW(NULL);
    int result = patch_import(main, "libcef.dll", "cef_urlrequest_create", (ULONG_PTR)cef_urlrequest_create);
    if (result) return result == 1 ? 1 : ERROR_ACCESS_DENIED;
    HMODULE core = GetModuleHandleW(L"Spotify.dll");
    if (core) {
        result = patch_import(core, "libcef.dll", "cef_urlrequest_create", (ULONG_PTR)cef_urlrequest_create);
        return result == 1 ? 1 : ERROR_PROC_NOT_FOUND;
    }
    // shortcut: Spotify.dll loads through the executable's LoadLibraryExW; revisit if startup changes.
    result = patch_import(main, "KERNEL32.dll", "LoadLibraryExW", (ULONG_PTR)filtered_load_library);
    return result == 1 ? 1 : ERROR_PROC_NOT_FOUND;
}
#endif
