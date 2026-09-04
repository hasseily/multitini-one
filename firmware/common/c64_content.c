/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_content.h"

#include "multitini_c64_interface.h"

#include <string.h>

static mt_c64_content_result_t content_result(
    mt_c64_content_status_t status, mt_content_validation_t validation,
    uint16_t hardware_type)
{
    mt_c64_content_result_t result;
    result.status = status;
    result.validation = validation;
    result.hardware_type = hardware_type;
    return result;
}

bool mt_c64_crt_hardware_supported(uint16_t hardware_type)
{
    switch (hardware_type) {
    case MT_CRT_NORMAL:
    case MT_CRT_SIMONS_BASIC:
    case MT_CRT_OCEAN_TYPE_1:
    case MT_CRT_FUN_PLAY:
    case MT_CRT_SUPER_GAMES:
    case MT_CRT_EPYX_FASTLOAD:
    case MT_CRT_FINAL_CARTRIDGE_1_2:
    case MT_CRT_C64_GAME_SYSTEM:
    case MT_CRT_DINAMIC:
    case MT_CRT_ZAXXON:
    case MT_CRT_MAGIC_DESK:
    case MT_CRT_COMAL_80:
    case MT_CRT_WATERLOO_BASIC:
    case MT_CRT_MIKRO_ASSEMBLER:
    case MT_CRT_PROPHET64:
    case MT_CRT_RGCD:
    case MT_CRT_MAGIC_DESK_2:
        return true;
    default:
        return false;
    }
}

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool indexed_8k_layout(size_t index, uint16_t bank,
                              uint16_t address, uint16_t image_size,
                              size_t bank_limit)
{
    return index < bank_limit && bank == index &&
           address == UINT16_C(0x8000) &&
           image_size == UINT16_C(0x2000);
}

static bool indexed_16k_layout(size_t index, uint16_t bank,
                               uint16_t address, uint16_t image_size,
                               size_t bank_limit)
{
    return index < bank_limit && bank == index &&
           address == UINT16_C(0x8000) &&
           image_size == UINT16_C(0x4000);
}

static bool crt_profile_lines_supported(uint16_t hardware_type,
                                        uint8_t exrom, uint8_t game)
{
    if (exrom > 1U || game > 1U) return false;
    switch (hardware_type) {
    case MT_CRT_NORMAL:
        return !(exrom == 1U && game == 1U);
    case MT_CRT_OCEAN_TYPE_1:
        return exrom == 0U;
    case MT_CRT_SIMONS_BASIC:
    case MT_CRT_FINAL_CARTRIDGE_1_2:
    case MT_CRT_SUPER_GAMES:
    case MT_CRT_ZAXXON:
    case MT_CRT_COMAL_80:
    case MT_CRT_MAGIC_DESK_2:
        return exrom == 0U && game == 0U;
    case MT_CRT_FUN_PLAY:
    case MT_CRT_EPYX_FASTLOAD:
    case MT_CRT_C64_GAME_SYSTEM:
    case MT_CRT_DINAMIC:
    case MT_CRT_MAGIC_DESK:
    case MT_CRT_WATERLOO_BASIC:
    case MT_CRT_MIKRO_ASSEMBLER:
    case MT_CRT_PROPHET64:
    case MT_CRT_RGCD:
        return exrom == 0U && game == 1U;
    default:
        return false;
    }
}

static bool crt_chip_layout_supported(uint16_t hardware_type, size_t index,
                                      uint16_t bank, uint16_t address,
                                      uint16_t image_size, uint8_t exrom,
                                      uint8_t game)
{
    switch (hardware_type) {
    case MT_CRT_NORMAL:
        if (bank != 0U || index > 1U) return false;
        if (exrom == 0U && game == 1U)
            return index == 0U && address == UINT16_C(0x8000) &&
                   image_size == UINT16_C(0x2000);
        if (exrom == 1U && game == 0U)
            return index == 0U && address == UINT16_C(0xe000) &&
                   image_size == UINT16_C(0x2000);
        if (index == 0U) {
            return (image_size == UINT16_C(0x4000) &&
                    address == UINT16_C(0x8000)) ||
                   (image_size == UINT16_C(0x2000) &&
                    address == UINT16_C(0x8000));
        }
        return image_size == UINT16_C(0x2000) &&
               address == UINT16_C(0xa000);
    case MT_CRT_SIMONS_BASIC:
    case MT_CRT_FINAL_CARTRIDGE_1_2:
        if (bank != 0U || index > 1U) return false;
        return index == 0U ?
            (address == UINT16_C(0x8000) &&
             (image_size == UINT16_C(0x2000) ||
              image_size == UINT16_C(0x4000))) :
            (address == UINT16_C(0xa000) &&
             image_size == UINT16_C(0x2000));
    case MT_CRT_OCEAN_TYPE_1:
        return indexed_8k_layout(index, bank, address, image_size, 64U);
    case MT_CRT_FUN_PLAY:
        return indexed_8k_layout(index, bank, address, image_size, 16U);
    case MT_CRT_SUPER_GAMES:
        return indexed_16k_layout(index, bank, address, image_size, 4U);
    case MT_CRT_EPYX_FASTLOAD:
    case MT_CRT_MIKRO_ASSEMBLER:
        return indexed_8k_layout(index, bank, address, image_size, 1U);
    case MT_CRT_C64_GAME_SYSTEM:
        return indexed_8k_layout(index, bank, address, image_size, 64U);
    case MT_CRT_DINAMIC:
        return indexed_8k_layout(index, bank, address, image_size, 16U);
    case MT_CRT_ZAXXON:
        if (index == 0U)
            return bank == 0U && address == UINT16_C(0x8000) &&
                   image_size == UINT16_C(0x1000);
        if (index == 1U)
            return bank == 0U && address == UINT16_C(0xa000) &&
                   image_size == UINT16_C(0x2000);
        return index == 2U && bank == 1U &&
               address == UINT16_C(0xa000) &&
               image_size == UINT16_C(0x2000);
    case MT_CRT_MAGIC_DESK:
        return indexed_8k_layout(index, bank, address, image_size, 128U);
    case MT_CRT_COMAL_80:
        return indexed_16k_layout(index, bank, address, image_size, 4U);
    case MT_CRT_WATERLOO_BASIC:
        return indexed_8k_layout(index, bank, address, image_size, 2U);
    case MT_CRT_PROPHET64:
        return indexed_8k_layout(index, bank, address, image_size, 32U);
    case MT_CRT_RGCD:
        return indexed_8k_layout(index, bank, address, image_size, 8U);
    case MT_CRT_MAGIC_DESK_2:
        return indexed_16k_layout(index, bank, address, image_size, 128U);
    default:
        return false;
    }
}

static bool crt_layout_complete(uint16_t hardware_type, size_t chip_count,
                                bool first_chip_16k)
{
    switch (hardware_type) {
    case MT_CRT_NORMAL:
        return chip_count == 1U || chip_count == 2U;
    case MT_CRT_SIMONS_BASIC:
    case MT_CRT_FINAL_CARTRIDGE_1_2:
        return (chip_count == 1U && first_chip_16k) ||
               (chip_count == 2U && !first_chip_16k);
    case MT_CRT_OCEAN_TYPE_1:
        return chip_count == 16U || chip_count == 32U ||
               chip_count == 64U;
    case MT_CRT_FUN_PLAY:
    case MT_CRT_DINAMIC:
        return chip_count == 16U;
    case MT_CRT_SUPER_GAMES:
    case MT_CRT_COMAL_80:
        return chip_count == 4U;
    case MT_CRT_EPYX_FASTLOAD:
    case MT_CRT_MIKRO_ASSEMBLER:
        return chip_count == 1U;
    case MT_CRT_C64_GAME_SYSTEM:
        return chip_count == 64U;
    case MT_CRT_ZAXXON:
        return chip_count == 3U;
    case MT_CRT_WATERLOO_BASIC:
        return chip_count == 2U;
    case MT_CRT_PROPHET64:
        return chip_count == 32U;
    case MT_CRT_RGCD:
        return chip_count == 8U;
    case MT_CRT_MAGIC_DESK:
    case MT_CRT_MAGIC_DESK_2:
        return chip_count != 0U && chip_count <= 128U &&
               (chip_count & (chip_count - 1U)) == 0U;
    default:
        return false;
    }
}

static bool crt_packets_supported(const uint8_t *data, size_t size,
                                  uint16_t hardware_type)
{
    bool low_bank_seen[256] = {false};
    bool high_bank_seen[256] = {false};
    size_t offset = 0x40U;
    size_t stored_size = 0U;
    size_t chip_count = 0U;
    bool first_chip_16k = false;
    const uint8_t exrom = data[0x18U];
    const uint8_t game = data[0x19U];

    if (read_be32(data + 0x10U) != UINT32_C(0x40)) return false;
    if (!crt_profile_lines_supported(hardware_type, exrom, game))
        return false;
    while (offset < size) {
        const uint32_t packet_size = read_be32(data + offset + 4U);
        const uint16_t chip_type = read_be16(data + offset + 8U);
        const uint16_t bank = read_be16(data + offset + 10U);
        const uint16_t address = read_be16(data + offset + 12U);
        const uint16_t image_size = read_be16(data + offset + 14U);
        const bool is_16k = image_size == UINT16_C(0x4000);
        bool *seen;

        stored_size = (stored_size + 0x1fffU) & ~(size_t)0x1fffU;
        if (chip_type != 0U || bank > UINT16_C(0x00ff) ||
            (address != UINT16_C(0x8000) &&
             address != UINT16_C(0xa000) &&
             address != UINT16_C(0xe000)) ||
            (image_size != UINT16_C(0x1000) &&
             image_size != UINT16_C(0x2000) && !is_16k) ||
            (uint32_t)address + image_size > UINT32_C(0x10000) ||
            (is_16k && address != UINT16_C(0x8000)) ||
            (image_size == UINT16_C(0x1000) &&
             hardware_type != MT_CRT_ZAXXON) ||
            packet_size != (uint32_t)image_size + UINT32_C(0x10) ||
            stored_size + image_size > UINT32_C(0x200000) ||
            !crt_chip_layout_supported(hardware_type, chip_count, bank,
                                       address, image_size, exrom, game)) {
            return false;
        }

        seen = address == UINT16_C(0x8000) ?
                   &low_bank_seen[bank] : &high_bank_seen[bank];
        if (*seen) return false;
        *seen = true;
        if (is_16k) {
            if (high_bank_seen[bank]) return false;
            high_bank_seen[bank] = true;
        }

        if (chip_count == 0U) first_chip_16k = is_16k;
        stored_size += image_size;
        ++chip_count;
        offset += packet_size;
    }
    return crt_layout_complete(hardware_type, chip_count,
                               first_chip_16k);
}

const char *mt_c64_crt_hardware_name(uint16_t hardware_type)
{
    switch (hardware_type) {
    case MT_CRT_NORMAL: return "Normal cartridge";
    case MT_CRT_SIMONS_BASIC: return "Simons' BASIC";
    case MT_CRT_OCEAN_TYPE_1: return "Ocean type 1";
    case MT_CRT_FUN_PLAY: return "Fun Play/Power Play";
    case MT_CRT_SUPER_GAMES: return "Super Games";
    case MT_CRT_EPYX_FASTLOAD: return "Epyx FastLoad";
    case MT_CRT_FINAL_CARTRIDGE_1_2: return "Final Cartridge I/II";
    case MT_CRT_C64_GAME_SYSTEM: return "C64 Game System";
    case MT_CRT_DINAMIC: return "Dinamic";
    case MT_CRT_ZAXXON: return "Zaxxon";
    case MT_CRT_MAGIC_DESK: return "Magic Desk";
    case MT_CRT_COMAL_80: return "Comal 80";
    case MT_CRT_WATERLOO_BASIC: return "Structured BASIC";
    case MT_CRT_MIKRO_ASSEMBLER: return "Mikro Assembler";
    case MT_CRT_PROPHET64: return "Prophet64";
    case MT_CRT_RGCD: return "RGCD";
    case MT_CRT_MAGIC_DESK_2: return "Magic Desk 2";
    default: return "unsupported CRT hardware";
    }
}

mt_c64_content_result_t mt_c64_validate_prg(
    const mt_c64_content_image_t *image)
{
    mt_content_info_t info;
    mt_content_validation_t validation;

    if (image == NULL || image->data == NULL) {
        return content_result(MT_C64_CONTENT_INVALID_ARGUMENT,
                              MT_CONTENT_INVALID_ARGUMENT, 0U);
    }
    if (image->size < MT_SIZE_C64_PRG_MIN ||
        image->size > MT_SIZE_C64_PRG_MAX) {
        return content_result(MT_C64_CONTENT_INVALID_IMAGE,
                              MT_CONTENT_WRONG_SIZE, 0U);
    }
    validation = mt_content_inspect(MT_CONTENT_PRG, image->data,
                                    image->size, &info);
    return content_result(validation == MT_CONTENT_VALID ?
                              MT_C64_CONTENT_OK :
                              MT_C64_CONTENT_INVALID_IMAGE,
                          validation, 0U);
}

mt_c64_content_result_t mt_c64_validate_crt(
    const mt_c64_content_image_t *image)
{
    mt_content_info_t info;
    mt_content_validation_t validation;

    if (image == NULL || image->data == NULL) {
        return content_result(MT_C64_CONTENT_INVALID_ARGUMENT,
                              MT_CONTENT_INVALID_ARGUMENT, 0U);
    }
    if (image->size < MT_SIZE_C64_CRT_MIN ||
        image->size > MT_SIZE_C64_CRT_MAX) {
        return content_result(MT_C64_CONTENT_INVALID_IMAGE,
                              MT_CONTENT_WRONG_SIZE, 0U);
    }
    if (image->data[0x18U] > 1U || image->data[0x19U] > 1U) {
        return content_result(MT_C64_CONTENT_INVALID_IMAGE,
                              MT_CONTENT_BAD_STRUCTURE, 0U);
    }
    validation = mt_content_inspect(MT_CONTENT_CRT, image->data,
                                    image->size, &info);
    if (validation != MT_CONTENT_VALID) {
        return content_result(MT_C64_CONTENT_INVALID_IMAGE, validation, 0U);
    }
    if (!mt_c64_crt_hardware_supported(info.hardware_type)) {
        return content_result(MT_C64_CONTENT_UNSUPPORTED_CRT,
                              MT_CONTENT_VALID, info.hardware_type);
    }
    if (!crt_packets_supported(image->data, image->size,
                               info.hardware_type)) {
        return content_result(MT_C64_CONTENT_INVALID_IMAGE,
                              MT_CONTENT_BAD_STRUCTURE,
                              info.hardware_type);
    }
    return content_result(MT_C64_CONTENT_OK, MT_CONTENT_VALID,
                          info.hardware_type);
}

static mt_link_result_t request_reset(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context)
{
    static const uint32_t args[4] = {1U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, max_polls,
                                progress, progress_context, &response);
}

static mt_link_result_t stream_checked(
    mt_core_link_t *link, uint8_t kind, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    mt_link_result_t result;
    result = mt_core_link_stream(link, kind, image->data, image->size,
                                 max_polls, progress, progress_context);
    if (result != MT_LINK_OK) {
        (void)request_reset(link, max_polls, progress, progress_context);
    }
    return result;
}

mt_link_result_t mt_c64_load_prg(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    if (link == NULL || max_polls == 0U ||
        mt_c64_validate_prg(image).status != MT_C64_CONTENT_OK) {
        return MT_LINK_BAD_ARGUMENT;
    }
    return stream_checked(link, MT_LOAD_C64_PRG, image, max_polls,
                          progress, progress_context);
}

mt_link_result_t mt_c64_stage_prg(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    if (link == NULL || max_polls == 0U ||
        mt_c64_validate_prg(image).status != MT_C64_CONTENT_OK) {
        return MT_LINK_BAD_ARGUMENT;
    }
    return stream_checked(link, MT_LOAD_C64_PRG_STAGE, image, max_polls,
                          progress, progress_context);
}

mt_link_result_t mt_c64_run_prg(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context)
{
    static const uint8_t run_token = 0U;
    if (link == NULL || max_polls == 0U) return MT_LINK_BAD_ARGUMENT;
    /* A rejected run token leaves the staged program and mounted disk intact;
     * unlike a partial object transfer, it must not force a blind core reset. */
    return mt_core_link_stream(link, MT_LOAD_C64_PRG_RUN,
                               &run_token, 1U, max_polls,
                               progress, progress_context);
}

mt_link_result_t mt_c64_load_crt(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    if (link == NULL || max_polls == 0U ||
        mt_c64_validate_crt(image).status != MT_C64_CONTENT_OK) {
        return MT_LINK_BAD_ARGUMENT;
    }
    return stream_checked(link, MT_LOAD_C64_CRT, image, max_polls,
                          progress, progress_context);
}

mt_link_result_t mt_c64_detach_crt(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context)
{
    static const uint8_t detach_token = 0U;
    const mt_c64_content_image_t image = {&detach_token, 1U};
    if (link == NULL || max_polls == 0U) return MT_LINK_BAD_ARGUMENT;
    return stream_checked(link, MT_LOAD_C64_CRT_DETACH, &image, max_polls,
                          progress, progress_context);
}

const char *mt_c64_content_status_name(mt_c64_content_status_t status)
{
    switch (status) {
    case MT_C64_CONTENT_OK: return "ok";
    case MT_C64_CONTENT_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_CONTENT_INVALID_IMAGE: return "invalid image";
    case MT_C64_CONTENT_UNSUPPORTED_CRT: return "unsupported CRT hardware";
    }
    return "unknown C64 content status";
}
