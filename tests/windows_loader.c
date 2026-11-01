#include <windows.h>
#include <assert.h>
#include <string.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv) {
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, 32768);
    assert(length && length < 32768);
    wchar_t *name = wcsrchr(path, L'\\');
    assert(name && (size_t)(name - path) + 13 < 32768);
    wcscpy(name + 1, L"Spotify.dll");
    /* Spotify also maps DLLs for inspection without resolving their imports. */
    HMODULE probe = LoadLibraryExW(path, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_WITH_ALTERED_SEARCH_PATH);
    assert(probe);
    FreeLibrary(probe);
    HMODULE core = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    assert(core);
    FARPROC symbol = GetProcAddress(core, "run_fixture");
    assert(symbol);
    int (*run)(int, wchar_t **);
    memcpy(&run, &symbol, sizeof(run));
    return run(argc, argv);
}
