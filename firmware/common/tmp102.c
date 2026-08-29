/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_tmp102.h"

#include <stddef.h>

int mt_tmp102_decode_micro_c(const uint8_t raw[2], int32_t *micro_c)
{
    int16_t sample;
    if (raw == NULL || micro_c == NULL) return -1;
    sample = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    sample = (int16_t)(sample >> 4);
    if ((sample & INT16_C(0x0800)) != 0)
        sample = (int16_t)(sample | (int16_t)UINT16_C(0xF000));
    *micro_c = (int32_t)sample * INT32_C(62500);
    return 0;
}
