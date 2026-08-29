/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_tmp102.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    const uint8_t positive[2] = {0x19U, 0x10U};
    const uint8_t negative[2] = {0xF5U, 0xE0U};
    const uint8_t minimum[2] = {0x80U, 0x00U};
    int32_t value = 0;

    assert(mt_tmp102_decode_micro_c(positive, &value) == 0);
    assert(value == INT32_C(25062500));
    assert(mt_tmp102_decode_micro_c(negative, &value) == 0);
    assert(value == -INT32_C(10125000));
    assert(mt_tmp102_decode_micro_c(minimum, &value) == 0);
    assert(value == -INT32_C(128000000));
    assert(mt_tmp102_decode_micro_c(NULL, &value) == -1);
    return 0;
}
