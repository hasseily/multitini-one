/* SPDX-License-Identifier: GPL-3.0-only */
/* Include the actual parser with allocator observation, so the test bounds
 * requested heap bytes rather than relying on a process RSS estimate. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union { max_align_t alignment; size_t size; } allocation_t;
static size_t live_bytes, peak_bytes, allocations, fail_at = SIZE_MAX;
static void *observed_malloc(size_t size) {
    if (allocations++ == fail_at) return NULL;
    allocation_t *p = malloc(sizeof(*p) + size);
    if (!p) return NULL;
    p->size = size;
    live_bytes += size;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return p + 1;
}
static void observed_free(void *value) {
    if (!value) return;
    allocation_t *p = (allocation_t *)value - 1;
    assert(live_bytes >= p->size);
    live_bytes -= p->size;
    free(p);
}
static void *observed_calloc(size_t count, size_t size) {
    assert(!count || size <= SIZE_MAX / count);
    void *p = observed_malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}
static void *observed_realloc(void *old, size_t size) {
    void *p = observed_malloc(size);
    if (!p) return NULL;
    if (old) {
        allocation_t *a = (allocation_t *)old - 1;
        memcpy(p, old, a->size < size ? a->size : size);
    }
    observed_free(old);
    return p;
}
#define malloc observed_malloc
#define calloc observed_calloc
#define realloc observed_realloc
#define free observed_free
#ifndef HTTP_JSON_SOURCE
#define HTTP_JSON_SOURCE "../../firmware/common/http_json.c"
#endif
#include HTTP_JSON_SOURCE
#undef malloc
#undef calloc
#undef realloc
#undef free
static void reset_observation(void) {
    assert(live_bytes == 0);
    peak_bytes = allocations = 0;
    fail_at = SIZE_MAX;
}
static void string_heap_bound(bool object) {
    char input[HTTP_BODY_LIMIT];
    size_t at = 0;
    input[at++] = object ? '{' : '[';
    for (unsigned i = 0; i < 1000; i++) {
        if (i) input[at++] = ',';
        const char *entry = object ? "\"k\":\"v\"" : "\"a\"";
        size_t length = strlen(entry);
        memcpy(input + at, entry, length);
        at += length;
    }
    input[at++] = object ? '}' : ']';
    memset(input + at, ' ', sizeof(input) - at);
    reset_observation();
    http_json_t *j = http_json_parse(input, sizeof(input));
    assert(j && http_json_count(j) == 1001);
    printf("JSON %s: input=%u peak_heap=%zu nodes=%u\n", object ? "object" : "array",
           HTTP_BODY_LIMIT, peak_bytes, http_json_count(j));
    fflush(stdout);
    /* Every key/string/number owns at most its encoded bytes plus one NUL;
     * each node has at most two strings. This bound excludes allocator headers. */
    assert(peak_bytes <= HTTP_BODY_LIMIT + HTTP_NODE_LIMIT * (sizeof(http_json_t) + 2));
    http_json_free(j);
    assert(!live_bytes);
}
static void malformed_and_failure(void) {
    const char *bad[] = {"\"unterminated", "\"bad\\q\"", "\"\\u0000\"", "\"\\uD800\"",
        "\"\\uDC00\"", "\"\\uD800\\u0041\"", "\"\\u12x4\"", "[\"ok\",]", "{\"a\":\"x\",}",
        "\"line\nbreak\"", "[01]", "[1e]", "[1.]", "\"\\"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        reset_observation();
        assert(!http_json_parse(bad[i], strlen(bad[i])) && !live_bytes);
    }
    const char good[] = "{\"q\\\"\\\\\":\"a\\n\\u00E9\\uD83D\\uDE00\",\"n\":[-17,1.5,true,null]}";
    reset_observation();
    http_json_t *j = http_json_parse(good, strlen(good));
    assert(j && !strcmp(j->child->key, "q\"\\") &&
           !strcmp(j->child->text, "a\n\xc3\xa9\xf0\x9f\x98\x80"));
    size_t count = allocations;
    http_json_free(j);
    for (size_t i = 0; i < count; i++) {
        reset_observation();
        fail_at = i;
        j = http_json_parse(good, strlen(good));
        assert(!j && !live_bytes);
    }
    char large[HTTP_BODY_LIMIT];
    memset(large, 'x', sizeof(large)); large[0] = large[sizeof(large)-1] = '"';
    reset_observation();
    j = http_json_parse(large, sizeof(large));
    assert(j && strlen(j->text) == sizeof(large)-2);
    http_json_free(j);
    assert(!live_bytes);
    large[sizeof(large)-1] = '\\';
    assert(!http_json_parse(large, sizeof(large)) && !live_bytes);
}
int main(void) {
    string_heap_bound(false);
    string_heap_bound(true);
    malformed_and_failure();
    puts("PASS JSON linear heap bound; malformed escapes/surrogates/numbers; allocation failures");
    return 0;
}
