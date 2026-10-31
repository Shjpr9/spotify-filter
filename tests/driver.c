#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent CEF ABI fixture: ref-counted base, is_read_only, get_url. */
struct url { uint16_t *str; size_t length; void (*dtor)(uint16_t *); };
struct request {
    size_t size;
    void (*base_methods[4])(void);
    int (*is_read_only)(void *);
    struct url *(*get_url)(void *);
};
static struct url value;
static struct url *get_url(void *request) { (void)request; return &value; }
static struct url *no_url(void *request) { (void)request; return NULL; }
extern void *cef_urlrequest_create(void *, void *, void *);
extern int strings_freed(void);
int main(int argc, char **argv) {
    assert(argc == 3);
    size_t n = strlen(argv[1]);
    value.str = calloc(n + 1, sizeof(uint16_t));
    assert(value.str);
    for (size_t i = 0; i < n; ++i) value.str[i] = (unsigned char)argv[1][i];
    value.length = n;
    struct request request = {.size = sizeof(request), .get_url = get_url};
    int mode = atoi(argv[2]);
    if (mode == 1) request.get_url = no_url;
    if (mode == 2) request.size = sizeof(size_t);
    if (mode == 3) request.get_url = NULL;
    void *result = cef_urlrequest_create(&request, (void *)(uintptr_t)1, (void *)(uintptr_t)2);
    assert(result == NULL || result == &request);
    assert(strings_freed() == (mode == 0 ? 1 : 0));
    puts(result ? "forwarded" : "blocked");
    free(value.str);
}
