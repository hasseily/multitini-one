/* SPDX-License-Identifier: GPL-3.0-only */
#include "vectrex_content_service.h"
#include "multitini_vectrex_boot.h"
#include "multitini_vectrex_interface.h"
#include "multitini_vectrex_overlay.h"
#include "multitini_regs.h"
#include <string.h>
#define POLLS UINT32_C(100000)
static mt_core_link_t *link;
static mt_link_progress_fn progress;
static void *progress_context;
static mt_vectrex_basic_run_fn basic_run;
static void *basic_context;
static char directory[MT_VECTREX_CONTENT_PATH_CAPACITY];
static char attached_name[MT_VECTREX_CONTENT_PATH_CAPACITY];
static uint8_t bios[MT_SIZE_VECTREX_BIOS];
static uint8_t content[MT_SIZE_VECTREX_BASIC_MAX + 1U];
/* BASIC executes from content while its sidecar is transferred separately. */
static uint8_t overlay[MT_VECTREX_OVERLAY_BYTES];
static const char *overlay_warning = "";
static bool attached;
static bool basic_selected;
static bool basic_running;
static char last_basic_name[MT_VECTREX_CONTENT_PATH_CAPACITY];
static bool bios_cached;

static mt_vectrex_content_result_t result(mt_vectrex_content_status_t status)
{
    mt_vectrex_content_result_t r;
    memset(&r, 0, sizeof(r)); r.status = status; return r;
}

static int normalize_relative_path(const char *name,
    char normalized[MT_VECTREX_CONTENT_PATH_CAPACITY])
{
    size_t i, component = 0U;
    if (name == NULL) return -1;
    for (i = 0U; i < MT_VECTREX_CONTENT_PATH_CAPACITY; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c == '\0' || c == '/' || c == '\\') {
            /* This also rejects dot and dotdot, including FAT's trailing
             * dot/space aliases, before any filesystem access. */
            if (i == component || normalized[i - 1U] == '.' || normalized[i - 1U] == ' ')
                return -1;
            normalized[i] = c == '\0' ? '\0' : '/';
            if (c == '\0') return 0;
            component = i + 1U;
        } else {
            if (i + 1U == MT_VECTREX_CONTENT_PATH_CAPACITY || c < 0x20U || c == 0x7fU ||
                strchr(":*?\"<>|", c) != NULL) return -1;
            normalized[i] = (char)c;
        }
    }
    return -1;
}

static int make_path(const char *relative, char path[MT_VECTREX_CONTENT_ABSOLUTE_CAPACITY])
{
    const size_t a = strlen(directory), b = strlen(relative);
    const size_t separator = a != 0U && directory[a - 1U] == '/' ? 0U : 1U;
    if (a == 0U || a + separator + b + 1U > MT_VECTREX_CONTENT_ABSOLUTE_CAPACITY) return -1;
    memcpy(path, directory, a);
    if (separator != 0U) path[a] = '/';
    memcpy(path + a + separator, relative, b + 1U);
    return 0;
}

void vectrex_content_service_init(mt_core_link_t *core_link, const char *root,
    mt_link_progress_fn notify, void *notify_context,
    mt_vectrex_basic_run_fn run, void *run_context)
{
    link = core_link; progress = notify; progress_context = notify_context;
    basic_run = run; basic_context = run_context; attached = false; basic_selected = false; bios_cached = false;
    basic_running = false; last_basic_name[0] = '\0';
    directory[0] = '\0'; attached_name[0] = '\0';
    overlay_warning = "";
    if (root != NULL && strlen(root) < sizeof(directory) && strlen(root) >= 3U &&
        root[1] == ':' && root[2] == '/' && strstr(root, "..") == NULL)
        memcpy(directory, root, strlen(root) + 1U);
}

static mt_link_result_t ready(void)
{
    uint32_t i;
    for (i = 0U; i < POLLS; ++i) {
        uint32_t status;
        if (progress != NULL) progress(progress_context);
        status = link->read32(link->context, MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U) return MT_LINK_REMOTE_ERROR;
        if ((status & MT_STATUS_CORE_READY) != 0U) return MT_LINK_OK;
    }
    return MT_LINK_TIMEOUT;
}

static bool overlay_supported(void)
{
    uint32_t version;
    if (link == NULL || link->read32 == NULL || link->write32 == NULL) return false;
    if (link->read32(link->context, MT_REG_CORE_ID) != MT_VECTREX_CORE_ID) return false;
    version = link->read32(link->context, MT_REG_ABI_VERSION);
    return (version >> 16U) == MT_ABI_MAJOR &&
        (uint16_t)version >= MT_VECTREX_VIDEO_MIN_ABI_MINOR;
}

static mt_link_result_t overlay_ready(bool loaded)
{
    uint32_t i;
    for (i = 0U; i < POLLS; ++i) {
        uint32_t status;
        if (progress != NULL) progress(progress_context);
        status = link->read32(link->context, MT_REG_VECTREX_OVERLAY_STATUS);
        if ((status & MT_VECTREX_OVERLAY_STATUS_ERROR) != 0U) return MT_LINK_REMOTE_ERROR;
        if (((status & MT_VECTREX_OVERLAY_STATUS_LOADED) != 0U) == loaded) return MT_LINK_OK;
    }
    return MT_LINK_TIMEOUT;
}

static void clear_overlay(void)
{
    if (overlay_supported() &&
        (mt_vectrex_overlay_clear(link, POLLS, progress, progress_context) != MT_LINK_OK ||
         overlay_ready(false) != MT_LINK_OK))
        overlay_warning = "Game loaded; previous overlay could not be cleared";
}

static mt_vectrex_content_result_t load_overlay_path(const char *path, bool optional)
{
    bool found = true;
    mt_vectrex_content_result_t r = result(MT_VECTREX_CONTENT_OK);
    if (!overlay_supported()) return result(MT_VECTREX_CONTENT_OVERLAY_UNSUPPORTED);
    r.sd = optional ? mt_sd_storage_read_file_optional_max(path, overlay, sizeof(overlay), &found) :
        mt_sd_storage_read_file_max(path, overlay, sizeof(overlay));
    if (r.sd.status != MT_SD_OK) { r.status = MT_VECTREX_CONTENT_READ_FAILED; return r; }
    if (!found) return r;
    if (!mt_vectrex_overlay_valid_size(r.sd.size)) {
        r.status = MT_VECTREX_CONTENT_REJECTED;
        r.validation = r.sd.size == 0U ? MT_CONTENT_TOO_SMALL : MT_CONTENT_WRONG_SIZE;
        return r;
    }
    r.link = mt_vectrex_overlay_load(link, overlay, sizeof(overlay), r.sd.size,
        POLLS, progress, progress_context);
    if (r.link == MT_LINK_OK) r.link = overlay_ready(true);
    if (r.link != MT_LINK_OK) {
        r.status = MT_VECTREX_CONTENT_TRANSFER_FAILED;
        clear_overlay();
    }
    return r;
}

static void load_sidecar(const char *relative)
{
    char sidecar[MT_VECTREX_CONTENT_PATH_CAPACITY];
    char path[MT_VECTREX_CONTENT_ABSOLUTE_CAPACITY];
    if (!overlay_supported()) return;
    if (!mt_vectrex_overlay_sidecar_path(relative, sidecar, sizeof(sidecar)) ||
        make_path(sidecar, path) != 0 ||
        load_overlay_path(path, true).status != MT_VECTREX_CONTENT_OK)
        overlay_warning = "Game loaded; overlay could not be loaded";
}

static mt_vectrex_content_result_t boot(const uint8_t *cart, size_t size)
{
    mt_vectrex_content_result_t r = result(MT_VECTREX_CONTENT_OK);
    if (link == NULL) return result(MT_VECTREX_CONTENT_INVALID_ARGUMENT);
    r.sd = mt_sd_storage_read_file_exact(MT_VECTREX_BIOS_PATH, bios, sizeof(bios));
    bios_cached = r.sd.status == MT_SD_OK;
    if (r.sd.status != MT_SD_OK) { r.status = MT_VECTREX_CONTENT_READ_FAILED; return r; }
    overlay_warning = "";
    clear_overlay();
    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    r.link = mt_vectrex_boot(link, bios, sizeof(bios), cart, size, POLLS,
                             progress, progress_context);
    if (r.link == MT_LINK_OK) r.link = ready();
    if (r.link == MT_LINK_OK) basic_selected = false;
    if (r.link != MT_LINK_OK) r.status = MT_VECTREX_CONTENT_TRANSFER_FAILED;
    return r;
}

mt_vectrex_content_result_t vectrex_content_service_boot_bios(void)
{
    mt_vectrex_content_result_t r = boot(NULL, 0U);
    if (r.status == MT_VECTREX_CONTENT_OK) load_sidecar("Mine_Storm.vec");
    return r;
}

mt_vectrex_content_result_t vectrex_content_service_load(const char *name)
{
    char path[MT_VECTREX_CONTENT_ABSOLUTE_CAPACITY];
    char relative[MT_VECTREX_CONTENT_PATH_CAPACITY];
    mt_content_info_t info;
    mt_content_type_t type;
    mt_vectrex_content_result_t r = result(MT_VECTREX_CONTENT_OK);
    if (basic_running) return result(MT_VECTREX_CONTENT_BASIC_FAILED);
    if (link == NULL || normalize_relative_path(name, relative) != 0 || make_path(relative, path) != 0)
        return result(MT_VECTREX_CONTENT_INVALID_ARGUMENT);
    type = mt_content_classify_for_core(MT_VECTREX_CORE_ID, relative);
    if (type == MT_CONTENT_VECTREX_OVERLAY) {
        r = load_overlay_path(path, false);
        if (r.status == MT_VECTREX_CONTENT_OK) overlay_warning = "";
        return r;
    }
    if (type != MT_CONTENT_VECTREX_CART && type != MT_CONTENT_VECTREX_BASIC)
        return result(MT_VECTREX_CONTENT_REJECTED);
    r.sd = mt_sd_storage_read_file_max(path, content,
        type == MT_CONTENT_VECTREX_CART ? MT_SIZE_VECTREX_CART_MAX : MT_SIZE_VECTREX_BASIC_MAX);
    if (r.sd.status != MT_SD_OK) { r.status = MT_VECTREX_CONTENT_READ_FAILED; return r; }
    r.validation = mt_content_inspect(type, content, r.sd.size, &info);
    if (r.validation != MT_CONTENT_VALID) { r.status = MT_VECTREX_CONTENT_REJECTED; return r; }
    if (type == MT_CONTENT_VECTREX_BASIC) {
        const char *basename = strrchr(relative, '/');
        const char *basic_directory = directory;
        if (basic_run == NULL) return result(MT_VECTREX_CONTENT_BASIC_FAILED);
        overlay_warning = "";
        clear_overlay();
        load_sidecar(relative);
        if (basename != NULL) {
            ++basename;
            *strrchr(path, '/') = '\0';
            basic_directory = path;
        } else basename = relative;
        content[r.sd.size] = 0U;
        basic_selected = true;
        last_basic_name[0] = '\0'; basic_running = true;
        attached = false; attached_name[0] = '\0';
        if (basic_run(basic_context, basic_directory, basename, (const char *)content) != 0)
            r.status = MT_VECTREX_CONTENT_BASIC_FAILED;
        basic_running = false;
        if (r.status == MT_VECTREX_CONTENT_OK)
            memcpy(last_basic_name, relative, strlen(relative) + 1U);
        return r;
    }
    r = boot(content, r.sd.size);
    if (r.status == MT_VECTREX_CONTENT_OK) {
        attached = true; memcpy(attached_name, relative, strlen(relative) + 1U);
        load_sidecar(relative);
    } else if (r.status == MT_VECTREX_CONTENT_TRANSFER_FAILED) {
        attached = false; attached_name[0] = '\0';
    }
    return r;
}

mt_vectrex_content_result_t vectrex_content_service_restart_basic(void)
{
    char name[sizeof(last_basic_name)];
    if (basic_running) return result(MT_VECTREX_CONTENT_BASIC_FAILED);
    if (!basic_selected || last_basic_name[0] == '\0')
        return result(MT_VECTREX_CONTENT_NO_BASIC_PROGRAM);
    /* The normal load path clears remembered state before entering the next
     * interpreter. Keep a local filename while it reloads the source. */
    memcpy(name, last_basic_name, sizeof(name));
    return vectrex_content_service_load(name);
}

mt_vectrex_content_result_t vectrex_content_service_detach(void)
{
    uint32_t args[4] = {1U, 0U, 0U, 0U};
    mt_command_response_t response;
    mt_vectrex_content_result_t r = result(MT_VECTREX_CONTENT_OK);
    if (basic_running) return result(MT_VECTREX_CONTENT_BASIC_FAILED);
    if (link == NULL) return result(MT_VECTREX_CONTENT_INVALID_ARGUMENT);
    r.link = mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, POLLS,
                                  progress, progress_context, &response);
    if (r.link == MT_LINK_OK)
        r.link = mt_vectrex_set_mode(link, 2U, POLLS, progress, progress_context);
    if (r.link == MT_LINK_OK) {
        basic_selected = false;
        attached = false; attached_name[0] = '\0';
        overlay_warning = "";
        clear_overlay();
        load_sidecar("Mine_Storm.vec");
        args[0] = 0U;
        r.link = mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, POLLS,
                                      progress, progress_context, &response);
    }
    if (r.link == MT_LINK_OK) r.link = ready();
    if (r.link != MT_LINK_OK) r.status = MT_VECTREX_CONTENT_TRANSFER_FAILED;
    return r;
}

bool vectrex_content_service_cartridge_attached(void) { return attached; }
bool vectrex_content_service_basic_selected(void) { return basic_selected; }
const char *vectrex_content_service_cartridge_name(void) { return attached_name; }
const char *vectrex_content_service_overlay_warning(void) { return overlay_warning; }
int vectrex_content_service_read_bios(uint16_t address, uint8_t *data, size_t count)
{
    if (address < 0xe000U || count > sizeof(bios) ||
        (size_t)(address - 0xe000U) > sizeof(bios) - count || (data == NULL && count != 0U)) return 0;
    if (!bios_cached)
        bios_cached = mt_sd_storage_read_file_exact(MT_VECTREX_BIOS_PATH, bios, sizeof(bios)).status == MT_SD_OK;
    if (!bios_cached) return 0;
    if (count != 0U) memcpy(data, bios + (address - 0xe000U), count);
    return 1;
}
const char *vectrex_content_status_name(mt_vectrex_content_status_t status)
{
    switch (status) {
    case MT_VECTREX_CONTENT_OK: return "ok";
    case MT_VECTREX_CONTENT_INVALID_ARGUMENT: return "invalid argument";
    case MT_VECTREX_CONTENT_READ_FAILED: return "file read failed";
    case MT_VECTREX_CONTENT_REJECTED: return "content rejected";
    case MT_VECTREX_CONTENT_TRANSFER_FAILED: return "core transfer failed";
    case MT_VECTREX_CONTENT_BASIC_FAILED: return "BASIC run failed";
    case MT_VECTREX_CONTENT_NO_BASIC_PROGRAM: return "No previous successful BASIC program";
    case MT_VECTREX_CONTENT_OVERLAY_UNSUPPORTED: return "Update the Vectrex core for overlays";
    default: return "unknown";
    }
}
