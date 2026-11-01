#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "../cef_prefix.h"

__declspec(dllimport) void *cef_urlrequest_create(struct cef_request *, void *, void *);
__declspec(dllimport) int strings_freed(void);
static struct cef_string value;
static struct cef_string *get_url(struct cef_request *request) { (void)request; return &value; }
int wmain(int argc, wchar_t **argv) {
    assert(argc == 3);
    value.str = (uint16_t *)argv[1];
    value.length = wcslen(argv[1]);
    struct cef_request request = {.size = sizeof(request), .get_url = get_url};
    void *result = cef_urlrequest_create(&request, (void *)(uintptr_t)1, (void *)(uintptr_t)2);
    assert(result == NULL || result == &request);
    FILE *output = _wfopen(argv[2], L"w");
    assert(output);
    fprintf(output, "%s %d", result ? "forwarded" : "blocked", strings_freed());
    assert(fclose(output) == 0);
    return 0;
}
