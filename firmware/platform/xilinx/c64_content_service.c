/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_content_service.h"

#include "multitini_c64_interface.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MT_C64_CONTENT_PATH_CAPACITY 256U
#define MT_C64_CONTENT_POLLS UINT32_C(100000)

static mt_core_link_t *content_link;
static mt_link_progress_fn content_progress;
static void *content_progress_context;
static uint8_t content_buffer[MT_SIZE_C64_CRT_MAX];
static bool crt_attached;
static char crt_name[MT_C64_CONTENT_PATH_CAPACITY];

static void clear_core_load_error(void)
{
    content_link->write32(content_link->context, MT_REG_ERROR,
                          MT_ERROR_CORE_LOAD);
}

static mt_link_result_t wait_for_core_result(void)
{
    uint32_t poll;
    for (poll = 0U; poll < MT_C64_CONTENT_POLLS; ++poll) {
        uint32_t status;
        if (content_progress != NULL)
            content_progress(content_progress_context);
        status = content_link->read32(content_link->context, MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U)
            return MT_LINK_REMOTE_ERROR;
        if ((status & MT_STATUS_CORE_READY) != 0U)
            return MT_LINK_OK;
    }
    return MT_LINK_TIMEOUT;
}

static mt_c64_content_service_result_t service_result(
    mt_c64_content_service_status_t status)
{
    mt_c64_content_service_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.sd.status = MT_SD_OK;
    result.content.status = MT_C64_CONTENT_OK;
    result.content.validation = MT_CONTENT_VALID;
    result.link = MT_LINK_OK;
    return result;
}

static int content_path(const char *file_name, char *path, size_t capacity,
                        mt_content_type_t *type)
{
    const size_t directory_length = strlen(MT_C64_SD_MEDIA_ROOT);
    size_t name_length;
    size_t index;
    mt_content_type_t classified;

    if (file_name == NULL || path == NULL || type == NULL ||
        file_name[0] == '\0' || strcmp(file_name, ".") == 0 ||
        strcmp(file_name, "..") == 0) {
        return -1;
    }
    name_length = strlen(file_name);
    if (directory_length + 1U + name_length + 1U > capacity) return -1;
    for (index = 0U; index < name_length; ++index) {
        if (file_name[index] == '/' || file_name[index] == '\\' ||
            file_name[index] == ':') {
            return -1;
        }
    }
    classified = mt_content_classify(file_name);
    if (classified != MT_CONTENT_PRG && classified != MT_CONTENT_CRT) {
        return -1;
    }
    memcpy(path, MT_C64_SD_MEDIA_ROOT, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1U, file_name, name_length + 1U);
    *type = classified;
    return 0;
}

void c64_content_service_init(mt_core_link_t *link,
                              mt_link_progress_fn progress,
                              void *progress_context)
{
    content_link = link;
    content_progress = progress;
    content_progress_context = progress_context;
    crt_attached = false;
    crt_name[0] = '\0';
}

mt_c64_content_service_result_t c64_content_service_detach_crt(void)
{
    mt_c64_content_service_result_t result =
        service_result(MT_C64_CONTENT_SERVICE_OK);
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }
    clear_core_load_error();
    result.link = mt_c64_detach_crt(
        content_link, MT_C64_CONTENT_POLLS, content_progress,
        content_progress_context);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    result.link = wait_for_core_result();
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    crt_attached = false;
    crt_name[0] = '\0';
    return result;
}

mt_c64_content_service_result_t c64_content_service_load(
    const char *file_name)
{
    mt_c64_content_service_result_t result =
        service_result(MT_C64_CONTENT_SERVICE_OK);
    mt_c64_content_image_t image;
    mt_content_type_t type;
    char path[MT_C64_CONTENT_PATH_CAPACITY];

    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL ||
        content_path(file_name, path, sizeof(path), &type) != 0) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }
    result.sd = mt_sd_storage_read_file_max(
        path, content_buffer,
        type == MT_CONTENT_PRG ? MT_SIZE_C64_PRG_MAX :
                                 MT_SIZE_C64_CRT_MAX);
    if (result.sd.status != MT_SD_OK) {
        result.status = MT_C64_CONTENT_SERVICE_READ_FAILED;
        return result;
    }
    image.data = content_buffer;
    image.size = result.sd.size;
    result.content = type == MT_CONTENT_PRG ?
        mt_c64_validate_prg(&image) : mt_c64_validate_crt(&image);
    if (result.content.status != MT_C64_CONTENT_OK) {
        result.status = MT_C64_CONTENT_SERVICE_REJECTED;
        return result;
    }

    if (type == MT_CONTENT_PRG && crt_attached) {
        result = c64_content_service_detach_crt();
        if (result.status != MT_C64_CONTENT_SERVICE_OK) return result;
    }
    clear_core_load_error();
    result.link = type == MT_CONTENT_PRG ?
        mt_c64_load_prg(content_link, &image, MT_C64_CONTENT_POLLS,
                        content_progress, content_progress_context) :
        mt_c64_load_crt(content_link, &image, MT_C64_CONTENT_POLLS,
                        content_progress, content_progress_context);
    if (result.link != MT_LINK_OK) {
        // The HDL detaches the active cartridge as soon as the first CRT byte
        // is accepted. A failed replacement therefore cannot leave the
        // previous image reported as attached.
        if (type == MT_CONTENT_CRT) {
            crt_attached = false;
            crt_name[0] = '\0';
        }
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    result.link = wait_for_core_result();
    if (result.link != MT_LINK_OK) {
        if (type == MT_CONTENT_CRT) {
            crt_attached = false;
            crt_name[0] = '\0';
        }
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    crt_attached = type == MT_CONTENT_CRT;
    if (crt_attached)
        memcpy(crt_name, file_name, strlen(file_name) + 1U);
    else
        crt_name[0] = '\0';
    return result;
}

bool c64_content_service_crt_attached(void)
{
    return crt_attached;
}

const char *c64_content_service_crt_name(void)
{
    return crt_attached ? crt_name : NULL;
}

const char *c64_content_service_status_name(
    mt_c64_content_service_status_t status)
{
    switch (status) {
    case MT_C64_CONTENT_SERVICE_OK: return "ok";
    case MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_CONTENT_SERVICE_READ_FAILED: return "read failed";
    case MT_C64_CONTENT_SERVICE_REJECTED: return "content rejected";
    case MT_C64_CONTENT_SERVICE_TRANSFER_FAILED: return "transfer failed";
    }
    return "unknown C64 content service status";
}
