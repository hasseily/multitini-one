/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"

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

typedef struct {
    uint32_t registers[TEST_REGISTER_WORDS];
    uint32_t mount_attempts;
    uint32_t mount_busy_remaining;
    uint32_t mount_generation;
    uint32_t eject_attempts;
    uint32_t eject_busy_remaining;
    uint32_t eject_generation;
    bool eject_generation_mismatch;
    bool clear_request_on_eject;
    uint32_t stream_begin_count;
    uint32_t stream_data_count;
    uint32_t stream_end_count;
    uint32_t complete_count;
} fake_shell_t;

static uint32_t register_index(uint32_t offset)
{
    assert((offset & UINT32_C(3)) == 0U);
    assert(offset / UINT32_C(4) < TEST_REGISTER_WORDS);
    return offset / UINT32_C(4);
}

static uint32_t fake_read32(void *context, uint32_t offset)
{
    fake_shell_t *const shell = context;
    return shell->registers[register_index(offset)];
}

static void fake_complete_command(fake_shell_t *shell, uint32_t sequence)
{
    const uint32_t opcode =
        shell->registers[register_index(MT_REG_COMMAND_OPCODE)];
    const uint32_t arg1 =
        shell->registers[register_index(MT_REG_COMMAND_ARG1)];
    uint32_t status = MT_RESPONSE_OK;

    switch (opcode) {
    case MT_COMMAND_MEDIA_MOUNT:
        ++shell->mount_attempts;
        shell->mount_generation =
            shell->registers[register_index(MT_REG_COMMAND_ARG2)];
        if (shell->mount_busy_remaining != 0U) {
            --shell->mount_busy_remaining;
            status = MT_RESPONSE_BUSY;
        }
        break;
    case MT_COMMAND_MEDIA_EJECT:
        ++shell->eject_attempts;
        if (shell->eject_generation == 0U) {
            shell->eject_generation = arg1;
        } else if (shell->eject_generation != arg1) {
            shell->eject_generation_mismatch = true;
        }
        if (shell->eject_busy_remaining != 0U) {
            --shell->eject_busy_remaining;
            status = MT_RESPONSE_BUSY;
        } else if (shell->clear_request_on_eject) {
            shell->registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
        }
        break;
    case MT_COMMAND_STREAM_BEGIN:
        ++shell->stream_begin_count;
        break;
    case MT_COMMAND_STREAM_DATA:
        ++shell->stream_data_count;
        break;
    case MT_COMMAND_STREAM_END:
        ++shell->stream_end_count;
        break;
    case MT_COMMAND_MEDIA_COMPLETE:
        ++shell->complete_count;
        break;
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
    }
}

static void set_request(fake_shell_t *shell, uint32_t id, uint32_t lba,
                        uint32_t blocks, uint32_t generation)
{
    shell->registers[register_index(MT_REG_MEDIA_REQUEST)] =
        MT_MEDIA_REQUEST_VALID;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)] = id;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_LBA)] = lba;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_BLOCK_COUNT)] =
        blocks;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_GENERATION)] =
        generation;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, TEST_D64_SIZE};
    assert(strcmp(path, "0:/cores/c64/media/drive8.d64") == 0);
    assert(dst != NULL);
    assert(capacity >= TEST_D64_SIZE);
    memset(dst, 0, TEST_D64_SIZE);
    return result;
}

mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)path;
    (void)callback;
    (void)context;
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

int main(void)
{
    fake_shell_t shell;
    mt_core_link_t link;
    uint32_t first_stream_data_count;

    memset(&shell, 0, sizeof(shell));
    mt_core_link_init(&link, &shell, fake_read32, fake_write32);
    c64_d64_service_init(&link, NULL, NULL);

    /* A transiently busy media mailbox is retried with one generation. */
    shell.mount_busy_remaining = 2U;
    assert(c64_d64_service_mount_default() == 0);
    assert(shell.mount_attempts == 3U);
    assert(shell.mount_generation != 0U);

    /* A held request is serviced once until reset explicitly clears dedupe. */
    set_request(&shell, UINT32_C(0x11223344), 0U, 1U,
                shell.mount_generation);
    c64_d64_service_poll();
    assert(shell.stream_begin_count == 1U);
    assert(shell.stream_data_count == 256U);
    assert(shell.stream_end_count == 1U);
    assert(shell.complete_count == 1U);
    first_stream_data_count = shell.stream_data_count;
    c64_d64_service_poll();
    assert(shell.stream_data_count == first_stream_data_count);
    assert(shell.complete_count == 1U);

    c64_d64_service_notify_core_reset();
    c64_d64_service_poll();
    assert(shell.stream_begin_count == 2U);
    assert(shell.stream_data_count == 512U);
    assert(shell.stream_end_count == 2U);
    assert(shell.complete_count == 2U);
    assert(shell.eject_attempts == 0U);

    /* A failed automatic eject stays pending and retries the same generation. */
    shell.registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
    c64_d64_service_poll();
    set_request(&shell, UINT32_C(0x55667788), 0U, 1U,
                shell.mount_generation + 1U);
    shell.eject_busy_remaining = 16U;
    shell.clear_request_on_eject = true;
    c64_d64_service_poll();
    assert(shell.eject_attempts == 16U);
    assert(shell.eject_generation != 0U);
    assert(!shell.eject_generation_mismatch);

    c64_d64_service_notify_core_reset();
    c64_d64_service_poll();
    assert(shell.eject_attempts == 17U);
    assert(!shell.eject_generation_mismatch);
    assert(shell.registers[register_index(MT_REG_MEDIA_REQUEST)] == 0U);
    return 0;
}
