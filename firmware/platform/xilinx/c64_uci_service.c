/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_uci_internal.h"
#include "c64_uci_image.h"
#include "c64_iec_service.h"
#include "multitini_c64_iec_dos.h"
#include "multitini_c64_interface.h"
#include "net_service.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

c64_uci_environment_t c64_uci_environment;
static mt_uci_engine_t engine;
static bool hold_active, hold_previous, fence_active, ram_active, reboot_hold;
static bool quiesce_active, quiesce_op_pending, reset_pending;
static uint8_t quiesce_ack;
static uint64_t quiesce_start;
static uint64_t hold_start, fence_start, ram_start;
static uint8_t fence_token, ram_token;
static uint16_t ram_address;
static size_t ram_length;
static c64_uci_action_t pending_action;
static char action_path[C64_UCI_PATH];
static unsigned action_device;
static int action_result;
static bool temporary_pending;
static bool temporary_failed, hardware_fault;
static mt_sd_directory_t *temporary_directory;
static mt_sd_directory_t *path_directory;
static c64_uci_image_file_t *path_image_directory;
static bool reload_only;
enum { FAULT_UNKNOWN, FAULT_RAM, FAULT_FENCE, FAULT_NATIVE };
static unsigned hardware_fault_kind;
static char temporary_cursor[C64_UCI_PATH];
uint32_t c64_uci_read(unsigned o)
{
    return c64_uci_environment.link->read32(c64_uci_environment.link->context, o);
}
void c64_uci_write(unsigned o, uint32_t v)
{
    c64_uci_environment.link->write32(c64_uci_environment.link->context, o, v);
}
uint64_t c64_uci_time(void)
{
    return c64_uci_environment.callbacks.microseconds(c64_uci_environment.callbacks.context);
}
static bool copy_path(char *to, const char *from)
{
    if (!from || strlen(from) >= C64_UCI_PATH)
        return false;
    memcpy(to, from, strlen(from) + 1);
    return true;
}
static bool equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++))
            return false;
    }
    return *a == *b;
}
static bool inside(const char *path, const char *root)
{
    size_t n = strlen(root);
    for (size_t i = 0; i < n; i++)
        if (!path[i] || toupper((unsigned char)path[i]) != toupper((unsigned char)root[i]))
            return false;
    return path[n] == 0 || path[n] == '/';
}
static bool protected_access(const char *path, bool writing, bool metadata)
{
    char canonical[C64_UCI_PATH];
    if (mt_sd_file_canonical_path(path, canonical, sizeof canonical).status != MT_SD_OK)
        return true;
    path = canonical;
    if (!inside(path, c64_uci_environment.root) && !inside(path, c64_uci_environment.temporary))
        return true;
    if (writing && (equal(path, c64_uci_environment.root) ||
                    equal(path, c64_uci_environment.temporary)))
        return true;
    char component[256];
    size_t used = 0;
    for (const char *p = path;; p++) {
        if (*p == '/' || !*p) {
            component[used] = 0;
            if (mt_iec_dos_recovery_name(component))
                return true;
            used = 0;
            if (!*p)
                break;
        } else if (used + 1 < sizeof component)
            component[used++] = *p;
        else
            return true;
    }
    if (metadata)
        return false;
    char image[C64_UCI_PATH];
    const char *member;
    if (c64_uci_image_path(path, image, &member) && *member)
        return protected_access(image, writing, false);
    /* Mounted-file ownership protects destructive ancestor operations, but
     * inspecting or entering that ancestor does not access the disk image. */
    mt_sd_info_t info;
    bool directory_read = !writing && mt_sd_file_stat(path, &info).status == MT_SD_OK &&
                          (info.attributes & 16) != 0;
    if (!directory_read && c64_uci_environment.callbacks.protected_path &&
        c64_uci_environment.callbacks.protected_path(c64_uci_environment.callbacks.context, path))
        return true;
    return mt_sd_file_path_busy(path, writing);
}
bool c64_uci_protected(const char *path, bool writing)
{
    return protected_access(path, writing, false);
}
bool c64_uci_metadata_protected(const char *path)
{
    return protected_access(path, false, true);
}
static bool wildcard(const char *pattern, const char *name)
{
    const char *star = NULL, *retry = NULL;
    while (*name) {
        if (*pattern == '?' || toupper((unsigned char)*pattern) == toupper((unsigned char)*name)) {
            pattern++;
            name++;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
        } else if (star) {
            pattern = star + 1;
            name = ++retry;
        } else
            return false;
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern;
}
bool c64_uci_path(const char *cwd, const uint8_t *bytes, size_t length, char out[C64_UCI_PATH])
{
    char name[C64_UCI_PATH], part[256];
    if (length >= sizeof name)
        return false;
    size_t n = 0;
    while (n < length && bytes[n]) {
        uint8_t c = bytes[n];
        if (c >= 0xc1 && c <= 0xda)
            c = (uint8_t)(c - 0x80);
        name[n] = c == '\\' ? '/' : (char)c;
        n++;
    }
    name[n] = 0;
    /* Reject malformed later components before Temp creation or wildcard
     * enumeration can touch storage. Dot components are navigation below. */
    for (const char *scan = name; *scan;) {
        if (*scan == '/') {
            scan++;
            continue;
        }
        size_t count = strcspn(scan, "/");
        bool dot = (count == 1 && scan[0] == '.') ||
                   (count == 2 && scan[0] == '.' && scan[1] == '.');
        if (count >= sizeof part || (!dot && (scan[count - 1] == '.' || scan[count - 1] == ' ')))
            return false;
        for (size_t i = 0; i < count; i++) {
            bool volume = scan == name && count == 2 && i == 1 && scan[2] == '/';
            if ((unsigned char)scan[i] < 32 || strchr("<>\"|", scan[i]) ||
                (scan[i] == ':' && !volume))
                return false;
        }
        scan += count;
    }
    const char *p = name, *root = c64_uci_environment.root;
    if (*p == '/') {
        while (*p == '/')
            p++;
        size_t k = strcspn(p, "/");
        if (k >= sizeof part)
            return false;
        memcpy(part, p, k);
        part[k] = 0;
        bool aliases =
            equal(part, "SD") || equal(part, "Usb0") || equal(part, "Usb1") || equal(part, "Usb2");
        bool temporary = equal(part, "Temp");
        if (strpbrk(part, "*?")) {
            aliases = wildcard(part, "SD") || wildcard(part, "Usb0") || wildcard(part, "Usb1") ||
                      wildcard(part, "Usb2");
            temporary = !aliases && wildcard(part, "Temp");
        }
        if (temporary) {
            root = c64_uci_environment.temporary;
            p += k;
            if (mt_sd_storage_ensure_directory(root).status != MT_SD_OK)
                return false;
        } else if (aliases)
            p += k;
        if (!copy_path(out, root))
            return false;
    } else if (strchr(name, ':')) {
        if (!inside(name, root) && !inside(name, c64_uci_environment.temporary))
            return false;
        root = inside(name, root) ? root : c64_uci_environment.temporary;
        p = name + strlen(root);
        if (!copy_path(out, root))
            return false;
    } else {
        if (!copy_path(out, cwd))
            return false;
        if (inside(cwd, c64_uci_environment.temporary))
            root = c64_uci_environment.temporary;
        else if (!inside(cwd, root))
            return false;
    }
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        size_t k = strcspn(p, "/");
        if (k >= sizeof part)
            return false;
        memcpy(part, p, k);
        part[k] = 0;
        p += k;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, "..")) {
            if (strlen(out) > strlen(root)) {
                char *slash = strrchr(out, '/');
                if (slash)
                    *slash = 0;
            }
            continue;
        }
        if (part[k - 1] == '.' || part[k - 1] == ' ' ||
            mt_iec_dos_recovery_name(part) || strpbrk(part, ":<>\"|") != NULL)
            return false;
        for (size_t j = 0; j < k; j++)
            if ((unsigned char)part[j] < 32)
                return false;
        if (strpbrk(part, "*?")) {
            mt_sd_directory_t *dir = NULL;
            c64_uci_image_file_t *image_dir = NULL;
            mt_sd_info_t info;
            bool found = false;
            char image[C64_UCI_PATH];
            const char *member;
            const bool virtual_dir = c64_uci_image_path(out, image, &member);
            mt_sd_result_t opened = virtual_dir ? c64_uci_image_directory(out, &image_dir) :
                                                  mt_sd_directory_open(out, &dir);
            if (opened.status != MT_SD_OK) {
                if (image_dir && c64_uci_image_close(&image_dir).status != MT_SD_OK) {
                    path_image_directory = image_dir;
                    c64_uci_environment.fault = true;
                }
                return false;
            }
            while ((virtual_dir ? c64_uci_image_next(image_dir, &info) :
                                  mt_sd_directory_next(dir, &info)).status == MT_SD_OK && info.name[0]) {
                if (!mt_iec_dos_recovery_name(info.name) && wildcard(part, info.name)) {
                    memcpy(part, info.name, strlen(info.name) + 1);
                    found = true;
                    break;
                }
            }
            bool closed = (virtual_dir ? c64_uci_image_close(&image_dir) :
                                        mt_sd_directory_close(dir)).status == MT_SD_OK;
            if (!closed) {
                path_directory = dir;
                path_image_directory = image_dir;
                c64_uci_environment.fault = true;
            }
            if (!found || !closed)
                return false;
        }
        size_t used = strlen(out), add = strlen(part);
        if (used + add + 2 > C64_UCI_PATH)
            return false;
        out[used] = '/';
        memcpy(out + used + 1, part, add + 1);
    }
    return mt_sd_file_canonical_path(out, out, C64_UCI_PATH).status == MT_SD_OK;
}
const char *c64_uci_sd_status(mt_sd_result_t r)
{
    if (r.status == MT_SD_OK)
        return "00,OK";
    if (r.status == MT_SD_FILE_BUSY)
        return "FILE IS IN USE";
    if (r.status == MT_SD_FILE_LIMIT)
        return "OUT OF MEMORY";
    if (r.status == MT_SD_FILE_EXISTS)
        return "FILE EXISTS";
    switch (r.backend_status) {
    case 1:
        return "DISK ERROR";
    case 2:
        return "INTERNAL ERROR";
    case 3:
        return "DEVICE NOT READY";
    case 4:
        return "FILE DOESN'T EXIST";
    case 5:
        return "PATH DOESN'T EXIST";
    case 6:
        return "INVALID NAME";
    case 7:
        return "ACCESS DENIED";
    case 8:
        return "FILE EXISTS";
    case 9:
        return "INVALID OBJECT";
    case 10:
        return "WRITE PROTECTED";
    case 11:
        return "INVALID DRIVE";
    case 12:
        return "NOT ENABLED";
    case 13:
        return "NO FILESYSTEM";
    case 14:
        return "MAKE FILESYSTEM ABORTED";
    case 15:
        return "I/O TIMEOUT";
    case 16:
        return "FILE IS IN USE";
    case 17:
        return "OUT OF MEMORY";
    case 18:
        return "FILE IS IN USE";
    default:
        return "UNKNOWN ERROR";
    }
}
/* /Temp is a dedicated volatile directory. One entry per foreground pass;
 * close every DIR before returning so USB/core handoff has no hidden owner. */
static int clean_temporary(void)
{
    if (temporary_directory) {
        if (mt_sd_directory_close(temporary_directory).status != MT_SD_OK)
            return -1;
        temporary_directory = NULL;
    }
    mt_sd_directory_t *directory = NULL;
    mt_sd_info_t info;
    mt_sd_result_t s = mt_sd_directory_open(temporary_cursor, &directory);
    if (s.status != MT_SD_OK) {
        if ((s.backend_status == 4 || s.backend_status == 5) &&
            !strcmp(temporary_cursor, c64_uci_environment.temporary)) {
            temporary_pending = false;
            return 0;
        }
        return -1;
    }
    for (unsigned i = 0; i < 3; i++) {
        s = mt_sd_directory_next(directory, &info);
        if (s.status != MT_SD_OK || !info.name[0] ||
            (strcmp(info.name, ".") && strcmp(info.name, "..")))
            break;
    }
    mt_sd_result_t closed = mt_sd_directory_close(directory);
    if (closed.status != MT_SD_OK)
        temporary_directory = directory;
    if (s.status != MT_SD_OK || closed.status != MT_SD_OK)
        return -1;
    if (!info.name[0]) {
        if (!strcmp(temporary_cursor, c64_uci_environment.temporary)) {
            temporary_pending = false;
            return 0;
        }
        if (mt_sd_file_remove(temporary_cursor).status != MT_SD_OK)
            return -1;
        char *end = strrchr(temporary_cursor, '/');
        if (!end)
            return -1;
        *end = 0;
        return 1;
    }
    size_t used = strlen(temporary_cursor), length = strlen(info.name);
    if (used + length + 2 > C64_UCI_PATH || strchr(info.name, '/') || strchr(info.name, '\\') ||
        !strcmp(info.name, ".") || !strcmp(info.name, ".."))
        return -1;
    char path[C64_UCI_PATH];
    memcpy(path, temporary_cursor, used);
    path[used] = '/';
    memcpy(path + used + 1, info.name, length + 1);
    if (c64_uci_protected(path, true))
        return -1;
    if (info.attributes & 16)
        memcpy(temporary_cursor, path, used + length + 2);
    else if (mt_sd_file_remove(path).status != MT_SD_OK)
        return -1;
    return 1;
}
int c64_uci_service_hold_begin(void)
{
    if (!c64_uci_environment.available || c64_uci_environment.fault || reload_only)
        return -1;
    if (!hold_active) {
        uint32_t c = c64_uci_read(MT_REG_C64_UCI_CONTROL);
        hold_previous = (c & MT_C64_UCI_CONTROL_HOLD) != 0;
        c64_uci_write(MT_REG_C64_UCI_CONTROL, c | MT_C64_UCI_CONTROL_HOLD);
        hold_start = c64_uci_time();
        hold_active = true;
    }
    if ((c64_uci_read(MT_REG_C64_CPU_STATUS) & MT_C64_CPU_STATUS_PAUSE) &&
        (c64_uci_read(MT_REG_C64_REU_HOST) & MT_C64_REU_HOST_IDLE))
        return 0;
    if (c64_uci_time() - hold_start > 1000000) {
        c64_uci_service_hold_end();
        return -1;
    }
    return 1;
}
void c64_uci_service_hold_end(void)
{
    if (hold_active && !reload_only && !reboot_hold && !quiesce_active && !ram_active && !fence_active &&
        !c64_uci_environment.fault) {
        uint32_t c = c64_uci_read(MT_REG_C64_UCI_CONTROL);
        if (!hold_previous)
            c64_uci_write(MT_REG_C64_UCI_CONTROL, c & ~MT_C64_UCI_CONTROL_HOLD);
        hold_active = false;
    }
}
void c64_uci_service_hold_fault(void)
{
    hardware_fault = true;
    hardware_fault_kind = FAULT_UNKNOWN;
    c64_uci_environment.fault = true;
    if (c64_uci_environment.available) {
        c64_uci_write(MT_REG_C64_UCI_CONTROL,
                      c64_uci_read(MT_REG_C64_UCI_CONTROL) | MT_C64_UCI_CONTROL_HOLD);
        hold_active = true;
    }
}
int c64_uci_reu_fence(void)
{
    if (reload_only)
        return 0;
    if (c64_uci_environment.fault)
        return -1;
    if (!fence_active) {
        c64_uci_reu_drain();
        uint32_t h = c64_uci_read(MT_REG_C64_REU_HOST);
        fence_token = (uint8_t)((h + 1) & 15);
        c64_uci_write(MT_REG_C64_REU_HOST, (h & MT_C64_REU_HOST_UA_HOLD) | fence_token);
        fence_start = c64_uci_time();
        fence_active = true;
        return 1;
    }
    if (((c64_uci_read(MT_REG_C64_REU_HOST) >> 8) & 15) == fence_token) {
        fence_active = false;
        return 0;
    }
    if (c64_uci_time() - fence_start > 1000000) {
        c64_uci_service_hold_fault();
        hardware_fault_kind = FAULT_FENCE;
        return -1;
    }
    return 1;
}
int c64_uci_ram_read(uint16_t address, uint8_t *data, size_t bytes)
{
    if (!bytes || bytes > 896 || !hold_active || !data)
        return -1;
    (void)c64_uci_read(MT_REG_C64_UCI_STATUS);
    uint32_t s = c64_uci_read(MT_REG_C64_UCI_STATUS2);
    if (!ram_active) {
        ram_token = (uint8_t)(((s >> 24) + 1) & 15);
        ram_start = c64_uci_time();
        ram_active = true;
        ram_address = address;
        ram_length = bytes;
        c64_uci_write(MT_REG_C64_UCI_RAM_READ, address | ((uint32_t)(bytes - 1) << 16));
        return 1;
    }
    if (address != ram_address || bytes != ram_length)
        return -1;
    if (((s >> 24) & 15) != ram_token) {
        if (c64_uci_time() - ram_start > 100000) {
            c64_uci_service_hold_fault();
            hardware_fault_kind = FAULT_RAM;
            return -1;
        }
        return 1;
    }
    for (size_t i = 0; i < bytes; i += 4) {
        uint32_t w = c64_uci_read(MT_C64_UCI_BUFFER_OFFSET + MT_C64_UCI_REPLY_OFFSET + (unsigned)i);
        for (size_t j = 0; j < 4 && i + j < bytes; j++)
            data[i + j] = (uint8_t)(w >> (8 * j));
    }
    ram_active = false;
    return 0;
}
int c64_uci_ram_write(uint16_t address, const uint8_t *data, size_t bytes)
{
    uint8_t stream[258];
    if (!hold_active || bytes > 256 || !bytes)
        return -1;
    stream[0] = (uint8_t)address;
    stream[1] = (uint8_t)(address >> 8);
    memcpy(stream + 2, data, bytes);
    if (mt_core_link_stream(c64_uci_environment.link, MT_C64_UCI_LOAD_KIND, stream, bytes + 2,
                            100000, NULL, NULL) != MT_LINK_OK) {
        c64_uci_service_hold_fault();
        return -1;
    }
    return 0;
}
int c64_uci_ram_cancel(void)
{
    if (reload_only)
        return 0;
    if (c64_uci_environment.fault)
        return -1;
    if (!ram_active)
        return 0;
    (void)c64_uci_read(MT_REG_C64_UCI_STATUS);
    if (((c64_uci_read(MT_REG_C64_UCI_STATUS2) >> 24) & 15) != ram_token) {
        if (c64_uci_time() - ram_start > 100000) {
            c64_uci_service_hold_fault();
            hardware_fault_kind = FAULT_RAM;
            return -1;
        }
        return 1;
    }
    ram_active = false;
    return 0;
}
bool c64_uci_post(c64_uci_action_t action, const char *path, unsigned device)
{
    if (pending_action != C64_UCI_ACTION_NONE)
        return false;
    if (!copy_path(action_path, path ? path : ""))
        return false;
    pending_action = action;
    action_device = device;
    action_result = 1;
    return true;
}
bool c64_uci_service_action(c64_uci_action_t *action, char *path, size_t cap, unsigned *device)
{
    if (pending_action == C64_UCI_ACTION_NONE || action_result != 1 || !action || !path ||
        cap <= strlen(action_path))
        return false;
    *action = pending_action;
    *device = action_device;
    memcpy(path, action_path, strlen(action_path) + 1);
    action_result = 2;
    if (pending_action == C64_UCI_ACTION_REBOOT) reboot_hold = true;
    return true;
}
void c64_uci_service_action_done(int status)
{
    if (status && reboot_hold) {
        reboot_hold = false;
        c64_uci_service_hold_end();
    }
    if (pending_action != C64_UCI_ACTION_NONE) {
        action_result = status ? -1 : 0;
        pending_action = C64_UCI_ACTION_NONE;
    }
}
int c64_uci_action_result(void)
{
    return action_result > 0 ? 1 : action_result;
}
bool c64_uci_cancel_action(void)
{
    if (action_result == 2 && pending_action != C64_UCI_ACTION_FREEZE)
        return false;
    pending_action = C64_UCI_ACTION_NONE;
    action_result = 0;
    return true;
}
int c64_uci_service_configure_root(const char *root)
{
    if (!root || strlen(root) > C64_UCI_PATH - 16 || !strchr(root, ':'))
        return -1;
    char canonical[C64_UCI_PATH];
    if (mt_sd_file_canonical_path(root, canonical, sizeof canonical).status != MT_SD_OK ||
        strlen(canonical) > C64_UCI_PATH - 16)
        return -1;
    copy_path(c64_uci_environment.root, canonical);
    size_t n = strlen(c64_uci_environment.root);
    while (n > 3 && c64_uci_environment.root[n - 1] == '/')
        c64_uci_environment.root[--n] = 0;
    copy_path(c64_uci_environment.home, c64_uci_environment.root);
    copy_path(c64_uci_environment.temporary, c64_uci_environment.root);
    char *end = strrchr(c64_uci_environment.temporary, '/');
    if (!end)
        return -1;
    strcpy(end, "/uci_temp");
    return 0;
}
int c64_uci_service_init(mt_core_link_t *link, const char *root,
                         const c64_uci_callbacks_t *callbacks)
{
    memset(&c64_uci_environment, 0, sizeof c64_uci_environment);
    hold_active = fence_active = ram_active = reboot_hold = false;
    hardware_fault = temporary_failed = false;
    temporary_directory = path_directory = NULL;
    path_image_directory = NULL;
    reload_only = false;
    hardware_fault_kind = FAULT_UNKNOWN;
    quiesce_active = quiesce_op_pending = reset_pending = false;
    pending_action = C64_UCI_ACTION_NONE;
    action_result = 0;
    if (!link || !link->read32 || !link->write32 || !callbacks || !callbacks->microseconds)
        return -1;
    c64_uci_environment.link = link;
    c64_uci_environment.callbacks = *callbacks;
    if (c64_uci_service_configure_root(root))
        return -1;
    uint32_t abi = c64_uci_read(MT_REG_ABI_VERSION);
    c64_uci_environment.available =
        (abi >> 16) == MT_ABI_MAJOR && (uint16_t)abi >= 26 &&
        c64_uci_read(MT_REG_CORE_ID) == MT_C64_CORE_ID &&
        (c64_uci_read(MT_REG_C64_EXPANSION_CAPS) & MT_C64_EXPANSION_CAPS_UCI) != 0;
    temporary_pending = c64_uci_environment.available;
    copy_path(temporary_cursor, c64_uci_environment.temporary);
    mt_uci_engine_init(&engine, link);
    mt_uci_target_t t = c64_uci_dos_target(0);
    mt_uci_engine_target(&engine, 1, &t);
    t = c64_uci_dos_target(1);
    mt_uci_engine_target(&engine, 2, &t);
    t = mt_net_service_network_target();
    mt_uci_engine_target(&engine, 3, &t);
    t = c64_uci_control_target();
    mt_uci_engine_target(&engine, 4, &t);
    t = c64_uci_softiec_target();
    mt_uci_engine_target(&engine, 5, &t);
    t = mt_net_service_http_target();
    mt_uci_engine_target(&engine, 6, &t);
    c64_uci_service_refresh_control();
    return 0;
}
static bool uci_enabled(void)
{
    return (c64_uci_read(MT_REG_C64_EXPANSION_CONFIG) & MT_C64_EXPANSION_UCI_ENABLE) ||
           (c64_uci_read(MT_REG_C64_UCI_STATUS) & MT_C64_UCI_STATUS_ENABLED);
}
void c64_uci_service_refresh_control(void)
{
    if (!c64_uci_environment.available)
        return;
    uint32_t caps = c64_uci_read(MT_REG_C64_EXPANSION_CAPS);
    uint32_t desired = engine.targets[5].begin && c64_iec_service_active()
                           ? c64_iec_service_device_number() : MT_C64_UCI_CONTROL_DEFAULT;
    if (uci_enabled()) {
        if (caps & MT_C64_EXPANSION_CAPS_UCI_RAM_DMA)
            desired |= MT_C64_UCI_CONTROL_FREEZE;
        if (caps & MT_C64_EXPANSION_CAPS_UCI_C64_IRQ)
            desired |= MT_C64_UCI_CONTROL_IRQ;
    }
    uint32_t old = c64_uci_read(MT_REG_C64_UCI_CONTROL);
    desired |= old & ~(MT_C64_UCI_CONTROL_DEFAULT | MT_C64_UCI_CONTROL_FREEZE |
                       MT_C64_UCI_CONTROL_IRQ);
    if (desired != old)
        c64_uci_write(MT_REG_C64_UCI_CONTROL, desired);
}
void c64_uci_service_set_home(const char *path)
{
    char canonical[C64_UCI_PATH];
    if (path && mt_sd_file_canonical_path(path, canonical, sizeof canonical).status == MT_SD_OK &&
        inside(canonical, c64_uci_environment.root))
        copy_path(c64_uci_environment.home, canonical);
}
void c64_uci_service_set_reu_image(const char *path)
{
    char canonical[C64_UCI_PATH];
    if (path && mt_sd_file_canonical_path(path, canonical, sizeof canonical).status == MT_SD_OK &&
        inside(canonical, c64_uci_environment.root))
        copy_path(c64_uci_environment.reu_image, canonical);
}
bool c64_uci_service_available(void)
{
    return c64_uci_environment.available;
}
unsigned c64_uci_service_drive_address(void)
{
    if (!c64_uci_environment.available ||
        (c64_uci_read(MT_REG_ABI_VERSION) & 0xffff) < MT_C64_UCI315_MIN_ABI_MINOR ||
        !(c64_uci_read(MT_REG_C64_EXPANSION_CAPS) & MT_C64_EXPANSION_CAPS_DRIVE_POWER) ||
        (c64_uci_read(MT_REG_C64_PERIPHERAL_CONTROL) & MT_C64_PERIPHERAL_DRIVE_A_OFF))
        return 8;
    const uint32_t status = c64_uci_read(MT_REG_C64_DRIVE_STATUS);
    return status & 0x80000000U ? status & 31 : 8;
}
bool c64_uci_service_active(void)
{
    return c64_uci_environment.available &&
           (engine.active || quiesce_active || reset_pending ||
            (temporary_pending && uci_enabled()));
}
int c64_uci_service_poll(unsigned budget)
{
    if (!c64_uci_environment.available)
        return 0;
    if (reload_only)
        return -1;
    c64_uci_service_refresh_control();
    if (c64_uci_environment.fault)
        return -1;
    if (quiesce_active)
        return c64_uci_service_quiesce();
    if (temporary_pending && uci_enabled()) {
        if (clean_temporary() < 0) {
            temporary_pending = false;
            temporary_failed = true;
            /* A failed deletion owns no hardware. Preserve the evidence and
             * allow ordinary service; only a still-open DIR blocks handoff. */
            if (temporary_directory) {
                c64_uci_environment.fault = true;
                return -1;
            }
        }
        return 0;
    }
    for (unsigned i = 0; i < budget; i++) {
        mt_uci_engine_poll(&engine);
        if (engine.faulted || c64_uci_environment.fault) {
            c64_uci_environment.fault = true;
            return -1;
        }
    }
    return 0;
}
static void finish_reset(void)
{
    pending_action = C64_UCI_ACTION_NONE;
    action_result = 0;
    ram_active = false;
    reset_pending = false;
    if (c64_uci_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) {
        reboot_hold = false;
        c64_uci_service_hold_end();
    }
    mt_net_service_guest_reset();
}
int c64_uci_service_quiesce(void)
{
    if (!c64_uci_environment.available)
        return 0;
    if (reload_only)
        return -1;
    if (c64_uci_environment.fault)
        return -1;
    if (!quiesce_active) {
        quiesce_active = true;
        quiesce_op_pending = false;
        quiesce_start = c64_uci_time();
    }
    if (c64_uci_time() - quiesce_start > 1000000) {
        c64_uci_service_hold_fault();
        hardware_fault_kind = FAULT_NATIVE;
        return -1;
    }
    if (!mt_uci_engine_quiesce(&engine)) {
        if (engine.faulted) {
            c64_uci_environment.fault = true;
            return -1;
        }
        return 1;
    }
    if (c64_uci_environment.fault)
        return -1;
    int pending = c64_uci_ram_cancel();
    if (pending)
        return pending;
    /* Target cleanup may finish while the native protocol still owns a BUSY
     * command or a MORE reply. Reset only that transaction after all memory
     * fences and resource cleanup complete; otherwise the next poll could
     * execute the old command again or strand the guest's NEXT request. */
    uint32_t status = c64_uci_read(MT_REG_C64_UCI_STATUS);
    uint32_t status2 = c64_uci_read(MT_REG_C64_UCI_STATUS2);
    if (quiesce_op_pending) {
        if (((status2 >> 19) & 3) == quiesce_ack)
            return 1;
        quiesce_op_pending = false;
        /* A guest PUSH/reset can obsolete the tagged operation in transit.
         * Retry from a fresh snapshot instead of treating a dropped op as
         * completion. The deadline above also bounds a continuously busy CPU. */
        if (status2 & (1U << 21))
            return 1;
    }
    if (status & MT_C64_UCI_STATUS_HANDSHAKE_BUSY)
        return 1;
    if ((status & MT_C64_UCI_STATUS_VALID) &&
        ((status & (0x3ffefU | MT_C64_UCI_STATUS_IDLE_ABORT)) ||
         (status2 & MT_C64_UCI_STATUS2_CMD_BUSY))) {
        /* RESET retains the protocol error flag; idle ABORT has its own ACK. */
        const uint32_t code = status & MT_C64_UCI_STATUS_IDLE_ABORT ?
                                  MT_C64_UCI_OP_IDLE_ABORT_DONE : MT_C64_UCI_OP_RESET;
        const uint32_t op = code | (((status >> 18) & 15U) << 25) |
                            (((status >> 22) & 7U) << 29);
        quiesce_ack = (uint8_t)((status2 >> 19) & 3);
        quiesce_op_pending = true;
        c64_uci_write(MT_REG_C64_UCI_HANDSHAKE, op);
        return 1;
    }
    quiesce_active = false;
    c64_uci_service_hold_end();
    if (reset_pending)
        finish_reset();
    return 0;
}
bool c64_uci_service_cleanup_warning(void)
{
    return temporary_failed;
}
int c64_uci_service_retry(void)
{
    if (!c64_uci_environment.available)
        return 0;
    /* Unknown DMA completion cannot be repaired by forgetting ownership. */
    if (reload_only)
        return -1;
    if (hardware_fault) {
        bool completed = hardware_fault_kind == FAULT_FENCE && fence_active &&
            ((c64_uci_read(MT_REG_C64_REU_HOST) >> 8) & 15U) == fence_token;
        if (completed) fence_active = false;
        if (hardware_fault_kind == FAULT_RAM && ram_active &&
            ((c64_uci_read(MT_REG_C64_UCI_STATUS2) >> 24) & 15U) == ram_token) {
            ram_active = false;
            completed = true;
        }
        if (hardware_fault_kind == FAULT_NATIVE && quiesce_op_pending &&
            !(c64_uci_read(MT_REG_C64_UCI_STATUS) & MT_C64_UCI_STATUS_HANDSHAKE_BUSY) &&
            ((c64_uci_read(MT_REG_C64_UCI_STATUS2) >> 19) & 3U) != quiesce_ack) {
            quiesce_op_pending = false;
            completed = true;
        }
        if (!completed) return -1;
        hardware_fault = false;
    }
    if (path_image_directory && c64_uci_image_close(&path_image_directory).status != MT_SD_OK)
        return -1;
    if (c64_uci_image_retry() != 0) return -1;
    if (path_directory) {
        if (mt_sd_directory_close(path_directory).status != MT_SD_OK)
            return -1;
        path_directory = NULL;
    }
    if (temporary_directory) {
        if (mt_sd_directory_close(temporary_directory).status != MT_SD_OK)
            return -1;
        temporary_directory = NULL;
    }
    if (c64_uci_environment.fault || engine.faulted) {
        c64_uci_environment.fault = engine.faulted = false;
        engine.quiesced = false;
        quiesce_active = false;
    }
    int result = c64_uci_service_quiesce();
    if (!result && temporary_failed) {
        temporary_failed = false;
        temporary_pending = true;
        copy_path(temporary_cursor, c64_uci_environment.temporary);
    }
    return result;
}
bool c64_uci_service_reload_required(void)
{
    return hardware_fault || reload_only;
}
int c64_uci_service_prepare_reload(void)
{
    if (!c64_uci_environment.available)
        return 0;
    /* Only explicit PL replacement may abandon native tokens. The CPU must
     * stay in reset and every external memory master must drain first. */
    if (!(c64_uci_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) ||
        !(c64_uci_read(MT_REG_VIDEO_FRAME_STATUS) & MT_VIDEO_FRAME_STATUS_QUIESCENT) ||
        !(c64_uci_read(MT_REG_C64_REU_HOST) & MT_C64_REU_HOST_IDLE))
        return 1;
    if ((c64_uci_read(MT_REG_C64_EXPANSION_CAPS) & MT_C64_EXPANSION_CAPS_CART_ERASE) &&
        (c64_uci_read(MT_REG_C64_CART_ERASE_STATUS) & MT_C64_CART_ERASE_BUSY))
        return 1;
    reload_only = true;
    c64_uci_write(MT_REG_C64_UCI_CONTROL,
                  c64_uci_read(MT_REG_C64_UCI_CONTROL) | MT_C64_UCI_CONTROL_HOLD);
    hold_active = true;
    ram_active = fence_active = quiesce_op_pending = false;
    hardware_fault = c64_uci_environment.fault = engine.faulted = false;
    reboot_hold = false;
    c64_uci_control_prepare_reload();
    quiesce_active = false;
    pending_action = C64_UCI_ACTION_NONE;
    action_result = 0;
    if (path_image_directory && c64_uci_image_close(&path_image_directory).status != MT_SD_OK)
        return -1;
    if (c64_uci_image_retry() != 0) return -1;
    if (path_directory) {
        if (mt_sd_directory_close(path_directory).status != MT_SD_OK) return -1;
        path_directory = NULL;
    }
    if (temporary_directory) {
        if (mt_sd_directory_close(temporary_directory).status != MT_SD_OK) return -1;
        temporary_directory = NULL;
    }
    engine.quiesced = false;
    if (!mt_uci_engine_quiesce(&engine)) return engine.faulted ? -1 : 1;
    return c64_uci_environment.fault ? -1 : 0;
}
void c64_uci_service_reset(void)
{
    reset_pending = true;
    if (c64_uci_service_quiesce() == 0 && reset_pending)
        finish_reset();
}
