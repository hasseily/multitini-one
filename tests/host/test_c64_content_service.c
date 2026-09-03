/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_content_service.h"

#include "multitini_c64_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_size;
    uint32_t active_offset;
    uint8_t active_kind;
    uint8_t completed[8];
    size_t completed_count;
    int raise_load_error_on_end;
} shell_t;

static char requested_path[256];
static int fail_read;

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    if (offset == MT_REG_ERROR && (value & MT_ERROR_CORE_LOAD) != 0U) {
        shell->registers[MT_REG_STATUS / 4U] &=
            ~MT_STATUS_CORE_LOAD_ERROR;
        return;
    }
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    switch (opcode) {
    case MT_COMMAND_STREAM_BEGIN:
        shell->active_kind =
            (uint8_t)shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->active_size = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        ++shell->active_offset;
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        assert(shell->completed_count <
               sizeof(shell->completed) / sizeof(shell->completed[0]));
        shell->completed[shell->completed_count++] = shell->active_kind;
        shell->active_size = 0U;
        if (shell->raise_load_error_on_end != 0)
            shell->registers[MT_REG_STATUS / 4U] |=
                MT_STATUS_CORE_LOAD_ERROR;
        break;
    case MT_COMMAND_STREAM_ABORT:
        shell->active_size = 0U;
        break;
    case MT_COMMAND_CORE_RESET:
        break;
    default:
        assert(0);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
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

static size_t make_crt(uint8_t *data, uint16_t hardware_type)
{
    const size_t size = 0x40U + 0x10U + 0x2000U;
    memset(data, 0, size);
    memcpy(data, "C64 CARTRIDGE   ", 16U);
    be32(data + 0x10U, UINT32_C(0x40));
    be16(data + 0x14U, UINT16_C(0x0100));
    be16(data + 0x16U, hardware_type);
    data[0x19U] = 1U;
    memcpy(data + 0x40U, "CHIP", 4U);
    be32(data + 0x44U, UINT32_C(0x2010));
    be16(data + 0x4cU, UINT16_C(0x8000));
    be16(data + 0x4eU, UINT16_C(0x2000));
    memset(data + 0x50U, 0x5a, 0x2000U);
    return size;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    static uint8_t crt[0x40U + 0x10U + 0x2000U];
    assert(strlen(path) < sizeof(requested_path));
    strcpy(requested_path, path);
    if (fail_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 7U;
        return result;
    }
    if (strstr(path, ".prg") != NULL) {
        static const uint8_t prg[] = {0x01U, 0x08U, 0x00U};
        assert(capacity >= sizeof(prg));
        memcpy(dst, prg, sizeof(prg));
        result.size = sizeof(prg);
    } else {
        const uint16_t type = strstr(path, "unsupported") != NULL ?
                                  32U : MT_CRT_MAGIC_DESK;
        result.size = make_crt(crt, type);
        assert(capacity >= result.size);
        memcpy(dst, crt, result.size);
    }
    return result;
}

int main(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;

    mt_core_link_init(&link, &shell, read32, write32);
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    c64_content_service_init(&link, respond, &shell);

    result = c64_content_service_load("demo.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path,
                  "0:/multitini/cores/c64/media/demo.crt") == 0);
    assert(c64_content_service_crt_attached());
    assert(shell.completed[0] == MT_LOAD_C64_CRT);

    result = c64_content_service_load("hello.prg");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(!c64_content_service_crt_attached());
    assert(shell.completed[1] == MT_LOAD_C64_CRT_DETACH);
    assert(shell.completed[2] == MT_LOAD_C64_PRG);

    result = c64_content_service_load("unsupported.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED);
    assert(result.content.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(shell.completed_count == 3U);

    shell.raise_load_error_on_end = 1;
    result = c64_content_service_load("pl-error.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(result.link == MT_LINK_REMOTE_ERROR);
    assert(!c64_content_service_crt_attached());
    shell.raise_load_error_on_end = 0;

    result = c64_content_service_load("../escape.prg");
    assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    fail_read = 1;
    result = c64_content_service_load("missing.prg");
    assert(result.status == MT_C64_CONTENT_SERVICE_READ_FAILED);
    assert(result.sd.backend_status == 7U);
    assert(strcmp(c64_content_service_status_name(result.status),
                  "read failed") == 0);
    return 0;
}
