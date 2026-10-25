/* SPDX-License-Identifier: GPL-3.0-only
 * This preamble and scenarios surround unmodified functions extracted from
 * main.c. The title parser/session and SID resolver are their real sources.
 */
#include "c64_title_service.h"
#include "multitini_c64_debug.h"
#include "multitini_c64_interface.h"
#include "multitini_content.h"
#include "multitini_media_browser.h"
#include "multitini_regs.h"
#include "multitini_supervisor.h"
#include "supervisor_settings.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define MT_SHELL_BASE_ADDRESS UINT32_C(0x40000000)
static char uart[2048];
static int capture_printf(const char *format, ...)
{
    size_t at = strlen(uart);
    va_list args;
    va_start(args, format);
    int written = vsnprintf(uart + at, sizeof(uart) - at, format, args);
    va_end(args);
    assert(written >= 0 && (size_t)written < sizeof(uart) - at);
    return written;
}
#define xil_printf capture_printf
static mt_supervisor_t supervisor;
static mt_core_selection_t c64_extension_selection;
static char c64_launch_disk_name[256] = "games/Demo.d64", uci_home[512];
static unsigned launches, hook_commits, home_updates;
static mt_supervisor_settings_t persistent_settings, settings_before_update, saved_settings;
static mt_c64_sid_model_t c64_stored_sid, saved_sid;
static mt_c64_sid_table_t c64_live_sid_table;
static mt_c64_capabilities_t c64_title_caps;
static mt_c64_debug_t c64_debug;
static bool c64_title_initialized, c64_uci_initialized, c64_sid_commit_fault,
    c64_legacy_turbo_available;
static bool c64_uci_applying_config;
static uint32_t peripheral_control;
static bool attached, save_fail, map_save_fail, supervisor_menu_dirty, held, hold_fail;
static bool legacy_enabled;
static uint32_t expansion_caps, config, cpu_config, sid_status, ua_fault, index_register;
static uint32_t table_words[57];
static unsigned writes, saves, map_saves, hold_count, table_writes, commits, commit_mode;
static uint8_t joystick, c64_selected_sid;
static uint64_t ticks;
static char message[160];
static const char *cfg_text;
static uint32_t shell_read(uint32_t reg)
{
    if (reg == MT_REG_ABI_VERSION) return supervisor.active.package.abi_minor;
    if (reg == MT_REG_C64_PERIPHERAL_CONTROL) return peripheral_control;
    if (reg == MT_REG_C64_EXPANSION_CONFIG)
        return config;
    if (reg == MT_REG_C64_CPU_CONFIG)
        return cpu_config;
    if (reg == MT_REG_C64_CPU_STATUS)
        return held ? MT_C64_CPU_STATUS_PAUSE : 0;
    if (reg == MT_REG_C64_SID_STATUS)
        return sid_status;
    if (reg == MT_REG_C64_UA_STATUS)
        return ua_fault;
    assert(0);
    return 0;
}
static uint32_t feature_read(void *unused, uint32_t reg)
{
    (void)unused;
    if (reg == MT_REG_ABI_VERSION)
        return supervisor.active.package.abi_minor;
    if (reg == MT_REG_C64_EXPANSION_CAPS)
        return expansion_caps;
    if (reg == MT_REG_C64_CPU_CAPS)
        return MT_C64_CPU_CAPS_RELEASE;
    if (reg == MT_REG_C64_CPU_CAPS2)
        return MT_C64_CPU_CAPS2_RELEASE;
    if (reg == MT_REG_DEBUG_STATUS)
        return MT_DEBUG_STATUS_AUDIO_PRESENT;
    assert(0);
    return 0;
}
static mt_core_link_t shell_link = {.read32 = feature_read};
static void Xil_Out32(uint32_t address, uint32_t value)
{
    uint32_t reg = address - MT_SHELL_BASE_ADDRESS;
    if (reg == MT_REG_C64_PERIPHERAL_CONTROL) {peripheral_control = value; return;}
    if (reg == MT_REG_C64_EXPANSION_CONFIG) {
        config = value;
        ++writes;
    } else if (reg == MT_REG_C64_CPU_CONFIG)
        cpu_config = value;
    else if (reg == MT_REG_C64_SID_INDEX)
        index_register = value;
    else if (reg == MT_REG_C64_SID_DATA) {
        assert(index_register < 57);
        table_words[index_register] = value;
        ++table_writes;
    } else if (reg == MT_REG_C64_SID_COMMIT) {
        assert(held && table_writes == 57);
        ++commits;
        sid_status = commit_mode == 1   ? MT_C64_SID_STATUS_REJECTED
                     : commit_mode == 2 ? MT_C64_SID_STATUS_BUSY
                                        : MT_C64_SID_STATUS_VALID | (value & 255);
    } else
        assert(0);
}
static uint64_t monotonic_ms(void *unused)
{
    (void)unused;
    ticks += 100;
    return ticks;
}
static void service_usb_input(void) {}
bool c64_content_service_crt_attached(void)
{
    return attached;
}
static int usb_hid_service_set_joystick_port(uint8_t p)
{
    joystick = p;
    return 0;
}
static bool usb_hid_service_symbolic_keyboard(void)
{
    return false;
}
static void set_c64_cpu_turbo(bool enabled)
{
    legacy_enabled = enabled;
}
static bool c64_uci_service_available(void)
{
    return true;
}
static int c64_uci_service_hold_begin(void)
{
    ++hold_count;
    if (hold_fail)
        return -1;
    held = true;
    return 0;
}
static void c64_uci_service_refresh_control(void) {}
static void c64_uci_service_hold_end(void)
{
    if (!c64_sid_commit_fault)
        held = false;
}
static void c64_uci_service_hold_fault(void)
{
    held = true;
}
static void c64_debug_pause(void *unused, bool pause)
{
    (void)unused;
    held = pause;
}
void mt_c64_debug_clear_override(mt_c64_debug_t *d)
{
    d->override_active = false;
}
uint32_t mt_c64_debug_effective_overlay(const mt_c64_debug_t *d, uint32_t value, uint32_t old,
                                        bool event)
{
    (void)d;
    return event ? ((value & ~MT_C64_CPU_CONFIG_APPLY_MASK) |
                    ((old + UINT32_C(0x1000)) & MT_C64_CPU_CONFIG_APPLY_MASK))
                 : value;
}
static void set_supervisor_warning(const char *s)
{
    (void)snprintf(message, sizeof message, "%s", s);
}
static void set_supervisor_status(const char *s)
{
    set_supervisor_warning(s);
}
static void set_supervisor_error(const char *s)
{
    set_supervisor_warning(s);
}
static void c64_uci_service_set_home(const char *s)
{
    ++home_updates;
    (void)snprintf(uci_home, sizeof(uci_home), "%s", s);
}
static mt_c64_content_service_result_t accepted_launch(const mt_c64_launch_hook_t *hook)
{
    ++launches;
    if (hook && hook->commit) {
        ++hook_commits;
        assert(hook->commit(hook->context) == 0);
    }
    return (mt_c64_content_service_result_t){.status = MT_C64_CONTENT_SERVICE_OK};
}
mt_c64_content_service_result_t c64_content_service_load_hooked(
    const char *path, const mt_c64_launch_hook_t *hook)
{
    assert(path && *path);
    mt_c64_content_service_result_t result = accepted_launch(hook);
    attached = mt_content_classify(path) == MT_CONTENT_CRT;
    return result;
}
mt_c64_content_service_result_t c64_content_service_stage_prg_image_hooked(
    const uint8_t *data, size_t size, const mt_c64_launch_hook_t *hook)
{
    assert(data && size);
    return accepted_launch(hook);
}
mt_c64_content_service_result_t c64_content_service_prepare_disk_load_hooked(
    const mt_c64_launch_hook_t *hook)
{
    return accepted_launch(hook);
}
static bool save_persistent_settings(void)
{
    ++saves;
    if (save_fail)
        return false;
    saved_settings = persistent_settings;
    return true;
}
bool mt_supervisor_sid_map_save(const char *id, const mt_c64_sid_model_t *m)
{
    (void)id;
    ++map_saves;
    if (map_save_fail)
        return false;
    saved_sid = *m;
    return true;
}
bool mt_sd_file_path_busy(const char *p, bool exclusive)
{
    (void)p;
    (void)exclusive;
    return false;
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *p, const char *j, uint8_t *b, size_t n)
{
    (void)p;
    (void)j;
    (void)b;
    (void)n;
    return (mt_sd_result_t){MT_SD_OK, 0, 0};
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *p, uint8_t *b, size_t n, bool *f)
{
    (void)p;
    (void)b;
    (void)n;
    *f = false;
    return (mt_sd_result_t){MT_SD_OK, 0, 0};
}
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *p, uint8_t *b, size_t n, bool *f)
{
    *f = cfg_text && strstr(p, ".cfg") && !strstr(p, ".mtj");
    size_t len = *f ? strlen(cfg_text) : 0;
    assert(len <= n);
    if (len)
        memcpy(b, cfg_text, len);
    return (mt_sd_result_t){MT_SD_OK, 0, len};
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *p, const char *j, const uint8_t *b,
                                             size_t n)
{
    (void)p;
    (void)j;
    (void)b;
    (void)n;
    return (mt_sd_result_t){MT_SD_OK, 0, 0};
}
static int apply_c64_title(void *, const mt_c64_title_values_t *, bool, bool);
/* PRODUCTION_FUNCTIONS */
static void media_failure(mt_c64_content_service_status_t status, bool remounted)
{
    mt_c64_content_service_result_t result = {.status = status};
    /* PRODUCTION_MENU_FAILURE */
}
static void initialize(void)
{
    memset(&supervisor, 0, sizeof supervisor);
    supervisor.has_active = true;
    supervisor.active.package.core_id = MT_C64_CORE_ID;
    supervisor.active.package.abi_minor = 26;
    strcpy(supervisor.active.package.id, "c64");
    strcpy(supervisor.active.package.variants[0].id, "pal");
    strcpy(supervisor.active.package.content_root, "0:/media");
    c64_extension_selection = supervisor.active;
    expansion_caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_DUAL_SID |
                     MT_C64_EXPANSION_CAPS_REU_16MB | MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO |
                     MT_C64_EXPANSION_CAPS_UCI | MT_C64_EXPANSION_CAPS_MULTI_SID |
                     MT_C64_EXPANSION_CAPS_SID_MODEL_8580 | 0x700U;
    config = 11;
    joystick = 2;
    mt_supervisor_settings_defaults(&persistent_settings);
    mt_c64_sid_default(&c64_stored_sid);
    c64_title_initialized = c64_uci_initialized = true;
    const c64_title_callbacks_t callbacks = {NULL, apply_c64_title};
    assert(c64_title_service_init("0:/media", &callbacks) == 0);
    refresh_c64_title(&supervisor.active);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    initialize();
    if (!strcmp(argv[1], "uci-unlock-config")) {
        supervisor.active.package.abi_minor = 27;
        expansion_caps |= MT_C64_EXPANSION_CAPS_UCI_UNLOCK;
        cfg_text = "[U64 Specific Settings]\nCPU Speed=32\n";
        char report[512];
        assert(uci_apply_config(NULL, "0:/media/Game.cfg", report, sizeof report) == 0);
        assert(!peripheral_control && !c64_uci_applying_config);
        cfg_text = "[U64 Specific Settings]\nCPU Speed=invalid\n";
        assert(uci_apply_config(NULL, "0:/media/Game.cfg", report, sizeof report) == 89);
        assert(!peripheral_control && !c64_uci_applying_config);
        configure_c64_expansion(&supervisor.active, NULL);
        assert(peripheral_control == MT_C64_PERIPHERAL_UCI_CONFIG_EPOCH);
    } else if (!strcmp(argv[1], "defaults")) {
        configure_c64_expansion(&supervisor.active, NULL);
        assert(config == 11 && !saves);
        assert(ensure_active_settings_profile());
        assert(
            mt_supervisor_settings_remember_c64_expansion(&persistent_settings, "c64", 2, false));
        assert(mt_c64_sid_preset(&c64_stored_sid, MT_SID_DUAL420));
        configure_c64_expansion(&supervisor.active, NULL);
        assert(config == 2 && !saves);
    } else if (!strcmp(argv[1], "gates")) {
        for (unsigned abi = 0; abi < 23; ++abi) {
            supervisor.active.package.abi_minor = (uint16_t)abi;
            refresh_c64_title(&supervisor.active);
            change_c64_expansion(2);
            change_c64_expansion(3);
        }
        assert(!writes && !saves);
        supervisor.active.package.abi_minor = 26;
        expansion_caps = 0;
        refresh_c64_title(&supervisor.active);
        change_c64_expansion(2);
        change_c64_expansion(3);
        assert(!writes && !saves);
    } else if (!strcmp(argv[1], "cycle")) {
        expansion_caps &= ~MT_C64_EXPANSION_CAPS_MULTI_SID;
        refresh_c64_title(&supervisor.active);
        const unsigned expected[] = {8, 10, 11};
        for (unsigned i = 0; i < 3; ++i) {
            change_c64_expansion(2);
            assert(config == expected[i] && saves == i + 1);
        }
        change_c64_expansion(3);
        assert(config == 3 && !persistent_settings.profiles[0].c64_reu_enabled);
        change_c64_expansion(3);
        assert(config == 11 && persistent_settings.profiles[0].c64_reu_enabled);
    } else if (!strcmp(argv[1], "conflict")) {
        assert(ensure_active_settings_profile());
        assert(
            mt_supervisor_settings_remember_c64_expansion(&persistent_settings, "c64", 3, false));
        attached = true;
        config = 3;
        change_c64_expansion(3);
        assert(!writes && !saves && config == 3);
        assert(strstr(message, "Eject"));
    } else if (!strcmp(argv[1], "save-failure")) {
        save_fail = true;
        change_c64_expansion(3);
        assert(config == 11 && saves == 1 && !persistent_settings.profile_count);
        assert(strstr(message, "previous value restored"));
    } else if (!strcmp(argv[1], "partial")) {
        expansion_caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_16MB;
        refresh_c64_title(&supervisor.active);
        change_c64_expansion(2);
        assert(!writes && !saves);
        change_c64_expansion(3);
        assert(!(config & 8) && saves == 1);
    } else if (!strcmp(argv[1], "ua")) {
        change_c64_expansion(8);
        assert(config == 27 && persistent_settings.c64_ultimate_audio);
        change_c64_expansion(3);
        assert(config == 3 && !persistent_settings.c64_ultimate_audio);
        ua_fault = 1;
        unsigned before = writes;
        change_c64_expansion(8);
        assert(writes == before);
    } else if (!strcmp(argv[1], "title")) {
        cfg_text = "[U64 Specific Settings]\nCPU Speed=16\n";
        assert(c64_title_service_apply("Game.cfg", false, NULL, 0) == 0);
        unsigned before = saves;
        change_c64_turbo(4);
        assert(saves == before && (c64_title_service_overlay()->owned & MT_TITLE_SPEED));
        assert(persistent_settings.c64_turbo_detail == 0U);
        change_c64_turbo(12);
        assert(saves == before && (c64_title_service_overlay()->owned & MT_TITLE_TURBO));
        assert(legacy_enabled && !held);
        change_c64_expansion(15);
        assert(saves == before + 1 && persistent_settings.c64_command_interface);
    } else if (!strcmp(argv[1], "sid-first")) {
        c64_selected_sid = 0;
        change_c64_sid_setup(8, -1);
        assert(commits == 1 && table_writes == 57 && (sid_status & MT_C64_SID_STATUS_VALID));
        assert(config & MT_C64_EXPANSION_SID_MAP_ENABLE);
        assert(!held && saves == 1 && map_saves == 1);
    } else if (!strcmp(argv[1], "io-owner")) {
        expansion_caps |= MT_C64_EXPANSION_CAPS_SID_IO_SLOTS;
        refresh_c64_title(&supervisor.active);
        mt_c64_title_values_t v = c64_requested_values(&supervisor.active, false);
        v.sid.sid_address[1] = 41; /* DF00 slot includes UCI DF18 and REU registers. */
        assert(!commit_c64_edit(&v, MT_TITLE_SID, UINT64_C(1) << 29, 0) && !writes && !saves);
        change_c64_expansion(3);
        assert(!persistent_settings.profiles[0].c64_reu_enabled);
        v = c64_requested_values(&supervisor.active, false);
        v.sid.sid_address[1] = 41;
        assert(commit_c64_edit(&v, MT_TITLE_SID, UINT64_C(1) << 29, 0));
        unsigned before = writes, saved = saves;
        change_c64_expansion(3);
        assert(writes == before && saves == saved && strstr(message, "REU"));
        change_c64_expansion(15);
        assert(writes == before && saves == saved && strstr(message, "Command"));
        attached = true;
        assert(apply_c64_title(NULL, &v, true, true) < 0 && writes == before && !held);
    } else if (!strcmp(argv[1], "hold-failure")) {
        hold_fail = true;
        change_c64_expansion(3);
        assert(!writes && !saves && config == 11 && !held);
    } else if (!strcmp(argv[1], "sid-reject")) {
        commit_mode = 1;
        c64_selected_sid = 0;
        change_c64_sid_setup(8, -1);
        assert(commits == 1 && !writes && !saves && !held && !c64_sid_commit_fault);
    } else if (!strcmp(argv[1], "sid-timeout")) {
        commit_mode = 2;
        c64_selected_sid = 0;
        change_c64_sid_setup(8, -1);
        assert(commits == 1 && !writes && !saves && held && c64_sid_commit_fault);
        change_c64_expansion(3);
        assert(!writes && !saves && held);
    } else if (!strcmp(argv[1], "launch-refusal")) {
        const char reason[] = "Turn REU off, or disable it in the title .cfg";
        assert(c64_stored_values(&supervisor.active).reu);
        mt_c64_content_service_result_t result = c64_title_load("games/Cart.crt", true);
        assert(result.status == MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
        assert(!launches && !hook_commits && !writes && !home_updates);
        assert(!strcmp(c64_title_service_status(), reason));
        assert(!strcmp(uart, "[C64] Turn REU off, or disable it in the title .cfg\r\n"));
        media_failure(result.status, true);
        assert(!strcmp(message, reason));
        media_failure(result.status, false);
        assert(!strcmp(message, "Content load and drive remount failed"));
        media_failure(MT_C64_CONTENT_SERVICE_READ_FAILED, true);
        assert(!strcmp(message, "Content load failed"));
        assert(!*c64_title_service_session());
    } else if (!strcmp(argv[1], "disk-refusal")) {
        const uint8_t program[] = {1, 8, 0};
        expansion_caps |= MT_C64_EXPANSION_CAPS_SID_IO_SLOTS;
        c64_stored_sid.sid_address[1] = 41;
        for (unsigned i = 0; i < 2; i++) {
            uart[0] = 0;
            mt_c64_content_service_result_t result = c64_title_disk(i ? program : NULL,
                                                                      i ? sizeof(program) : 0);
            assert(result.status == MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
            assert(!launches && !hook_commits && !writes && !home_updates);
            assert(!strcmp(uart, "[C64] Disable REU before mapping a SID in IO2\r\n"));
            media_failure(result.status, true);
            assert(!strcmp(message, "Disable REU before mapping a SID in IO2"));
        }
    } else if (!strcmp(argv[1], "launch-allowed")) {
        const uint8_t program[] = {1, 8, 0};
        assert(c64_title_load("games/Demo.prg", true).status == MT_C64_CONTENT_SERVICE_OK);
        assert(launches == 1 && home_updates == 1 && !strcmp(uci_home, "0:/media/games"));
        assert(c64_title_disk(NULL, 0).status == MT_C64_CONTENT_SERVICE_OK);
        assert(c64_title_disk(program, sizeof(program)).status == MT_C64_CONTENT_SERVICE_OK);
        assert(launches == 3 && home_updates == 3 && !strcmp(c64_title_service_session(),
                                                           "games/Demo.d64"));
        cfg_text = "[C64 and Cartridge Settings]\nRAM Expansion Unit=Disabled\n";
        assert(c64_title_load("games/Cart.crt", true).status == MT_C64_CONTENT_SERVICE_OK);
        assert(launches == 4 && hook_commits == 1 && home_updates == 4 && attached);
        assert(!(config & MT_C64_EXPANSION_REU_ENABLE));
        assert(!strcmp(c64_title_service_session(), "games/Cart.crt") &&
               !strcmp(uci_home, "0:/media/games"));
    } else
        assert(0);
    printf("PASS: C64 expansion %s\n", argv[1]);
    return 0;
}
