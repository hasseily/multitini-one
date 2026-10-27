/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_content_service.h"

#include "multitini_c64_interface.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"
#include "multitini_media_browser.h"
#include "persistent_save.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MT_C64_CONTENT_PATH_CAPACITY (2U * MT_MEDIA_BROWSER_PATH_MAX + 2U)
#define MT_C64_CONTENT_POLLS UINT32_C(100000)
#define MT_C64_PRG_STREAM_POLLS UINT32_MAX
#define MT_C64_PRG_BOOT_TIMEOUT_MS UINT32_C(5000)
#define MT_C64_PRG_RUN_TIMEOUT_MS UINT32_C(2000)
/* Save once flash writes have been quiet this long. */
#define MT_C64_EF_QUIET_MS UINT64_C(1500)
/* With the guest held, this many empty journal reads end a drain. */
#define MT_C64_EF_IDLE_READS 64U
#define MT_C64_EF_POLL_ENTRIES 64U

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
static uint16_t crt_hardware_type;
static uint8_t crt_subtype;
static bool prg_staged;
static char crt_name[MT_MEDIA_BROWSER_PATH_MAX + 1U];
static char media_root[MT_MEDIA_BROWSER_PATH_MAX + 1U] = MT_C64_SD_MEDIA_ROOT;
static uint8_t ef_image[MT_C64_EASYFLASH_IMAGE_SIZE];
static mt_persistent_save_t ef_save;
static bool ef_active;
static bool ef_dirty;
static bool ef_faulted;
static uint64_t ef_changed_ms;
static uint32_t ef_flash_writes;
/* Where NAME.sav lives, and the CRT's own title, for exports. */
static char ef_directory[MT_C64_CONTENT_PATH_CAPACITY];
static char ef_base[MT_MEDIA_BROWSER_PATH_MAX + 1U];
static char ef_title[33];

uint32_t c64_content_service_expansion_caps(void)
{
    if (content_link == NULL || content_link->read32 == NULL ||
        content_link->read32(content_link->context, MT_REG_CORE_ID) != MT_C64_CORE_ID) return 0U;
    const uint32_t abi = content_link->read32(content_link->context, MT_REG_ABI_VERSION);
    if ((abi >> 16U) != MT_ABI_MAJOR || (uint16_t)abi < MT_C64_EXPANSION_MIN_ABI_MINOR) return 0U;
    const uint32_t caps = content_link->read32(content_link->context, MT_REG_C64_EXPANSION_CAPS);
    return (caps & MT_C64_EXPANSION_CAPS_VALID) != 0U ? caps : 0U;
}

static bool reu_requested(void)
{
    return (c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_REU_16MB) != 0U &&
        (content_link->read32(content_link->context, MT_REG_C64_EXPANSION_CONFIG) &
         MT_C64_EXPANSION_REU_ENABLE) != 0U;
}

uint8_t c64_content_service_crt_expansion_policy(void)
{
    if (!crt_attached)
        return MT_C64_CRT_ALLOW_REU | MT_C64_CRT_ALLOW_UCI | MT_C64_CRT_ALLOW_ULTIMATE_AUDIO;
    if (!(c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_COEXIST))
        return 0U;
    return mt_c64_crt_expansion_policy(crt_hardware_type, crt_subtype);
}

bool c64_content_service_reu_enabled(void)
{
    if (!reu_requested()) return false;
    if (c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_COEXIST)
        return (content_link->read32(content_link->context, MT_REG_C64_REU_HOST) &
                MT_C64_REU_HOST_ACTIVE) != 0U;
    return !crt_attached;
}

bool c64_content_service_reu_load_allowed(void)
{
    if (!(c64_content_service_crt_expansion_policy() & MT_C64_CRT_ALLOW_REU))
        return false;
    /* A disabled REU may be preloaded for later use. When requested on, the
     * live status also protects against an ambiguous content-transfer result. */
    return !reu_requested() || c64_content_service_reu_enabled();
}

static uint64_t now_ms(void)
{
    return content_time_ms == NULL ? 0U : content_time_ms(content_time_context);
}

static bool ef_journal_available(void)
{
    return (c64_content_service_expansion_caps() &
            MT_C64_EXPANSION_CAPS_CART_EASYFLASH) != 0U;
}

/* Pop up to limit journal words. apply is false for words that belong to an
 * image no longer attached. Returns the count, or -1 for an invalid word. */
static int ef_drain(unsigned limit, bool apply)
{
    unsigned count;
    for (count = 0U; count < limit; ++count) {
        const uint32_t entry = content_link->read32(content_link->context,
                                                    MT_REG_C64_EF_JOURNAL);
        if ((entry & MT_C64_EF_JOURNAL_VALID) == 0U) break;
        content_link->write32(content_link->context, MT_REG_C64_EF_JOURNAL, entry);
        if (!apply) continue;
        if (!mt_c64_easyflash_apply(ef_image, entry)) {
            ef_faulted = true;
            return -1;
        }
        ++ef_flash_writes;
        ef_dirty = true;
        ef_changed_ms = now_ms();
    }
    return (int)count;
}

static void ef_drain_idle(bool apply)
{
    unsigned idle = 0U;
    if (!ef_journal_available()) return;
    while (idle < MT_C64_EF_IDLE_READS) {
        const int drained = ef_drain(MT_C64_EF_POLL_ENTRIES, apply);
        if (drained < 0) return;
        idle = drained == 0 ? idle + 1U : 0U;
    }
}

static int ef_commit(void)
{
    if (!ef_dirty) return 0;
    if (mt_persistent_save_commit(&ef_save, ef_image, sizeof(ef_image)).status !=
        MT_PERSISTENT_SAVE_OK) {
        ef_faulted = true;
        /* Retry after another quiet interval, not on every poll. */
        ef_changed_ms = now_ms();
        return -1;
    }
    ef_dirty = false;
    ef_faulted = false;
    return 0;
}

static void ef_close(void)
{
    mt_persistent_save_close(&ef_save);
    ef_active = false;
    ef_dirty = false;
    ef_faulted = false;
}

/* NAME.sav beside the CRT holds the whole canonical flash image. Without one,
 * the image comes from the CRT. */
static int ef_open(const char *file_name, const uint8_t *crt, size_t size)
{
    char directory[MT_C64_CONTENT_PATH_CAPACITY];
    const char *const slash = strrchr(file_name, '/');
    const char *const base = slash == NULL ? file_name : slash + 1;
    if (slash == NULL) {
        memcpy(directory, media_root, strlen(media_root) + 1U);
    } else {
        char parent[MT_MEDIA_BROWSER_PATH_MAX + 1U];
        const size_t length = (size_t)(slash - file_name);
        if (length == 0U || length >= sizeof(parent)) return -1;
        memcpy(parent, file_name, length);
        parent[length] = '\0';
        if (!mt_media_browser_join(media_root, parent, directory, sizeof(directory)))
            return -1;
    }
    if (strlen(base) >= sizeof(ef_base)) return -1;
    if (mt_persistent_save_open(&ef_save, directory, base, ef_image,
                                sizeof(ef_image), 0xffU).status !=
        MT_PERSISTENT_SAVE_OK) return -1;
    if (!ef_save.existed) mt_c64_easyflash_expand(crt, size, ef_image);
    memcpy(ef_directory, directory, strlen(directory) + 1U);
    memcpy(ef_base, base, strlen(base) + 1U);
    memset(ef_title, 0, sizeof(ef_title));
    if (size >= 0x40U) memcpy(ef_title, crt + 0x20U, 32U);
    return 0;
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

static int release_launch_hold(void *unused)
{
    (void)unused;
    return set_core_reset(false) == MT_LINK_OK ? 0 : -1;
}

static int accept_crt_replacement(void *context)
{
    const bool held = *(const bool *)context;
    /* STREAM_BEGIN alone does not replace a committed cartridge. Only the
     * receiver's first accepted byte transfers ownership to this object. */
    crt_attached = false;
    crt_name[0] = '\0';
    return held ? release_launch_hold(NULL) : 0;
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
    mt_content_type_t classified;
    if (type == NULL ||
        !mt_media_browser_join(media_root, file_name, path, capacity)) return -1;
    classified = mt_content_classify(file_name);
    if (classified != MT_CONTENT_PRG && classified != MT_CONTENT_CRT) {
        return -1;
    }
    *type = classified;
    return 0;
}

int c64_content_service_set_media_root(const char *root)
{
    mt_media_browser_t browser;
    if (!mt_media_browser_init(&browser, root) ||
        (crt_attached && strcmp(media_root, browser.root) != 0)) return -1;
    memcpy(media_root, browser.root, strlen(browser.root) + 1U);
    return 0;
}

static mt_c64_content_service_result_t load_image(
    mt_content_type_t type, const uint8_t *data, size_t size,
    const char *file_name, bool stage_prg, const mt_c64_launch_hook_t *hook)
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
    /* New mapper types require their optional FPGA capability before any
     * reset, launch hook or stream can disturb an attached cartridge. */
    if (type == MT_CONTENT_CRT) {
        uint32_t required_caps = 0U;
        switch (result.content.hardware_type) {
        case MT_CRT_FINAL_CARTRIDGE_3:
            required_caps = MT_C64_EXPANSION_CAPS_CART_COEXIST;
            break;
        case MT_CRT_WESTERMANN:
        case MT_CRT_KINGSOFT_BASIC:
        case MT_CRT_BLACKBOX_V8:
        case MT_CRT_BLACKBOX_V3:
        case MT_CRT_BLACKBOX_V4:
        case MT_CRT_BLACKBOX_V9:
        case MT_CRT_MEGABYTER:
        case MT_CRT_TWO_MEGABYTER:
            required_caps = MT_C64_EXPANSION_CAPS_CART_ROM_EXT;
            break;
        case MT_CRT_EASYFLASH:
            required_caps = MT_C64_EXPANSION_CAPS_CART_EASYFLASH;
            break;
        default:
            break;
        }
        if (required_caps != 0U &&
            (c64_content_service_expansion_caps() & required_caps) != required_caps) {
            result.content.status = MT_C64_CONTENT_UNSUPPORTED_CRT;
            result.status = MT_C64_CONTENT_SERVICE_REJECTED;
            return result;
        }
    }

    /* An attached EasyFlash must reach SD before anything replaces it. Hold
     * the guest so no flash write follows the final journal drain. */
    if (ef_active) {
        (void)set_core_reset(true);
        ef_drain_idle(true);
        if (ef_commit() != 0) {
            if (!(hook != NULL && hook->hold_reset)) (void)set_core_reset(false);
            result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
            return result;
        }
        ef_close();
    }
    const bool easyflash = type == MT_CONTENT_CRT &&
                           result.content.hardware_type == MT_CRT_EASYFLASH;
    if (easyflash) {
        if (ef_open(file_name, image.data, image.size) != 0) {
            result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
            return result;
        }
        /* The CRT has been expanded, so its buffer can carry the stream. */
        image.data = content_buffer;
        image.size = mt_c64_easyflash_stream(ef_image, content_buffer,
                                             sizeof(content_buffer));
    }

    const bool held = hook != NULL && hook->hold_reset;
    const bool track_crt_acceptance = held ||
        (type == MT_CONTENT_CRT &&
         (c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_COEXIST));
    result.link = held ? set_core_reset(true) : release_stale_core_reset();
    if (result.link != MT_LINK_OK) {
        result.status = held ? MT_C64_CONTENT_SERVICE_CONFIG_FAILED :
                               MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    if (hook != NULL && hook->commit != NULL && hook->commit(hook->context) != 0) {
        if (held) (void)set_core_reset(false);
        result.status = MT_C64_CONTENT_SERVICE_CONFIG_FAILED;
        return result;
    }

    if (type == MT_CONTENT_PRG && crt_attached) {
        if (held) {
            result.link = mt_c64_detach_crt(content_link, MT_C64_CONTENT_POLLS,
                                           content_progress, content_progress_context);
            if (result.link != MT_LINK_OK) {
                (void)set_core_reset(false);
                result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
            } else { crt_attached = false; crt_name[0] = '\0'; }
        } else result = c64_content_service_detach_crt();
        if (result.status != MT_C64_CONTENT_SERVICE_OK) return result;
    }
    prg_staged = false;
    clear_core_load_error();
    if (type == MT_CONTENT_PRG) {
        start_deadline(&deadline, MT_C64_PRG_BOOT_TIMEOUT_MS);
        const uint32_t polls = content_time_ms == NULL ?
                               MT_C64_CONTENT_POLLS :
                               MT_C64_PRG_STREAM_POLLS;
        result.link = mt_core_link_stream_handoff(
            content_link,
            stage_prg ? MT_LOAD_C64_PRG_STAGE : MT_LOAD_C64_PRG,
            image.data, image.size, polls,
            content_progress, content_progress_context,
            before_deadline, &deadline, held ? release_launch_hold : NULL, NULL);
    } else {
        result.link = track_crt_acceptance ? mt_core_link_stream_handoff(content_link,
            MT_LOAD_C64_CRT, image.data, image.size, MT_C64_CONTENT_POLLS,
            content_progress, content_progress_context, NULL, NULL,
            accept_crt_replacement, (void *)&held) : mt_c64_load_crt(content_link, &image,
            MT_C64_CONTENT_POLLS, content_progress, content_progress_context);
    }
    if (result.link != MT_LINK_OK) {
        if (type == MT_CONTENT_PRG) {
            /* mt_core_link_stream_until() already issued the single abort. */
            pulse_core_reset();
        } else {
            (void)set_core_reset(false);
        }
        /* New images clear ownership at the acknowledged first byte above.
         * A failure before that point preserves the old attachment. A missing
         * acknowledgement is ambiguous, so live effective expansion status,
         * not this conservative metadata, authorizes REU access. */
        if (type == MT_CONTENT_CRT && !track_crt_acceptance) {
            crt_attached = false;
            crt_name[0] = '\0';
        }
        if (easyflash) ef_close();
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
        if (easyflash) ef_close();
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    if (easyflash) {
        /* The load cleared the FPGA queue; anything left belongs to the
         * image this one replaced. */
        if (ef_journal_available()) (void)ef_drain(MT_C64_EF_POLL_ENTRIES, false);
        ef_active = true;
        ef_dirty = false;
        ef_faulted = false;
    }
    crt_attached = type == MT_CONTENT_CRT;
    if (crt_attached) {
        crt_hardware_type = result.content.hardware_type;
        crt_subtype = result.content.subtype;
        memcpy(crt_name, file_name, strlen(file_name) + 1U);
    } else
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
    memcpy(media_root, MT_C64_SD_MEDIA_ROOT, sizeof(MT_C64_SD_MEDIA_ROOT));
    ef_close();
}

void c64_content_service_poll(void)
{
    if (!ef_active || content_link == NULL || !ef_journal_available()) return;
    (void)ef_drain(MT_C64_EF_POLL_ENTRIES, true);
    if (ef_dirty && now_ms() - ef_changed_ms >= MT_C64_EF_QUIET_MS)
        (void)ef_commit();
}

int c64_content_service_flush(void)
{
    if (!ef_active || content_link == NULL) return 0;
    ef_drain_idle(true);
    return ef_commit();
}

/* The guest is held by the caller; flush, then end the EasyFlash session. */
static int ef_finish(void)
{
    if (!ef_active) return 0;
    ef_drain_idle(true);
    if (ef_commit() != 0) return -1;
    ef_close();
    return 0;
}

bool c64_content_service_save_active(void)
{
    return ef_active;
}

bool c64_content_service_save_dirty(void)
{
    return ef_active && ef_dirty;
}

bool c64_content_service_save_faulted(void)
{
    return ef_active && ef_faulted;
}

uint32_t c64_content_service_flash_writes(void)
{
    return ef_flash_writes;
}

bool c64_content_service_ef_jumper_disabled(void)
{
    return (c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_EASYFLASH) != 0U &&
        (content_link->read32(content_link->context, MT_REG_C64_PERIPHERAL_CONTROL) &
         MT_C64_PERIPHERAL_EF_BOOT_DISABLE) != 0U;
}

int c64_content_service_set_ef_jumper(bool disabled)
{
    uint32_t value;
    if ((c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_EASYFLASH) == 0U)
        return -1;
    value = content_link->read32(content_link->context, MT_REG_C64_PERIPHERAL_CONTROL);
    value = disabled ? value | MT_C64_PERIPHERAL_EF_BOOT_DISABLE :
                       value & ~MT_C64_PERIPHERAL_EF_BOOT_DISABLE;
    content_link->write32(content_link->context, MT_REG_C64_PERIPHERAL_CONTROL, value);
    return c64_content_service_ef_jumper_disabled() == disabled ? 0 : -1;
}

mt_c64_content_service_result_t c64_content_service_export_crt(
    char *exported, size_t capacity)
{
    mt_c64_content_service_result_t result = service_result(MT_C64_CONTENT_SERVICE_OK);
    char final_path[MT_C64_CONTENT_PATH_CAPACITY + 16U];
    char temporary_path[sizeof(final_path) + 8U];
    char name[MT_MEDIA_BROWSER_PATH_MAX + 16U];
    mt_sd_file_t *file = NULL;
    mt_sd_info_t info;
    const char *dot;
    size_t stem;
    size_t size;
    if (!ef_active || content_link == NULL) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        return result;
    }
    /* Mirror every journaled write first; a faulted mirror is not exported. */
    ef_drain_idle(true);
    if (ef_faulted && ef_dirty) {
        result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
        return result;
    }
    dot = strrchr(ef_base, '.');
    stem = dot == NULL ? strlen(ef_base) : (size_t)(dot - ef_base);
    if (stem == 0U ||
        (size_t)snprintf(name, sizeof(name), "%.*s-saved.crt", (int)stem, ef_base) >= sizeof(name) ||
        (size_t)snprintf(final_path, sizeof(final_path), "%s/%s", ef_directory, name) >=
            sizeof(final_path) ||
        (size_t)snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", final_path) >=
            sizeof(temporary_path)) {
        result.status = MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT;
        return result;
    }
    size = mt_c64_easyflash_export(ef_image, ef_title, content_buffer, sizeof(content_buffer));
    if (size == 0U) {
        result.status = MT_C64_CONTENT_SERVICE_EXPORT_FAILED;
        return result;
    }
    /* Write a temporary file, then replace an earlier export of this name. */
    result.sd = mt_sd_file_open(temporary_path, "wb", &file);
    if (result.sd.status == MT_SD_OK) {
        result.sd = mt_sd_file_write_buffered(file, content_buffer, size);
        const mt_sd_result_t closed = mt_sd_file_close(file);
        if (result.sd.status == MT_SD_OK) result.sd = closed;
    }
    if (result.sd.status == MT_SD_OK && mt_sd_file_stat(final_path, &info).status == MT_SD_OK)
        result.sd = mt_sd_file_remove(final_path);
    if (result.sd.status == MT_SD_OK)
        result.sd = mt_sd_file_rename(temporary_path, final_path);
    if (result.sd.status != MT_SD_OK) {
        if (file != NULL) (void)mt_sd_file_remove(temporary_path);
        result.status = MT_C64_CONTENT_SERVICE_EXPORT_FAILED;
        return result;
    }
    if (exported != NULL && capacity != 0U)
        (void)snprintf(exported, capacity, "%s", name);
    result.sd.size = size;
    return result;
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
    if (ef_active) {
        (void)set_core_reset(true);
        if (ef_finish() != 0) {
            (void)set_core_reset(false);
            result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
            return result;
        }
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

mt_c64_content_service_result_t c64_content_service_detach_crt_hooked(
    const mt_c64_launch_hook_t *hook)
{
    if (hook == NULL) return c64_content_service_detach_crt();
    mt_c64_content_service_result_t result = service_result(MT_C64_CONTENT_SERVICE_OK);
    if (content_link == NULL || content_link->read32 == NULL || content_link->write32 == NULL)
        return service_result(MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    result.link = set_core_reset(true);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_CONFIG_FAILED;
        return result;
    }
    if (ef_finish() != 0) {
        (void)set_core_reset(false);
        result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
        return result;
    }
    clear_core_load_error();
    result.link = mt_c64_detach_crt(content_link, MT_C64_CONTENT_POLLS,
        content_progress, content_progress_context);
    if (result.link == MT_LINK_OK) {
        crt_attached = false;
        prg_staged = false;
        crt_name[0] = '\0';
        if (hook->commit != NULL && hook->commit(hook->context) != 0)
            result.status = MT_C64_CONTENT_SERVICE_CONFIG_FAILED;
    } else result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
    const mt_link_result_t released = set_core_reset(false);
    if (result.status == MT_C64_CONTENT_SERVICE_OK) {
        result.link = released;
        if (result.link == MT_LINK_OK) result.link = wait_for_core_result(NULL);
        if (result.link != MT_LINK_OK) result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
    }
    return result;
}

mt_c64_content_service_result_t c64_content_service_load(
    const char *file_name)
{
    return c64_content_service_load_hooked(file_name, NULL);
}

mt_c64_content_service_result_t c64_content_service_reboot(
    const char *default_cartridge, int (*restore_settings)(void *), void *context)
{
    mt_c64_content_service_result_t result = service_result(MT_C64_CONTENT_SERVICE_OK);
    if (content_link == NULL || content_link->read32 == NULL || content_link->write32 == NULL)
        return service_result(MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
    result.link = set_core_reset(true);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    if (ef_finish() != 0) {
        result.status = MT_C64_CONTENT_SERVICE_SAVE_FAILED;
        return result;
    }
    clear_core_load_error();
    result.link = mt_c64_detach_crt(content_link, MT_C64_CONTENT_POLLS,
                                   content_progress, content_progress_context);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        return result;
    }
    crt_attached = prg_staged = false;
    crt_name[0] = '\0';
    if (restore_settings != NULL && restore_settings(context) != 0)
        return service_result(MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
    if (default_cartridge != NULL && default_cartridge[0] != '\0') {
        if (mt_content_classify(default_cartridge) != MT_CONTENT_CRT)
            return service_result(MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT);
        const mt_c64_launch_hook_t hook = {
            .reu_requested = reu_requested(), .hold_reset = true};
        result = c64_content_service_load_hooked(default_cartridge, &hook);
        if (result.status != MT_C64_CONTENT_SERVICE_OK)
            (void)set_core_reset(true);
        return result;
    }
    result.link = set_core_reset(false);
    if (result.link == MT_LINK_OK) result.link = wait_for_core_result(NULL);
    if (result.link != MT_LINK_OK) {
        result.status = MT_C64_CONTENT_SERVICE_TRANSFER_FAILED;
        (void)set_core_reset(true);
    }
    return result;
}

mt_c64_content_service_result_t c64_content_service_load_hooked(
    const char *file_name, const mt_c64_launch_hook_t *hook)
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
    if (type == MT_CONTENT_CRT &&
        !(c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_COEXIST) &&
        (hook != NULL ? hook->reu_requested : reu_requested())) {
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
        result = load_image(type, image.data, image.size, file_name, false, hook);
        result.sd = storage_result;
    }
    return result;
}

mt_c64_content_service_result_t c64_content_service_stage_prg_image(
    const uint8_t *data, size_t size)
{
    return c64_content_service_stage_prg_image_hooked(data, size, NULL);
}

mt_c64_content_service_result_t c64_content_service_stage_prg_image_hooked(
    const uint8_t *data, size_t size, const mt_c64_launch_hook_t *hook)
{
    return load_image(MT_CONTENT_PRG, data, size, NULL, true, hook);
}

mt_c64_content_service_result_t c64_content_service_prepare_disk_load(void)
{
    return c64_content_service_prepare_disk_load_hooked(NULL);
}

mt_c64_content_service_result_t c64_content_service_prepare_disk_load_hooked(
    const mt_c64_launch_hook_t *hook)
{
    static const uint8_t empty_basic[] = {0x01U, 0x08U, 0U, 0U};
    const mt_c64_content_service_result_t result =
        c64_content_service_stage_prg_image_hooked(empty_basic, sizeof(empty_basic), hook);
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
    case MT_C64_CONTENT_SERVICE_CONFIG_FAILED: return "title settings could not be applied";
    case MT_C64_CONTENT_SERVICE_SAVE_FAILED: return "EasyFlash save could not be read or written";
    case MT_C64_CONTENT_SERVICE_EXPORT_FAILED: return "EasyFlash CRT export could not be written";
    }
    return "unknown C64 content service status";
}
