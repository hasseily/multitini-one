/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_nes_content.h"
#include <string.h>

static mt_content_validation_t reject(mt_nes_content_info_t *info,
    mt_content_validation_t status, const char *reason)
{
    info->rejection_reason = reason;
    return status;
}

mt_content_validation_t mt_nes_content_inspect(const uint8_t *data, size_t size,
                                               mt_nes_content_info_t *info)
{
    mt_nes_content_info_t result = {0};
    if (info == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    if (data == NULL) return reject(info, MT_CONTENT_INVALID_ARGUMENT, "NES cartridge data is missing");
    if (size < 16U) return reject(info, MT_CONTENT_TOO_SMALL, "NES cartridge header is truncated");
    if (memcmp(data, "NES\032", 4U) != 0)
        return reject(info, MT_CONTENT_BAD_MAGIC, "File has no NES cartridge header");
    const uint8_t version = data[7] & 0x0cU;
    if (version != 0U && version != 8U)
        return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "Unknown NES header version");
    if ((data[6] & 4U) != 0U)
        return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES trainer data is unsupported");
    if ((data[6] & 8U) != 0U)
        return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES four-screen mirroring is unsupported");
    if ((data[7] & 3U) != 0U)
        return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES console type is unsupported");
    memcpy(result.canonical_header, data, sizeof(result.canonical_header));
    result.mapper = (uint16_t)((data[6] >> 4U) | (data[7] & 0xf0U));
    result.battery = (data[6] & 2U) != 0U;
    if (version == 8U) {
        const uint8_t submapper = data[8] >> 4U;
        if ((data[8] & 15U) != 0U ||
            (submapper != 0U && !(result.mapper == 2U && submapper <= 2U)))
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES cartridge board variant is unsupported");
        if (data[9] != 0U)
            return reject(info, MT_CONTENT_WRONG_SIZE, "NES extended ROM size is unsupported");
        /* The retained machine has one 8 KiB PRG RAM store, either volatile
         * or battery-backed, and an 8 KiB CHR RAM store when CHR ROM is absent.
         * Do not collapse additional banks or simultaneous RAM/NVRAM stores. */
        if ((result.battery && data[10] != 0x70U) ||
            (!result.battery && data[10] != 0U && data[10] != 7U) ||
            data[11] != (data[5] == 0U ? 7U : 0U))
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES cartridge needs unsupported RAM");
        if (data[12] != 0U && data[12] != 2U)
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES cartridge requires PAL or Dendy timing");
        if (data[13] != 0U || data[14] != 0U)
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES extra cartridge hardware is unsupported");
        if (data[15] > 1U)
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES expansion input is unsupported");
        result.canonical_header[7] &= 0xf0U;
        memset(result.canonical_header + 8U, 0, 8U);
        /* Multitini's canonical mapper-2 token preserves NES 2.0 submapper-2
         * ROM/CPU AND bus conflicts. Older loaders reject it safely. */
        if (result.mapper == 2U && submapper == 2U)
            result.canonical_header[10] = 0x20U;
    } else {
        if ((data[9] & 1U) != 0U || (data[10] & 3U) != 0U)
            return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES cartridge requires PAL timing");
        if ((data[9] & 0xfeU) != 0U ||
            (data[10] != 0U && !(result.mapper == 2U && data[10] == 0x20U)))
            return reject(info, MT_CONTENT_BAD_STRUCTURE, "NES reserved header fields are not clear");
        for (size_t i = 11U; i < 16U; ++i)
            if (data[i] != 0U)
                return reject(info, MT_CONTENT_BAD_STRUCTURE, "NES reserved header fields are not clear");
        if (data[8] > 1U)
            return reject(info, MT_CONTENT_WRONG_SIZE, "NES cartridge needs more than 8 KiB RAM");
    }
    result.prg_size = (size_t)data[4] * 16384U;
    result.chr_size = (size_t)data[5] * 8192U;
    if (result.prg_size == 0U || result.prg_size > MT_SIZE_NES_PRG_MAX ||
        result.chr_size > MT_SIZE_NES_CHR_MAX ||
        size != 16U + result.prg_size + result.chr_size)
        return reject(info, MT_CONTENT_WRONG_SIZE, "NES cartridge ROM size does not match header");
    if ((result.prg_size & (result.prg_size - 1U)) != 0U ||
        (result.chr_size != 0U && (result.chr_size & (result.chr_size - 1U)) != 0U))
        return reject(info, MT_CONTENT_WRONG_SIZE, "NES cartridge ROM size is unsupported");
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
    default: return reject(info, MT_CONTENT_UNSUPPORTED_VERSION, "NES mapper is not implemented");
    }
    if (!board_size_valid)
        return reject(info, MT_CONTENT_WRONG_SIZE, "NES ROM size is unsupported for this mapper");
    result.vertical_mirroring = (data[6] & 1U) != 0U;
    *info = result;
    return MT_CONTENT_VALID;
}
