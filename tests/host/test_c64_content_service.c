/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_content_service.h"

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
} shell_t;

static char requested_path[256];
static int fail_read;
static uint64_t fake_time_ms;
static uint32_t fake_time_calls;
static uint32_t sd_reads;

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    if (offset == MT_REG_C64_EXPANSION_CONFIG || offset == MT_REG_C64_EXPANSION_CAPS)
        ++shell->expansion_reads;
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    ++shell->writes;
    if (offset == MT_REG_STREAM_DATA_BURST) {
        const uint32_t remaining = shell->active_size - shell->active_offset;
        assert(remaining != 0U);
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
        shell->active_kind =
            (uint8_t)shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->active_size = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        if (shell->stall_stream_data != 0) {
            shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_BUSY;
            shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
            return;
        }
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
        ++shell->abort_commands;
        shell->active_size = 0U;
        break;
    case MT_COMMAND_CORE_RESET:
        if (shell->registers[MT_REG_COMMAND_ARG0 / 4U] != 0U) {
            ++shell->reset_asserts;
            shell->registers[MT_REG_CONTROL / 4U] |=
                MT_CONTROL_CORE_RESET_REQUEST;
        } else {
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

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    ++sd_reads;
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

static void test_reu_excludes_cartridges(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    c64_content_service_init(&link, respond, &shell, NULL, NULL);
    shell.registers[MT_REG_CORE_ID / 4U] = MT_C64_CORE_ID;
    shell.registers[MT_REG_ABI_VERSION / 4U] = 23U;
    shell.registers[MT_REG_STREAM_STATUS / 4U] = MT_STREAM_STATUS_READY | MT_STREAM_STATUS_DRAINED;
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

int main(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_content_service_result_t result;
    static const uint8_t extracted_prg[] = {
        0x01U, 0x08U, 0x0bU, 0x08U, 0x01U, 0x00U
    };

    test_reu_excludes_cartridges();
    test_run_preserves_pending_media_request();
    test_run_refuses_existing_core_fault();

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
