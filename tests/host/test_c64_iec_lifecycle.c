/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_amiga_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_regs.h"
#include "multitini_vectrex_interface.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t core_id = MT_C64_CORE_ID, control;
static bool c64_iec_initialized = true, c64_services_initialized = true, c64_core_reset_active;
static bool gameboy_service_initialized, genesis_service_initialized, nes_service_initialized, iigs_service_initialized,amiga_service_initialized;
static bool c64_debug_pause_active, usb_sd_core_stopped, usb_sd_previously_reset;
static bool c64_uci_initialized, c64_title_initialized, c64_title_foreground, usb_sd_modal;
static char c64_launch_disk_name[256];
static struct { bool c64_directory_drive_off; } persistent_settings;
static int uci_error, enable_error, restore_error;
static bool save_ok, directory_active;
static unsigned saves, status_count, error_count, refresh_count, quiesce_count;
static const char *last_status;
/* This older fixture isolates IEC/reset orchestration. UCI/native transaction
 * cleanup and USB cancellation have their own production service fixtures. */
static int quiesce_c64_uci(void) {return uci_error;}
static int retry_c64_uci(void) {return quiesce_c64_uci();}
static bool c64_uci_service_reload_required(void) {return false;}
/* No EasyFlash is attached in these drive lifecycle scenarios. */
static int c64_content_service_flush(void) {return 0;}
static void c64_uci_service_refresh_control(void) {++refresh_count;}
static void set_supervisor_status(const char *status) {last_status = status; ++status_count;}
static void set_supervisor_error(const char *status) {last_status = status; ++error_count;}
static void set_supervisor_warning(const char *status) {set_supervisor_error(status);}
static bool save_persistent_settings(void) {++saves; return save_ok;}
/* Ambiguous native-token recovery is exercised by test_c64_reload_runtime.
 * It must never be selected by these normal reset/IEC scenarios. */
static int quiesce_c64_for_reload(void) {assert(0);return -1;}
static void c64_uci_service_reset(void) {}
static void mt_net_service_guest_reset(void) {}
static void mt_net_service_shutdown(void) {}
static void c64_title_service_clear(void) {}
static const char *c64_d64_service_mounted_name(void) {return "mounted.d64";}
static int c64_debug, document_reader;
static int directory_error, drive_error, command_error, reset_error, mount_error;
static bool available = true, menu_ok = true;
static char trace[128];
static size_t trace_size;
static void mark(char c)
{
    assert(trace_size + 1 < sizeof trace);
    trace[trace_size++] = c;
    trace[trace_size] = 0;
}
static uint32_t shell_read(uint32_t reg)
{
    if (reg == MT_REG_CORE_ID)
        return core_id;
    if (reg == MT_REG_CONTROL)
        return control;
    if (reg == MT_REG_VIDEO_FRAME_STATUS)
        return MT_VIDEO_FRAME_STATUS_QUIESCENT;
    assert(0);
    return 0;
}
static bool shell_is_c64(void)
{
    return core_id == MT_C64_CORE_ID;
}
static int xil_printf(const char *format, ...)
{
    (void)format;
    return 0;
}
static bool c64_iec_service_available(void)
{
    return available;
}
static int c64_iec_service_quiesce(void)
{
    mark('q');
    directory_active = false;
    return ++quiesce_count > 1 && restore_error ? restore_error : directory_error;
}
static int c64_iec_service_enable(bool on)
{
    assert(on);
    mark('e');
    if (!enable_error) directory_active = true;
    return enable_error;
}
static const char *c64_iec_service_status(void)
{
    return "test";
}
static int c64_iec_service_reset(void)
{
    mark('r');
    return reset_error;
}
static bool hide_menu_for_core_action(void)
{
    return menu_ok;
}
static int c64_d64_service_prepare_reset(void)
{
    mark('d');
    return drive_error;
}
static int c64_d64_service_resume_after_reset(void)
{
    mark('b');
    return 0;
}
static void c64_d64_service_notify_core_reset(void)
{
    mark('n');
}
static int c64_d64_service_mount(const char *name)
{
    assert(!strcmp(name, "SUB/DISK.D64"));
    mark('m');
    return mount_error;
}
#define SAVE_STUB(name)                                                                                                \
    static int name(void)                                                                                              \
    {                                                                                                                  \
        return 0;                                                                                                      \
    }
SAVE_STUB(gameboy_content_service_prepare_reset)
SAVE_STUB(gameboy_content_service_resume_after_reset)
SAVE_STUB(genesis_content_service_prepare_reset)
SAVE_STUB(genesis_content_service_resume_after_reset)
SAVE_STUB(nes_content_service_prepare_reset)
SAVE_STUB(nes_content_service_resume_after_reset)
SAVE_STUB(iigs_content_service_prepare_reset)
SAVE_STUB(iigs_content_service_resume_after_reset)
SAVE_STUB(amiga_content_service_prepare_reset)
SAVE_STUB(amiga_content_service_resume_after_reset)
SAVE_STUB(amiga_content_service_shutdown)
static int command(uint32_t cmd, uint32_t arg, uint32_t unused)
{
    assert(cmd == MT_COMMAND_CORE_RESET && !unused);
    mark(arg ? 'a' : 'u');
    if (!command_error)
    {
        if (arg)
            control |= MT_CONTROL_CORE_RESET_REQUEST;
        else
            control &= ~MT_CONTROL_CORE_RESET_REQUEST;
    }
    return command_error;
}
static void usb_hid_service_set_core_ready(bool ready)
{
    (void)ready;
}
static bool mt_screenshot_service_cancel(void)
{
    return true;
}
static void mt_c64_debug_deactivate(void *p)
{
    (void)p;
}
static void set_audio(bool mute, bool test)
{
    (void)mute;
    (void)test;
}
static void mt_text_reader_close(void *p)
{
    (void)p;
}
static void mt_video_effect_service_suspend(void)
{
}
static bool core_requires_ddr_drain(void)
{
    return true;
}
static uint64_t monotonic_ms(void *p)
{
    (void)p;
    return 0;
}
static bool mt_video_effect_service_quiescent(void)
{
    return true;
}
static bool mt_video_worker_quiesce(void)
{
    return true;
}
static void usb_hid_service_poll_deferred(void)
{
}
static void amiga_display_guest_restarted(void) {assert(core_id == MT_AMIGA_CORE_ID); }
#include "c64_iec_lifecycle.inc"
static void reset_fixture(void)
{
    core_id = MT_C64_CORE_ID;
    control = 0;
    directory_error = drive_error = command_error = reset_error = mount_error = 0;
    trace_size = 0;
    trace[0] = 0;
    available = menu_ok = true;
    c64_core_reset_active = false;
    c64_iec_initialized = c64_services_initialized = true;
    c64_uci_initialized = true;
    persistent_settings.c64_directory_drive_off = false;
    uci_error = enable_error = restore_error = 0;
    save_ok = directory_active = true;
    saves = status_count = error_count = refresh_count = quiesce_count = 0;
    last_status = NULL;
}
int main(void)
{
    unsigned cases = 0;
    reset_fixture();
    assert(set_core_reset_request(true) == 0);
    assert(!strcmp(trace, "qdanr") && c64_core_reset_active);
    ++cases;
    trace_size = 0;
    trace[0] = 0;
    assert(set_core_reset_request(false) == 0 && !strcmp(trace, "ue"));
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(set_core_reset_request(true) == -1 && !strcmp(trace, "q") && control == 0);
    ++cases;
    reset_fixture();
    drive_error = 1;
    assert(set_core_reset_request(true) == -1 && !strcmp(trace, "qde") && control == 0);
    ++cases;
    reset_fixture();
    command_error = -1;
    assert(set_core_reset_request(true) == -1 && !strcmp(trace, "qdabe") && control == 0);
    ++cases;
    reset_fixture();
    assert(supervisor_quiesce(NULL) == 0 && !strcmp(trace, "qdanr"));
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(supervisor_quiesce(NULL) == -1 && !strcmp(trace, "q"));
    ++cases;
    reset_fixture();
    assert(usb_sd_quiesce(NULL) == 0 && !strcmp(trace, "qdanr") && usb_sd_core_stopped);
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(usb_sd_quiesce(NULL) == -1 && !strcmp(trace, "q") && !usb_sd_core_stopped);
    ++cases;
    reset_fixture();
    core_id = MT_GENESIS_CORE_ID;
    assert(set_core_reset_request(true) == 0 && !strcmp(trace, "a"));
    trace_size = 0;
    trace[0] = 0;
    assert(set_core_reset_request(false) == 0 && !strcmp(trace, "u"));
    ++cases;
    reset_fixture();
    core_id = MT_GENESIS_CORE_ID;
    assert(usb_sd_quiesce(NULL) == 0 && !strcmp(trace, "a"));
    ++cases;
    reset_fixture();
    assert(prepare_c64_content_change() && !strcmp(trace, "qd"));
    assert(finish_c64_content_change() && !strcmp(trace, "qdreb"));
    ++cases;
    reset_fixture();
    drive_error = 1;
    assert(!prepare_c64_content_change() && !strcmp(trace, "qde"));
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(!prepare_c64_content_change() && !strcmp(trace, "q"));
    ++cases;
    reset_fixture();
    assert(mount_c64_disk("SUB/DISK.D64") == 0 && !strcmp(trace, "qme"));
    ++cases;
    reset_fixture();
    mount_error = -1;
    assert(mount_c64_disk("SUB/DISK.D64") == -1 && !strcmp(trace, "qme"));
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(mount_c64_disk("SUB/DISK.D64") == -1 && !strcmp(trace, "q"));
    ++cases;
    reset_fixture();
    menu_ok = false;
    assert(set_core_reset_request(true) == -1 && !trace[0]);
    ++cases;
    reset_fixture();
    reset_error = 25;
    assert(set_core_reset_request(true) == -1 && !strcmp(trace, "qdanr") && (control & MT_CONTROL_CORE_RESET_REQUEST));
    ++cases;
    reset_fixture();
    available = false;
    assert(quiesce_c64_directory() == 0 && resume_c64_directory() == 0 && !trace[0]);
    ++cases;
    reset_fixture();
    assert(change_c64_directory_drive() && persistent_settings.c64_directory_drive_off);
    assert(!directory_active && saves == 1 && status_count == 1 && !error_count && !strcmp(trace, "q"));
    assert(strstr(last_status, "Off") && refresh_count == 2);
    ++cases;
    trace_size = 0; trace[0] = 0;
    assert(resume_c64_directory() == 0 && resume_c64_directory() == 0 && !trace[0]);
    assert(!directory_active && refresh_count == 4);
    ++cases;
    assert(change_c64_directory_drive() && !persistent_settings.c64_directory_drive_off);
    assert(directory_active && saves == 2 && status_count == 2 && !strcmp(trace, "qe"));
    ++cases;
    reset_fixture();
    persistent_settings.c64_directory_drive_off = true;
    directory_active = false;
    assert(set_core_reset_request(true) == 0 && !strcmp(trace, "qdanr"));
    trace_size = 0; trace[0] = 0;
    assert(set_core_reset_request(false) == 0 && !strcmp(trace, "u") && !directory_active);
    ++cases;
    reset_fixture();
    persistent_settings.c64_directory_drive_off = true;
    assert(prepare_c64_content_change() && finish_c64_content_change() && !strcmp(trace, "qdrb"));
    assert(persistent_settings.c64_directory_drive_off && !directory_active);
    ++cases;
    reset_fixture();
    persistent_settings.c64_directory_drive_off = true;
    assert(mount_c64_disk("SUB/DISK.D64") == 0 && !strcmp(trace, "qm"));
    assert(!directory_active);
    ++cases;
    reset_fixture();
    uci_error = -1;
    assert(!change_c64_directory_drive() && !trace[0] && !saves && !status_count && error_count == 1);
    assert(!persistent_settings.c64_directory_drive_off && directory_active);
    ++cases;
    reset_fixture();
    directory_error = 25;
    assert(!change_c64_directory_drive() && !strcmp(trace, "q") && !saves && !status_count);
    assert(!persistent_settings.c64_directory_drive_off && error_count == 1);
    ++cases;
    reset_fixture();
    persistent_settings.c64_directory_drive_off = true;
    directory_active = false;
    enable_error = -1;
    assert(!change_c64_directory_drive() && !strcmp(trace, "qeq") && !saves && !status_count);
    assert(persistent_settings.c64_directory_drive_off && !directory_active && error_count == 1);
    ++cases;
    reset_fixture();
    save_ok = false;
    assert(!change_c64_directory_drive() && !strcmp(trace, "qqe") && saves == 1 && !status_count);
    assert(!persistent_settings.c64_directory_drive_off && directory_active && error_count == 1);
    ++cases;
    reset_fixture();
    persistent_settings.c64_directory_drive_off = true;
    directory_active = false;
    save_ok = false;
    assert(!change_c64_directory_drive() && !strcmp(trace, "qeq") && saves == 1 && !status_count);
    assert(persistent_settings.c64_directory_drive_off && !directory_active && error_count == 1);
    ++cases;
    reset_fixture();
    save_ok = false;
    restore_error = 25;
    assert(!change_c64_directory_drive() && !strcmp(trace, "qq") && !status_count);
    assert(!persistent_settings.c64_directory_drive_off && !directory_active);
    assert(strstr(last_status, "restore failed"));
    ++cases;
    reset_fixture();
    available = false;
    assert(!change_c64_directory_drive() && !trace[0] && !saves && !status_count && error_count == 1);
    ++cases;
    printf("PASS C64 IEC lifecycle: %u scenarios\n", cases);
    return 0;
}
