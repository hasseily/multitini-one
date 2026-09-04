/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_boot.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_rom_files.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    unsigned calls;
    unsigned fail_call;
    unsigned short_call;
    unsigned uniform_call;
    const char *expected_paths[4];
    unsigned expected_path_count;
} rom_reader_t;

static mt_c64_rom_file_read_result_t read_rom_file(
    void *context, const char *path, uint8_t *destination,
    size_t expected_size)
{
    static const char *const expected_paths[] = {
        MT_C64_SD_BASIC_PATH,
        MT_C64_SD_KERNAL_PATH,
        MT_C64_SD_CHARACTER_PATH,
    };
    rom_reader_t *const reader = context;
    mt_c64_rom_file_read_result_t result = {0, 0U, expected_size};
    size_t index;

    assert(reader != NULL);
    if (reader->expected_path_count != 0U) {
        assert(reader->calls < reader->expected_path_count);
        assert(strcmp(path, reader->expected_paths[reader->calls]) == 0);
    } else {
        assert(reader->calls <
               sizeof(expected_paths) / sizeof(expected_paths[0]));
        assert(strcmp(path, expected_paths[reader->calls]) == 0);
    }
    ++reader->calls;

    if (reader->calls == reader->fail_call) {
        result.status = -7;
        result.backend_status = 91U;
        result.size = 0U;
        return result;
    }
    for (index = 0U; index < expected_size; ++index) {
        destination[index] = (uint8_t)(index + reader->calls);
    }
    if (reader->calls == reader->uniform_call) {
        memset(destination, 0x5AU, expected_size);
    }
    if (reader->calls == reader->short_call) {
        result.size = expected_size - 1U;
    }
    return result;
}

static void test_rom_file_paths_and_loading(void)
{
    static uint8_t bundle[MT_C64_SYSTEM_ROM_BUNDLE_SIZE];
    mt_c64_system_roms_t roms;
    mt_c64_rom_files_result_t result;
    rom_reader_t reader = {0};

    assert(strcmp(MT_SD_CORE_ROOT, "0:/multitini/cores") == 0);
    assert(strcmp(MT_C64_SD_MEDIA_ROOT,
                  "0:/multitini/cores/c64/media") == 0);
    assert(strcmp(mt_c64_sd_rom_path(MT_C64_ROM_BASIC),
                  "0:/multitini/cores/c64/roms/basic.bin") == 0);
    assert(strcmp(mt_c64_sd_rom_path(MT_C64_ROM_KERNAL),
                  "0:/multitini/cores/c64/roms/kernal.bin") == 0);
    assert(strcmp(mt_c64_sd_rom_path(MT_C64_ROM_CHARACTER),
                  "0:/multitini/cores/c64/roms/characters.bin") == 0);
    assert(strcmp(mt_c64_sd_rom_path(MT_C64_ROM_DRIVE_1541),
                  "0:/multitini/cores/c64/roms/1541.bin") == 0);
    assert(mt_c64_sd_rom_path((mt_c64_rom_kind_t)99) == NULL);
    assert(strcmp(mt_c64_fast_rom_paths()->basic,
                  MT_C64_SD_BASIC_PATH) == 0);
    assert(strcmp(mt_c64_fast_rom_paths()->kernal,
                  "0:/multitini/cores/c64/roms/fast/kernal.bin") == 0);
    assert(strcmp(mt_c64_fast_rom_paths()->character,
                  MT_C64_SD_CHARACTER_PATH) == 0);
    assert(strcmp(mt_c64_fast_rom_paths()->drive_1541,
                  "0:/multitini/cores/c64/roms/fast/1541.bin") == 0);
    assert(strcmp(mt_c64_stock_rom_paths()->drive_1541,
                  MT_C64_SD_1541_PATH) == 0);

    result = mt_c64_rom_files_load_system(
        read_rom_file, &reader, bundle, sizeof(bundle), &roms);
    assert(result.status == MT_C64_ROM_FILES_OK);
    assert(result.path == NULL);
    assert(reader.calls == 3U);
    assert(roms.basic.data == bundle);
    assert(roms.basic.size == MT_SIZE_C64_BASIC);
    assert(roms.kernal.data == bundle + MT_SIZE_C64_BASIC);
    assert(roms.kernal.size == MT_SIZE_C64_KERNAL);
    assert(roms.character.data ==
           bundle + MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL);
    assert(roms.character.size == MT_SIZE_C64_CHARACTER);

    memset(&reader, 0, sizeof(reader));
    reader.expected_paths[0] = MT_C64_SD_BASIC_PATH;
    reader.expected_paths[1] = MT_C64_SD_FAST_KERNAL_PATH;
    reader.expected_paths[2] = MT_C64_SD_CHARACTER_PATH;
    reader.expected_path_count = 3U;
    result = mt_c64_rom_files_load_system_from(
        mt_c64_fast_rom_paths(), read_rom_file, &reader, bundle,
        sizeof(bundle), &roms);
    assert(result.status == MT_C64_ROM_FILES_OK && reader.calls == 3U);

    memset(&reader, 0, sizeof(reader));
    reader.expected_paths[0] = MT_C64_SD_FAST_1541_PATH;
    reader.expected_path_count = 1U;
    result = mt_c64_rom_file_load(
        MT_C64_ROM_DRIVE_1541, MT_C64_SD_FAST_1541_PATH, read_rom_file,
        &reader, bundle, sizeof(bundle));
    assert(result.status == MT_C64_ROM_FILES_OK && reader.calls == 1U);

    memset(&reader, 0, sizeof(reader));
    reader.fail_call = 2U;
    result = mt_c64_rom_files_load_system(
        read_rom_file, &reader, bundle, sizeof(bundle), &roms);
    assert(result.status == MT_C64_ROM_FILES_READ_FAILED);
    assert(result.kind == MT_C64_ROM_KERNAL);
    assert(strcmp(result.path, MT_C64_SD_KERNAL_PATH) == 0);
    assert(result.read.status == -7);
    assert(result.read.backend_status == 91U);
    assert(reader.calls == 2U);
    assert(roms.basic.data == NULL);

    memset(&reader, 0, sizeof(reader));
    reader.short_call = 1U;
    result = mt_c64_rom_files_load_system(
        read_rom_file, &reader, bundle, sizeof(bundle), &roms);
    assert(result.status == MT_C64_ROM_FILES_SIZE_MISMATCH);
    assert(result.kind == MT_C64_ROM_BASIC);
    assert(result.read.size == MT_SIZE_C64_BASIC - 1U);
    assert(reader.calls == 1U);

    memset(&reader, 0, sizeof(reader));
    reader.uniform_call = 3U;
    result = mt_c64_rom_files_load_system(
        read_rom_file, &reader, bundle, sizeof(bundle), &roms);
    assert(result.status == MT_C64_ROM_FILES_VALIDATION_FAILED);
    assert(result.kind == MT_C64_ROM_CHARACTER);
    assert(result.validation == MT_CONTENT_BAD_STRUCTURE);
    assert(reader.calls == 3U);

    memset(&reader, 0, sizeof(reader));
    result = mt_c64_rom_files_load_system(
        read_rom_file, &reader, bundle, sizeof(bundle) - 1U, &roms);
    assert(result.status == MT_C64_ROM_FILES_INVALID_ARGUMENT);
    assert(reader.calls == 0U);
    assert(strcmp(mt_c64_rom_files_status_name(result.status),
                  "invalid argument") == 0);
    assert(mt_c64_rom_files_load_system(
               NULL, &reader, bundle, sizeof(bundle), &roms).status ==
           MT_C64_ROM_FILES_INVALID_ARGUMENT);
    assert(mt_c64_rom_files_load_system_from(
               NULL, read_rom_file, &reader, bundle, sizeof(bundle),
               &roms).status == MT_C64_ROM_FILES_INVALID_ARGUMENT);
    assert(mt_c64_rom_file_load(
               MT_C64_ROM_DRIVE_1541, MT_C64_SD_FAST_1541_PATH,
               read_rom_file, &reader, bundle,
               MT_SIZE_C64_1541_ROM - 1U).status ==
           MT_C64_ROM_FILES_INVALID_ARGUMENT);
}

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_kind;
    uint32_t active_size;
    uint32_t active_offset;
    uint32_t checksum;
    uint32_t completed_kinds;
    uint32_t reset_commands;
    uint32_t fail_data_at;
    int fail_data_enabled;
    int failure_injected;
    int core_reset;
} boot_shell_t;

static uint32_t read32(void *context, uint32_t offset)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    boot_shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
    const uint32_t arg1 = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
    uint32_t response_status = MT_RESPONSE_OK;

    if (sequence == shell->sequence) {
        return;
    }
    shell->sequence = sequence;

    switch (opcode) {
    case MT_COMMAND_CORE_RESET:
        shell->core_reset = arg0 != 0U;
        ++shell->reset_commands;
        break;
    case MT_COMMAND_STREAM_BEGIN:
        assert(shell->core_reset != 0);
        assert(shell->active_size == 0U);
        shell->active_kind = arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        assert(shell->active_size != 0U);
        assert(shell->active_offset < shell->active_size);
        if (shell->fail_data_enabled != 0 && shell->failure_injected == 0 &&
            shell->active_offset == shell->fail_data_at) {
            response_status = MT_RESPONSE_INVALID_ARGUMENT;
            shell->failure_injected = 1;
        } else {
            shell->checksum += arg0;
            ++shell->active_offset;
        }
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        shell->completed_kinds |= UINT32_C(1) << shell->active_kind;
        shell->active_kind = 0U;
        shell->active_size = 0U;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_ABORT:
        shell->active_kind = 0U;
        shell->active_size = 0U;
        shell->active_offset = 0U;
        break;
    default:
        assert(0);
    }

    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = response_status;
    shell->registers[MT_REG_RESPONSE_ARG0 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_ARG1 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

int main(void)
{
    uint8_t basic[MT_SIZE_C64_BASIC];
    uint8_t kernal[MT_SIZE_C64_KERNAL];
    uint8_t character[MT_SIZE_C64_CHARACTER];
    boot_shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_system_roms_t roms;
    uint32_t expected_checksum = 0U;
    size_t index;

    test_rom_file_paths_and_loading();

    for (index = 0U; index < sizeof(basic); ++index) {
        basic[index] = (uint8_t)index;
        expected_checksum += basic[index];
    }
    for (index = 0U; index < sizeof(kernal); ++index) {
        kernal[index] = (uint8_t)(index ^ 0xA5U);
        expected_checksum += kernal[index];
    }
    for (index = 0U; index < sizeof(character); ++index) {
        character[index] = (uint8_t)(index ^ 0x5AU);
        expected_checksum += character[index];
    }

    roms.basic.data = basic;
    roms.basic.size = sizeof(basic);
    roms.kernal.data = kernal;
    roms.kernal.size = sizeof(kernal);
    roms.character.data = character;
    roms.character.size = sizeof(character);

    mt_core_link_init(&link, &shell, read32, write32);
    assert(mt_c64_load_system_roms(&link, &roms, 2U, respond, &shell) ==
           MT_LINK_OK);
    assert(shell.core_reset == 0);
    assert(shell.reset_commands == 2U);
    assert(shell.completed_kinds ==
           ((UINT32_C(1) << MT_LOAD_C64_BASIC) |
            (UINT32_C(1) << MT_LOAD_C64_KERNAL) |
            (UINT32_C(1) << MT_LOAD_C64_CHARACTER)));
    assert(shell.checksum == expected_checksum);

    roms.basic.size = sizeof(basic) - 1U;
    assert(mt_c64_load_system_roms(&link, &roms, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(mt_c64_load_system_roms(&link, NULL, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);

    {
        boot_shell_t failing_shell = {0};
        mt_core_link_t failing_link;
        failing_shell.fail_data_at = 17U;
        failing_shell.fail_data_enabled = 1;
        mt_core_link_init(&failing_link, &failing_shell, read32, write32);
        roms.basic.size = sizeof(basic);
        assert(mt_c64_load_system_roms(&failing_link, &roms, 2U, respond,
                                       &failing_shell) ==
               MT_LINK_REMOTE_ERROR);
        assert(failing_shell.failure_injected != 0);
        assert(failing_shell.core_reset != 0);
        assert(failing_shell.reset_commands == 2U);
        assert(failing_shell.completed_kinds == 0U);
    }
    return 0;
}
