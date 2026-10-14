/* SPDX-License-Identifier: GPL-3.0-only */
/* Compare the production CRC with the former bitwise recurrence. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "crc32.h"

static uint32_t reference_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *bytes = data;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc;
}

static uint32_t random_state = UINT32_C(0x20261014);
static uint32_t random_word(void)
{
    random_state ^= random_state << 13U;
    random_state ^= random_state >> 17U;
    random_state ^= random_state << 5U;
    return random_state;
}

static void check_stream(const uint8_t *data, size_t length, uint32_t seed)
{
    const uint32_t expected = reference_update(seed, data, length);
    uint32_t state = seed;
    size_t offset = 0U;
    assert(crc32_update(seed, data, length) == expected);
    while (offset < length) {
        size_t chunk = 1U + (random_word() % 211U);
        if (chunk > length - offset) chunk = length - offset;
        assert(crc32_update(state, NULL, 0U) == state);
        state = crc32_update(state, data + offset, chunk);
        offset += chunk;
    }
    assert(state == expected);
    assert(crc32_finish(state) == (expected ^ UINT32_C(0xFFFFFFFF)));
}

static void vectors(void)
{
    static const char fox[] = "The quick brown fox jumps over the lazy dog";
    uint8_t bytes[256];
    for (unsigned i = 0U; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)i;
    assert(crc32_init() == UINT32_C(0xFFFFFFFF));
    assert(crc32_finish(crc32_update(crc32_init(), NULL, 0U)) == 0U);
    assert(crc32_finish(crc32_update(crc32_init(), "123456789", 9U)) == UINT32_C(0xCBF43926));
    assert(crc32_finish(crc32_update(crc32_init(), "abc", 3U)) == UINT32_C(0x352441C2));
    assert(crc32_finish(crc32_update(crc32_init(), fox, sizeof(fox) - 1U)) == UINT32_C(0x414FA339));
    assert(crc32_finish(crc32_update(crc32_init(), bytes, sizeof(bytes))) == UINT32_C(0x29058C73));
    assert(crc32_update(UINT32_C(0xA5C31F09), NULL, 0U) == UINT32_C(0xA5C31F09));
    assert(crc32_finish(0U) == UINT32_C(0xFFFFFFFF));
}

static void equivalence(void)
{
    static const uint32_t seeds[] = {
        0U, 1U, UINT32_C(0xFFFFFFFF), UINT32_C(0x80000000),
        UINT32_C(0xDEADBEEF), UINT32_C(0x12345678)
    };
    static const size_t boundaries[] = {
        127U, 128U, 129U, 255U, 256U, 257U, 511U, 512U,
        513U, 1023U, 1024U, 1025U, 4095U, 4096U
    };
    uint8_t storage[4096U + 64U];
    unsigned cases = 0U;
    for (size_t i = 0U; i < sizeof(storage); ++i) storage[i] = (uint8_t)random_word();
    for (unsigned alignment = 0U; alignment < 64U; ++alignment) {
        for (unsigned seed = 0U; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
            for (size_t length = 0U; length <= 65U; ++length) {
                check_stream(storage + alignment, length, seeds[seed]); ++cases;
            }
            for (unsigned i = 0U; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
                check_stream(storage + alignment, boundaries[i], seeds[seed]); ++cases;
            }
        }
    }
    for (unsigned i = 0U; i < 512U; ++i) {
        const unsigned alignment = random_word() % 64U;
        const size_t length = random_word() % 4097U;
        const uint32_t seed = random_word();
        check_stream(storage + alignment, length, seed); ++cases;
    }
    printf("PASS: %u seeded CRC streams, 64 alignments, boundary lengths and incremental chunking\n", cases);
}

static void large_image(int benchmark)
{
    enum { IMAGE_BYTES = 6U * 1024U * 1024U, ROUNDS = 3U };
    uint8_t *data = malloc(IMAGE_BYTES);
    uint32_t reference[ROUNDS], actual[ROUNDS];
    clock_t slow_start, slow_end, fast_start, fast_end;
    volatile uint32_t sink = 0U;
    assert(data != NULL);
    for (size_t i = 0U; i < IMAGE_BYTES; ++i)
        data[i] = (uint8_t)((i * 73U) ^ (i >> 7U) ^ (i >> 17U) ^ 0xA5U);
    const unsigned rounds = benchmark ? ROUNDS : 1U;
    slow_start = clock();
    for (unsigned i = 0U; i < rounds; ++i) {
        reference[i] = crc32_finish(reference_update(crc32_init() ^ i, data, IMAGE_BYTES));
        sink ^= reference[i];
    }
    slow_end = clock();
    fast_start = clock();
    for (unsigned i = 0U; i < rounds; ++i) {
        actual[i] = crc32_finish(crc32_update(crc32_init() ^ i, data, IMAGE_BYTES));
        sink ^= actual[i];
    }
    fast_end = clock();
    for (unsigned i = 0U; i < rounds; ++i) assert(actual[i] == reference[i]);
    assert(sink == 0U);
    puts("PASS: synthetic 6 MiB firmware CRC identity preserved");
    if (benchmark) {
        const double slow_ms = (double)(slow_end - slow_start) * 1000.0 / CLOCKS_PER_SEC;
        const double fast_ms = (double)(fast_end - fast_start) * 1000.0 / CLOCKS_PER_SEC;
        printf("HOST_BENCHMARK_JSON: {\"scope\":\"host only\",\"bytes\":%u,\"rounds\":%u,"
               "\"reference_ms\":%.3f,\"table_ms\":%.3f,\"speedup\":%.3f,"
               "\"crc32\":[%u,%u,%u]}\n", IMAGE_BYTES, ROUNDS,
               slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
               actual[0], actual[1], actual[2]);
    }
    free(data);
}

int main(int argc, char **argv)
{
    const int benchmark = argc == 2 && !strcmp(argv[1], "--benchmark");
    assert(argc == 1 || benchmark);
    vectors();
    equivalence();
    large_image(benchmark);
    puts("PASS: CRC-32 known vectors, empty/NULL chunks and arbitrary initial states");
    return 0;
}
