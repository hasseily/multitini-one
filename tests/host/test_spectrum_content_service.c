/* SPDX-License-Identifier: GPL-3.0-only */
#include "spectrum_content_service.h"
#include "multitini_spectrum_interface.h"
#include "sd_storage.h"
#include "sleep.h"

#include <assert.h>
#include <string.h>

#define CORE_DIRECTORY "0:/multitini/cores/zx-spectrum"

typedef struct {
    uint32_t regs[MT_REGISTER_SPAN / 4U];
    unsigned reset_commands;
    size_t size;
    size_t received;
    bool reset_held;
    bool stream_open;
    bool loaded;
} shell_t;

static size_t rom_size;

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst, size_t capacity)
{
    assert(strcmp(path, CORE_DIRECTORY "/roms/spectrum.rom") == 0);
    assert(capacity == MT_SPECTRUM_ROM_128_BYTES && rom_size <= capacity);
    if (rom_size == 0U) return (mt_sd_result_t){.status = MT_SD_OPEN_FAILED};
    memset(dst, 0x5A, rom_size);
    dst[rom_size - 1U] = 0xA5U;
    return (mt_sd_result_t){.status = MT_SD_OK, .size = rom_size};
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    (void)status;
    return "mock SD error";
}

void usleep(uint32_t microseconds) { (void)microseconds; }

static uint32_t read32(void *context, uint32_t offset)
{
    const shell_t *shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_STATUS && shell->stream_open)
        return shell->received < shell->size ? MT_STREAM_STATUS_READY : MT_STREAM_STATUS_DRAINED;
    return shell->regs[offset / 4U];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_DATA_BURST) {
        assert(shell->reset_held && shell->stream_open && shell->received < shell->size);
        for (unsigned lane = 0U; lane < MT_STREAM_BURST_BYTES && shell->received < shell->size; ++lane) {
            const uint8_t expected = ++shell->received == shell->size ? 0xA5U : 0x5AU;
            assert((uint8_t)(value >> (lane * 8U)) == expected);
        }
    } else if (offset == MT_REG_ERROR) {
        assert(value == MT_ERROR_CORE_LOAD);
        shell->regs[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_LOAD_ERROR;
    } else if (offset == MT_REG_COMMAND_DOORBELL) {
        const uint32_t arg = shell->regs[MT_REG_COMMAND_ARG0 / 4U];
        assert(value == shell->regs[MT_REG_COMMAND_SEQ / 4U]);
        switch (shell->regs[MT_REG_COMMAND_OPCODE / 4U]) {
        case MT_COMMAND_CORE_RESET:
            ++shell->reset_commands;
            assert(!shell->stream_open);
            shell->reset_held = arg != 0U;
            if (shell->reset_held) {
                shell->loaded = false;
                shell->regs[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_READY;
            } else {
                assert(shell->loaded);
                shell->regs[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_READY;
            }
            break;
        case MT_COMMAND_STREAM_BEGIN:
            assert(shell->reset_held && !shell->stream_open && arg == MT_LOAD_SPECTRUM_ROM);
            shell->size = shell->regs[MT_REG_COMMAND_ARG1 / 4U];
            shell->received = 0U;
            shell->stream_open = true;
            break;
        case MT_COMMAND_STREAM_END:
            assert(shell->reset_held && shell->stream_open && shell->received == shell->size);
            shell->stream_open = false;
            shell->loaded = true;
            break;
        default:
            assert(false);
        }
        shell->regs[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
        shell->regs[MT_REG_RESPONSE_SEQ / 4U] = value;
    } else {
        shell->regs[offset / 4U] = value;
    }
}

int main(void)
{
    static shell_t shell;
    mt_core_link_t link;
    shell.regs[MT_REG_CORE_ID / 4U] = MT_SPECTRUM_CORE_ID;
    shell.regs[MT_REG_ABI_VERSION / 4U] = MT_ABI_VERSION;
    mt_core_link_init(&link, &shell, read32, write32);

    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) != NULL);
    rom_size = 4096U;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) != NULL);
    assert(shell.reset_commands == 0U);

    rom_size = MT_SPECTRUM_ROM_48_BYTES;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(!shell.reset_held && shell.reset_commands == 2U && shell.size == rom_size);

    rom_size = MT_SPECTRUM_ROM_128_BYTES;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(!shell.reset_held && shell.reset_commands == 4U && shell.size == rom_size);
    return 0;
}
