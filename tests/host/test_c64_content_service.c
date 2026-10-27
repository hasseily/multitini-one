/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_content_service.h"
#include "multitini_c64_content.h"
#include "multitini_c64_rom_files.h"

#include "multitini_c64_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_size;
    uint32_t active_offset;
    uint8_t active_kind;
    uint8_t completed[10];
    size_t completed_count;
    int raise_load_error_on_end;
    int stall_stream_data;
    uint32_t abort_commands;
    uint32_t reset_asserts;
    uint32_t reset_releases;
    uint32_t writes;
    uint32_t core_load_error_clears;
    bool media_request_active;
    uint32_t media_request_cancellations;
    uint32_t expansion_reads;
    unsigned drain_delay, drain_polls, commits;
    bool require_handoff, fail_reset, fail_commit, never_ack;
    bool fail_stream_begin, fail_first_data, stall_after_prefix;
    uint32_t journal[16];
    unsigned journal_head, journal_count, journal_pops;
} shell_t;

static char requested_path[512];
static int fail_read;
static uint64_t fake_time_ms;
static uint32_t fake_time_calls;
static uint32_t sd_reads;

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    if (offset == MT_REG_STREAM_STATUS) {
        if (shell->stall_after_prefix && shell->active_offset >= 5U) return 0U;
        if (!shell->active_size) return 0U;
        if (shell->active_offset < shell->active_size) return MT_STREAM_STATUS_READY;
        return shell->drain_delay || shell->never_ack ? 0U : MT_STREAM_STATUS_DRAINED;
    }
    if (offset == MT_REG_STATUS) {
        if (shell->drain_delay) {
            assert(shell->registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST);
            ++shell->drain_polls;
            --shell->drain_delay;
        }
        const uint32_t status = shell->registers[offset / sizeof(uint32_t)] &
                                ~MT_STATUS_STREAM_READY;
        return status | ((!shell->drain_delay && !shell->never_ack) ?
                          MT_STATUS_STREAM_READY : 0U);
    }
    if (offset == MT_REG_C64_EXPANSION_CONFIG || offset == MT_REG_C64_EXPANSION_CAPS)
        ++shell->expansion_reads;
    if (offset == MT_REG_C64_EF_JOURNAL)
        return shell->journal_count != 0U ? shell->journal[shell->journal_head] : 0U;
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    ++shell->writes;
    if (offset == MT_REG_C64_EF_JOURNAL) {
        /* The FPGA pops only when the exact head word is written back. */
        assert(shell->journal_count != 0U && value == shell->journal[shell->journal_head]);
        shell->journal_head = (shell->journal_head + 1U) % 16U;
        --shell->journal_count;
        ++shell->journal_pops;
        return;
    }
    if (offset == MT_REG_STREAM_DATA_BURST) {
        const uint32_t remaining = shell->active_size - shell->active_offset;
        assert(remaining != 0U);
        assert(!shell->drain_delay && !shell->never_ack);
        shell->active_offset += remaining < MT_STREAM_BURST_BYTES ? remaining : MT_STREAM_BURST_BYTES;
        return;
    }
    if (offset == MT_REG_ERROR && (value & MT_ERROR_CORE_LOAD) != 0U) {
        ++shell->core_load_error_clears;
        shell->registers[MT_REG_STATUS / 4U] &=
            ~MT_STATUS_CORE_LOAD_ERROR;
        shell->registers[MT_REG_ERROR / 4U] &= ~MT_ERROR_CORE_LOAD;
        /* The shared clear_error reaches the D64 track receiver too. It
         * cancels a live request even though the shell's previously latched
         * media descriptor still identifies that request. */
        if (shell->media_request_active) {
            shell->media_request_active = false;
            ++shell->media_request_cancellations;
        }
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
        if (shell->fail_stream_begin) {
            shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_IO_ERROR;
            shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
            return;
        }
        shell->active_kind =
            (uint8_t)shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->active_size = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        if (shell->fail_first_data && shell->active_offset == 0) {
            shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_IO_ERROR;
            shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
            return;
        }
        if (shell->stall_stream_data != 0) {
            shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_BUSY;
            shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
            return;
        }
        ++shell->active_offset;
        if (shell->require_handoff && shell->active_offset == 1) {
            assert(shell->commits == 1);
            assert(shell->registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST);
            shell->drain_delay = 7;
        }
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        assert(!shell->drain_delay && !shell->never_ack);
        assert(shell->completed_count <
               sizeof(shell->completed) / sizeof(shell->completed[0]));
        shell->completed[shell->completed_count++] = shell->active_kind;
        shell->active_size = 0U;
        if (shell->raise_load_error_on_end != 0)
            shell->registers[MT_REG_STATUS / 4U] |=
                MT_STATUS_CORE_LOAD_ERROR;
        break;
    case MT_COMMAND_STREAM_ABORT:
        ++shell->abort_commands;
        shell->active_size = 0U;
        shell->drain_delay = 0U;
        break;
    case MT_COMMAND_CORE_RESET:
        if (shell->fail_reset) {
            shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_IO_ERROR;
            shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
            return;
        }
        if (shell->registers[MT_REG_COMMAND_ARG0 / 4U] != 0U) {
            ++shell->reset_asserts;
            shell->registers[MT_REG_CONTROL / 4U] |=
                MT_CONTROL_CORE_RESET_REQUEST;
        } else {
            if (shell->require_handoff && !shell->fail_commit && shell->active_size) {
                assert(shell->active_offset >= 1 && shell->drain_delay == 0 &&
                       shell->drain_polls >= 7);
            }
            ++shell->reset_releases;
            shell->registers[MT_REG_CONTROL / 4U] &=
                ~MT_CONTROL_CORE_RESET_REQUEST;
        }
        break;
    case MT_COMMAND_MEDIA_COMPLETE:
        assert(shell->media_request_active);
        assert(shell->registers[MT_REG_COMMAND_ARG0 / 4U] ==
               shell->registers[MT_REG_MEDIA_REQUEST_ID / 4U]);
        shell->media_request_active = false;
        shell->registers[MT_REG_MEDIA_REQUEST / 4U] = 0U;
        shell->registers[MT_REG_STATUS / 4U] &= ~MT_STATUS_MEDIA_REQUEST;
        break;
    default:
        assert(0);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

static uint64_t fake_monotonic_ms(void *context)
{
    (void)context;
    ++fake_time_calls;
    fake_time_ms += UINT64_C(1000);
    return fake_time_ms;
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

static const struct {
    const char *name;
    uint16_t type, bank_size;
    unsigned banks;
    uint8_t exrom, game, allowed;
} rom_extensions[] = {
    {"westermann.crt", MT_CRT_WESTERMANN, 0x4000U, 1U, 0U, 0U, 0U},
    {"kingsoft.crt", MT_CRT_KINGSOFT_BASIC, 0x2000U, 3U, 0U, 0U, 7U},
    {"blackbox8.crt", MT_CRT_BLACKBOX_V8, 0x4000U, 4U, 0U, 0U, 0U},
    {"blackbox3.crt", MT_CRT_BLACKBOX_V3, 0x2000U, 1U, 0U, 1U, 0U},
    {"blackbox4.crt", MT_CRT_BLACKBOX_V4, 0x4000U, 1U, 0U, 0U, 0U},
    {"blackbox9.crt", MT_CRT_BLACKBOX_V9, 0x4000U, 2U, 1U, 0U, 7U},
    {"one-megabyter.crt", MT_CRT_MEGABYTER, 0x2000U, 4U, 0U, 1U, 7U},
    {"two-megabyter.crt", MT_CRT_TWO_MEGABYTER, 0x4000U, 4U, 0U, 0U, 7U},
};

static uint8_t sav_store[MT_C64_EASYFLASH_IMAGE_SIZE];
static bool sav_present, fail_save;
static unsigned sav_commits;
static char sav_path[512];

/* One exported file at a time: a temporary file renamed over its target. */
struct mt_sd_file { int unused; };
static struct mt_sd_file export_file;
static uint8_t export_store[MT_C64_EASYFLASH_STREAM_SIZE];
static size_t export_size;
static char export_temp_path[512], export_final_path[512];
static bool export_final_exists, export_open;
static unsigned export_removes, export_renames;

mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **file)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(strcmp(mode, "wb") == 0 && !export_open && strlen(path) < sizeof export_temp_path);
    strcpy(export_temp_path, path);
    export_size = 0U;
    export_open = true;
    *file = &export_file;
    return result;
}

mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *file, const void *data, size_t size)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, size};
    assert(file == &export_file && export_open && export_size + size <= sizeof export_store);
    memcpy(export_store + export_size, data, size);
    export_size += size;
    return result;
}

mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(file == &export_file && export_open);
    export_open = false;
    return result;
}

mt_sd_result_t mt_sd_file_stat(const char *path, mt_sd_info_t *info)
{
    mt_sd_result_t result = {MT_SD_OPEN_FAILED, 0U, 0U};
    (void)info;
    if (export_final_exists && strcmp(path, export_final_path) == 0) result.status = MT_SD_OK;
    return result;
}

mt_sd_result_t mt_sd_file_remove(const char *path)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(export_final_exists && strcmp(path, export_final_path) == 0);
    export_final_exists = false;
    ++export_removes;
    return result;
}

mt_sd_result_t mt_sd_file_rename(const char *old_path, const char *new_path)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(!export_open && strcmp(old_path, export_temp_path) == 0 && !export_final_exists);
    strcpy(export_final_path, new_path);
    export_final_exists = true;
    ++export_renames;
    return result;
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t recovery_capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)journal_path;
    (void)recovery_buffer;
    (void)recovery_capacity;
    assert(strlen(path) < sizeof(sav_path));
    strcpy(sav_path, path);
    return result;
}

mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)path;
    *found = sav_present;
    if (sav_present) {
        assert(expected_size == sizeof sav_store);
        memcpy(dst, sav_store, expected_size);
        result.size = expected_size;
    }
    return result;
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src, size_t size)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)path;
    (void)journal_path;
    if (fail_save) {
        result.status = MT_SD_WRITE_FAILED;
        return result;
    }
    assert(size == sizeof sav_store);
    memcpy(sav_store, src, size);
    sav_present = true;
    ++sav_commits;
    return result;
}

/* Sparse EasyFlash: bank 0 ROML, then bank 9 ROMH loaded at $E000. */
static size_t make_easyflash(uint8_t *data)
{
    memset(data, 0, 0x40U);
    memcpy(data, "C64 CARTRIDGE   ", 16U);
    be32(data + 0x10U, 0x40U);
    be16(data + 0x14U, 0x0100U);
    be16(data + 0x16U, MT_CRT_EASYFLASH);
    data[0x18U] = 1U;
    for (unsigned packet = 0U; packet < 2U; ++packet) {
        uint8_t *chip = data + 0x40U + packet * 0x2010U;
        memcpy(chip, "CHIP", 4U);
        be32(chip + 4U, 0x2010U);
        be16(chip + 8U, 2U);
        be16(chip + 10U, packet == 0U ? 0U : 9U);
        be16(chip + 12U, packet == 0U ? 0x8000U : 0xe000U);
        be16(chip + 14U, 0x2000U);
        memset(chip + 0x10U, packet == 0U ? 0x10 : 0x99, 0x2000U);
    }
    return 0x40U + 2U * 0x2010U;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    ++sd_reads;
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    static uint8_t crt[0x40U + 4U * (0x10U + 0x4000U)];
    assert(strlen(path) < sizeof(requested_path));
    strcpy(requested_path, path);
    if (fail_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 7U;
        return result;
    }
    if (strstr(path, "easyflash") != NULL) {
        result.size = make_easyflash(dst);
        assert(capacity >= result.size);
        return result;
    }
    if (strstr(path, ".prg") != NULL) {
        static const uint8_t prg[] = {0x01U, 0x08U, 0x00U};
        /* Match the reported program's size and load address without copying
         * any demo content into the repository. This ends on a partial burst
         * after the separately accepted first byte. */
        result.size = strstr(path, "udemo2026.prg") ? 37410U : sizeof(prg);
        assert(capacity >= result.size);
        memset(dst, 0, result.size);
        memcpy(dst, prg, sizeof(prg));
    } else {
        const uint16_t type = strstr(path, "unsupported") != NULL ?
                                  36U : strstr(path, "epyx") ? MT_CRT_EPYX_FASTLOAD : MT_CRT_MAGIC_DESK;
        result.size = make_crt(crt, type);
        if (strstr(path, "fc3")) {
            memset(crt, 0, sizeof crt);
            memcpy(crt, "C64 CARTRIDGE   ", 16);
            be32(crt + 0x10, 0x40); be16(crt + 0x14, 0x100);
            be16(crt + 0x16, MT_CRT_FINAL_CARTRIDGE_3);
            crt[0x1a] = strstr(path, "fc3-reu") ? 1U : 0U;
            for (unsigned bank = 0; bank < 4; ++bank) {
                uint8_t *chip = crt + 0x40 + bank * 0x4010;
                memcpy(chip, "CHIP", 4); be32(chip + 4, 0x4010);
                be16(chip + 10, (uint16_t)bank); be16(chip + 12, 0x8000);
                be16(chip + 14, 0x4000);
            }
            result.size = sizeof crt;
        }
        for (size_t i = 0U; i < sizeof rom_extensions / sizeof rom_extensions[0]; ++i) {
            if (!strstr(path, rom_extensions[i].name)) continue;
            memset(crt, 0, sizeof crt);
            memcpy(crt, "C64 CARTRIDGE   ", 16U);
            be32(crt + 0x10U, 0x40U); be16(crt + 0x14U, 0x100U);
            be16(crt + 0x16U, rom_extensions[i].type);
            crt[0x18U] = rom_extensions[i].exrom;
            crt[0x19U] = rom_extensions[i].game;
            for (unsigned bank = 0U; bank < rom_extensions[i].banks; ++bank) {
                uint8_t *chip = crt + 0x40U + bank * (0x10U + rom_extensions[i].bank_size);
                memcpy(chip, "CHIP", 4U); be32(chip + 4U, 0x10U + rom_extensions[i].bank_size);
                be16(chip + 10U, (uint16_t)bank); be16(chip + 12U, 0x8000U);
                be16(chip + 14U, rom_extensions[i].bank_size);
            }
            result.size = 0x40U + rom_extensions[i].banks * (0x10U + rom_extensions[i].bank_size);
        }
        assert(capacity >= result.size);
        memcpy(dst, crt, result.size);
    }
    return result;
}

static void test_reu_excludes_cartridges(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = 23U;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_16MB;
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = MT_C64_EXPANSION_CONFIG_DEFAULT;
    const uint32_t reads_before = sd_reads;
    assert(c64_content_service_load("test.crt").status == MT_C64_CONTENT_SERVICE_REU_CONFLICT);
    assert(sd_reads == reads_before && shell.writes == 0U && !c64_content_service_crt_attached());
    assert(strstr(c64_content_service_status_name(MT_C64_CONTENT_SERVICE_REU_CONFLICT), "REU off") != NULL);
    assert(c64_content_service_load("test.prg").status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = 3U;
    assert(c64_content_service_load("test.crt").status == MT_C64_CONTENT_SERVICE_OK);
    assert(c64_content_service_crt_attached());
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = MT_C64_EXPANSION_CONFIG_DEFAULT;
    const uint32_t writes_before = shell.writes;
    assert(c64_content_service_load("replacement.crt").status == MT_C64_CONTENT_SERVICE_REU_CONFLICT);
    assert(shell.writes == writes_before && c64_content_service_crt_attached());
    assert(strcmp(c64_content_service_crt_name(), "test.crt") == 0);
    for (unsigned abi = 0U; abi < 23U; ++abi) {
        shell.registers[MT_REG_ABI_VERSION / 4U] = abi;
        shell.expansion_reads = 0U;
        assert(!c64_content_service_reu_enabled() && shell.expansion_reads == 0U);
    }
    shell.registers[MT_REG_ABI_VERSION / 4U] = 0x10017U;
    assert(!c64_content_service_reu_enabled() && shell.expansion_reads == 0U);
    shell.registers[MT_REG_ABI_VERSION / 4U] = 23U;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = MT_C64_EXPANSION_CAPS_REU_16MB;
    assert(!c64_content_service_reu_enabled());
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_DUAL_SID;
    assert(!c64_content_service_reu_enabled());
    shell.registers[MT_REG_CORE_ID / 4U] = 0U;
    shell.expansion_reads = 0U;
    assert(!c64_content_service_reu_enabled() && shell.expansion_reads == 0U);
    c64_content_service_init(NULL, NULL, NULL, NULL, NULL);
    assert(!c64_content_service_reu_enabled());
}

static void test_cartridge_expansion_policy(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_MINOR;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = MT_C64_EXPANSION_CAPS_VALID |
        MT_C64_EXPANSION_CAPS_REU_16MB | MT_C64_EXPANSION_CAPS_CART_COEXIST;
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = MT_C64_EXPANSION_CONFIG_DEFAULT;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = MT_C64_REU_HOST_ACTIVE;
    assert(c64_content_service_crt_expansion_policy() == 7 && c64_content_service_reu_enabled());
    shell.registers[MT_REG_C64_REU_HOST / 4U] = 0;
    assert(!c64_content_service_reu_enabled() && !c64_content_service_reu_load_allowed());
    shell.registers[MT_REG_C64_REU_HOST / 4U] = MT_C64_REU_HOST_ACTIVE;
    assert(c64_content_service_load("epyx.crt").status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = 0;
    assert(c64_content_service_crt_expansion_policy() == 0 && !c64_content_service_reu_enabled());
    assert(shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] & MT_C64_EXPANSION_REU_ENABLE);
    mt_c64_content_service_result_t result = c64_content_service_load("fc3-reu.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK && result.content.subtype == 1);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = MT_C64_REU_HOST_ACTIVE;
    assert(c64_content_service_crt_expansion_policy() == 3 && c64_content_service_reu_enabled());
    assert(c64_content_service_load("fc3.crt").status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = 0;
    assert(c64_content_service_crt_expansion_policy() == 0 && !c64_content_service_reu_enabled());
    assert(c64_content_service_detach_crt().status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = MT_C64_REU_HOST_ACTIVE;
    assert(c64_content_service_reu_enabled() && c64_content_service_crt_expansion_policy() == 7);
    /* A failed replacement loses the previous cartridge, not the requested REU. */
    assert(c64_content_service_load("epyx.crt").status == MT_C64_CONTENT_SERVICE_OK);
    shell.registers[MT_REG_C64_REU_HOST / 4U] = 0;
    shell.fail_stream_begin = true;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(c64_content_service_crt_attached() && !strcmp(c64_content_service_crt_name(), "epyx.crt"));
    assert(c64_content_service_crt_expansion_policy() == 0 && !c64_content_service_reu_load_allowed());
    shell.fail_stream_begin = false;
    shell.fail_first_data = true;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(c64_content_service_crt_attached() && !strcmp(c64_content_service_crt_name(), "epyx.crt"));
    assert(!c64_content_service_reu_enabled());
    shell.fail_first_data = false;
    shell.stall_after_prefix = true;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(!c64_content_service_crt_attached());
    /* Hardware status remains authoritative even after software ownership was
     * dropped, e.g. while a failed transaction/reset has not settled. */
    assert(!c64_content_service_reu_enabled() && !c64_content_service_reu_load_allowed());
    shell.stall_after_prefix = false;
    shell.registers[MT_REG_C64_REU_HOST / 4U] = MT_C64_REU_HOST_ACTIVE;
    shell.raise_load_error_on_end = true;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(!c64_content_service_crt_attached() && c64_content_service_reu_enabled());
    shell.raise_load_error_on_end = false;
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = 0;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] &= ~MT_C64_EXPANSION_CAPS_CART_COEXIST;
    result = c64_content_service_load("fc3-reu.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED &&
           result.content.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
}

static void test_rom_extension_capability_profile(size_t i)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_MINOR;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    const uint32_t caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_16MB;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = caps;
    assert(c64_content_service_load("magic.crt").status == MT_C64_CONTENT_SERVICE_OK);
    const uint32_t writes = shell.writes;
    for (unsigned coexist = 0U; coexist < 2U; ++coexist) {
        shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = caps |
            (coexist ? MT_C64_EXPANSION_CAPS_CART_COEXIST : 0U);
        const mt_c64_content_service_result_t result =
            c64_content_service_load(rom_extensions[i].name);
        assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED);
        assert(result.content.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
        assert(result.content.hardware_type == rom_extensions[i].type && result.content.subtype == 0U);
        assert(shell.writes == writes && shell.completed_count == 1U);
        assert(c64_content_service_crt_attached());
        assert(strcmp(c64_content_service_crt_name(), "magic.crt") == 0);
    }
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] |= MT_C64_EXPANSION_CAPS_CART_ROM_EXT;
    shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] = MT_C64_EXPANSION_CONFIG_DEFAULT;
    shell.registers[MT_REG_C64_REU_HOST / 4U] = rom_extensions[i].allowed ? MT_C64_REU_HOST_ACTIVE : 0U;
    const mt_c64_content_service_result_t result =
        c64_content_service_load(rom_extensions[i].name);
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(result.content.hardware_type == rom_extensions[i].type && result.content.subtype == 0U);
    assert(c64_content_service_crt_attached() && shell.completed_count == 2U);
    assert(c64_content_service_crt_expansion_policy() == rom_extensions[i].allowed);
    assert(c64_content_service_reu_enabled() == (rom_extensions[i].allowed != 0U));
    assert(shell.registers[MT_REG_C64_EXPANSION_CONFIG / 4U] == MT_C64_EXPANSION_CONFIG_DEFAULT);
}

static void test_rom_extension_capability(void)
{
    for (size_t i = 0U; i < sizeof rom_extensions / sizeof rom_extensions[0]; ++i)
        test_rom_extension_capability_profile(i);
}

static void test_run_preserves_pending_media_request(void)
{
    static const uint8_t prg[] = {1U, 8U, 0U, 0U};
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;
    mt_command_response_t response;
    const uint32_t complete_args[4] = {39U, 0U, 19U, 12U};
    uint32_t clears_before;
    uint32_t resets_before;

    mt_core_link_init(&link, &shell, read32, write32);
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    result = c64_content_service_stage_prg_image(prg, sizeof(prg));
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(c64_content_service_prg_staged());
    clears_before = shell.core_load_error_clears;
    resets_before = shell.reset_asserts + shell.reset_releases;

    /* Remount starts an ordinary directory-track fetch between STAGE and
     * RUN. There is no fault to clear, and the request must remain live. */
    shell.media_request_active = true;
    shell.registers[MT_REG_STATUS / 4U] |= MT_STATUS_MEDIA_REQUEST;
    shell.registers[MT_REG_MEDIA_STATE / 4U] = MT_MEDIA_STATE_MOUNTED;
    shell.registers[MT_REG_MEDIA_REQUEST / 4U] = MT_MEDIA_REQUEST_VALID;
    shell.registers[MT_REG_MEDIA_REQUEST_ID / 4U] = complete_args[0];
    shell.registers[MT_REG_MEDIA_REQUEST_LBA / 4U] = 357U;
    shell.registers[MT_REG_MEDIA_REQUEST_BLOCK_COUNT / 4U] = complete_args[2];
    shell.registers[MT_REG_MEDIA_REQUEST_GENERATION / 4U] = complete_args[3];
    result = c64_content_service_run_staged_prg();
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(!c64_content_service_prg_staged());
    assert(shell.completed_count == 2U);
    assert(shell.completed[1] == MT_LOAD_C64_PRG_RUN);
    assert(shell.core_load_error_clears == clears_before);
    assert(shell.reset_asserts + shell.reset_releases == resets_before);
    assert(shell.media_request_active && shell.media_request_cancellations == 0U);
    assert(shell.registers[MT_REG_MEDIA_REQUEST_ID / 4U] == complete_args[0]);
    assert(shell.registers[MT_REG_MEDIA_REQUEST_LBA / 4U] == 357U);
    assert(shell.registers[MT_REG_MEDIA_REQUEST_BLOCK_COUNT / 4U] == complete_args[2]);
    assert(shell.registers[MT_REG_MEDIA_REQUEST_GENERATION / 4U] == complete_args[3]);
    assert(shell.registers[MT_REG_MEDIA_STATE / 4U] == MT_MEDIA_STATE_MOUNTED);
    /* The preserved request still accepts its original completion ID. */
    assert(mt_core_link_command(&link, MT_COMMAND_MEDIA_COMPLETE,
               complete_args, 100U, respond, &shell, &response) == MT_LINK_OK);
    assert(!shell.media_request_active);
    assert(shell.media_request_cancellations == 0U);
}

static void test_run_refuses_existing_core_fault(void)
{
    static const uint8_t prg[] = {1U, 8U, 0U, 0U};
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;
    uint32_t writes_before;
    uint32_t sequence_before;

    mt_core_link_init(&link, &shell, read32, write32);
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    result = c64_content_service_stage_prg_image(prg, sizeof(prg));
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    /* A mounted-drive failure must be handled by its recovery path. RUN
     * cannot acknowledge a shared error or release a held reset for it. */
    shell.registers[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_LOAD_ERROR;
    shell.registers[MT_REG_ERROR / 4U] = MT_ERROR_CORE_LOAD;
    shell.registers[MT_REG_CONTROL / 4U] = MT_CONTROL_CORE_RESET_REQUEST;
    shell.registers[MT_REG_MEDIA_STATE / 4U] = MT_MEDIA_STATE_MOUNTED;
    shell.registers[MT_REG_MEDIA_REQUEST_ID / 4U] = 51U;
    shell.media_request_active = true;
    writes_before = shell.writes;
    sequence_before = shell.sequence;
    result = c64_content_service_run_staged_prg();
    assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
    assert(result.link == MT_LINK_REMOTE_ERROR);
    assert(c64_content_service_prg_staged());
    assert(shell.completed_count == 1U);
    assert(shell.completed[0] == MT_LOAD_C64_PRG_STAGE);
    assert(shell.writes == writes_before && shell.sequence == sequence_before);
    assert((shell.registers[MT_REG_STATUS / 4U] & MT_STATUS_CORE_LOAD_ERROR) != 0U);
    assert(shell.registers[MT_REG_ERROR / 4U] == MT_ERROR_CORE_LOAD);
    assert(shell.registers[MT_REG_CONTROL / 4U] == MT_CONTROL_CORE_RESET_REQUEST);
    assert(shell.registers[MT_REG_MEDIA_STATE / 4U] == MT_MEDIA_STATE_MOUNTED);
    assert(shell.registers[MT_REG_MEDIA_REQUEST_ID / 4U] == 51U);
    assert(shell.media_request_active && shell.media_request_cancellations == 0U);
}

static void test_nested_package_paths(void)
{
    static const char *const bad[] = {"../x.prg", "Games/../x.prg", "/x.prg",
        "0:/x.prg", "Games//x.prg", "Games/./x.prg", "Games\\x.prg", "Games/x.prg/"};
    shell_t shell = {0};
    mt_core_link_t link;
    size_t i;
    uint32_t reads, writes;
    mt_core_link_init(&link, &shell, read32, write32);
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    assert(c64_content_service_set_media_root("0:/library/c64/") == 0);
    assert(c64_content_service_set_media_root("0:/library/../escape") != 0);
    assert(c64_content_service_load("Games/Sub/demo.crt").status == MT_C64_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path, "0:/library/c64/Games/Sub/demo.crt") == 0);
    assert(strcmp(c64_content_service_crt_name(), "Games/Sub/demo.crt") == 0);
    assert(c64_content_service_set_media_root("0:/library/c64") == 0);
    assert(c64_content_service_set_media_root("0:/other") != 0);
    assert(c64_content_service_load("Programs/hello.prg").status == MT_C64_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path, "0:/library/c64/Programs/hello.prg") == 0);
    assert(!c64_content_service_crt_attached());
    reads = sd_reads; writes = shell.writes;
    for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(c64_content_service_load(bad[i]).status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    assert(sd_reads == reads && shell.writes == writes);
    assert(c64_content_service_set_media_root("0:/other") == 0);
}

static int title_commit(void *context)
{
    shell_t *shell = context;
    assert(shell->registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST);
    assert(shell->active_offset == 0);
    ++shell->commits;
    return shell->fail_commit ? -1 : 0;
}
static void test_title_handoff(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = 26;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    mt_c64_launch_hook_t hook = {false, true, title_commit, &shell};
    shell.require_handoff = true;
    shell.fail_reset = true;
    assert(c64_content_service_load_hooked("title.prg", &hook).status ==
           MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
    assert(!shell.commits && !shell.active_offset);
    shell.fail_reset = false;
    shell.fail_commit = true;
    assert(c64_content_service_load_hooked("title.prg", &hook).status ==
           MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
    assert(shell.commits == 1 && !shell.active_offset && !shell.completed_count);
    shell.commits = 0;
    shell.fail_commit = false;
    const mt_c64_content_service_result_t demo = c64_content_service_load_hooked(
        "idi8b/ultdemo2026/udemo2026.prg", &hook);
    assert(demo.status == MT_C64_CONTENT_SERVICE_OK && demo.sd.size == 37410U);
    assert(shell.active_offset == 37410U);
    assert(shell.commits == 1 && shell.completed_count == 1 && shell.drain_polls == 7);
    assert(!(shell.registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST));
    shell.active_offset = 0;
    shell.commits = 0;
    shell.drain_polls = 0;
    assert(c64_content_service_load_hooked("title.crt", &hook).status == MT_C64_CONTENT_SERVICE_OK);
    assert(c64_content_service_crt_attached() && shell.commits == 1 && shell.drain_polls == 7);
    shell.active_offset = 0;
    shell.commits = 0;
    shell.drain_polls = 0;
    /* Cart detach is a separate zero-byte transaction before PRG bytes. */
    shell.require_handoff = false;
    assert(c64_content_service_load_hooked("title.prg", &hook).status == MT_C64_CONTENT_SERVICE_OK);
    assert(!c64_content_service_crt_attached() && shell.commits == 1);
    assert(c64_content_service_load("idi8b/ultdemo2026/udemo2026.prg").status ==
           MT_C64_CONTENT_SERVICE_OK);
    assert(shell.active_offset == 37410U && shell.commits == 1);
}

static void test_title_handoff_timeout_recovery(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, fake_monotonic_ms, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = 26;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    shell.require_handoff = shell.never_ack = true;
    mt_c64_launch_hook_t hook = {false, true, title_commit, &shell};
    const mt_c64_content_service_result_t failed =
        c64_content_service_load_hooked("title.prg", &hook);
    assert(failed.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED &&
           failed.link == MT_LINK_TIMEOUT);
    assert(shell.active_offset == 1U && !shell.completed_count);
    assert(shell.abort_commands == 1U && shell.reset_releases == 1U);
    assert(!(shell.registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST));

    shell.never_ack = false;
    shell.commits = shell.drain_polls = 0U;
    shell.active_offset = 0U;
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    assert(c64_content_service_load_hooked("title.prg", &hook).status ==
           MT_C64_CONTENT_SERVICE_OK);
    assert(shell.completed_count == 1U && shell.commits == 1U && shell.drain_polls == 7U);
}

static int reboot_settings(void *context)
{
    shell_t *shell = context;
    assert(!c64_content_service_crt_attached());
    assert(!c64_content_service_prg_staged());
    assert(shell->completed[shell->completed_count - 1] == MT_LOAD_C64_CRT_DETACH);
    assert(shell->registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST);
    ++shell->commits;
    return shell->fail_commit ? -1 : 0;
}

static void test_reboot_default_cartridge(void)
{
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        shell_t shell = {0};
        mt_core_link_t link;
        mt_core_link_init(&link, &shell, read32, write32);
        c64_content_service_init(&link, respond, &shell, NULL, NULL);
        shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
        shell.registers[MT_REG_ABI_VERSION / 4U] = 27;
        shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
        assert(c64_content_service_load("transient.crt").status == MT_C64_CONTENT_SERVICE_OK);
        const uint32_t releases = shell.reset_releases;
        shell.require_handoff = true;
        shell.fail_commit = scenario == 2;
        fail_read = scenario == 3;
        shell.fail_reset = scenario == 5;
        const char *default_cart = scenario == 0 ? NULL : scenario == 4 ? "bad.prg" : "saved/default.crt";
        const mt_c64_content_service_result_t result = c64_content_service_reboot(
            default_cart, reboot_settings, &shell);
        fail_read = 0;
        if (scenario == 5) {
            assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
            assert(!shell.commits && c64_content_service_crt_attached());
            continue;
        }
        assert(shell.commits == 1 && shell.completed[1] == MT_LOAD_C64_CRT_DETACH);
        if (scenario >= 2) {
            assert(result.status != MT_C64_CONTENT_SERVICE_OK);
            assert(shell.registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST);
            assert(!c64_content_service_crt_attached() && shell.reset_releases == releases);
        } else {
            assert(result.status == MT_C64_CONTENT_SERVICE_OK);
            assert(!(shell.registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST));
            assert(c64_content_service_crt_attached() == (scenario == 1));
            if (scenario == 1) {
                assert(shell.completed[2] == MT_LOAD_C64_CRT && shell.drain_polls == 7);
                assert(strcmp(c64_content_service_crt_name(), "saved/default.crt") == 0);
            }
        }
    }
}

static uint32_t ef_entry(uint32_t kind, bool high, uint32_t bank,
                         uint32_t offset, uint8_t data)
{
    return MT_C64_EF_JOURNAL_VALID | kind | (high ? MT_C64_EF_JOURNAL_ROMH : 0U) |
           (bank << 22U) | (offset << 9U) | data;
}

static void push_entry(shell_t *shell, uint32_t entry)
{
    assert(shell->journal_count < 16U);
    shell->journal[(shell->journal_head + shell->journal_count) % 16U] = entry;
    ++shell->journal_count;
}

static void test_easyflash_saves(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, fake_monotonic_ms, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_MINOR;
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] = MT_C64_EXPANSION_CAPS_VALID |
        MT_C64_EXPANSION_CAPS_CART_COEXIST;
    sav_present = false;
    sav_commits = 0U;
    fail_save = false;

    /* An image without the EasyFlash capability is refused before any reset. */
    result = c64_content_service_load("easyflash.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED);
    assert(result.content.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(shell.reset_asserts == 0U && shell.completed_count == 0U);

    /* Without a save, the sparse CRT streams as the dense canonical image. */
    shell.registers[MT_REG_C64_EXPANSION_CAPS / 4U] |= MT_C64_EXPANSION_CAPS_CART_EASYFLASH;
    push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_PROGRAM, false, 0U, 0U, 0x00U));
    result = c64_content_service_load("Games/easyflash.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(result.content.hardware_type == MT_CRT_EASYFLASH);
    assert(shell.completed_count == 1U && shell.completed[0] == MT_LOAD_C64_CRT);
    assert(strcmp(sav_path, MT_C64_SD_MEDIA_ROOT "/Games/easyflash.crt.sav") == 0);
    assert(c64_content_service_save_active() && !c64_content_service_save_dirty());
    assert(shell.journal_count == 0U && shell.journal_pops == 1U);
    assert(sav_commits == 0U);

    /* Journal entries apply to the image; a quiet interval saves it. */
    push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_PROGRAM, false, 0U, 0x0005U, 0x01U));
    push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_PROGRAM, true, 9U, 0x1fffU, 0x0fU));
    /* The fake clock advances one second per read: the first poll applies
     * the entries, the next finds them quiet for two seconds and saves. */
    c64_content_service_poll();
    assert(shell.journal_count == 0U && c64_content_service_save_dirty() && sav_commits == 0U);
    c64_content_service_poll();
    assert(sav_commits == 1U && !c64_content_service_save_dirty());
    assert(sav_store[5] == 0x00U && sav_store[6] == 0x10U);
    assert(sav_store[9U * 0x4000U + 0x2000U + 0x1fffU] == 0x09U);
    assert(sav_store[9U * 0x4000U] == 0xffU && sav_store[0x2000U] == 0xffU);

    /* Export writes NAME-saved.crt beside the CRT from the mirrored image. */
    {
        static uint8_t reimported[MT_C64_EASYFLASH_IMAGE_SIZE];
        char exported[300];
        push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_PROGRAM, false, 0U, 0x0007U, 0x00U));
        result = c64_content_service_export_crt(exported, sizeof exported);
        assert(result.status == MT_C64_CONTENT_SERVICE_OK && shell.journal_count == 0U);
        assert(strcmp(exported, "easyflash-saved.crt") == 0);
        assert(strcmp(export_temp_path, MT_C64_SD_MEDIA_ROOT "/Games/easyflash-saved.crt.tmp") == 0);
        assert(strcmp(export_final_path, MT_C64_SD_MEDIA_ROOT "/Games/easyflash-saved.crt") == 0);
        assert(export_size == 0x40U + 2U * 0x2010U && result.sd.size == export_size);
        assert(export_store[0x40U + 0x10U + 7U] == 0x00U && export_store[0x40U + 0x10U + 8U] == 0x10U);
        mt_c64_easyflash_expand(export_store, export_size, reimported);
        assert(reimported[7] == 0x00U && reimported[9U * 0x4000U + 0x2000U + 0x1fffU] == 0x09U);
        assert(c64_content_service_save_dirty() && sav_commits == 1U);
        /* A second export replaces the first one. */
        result = c64_content_service_export_crt(NULL, 0U);
        assert(result.status == MT_C64_CONTENT_SERVICE_OK);
        assert(export_removes == 1U && export_renames == 2U && export_final_exists);
        for (unsigned poll = 0U; poll < 4U && c64_content_service_save_dirty(); ++poll)
            c64_content_service_poll();
        assert(sav_commits == 2U && !c64_content_service_save_dirty());
    }

    /* A failed save is reported and blocks detach until it succeeds. */
    push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_SECTOR_ERASE, true, 8U, 0U, 0xffU));
    fail_save = true;
    c64_content_service_poll();
    c64_content_service_poll();
    assert(c64_content_service_save_faulted() && c64_content_service_save_dirty());
    result = c64_content_service_detach_crt();
    assert(result.status == MT_C64_CONTENT_SERVICE_SAVE_FAILED);
    assert(c64_content_service_crt_attached() && c64_content_service_save_active());
    assert((shell.registers[MT_REG_CONTROL / 4U] & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    fail_save = false;
    result = c64_content_service_detach_crt();
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(sav_commits == 3U && !c64_content_service_save_active());
    assert(sav_store[9U * 0x4000U + 0x2000U + 0x1fffU] == 0xffU && sav_store[5] == 0x00U);
    result = c64_content_service_export_crt(NULL, 0U);
    assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);

    /* A later load starts from the save, and replacing it saves first. */
    result = c64_content_service_load("Games/easyflash.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK && c64_content_service_save_active());
    push_entry(&shell, ef_entry(MT_C64_EF_JOURNAL_PROGRAM, false, 0U, 0x0006U, 0x00U));
    const unsigned asserts = shell.reset_asserts;
    result = c64_content_service_load("magic.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(shell.reset_asserts > asserts && sav_commits == 4U);
    assert(sav_store[5] == 0x00U && sav_store[6] == 0x00U && sav_store[7] == 0x00U);
    assert(!c64_content_service_save_active());
    assert(c64_content_service_flush() == 0 && sav_commits == 4U);
}

int main(void)
{
    test_rom_extension_capability();
    test_easyflash_saves();
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;
    static const uint8_t extracted_prg[] = {
        0x01U, 0x08U, 0x0bU, 0x08U, 0x01U, 0x00U
    };

    test_title_handoff();
    test_title_handoff_timeout_recovery();
    test_reboot_default_cartridge();
    test_reu_excludes_cartridges();
    test_cartridge_expansion_policy();
    test_run_preserves_pending_media_request();
    test_run_refuses_existing_core_fault();
    test_nested_package_paths();

    mt_core_link_init(&link, &shell, read32, write32);
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
    c64_content_service_init(
        &link, respond, &shell, fake_monotonic_ms, NULL);

    result = c64_content_service_load("demo.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path,
                  "0:/multitini/cores/c64/media/demo.crt") == 0);
    assert(c64_content_service_crt_attached());
    assert(shell.completed[0] == MT_LOAD_C64_CRT);

    result = c64_content_service_load("hello.prg");
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(result.sd.size == 3U);
    assert(!c64_content_service_crt_attached());
    assert(shell.completed[1] == MT_LOAD_C64_CRT_DETACH);
    assert(shell.completed[2] == MT_LOAD_C64_PRG);

    strcpy(requested_path, "unchanged");
    result = c64_content_service_run_staged_prg();
    assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    result = c64_content_service_stage_prg_image(
        extracted_prg, sizeof(extracted_prg));
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(c64_content_service_prg_staged());
    assert(strcmp(requested_path, "unchanged") == 0);
    assert(shell.completed[3] == MT_LOAD_C64_PRG_STAGE);
    result = c64_content_service_run_staged_prg();
    assert(result.status == MT_C64_CONTENT_SERVICE_OK);
    assert(!c64_content_service_prg_staged());
    assert(shell.completed[4] == MT_LOAD_C64_PRG_RUN);
    result = c64_content_service_run_staged_prg();
    assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);

    result = c64_content_service_stage_prg_image(NULL, sizeof(extracted_prg));
    assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    result = c64_content_service_stage_prg_image(extracted_prg, 1U);
    assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED);

    shell.stall_stream_data = 1;
    {
        const uint32_t aborts_before = shell.abort_commands;
        const uint32_t asserts_before = shell.reset_asserts;
        const uint32_t releases_before = shell.reset_releases;
        const uint32_t time_calls_before = fake_time_calls;
        result = c64_content_service_stage_prg_image(
            extracted_prg, sizeof(extracted_prg));
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(result.link == MT_LINK_TIMEOUT);
        /* One timestamp starts the deadline and five checks consume the
         * complete 5,000 ms reset-to-KERNAL budget. */
        assert(fake_time_calls == time_calls_before + 6U);
        assert(shell.abort_commands == aborts_before + 1U);
        assert(shell.reset_asserts == asserts_before + 1U);
        assert(shell.reset_releases == releases_before + 1U);
    }
    assert(shell.reset_asserts != 0U);
    assert(shell.reset_releases == shell.reset_asserts);
    assert((shell.registers[MT_REG_CONTROL / 4U] &
            MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    shell.stall_stream_data = 0;

    shell.registers[MT_REG_STATUS / 4U] = 0U;
    {
        const uint32_t aborts_before = shell.abort_commands;
        const uint32_t asserts_before = shell.reset_asserts;
        const uint32_t releases_before = shell.reset_releases;
        const uint32_t time_calls_before = fake_time_calls;
        result = c64_content_service_stage_prg_image(
            extracted_prg, sizeof(extracted_prg));
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(result.link == MT_LINK_TIMEOUT);
        /* A fast stream starts the 5 s boot deadline, then the separate ready
         * wait starts and consumes its complete 2 s completion budget. */
        assert(fake_time_calls == time_calls_before + 4U);
        assert(shell.abort_commands == aborts_before + 1U);
        assert(shell.reset_asserts == asserts_before + 1U);
        assert(shell.reset_releases == releases_before + 1U);
    }
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;

    shell.registers[MT_REG_CONTROL / 4U] |=
        MT_CONTROL_CORE_RESET_REQUEST;
    {
        const uint32_t releases_before = shell.reset_releases;
        result = c64_content_service_stage_prg_image(
            extracted_prg, sizeof(extracted_prg));
        assert(result.status == MT_C64_CONTENT_SERVICE_OK);
        assert(shell.reset_releases == releases_before + 1U);
        assert((shell.registers[MT_REG_CONTROL / 4U] &
                MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    }

    shell.stall_stream_data = 1;
    {
        const uint32_t asserts_before = shell.reset_asserts;
        const uint32_t releases_before = shell.reset_releases;
        const uint32_t aborts_before = shell.abort_commands;
        const uint32_t time_calls_before = fake_time_calls;
        result = c64_content_service_run_staged_prg();
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(result.link == MT_LINK_TIMEOUT);
        assert(c64_content_service_prg_staged());
        assert(shell.abort_commands == aborts_before + 1U);
        assert(fake_time_calls == time_calls_before + 3U);
        assert(shell.reset_asserts == asserts_before);
        assert(shell.reset_releases == releases_before);
        assert((shell.registers[MT_REG_CONTROL / 4U] &
                MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    }
    shell.stall_stream_data = 0;
    shell.registers[MT_REG_STATUS / 4U] = 0U;
    {
        const uint32_t asserts_before = shell.reset_asserts;
        const uint32_t releases_before = shell.reset_releases;
        const uint32_t aborts_before = shell.abort_commands;
        const uint32_t time_calls_before = fake_time_calls;
        result = c64_content_service_run_staged_prg();
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(result.link == MT_LINK_TIMEOUT);
        assert(!c64_content_service_prg_staged());
        assert(shell.abort_commands == aborts_before);
        assert(fake_time_calls == time_calls_before + 3U);
        assert(shell.reset_asserts == asserts_before);
        assert(shell.reset_releases == releases_before);
        assert((shell.registers[MT_REG_CONTROL / 4U] &
                MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    }
    shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;

    result = c64_content_service_load("unsupported.crt");
    assert(result.status == MT_C64_CONTENT_SERVICE_REJECTED);
    assert(result.content.status == MT_C64_CONTENT_UNSUPPORTED_CRT);
    assert(shell.completed_count == 8U);

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

    {
        shell_t bounded_shell = {0};
        mt_core_link_t bounded_link;
        mt_core_link_init(&bounded_link, &bounded_shell, read32, write32);
        bounded_shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
        bounded_shell.stall_stream_data = 1;
        c64_content_service_init(
            &bounded_link, respond, &bounded_shell, NULL, NULL);
        result = c64_content_service_stage_prg_image(
            extracted_prg, sizeof(extracted_prg));
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(result.link == MT_LINK_TIMEOUT);
        assert(bounded_shell.abort_commands == 1U);
        assert(bounded_shell.reset_asserts == 1U);
        assert(bounded_shell.reset_releases == 1U);
    }
    {
        shell_t native_shell = {0};
        mt_core_link_t native_link;
        mt_core_link_init(&native_link, &native_shell, read32, write32);
        native_shell.registers[MT_REG_STATUS / 4U] = MT_STATUS_CORE_READY;
        c64_content_service_init(&native_link, respond, &native_shell,
                                 NULL, NULL);
        result = c64_content_service_prepare_disk_load();
        assert(result.status == MT_C64_CONTENT_SERVICE_OK);
        assert(native_shell.completed_count == 1U);
        assert(native_shell.completed[0] == MT_LOAD_C64_PRG_STAGE);
        assert(!c64_content_service_prg_staged());
        result = c64_content_service_run_staged_prg();
        assert(result.status == MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
        assert(native_shell.completed_count == 1U);

        native_shell.stall_stream_data = 1;
        result = c64_content_service_prepare_disk_load();
        assert(result.status == MT_C64_CONTENT_SERVICE_TRANSFER_FAILED);
        assert(!c64_content_service_prg_staged());
    }
    return 0;
}
