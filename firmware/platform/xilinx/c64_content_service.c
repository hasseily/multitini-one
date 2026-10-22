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
#define MT_C64_PRG_STREAM_POLLS UINT32_MAX
#define MT_C64_PRG_BOOT_TIMEOUT_MS UINT32_C(5000)
#define MT_C64_PRG_RUN_TIMEOUT_MS UINT32_C(2000)

typedef struct {
    uint64_t started_ms;
    uint32_t duration_ms;
} mt_c64_content_deadline_t;

static mt_core_link_t *content_link;
static mt_link_progress_fn content_progress;
static void *content_progress_context;
static mt_c64_content_time_ms_fn content_time_ms;
static void *content_time_context;
static uint8_t content_buffer[MT_SIZE_C64_CRT_MAX];
static bool crt_attached;
static bool prg_staged;
static char crt_name[MT_C64_CONTENT_PATH_CAPACITY];

uint32_t c64_content_service_expansion_caps(void)
{
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->read32(content_link->context, MT_REG_CORE_ID) != MT_C64_CORE_ID) return 0U;
    const uint32_t abi = content_link->read32(content_link->context, MT_REG_ABI_VERSION);
    if ((abi >> 16U) != MT_ABI_MAJOR || (uint16_t)abi < MT_C64_EXPANSION_MIN_ABI_MINOR) return 0U;
    const uint32_t caps = content_link->read32(content_link->context, MT_REG_C64_EXPANSION_CAPS);
    return (caps & MT_C64_EXPANSION_CAPS_VALID) != 0U ? caps : 0U;
}

bool c64_content_service_reu_enabled(void)
{
    return (c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_REU_16MB) != 0U &&
        (content_link->read32(content_link->context, MT_REG_C64_EXPANSION_CONFIG) &
         MT_C64_EXPANSION_REU_ENABLE) != 0U;
}

static mt_link_result_t set_core_reset(bool asserted)
{
    const uint32_t args[4] = {asserted ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(content_link, MT_COMMAND_CORE_RESET, args,
                                MT_C64_CONTENT_POLLS, content_progress,
                                content_progress_context, &response);
}

static mt_link_result_t release_stale_core_reset(void)
{
    if ((content_link->read32(content_link->context, MT_REG_CONTROL) &
         MT_CONTROL_CORE_RESET_REQUEST) == 0U) {
        return MT_LINK_OK;
    }
    return set_core_reset(false);
}

static void pulse_core_reset(void)
{
    (void)set_core_reset(true);
    (void)set_core_reset(false);
}

static void abort_and_reset_failed_transfer(void)
{
    /* A failure after STREAM_END needs one explicit abort to reset the
     * core-side receiver. A stream call failure has already aborted itself and
     * must not come through this path a second time. */
    (void)mt_core_link_abort_stream(
        content_link, MT_C64_CONTENT_POLLS,
        content_progress, content_progress_context);
    pulse_core_reset();
}

static void start_deadline(mt_c64_content_deadline_t *deadline,
                           uint32_t duration_ms)
{
    deadline->started_ms = content_time_ms == NULL ? 0U :
                           content_time_ms(content_time_context);
    deadline->duration_ms = duration_ms;
}

static int before_deadline(void *context)
{
    const mt_c64_content_deadline_t *const deadline = context;
    if (content_time_ms == NULL) return 1;
    return content_time_ms(content_time_context) - deadline->started_ms <
           (uint64_t)deadline->duration_ms;
}

static void clear_core_load_error(void)
{
    content_link->write32(content_link->context, MT_REG_ERROR,
                          MT_ERROR_CORE_LOAD);
}

static mt_link_result_t wait_for_core_result(
    mt_c64_content_deadline_t *deadline)
{
    uint32_t poll;
    const uint32_t max_polls = deadline == NULL || content_time_ms == NULL ?
                               MT_C64_CONTENT_POLLS :
                               MT_C64_PRG_STREAM_POLLS;
    for (poll = 0U; poll < max_polls; ++poll) {
        uint32_t status;
        if (content_progress != NULL)
            content_progress(content_progress_context);
        status = content_link->read32(content_link->context, MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U)
            return MT_LINK_REMOTE_ERROR;
        if ((status & MT_STATUS_CORE_READY) != 0U)
            return MT_LINK_OK;
        if (deadline != NULL && !before_deadline(deadline))
            return MT_LINK_TIMEOUT;
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

static mt_c64_content_service_result_t load_image(
    mt_content_type_t type, const uint8_t *data, size_t size,
    const char *file_name, bool stage_prg)
{
    mt_c64_content_service_result_t result =
        service_result(MT_C64_CONTENT_SERVICE_OK);
    mt_c64_content_image_t image;
    mt_c64_content_deadline_t deadline;

    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL || data == NULL ||
        (type != MT_CONTENT_PRG && type != MT_CONTENT_CRT) ||
        (type == MT_CONTENT_CRT && file_name == NULL)) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }

    image.data = data;
    image.size = size;
    result.content = type == MT_CONTENT_PRG ?
        mt_c64_validate_prg(&image) : mt_c64_validate_crt(&image);
    if (result.content.status != MT_C64_CONTENT_OK) {
        result.status = MT_C64_CONTENT_SERVICE_REJECTED;
        return result;
    }

    result.link = release_stale_core_reset();
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }

    if (type == MT_CONTENT_PRG && crt_attached) {
        result = c64_content_service_detach_crt();
        if (result.status != MT_C64_CONTENT_SERVICE_OK) return result;
    }
    prg_staged = false;
    clear_core_load_error();
    if (type == MT_CONTENT_PRG) {
        start_deadline(&deadline, MT_C64_PRG_BOOT_TIMEOUT_MS);
        const uint32_t polls = content_time_ms == NULL ?
                               MT_C64_CONTENT_POLLS :
                               MT_C64_PRG_STREAM_POLLS;
        result.link = mt_core_link_stream_until(
            content_link,
            stage_prg ? MT_LOAD_C64_PRG_STAGE : MT_LOAD_C64_PRG,
            image.data, image.size, polls,
            content_progress, content_progress_context,
            before_deadline, &deadline);
    } else {
        result.link = mt_c64_load_crt(
            content_link, &image, MT_C64_CONTENT_POLLS,
            content_progress, content_progress_context);
    }
    if (result.link != MT_LINK_OK) {
        if (type == MT_CONTENT_PRG) {
            /* mt_core_link_stream_until() already issued the single abort. */
            pulse_core_reset();
        } else {
            (void)set_core_reset(false);
        }
        /* The HDL detaches the active cartridge as soon as the first CRT byte
         * is accepted. A failed replacement therefore cannot leave the
         * previous image reported as attached. */
        if (type == MT_CONTENT_CRT) {
            crt_attached = false;
            crt_name[0] = '\0';
        }
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    if (type == MT_CONTENT_PRG)
        start_deadline(&deadline, MT_C64_PRG_RUN_TIMEOUT_MS);
    result.link = wait_for_core_result(
        type == MT_CONTENT_PRG ? &deadline : NULL);
    if (result.link != MT_LINK_OK) {
        abort_and_reset_failed_transfer();
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
    prg_staged = type == MT_CONTENT_PRG && stage_prg;
    return result;
}

void c64_content_service_init(mt_core_link_t *link,
                              mt_link_progress_fn progress,
                              void *progress_context,
                              mt_c64_content_time_ms_fn time_ms,
                              void *time_context)
{
    content_link = link;
    content_progress = progress;
    content_progress_context = progress_context;
    content_time_ms = time_ms;
    content_time_context = time_context;
    crt_attached = false;
    prg_staged = false;
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
    result.link = release_stale_core_reset();
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
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
    result.link = wait_for_core_result(NULL);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    crt_attached = false;
    prg_staged = false;
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
    if (type == MT_CONTENT_CRT && c64_content_service_reu_enabled()) {
        result.status = MT_C64_CONTENT_SERVICE_REU_CONFLICT;
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
    {
        const mt_sd_result_t storage_result = result.sd;
        result = load_image(type, image.data, image.size, file_name, false);
        result.sd = storage_result;
    }
    return result;
}

mt_c64_content_service_result_t c64_content_service_stage_prg_image(
    const uint8_t *data, size_t size)
{
    return load_image(MT_CONTENT_PRG, data, size, NULL, true);
}

mt_c64_content_service_result_t c64_content_service_prepare_disk_load(void)
{
    static const uint8_t empty_basic[] = {0x01U, 0x08U, 0U, 0U};
    const mt_c64_content_service_result_t result =
        c64_content_service_stage_prg_image(empty_basic, sizeof(empty_basic));
    prg_staged = false;
    return result;
}

mt_c64_content_service_result_t c64_content_service_run_staged_prg(void)
{
    mt_c64_content_service_result_t result =
        service_result(MT_C64_CONTENT_SERVICE_OK);
    mt_c64_content_deadline_t deadline;
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->write32 == NULL || !prg_staged) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        result.link = MT_LINK_BAD_ARGUMENT;
        return result;
    }
    /* Drive 8 is already remounted. The shared core-error acknowledgement
     * also clears the D64 receiver and can cancel a healthy track request.
     * Leave existing faults and the staged program intact for recovery;
     * starting RUN must not acknowledge an error or reset mounted media. */
    if ((content_link->read32(content_link->context, MT_REG_STATUS) &
         MT_STATUS_CORE_LOAD_ERROR) != 0U) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        result.link = MT_LINK_REMOTE_ERROR;
        return result;
    }
    result.link = release_stale_core_reset();
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    start_deadline(&deadline, MT_C64_PRG_RUN_TIMEOUT_MS);
    {
        static const uint8_t run_token = 0U;
        const uint32_t polls = content_time_ms == NULL ?
                               MT_C64_CONTENT_POLLS :
                               MT_C64_PRG_STREAM_POLLS;
        result.link = mt_core_link_stream_until(
            content_link, MT_LOAD_C64_PRG_RUN, &run_token, 1U,
            polls, content_progress,
            content_progress_context, before_deadline, &deadline);
    }
    if (result.link != MT_LINK_OK) {
        /* STREAM_ABORT leaves a committed staged PRG available for retry and
         * cancels any partially accepted RUN token. Do not reset here: Drive
         * 8 has already been remounted by the Mount & Run transaction. */
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    /* Once the token was accepted, retrying could type RUN twice even if the
     * ready transition is later lost or times out. */
    prg_staged = false;
    result.link = wait_for_core_result(&deadline);
    if (result.link != MT_LINK_OK) {
        /* The token was accepted, so the injector may complete after a lost
         * ready transition. Never retry it and never reset a mounted drive. */
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
    }
    return result;
}

bool c64_content_service_prg_staged(void)
{
    return prg_staged;
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
    case MT_C64_CONTENT_SERVICE_REU_CONFLICT: return "turn REU off before loading a cartridge";
    }
    return "unknown C64 content service status";
}
