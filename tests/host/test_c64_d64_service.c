/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"

#include "multitini_c64_interface.h"
#include "multitini_regs.h"
#include "sd_storage.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_REGISTER_WORDS 64U
#define TEST_D64_SIZE ((size_t)174848)
#define TEST_D64_ERROR_SIZE ((size_t)175531)
#define TEST_G64_SIZE ((size_t)64)
#define TEST_CAPTURE_SIZE 32U

typedef struct {
    uint32_t registers[TEST_REGISTER_WORDS];
    uint32_t mount_attempts;
    uint32_t mount_busy_remaining;
    uint32_t mount_generation;
    uint32_t mount_flags;
    uint32_t eject_attempts;
    uint32_t eject_generation;
    bool eject_generation_mismatch;
    uint32_t stream_begin_count;
    uint32_t stream_data_count;
    uint32_t stream_end_count;
    uint32_t stream_kind;
    uint32_t stream_total;
    uint8_t stream_capture[TEST_CAPTURE_SIZE];
    size_t stream_capture_size;
    uint32_t complete_count;
    uint32_t complete_status;
    uint32_t complete_blocks;
    uint32_t complete_generation;
    uint32_t error_clear_count;
    uint32_t write_request_id;
    uint32_t write_lba;
    uint32_t write_blocks;
    uint8_t write_seed;
    uint8_t reverse_data[8192U];
    size_t reverse_size;
    size_t reverse_index;
    size_t early_last_index;
    uint32_t media_data_reads;
    bool auto_write_on_eject;
    bool write_completion_before_replace;
    bool faulted;
} fake_shell_t;

typedef struct {
    mt_sd_status_t recover_status;
    mt_sd_status_t replace_status;
    uint32_t recover_count;
    uint32_t replace_count;
    char recovered_path[256U];
    char recovered_journal[264U];
    char replaced_path[256U];
    char replaced_journal[264U];
    size_t replaced_size;
    uint8_t replaced_first_byte;
} fake_sd_t;

static fake_sd_t fake_sd;

static uint32_t register_index(uint32_t offset)
{
    assert((offset & UINT32_C(3)) == 0U);
    assert(offset / UINT32_C(4) < TEST_REGISTER_WORDS);
    return offset / UINT32_C(4);
}

static void set_request(fake_shell_t *shell, uint32_t id, bool write,
                        uint32_t lba, uint32_t blocks, uint32_t generation)
{
    shell->registers[register_index(MT_REG_MEDIA_REQUEST)] =
        MT_MEDIA_REQUEST_VALID | (write ? MT_MEDIA_REQUEST_WRITE : 0U);
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)] = id;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_LBA)] = lba;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_BLOCK_COUNT)] =
        blocks;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_GENERATION)] =
        generation;
}

static void prepare_reverse_write(fake_shell_t *shell)
{
    size_t index;
    shell->reverse_size = (size_t)shell->write_blocks * 256U;
    assert(shell->reverse_size <= sizeof(shell->reverse_data));
    for (index = 0U; index < shell->reverse_size; ++index) {
        shell->reverse_data[index] = (uint8_t)(shell->write_seed + index);
    }
    shell->reverse_index = 0U;
    ++shell->write_request_id;
    if (shell->write_request_id == 0U) ++shell->write_request_id;
    set_request(shell, shell->write_request_id, true, shell->write_lba,
                shell->write_blocks, shell->mount_generation);
}

static uint32_t fake_read32(void *context, uint32_t offset)
{
    fake_shell_t *const shell = context;
    if (offset == MT_REG_MEDIA_DATA) {
        uint32_t result;
        bool last;
        ++shell->media_data_reads;
        if (shell->reverse_index >= shell->reverse_size) return 0U;
        last = shell->reverse_index + 1U == shell->reverse_size;
        if (shell->early_last_index != SIZE_MAX &&
            shell->reverse_index == shell->early_last_index) {
            last = true;
        }
        result = MT_MEDIA_DATA_VALID |
                 ((uint32_t)shell->reverse_data[shell->reverse_index] <<
                  MT_MEDIA_DATA_BYTE_SHIFT) |
                 (last ? MT_MEDIA_DATA_LAST : 0U);
        ++shell->reverse_index;
        return result;
    }
    return shell->registers[register_index(offset)];
}

static void fake_complete_command(fake_shell_t *shell, uint32_t sequence)
{
    const uint32_t opcode =
        shell->registers[register_index(MT_REG_COMMAND_OPCODE)];
    const uint32_t arg0 =
        shell->registers[register_index(MT_REG_COMMAND_ARG0)];
    const uint32_t arg1 =
        shell->registers[register_index(MT_REG_COMMAND_ARG1)];
    const uint32_t arg2 =
        shell->registers[register_index(MT_REG_COMMAND_ARG2)];
    const uint32_t arg3 =
        shell->registers[register_index(MT_REG_COMMAND_ARG3)];
    uint32_t status = MT_RESPONSE_OK;

    switch (opcode) {
    case MT_COMMAND_MEDIA_MOUNT:
        ++shell->mount_attempts;
        shell->mount_generation = arg2;
        shell->mount_flags = arg0;
        if (shell->mount_busy_remaining != 0U) {
            --shell->mount_busy_remaining;
            status = MT_RESPONSE_BUSY;
        } else {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] =
                MT_MEDIA_STATE_MOUNTED;
        }
        break;
    case MT_COMMAND_MEDIA_EJECT:
        ++shell->eject_attempts;
        if (shell->eject_generation == 0U) {
            shell->eject_generation = arg1;
        } else if (shell->eject_generation != arg1) {
            shell->eject_generation_mismatch = true;
        }
        if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
             MT_MEDIA_STATE_DIRTY) != 0U) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] |=
                MT_MEDIA_STATE_EJECT_PENDING;
            if (shell->auto_write_on_eject) prepare_reverse_write(shell);
        } else {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
            shell->registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
        }
        break;
    case MT_COMMAND_STREAM_BEGIN:
        ++shell->stream_begin_count;
        shell->stream_kind = arg0;
        shell->stream_total = arg1;
        shell->stream_capture_size = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        ++shell->stream_data_count;
        if (shell->stream_capture_size < sizeof(shell->stream_capture)) {
            shell->stream_capture[shell->stream_capture_size++] =
                (uint8_t)arg0;
        }
        break;
    case MT_COMMAND_STREAM_END:
        ++shell->stream_end_count;
        break;
    case MT_COMMAND_MEDIA_COMPLETE: {
        const bool was_write =
            (shell->registers[register_index(MT_REG_MEDIA_REQUEST)] &
             MT_MEDIA_REQUEST_WRITE) != 0U;
        ++shell->complete_count;
        shell->complete_status = arg1;
        shell->complete_blocks = arg2;
        shell->complete_generation = arg3;
        if (was_write && arg1 == MT_RESPONSE_OK &&
            fake_sd.replace_count == 0U) {
            shell->write_completion_before_replace = true;
        }
        shell->registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
        if (arg1 == MT_RESPONSE_OK && was_write) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] &=
                ~MT_MEDIA_STATE_DIRTY;
            if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
                 MT_MEDIA_STATE_EJECT_PENDING) != 0U) {
                shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
            }
        } else if (arg1 != MT_RESPONSE_OK) {
            shell->faulted = true;
        }
        break;
    }
    default:
        break;
    }

    shell->registers[register_index(MT_REG_RESPONSE_STATUS)] = status;
    shell->registers[register_index(MT_REG_RESPONSE_ARG0)] = 0U;
    shell->registers[register_index(MT_REG_RESPONSE_ARG1)] = 0U;
    shell->registers[register_index(MT_REG_RESPONSE_SEQ)] = sequence;
}

static void fake_write32(void *context, uint32_t offset, uint32_t value)
{
    fake_shell_t *const shell = context;
    shell->registers[register_index(offset)] = value;
    if (offset == MT_REG_COMMAND_DOORBELL) {
        fake_complete_command(shell, value);
    } else if (offset == MT_REG_ERROR &&
               (value & MT_ERROR_CORE_LOAD) != 0U) {
        ++shell->error_clear_count;
        shell->faulted = false;
        if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
             (MT_MEDIA_STATE_DIRTY | MT_MEDIA_STATE_EJECT_PENDING)) ==
                (MT_MEDIA_STATE_DIRTY | MT_MEDIA_STATE_EJECT_PENDING) &&
            shell->auto_write_on_eject) {
            prepare_reverse_write(shell);
        }
    }
}

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void make_g64(uint8_t *data, size_t size)
{
    const size_t track_offset = 28U;
    size_t index;
    assert(size >= track_offset + 12U);
    memset(data, 0U, size);
    memcpy(data, "GCR-1541", 8U);
    data[9] = 2U;
    write_le16(data + 10U, 16U);
    write_le32(data + 12U, (uint32_t)track_offset);
    write_le32(data + 20U, 2U);
    write_le16(data + track_offset, 10U);
    for (index = 0U; index < 10U; ++index) {
        data[track_offset + 2U + index] = (uint8_t)(0x80U + index);
    }
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(dst != NULL);
    if (strcmp(path, "0:/multitini/cores/c64/media/drive8.d64") == 0) {
        result.size = TEST_D64_SIZE;
        assert(capacity >= result.size);
        memset(dst, 0U, result.size);
    } else if (strcmp(path, "0:/multitini/cores/c64/media/error.d64") == 0) {
        result.size = TEST_D64_ERROR_SIZE;
        assert(capacity >= result.size);
        memset(dst, 0U, result.size);
    } else if (strcmp(path, "0:/multitini/cores/c64/media/disk.g64") == 0) {
        result.size = TEST_G64_SIZE;
        assert(capacity >= result.size);
        make_g64(dst, result.size);
    } else {
        assert(false);
    }
    return result;
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    mt_sd_result_t result = {fake_sd.recover_status, 0U, 0U};
    assert(path != NULL && journal_path != NULL && recovery_buffer != NULL);
    assert(capacity >= TEST_D64_SIZE);
    ++fake_sd.recover_count;
    memcpy(fake_sd.recovered_path, path, strlen(path) + 1U);
    memcpy(fake_sd.recovered_journal, journal_path,
           strlen(journal_path) + 1U);
    return result;
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size)
{
    mt_sd_result_t result = {fake_sd.replace_status, 0U, size};
    assert(path != NULL && journal_path != NULL && src != NULL);
    ++fake_sd.replace_count;
    memcpy(fake_sd.replaced_path, path, strlen(path) + 1U);
    memcpy(fake_sd.replaced_journal, journal_path,
           strlen(journal_path) + 1U);
    fake_sd.replaced_size = size;
    fake_sd.replaced_first_byte = src[0];
    return result;
}

mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(strcmp(path, "0:/multitini/cores/c64/media") == 0);
    assert(callback != NULL);
    (void)callback(context, "drive8.d64", TEST_D64_SIZE, false);
    (void)callback(context, "disk.g64", TEST_G64_SIZE, false);
    return result;
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    (void)status;
    return "test";
}

int xil_printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_end(arguments);
    return 0;
}

static void setup(fake_shell_t *shell, mt_core_link_t *link)
{
    memset(shell, 0, sizeof(*shell));
    memset(&fake_sd, 0, sizeof(fake_sd));
    shell->early_last_index = SIZE_MAX;
    shell->write_blocks = 1U;
    shell->write_seed = 0x40U;
    mt_core_link_init(link, shell, fake_read32, fake_write32);
    c64_d64_service_init(link, NULL, NULL);
}

static void test_d64_mount_read_and_write(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    shell.mount_busy_remaining = 2U;
    assert(c64_d64_service_mount_default() == 0);
    assert(shell.mount_attempts == 3U);
    assert(shell.mount_generation != 0U);
    assert((shell.mount_flags & MT_MEDIA_KIND_MASK) ==
           ((uint32_t)MT_LOAD_C64_D64 << MT_MEDIA_KIND_SHIFT));
    assert((shell.mount_flags & MT_MEDIA_MOUNT_READ_ONLY) == 0U);
    assert(fake_sd.recover_count == 1U);
    assert(strcmp(fake_sd.recovered_journal,
                  "0:/multitini/cores/c64/media/drive8.d64.mtj") == 0);

    set_request(&shell, 0x100U, false, 0U, 1U,
                shell.mount_generation);
    c64_d64_service_poll();
    assert(shell.stream_begin_count == 1U);
    assert(shell.stream_kind == MT_LOAD_C64_D64);
    assert(shell.stream_total == 256U);
    assert(shell.stream_data_count == 256U);
    assert(shell.stream_end_count == 1U);
    assert(shell.complete_count == 1U);
    assert(shell.complete_status == MT_RESPONSE_OK);
    assert(shell.complete_blocks == 1U);

    shell.registers[register_index(MT_REG_MEDIA_STATE)] =
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    prepare_reverse_write(&shell);
    c64_d64_service_poll();
    assert(shell.reverse_index == 256U);
    assert(shell.media_data_reads == 256U);
    assert(fake_sd.replace_count == 1U);
    assert(fake_sd.replaced_size == TEST_D64_SIZE);
    assert(fake_sd.replaced_first_byte == 0x40U);
    assert(strcmp(fake_sd.replaced_journal,
                  "0:/multitini/cores/c64/media/drive8.d64.mtj") == 0);
    assert(shell.complete_count == 2U);
    assert(shell.complete_status == MT_RESPONSE_OK);
    assert(shell.complete_blocks == 1U);
    assert(!shell.write_completion_before_replace);
}

static void test_invalid_journal_blocks_mount(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    fake_sd.recover_status = MT_SD_JOURNAL_INVALID;
    assert(c64_d64_service_mount_default() != 0);
    assert(fake_sd.recover_count == 1U);
    assert(shell.mount_attempts == 0U);
}

static void test_write_last_is_strict(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    assert(c64_d64_service_mount_default() == 0);
    shell.registers[register_index(MT_REG_MEDIA_STATE)] =
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    shell.early_last_index = 10U;
    prepare_reverse_write(&shell);
    c64_d64_service_poll();
    assert(shell.reverse_index == 11U);
    assert(fake_sd.replace_count == 0U);
    assert(shell.complete_count == 1U);
    assert(shell.complete_status == MT_RESPONSE_INVALID_ARGUMENT);
    assert(shell.complete_blocks == 0U);
    assert(shell.faulted);
}

static void test_dirty_eject_failure_and_retry(void)
{
    fake_shell_t shell;
    mt_core_link_t link;
    uint32_t eject_generation;

    setup(&shell, &link);
    assert(c64_d64_service_mount_default() == 0);
    shell.registers[register_index(MT_REG_MEDIA_STATE)] =
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    shell.auto_write_on_eject = true;
    fake_sd.replace_status = MT_SD_WRITE_FAILED;
    assert(c64_d64_service_eject() != 0);
    assert(shell.eject_attempts == 1U);
    eject_generation = shell.eject_generation;
    assert(eject_generation != 0U);
    assert(fake_sd.replace_count == 1U);
    assert(shell.complete_status == MT_RESPONSE_IO_ERROR);
    assert(shell.complete_blocks == 0U);
    assert((shell.registers[register_index(MT_REG_MEDIA_STATE)] &
            (MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
             MT_MEDIA_STATE_EJECT_PENDING)) ==
           (MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
            MT_MEDIA_STATE_EJECT_PENDING));

    fake_sd.replace_status = MT_SD_OK;
    assert(c64_d64_service_eject() == 0);
    assert(shell.error_clear_count == 1U);
    assert(shell.eject_attempts == 1U);
    assert(shell.eject_generation == eject_generation);
    assert(!shell.eject_generation_mismatch);
    assert(fake_sd.replace_count == 2U);
    assert(shell.complete_status == MT_RESPONSE_OK);
    assert(shell.registers[register_index(MT_REG_MEDIA_STATE)] == 0U);
}

static void test_retry_does_not_cancel_healthy_write(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    assert(c64_d64_service_mount_default() == 0);
    prepare_reverse_write(&shell);
    assert(c64_d64_service_retry() != 0);
    assert(shell.error_clear_count == 0U);
    assert(shell.reverse_index == 0U);
    assert((shell.registers[register_index(MT_REG_MEDIA_REQUEST)] &
            (MT_MEDIA_REQUEST_VALID | MT_MEDIA_REQUEST_WRITE)) ==
           (MT_MEDIA_REQUEST_VALID | MT_MEDIA_REQUEST_WRITE));
}

static void test_error_tail_is_read_only(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    assert(c64_d64_service_mount("error.d64") == 0);
    assert((shell.mount_flags & MT_MEDIA_MOUNT_READ_ONLY) != 0U);
    assert((shell.mount_flags & MT_MEDIA_KIND_MASK) ==
           ((uint32_t)MT_LOAD_C64_D64 << MT_MEDIA_KIND_SHIFT));
}

static void test_g64_read_only_track(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    assert(c64_d64_service_mount("disk.g64") == 0);
    assert(fake_sd.recover_count == 0U);
    assert((shell.mount_flags & MT_MEDIA_MOUNT_READ_ONLY) != 0U);
    assert((shell.mount_flags & MT_MEDIA_KIND_MASK) ==
           ((uint32_t)MT_LOAD_C64_G64 << MT_MEDIA_KIND_SHIFT));

    set_request(&shell, 0x200U, false, 0U, 32U,
                shell.mount_generation);
    c64_d64_service_poll();
    assert(shell.stream_kind == MT_LOAD_C64_G64);
    assert(shell.stream_total == 8192U);
    assert(shell.stream_data_count == 8192U);
    assert(shell.stream_capture_size == TEST_CAPTURE_SIZE);
    assert(shell.stream_capture[0] == 10U);
    assert(shell.stream_capture[1] == 0x40U);
    assert(shell.stream_capture[2] == 0x80U);
    assert(shell.stream_capture[12] == 0xFFU);
    assert(shell.complete_status == MT_RESPONSE_OK);
    assert(shell.complete_blocks == 32U);
}

static void test_reset_barrier_remounts(void)
{
    fake_shell_t shell;
    mt_core_link_t link;
    uint32_t first_mount_generation;

    setup(&shell, &link);
    assert(c64_d64_service_mount_default() == 0);
    first_mount_generation = shell.mount_generation;
    shell.registers[register_index(MT_REG_MEDIA_STATE)] =
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    shell.auto_write_on_eject = true;
    assert(c64_d64_service_prepare_reset() == 0);
    assert(shell.registers[register_index(MT_REG_MEDIA_STATE)] == 0U);
    c64_d64_service_notify_core_reset();
    assert(c64_d64_service_resume_after_reset() == 0);
    assert(shell.mount_attempts == 2U);
    assert(shell.mount_generation != first_mount_generation);
    assert(fake_sd.recover_count == 2U);
    assert((shell.registers[register_index(MT_REG_MEDIA_STATE)] &
            MT_MEDIA_STATE_MOUNTED) != 0U);
}

static void test_failed_reset_remount_can_be_retried(void)
{
    fake_shell_t shell;
    mt_core_link_t link;

    setup(&shell, &link);
    assert(c64_d64_service_mount_default() == 0);
    assert(c64_d64_service_prepare_reset() == 0);
    c64_d64_service_notify_core_reset();

    shell.mount_busy_remaining = 16U;
    assert(c64_d64_service_resume_after_reset() != 0);
    assert(shell.error_clear_count == 0U);
    assert(shell.registers[register_index(MT_REG_MEDIA_STATE)] == 0U);

    shell.mount_busy_remaining = 0U;
    assert(c64_d64_service_retry() == 0);
    assert(shell.error_clear_count == 0U);
    assert(c64_d64_service_resume_after_reset() == 0);
    assert((shell.registers[register_index(MT_REG_MEDIA_STATE)] &
            MT_MEDIA_STATE_MOUNTED) != 0U);
}

int main(void)
{
    test_d64_mount_read_and_write();
    test_invalid_journal_blocks_mount();
    test_write_last_is_strict();
    test_dirty_eject_failure_and_retry();
    test_retry_does_not_cancel_healthy_write();
    test_error_tail_is_read_only();
    test_g64_read_only_track();
    test_reset_barrier_remounts();
    test_failed_reset_remount_can_be_retried();
    return 0;
}
