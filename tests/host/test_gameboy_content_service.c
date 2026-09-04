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
    uint8_t active_kind;
    uint8_t completed_kinds[4];
    uint32_t completed_sizes[4];
    unsigned completed_streams;
    unsigned reset_commands;
    unsigned mount_commands;
    unsigned eject_commands;
    unsigned completion_commands;
    uint32_t mount_flags;
    uint32_t mount_size;
    uint32_t mount_generation;
    uint32_t completion_status;
    uint32_t completion_blocks;
    uint8_t media_data[MT_SIZE_GB_CART_RAM_MAX];
    size_t media_data_size;
    size_t media_data_offset;
    int raise_load_error_on_end;
} shell_t;

static char requested_path[256];
static char requested_save_path[384];
static char requested_journal_path[384];
static char committed_save_path[384];
static char committed_journal_path[384];
static uint8_t committed_save[MT_SIZE_GB_CART_RAM_MAX];
static size_t committed_size;
static uint8_t cartridge_mapper = 0x1bU;
static uint8_t cartridge_ram_size = 0x03U;
static int fail_read;
static int fail_save_read;
static int fail_save_commit;
static bool save_exists;

static size_t register_index(uint32_t offset)
{
    return (size_t)(offset / (uint32_t)sizeof(uint32_t));
}

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    if (offset == MT_REG_MEDIA_DATA) {
        uint32_t value;
        bool last;
        if (shell->media_data_offset >= shell->media_data_size) return 0U;
        last = shell->media_data_offset + 1U == shell->media_data_size;
        value = MT_MEDIA_DATA_VALID |
            ((uint32_t)shell->media_data[shell->media_data_offset] <<
             MT_MEDIA_DATA_BYTE_SHIFT) |
            (last ? MT_MEDIA_DATA_LAST : 0U);
        ++shell->media_data_offset;
        if (last)
            shell->registers[register_index(MT_REG_STATUS)] &=
                ~MT_STATUS_MEDIA_DATA;
        return value;
    }
    return shell->registers[register_index(offset)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    if (offset == MT_REG_ERROR && (value & MT_ERROR_CORE_LOAD) != 0U) {
        shell->registers[register_index(MT_REG_STATUS)] &=
            ~MT_STATUS_CORE_LOAD_ERROR;
        return;
    }
    shell->registers[register_index(offset)] = value;
}

static void respond(void *context)
{
    shell_t *const shell = context;
    const uint32_t sequence =
        shell->registers[register_index(MT_REG_COMMAND_SEQ)];
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
    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    if (opcode == MT_COMMAND_STREAM_BEGIN) {
        shell->active_kind = (uint8_t)arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
    } else if (opcode == MT_COMMAND_STREAM_DATA) {
        ++shell->active_offset;
    } else if (opcode == MT_COMMAND_STREAM_END) {
        assert(shell->active_offset == shell->active_size);
        assert(shell->completed_streams < 4U);
        shell->completed_kinds[shell->completed_streams] = shell->active_kind;
        shell->completed_sizes[shell->completed_streams] = shell->active_size;
        ++shell->completed_streams;
        if (shell->raise_load_error_on_end != 0)
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_LOAD_ERROR;
    } else if (opcode == MT_COMMAND_CORE_RESET) {
        ++shell->reset_commands;
        if (arg0 == 0U)
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_READY;
        else
            shell->registers[register_index(MT_REG_STATUS)] &=
                ~MT_STATUS_CORE_READY;
    } else if (opcode == MT_COMMAND_MEDIA_MOUNT) {
        ++shell->mount_commands;
        shell->mount_flags = arg0;
        shell->mount_size = arg1;
        shell->mount_generation = arg2;
        shell->registers[register_index(MT_REG_MEDIA_STATE)] =
            MT_MEDIA_STATE_MOUNTED;
    } else if (opcode == MT_COMMAND_MEDIA_EJECT) {
        ++shell->eject_commands;
        if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
             MT_MEDIA_STATE_DIRTY) != 0U) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] |=
                MT_MEDIA_STATE_EJECT_PENDING;
        } else {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
        }
    } else if (opcode == MT_COMMAND_MEDIA_COMPLETE) {
        ++shell->completion_commands;
        shell->completion_status = arg1;
        shell->completion_blocks = arg2;
        assert(arg0 ==
               shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)]);
        assert(arg3 == shell->registers[
                           register_index(MT_REG_MEDIA_REQUEST_GENERATION)]);
        shell->registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
        if (arg1 == MT_RESPONSE_OK) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] &=
                ~MT_MEDIA_STATE_DIRTY;
            if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
                 MT_MEDIA_STATE_EJECT_PENDING) != 0U)
                shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
        } else {
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_LOAD_ERROR;
        }
    } else {
        assert(opcode == MT_COMMAND_STREAM_ABORT);
    }
    shell->registers[register_index(MT_REG_RESPONSE_STATUS)] = MT_RESPONSE_OK;
    shell->registers[register_index(MT_REG_RESPONSE_SEQ)] = sequence;
}

static void finish_header(uint8_t *cart, size_t size)
{
    uint8_t checksum = 0U;
    size_t index;
    memset(cart, 0, size);
    cart[0x143U] = 0x80U;
    cart[0x147U] = cartridge_mapper;
    cart[0x148U] = 0x00U;
    cart[0x149U] = cartridge_ram_size;
    for (index = 0x134U; index <= 0x14cU; ++index)
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

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)recovery_buffer;
    (void)capacity;
    strcpy(requested_save_path, path);
    strcpy(requested_journal_path, journal_path);
    return result;
}

mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(strcmp(path, requested_save_path) == 0);
    if (fail_save_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 11U;
        return result;
    }
    *found = save_exists;
    if (save_exists) {
        assert(expected_size == committed_size);
        memcpy(dst, committed_save, expected_size);
        result.size = expected_size;
    }
    return result;
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    strcpy(committed_save_path, path);
    strcpy(committed_journal_path, journal_path);
    if (fail_save_commit != 0) {
        result.status = MT_SD_WRITE_FAILED;
        result.backend_status = 12U;
        return result;
    }
    assert(size <= sizeof(committed_save));
    memcpy(committed_save, src, size);
    committed_size = size;
    save_exists = true;
    result.size = size;
    return result;
}

static void queue_save_request(shell_t *shell, uint32_t request_id)
{
    size_t index;
    assert(shell->mount_size <= sizeof(shell->media_data));
    for (index = 0U; index < shell->mount_size; ++index)
        shell->media_data[index] = (uint8_t)(index ^ request_id);
    shell->media_data_size = shell->mount_size;
    shell->media_data_offset = 0U;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST)] =
        MT_MEDIA_REQUEST_VALID | MT_MEDIA_REQUEST_WRITE;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)] = request_id;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_LBA)] = 0U;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_BLOCK_COUNT)] =
        shell->mount_size / MT_MEDIA_BLOCK_BYTES;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_GENERATION)] =
        shell->mount_generation;
    shell->registers[register_index(MT_REG_MEDIA_STATE)] |=
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    shell->registers[register_index(MT_REG_STATUS)] |= MT_STATUS_MEDIA_DATA;
}

int main(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    mt_gameboy_content_service_result_t result;
    size_t index;

    mt_core_link_init(&link, &shell, read32, write32);
    gameboy_content_service_init(&link, "0:/multitini/cores/gbc/media", true,
                                 respond, &shell);
    result = gameboy_content_service_load("demo.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
    assert(strcmp(requested_path,
                  "0:/multitini/cores/gbc/media/demo.gbc") == 0);
    assert(strcmp(requested_save_path,
                  "0:/multitini/cores/gbc/media/demo.gbc.sav") == 0);
    assert(strcmp(requested_journal_path,
                  "0:/multitini/cores/gbc/media/demo.gbc.sav.mtj") == 0);
    assert(shell.completed_streams == 2U);
    assert(shell.completed_kinds[0] == MT_LOAD_GB_CART);
    assert(shell.completed_kinds[1] == MT_LOAD_GB_SAVE);
    assert(shell.completed_sizes[1] == 32U * 1024U);
    assert(shell.reset_commands == 2U);
    assert(shell.mount_commands == 1U);
    assert((shell.mount_flags & MT_MEDIA_KIND_MASK) ==
           ((uint32_t)MT_LOAD_GB_SAVE << MT_MEDIA_KIND_SHIFT));
    assert(shell.mount_size == 32U * 1024U);
    assert(result.info.mapper_type == 0x1bU);
    assert(result.info.ram_size_code == 0x03U);
    assert(result.save_active && !result.save_existed);
    assert(result.save_size == 32U * 1024U);
    assert(gameboy_content_service_cartridge_attached());
    assert(gameboy_content_service_save_active());
    assert(!gameboy_content_service_save_dirty());
    assert(strcmp(gameboy_content_service_cartridge_name(), "demo.gbc") == 0);
    assert(strcmp(gameboy_content_service_save_path(), requested_save_path) ==
           0);

    queue_save_request(&shell, 7U);
    assert(gameboy_content_service_save_dirty());
    gameboy_content_service_poll();
    assert(shell.completion_commands == 1U);
    assert(shell.completion_status == MT_RESPONSE_OK);
    assert(shell.completion_blocks == 128U);
    assert(committed_size == 32U * 1024U);
    assert(strcmp(committed_save_path, requested_save_path) == 0);
    assert(strcmp(committed_journal_path, requested_journal_path) == 0);
    for (index = 0U; index < committed_size; ++index)
        assert(committed_save[index] == (uint8_t)(index ^ 7U));
    assert(!gameboy_content_service_save_dirty());
    assert(!gameboy_content_service_save_faulted());

    fail_save_commit = 1;
    queue_save_request(&shell, 8U);
    gameboy_content_service_poll();
    assert(shell.completion_commands == 2U);
    assert(shell.completion_status == MT_RESPONSE_IO_ERROR);
    assert(gameboy_content_service_save_faulted());
    assert(gameboy_content_service_save_dirty());
    assert(gameboy_content_service_retry_save() == 0);
    assert(!gameboy_content_service_save_faulted());
    fail_save_commit = 0;
    queue_save_request(&shell, 9U);
    gameboy_content_service_poll();
    assert(shell.completion_status == MT_RESPONSE_OK);

    fail_save_commit = 1;
    queue_save_request(&shell, 10U);
    assert(gameboy_content_service_prepare_reset() != 0);
    assert(gameboy_content_service_cartridge_attached());
    assert(gameboy_content_service_save_active());
    assert(gameboy_content_service_save_faulted());
    assert((shell.registers[register_index(MT_REG_MEDIA_STATE)] &
            (MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
             MT_MEDIA_STATE_EJECT_PENDING)) ==
           (MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
            MT_MEDIA_STATE_EJECT_PENDING));
    fail_save_commit = 0;
    assert(gameboy_content_service_retry_save() == 0);
    queue_save_request(&shell, 11U);
    gameboy_content_service_poll();
    assert(shell.completion_status == MT_RESPONSE_OK);

    assert(gameboy_content_service_prepare_reset() == 0);
    assert(shell.eject_commands == 2U);
    assert(gameboy_content_service_save_active());
    assert(gameboy_content_service_resume_after_reset() == 0);
    assert(shell.mount_commands == 2U);

    result = gameboy_content_service_detach();
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
    assert(shell.reset_commands == 3U);
    assert(shell.eject_commands == 3U);
    assert(!gameboy_content_service_cartridge_attached());
    assert(!gameboy_content_service_save_active());
    assert(!result.save_active);
    assert(!result.save_existed);
    assert(result.save_size == 0U);

    cartridge_mapper = 0x19U;
    cartridge_ram_size = 0U;
    shell.completed_streams = 0U;
    result = gameboy_content_service_load("plain.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
    assert(shell.completed_streams == 1U);
    assert(shell.completed_kinds[0] == MT_LOAD_GB_CART);
    assert(!result.save_active);
    assert(!gameboy_content_service_save_active());

    result = gameboy_content_service_load("bad.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_REJECTED);
    assert(result.validation == MT_CONTENT_BAD_STRUCTURE);
    assert(gameboy_content_service_cartridge_attached());

    cartridge_mapper = 0x1bU;
    cartridge_ram_size = 3U;
    fail_save_read = 1;
    result = gameboy_content_service_load("savefail.gbc");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_SAVE_FAILED);
    assert(gameboy_content_service_cartridge_attached());
    fail_save_read = 0;

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

    fail_read = 0;
    shell.completed_streams = 0U;
    {
        const unsigned mounts_before = shell.mount_commands;
        gameboy_content_service_init(
            &link, "0:/multitini/cores/gbc/media", false, respond, &shell);
        result = gameboy_content_service_load("legacy.gbc");
        assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_OK);
        assert(shell.completed_streams == 1U);
        assert(shell.completed_kinds[0] == MT_LOAD_GB_CART);
        assert(shell.mount_commands == mounts_before);
        assert(!result.save_active);
    }

    gameboy_content_service_init(&link, "relative/path", true, respond,
                                 &shell);
    result = gameboy_content_service_load("demo.gb");
    assert(result.status == MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT);
    return 0;
}
