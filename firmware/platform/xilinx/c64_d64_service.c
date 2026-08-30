/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"

#include "multitini_c64_d64.h"
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
#define MT_C64_MEDIA_BUSY_RETRIES UINT32_C(16)

static mt_core_link_t *media_link;
static mt_link_progress_fn media_progress;
static void *media_progress_context;
static uint8_t d64_image[MT_C64_D64_MAX_SIZE];
static size_t d64_image_size;
static uint32_t mount_generation;
static uint32_t next_generation = 1U;
static uint32_t serviced_request_id;
static uint32_t serviced_request_generation;
static uint32_t pending_eject_generation;
static bool request_seen;
static bool eject_retry_pending;
static bool mounted;
static char mounted_name[MT_C64_MEDIA_PATH_CAPACITY];

static uint32_t allocate_generation(void)
{
    uint32_t result = next_generation++;
    if (result == 0U) {
        result = next_generation++;
    }
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

static int media_path(const char *file_name, char *path, size_t capacity)
{
    const size_t directory_length = strlen(MT_C64_MEDIA_DIRECTORY);
    size_t name_length;
    size_t index;

    if (file_name == NULL || path == NULL || *file_name == '\0') return -1;
    name_length = strlen(file_name);
    if (directory_length + 1U + name_length + 1U > capacity) return -1;
    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) return -1;
    for (index = 0U; index < name_length; ++index) {
        if (file_name[index] == '/' || file_name[index] == '\\' ||
            file_name[index] == ':') {
            return -1;
        }
    }
    if (mt_content_classify(file_name) != MT_CONTENT_D64) return -1;

    memcpy(path, MT_C64_MEDIA_DIRECTORY, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1U, file_name, name_length + 1U);
    return 0;
}

static int eject_with_generation(uint32_t generation, bool report_failure)
{
    const mt_link_result_t result = media_command(
        MT_COMMAND_MEDIA_EJECT, MT_C64_MEDIA_SLOT_DRIVE8, generation, 0U, 0U);
    if (result != MT_LINK_OK) {
        if (report_failure) {
            xil_printf("[C64] drive8 eject failed: %s; retry pending\r\n",
                       mt_link_result_name(result));
        }
        return -1;
    }
    mounted = false;
    d64_image_size = 0U;
    mounted_name[0] = '\0';
    mount_generation = generation;
    request_seen = false;
    return 0;
}

static int retry_pending_eject(bool report_failure)
{
    if (!eject_retry_pending) return 0;
    if (eject_with_generation(pending_eject_generation,
                              report_failure) != 0) {
        return -1;
    }
    eject_retry_pending = false;
    pending_eject_generation = 0U;
    return 0;
}

static int queue_eject(bool report_failure)
{
    if (!eject_retry_pending) {
        pending_eject_generation = allocate_generation();
        eject_retry_pending = true;
    }
    return retry_pending_eject(report_failure);
}

void c64_d64_service_init(mt_core_link_t *link,
                          mt_link_progress_fn progress,
                          void *progress_context)
{
    media_link = link;
    media_progress = progress;
    media_progress_context = progress_context;
    d64_image_size = 0U;
    mount_generation = 0U;
    serviced_request_id = 0U;
    serviced_request_generation = 0U;
    pending_eject_generation = 0U;
    request_seen = false;
    eject_retry_pending = false;
    mounted = false;
    mounted_name[0] = '\0';
}

void c64_d64_service_notify_core_reset(void)
{
    serviced_request_id = 0U;
    serviced_request_generation = 0U;
    request_seen = false;
}

int c64_d64_service_eject(void)
{
    return queue_eject(true);
}

int c64_d64_service_mount(const char *file_name)
{
    char path[MT_C64_MEDIA_PATH_CAPACITY];
    mt_sd_result_t read_result;
    mt_content_info_t info;
    mt_content_validation_t validation;
    mt_link_result_t command_result;
    uint32_t generation;
    uint32_t mount_flags;
    size_t name_length;

    if (media_path(file_name, path, sizeof(path)) != 0) {
        xil_printf("[C64] drive8 mount requires a .d64 file name under %s\r\n",
                   MT_C64_MEDIA_DIRECTORY);
        return -1;
    }
    if ((mounted || eject_retry_pending) &&
        c64_d64_service_eject() != 0) {
        return -1;
    }

    read_result = mt_sd_storage_read_file_max(
        path, d64_image, sizeof(d64_image));
    if (read_result.status != MT_SD_OK) {
        xil_printf("[C64] drive8 read %s failed: %s (%u)\r\n", path,
                   mt_sd_status_name(read_result.status),
                   read_result.backend_status);
        return -1;
    }
    validation = mt_content_inspect(MT_CONTENT_D64, d64_image,
                                    read_result.size, &info);
    if (validation != MT_CONTENT_VALID) {
        xil_printf("[C64] drive8 rejected %s: %s\r\n", path,
                   mt_content_validation_name(validation));
        return -1;
    }

    generation = allocate_generation();
    mount_flags = MT_C64_MEDIA_SLOT_DRIVE8 |
                  ((uint32_t)MT_LOAD_C64_D64 << MT_MEDIA_KIND_SHIFT) |
                  MT_MEDIA_MOUNT_READ_ONLY;
    command_result = media_command(MT_COMMAND_MEDIA_MOUNT, mount_flags,
                                   (uint32_t)read_result.size,
                                   generation, 0U);
    if (command_result != MT_LINK_OK) {
        xil_printf("[C64] drive8 mount command failed: %s\r\n",
                   mt_link_result_name(command_result));
        return -1;
    }

    d64_image_size = read_result.size;
    mount_generation = generation;
    mounted = true;
    request_seen = false;
    name_length = strlen(file_name);
    memcpy(mounted_name, file_name, name_length + 1U);
    xil_printf("[C64] drive8 mounted %s bytes=%u sectors=%u read-only\r\n",
               path, (unsigned int)d64_image_size,
               (unsigned int)mt_c64_d64_sector_count(d64_image_size));
    return 0;
}

int c64_d64_service_mount_default(void)
{
    return c64_d64_service_mount(MT_C64_DEFAULT_D64_NAME);
}

static void fail_request(const char *reason)
{
    xil_printf("[C64] drive8 request failed: %s; ejecting disk\r\n", reason);
    (void)queue_eject(true);
}

void c64_d64_service_poll(void)
{
    uint32_t request_word;
    uint32_t request_id;
    uint32_t request_lba;
    uint32_t request_blocks_word;
    uint32_t request_generation;
    const uint8_t *track_data;
    size_t track_size;
    mt_c64_d64_result_t resolve_result;
    mt_link_result_t link_result;

    if (media_link == NULL) return;
    if (eject_retry_pending) {
        (void)retry_pending_eject(false);
        return;
    }
    request_word = media_link->read32(media_link->context,
                                      MT_REG_MEDIA_REQUEST);
    if ((request_word & MT_MEDIA_REQUEST_VALID) == 0U) {
        request_seen = false;
        return;
    }

    request_id = media_link->read32(media_link->context,
                                    MT_REG_MEDIA_REQUEST_ID);
    request_lba = media_link->read32(media_link->context,
                                     MT_REG_MEDIA_REQUEST_LBA);
    request_blocks_word = media_link->read32(
        media_link->context, MT_REG_MEDIA_REQUEST_BLOCK_COUNT);
    request_generation = media_link->read32(
        media_link->context, MT_REG_MEDIA_REQUEST_GENERATION);
    if (request_seen && request_id == serviced_request_id &&
        request_generation == serviced_request_generation) {
        return;
    }
    request_seen = true;
    serviced_request_id = request_id;
    serviced_request_generation = request_generation;

    if (!mounted || (request_word & MT_MEDIA_REQUEST_WRITE) != 0U ||
        request_generation != mount_generation ||
        (request_blocks_word & ~MT_MEDIA_BLOCK_COUNT_MASK) != 0U ||
        (request_blocks_word & MT_MEDIA_BLOCK_COUNT_MASK) == 0U) {
        fail_request("invalid or stale request");
        return;
    }

    resolve_result = mt_c64_d64_resolve_request(
        d64_image, d64_image_size, request_lba,
        request_blocks_word & MT_MEDIA_BLOCK_COUNT_MASK,
        &track_data, &track_size);
    if (resolve_result != MT_C64_D64_OK) {
        fail_request(mt_c64_d64_result_name(resolve_result));
        return;
    }

    link_result = mt_core_link_stream(
        media_link, MT_LOAD_C64_D64, track_data, track_size,
        UINT32_C(100000), media_progress, media_progress_context);
    if (link_result != MT_LINK_OK) {
        fail_request(mt_link_result_name(link_result));
        return;
    }
    link_result = media_command(
        MT_COMMAND_MEDIA_COMPLETE, request_id, MT_RESPONSE_OK,
        request_blocks_word & MT_MEDIA_BLOCK_COUNT_MASK,
        request_generation);
    if (link_result != MT_LINK_OK) {
        fail_request(mt_link_result_name(link_result));
        return;
    }
    xil_printf("[C64] drive8 track data lba=%u blocks=%u\r\n",
               (unsigned int)request_lba,
               (unsigned int)(request_blocks_word &
                              MT_MEDIA_BLOCK_COUNT_MASK));
}

static int print_media_entry(void *context, const char *name, size_t size,
                             bool is_directory)
{
    (void)context;
    if (!is_directory && mt_content_classify(name) == MT_CONTENT_D64) {
        xil_printf("  %s (%u bytes)\r\n", name, (unsigned int)size);
    }
    return 0;
}

void c64_d64_service_list(void)
{
    mt_sd_result_t result;
    xil_printf("[C64] D64 files in %s:\r\n", MT_C64_MEDIA_DIRECTORY);
    result = mt_sd_storage_list(
        MT_C64_MEDIA_DIRECTORY, print_media_entry, NULL);
    if (result.status != MT_SD_OK) {
        xil_printf("  list failed: %s (%u)\r\n",
                   mt_sd_status_name(result.status), result.backend_status);
    }
}

void c64_d64_service_dump_status(void)
{
    if (mounted) {
        xil_printf("[C64] drive8=%s bytes=%u generation=%u read-only\r\n",
                   mounted_name, (unsigned int)d64_image_size,
                   (unsigned int)mount_generation);
    } else {
        xil_printf("[C64] drive8 empty\r\n");
    }
}
