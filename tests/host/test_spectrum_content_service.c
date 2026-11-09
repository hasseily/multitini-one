/* SPDX-License-Identifier: GPL-3.0-only */
#include "spectrum_content_service.h"
#include "multitini_spectrum_interface.h"
#include "multitini_spectrum_tzx.h"
#include "sd_storage.h"
#include "sleep.h"

#include <assert.h>
#include <string.h>

#define CORE_DIRECTORY "0:/multitini/cores/zx-spectrum"
#define MEDIA_ROOT CORE_DIRECTORY "/media"
#define TAPE_MODE_PATH "0:/multitini/spectrum_tape.cfg"

typedef struct {
    uint32_t regs[MT_REGISTER_SPAN / 4U];
    unsigned reset_commands;
    unsigned tape_streams;
    uint32_t stream_kind;
    size_t size;
    size_t received;
    const uint8_t *expected_tape;
    size_t expected_tape_size;
    bool reset_held;
    bool stream_open;
    bool loaded;
    bool tape_config_supported;
    bool tzx_supported;
    bool reject_tape;
    unsigned tape_config_writes;
} shell_t;

static size_t rom_size;
static bool stock_checksum;
static uint8_t rom_data[MT_SPECTRUM_ROM_128_BYTES];
static uint8_t tape_setting[8U];
static size_t tape_setting_size;
static bool tape_setting_write_fault;
static unsigned tape_reads;
static const uint8_t tap_data[] = {3U, 0U, 0xffU, 0x11U, 0xeeU};
static const uint8_t tzx_data[] = {
    'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1aU, 1U, 20U,
    0x10U, 0xe8U, 0x03U, 3U, 0U, 0xffU, 0x11U, 0xeeU
};

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst, size_t capacity)
{
    if (strncmp(path, MEDIA_ROOT "/", sizeof(MEDIA_ROOT "/") - 1U) == 0) {
        const bool tap = strcmp(path, MEDIA_ROOT "/game.TAP") == 0;
        const bool broken = strcmp(path, MEDIA_ROOT "/broken.tzx") == 0;
        assert(tap || broken || strcmp(path, MEDIA_ROOT "/Games/game.TZX") == 0);
        const uint8_t *data = tap ? tap_data : tzx_data;
        const size_t size = tap ? sizeof(tap_data) : sizeof(tzx_data) - (broken ? 1U : 0U);
        assert(capacity >= size);
        ++tape_reads;
        memcpy(dst, data, size);
        return (mt_sd_result_t){.status = MT_SD_OK, .size = size};
    }
    assert(strcmp(path, CORE_DIRECTORY "/roms/spectrum.rom") == 0);
    assert(capacity == MT_SPECTRUM_ROM_128_BYTES && rom_size <= capacity);
    if (rom_size == 0U) return (mt_sd_result_t){.status = MT_SD_OPEN_FAILED};
    memset(rom_data, 0x5A, rom_size);
    rom_data[rom_size - 1U] = 0xA5U;
    if (stock_checksum) {
        /* Synthetic test ROMs with a matching CRC and no original ROM bytes. */
        static const uint8_t suffix_48k[] = {0x67, 0x9a, 0x3c, 0xa5};
        static const uint8_t suffix_128k[] = {0x5d, 0xff, 0xd1, 0xdd};
        memcpy(rom_data + rom_size - sizeof(suffix_48k),
               rom_size == MT_SPECTRUM_ROM_48_BYTES ? suffix_48k : suffix_128k,
               sizeof(suffix_48k));
    }
    memcpy(dst, rom_data, rom_size);
    return (mt_sd_result_t){.status = MT_SD_OK, .size = rom_size};
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal,
                                             uint8_t *buffer, size_t capacity)
{
    assert(strcmp(path, TAPE_MODE_PATH) == 0 && strcmp(journal, TAPE_MODE_PATH ".mtj") == 0);
    assert(buffer != NULL && capacity == sizeof(tape_setting));
    return (mt_sd_result_t){.status = MT_SD_OK};
}

mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *path, uint8_t *dst,
                                                    size_t capacity, bool *found)
{
    assert(strcmp(path, TAPE_MODE_PATH) == 0 && capacity == sizeof(tape_setting));
    *found = tape_setting_size != 0U;
    memcpy(dst, tape_setting, tape_setting_size);
    return (mt_sd_result_t){.status = MT_SD_OK, .size = tape_setting_size};
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal,
                                              const uint8_t *src, size_t size)
{
    assert(strcmp(path, TAPE_MODE_PATH) == 0 && strcmp(journal, TAPE_MODE_PATH ".mtj") == 0);
    assert(size <= sizeof(tape_setting));
    if (tape_setting_write_fault) return (mt_sd_result_t){.status = MT_SD_WRITE_FAILED};
    memcpy(tape_setting, src, size);
    tape_setting_size = size;
    return (mt_sd_result_t){.status = MT_SD_OK, .size = size};
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
    if (offset == MT_REG_SPECTRUM_TAPE_CONFIG)
        return shell->tape_config_supported ?
            MT_SPECTRUM_TAPE_SIGNATURE | shell->regs[offset / 4U] |
                (shell->tzx_supported ? MT_SPECTRUM_TAPE_TZX : 0U) : 0U;
    if (offset == MT_REG_STREAM_STATUS && shell->stream_open)
        return shell->received < shell->size ? MT_STREAM_STATUS_READY : MT_STREAM_STATUS_DRAINED;
    return shell->regs[offset / 4U];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_DATA_BURST) {
        assert(shell->stream_open && shell->received < shell->size);
        const uint8_t *expected = shell->stream_kind == MT_LOAD_SPECTRUM_ROM ?
            rom_data : shell->expected_tape;
        for (unsigned lane = 0U; lane < MT_STREAM_BURST_BYTES && shell->received < shell->size; ++lane) {
            assert((uint8_t)(value >> (lane * 8U)) == expected[shell->received++]);
        }
    } else if (offset == MT_REG_SPECTRUM_TAPE_CONFIG) {
        assert(shell->tape_config_supported);
        assert((value & MT_SPECTRUM_TAPE_TZX) == 0U);
        ++shell->tape_config_writes;
        shell->regs[offset / 4U] = value;
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
            assert(!shell->stream_open);
            shell->size = shell->regs[MT_REG_COMMAND_ARG1 / 4U];
            if (arg == MT_LOAD_SPECTRUM_ROM) {
                assert(shell->reset_held && shell->size == rom_size);
            } else {
                assert(!shell->reset_held && (arg == MT_LOAD_SPECTRUM_TAP ||
                    (arg == MT_LOAD_SPECTRUM_TZX && shell->tzx_supported)));
                assert(shell->expected_tape != NULL && shell->size == shell->expected_tape_size);
                ++shell->tape_streams;
            }
            shell->stream_kind = arg;
            shell->received = 0U;
            shell->stream_open = true;
            break;
        case MT_COMMAND_STREAM_END:
            assert(shell->stream_open && shell->received == shell->size);
            shell->stream_open = false;
            if (shell->stream_kind == MT_LOAD_SPECTRUM_ROM) shell->loaded = true;
            else if (shell->reject_tape)
                shell->regs[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_LOAD_ERROR;
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

static void test_tape_selection(mt_core_link_t *link, shell_t *shell)
{
    shell->expected_tape = tap_data;
    shell->expected_tape_size = sizeof(tap_data);
    assert(spectrum_content_insert_tape(link, MEDIA_ROOT, "game.TAP", NULL) == NULL);
    assert(strcmp(spectrum_content_tape_name(), "game.TAP") == 0 &&
           shell->stream_kind == MT_LOAD_SPECTRUM_TAP);

    const char *error = spectrum_content_insert_tape(link, MEDIA_ROOT, "Games/game.TZX", NULL);
    assert(error != NULL && strstr(error, "Update the Spectrum FPGA") != NULL);
    assert(tape_reads == 1U && shell->tape_streams == 1U &&
           strcmp(spectrum_content_tape_name(), "game.TAP") == 0);

    shell->tzx_supported = true;
    assert((spectrum_content_tape_config(link) & MT_SPECTRUM_TAPE_TZX) != 0U);
    assert(spectrum_content_insert_tape(link, MEDIA_ROOT, "broken.tzx", NULL) != NULL);
    assert(tape_reads == 2U && shell->tape_streams == 1U &&
           strcmp(spectrum_content_tape_name(), "game.TAP") == 0);

    static uint8_t compiled[MT_SPECTRUM_TZX_MAX_BYTES];
    assert(mt_spectrum_tzx_compile(tzx_data, sizeof(tzx_data), compiled, sizeof(compiled),
                                   &shell->expected_tape_size) == MT_SPECTRUM_TZX_OK);
    shell->expected_tape = compiled;
    assert(spectrum_content_insert_tape(link, MEDIA_ROOT, "Games/game.TZX", NULL) == NULL);
    assert(tape_reads == 3U && shell->tape_streams == 2U &&
           shell->stream_kind == MT_LOAD_SPECTRUM_TZX &&
           strcmp(spectrum_content_tape_name(), "Games/game.TZX") == 0);

    shell->reject_tape = true;
    assert(spectrum_content_insert_tape(link, MEDIA_ROOT, "Games/game.TZX", NULL) != NULL);
    assert(shell->tape_streams == 3U && spectrum_content_tape_name() == NULL);
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
    assert(spectrum_content_next_tape_mode(&link) != NULL);
    assert(shell.tape_config_writes == 0U && tape_setting_size == 0U);

    shell.tape_config_supported = true;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(spectrum_content_tape_config(&link) == MT_SPECTRUM_TAPE_SIGNATURE);
    assert(spectrum_content_next_tape_mode(&link) == NULL);
    assert(spectrum_content_tape_config(&link) == (MT_SPECTRUM_TAPE_SIGNATURE | MT_SPECTRUM_TAPE_FAST));
    assert(tape_setting_size == 5U && memcmp(tape_setting, "fast\n", 5U) == 0);
    assert(spectrum_content_next_tape_mode(&link) == NULL);
    assert(spectrum_content_tape_config(&link) == MT_SPECTRUM_TAPE_SIGNATURE);
    assert(spectrum_content_next_tape_mode(&link) == NULL);

    stock_checksum = true;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    const uint32_t stock_config = MT_SPECTRUM_TAPE_SIGNATURE | MT_SPECTRUM_TAPE_STOCK_ROM;
    assert(spectrum_content_tape_config(&link) == (stock_config | MT_SPECTRUM_TAPE_FAST));
    assert(spectrum_content_next_tape_mode(&link) == NULL);
    assert(spectrum_content_tape_config(&link) == (stock_config | MT_SPECTRUM_TAPE_INSTANT));
    tape_setting_write_fault = true;
    assert(spectrum_content_next_tape_mode(&link) != NULL);
    assert(spectrum_content_tape_config(&link) == (stock_config | MT_SPECTRUM_TAPE_INSTANT));
    tape_setting_write_fault = false;

    rom_size = MT_SPECTRUM_ROM_48_BYTES;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(spectrum_content_tape_config(&link) == (stock_config | MT_SPECTRUM_TAPE_INSTANT));
    stock_checksum = false;
    assert(spectrum_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(spectrum_content_tape_config(&link) == (MT_SPECTRUM_TAPE_SIGNATURE | MT_SPECTRUM_TAPE_INSTANT));
    assert(spectrum_content_next_tape_mode(&link) == NULL);
    assert(spectrum_content_tape_config(&link) == (MT_SPECTRUM_TAPE_SIGNATURE | MT_SPECTRUM_TAPE_FAST));
    test_tape_selection(&link, &shell);
    return 0;
}
