/* SPDX-License-Identifier: GPL-3.0-only */
#include "gameboy_content_service.h"

#include "multitini_gameboy_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_size;
    uint32_t active_offset;
    uint8_t completed_kind;
    unsigned reset_commands;
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
        shell->registers[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_LOAD_ERROR;
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
    if (opcode == MT_COMMAND_STREAM_BEGIN) {
        shell->completed_kind =
            (uint8_t)shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->active_size = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->active_offset = 0U;
    } else if (opcode == MT_COMMAND_STREAM_DATA) {
        ++shell->active_offset;
    } else if (opcode == MT_COMMAND_STREAM_END) {
        assert(shell->active_offset == shell->active_size);
        if (shell->raise_load_error_on_end != 0)
            shell->registers[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_LOAD_ERROR;
    } else if (opcode == MT_COMMAND_CORE_RESET) {
        ++shell->reset_commands;
        if (shell->registers[MT_REG_COMMAND_ARG0 / 4U] == 0U)
            shell->registers[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_READY;
        else
            shell->registers[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_READY;
    } else {
        assert(opcode == MT_COMMAND_STREAM_ABORT);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

static void finish_header(uint8_t *cart, size_t size)
{
    uint8_t checksum = 0U;
    memset(cart, 0, size);
    cart[0x143U] = 0x80U;
    cart[0x147U] = 0x1bU;
    cart[0x148U] = 0x00U;
    cart[0x149U] = 0x03U;
    for (size_t index = 0x134U; index <= 0x14cU; ++index)
        checksum = (uint8_t)(checksum - cart[index] - 1U);
    cart[0x14dU] = checksum;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    static uint8_t cart[MT_SIZE_GB_CART_MIN];
    assert(strlen(path) < sizeof(requested_path));
    strcpy(requested_path, path);
    if (fail_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 9U;
        return result;
    }
    assert(capacity >= sizeof(cart));
    finish_header(cart, sizeof(cart));
    if (strstr(path, "bad") != NULL) cart[0x14dU] ^= 1U;
    memcpy(dst, cart, sizeof(cart));
    result.size = sizeof(cart);
    return result;
}

int main(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_gameboy_content_service_result_t result;

    mt_core_link_init(&link, &shell, read32, write32);
    gameboy_content_service_init(&link, "0:/multitini/cores/gbc/media", respond,
                                 &shell);
    result = gameboy_content_service_load("demo.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path,
                  "0:/multitini/cores/gbc/media/demo.gbc") == 0);
    assert(shell.completed_kind == MT_LOAD_GB_CART);
    assert(shell.reset_commands == 2U);
    assert(result.info.mapper_type == 0x1bU);
    assert(result.info.rom_size_code == 0x00U);
    assert(result.info.ram_size_code == 0x03U);
    assert(result.info.color_capable);
    assert(!result.info.color_required);
    assert(gameboy_content_service_cartridge_attached());
    assert(strcmp(gameboy_content_service_cartridge_name(), "demo.gbc") == 0);

    result = gameboy_content_service_detach();
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
    assert(shell.reset_commands == 3U);
    assert(!gameboy_content_service_cartridge_attached());

    result = gameboy_content_service_load("demo.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);

    result = gameboy_content_service_load("bad.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_REJECTED);
    assert(result.validation == MT_CONTENT_BAD_STRUCTURE);
    assert(gameboy_content_service_cartridge_attached());

    shell.raise_load_error_on_end = 1;
    result = gameboy_content_service_load("fault.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(result.link == MT_LINK_REMOTE_ERROR);
    assert(!gameboy_content_service_cartridge_attached());
    shell.raise_load_error_on_end = 0;

    result = gameboy_content_service_load("../escape.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT);
    fail_read = 1;
    result = gameboy_content_service_load("missing.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_READ_FAILED);
    assert(result.sd.backend_status == 9U);
    assert(strcmp(gameboy_content_service_status_name(result.status),
                  "read failed") == 0);

    gameboy_content_service_init(&link, "relative/path", respond, &shell);
    result = gameboy_content_service_load("demo.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT);
    return 0;
}
