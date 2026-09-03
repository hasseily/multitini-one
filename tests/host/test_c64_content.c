/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_content.h"

#include "multitini_c64_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_kind;
    uint32_t active_size;
    uint32_t active_offset;
    uint32_t completed_kind;
    uint32_t reset_requests;
    uint32_t fail_at;
    int fail_enabled;
} shell_t;

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
    const uint32_t arg1 = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
    uint32_t response = MT_RESPONSE_OK;

    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    switch (opcode) {
    case MT_COMMAND_STREAM_BEGIN:
        shell->active_kind = arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        if (shell->fail_enabled != 0 && shell->active_offset == shell->fail_at) {
            response = MT_RESPONSE_INVALID_ARGUMENT;
        } else {
            ++shell->active_offset;
        }
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        shell->completed_kind = shell->active_kind;
        shell->active_size = 0U;
        break;
    case MT_COMMAND_STREAM_ABORT:
        shell->active_size = 0U;
        break;
    case MT_COMMAND_CORE_RESET:
        assert(arg0 == 1U);
        ++shell->reset_requests;
        break;
    default:
        assert(0);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = response;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

static void be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static size_t make_crt(uint8_t *crt, uint16_t hardware_type)
{
    const size_t size = 0x40U + 0x10U + 0x2000U;
    memset(crt, 0, size);
    memcpy(crt, "C64 CARTRIDGE   ", 16U);
    be32(crt + 0x10U, UINT32_C(0x40));
    be16(crt + 0x14U, UINT16_C(0x0100));
    be16(crt + 0x16U, hardware_type);
    crt[0x18U] = 0U;
    crt[0x19U] = 1U;
    memcpy(crt + 0x40U, "CHIP", 4U);
    be32(crt + 0x44U, UINT32_C(0x2010));
    be16(crt + 0x48U, 0U);
    be16(crt + 0x4aU, 0U);
    be16(crt + 0x4cU, UINT16_C(0x8000));
    be16(crt + 0x4eU, UINT16_C(0x2000));
    memset(crt + 0x50U, 0xa5, 0x2000U);
    return size;
}

int main(void)
{
    uint8_t prg[] = {0x01U, 0x08U, 0xa9U, 0x00U, 0x60U};
    static uint8_t crt[0x40U + 0x10U + 0x4000U];
    mt_c64_content_image_t image = {prg, sizeof(prg)};
    mt_c64_content_result_t validation;
    shell_t shell = {0};
    mt_core_link_t link;
    size_t crt_size;

    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_OK);
    prg[0] = 0xffU;
    prg[1] = 0xffU;
    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_INVALID_IMAGE);
    prg[0] = 0x01U;
    prg[1] = 0x08U;

    crt_size = make_crt(crt, MT_CRT_NORMAL);
    image.data = crt;
    image.size = crt_size;
    validation = mt_c64_validate_crt(&image);
    assert(validation.status == MT_C64_CONTENT_OK);
    assert(validation.hardware_type == MT_CRT_NORMAL);
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_NORMAL),
                  "Normal cartridge") == 0);

    be16(crt + 0x16U, 32U);
    validation = mt_c64_validate_crt(&image);
    assert(validation.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(validation.hardware_type == 32U);
    be16(crt + 0x16U, MT_CRT_OCEAN_TYPE_1);
    be16(crt + 0x48U, 1U);
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x48U, 0U);
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x16U, MT_CRT_NORMAL);
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    crt[0x18U] = 2U;
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    crt[0x18U] = 0U;
    be16(crt + 0x16U, MT_CRT_MAGIC_DESK);
    crt[0x19U] = 0U;
    assert(mt_c64_validate_crt(&image).status ==
           MT_C64_CONTENT_INVALID_IMAGE);
    be16(crt + 0x16U, MT_CRT_NORMAL);
    crt[0x19U] = 1U;

    crt_size = make_crt(crt, MT_CRT_FINAL_CARTRIDGE_1_2);
    crt[0x19U] = 0U;
    be32(crt + 0x44U, UINT32_C(0x4010));
    be16(crt + 0x4eU, UINT16_C(0x4000));
    memset(crt + 0x50U, 0xa5, 0x4000U);
    image.size = 0x40U + 0x10U + 0x4000U;
    assert(mt_c64_validate_crt(&image).status == MT_C64_CONTENT_OK);
    assert(strcmp(mt_c64_crt_hardware_name(MT_CRT_FINAL_CARTRIDGE_1_2),
                  "Final Cartridge I/II") == 0);

    crt_size = make_crt(crt, MT_CRT_NORMAL);
    image.size = crt_size;

    mt_core_link_init(&link, &shell, read32, write32);
    image.data = prg;
    image.size = sizeof(prg);
    assert(mt_c64_load_prg(&link, &image, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_PRG);

    image.data = crt;
    image.size = crt_size;
    assert(mt_c64_load_crt(&link, &image, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_CRT);
    assert(mt_c64_detach_crt(&link, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.completed_kind == MT_LOAD_C64_CRT_DETACH);

    shell.fail_enabled = 1;
    shell.fail_at = 2U;
    image.data = prg;
    image.size = sizeof(prg);
    assert(mt_c64_load_prg(&link, &image, 2U, respond, &shell) ==
           MT_LINK_REMOTE_ERROR);
    assert(shell.reset_requests == 1U);

    assert(mt_c64_load_prg(NULL, &image, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(strcmp(mt_c64_content_status_name(MT_C64_CONTENT_UNSUPPORTED_CRT),
                  "unsupported CRT hardware") == 0);
    return 0;
}
