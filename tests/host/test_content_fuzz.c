/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_content.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

int main(void)
{
    uint8_t bytes[1024];
    uint32_t state = UINT32_C(0x4D543031);
    size_t iteration;
    size_t index;

    for (iteration = 0U; iteration < 2000U; ++iteration) {
        mt_content_info_t info = {0};
        const size_t size = next_random(&state) % (sizeof(bytes) + 1U);
        const mt_content_type_t type = (mt_content_type_t)
            (next_random(&state) % (MT_CONTENT_D81_FUTURE + 1U));
        for (index = 0U; index < size; ++index) {
            bytes[index] = (uint8_t)next_random(&state);
        }
        (void)mt_content_inspect(type, bytes, size, &info);
    }

    assert(mt_content_inspect(MT_CONTENT_PRG, NULL, 0U, NULL) ==
           MT_CONTENT_INVALID_ARGUMENT);
    return 0;
}
