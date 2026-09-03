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

static mt_core_link_t *content_link;
static mt_link_progress_fn content_progress;
static void *content_progress_context;
static uint8_t content_buffer[MT_SIZE_GB_CART_MAX];
static char content_directory[MT_CORE_CONTENT_ROOT_MAX];
static bool cartridge_attached;
static char cartridge_name[MT_GAMEBOY_CONTENT_PATH_CAPACITY];

static mt_gameboy_content_service_result_t service_result(
    mt_gameboy_content_service_status_t status)
{
    mt_gameboy_content_service_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.sd.status = MT_SD_OK;
    result.validation = MT_CONTENT_VALID;
    result.link = MT_LINK_OK;
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
        content_directory[0] == '\0' ||
        file_name[0] == '\0' || strcmp(file_name, ".") == 0 ||
        strcmp(file_name, "..") == 0) {
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
            file_name[index] == ':') {
            return -1;
        }
    }
    memcpy(path, content_directory, root_length);
    path[root_length] = '/';
    memcpy(path + root_length + 1U, file_name, name_length + 1U);
    *type = classified;
    return 0;
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

static mt_link_result_t set_core_reset(bool asserted)
{
    const uint32_t args[4] = {asserted ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(
        content_link, MT_COMMAND_CORE_RESET, args, MT_GAMEBOY_CONTENT_POLLS,
        content_progress, content_progress_context, &response);
}

void gameboy_content_service_init(mt_core_link_t *link,
                                  const char *content_root,
                                  mt_link_progress_fn progress,
                                  void *progress_context)
{
    content_link = link;
    content_progress = progress;
    content_progress_context = progress_context;
    content_directory[0] = '\0';
    if (valid_root(content_root))
        memcpy(content_directory, content_root, strlen(content_root) + 1U);
    cartridge_attached = false;
    cartridge_name[0] = '\0';
}

mt_gameboy_content_service_result_t gameboy_content_service_load(
    const char *file_name)
{
    mt_gameboy_content_service_result_t result =
        service_result(MT_GAMEBOY_CONTENT_SERVICE_OK);
    mt_gameboy_boot_images_t images;
    mt_content_info_t info;
    mt_content_type_t type;
    char path[MT_GAMEBOY_CONTENT_PATH_CAPACITY];

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

    images.cartridge.data = content_buffer;
    images.cartridge.size = result.sd.size;
    images.bootstrap_override.data = NULL;
    images.bootstrap_override.size = 0U;
    clear_core_load_error();
    cartridge_attached = false;
    cartridge_name[0] = '\0';
    result.link = mt_gameboy_boot(
        content_link, &images, MT_GAMEBOY_CONTENT_POLLS,
        content_progress, content_progress_context);
    if (result.link == MT_LINK_OK)
        result.link = wait_for_core_result();
    if (result.link != MT_LINK_OK) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }

    cartridge_attached = true;
    memcpy(cartridge_name, file_name, strlen(file_name) + 1U);
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
    result.link = set_core_reset(true);
    if (result.link != MT_LINK_OK) {
        result.status = MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    cartridge_attached = false;
    cartridge_name[0] = '\0';
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
    }
    return "unknown Game Boy content service status";
}
