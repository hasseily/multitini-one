/* SPDX-License-Identifier: GPL-3.0-only */
#include "gameboy_content_service.h"

#include "multitini_core_package.h"
#include "multitini_gameboy_boot.h"
#include "multitini_gameboy_interface.h"
#include "multitini_regs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MT_GAMEBOY_CONTENT_PATH_CAPACITY 256U
#define MT_GAMEBOY_CONTENT_POLLS UINT32_C(100000)
#define MT_GAMEBOY_MEDIA_DRAIN_POLLS UINT32_C(1000000)
#define MT_GAMEBOY_MEDIA_BUSY_RETRIES UINT32_C(16)
#define MT_GAMEBOY_MEDIA_SLOT_SAVE UINT32_C(0)

typedef enum {
    MT_GAMEBOY_RECEIVE_OK = 0,
    MT_GAMEBOY_RECEIVE_TIMEOUT,
    MT_GAMEBOY_RECEIVE_BAD_LAST
} mt_gameboy_receive_result_t;

static mt_core_link_t *content_link;
static mt_link_progress_fn content_progress;
static void *content_progress_context;
static uint8_t content_buffer[MT_SIZE_GB_CART_MAX];
static uint8_t save_buffer[MT_SIZE_GB_CART_RAM_MAX];
static char content_directory[MT_CORE_CONTENT_ROOT_MAX];
static bool cartridge_attached;
static char cartridge_name[MT_GAMEBOY_CONTENT_PATH_CAPACITY];

static mt_persistent_save_t persistent_save;
static mt_persistent_save_result_t last_save_result;
static size_t active_save_size;
static uint32_t save_generation;
static uint32_t next_generation;
static uint32_t serviced_request_id;
static uint32_t serviced_request_generation;
static uint32_t completion_request_id;
static uint32_t completion_status;
static uint32_t completion_block_count;
static uint32_t completion_generation;
static bool save_active;
static bool save_mounted;
static bool save_faulted;
static bool save_dirty_cached;
static bool save_resume_pending;
static bool save_capable;
static bool request_seen;
static bool completion_pending;

static mt_sd_result_t empty_sd_result(void)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    return result;
}

static mt_persistent_save_result_t empty_save_result(void)
{
    mt_persistent_save_result_t result;
    result.status = MT_PERSISTENT_SAVE_OK;
    result.sd = empty_sd_result();
    return result;
}

static void service_progress(void)
{
    if (content_progress != NULL) content_progress(content_progress_context);
}

static mt_gameboy_content_service_result_t service_result(
    mt_gameboy_content_service_status_t status)
{
    mt_gameboy_content_service_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.sd = empty_sd_result();
    result.validation = MT_CONTENT_VALID;
    result.link = MT_LINK_OK;
    result.save = last_save_result;
    result.save_size = active_save_size;
    result.save_active = save_active;
    result.save_existed = persistent_save.existed;
    return result;
}

static bool valid_root(const char *root)
{
    const size_t length = root == NULL ? 0U : strlen(root);
    return length >= 3U && length < sizeof(content_directory) &&
           root[0] >= '0' && root[0] <= '9' && root[1] == ':' &&
           root[2] == '/' && strstr(root, "..") == NULL;
}

static int cartridge_path(const char *file_name, char *path, size_t capacity,
                          mt_content_type_t *type)
{
    size_t name_length;
    size_t root_length;
    size_t index;
    mt_content_type_t classified;
    if (file_name == NULL || path == NULL || type == NULL ||
        content_directory[0] == '\0' || file_name[0] == '\0' ||
        strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
        return -1;
    }
    classified = mt_content_classify(file_name);
    if (classified != MT_CONTENT_GB_CART &&
        classified != MT_CONTENT_GBC_CART) return -1;
    name_length = strlen(file_name);
    root_length = strlen(content_directory);
    if (root_length + 1U + name_length + 1U > capacity) return -1;
    for (index = 0U; index < name_length; ++index) {
        if (file_name[index] == '/' || file_name[index] == '\\' ||
            file_name[index] == ':') return -1;
    }
    memcpy(path, content_directory, root_length);
    path[root_length] = '/';
    memcpy(path + root_length + 1U, file_name, name_length + 1U);
    *type = classified;
    return 0;
}

static bool mapper_has_external_battery_ram(uint8_t mapper)
{
    switch (mapper) {
    case 0x03U: /* MBC1+RAM+BATTERY */
    case 0x06U: /* MBC2+BATTERY */
    case 0x09U: /* ROM+RAM+BATTERY */
    case 0x0dU: /* MMM01+RAM+BATTERY */
    case 0x10U: /* MBC3+TIMER+RAM+BATTERY */
    case 0x13U: /* MBC3+RAM+BATTERY */
    case 0x1bU: /* MBC5+RAM+BATTERY */
    case 0x1eU: /* MBC5+RUMBLE+RAM+BATTERY */
    case 0x20U: /* MBC6 */
    case 0xfcU: /* Game Boy Camera */
    case 0xfeU: /* HuC3 */
    case 0xffU: /* HuC1+RAM+BATTERY */
        return true;
    default:
        return false;
    }
}

static size_t cartridge_save_size(const mt_content_info_t *info)
{
    if (info == NULL ||
        !mapper_has_external_battery_ram(info->mapper_type)) return 0U;
    if (info->mapper_type == 0x06U) return 512U;
    switch (info->ram_size_code) {
    case 1U: return 2U * 1024U;
    case 2U: return 8U * 1024U;
    case 3U: return 32U * 1024U;
    case 4U: return 128U * 1024U;
    case 5U: return 64U * 1024U;
    default: return 0U;
    }
}

static void clear_core_load_error(void)
{
    content_link->write32(content_link->context, MT_REG_ERROR,
                          MT_ERROR_CORE_LOAD);
}

static mt_link_result_t wait_for_core_result(void)
{
    uint32_t poll;
    for (poll = 0U; poll < MT_GAMEBOY_CONTENT_POLLS; ++poll) {
        uint32_t status;
        service_progress();
        status = content_link->read32(content_link->context, MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U)
            return MT_LINK_REMOTE_ERROR;
        if ((status & MT_STATUS_CORE_READY) != 0U)
            return MT_LINK_OK;
    }
    return MT_LINK_TIMEOUT;
}

static mt_link_result_t set_core_reset(bool asserted)
{
    const uint32_t args[4] = {asserted ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(
        content_link, MT_COMMAND_CORE_RESET, args, MT_GAMEBOY_CONTENT_POLLS,
        content_progress, content_progress_context, &response);
}

static mt_link_result_t media_command(uint32_t opcode, uint32_t arg0,
                                      uint32_t arg1, uint32_t arg2,
                                      uint32_t arg3)
{
    const uint32_t args[4] = {arg0, arg1, arg2, arg3};
    uint32_t attempt;
    if (content_link == NULL) return MT_LINK_BAD_ARGUMENT;
    for (attempt = 0U; attempt < MT_GAMEBOY_MEDIA_BUSY_RETRIES; ++attempt) {
        mt_command_response_t response = {0U, 0U, 0U, 0U};
        const mt_link_result_t result = mt_core_link_command(
            content_link, opcode, args, MT_GAMEBOY_CONTENT_POLLS,
            content_progress, content_progress_context, &response);
        if (result != MT_LINK_REMOTE_ERROR ||
            response.status != MT_RESPONSE_BUSY) return result;
    }
    return MT_LINK_TIMEOUT;
}

static uint32_t allocate_generation(void)
{
    uint32_t result = next_generation++;
    if (result == 0U) result = next_generation++;
    return result;
}

static bool read_stable_media_state(uint32_t *state)
{
    uint32_t first;
    uint32_t second;
    if (state == NULL || content_link == NULL) return false;
    first = content_link->read32(content_link->context, MT_REG_MEDIA_STATE);
    second = content_link->read32(content_link->context, MT_REG_MEDIA_STATE);
    if (first != second) return false;
    *state = second;
    save_dirty_cached = (second & MT_MEDIA_STATE_DIRTY) != 0U;
    return true;
}

static bool request_matches(uint32_t request_id, uint32_t generation)
{
    const uint32_t request_word = content_link->read32(
        content_link->context, MT_REG_MEDIA_REQUEST);
    return (request_word & MT_MEDIA_REQUEST_VALID) != 0U &&
           content_link->read32(content_link->context,
                                MT_REG_MEDIA_REQUEST_ID) == request_id &&
           content_link->read32(content_link->context,
                                MT_REG_MEDIA_REQUEST_GENERATION) == generation;
}

static int submit_pending_completion(void)
{
    mt_link_result_t result;
    if (!completion_pending) return 0;
    result = media_command(MT_COMMAND_MEDIA_COMPLETE,
                           completion_request_id, completion_status,
                           completion_block_count, completion_generation);
    if (result == MT_LINK_OK) {
        completion_pending = false;
        return 0;
    }
    if (!request_matches(completion_request_id, completion_generation)) {
        completion_pending = false;
        return 0;
    }
    return -1;
}

static void queue_completion(uint32_t request_id, uint32_t status,
                             uint32_t block_count, uint32_t generation)
{
    completion_request_id = request_id;
    completion_status = status;
    completion_block_count = block_count;
    completion_generation = generation;
    completion_pending = true;
    if (status != MT_RESPONSE_OK) save_faulted = true;
    (void)submit_pending_completion();
}

static mt_gameboy_receive_result_t receive_save_data(size_t expected_size)
{
    size_t offset;
    uint32_t idle_polls = 0U;
    for (offset = 0U; offset < expected_size;) {
        const uint32_t word = content_link->read32(
            content_link->context, MT_REG_MEDIA_DATA);
        bool expected_last;
        bool actual_last;
        if ((word & MT_MEDIA_DATA_VALID) == 0U) {
            if (++idle_polls >= MT_GAMEBOY_CONTENT_POLLS)
                return MT_GAMEBOY_RECEIVE_TIMEOUT;
            service_progress();
            continue;
        }
        idle_polls = 0U;
        expected_last = offset + 1U == expected_size;
        actual_last = (word & MT_MEDIA_DATA_LAST) != 0U;
        save_buffer[offset++] = (uint8_t)(
            (word & MT_MEDIA_DATA_BYTE_MASK) >> MT_MEDIA_DATA_BYTE_SHIFT);
        if (actual_last != expected_last)
            return MT_GAMEBOY_RECEIVE_BAD_LAST;
    }
    for (idle_polls = 0U; idle_polls < MT_GAMEBOY_CONTENT_POLLS;
         ++idle_polls) {
        if ((content_link->read32(content_link->context, MT_REG_STATUS) &
             MT_STATUS_MEDIA_DATA) == 0U) return MT_GAMEBOY_RECEIVE_OK;
        service_progress();
    }
    return MT_GAMEBOY_RECEIVE_TIMEOUT;
}

static mt_link_result_t mount_save_media(size_t size)
{
    const uint32_t generation = allocate_generation();
    const uint32_t flags = MT_GAMEBOY_MEDIA_SLOT_SAVE |
        ((uint32_t)MT_LOAD_GB_SAVE << MT_MEDIA_KIND_SHIFT);
    const mt_link_result_t result = media_command(
        MT_COMMAND_MEDIA_MOUNT, flags, (uint32_t)size, generation, 0U);
    if (result == MT_LINK_OK) {
        save_generation = generation;
        save_mounted = true;
        save_faulted = false;
        save_dirty_cached = false;
        save_resume_pending = false;
        request_seen = false;
        completion_pending = false;
    }
    return result;
}

static void close_save(void)
{
    mt_persistent_save_close(&persistent_save);
    active_save_size = 0U;
    save_generation = 0U;
    save_active = false;
    save_mounted = false;
    save_faulted = false;
    save_dirty_cached = false;
    save_resume_pending = false;
    request_seen = false;
    completion_pending = false;
}

static void forget_cartridge(void)
{
    cartridge_attached = false;
    cartridge_name[0] = '\0';
}

void gameboy_content_service_init(mt_core_link_t *link,
                                  const char *content_root,
                                  bool persistent_save_supported,
                                  mt_link_progress_fn progress,
                                  void *progress_context)
{
    content_link = link;
    content_progress = progress;
    content_progress_context = progress_context;
    content_directory[0] = '\0';
    if (valid_root(content_root))
        memcpy(content_directory, content_root, strlen(content_root) + 1U);
    forget_cartridge();
    memset(&persistent_save, 0, sizeof(persistent_save));
    last_save_result = empty_save_result();
    active_save_size = 0U;
    save_generation = 0U;
    next_generation = 1U;
    serviced_request_id = 0U;
    serviced_request_generation = 0U;
    completion_request_id = 0U;
    completion_status = 0U;
    completion_block_count = 0U;
    completion_generation = 0U;
    save_active = false;
    save_mounted = false;
    save_faulted = false;
    save_dirty_cached = false;
    save_resume_pending = false;
    save_capable = persistent_save_supported;
    request_seen = false;
    completion_pending = false;
}

void gameboy_content_service_poll(void)
{
    uint32_t request_word;
    uint32_t request_id;
    uint32_t request_lba;
    uint32_t request_blocks_word;
    uint32_t request_generation;
    uint32_t blocks;
    size_t expected_size;
    mt_gameboy_receive_result_t receive_result;

    if (content_link == NULL || !save_active) return;
    if (completion_pending) {
        (void)submit_pending_completion();
        return;
    }
    request_word = content_link->read32(content_link->context,
                                        MT_REG_MEDIA_REQUEST);
    if ((request_word & MT_MEDIA_REQUEST_VALID) == 0U) {
        request_seen = false;
        return;
    }
    request_id = content_link->read32(content_link->context,
                                      MT_REG_MEDIA_REQUEST_ID);
    request_generation = content_link->read32(
        content_link->context, MT_REG_MEDIA_REQUEST_GENERATION);
    if (request_seen && request_id == serviced_request_id &&
        request_generation == serviced_request_generation) return;
    if (save_faulted) return;

    request_seen = true;
    serviced_request_id = request_id;
    serviced_request_generation = request_generation;
    request_lba = content_link->read32(content_link->context,
                                       MT_REG_MEDIA_REQUEST_LBA);
    request_blocks_word = content_link->read32(
        content_link->context, MT_REG_MEDIA_REQUEST_BLOCK_COUNT);
    blocks = request_blocks_word & MT_MEDIA_BLOCK_COUNT_MASK;
    expected_size = (size_t)blocks * (size_t)MT_MEDIA_BLOCK_BYTES;
    if (!save_mounted || request_generation != save_generation ||
        (request_word & MT_MEDIA_REQUEST_WRITE) == 0U || request_lba != 0U ||
        (request_blocks_word & ~MT_MEDIA_BLOCK_COUNT_MASK) != 0U ||
        blocks == 0U || expected_size != active_save_size) {
        queue_completion(request_id, MT_RESPONSE_INVALID_ARGUMENT, 0U,
                         request_generation);
        return;
    }

    receive_result = receive_save_data(expected_size);
    if (receive_result != MT_GAMEBOY_RECEIVE_OK) {
        queue_completion(request_id,
                         receive_result == MT_GAMEBOY_RECEIVE_TIMEOUT ?
                             MT_RESPONSE_TIMEOUT : MT_RESPONSE_IO_ERROR,
                         0U, request_generation);
        return;
    }
    last_save_result = mt_persistent_save_commit(
        &persistent_save, save_buffer, active_save_size);
    if (last_save_result.status != MT_PERSISTENT_SAVE_OK) {
        queue_completion(request_id, MT_RESPONSE_IO_ERROR, 0U,
                         request_generation);
        return;
    }
    save_faulted = false;
    save_dirty_cached = false;
    queue_completion(request_id, MT_RESPONSE_OK, blocks, request_generation);
}

int gameboy_content_service_retry_save(void)
{
    uint32_t poll;
    uint32_t request_word = MT_MEDIA_REQUEST_VALID;
    if (content_link == NULL || !save_active) return -1;
    if (!save_faulted) return 0;
    if (completion_pending && submit_pending_completion() != 0) return -1;
    for (poll = 0U; poll < MT_GAMEBOY_MEDIA_DRAIN_POLLS; ++poll) {
        request_word = content_link->read32(content_link->context,
                                            MT_REG_MEDIA_REQUEST);
        if ((request_word & MT_MEDIA_REQUEST_VALID) == 0U) break;
        service_progress();
    }
    if ((request_word & MT_MEDIA_REQUEST_VALID) != 0U) return -1;
    clear_core_load_error();
    save_faulted = false;
    request_seen = false;
    return 0;
}

int gameboy_content_service_prepare_reset(void)
{
    uint32_t poll;
    uint32_t state;
    uint32_t eject_generation;
    mt_link_result_t link_result;
    if (!save_active || (!save_mounted && save_resume_pending)) return 0;
    if (!save_mounted) return -1;
    if (save_faulted && gameboy_content_service_retry_save() != 0) return -1;

    eject_generation = allocate_generation();
    link_result = media_command(MT_COMMAND_MEDIA_EJECT,
                                MT_GAMEBOY_MEDIA_SLOT_SAVE,
                                eject_generation, 0U, 0U);
    if (link_result != MT_LINK_OK) return -1;
    for (poll = 0U; poll < MT_GAMEBOY_MEDIA_DRAIN_POLLS; ++poll) {
        gameboy_content_service_poll();
        if (read_stable_media_state(&state) &&
            (state & (MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
                      MT_MEDIA_STATE_EJECT_PENDING)) == 0U) {
            save_generation = eject_generation;
            save_mounted = false;
            save_dirty_cached = false;
            save_resume_pending = true;
            request_seen = false;
            completion_pending = false;
            return 0;
        }
        if (save_faulted && !completion_pending) return -1;
        service_progress();
    }
    return -1;
}

int gameboy_content_service_resume_after_reset(void)
{
    if (!save_resume_pending) return 0;
    if (!save_active || active_save_size == 0U) return -1;
    return mount_save_media(active_save_size) == MT_LINK_OK ? 0 : -1;
}

mt_gameboy_content_service_result_t gameboy_content_service_load(
    const char *file_name)
{
    mt_gameboy_content_service_result_t result =
        service_result(MT_GAMEBOY_CONTENT_SERVICE_OK);
    mt_gameboy_boot_images_t images;
    mt_content_info_t info;
    mt_content_type_t type;
    mt_persistent_save_t candidate_save;
    mt_persistent_save_result_t candidate_save_result;
    size_t candidate_save_size;
    char path[MT_GAMEBOY_CONTENT_PATH_CAPACITY];

    memset(&candidate_save, 0, sizeof(candidate_save));
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL ||
        cartridge_path(file_name, path, sizeof(path), &type) != 0) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }
    result.sd = mt_sd_storage_read_file_max(path, content_buffer,
                                            sizeof(content_buffer));
    if (result.sd.status != MT_SD_OK) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_READ_FAILED;
        return result;
    }
    result.validation = mt_content_inspect(type, content_buffer,
                                           result.sd.size, &info);
    if (result.validation != MT_CONTENT_VALID) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_REJECTED;
        return result;
    }
    result.info = info;
    candidate_save_size = save_capable ? cartridge_save_size(&info) : 0U;

    if (save_active && gameboy_content_service_prepare_reset() != 0) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED;
        result.save = last_save_result;
        return result;
    }
    if (candidate_save_size != 0U) {
        candidate_save_result = mt_persistent_save_open(
            &candidate_save, content_directory, file_name, save_buffer,
            candidate_save_size, 0xffU);
        result.save = candidate_save_result;
        result.save_size = candidate_save_size;
        result.save_active = true;
        result.save_existed = candidate_save.existed;
        if (candidate_save_result.status != MT_PERSISTENT_SAVE_OK) {
            result.status = MT_GAMEBOY_CONTENT_SERVICE_SAVE_FAILED;
            (void)gameboy_content_service_resume_after_reset();
            return result;
        }
    } else {
        candidate_save_result = empty_save_result();
    }

    images.cartridge.data = content_buffer;
    images.cartridge.size = result.sd.size;
    images.bootstrap_override.data = NULL;
    images.bootstrap_override.size = 0U;
    images.save.data = candidate_save_size != 0U ? save_buffer : NULL;
    images.save.size = candidate_save_size;
    clear_core_load_error();
    result.link = mt_gameboy_stage(
        content_link, &images, MT_GAMEBOY_CONTENT_POLLS,
        content_progress, content_progress_context);
    if (result.link != MT_LINK_OK) {
        forget_cartridge();
        close_save();
        mt_persistent_save_close(&candidate_save);
        result.status = MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }

    forget_cartridge();
    close_save();
    persistent_save = candidate_save;
    last_save_result = candidate_save_result;
    active_save_size = candidate_save_size;
    save_active = candidate_save_size != 0U;
    if (save_active) {
        result.link = mount_save_media(active_save_size);
        if (result.link != MT_LINK_OK) {
            close_save();
            result.status = MT_GAMEBOY_CONTENT_SERVICE_SAVE_MOUNT_FAILED;
            return result;
        }
    }
    result.link = mt_gameboy_release(
        content_link, MT_GAMEBOY_CONTENT_POLLS,
        content_progress, content_progress_context);
    if (result.link == MT_LINK_OK) result.link = wait_for_core_result();
    if (result.link != MT_LINK_OK) {
        if (save_active && gameboy_content_service_prepare_reset() == 0)
            close_save();
        result.status = MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }

    cartridge_attached = true;
    memcpy(cartridge_name, file_name, strlen(file_name) + 1U);
    result.save = last_save_result;
    result.save_size = active_save_size;
    result.save_active = save_active;
    result.save_existed = persistent_save.existed;
    return result;
}

mt_gameboy_content_service_result_t gameboy_content_service_detach(void)
{
    mt_gameboy_content_service_result_t result =
        service_result(MT_GAMEBOY_CONTENT_SERVICE_OK);
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }
    if (save_active && gameboy_content_service_prepare_reset() != 0) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED;
        result.save = last_save_result;
        return result;
    }
    result.link = set_core_reset(true);
    if (result.link != MT_LINK_OK) {
        (void)gameboy_content_service_resume_after_reset();
        result.status = MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    forget_cartridge();
    close_save();
    result.save_size = 0U;
    result.save_active = false;
    result.save_existed = false;
    return result;
}

bool gameboy_content_service_cartridge_attached(void)
{
    return cartridge_attached;
}

const char *gameboy_content_service_cartridge_name(void)
{
    return cartridge_attached ? cartridge_name : NULL;
}

bool gameboy_content_service_save_active(void)
{
    return save_active;
}

bool gameboy_content_service_save_dirty(void)
{
    uint32_t state;
    if (save_active && read_stable_media_state(&state))
        save_dirty_cached = (state & MT_MEDIA_STATE_DIRTY) != 0U;
    return save_dirty_cached;
}

bool gameboy_content_service_save_faulted(void)
{
    return save_faulted;
}

const char *gameboy_content_service_save_path(void)
{
    return save_active ? persistent_save.path : NULL;
}

const char *gameboy_content_service_status_name(
    mt_gameboy_content_service_status_t status)
{
    switch (status) {
    case MT_GAMEBOY_CONTENT_SERVICE_OK: return "ok";
    case MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT:
        return "invalid argument";
    case MT_GAMEBOY_CONTENT_SERVICE_READ_FAILED: return "read failed";
    case MT_GAMEBOY_CONTENT_SERVICE_REJECTED: return "content rejected";
    case MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED: return "transfer failed";
    case MT_GAMEBOY_CONTENT_SERVICE_SAVE_FAILED: return "save read failed";
    case MT_GAMEBOY_CONTENT_SERVICE_SAVE_MOUNT_FAILED:
        return "save mount failed";
    case MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED: return "save flush failed";
    }
    return "unknown Game Boy content service status";
}
