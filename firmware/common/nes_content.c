/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_nes_content.h"
#include <string.h>

mt_content_validation_t mt_nes_content_inspect(const uint8_t *data, size_t size,
                                               mt_nes_content_info_t *info)
{
    mt_nes_content_info_t result = {0};
    if (data == NULL || info == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    if (size < 16U) return MT_CONTENT_TOO_SMALL;
    if (memcmp(data, "NES\032", 4U) != 0) return MT_CONTENT_BAD_MAGIC;
    if ((data[7] & 0x0cU) != 0U) return MT_CONTENT_UNSUPPORTED_VERSION;
    /* Trainer, four-screen, VS/PlayChoice and PAL require different machine
     * wiring or content layout. Reject rather than silently booting wrong. */
    if ((data[6] & 0x0cU) != 0U || (data[7] & 3U) != 0U ||
        (data[9] & 1U) != 0U || (data[10] & 3U) != 0U)
        return MT_CONTENT_UNSUPPORTED_VERSION;
    if ((data[9] & 0xfeU) != 0U || (data[10] & 0xfcU) != 0U)
        return MT_CONTENT_BAD_STRUCTURE;
    for (size_t i = 11U; i < 16U; ++i)
        if (data[i] != 0U) return MT_CONTENT_BAD_STRUCTURE;
    result.prg_size = (size_t)data[4] * 16384U;
    result.chr_size = (size_t)data[5] * 8192U;
    if (result.prg_size == 0U || result.prg_size > MT_SIZE_NES_PRG_MAX ||
        result.chr_size > MT_SIZE_NES_CHR_MAX || data[8] > 1U ||
        size != 16U + result.prg_size + result.chr_size)
        return MT_CONTENT_WRONG_SIZE;
    if ((result.prg_size & (result.prg_size - 1U)) != 0U ||
        (result.chr_size != 0U && (result.chr_size & (result.chr_size - 1U)) != 0U))
        return MT_CONTENT_WRONG_SIZE;
    result.mapper = (uint16_t)((data[6] >> 4U) | (data[7] & 0xf0U));
    bool board_size_valid = false;
    switch (result.mapper) {
    case 0: board_size_valid = result.prg_size <= 32768U && result.chr_size <= 8192U; break;
    case 1: board_size_valid = result.prg_size <= 262144U && result.chr_size <= 131072U; break;
    case 2: case 71: board_size_valid = result.prg_size <= 262144U && result.chr_size == 0U; break;
    case 3: board_size_valid = result.prg_size <= 32768U && result.chr_size >= 8192U && result.chr_size <= 32768U; break;
    case 4: board_size_valid = true; break;
    case 7: board_size_valid = result.prg_size >= 32768U && result.prg_size <= 262144U && result.chr_size == 0U; break;
    case 9: board_size_valid = result.prg_size == 131072U && result.chr_size <= 131072U; break;
    case 10: board_size_valid = result.prg_size <= 262144U && result.chr_size <= 131072U; break;
    case 11: board_size_valid = result.prg_size <= 131072U && result.chr_size <= 131072U; break;
    case 66: board_size_valid = result.prg_size <= 131072U && result.chr_size <= 32768U; break;
    default: return MT_CONTENT_UNSUPPORTED_VERSION;
    }
    if (!board_size_valid) return MT_CONTENT_WRONG_SIZE;
    result.battery = (data[6] & 2U) != 0U;
    result.vertical_mirroring = (data[6] & 1U) != 0U;
    *info = result;
    return MT_CONTENT_VALID;
}
