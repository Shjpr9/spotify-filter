#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "quote.h"

static int spotify_running(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W item = {.dwSize = sizeof(item)};
    int found = 0;
    if (!Process32FirstW(snapshot, &item)) found = -1;
    else do {
        if (!_wcsicmp(item.szExeFile, L"Spotify.exe")) { found = 1; break; }
    } while (Process32NextW(snapshot, &item));
    CloseHandle(snapshot);
    return found;
}

static HMODULE remote_module(HANDLE process, const wchar_t *name) {
    /* Loader module lists are unavailable before a suspended process starts. */
    ULONG_PTR address = 0;
    MEMORY_BASIC_INFORMATION region;
    while (VirtualQueryEx(process, (LPCVOID)address, &region, sizeof(region))) {
        if (region.Type == MEM_IMAGE && region.BaseAddress == region.AllocationBase) {
            wchar_t path[32768];
            DWORD length = GetMappedFileNameW(process, region.AllocationBase, path, 32768);
            if (length && length < 32768) {
                const wchar_t *base = wcsrchr(path, L'\\');
                if (base && !_wcsicmp(base + 1, name)) return (HMODULE)region.AllocationBase;
            }
        }
        ULONG_PTR next = (ULONG_PTR)region.BaseAddress + region.RegionSize;
        if (next <= address) break;
        address = next;
    }
    SetLastError(ERROR_MOD_NOT_FOUND);
    return NULL;
}

static int call_remote(HANDLE process, LPTHREAD_START_ROUTINE function, void *arg, DWORD *result) {
    HANDLE thread = CreateRemoteThread(process, NULL, 0, function, arg, 0, NULL);
    if (!thread) return 0;
    int ok = WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, result);
    CloseHandle(thread);
    return ok;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t spotify[32768], library[32768];
    int first = 1;
    DWORD length = GetModuleFileNameW(NULL, library, 32768);
    if (!length || length >= 32768) return 1;
    wchar_t *name = wcsrchr(library, L'\\');
    if (!name || (size_t)(name - library) + wcslen(L"\\spotify-filter.dll") >= 32768) return 1;
    wcscpy(name + 1, L"spotify-filter.dll");
    if (argc > 1 && !wcscmp(argv[1], L"--help")) {
        puts("Usage: spotify-filter.exe [--spotify PATH] [Spotify arguments...]\nQuit Spotify first. Requires the desktop x64 client; Store installs are unsupported.");
        return 0;
    }
    if (argc > 1 && !wcscmp(argv[1], L"--spotify")) {
        if (argc < 3) { fputs("--spotify requires a path to Spotify.exe\n", stderr); return 1; }
        length = GetFullPathNameW(argv[2], 32768, spotify, NULL);
        if (!length || length >= 32768) return 1;
        first = 3;
    } else {
        length = GetEnvironmentVariableW(L"APPDATA", spotify, 32768);
        if (!length || length + wcslen(L"\\Spotify\\Spotify.exe") >= 32768) return 1;
        wcscat(spotify, L"\\Spotify\\Spotify.exe");
    }
    DWORD kind;
    if (!GetBinaryTypeW(spotify, &kind) || kind != SCS_64BIT_BINARY) {
        fputs("Install desktop x64 Spotify or pass --spotify with its executable path.\n", stderr);
        return 1;
    }
    if (spotify_running() != 0) {
        fputs("Quit Spotify first (or process enumeration failed).\n", stderr);
        return 1;
    }
    /* Map locally to get the initializer RVA; it performs no work during DLL load. */
    HMODULE local = LoadLibraryW(library);
    if (!local) { fputs("Cannot load spotify-filter.dll. Build the x64 DLL beside this launcher.\n", stderr); return 1; }
    FARPROC initialize = GetProcAddress(local, "initialize_filter");
    FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = NULL;
    /* Forwarded kernel32 exports can live in KernelBase; use the actual owner. */
    if (!initialize || !load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(ULONG_PTR)load, &owner)) {
        FreeLibrary(local); return 1;
    }
    wchar_t owner_path[32768];
    length = GetModuleFileNameW(owner, owner_path, 32768);
    if (!length || length >= 32768) { FreeLibrary(local); return 1; }
    const wchar_t *owner_name = wcsrchr(owner_path, L'\\');
    owner_name = owner_name ? owner_name + 1 : owner_path;
    size_t capacity = 2 * wcslen(spotify) + 4;
    for (int i = first; i < argc; ++i) capacity += 2 * wcslen(argv[i]) + 4;
    if (capacity > 32767) { fputs("Command line too long.\n", stderr); FreeLibrary(local); return 1; }
    wchar_t *command = calloc(capacity, sizeof(wchar_t));
    if (!command) { FreeLibrary(local); return 1; }
    wchar_t *end = quote_argument(command, spotify);
    for (int i = first; i < argc; ++i) { *end++ = L' '; end = quote_argument(end, argv[i]); }
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION child = {0};
    int ok = 0;
    DWORD result = 0;
    const char *stage = "starting process";
    void *remote_path = NULL;
    if (!CreateProcessW(spotify, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &child)) {
        fprintf(stderr, "Cannot start Spotify (Windows error %lu).\n", GetLastError());
        goto cleanup;
    }
    /* A suspended process may only have ntdll mapped. A remote thread runs
       loader initialization before invoking its start routine. */
    stage = "locating ntdll";
    HMODULE local_ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE remote_ntdll = remote_module(child.hProcess, L"ntdll.dll");
    FARPROC alert = GetProcAddress(local_ntdll, "NtTestAlert");
    DWORD bootstrap_result;
    if (!remote_ntdll || !alert) goto cleanup;
    stage = "initializing process loader";
    if (!call_remote(child.hProcess,
            (LPTHREAD_START_ROUTINE)((BYTE *)remote_ntdll +
                ((ULONG_PTR)alert - (ULONG_PTR)local_ntdll)), NULL, &bootstrap_result)) goto cleanup;
    stage = "locating loader module";
    HMODULE remote_owner = remote_module(child.hProcess, owner_name);
    if (!remote_owner) goto cleanup;
    stage = "copying DLL path";
    SIZE_T bytes = (wcslen(library) + 1) * sizeof(wchar_t);
    remote_path = VirtualAllocEx(child.hProcess, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path || !WriteProcessMemory(child.hProcess, remote_path, library, bytes, NULL)) goto cleanup;
    LPTHREAD_START_ROUTINE remote_load = (LPTHREAD_START_ROUTINE)((BYTE *)remote_owner +
        ((ULONG_PTR)load - (ULONG_PTR)owner));
    stage = "loading filter DLL";
    if (!call_remote(child.hProcess, remote_load, remote_path, &result)) goto cleanup;
    /* A thread exit code truncates pointers on x64; enumerate the actual DLL base. */
    stage = "locating filter DLL";
    HMODULE remote = remote_module(child.hProcess, L"spotify-filter.dll");
    if (!remote) goto cleanup;
    LPTHREAD_START_ROUTINE remote_init = (LPTHREAD_START_ROUTINE)((BYTE *)remote +
        ((ULONG_PTR)initialize - (ULONG_PTR)local));
    stage = "installing CEF hook";
    if (!call_remote(child.hProcess, remote_init, NULL, &result) || result != 1) goto cleanup;
    stage = "resuming process";
    if (ResumeThread(child.hThread) == (DWORD)-1) goto cleanup;
    ok = 1;
cleanup:
    if (child.hProcess) {
        if (!ok) {
            fprintf(stderr, "Filter initialization failed at %s (Windows error %lu, thread result %lu); Spotify was stopped.\n", stage, GetLastError(), result);
            TerminateProcess(child.hProcess, 1);
            WaitForSingleObject(child.hProcess, INFINITE);
        }
        if (remote_path) VirtualFreeEx(child.hProcess, remote_path, 0, MEM_RELEASE);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    free(command);
    FreeLibrary(local);
    return ok ? 0 : 1;
}
