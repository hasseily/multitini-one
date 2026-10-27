/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_title_service.h"
#include "multitini_media_browser.h"
#include "sd_storage.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char media_root[256], session[256], pending_name[256], notice[64];
static mt_c64_title_origin_t session_origin, pending_origin;
static mt_c64_title_values_t stored;
static mt_c64_capabilities_t caps;
static mt_c64_title_overlay_t overlay, pending;
static mt_c64_title_report_t last_report;
static c64_title_callbacks_t callbacks;
static mt_c64_launch_hook_t launch_hook;
static bool automatic, ntsc, allowed, committed, pending_event;
static uint8_t read_buffer[MT_C64_CFG_BYTES], write_buffer[MT_C64_CFG_BYTES];

/* New images own per-cartridge inhibition in hardware. Keep the requested
 * registers intact so detach restores them without rewriting saved settings. */
static void effective(mt_c64_title_values_t *requested, mt_c64_title_values_t *applied,
                      const mt_c64_title_values_t *base,
                      const mt_c64_title_overlay_t *title, bool cartridge)
{
    mt_c64_title_effective(requested, applied, base, title,
        cartridge && !(caps.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST));
}

static void status(const char *s)
{
    (void)snprintf(notice, sizeof(notice), "%s", s);
}
static bool prefix(const char *path, const char *root)
{
    size_t n = strlen(root);
    for (size_t i = 0; i < n; ++i)
        if (!path[i] || tolower((unsigned char)path[i]) != tolower((unsigned char)root[i]))
            return false;
    return path[n] == '/';
}
static bool resolve(const char *path, bool uci, char out[512])
{
    if (!path || !*path)
        return false;
    if (!uci)
        return mt_media_browser_join(media_root, path, out, 512);
    char root[256];
    (void)snprintf(root, sizeof(root), "%s", media_root);
    const char *relative = NULL;
    if (prefix(path, root))
        relative = path + strlen(root) + 1;
    else {
        char *slash = strrchr(root, '/');
        if (!slash)
            return false;
        *slash = 0;
        size_t n = strlen(root);
        if (n + 10 >= sizeof(root))
            return false;
        memcpy(root + n, "/uci_temp", 10);
        if (prefix(path, root))
            relative = path + strlen(root) + 1;
    }
    return relative && mt_media_browser_join(root, relative, out, 512);
}
static bool read_config(const char *path, size_t *size, bool *found)
{
    char journal[520];
    int n = snprintf(journal, sizeof(journal), "%s.mtj", path);
    if (n < 0 || (size_t)n >= sizeof(journal)) {
        status("Configuration path too long");
        return false;
    }
    if (mt_sd_file_path_busy(path, true) || mt_sd_file_path_busy(journal, true)) {
        status("Configuration is open on drive 9 or UCI");
        return false;
    }
    mt_sd_result_t r =
        mt_sd_storage_journal_recover(path, journal, read_buffer, sizeof(read_buffer));
    if (r.status != MT_SD_OK) {
        status("Configuration recovery failed; file preserved");
        return false;
    }
    r = mt_sd_storage_read_file_optional_max(path, read_buffer, sizeof(read_buffer), found);
    if (r.status != MT_SD_OK) {
        status(r.status == MT_SD_TOO_LARGE ? "Configuration exceeds 16 KiB"
                                           : "Configuration could not be read");
        return false;
    }
    *size = r.size;
    return true;
}
static void summary(const mt_c64_title_report_t *r)
{
    if ((pending.owned & MT_TITLE_SCPU) && (pending.values.flags & 8))
        status("SuperCPU Detect on; Revert before GEOS");
    else
        (void)snprintf(notice, sizeof(notice),
                       "Title settings: %u applied, %u adjusted, %u skipped", r->applied,
                       r->adjusted, r->skipped);
}
int c64_title_service_init(const char *root, const c64_title_callbacks_t *cb)
{
    mt_media_browser_t browser;
    if (!cb || !cb->apply || !mt_media_browser_init(&browser, root))
        return -1;
    callbacks = *cb;
    (void)snprintf(media_root, sizeof(media_root), "%s", browser.root);
    c64_title_service_clear();
    mt_c64_title_defaults(&stored);
    memset(&caps, 0, sizeof(caps));
    return 0;
}
void c64_title_service_refresh(const mt_c64_title_values_t *base,
                               const mt_c64_capabilities_t *features, bool is_ntsc, bool auto_files)
{
    stored = *base;
    stored.detail = mt_c64_turbo_detail(stored.detail);
    caps = *features;
    ntsc = is_ntsc;
    automatic = auto_files;
}
void c64_title_service_effective(mt_c64_title_values_t *requested, mt_c64_title_values_t *applied,
                                 bool cartridge)
{
    effective(requested, applied, &stored, &overlay, cartridge);
}
static int commit(void *unused)
{
    (void)unused;
    mt_c64_title_values_t requested, applied;
    effective(&requested, &applied, &stored, &pending, pending_origin == MT_TITLE_CRT);
    if (callbacks.apply(callbacks.context, &applied, false, pending_event) != 0)
        return -1;
    overlay = pending;
    committed = true;
    return 0;
}
const mt_c64_launch_hook_t *
c64_title_service_prepare(const char *relative, mt_c64_title_origin_t origin, bool apply_event)
{
    memset(&pending, 0, sizeof(pending));
    memset(&last_report, 0, sizeof(last_report));
    pending.values = stored;
    pending.origin = origin;
    pending_origin = origin;
    pending_event = apply_event;
    committed = false;
    allowed = true;
    notice[0] = 0;
    pending_name[0] = 0;
    if (!relative || strlen(relative) >= sizeof(pending_name)) {
        allowed = false;
        status("Title path too long");
        return NULL;
    }
    (void)snprintf(pending_name, sizeof(pending_name), "%s", relative);
    if (automatic) {
        char sidecar[256], absolute[512];
        size_t size = 0;
        bool found = false;
        if (mt_c64_title_sidecar(sidecar, sizeof(sidecar), relative, ".cfg") &&
            resolve(sidecar, false, absolute)) {
            bool read_ok = read_config(absolute, &size, &found);
            if (read_ok && !found && origin == MT_TITLE_PRG) {
                (void)mt_c64_title_sidecar(sidecar, sizeof(sidecar), relative, ".usr");
                if (resolve(sidecar, false, absolute))
                    read_ok = read_config(absolute, &size, &found);
            }
            if (read_ok && found) {
                mt_c64_title_parse_context_t context = {
                    &caps, &stored, ntsc, origin == MT_TITLE_CRT, false, origin};
                bool ok = mt_c64_title_parse(read_buffer, size, &context, &pending, &last_report);
                if (!ok || (!last_report.known_sections && strstr(sidecar, ".usr"))) {
                    memset(&pending, 0, sizeof(pending));
                    pending.values = stored;
                    status("Configuration not used: invalid text");
                } else {
                    (void)snprintf(pending.source, sizeof(pending.source), "%s", sidecar);
                    summary(&last_report);
                }
            }
        }
    }
    mt_c64_title_values_t next, applied, old, old_applied;
    effective(&next, &applied, &stored, &pending, origin == MT_TITLE_CRT);
    effective(&old, &old_applied, &stored, &overlay, false);
    if (origin == MT_TITLE_CRT && next.reu &&
        !(caps.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST)) {
        allowed = false;
        status("Turn REU off, or disable it in the title .cfg");
        return NULL;
    }
    const char *conflict = mt_c64_title_io_conflict(&applied, &caps, origin == MT_TITLE_CRT);
    if (conflict) {
        allowed = false;
        status(conflict);
        return NULL;
    }
    launch_hook = (mt_c64_launch_hook_t){next.reu, true, commit, NULL};
    if (!pending.owned && !overlay.owned && !c64_content_service_crt_attached())
        return NULL;
    return &launch_hook;
}
bool c64_title_service_launch_allowed(void)
{
    return allowed;
}
const mt_c64_launch_hook_t *c64_title_service_prepare_detach(void)
{
    memset(&pending, 0, sizeof(pending));
    pending.values = stored;
    pending_origin = MT_TITLE_NONE;
    pending_name[0] = 0;
    pending_event = true;
    committed = false;
    allowed = true;
    launch_hook = (mt_c64_launch_hook_t){stored.reu, true, commit, NULL};
    return &launch_hook;
}
void c64_title_service_finish(mt_c64_content_service_status_t result)
{
    if (result == MT_C64_CONTENT_SERVICE_OK) {
        overlay = pending;
        (void)snprintf(session, sizeof(session), "%s", pending_name);
        session_origin = pending_origin;
    } else if (committed) {
        mt_c64_title_values_t requested, applied;
        effective(&requested, &applied, &stored, NULL,
                               c64_content_service_crt_attached());
        if (callbacks.apply(callbacks.context, &applied, true, true) == 0) {
            memset(&overlay, 0, sizeof(overlay));
            session[0] = 0;
            session_origin = MT_TITLE_NONE;
            status("Load failed; saved settings restored");
        } else
            status("Load failed; title settings could not be restored");
    }
    committed = false;
}
int c64_title_service_apply(const char *path, bool uci, char *report, size_t capacity)
{
    char absolute[512];
    size_t size = 0;
    bool found = false;
    int result = 0;
    memset(&last_report, 0, sizeof(last_report));
    status("Configuration not found or outside media root");
    if (!resolve(path, uci, absolute) || !read_config(absolute, &size, &found) || !found) {
        result = 88;
        goto done;
    }
    mt_c64_title_values_t base, applied;
    c64_title_service_effective(&base, &applied, c64_content_service_crt_attached());
    mt_c64_title_parse_context_t context = {&caps, &base,
                                            ntsc,  c64_content_service_crt_attached(),
                                            true,  uci ? MT_TITLE_UCI_FILE : MT_TITLE_MANUAL};
    if (!mt_c64_title_parse(read_buffer, size, &context, &pending, &last_report)) {
        result = 89;
        status("Configuration has errors; nothing applied");
        goto done;
    }
    (void)snprintf(pending.source, sizeof(pending.source), "%s", uci ? "UCI configuration" : path);
    mt_c64_title_overlay_t merged = overlay;
    mt_c64_title_merge(&merged, &pending);
    effective(&base, &applied, &stored, &merged, c64_content_service_crt_attached());
    const char *conflict =
        mt_c64_title_io_conflict(&applied, &caps, c64_content_service_crt_attached());
    if (conflict) {
        result = 89;
        status(conflict);
        goto done;
    }
    if (callbacks.apply(callbacks.context, &applied, true, true) != 0) {
        result = 87;
        status("Configuration could not reach a safe apply point");
        goto done;
    }
    overlay = merged;
    if (!uci && !session[0]) {
        (void)snprintf(session, sizeof(session), "%s", path);
        session_origin = MT_TITLE_MANUAL;
    }
    summary(&last_report);
done:
    if (report && capacity) {
        /* UCI LOAD_CONFIG returns the parser log, empty on clean success.
         * Keep the GUI summary out of the wire reply; errors without parser
         * diagnostics still need an actionable explanation. */
        size_t used = 0;
        report[0] = 0;
        if (uci && capacity > 512)
            capacity = 512;
        if (!uci || (result && (result != 89 || !last_report.entries))) {
            used = (size_t)snprintf(report, capacity, "%s\n", notice);
            if (used >= capacity)
                used = capacity - 1;
        }
        for (unsigned i = 0; i < last_report.entries && used < capacity - 1; ++i) {
            int n = snprintf(report + used, capacity - used, "%s\n", last_report.message[i]);
            if (n < 0)
                break;
            used += (size_t)n < capacity - used ? (size_t)n : capacity - used - 1;
        }
    }
    return result;
}
int c64_title_service_test(const char *relative, char *report, size_t capacity)
{
    char path[512], journal[520];
    if (!report || !capacity)
        return -1;
    report[0] = 0;
    if (!resolve(relative, false, path)) {
        (void)snprintf(report, capacity, "Configuration path is outside media root\n");
        return -1;
    }
    int n = snprintf(journal, sizeof journal, "%s.mtj", path);
    if (n < 0 || (size_t)n >= sizeof journal || mt_sd_file_path_busy(path, true) ||
        mt_sd_file_path_busy(journal, true)) {
        (void)snprintf(report, capacity, "Configuration is busy or path is too long\n");
        return -1;
    }
    bool found = false;
    mt_sd_result_t r =
        mt_sd_storage_read_file_optional_max(journal, read_buffer, sizeof read_buffer, &found);
    if (r.status != MT_SD_OK || found) {
        (void)snprintf(report, capacity,
                       "Pending recovery or unreadable journal; dry run did not modify it\n");
        return -1;
    }
    r = mt_sd_storage_read_file_optional_max(path, read_buffer, sizeof read_buffer, &found);
    if (r.status != MT_SD_OK || !found) {
        (void)snprintf(report, capacity, "Configuration missing or unreadable\n");
        return -1;
    }
    mt_c64_title_values_t base, applied;
    c64_title_service_effective(&base, &applied, c64_content_service_crt_attached());
    const mt_c64_title_parse_context_t context = {
        &caps, &base, ntsc, c64_content_service_crt_attached(), true, MT_TITLE_MANUAL};
    mt_c64_title_overlay_t candidate;
    mt_c64_title_report_t parsed;
    bool ok = mt_c64_title_parse(read_buffer, r.size, &context, &candidate, &parsed);
    size_t used = (size_t)snprintf(
        report, capacity, "Dry run: %u applied, %u adjusted, %u skipped, %u errors; owned=%08x\n",
        parsed.applied, parsed.adjusted, parsed.skipped, parsed.errors, (unsigned)candidate.owned);
    if (used >= capacity)
        used = capacity - 1;
    for (unsigned i = 0; i < parsed.entries && used < capacity - 1; ++i) {
        n = snprintf(report + used, capacity - used, "%s\n", parsed.message[i]);
        if (n < 0)
            break;
        used += (size_t)n < capacity - used ? (size_t)n : capacity - used - 1;
    }
    return ok ? 0 : -1;
}
int c64_title_service_revert(void)
{
    mt_c64_title_values_t requested, applied;
    effective(&requested, &applied, &stored, NULL, c64_content_service_crt_attached());
    if (callbacks.apply(callbacks.context, &applied, true, true) != 0) {
        status("Title settings could not be reverted");
        return -1;
    }
    memset(&overlay, 0, sizeof(overlay));
    status("Saved settings restored");
    return 0;
}
int c64_title_service_save(void)
{
    if (!session[0]) {
        status("Launch a title or apply a configuration first");
        return -1;
    }
    char relative[256], path[512], journal[520];
    size_t size = 0, written = 0;
    bool found = false;
    if (!mt_c64_title_sidecar(relative, sizeof(relative), session, ".cfg") ||
        !resolve(relative, false, path) || !read_config(path, &size, &found))
        return -1;
    mt_c64_title_values_t current, applied;
    c64_title_service_effective(&current, &applied, false);
    mt_c64_title_overlay_t ignored;
    mt_c64_title_report_t parsed;
    mt_c64_title_parse_context_t context = {
        &caps, &stored, ntsc, session_origin == MT_TITLE_CRT, false, session_origin};
    if (!mt_c64_title_parse(read_buffer, size, &context, &ignored, &parsed) || parsed.errors ||
        !mt_c64_title_write(read_buffer, size, &current, &caps, &parsed, write_buffer,
                            sizeof(write_buffer), &written)) {
        status("Configuration has errors or would exceed 16 KiB");
        return -1;
    }
    (void)snprintf(journal, sizeof(journal), "%s.mtj", path);
    if (mt_sd_storage_journal_replace(path, journal, write_buffer, written).status != MT_SD_OK) {
        status("Configuration save failed; recovery retained");
        return -1;
    }
    if (!mt_c64_title_parse(write_buffer, written, &context, &pending, &last_report)) {
        status("Configuration saved; parse failed");
        return -1;
    }
    mt_c64_title_values_t saved, saved_applied;
    effective(&saved, &saved_applied, &stored, &pending,
                           c64_content_service_crt_attached());
    if (callbacks.apply(callbacks.context, &saved_applied, true, false) != 0) {
        status("Configuration saved; live apply failed");
        return -1;
    }
    (void)snprintf(pending.source, sizeof(pending.source), "%s", relative);
    overlay = pending;
    status("Title configuration saved");
    return 0;
}
void c64_title_service_clear(void)
{
    memset(&overlay, 0, sizeof(overlay));
    memset(&pending, 0, sizeof(pending));
    session[0] = pending_name[0] = notice[0] = 0;
    session_origin = pending_origin = MT_TITLE_NONE;
    committed = false;
}
void c64_title_service_reset(bool cartridge, bool disk_present)
{
    if (!cartridge && session_origin == MT_TITLE_DISK && disk_present)
        return;
    c64_title_service_clear();
}
int c64_title_service_reboot(bool default_cartridge)
{
    mt_c64_title_overlay_t none = {0};
    mt_c64_title_values_t requested, applied;
    effective(&requested, &applied, &stored, &none, default_cartridge);
    const char *conflict = mt_c64_title_io_conflict(&applied, &caps, default_cartridge);
    if (conflict) {
        status(conflict);
        return -1;
    }
    if (callbacks.apply(callbacks.context, &applied, false, true) != 0)
        return -1;
    c64_title_service_clear();
    return 0;
}
const mt_c64_title_overlay_t *c64_title_service_overlay(void)
{
    return &overlay;
}
const char *c64_title_service_session(void)
{
    return session;
}
const char *c64_title_service_status(void)
{
    return notice;
}
bool c64_title_service_edit(uint32_t groups, const mt_c64_title_values_t *values)
{
    if (!(overlay.owned & groups))
        return false;
    mt_c64_title_overlay_t edit = {0};
    edit.values = *values;
    edit.owned = groups;
    if (groups & MT_TITLE_SID) {
        edit.sid_fields = UINT64_MAX;
        edit.sid_classes = 7;
    }
    mt_c64_title_merge(&overlay, &edit);
    return true;
}
bool c64_title_service_sid_owned(uint64_t fields)
{
    uint64_t owned = overlay.sid_fields;
    for (unsigned n = 0; n < 8; ++n) {
        unsigned b = 25 + 4 * n;
        if (overlay.sid_classes & 1)
            owned |= UINT64_C(1) << b;
        if (overlay.sid_classes & 4)
            owned |= UINT64_C(1) << (b + 1);
        if (overlay.sid_classes & 2)
            owned |= UINT64_C(3) << (b + 2);
    }
    return (owned & fields) != 0;
}
void c64_title_service_edit_sid(const mt_c64_sid_model_t *model, uint64_t fields, uint8_t classes)
{
    mt_c64_title_overlay_t edit = {0};
    edit.owned = MT_TITLE_SID;
    edit.values.sid = *model;
    edit.values.sid_custom = true;
    edit.values.sid_mode = 3;
    edit.sid_fields = fields;
    edit.sid_classes = classes;
    mt_c64_title_merge(&overlay, &edit);
}
