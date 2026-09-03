/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"

#include "multitini_c64_d64.h"
#include "multitini_c64_g64.h"
#include "multitini_c64_interface.h"
#include "multitini_content.h"
#include "multitini_regs.h"
#include "sd_storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xil_printf.h"

#define MT_C64_MEDIA_SLOT_DRIVE8 UINT32_C(0)
#define MT_C64_MEDIA_PATH_CAPACITY 256U
#define MT_C64_MEDIA_JOURNAL_PATH_CAPACITY 264U
#define MT_C64_MEDIA_IMAGE_CAPACITY ((size_t)1048576)
#define MT_C64_MEDIA_BUSY_RETRIES UINT32_C(16)
#define MT_C64_MEDIA_DATA_IDLE_POLLS UINT32_C(100000)
#define MT_C64_MEDIA_DRAIN_POLLS UINT32_C(1000000)
#define MT_C64_MEDIA_EJECT_SETTLE_POLLS UINT32_C(4)

typedef enum {
    MT_C64_MEDIA_NONE = 0,
    MT_C64_MEDIA_D64,
    MT_C64_MEDIA_G64
} mt_c64_media_type_t;

typedef enum {
    MT_C64_RECEIVE_OK = 0,
    MT_C64_RECEIVE_TIMEOUT,
    MT_C64_RECEIVE_BAD_LAST
} mt_c64_receive_result_t;

static mt_core_link_t *media_link;
static mt_link_progress_fn media_progress;
static void *media_progress_context;
static uint8_t media_image[MT_C64_MEDIA_IMAGE_CAPACITY];
static uint8_t transfer_buffer[MT_C64_G64_TRACK_BUFFER_SIZE];
static mt_c64_g64_image_t g64_image;
static size_t media_image_size;
static uint32_t mount_generation;
static uint32_t next_generation;
static uint32_t serviced_request_id;
static uint32_t serviced_request_generation;
static uint32_t pending_eject_generation;
static uint32_t eject_settle_polls;
static uint32_t completion_request_id;
static uint32_t completion_status;
static uint32_t completion_block_count;
static uint32_t completion_generation;
static mt_c64_media_type_t mounted_type;
static bool request_seen;
static bool mounted;
static bool mounted_read_only;
static bool image_dirty;
static bool service_faulted;
static bool completion_pending;
static bool eject_pending;
static bool eject_command_sent;
static bool reset_resume_pending;
static char mounted_name[MT_C64_MEDIA_PATH_CAPACITY];
static char mounted_path[MT_C64_MEDIA_PATH_CAPACITY];
static char mounted_journal_path[MT_C64_MEDIA_JOURNAL_PATH_CAPACITY];
static char reset_resume_name[MT_C64_MEDIA_PATH_CAPACITY];

static void service_progress(void)
{
    if (media_progress != NULL) media_progress(media_progress_context);
}

static uint32_t allocate_generation(void)
{
    uint32_t result = next_generation++;
    if (result == 0U) result = next_generation++;
    return result;
}

static mt_link_result_t media_command(uint32_t opcode,
                                      uint32_t arg0, uint32_t arg1,
                                      uint32_t arg2, uint32_t arg3)
{
    const uint32_t args[4] = {arg0, arg1, arg2, arg3};
    uint32_t attempt;

    if (media_link == NULL) return MT_LINK_BAD_ARGUMENT;
    for (attempt = 0U; attempt < MT_C64_MEDIA_BUSY_RETRIES; ++attempt) {
        mt_command_response_t response = {0U, 0U, 0U, 0U};
        const mt_link_result_t result = mt_core_link_command(
            media_link, opcode, args, UINT32_C(100000), media_progress,
            media_progress_context, &response);
        if (result != MT_LINK_REMOTE_ERROR ||
            response.status != MT_RESPONSE_BUSY) {
            return result;
        }
    }
    return MT_LINK_TIMEOUT;
}

static int media_path(const char *file_name, char *path, size_t capacity,
                      mt_content_type_t *content_type)
{
    const size_t directory_length = strlen(MT_C64_MEDIA_DIRECTORY);
    mt_content_type_t type;
    size_t name_length;
    size_t index;

    if (file_name == NULL || path == NULL || content_type == NULL ||
        *file_name == '\0') {
        return -1;
    }
    name_length = strlen(file_name);
    if (directory_length + 1U + name_length + 1U > capacity) return -1;
    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
        return -1;
    }
    for (index = 0U; index < name_length; ++index) {
        if (file_name[index] == '/' || file_name[index] == '\\' ||
            file_name[index] == ':') {
            return -1;
        }
    }
    type = mt_content_classify(file_name);
    if (type != MT_CONTENT_D64 && type != MT_CONTENT_G64) return -1;

    memcpy(path, MT_C64_MEDIA_DIRECTORY, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1U, file_name, name_length + 1U);
    *content_type = type;
    return 0;
}

static int make_journal_path(const char *path, char *result, size_t capacity)
{
    static const char suffix[] = ".mtj";
    const size_t path_length = strlen(path);
    if (path_length + sizeof(suffix) > capacity) return -1;
    memcpy(result, path, path_length);
    memcpy(result + path_length, suffix, sizeof(suffix));
    return 0;
}

static bool d64_has_error_tail(size_t size)
{
    return size != mt_c64_d64_sector_count(size) * MT_C64_D64_SECTOR_SIZE;
}

static const char *media_type_name(mt_c64_media_type_t type)
{
    switch (type) {
    case MT_C64_MEDIA_D64: return "D64";
    case MT_C64_MEDIA_G64: return "G64";
    case MT_C64_MEDIA_NONE: break;
    }
    return "none";
}

static void forget_mount(void)
{
    media_image_size = 0U;
    mount_generation = pending_eject_generation;
    mounted_type = MT_C64_MEDIA_NONE;
    mounted = false;
    mounted_read_only = true;
    image_dirty = false;
    service_faulted = false;
    completion_pending = false;
    eject_pending = false;
    eject_command_sent = false;
    pending_eject_generation = 0U;
    eject_settle_polls = 0U;
    request_seen = false;
    mounted_name[0] = '\0';
    mounted_path[0] = '\0';
    mounted_journal_path[0] = '\0';
    memset(&g64_image, 0, sizeof(g64_image));
}

static bool read_stable_media_state(uint32_t *state)
{
    uint32_t first;
    uint32_t second;
    if (state == NULL || media_link == NULL) return false;
    first = media_link->read32(media_link->context, MT_REG_MEDIA_STATE);
    second = media_link->read32(media_link->context, MT_REG_MEDIA_STATE);
    if (first != second) return false;
    *state = second;
    return true;
}

static bool request_matches(uint32_t request_id, uint32_t generation)
{
    uint32_t request_word;
    if (media_link == NULL) return false;
    request_word = media_link->read32(media_link->context,
                                      MT_REG_MEDIA_REQUEST);
    return (request_word & MT_MEDIA_REQUEST_VALID) != 0U &&
           media_link->read32(media_link->context,
                              MT_REG_MEDIA_REQUEST_ID) == request_id &&
           media_link->read32(media_link->context,
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
        /* A timed-out completion may already have crossed the mailbox. */
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
    if (status != MT_RESPONSE_OK) service_faulted = true;
    (void)submit_pending_completion();
}

static int submit_eject(void)
{
    const mt_link_result_t result = media_command(
        MT_COMMAND_MEDIA_EJECT, MT_C64_MEDIA_SLOT_DRIVE8,
        pending_eject_generation, 0U, 0U);
    if (result != MT_LINK_OK) return -1;
    eject_command_sent = true;
    eject_settle_polls = 0U;
    return 0;
}

static mt_c64_receive_result_t receive_write_data(uint8_t *destination,
                                                   size_t expected_size)
{
    size_t offset;
    uint32_t idle_polls = 0U;

    for (offset = 0U; offset < expected_size;) {
        const uint32_t word = media_link->read32(media_link->context,
                                                 MT_REG_MEDIA_DATA);
        bool expected_last;
        bool actual_last;
        if ((word & MT_MEDIA_DATA_VALID) == 0U) {
            if (++idle_polls >= MT_C64_MEDIA_DATA_IDLE_POLLS) {
                return MT_C64_RECEIVE_TIMEOUT;
            }
            service_progress();
            continue;
        }
        idle_polls = 0U;
        expected_last = offset + 1U == expected_size;
        actual_last = (word & MT_MEDIA_DATA_LAST) != 0U;
        destination[offset] = (uint8_t)(
            (word & MT_MEDIA_DATA_BYTE_MASK) >> MT_MEDIA_DATA_BYTE_SHIFT);
        ++offset;
        if (actual_last != expected_last) return MT_C64_RECEIVE_BAD_LAST;
    }

    /* MEDIA_DATA reads are side-effecting.  Use STATUS to wait for the final
     * pop to cross back without accidentally consuming a second byte. */
    for (idle_polls = 0U; idle_polls < MT_C64_MEDIA_DATA_IDLE_POLLS;
         ++idle_polls) {
        if ((media_link->read32(media_link->context, MT_REG_STATUS) &
             MT_STATUS_MEDIA_DATA) == 0U) {
            return MT_C64_RECEIVE_OK;
        }
        service_progress();
    }
    return MT_C64_RECEIVE_TIMEOUT;
}

static void fail_request(uint32_t request_id, uint32_t generation,
                         uint32_t status, const char *reason)
{
    xil_printf("[C64] drive8 request failed: %s\r\n", reason);
    queue_completion(request_id, status, 0U, generation);
}

static void service_d64_read(uint32_t request_id, uint32_t lba,
                             uint32_t blocks, uint32_t generation)
{
    const uint8_t *track_data = NULL;
    size_t track_size = 0U;
    mt_c64_d64_result_t resolve_result = mt_c64_d64_resolve_request(
        media_image, media_image_size, lba, blocks, &track_data, &track_size);
    mt_link_result_t link_result;

    if (resolve_result != MT_C64_D64_OK) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     mt_c64_d64_result_name(resolve_result));
        return;
    }
    link_result = mt_core_link_stream(
        media_link, MT_LOAD_C64_D64, track_data, track_size,
        UINT32_C(100000), media_progress, media_progress_context);
    if (link_result != MT_LINK_OK) {
        fail_request(request_id, generation,
                     link_result == MT_LINK_TIMEOUT ? MT_RESPONSE_TIMEOUT :
                                                      MT_RESPONSE_IO_ERROR,
                     mt_link_result_name(link_result));
        return;
    }
    queue_completion(request_id, MT_RESPONSE_OK, blocks, generation);
    xil_printf("[C64] drive8 D64 read lba=%u blocks=%u\r\n",
               (unsigned int)lba, (unsigned int)blocks);
}

static void service_g64_read(uint32_t request_id, uint32_t lba,
                             uint32_t blocks, uint32_t generation)
{
    mt_c64_g64_result_t g64_result;
    mt_link_result_t link_result;
    size_t meaningful_size = 0U;

    if (blocks != 32U || (lba % 32U) != 0U || lba / 32U >= 84U) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     "invalid G64 half-track request");
        return;
    }
    g64_result = mt_c64_g64_read_track(
        &g64_image, (uint8_t)(lba / 32U), transfer_buffer,
        sizeof(transfer_buffer), &meaningful_size);
    if (g64_result != MT_C64_G64_OK) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     mt_c64_g64_result_name(g64_result));
        return;
    }
    link_result = mt_core_link_stream(
        media_link, MT_LOAD_C64_G64, transfer_buffer,
        sizeof(transfer_buffer), UINT32_C(100000), media_progress,
        media_progress_context);
    if (link_result != MT_LINK_OK) {
        fail_request(request_id, generation,
                     link_result == MT_LINK_TIMEOUT ? MT_RESPONSE_TIMEOUT :
                                                      MT_RESPONSE_IO_ERROR,
                     mt_link_result_name(link_result));
        return;
    }
    queue_completion(request_id, MT_RESPONSE_OK, blocks, generation);
    xil_printf("[C64] drive8 G64 half-track=%u bytes=%u\r\n",
               (unsigned int)(lba / 32U), (unsigned int)meaningful_size);
}

static void service_d64_write(uint32_t request_id, uint32_t lba,
                              uint32_t blocks, uint32_t generation)
{
    const uint8_t *old_data = NULL;
    mt_c64_receive_result_t receive_result;
    mt_c64_d64_result_t write_result;
    mt_sd_result_t persist_result;
    size_t byte_count;
    size_t old_size = 0U;

    if (mounted_read_only || blocks >
        sizeof(transfer_buffer) / MT_C64_D64_SECTOR_SIZE) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     "write is not valid for this image");
        return;
    }
    byte_count = (size_t)blocks * MT_C64_D64_SECTOR_SIZE;
    /* Check every range before consuming a side-effecting MEDIA_DATA byte. */
    write_result = mt_c64_d64_resolve_request(
        media_image, media_image_size, lba, blocks, &old_data, &old_size);
    if (write_result != MT_C64_D64_OK || old_size != byte_count) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     mt_c64_d64_result_name(write_result));
        return;
    }

    receive_result = receive_write_data(transfer_buffer, byte_count);
    if (receive_result != MT_C64_RECEIVE_OK) {
        fail_request(request_id, generation,
                     receive_result == MT_C64_RECEIVE_TIMEOUT ?
                         MT_RESPONSE_TIMEOUT : MT_RESPONSE_INVALID_ARGUMENT,
                     receive_result == MT_C64_RECEIVE_TIMEOUT ?
                         "MEDIA_DATA timeout" : "MEDIA_DATA LAST mismatch");
        return;
    }
    write_result = mt_c64_d64_write_blocks(
        media_image, media_image_size, lba, blocks,
        transfer_buffer, byte_count);
    if (write_result != MT_C64_D64_OK) {
        fail_request(request_id, generation, MT_RESPONSE_INVALID_ARGUMENT,
                     mt_c64_d64_result_name(write_result));
        return;
    }

    image_dirty = true;
    persist_result = mt_sd_storage_journal_replace(
        mounted_path, mounted_journal_path, media_image, media_image_size);
    if (persist_result.status != MT_SD_OK) {
        xil_printf("[C64] drive8 journal commit failed: %s (%u)\r\n",
                   mt_sd_status_name(persist_result.status),
                   persist_result.backend_status);
        fail_request(request_id, generation, MT_RESPONSE_IO_ERROR,
                     "D64 persistence failed; dirty disk retained");
        return;
    }

    image_dirty = false;
    queue_completion(request_id, MT_RESPONSE_OK, blocks, generation);
    xil_printf("[C64] drive8 D64 write committed lba=%u blocks=%u\r\n",
               (unsigned int)lba, (unsigned int)blocks);
}

void c64_d64_service_init(mt_core_link_t *link,
                          mt_link_progress_fn progress,
                          void *progress_context)
{
    media_link = link;
    media_progress = progress;
    media_progress_context = progress_context;
    media_image_size = 0U;
    mount_generation = 0U;
    next_generation = 1U;
    serviced_request_id = 0U;
    serviced_request_generation = 0U;
    pending_eject_generation = 0U;
    eject_settle_polls = 0U;
    completion_request_id = 0U;
    completion_status = 0U;
    completion_block_count = 0U;
    completion_generation = 0U;
    mounted_type = MT_C64_MEDIA_NONE;
    request_seen = false;
    mounted = false;
    mounted_read_only = true;
    image_dirty = false;
    service_faulted = false;
    completion_pending = false;
    eject_pending = false;
    eject_command_sent = false;
    reset_resume_pending = false;
    mounted_name[0] = '\0';
    mounted_path[0] = '\0';
    mounted_journal_path[0] = '\0';
    reset_resume_name[0] = '\0';
    memset(&g64_image, 0, sizeof(g64_image));
}

void c64_d64_service_notify_core_reset(void)
{
    serviced_request_id = 0U;
    serviced_request_generation = 0U;
    request_seen = false;
    completion_pending = false;
}

int c64_d64_service_retry(void)
{
    uint32_t poll;
    uint32_t request_word = MT_MEDIA_REQUEST_VALID;

    if (media_link == NULL) return -1;
    /* Never disturb a healthy request. The shared load-error clear also
     * resets the core-side media request state. */
    /* A failed post-reset remount has no active FPGA media fault to clear.
     * Accept the retry so the caller can run resume_after_reset() without
     * disturbing any healthy media request. */
    if (!service_faulted) return reset_resume_pending ? 0 : -1;
    if (completion_pending && submit_pending_completion() != 0) return -1;
    /* A completion response and its MMIO request clear are registered in
     * adjacent control-clock cycles. Do not let the independent clear-error
     * CDC pulse overtake that ordered completion. */
    for (poll = 0U; poll < MT_C64_MEDIA_DRAIN_POLLS; ++poll) {
        request_word = media_link->read32(media_link->context,
                                          MT_REG_MEDIA_REQUEST);
        if ((request_word & MT_MEDIA_REQUEST_VALID) == 0U) break;
        service_progress();
    }
    if ((request_word & MT_MEDIA_REQUEST_VALID) != 0U) return -1;
    media_link->write32(media_link->context, MT_REG_ERROR,
                        MT_ERROR_CORE_LOAD);
    service_faulted = false;
    request_seen = false;
    return 0;
}

void c64_d64_service_poll(void)
{
    uint32_t request_word;
    uint32_t request_id;
    uint32_t request_lba;
    uint32_t request_blocks_word;
    uint32_t request_generation;
    uint32_t media_state;
    uint32_t blocks;

    if (media_link == NULL) return;
    if (completion_pending) {
        (void)submit_pending_completion();
        return;
    }
    if (eject_pending && !eject_command_sent) {
        if (submit_eject() != 0) return;
    }

    request_word = media_link->read32(media_link->context,
                                      MT_REG_MEDIA_REQUEST);
    if ((request_word & MT_MEDIA_REQUEST_VALID) == 0U) {
        request_seen = false;
        if (eject_pending && eject_command_sent) {
            if (eject_settle_polls < MT_C64_MEDIA_EJECT_SETTLE_POLLS) {
                ++eject_settle_polls;
            } else if (read_stable_media_state(&media_state) &&
                       (media_state & (MT_MEDIA_STATE_MOUNTED |
                                       MT_MEDIA_STATE_DIRTY |
                                       MT_MEDIA_STATE_EJECT_PENDING)) == 0U) {
                forget_mount();
            }
        }
        return;
    }

    request_id = media_link->read32(media_link->context,
                                    MT_REG_MEDIA_REQUEST_ID);
    request_generation = media_link->read32(
        media_link->context, MT_REG_MEDIA_REQUEST_GENERATION);
    if (request_seen && request_id == serviced_request_id &&
        request_generation == serviced_request_generation) {
        return;
    }
    if (service_faulted) return;

    request_seen = true;
    serviced_request_id = request_id;
    serviced_request_generation = request_generation;
    request_lba = media_link->read32(media_link->context,
                                     MT_REG_MEDIA_REQUEST_LBA);
    request_blocks_word = media_link->read32(
        media_link->context, MT_REG_MEDIA_REQUEST_BLOCK_COUNT);
    blocks = request_blocks_word & MT_MEDIA_BLOCK_COUNT_MASK;

    if (!mounted || request_generation != mount_generation ||
        (request_blocks_word & ~MT_MEDIA_BLOCK_COUNT_MASK) != 0U ||
        blocks == 0U) {
        fail_request(request_id, request_generation,
                     MT_RESPONSE_INVALID_ARGUMENT,
                     "invalid or stale media request");
        return;
    }
    if ((request_word & MT_MEDIA_REQUEST_WRITE) != 0U) {
        if (mounted_type != MT_C64_MEDIA_D64) {
            fail_request(request_id, request_generation,
                         MT_RESPONSE_INVALID_ARGUMENT,
                         "write requested for read-only media type");
            return;
        }
        service_d64_write(request_id, request_lba, blocks,
                          request_generation);
    } else if (mounted_type == MT_C64_MEDIA_D64) {
        service_d64_read(request_id, request_lba, blocks,
                         request_generation);
    } else if (mounted_type == MT_C64_MEDIA_G64) {
        service_g64_read(request_id, request_lba, blocks,
                         request_generation);
    } else {
        fail_request(request_id, request_generation,
                     MT_RESPONSE_INVALID_ARGUMENT, "no supported image");
    }
}

int c64_d64_service_eject(void)
{
    uint32_t poll;

    if (!mounted && !eject_pending) return 0;
    if (service_faulted && c64_d64_service_retry() != 0) return -1;
    if (!eject_pending) {
        pending_eject_generation = allocate_generation();
        eject_pending = true;
        eject_command_sent = false;
        eject_settle_polls = 0U;
    }

    for (poll = 0U; poll < MT_C64_MEDIA_DRAIN_POLLS; ++poll) {
        c64_d64_service_poll();
        if (!eject_pending) return 0;
        if (service_faulted && !completion_pending) {
            xil_printf("[C64] drive8 eject blocked; dirty disk retained\r\n");
            return -1;
        }
        service_progress();
    }
    xil_printf("[C64] drive8 eject timed out; retry pending\r\n");
    return -1;
}

static int mount_image(const char *file_name)
{
    char path[MT_C64_MEDIA_PATH_CAPACITY];
    char candidate_journal[MT_C64_MEDIA_JOURNAL_PATH_CAPACITY];
    mt_content_type_t content_type;
    mt_sd_result_t storage_result;
    mt_link_result_t command_result;
    mt_content_info_t info;
    mt_content_validation_t validation;
    mt_c64_g64_image_t candidate_g64;
    mt_c64_g64_result_t g64_result;
    mt_c64_media_type_t candidate_type;
    bool candidate_read_only;
    uint32_t generation;
    uint32_t mount_flags;
    size_t name_length;

    if (media_path(file_name, path, sizeof(path), &content_type) != 0) {
        xil_printf("[C64] drive8 mount requires a .d64 or .g64 file name "
                   "under %s\r\n", MT_C64_MEDIA_DIRECTORY);
        return -1;
    }
    if ((mounted || eject_pending) && c64_d64_service_eject() != 0) {
        return -1;
    }

    candidate_journal[0] = '\0';
    if (content_type == MT_CONTENT_D64) {
        if (make_journal_path(path, candidate_journal,
                              sizeof(candidate_journal)) != 0) {
            return -1;
        }
        storage_result = mt_sd_storage_journal_recover(
            path, candidate_journal, media_image, MT_C64_D64_MAX_SIZE);
        if (storage_result.status != MT_SD_OK) {
            xil_printf("[C64] drive8 journal recovery %s failed: %s (%u)\r\n",
                       path, mt_sd_status_name(storage_result.status),
                       storage_result.backend_status);
            return -1;
        }
        if (storage_result.size != 0U) {
            xil_printf("[C64] drive8 recovered %s bytes=%u\r\n", path,
                       (unsigned int)storage_result.size);
        }
    }

    storage_result = mt_sd_storage_read_file_max(
        path, media_image,
        content_type == MT_CONTENT_D64 ? MT_C64_D64_MAX_SIZE :
                                         sizeof(media_image));
    if (storage_result.status != MT_SD_OK) {
        xil_printf("[C64] drive8 read %s failed: %s (%u)\r\n", path,
                   mt_sd_status_name(storage_result.status),
                   storage_result.backend_status);
        return -1;
    }

    memset(&candidate_g64, 0, sizeof(candidate_g64));
    if (content_type == MT_CONTENT_D64) {
        validation = mt_content_inspect(MT_CONTENT_D64, media_image,
                                        storage_result.size, &info);
        if (validation != MT_CONTENT_VALID) {
            xil_printf("[C64] drive8 rejected %s: %s\r\n", path,
                       mt_content_validation_name(validation));
            return -1;
        }
        candidate_type = MT_C64_MEDIA_D64;
        candidate_read_only = d64_has_error_tail(storage_result.size);
    } else {
        g64_result = mt_c64_g64_open(media_image, storage_result.size,
                                     &candidate_g64);
        if (g64_result != MT_C64_G64_OK) {
            xil_printf("[C64] drive8 rejected %s: %s\r\n", path,
                       mt_c64_g64_result_name(g64_result));
            return -1;
        }
        candidate_type = MT_C64_MEDIA_G64;
        candidate_read_only = true;
    }

    generation = allocate_generation();
    mount_flags = MT_C64_MEDIA_SLOT_DRIVE8 |
                  ((uint32_t)(candidate_type == MT_C64_MEDIA_D64 ?
                      MT_LOAD_C64_D64 : MT_LOAD_C64_G64) <<
                   MT_MEDIA_KIND_SHIFT) |
                  (candidate_read_only ? MT_MEDIA_MOUNT_READ_ONLY : 0U);
    command_result = media_command(MT_COMMAND_MEDIA_MOUNT, mount_flags,
                                   (uint32_t)storage_result.size,
                                   generation, 0U);
    if (command_result != MT_LINK_OK) {
        xil_printf("[C64] drive8 mount command failed: %s\r\n",
                   mt_link_result_name(command_result));
        return -1;
    }

    media_image_size = storage_result.size;
    mount_generation = generation;
    mounted_type = candidate_type;
    mounted = true;
    mounted_read_only = candidate_read_only;
    image_dirty = false;
    service_faulted = false;
    completion_pending = false;
    request_seen = false;
    eject_pending = false;
    eject_command_sent = false;
    g64_image = candidate_g64;
    name_length = strlen(file_name);
    memcpy(mounted_name, file_name, name_length + 1U);
    memcpy(mounted_path, path, strlen(path) + 1U);
    memcpy(mounted_journal_path, candidate_journal,
           strlen(candidate_journal) + 1U);
    xil_printf("[C64] drive8 mounted %s type=%s bytes=%u %s\r\n", path,
               media_type_name(mounted_type),
               (unsigned int)media_image_size,
               mounted_read_only ? "read-only" : "writable");
    return 0;
}

int c64_d64_service_mount(const char *file_name)
{
    reset_resume_pending = false;
    reset_resume_name[0] = '\0';
    return mount_image(file_name);
}

int c64_d64_service_mount_default(void)
{
    return c64_d64_service_mount(MT_C64_DEFAULT_D64_NAME);
}

int c64_d64_service_prepare_reset(void)
{
    size_t name_length;
    if (!mounted && !eject_pending) return 0;
    if (mounted_name[0] == '\0') return -1;
    name_length = strlen(mounted_name);
    memcpy(reset_resume_name, mounted_name, name_length + 1U);
    if (c64_d64_service_eject() != 0) return -1;
    reset_resume_pending = true;
    return 0;
}

int c64_d64_service_resume_after_reset(void)
{
    char name[MT_C64_MEDIA_PATH_CAPACITY];
    if (!reset_resume_pending) return 0;
    memcpy(name, reset_resume_name, strlen(reset_resume_name) + 1U);
    reset_resume_pending = false;
    if (mount_image(name) != 0) {
        reset_resume_pending = true;
        return -1;
    }
    reset_resume_name[0] = '\0';
    return 0;
}

static int print_media_entry(void *context, const char *name, size_t size,
                             bool is_directory)
{
    const mt_content_type_t type = mt_content_classify(name);
    (void)context;
    if (!is_directory &&
        (type == MT_CONTENT_D64 || type == MT_CONTENT_G64 ||
         type == MT_CONTENT_PRG || type == MT_CONTENT_CRT)) {
        xil_printf("  %s (%u bytes)\r\n", name, (unsigned int)size);
    }
    return 0;
}

void c64_d64_service_list(void)
{
    mt_sd_result_t result;
    xil_printf("[C64] D64/G64/PRG/CRT files in %s:\r\n",
               MT_C64_MEDIA_DIRECTORY);
    result = mt_sd_storage_list(
        MT_C64_MEDIA_DIRECTORY, print_media_entry, NULL);
    if (result.status != MT_SD_OK) {
        xil_printf("  list failed: %s (%u)\r\n",
                   mt_sd_status_name(result.status), result.backend_status);
    }
}

void c64_d64_service_dump_status(void)
{
    uint32_t media_state = 0U;
    if (media_link != NULL) {
        media_state = media_link->read32(media_link->context,
                                         MT_REG_MEDIA_STATE);
    }
    if (mounted) {
        xil_printf("[C64] drive8=%s type=%s bytes=%u generation=%u %s "
                   "hw=%s%s%s ram-dirty=%s fault=%s\r\n",
                   mounted_name, media_type_name(mounted_type),
                   (unsigned int)media_image_size,
                   (unsigned int)mount_generation,
                   mounted_read_only ? "read-only" : "writable",
                   (media_state & MT_MEDIA_STATE_MOUNTED) != 0U ?
                       "mounted" : "unmounted",
                   (media_state & MT_MEDIA_STATE_DIRTY) != 0U ?
                       ",dirty" : "",
                   (media_state & MT_MEDIA_STATE_EJECT_PENDING) != 0U ?
                       ",eject-pending" : "",
                   image_dirty ? "yes" : "no",
                   service_faulted ? "yes" : "no");
    } else {
        xil_printf("[C64] drive8 empty%s\r\n",
                   reset_resume_pending ? "; reset remount pending" : "");
    }
}

bool c64_d64_service_mounted(void)
{
    return mounted;
}

const char *c64_d64_service_mounted_name(void)
{
    return mounted ? mounted_name : NULL;
}
