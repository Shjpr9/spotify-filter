#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static int freed;
void cef_string_userfree_utf16_free(void *value) { assert(value); ++freed; }
int strings_freed(void) { return freed; }
void *cef_urlrequest_create(void *request, void *client, void *context) {
    assert(client == (void *)(uintptr_t)1);
    assert(context == (void *)(uintptr_t)2);
    return request;
}
