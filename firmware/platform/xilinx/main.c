/* SPDX-License-Identifier: GPL-3.0-only */
#include "../../golden/lib/golden_self_update.h"
#include "../../golden/lib/multiboot_platform.h"
#include "c64_content_service.h"
#include "c64_d64_service.h"
#include "c64_iec_service.h"
#include "c64_title_service.h"
#include "c64_uci_service.h"
#include "core_catalog_sd.h"
#include "gameboy_content_service.h"
#include "genesis_content_service.h"
#include "iigs_content_service.h"
#include "amiga_content_service.h"
#include "amiga_display_service.h"
#include "multitini_ak4493.h"
#include "multitini_c64_boot.h"
#include "multitini_c64_debug.h"
#include "multitini_c64_features.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_native_load.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"
#include "multitini_core_frontend.h"
#include "multitini_core_link.h"
#include "multitini_display.h"
#include "multitini_fatfs_time.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_hid_input.h"
#include "multitini_media_browser.h"
#include "multitini_menu.h"
#include "multitini_nes_interface.h"
#include "multitini_regs.h"
#include "multitini_supervisor.h"
#include "multitini_system.h"
#include "multitini_tmp102.h"
#include "multitini_ui.h"
#include "multitini_drive_activity.h"
#include "multitini_usb_storage_mode.h"
#include "multitini_vectrex_interface.h"
#include "multitini_spectrum_interface.h"
#include "spectrum_content_service.h"
#include "multitini_version.h"
#include "multitini_video_effects_frontend.h"
#include "multitini_xmodem.h"
#include "nes_content_service.h"
#include "net_service.h"
#include "pl_loader.h"
#include "screenshot_service.h"
#include "sd_storage.h"
#include "supervisor_settings.h"
#include "text_document.h"
#include "usb_hid_service.h"
#include "usb_storage_service.h"
#include "vectrex32_service.h"
#include "vectrex_content_service.h"
#include "video_effect_service.h"
#include "video_worker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "sleep.h"
#include "xdevcfg_hw.h"
#include "xiicps.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xiltimer.h"
#include "xuartps_hw.h"

#define MT_SHELL_BASE_ADDRESS UINT32_C(0x40000000)
#define MT_SUPERVISOR_CORE_ID UINT32_C(0)
#define MT_I2C_CLOCK_HZ UINT32_C(100000)
#define MT_I2C_BUSY_POLLS UINT32_C(1000000)
#define MT_UART_DRAIN_POLLS UINT32_C(10000000)
#define MT_UART_BYTE_TIMEOUT_MS UINT32_C(1000)
#define MT_UART_XMODEM_RETRIES UINT32_C(20)
#define MT_CORE_READY_POLLS UINT32_C(1000000)
#define MT_LINK_COMMAND_POLLS UINT32_C(100000)
#define MT_EXTENDED_COMMAND_CAPACITY 272U
#define MT_EXTENDED_ARGUMENT_CAPACITY 4
#define MT_MENU_MAX_FILES 144U
#define MT_MENU_HIDE_TIMEOUT_MS UINT64_C(1000)

/* Zynq-7000 UG585 multiboot and PS software-reset registers. */
#define MT_DEVCFG_MULTIBOOT UINT32_C(0xF800702C)
#define MT_DEVCFG_MULTIBOOT_MASK UINT32_C(0x00001FFF)
#define MT_SLCR_LOCK UINT32_C(0xF8000004)
#define MT_SLCR_UNLOCK UINT32_C(0xF8000008)
#define MT_SLCR_LOCK_KEY UINT32_C(0x0000767B)
#define MT_SLCR_UNLOCK_KEY UINT32_C(0x0000DF0D)
#define MT_PS_RESET_CONTROL UINT32_C(0xF8000200)
#define MT_PS_SOFTWARE_RESET UINT32_C(0x00000001)
#define MT_SLCR_FPGA_RESET UINT32_C(0xF8000240)
#define MT_SLCR_LEVEL_SHIFTER UINT32_C(0xF8000900)
#define MT_SLCR_IO_PLL_CONTROL UINT32_C(0xF8000108)
#define MT_SLCR_ARM_PLL_CONTROL UINT32_C(0xF8000100)
#define MT_SLCR_DDR_PLL_CONTROL UINT32_C(0xF8000104)
#define MT_SLCR_FCLK0_CONTROL UINT32_C(0xF8000170)
#define MT_SLCR_FCLK2_CONTROL UINT32_C(0xF8000190)

static XIicPs i2c_instance;
static mt_core_link_t shell_link;
static mt_c64_debug_t c64_debug;
static mt_sd_file_t *c64_debug_file;
static bool c64_debug_pause_active, c64_debug_was_visible;
static bool c64_debug_restoring;
static void initialize_c64_debug(void);
static bool dispatch_c64_debug(const char *line);
static void change_c64_turbo(size_t option);
static void c64_debug_pause(void *context, bool pause);
static void refresh_c64_title(const mt_core_selection_t *selection);
static int quiesce_c64_uci(void);
static int retry_c64_uci(void);
static void service_c64_uci_action(void);
static bool dispatch_c64_title(const char *line);
static bool supervisor_serial_command(const char *line);
static uint64_t monotonic_ms(void *context);
static bool amiga_serial_command(const char *line);
static void print_amiga_media_status(void);
static bool amiga_apply_display(void *context,uint8_t mode);
static bool amiga_request_display(uint8_t mode);
static void amiga_display_guest_restarted(void);
static void handle_menu_action(mt_menu_action_t action);
static void network_console(void *context, const char *text);
static uint8_t stored_c64_joystick(void);
static mt_c64_sid_model_t c64_stored_sid;
static mt_c64_sid_table_t c64_live_sid_table;
static mt_c64_capabilities_t c64_title_caps;
static bool c64_legacy_turbo_available;
static bool c64_sid_commit_fault;
static bool c64_title_initialized, c64_uci_initialized, c64_title_foreground;
static bool c64_uci_freeze_pending;
static bool c64_uci_applying_config;
static mt_core_selection_t c64_extension_selection;
static char c64_launch_disk_name[256];
static uint8_t c64_selected_sid;
static mt_ak4493_bus_t dac_bus;
static bool i2c_ready;
static bool dac_ready;
static bool c64_drive_ready;
static bool c64_core_reset_active;
static bool c64_services_initialized;
static bool c64_iec_initialized;
static bool c64_iec_fault_displayed;
static bool genesis_service_initialized;
static bool nes_service_initialized;
static bool nes_save_dirty_displayed;
static bool nes_save_fault_displayed;
static bool iigs_service_initialized;
static bool amiga_service_initialized;
static const char *spectrum_boot_error;
static bool amiga_fault_displayed;
static bool iigs_fault_displayed;
static unsigned iigs_browser_drive;
static bool genesis_save_dirty_displayed;
static bool genesis_save_fault_displayed;
static bool gameboy_service_initialized;
static bool gameboy_save_dirty_displayed;
static bool gameboy_save_fault_displayed;
static bool vectrex_service_initialized;
static bool vectrex_basic_running;
static bool vectrex_basic_stop;
static uint8_t c64_rom_bundle[MT_C64_SYSTEM_ROM_BUNDLE_SIZE];
static uint8_t c64_drive_rom[MT_SIZE_C64_1541_ROM];
static uint8_t c64_candidate_rom_bundle[MT_C64_SYSTEM_ROM_BUNDLE_SIZE];
static uint8_t c64_candidate_drive_rom[MT_SIZE_C64_1541_ROM];
static bool c64_rom_profile_active_valid;
static bool c64_active_drive_rom_available;
static bool c64_fast_roms_active;
static bool c64_fast_roms_fallback;

typedef enum {
    C64_ROM_RESTORE_OK = 0,
    C64_ROM_RESTORE_MEDIA_PENDING,
    C64_ROM_RESTORE_FAILED
} c64_rom_restore_result_t;

typedef enum {
    C64_ROM_STAGE_OK = 0,
    C64_ROM_STAGE_PROFILE_UNAVAILABLE,
    C64_ROM_STAGE_NO_ROLLBACK_PAIR,
    C64_ROM_STAGE_SWITCH_FAILED,
    C64_ROM_STAGE_MEDIA_FAILED,
    C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING,
    C64_ROM_STAGE_ROLLBACK_FAILED
} c64_rom_stage_result_t;

static XTime last_fatfs_time_poll;
static bool board_clock_available;
static bool board_clock_valid;
static mt_clock_time_t board_clock;
static mt_system_frontend_t system_frontend;
static mt_video_effects_frontend_t effects_frontend;
static mt_text_reader_t document_reader;
static char document_title[MT_MENU_FILE_NAME_MAX];
static bool board_temperature_valid;
static int32_t board_temperature_micro_c;
static uint64_t last_system_refresh_ms;
static mb_platform_status_t multiboot_status;
static char installed_golden_version[32];
static bool golden_update_present;
/* Sticky until the PS restarts. Never recycle a reset CPU1's DDR in place. */
static bool golden_update_worker_held;
static bool usb_sd_modal, usb_sd_stop_requested, usb_sd_core_stopped, usb_sd_previously_reset;
static bool usb_sd_had_active, usb_sd_restart_failed;
static mt_core_selection_t usb_sd_previous;
static mt_core_catalog_sd_t core_catalog;
static mt_supervisor_t supervisor;
static mt_menu_t supervisor_menu;
static mt_ui_frame_t supervisor_frame;
static mt_ui_presenter_t supervisor_presenter;
static mt_activity_t drive_activity;
static mt_ui_legacy_frame_t activity_frame;
static uint32_t activity_core;
static bool activity_badge_shown;
static mt_menu_action_t pending_menu_action;
static bool supervisor_menu_ready;
static bool supervisor_menu_dirty;
static bool supervisor_menu_action_active;
static uint8_t supervisor_menu_keys[MT_C64_HID_SLOT_COUNT]
                                   [MT_C64_HID_USAGE_BYTES];
static char supervisor_status_text[64] = "Ready";
static mt_ui_color_t supervisor_status_color = MT_UI_COLOR_MUTED;
static mt_menu_file_t supervisor_menu_files[MT_MENU_MAX_FILES];
static size_t supervisor_menu_file_count;
static mt_media_browser_t vectrex_media_browser;
static char vectrex_browser_path[512];
static mt_c64_d64_prg_entry_t supervisor_d64_prgs[MT_MENU_MAX_FILES];
static size_t supervisor_d64_prg_count;
static uint8_t supervisor_disk_prg_buffer[MT_SIZE_C64_PRG_MAX];
static mt_core_frontend_t supervisor_frontend;
static mt_supervisor_settings_t persistent_settings;
static mt_supervisor_settings_t settings_before_update;
static mt_controller_device_t controller_edit_devices[MT_GAMEPAD_SLOTS];
static uint32_t controller_edit_core;
static mt_core_selection_t startup_auto_selection;
static bool settings_faulted;
static bool settings_missing_on_boot;
static bool last_settings_restore_ok = true;

static bool save_persistent_settings(void);
static bool remember_active_drive8(const char *file_name);
static bool remember_active_cartridge(const char *file_name);
static bool remember_c64_input_settings(void);
static uint32_t shell_read(uint32_t offset);
static void print_read_only_diagnostics(const char *stage);
static void set_supervisor_status(const char *text);
static void set_supervisor_warning(const char *text);
static void set_supervisor_error(const char *text);
static void set_supervisor_outcome(bool succeeded, const char *success,
                                    const char *failure);
static bool hide_menu_for_core_action(void);
static void service_screenshots(void);
static void configure_selection_effects(const mt_core_selection_t *selection);
static void show_handoff_progress(const char *message);

static void refresh_multiboot_status(void)
{
    /* Entry refresh is advisory and bounded. Cold Golden verification is
     * warmed after the initial screen below; every boot action revalidates. */
    (void)mb_platform_status_ui(MB_PRODUCT_MULTITINI, &multiboot_status);
    installed_golden_version[0] = '\0';
    (void)mb_platform_golden_version_cached(installed_golden_version,
        (uint32_t)sizeof(installed_golden_version));
    supervisor_menu_dirty = true;
}

static void refresh_golden_update_file(void)
{
    mt_sd_file_t *file = NULL;
    golden_update_present = mt_sd_file_open("0:/BOOT.BIN", "rb", &file).status == MT_SD_OK;
    if (file != NULL && mt_sd_file_close(file).status != MT_SD_OK)
        golden_update_present = false;
}
static int run_vectrex_basic(void *, const char *, const char *, const char *);
static mt_genesis_content_service_result_t load_genesis_content_with_input(const char *name);
static void handle_genesis_content_load(const char *name);
static void handle_genesis_cart_detach(void);
static mt_nes_content_service_result_t load_nes_content_with_input(const char *name);
static void handle_nes_content_load(const char *name);
static void handle_nes_cart_detach(void);
static void change_genesis_controller(size_t player);

static void set_c64_cpu_turbo(bool enabled)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL,
              enabled ? control | MT_CONTROL_C64_CPU_TURBO_ENABLE :
                        control & ~MT_CONTROL_C64_CPU_TURBO_ENABLE);
    supervisor_menu_dirty = true;
}

/* The user-supplied fast ROM pair expects the DolphinDOS-style parallel
 * cable and the 8 KiB drive RAM it brings. The stock drive ROM drives the
 * same VIA port low, so the cable exists only while a fast pair is
 * installed. Shells older than ABI 0.10 ignore the bit. */
static void set_c64_drive_parallel_cable(bool enabled)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL,
              enabled ? control | MT_CONTROL_C64_DRIVE_PARALLEL_ENABLE :
                        control & ~MT_CONTROL_C64_DRIVE_PARALLEL_ENABLE);
}

static int board_i2c_write(void *context, uint8_t address, uint8_t reg,
                           uint8_t value)
{
    XIicPs *const i2c = context;
    uint8_t payload[2] = {reg, value};
    uint32_t poll;

    if (XIicPs_MasterSendPolled(i2c, payload, 2, address) != XST_SUCCESS) {
        return -1;
    }
    for (poll = 0U; poll < MT_I2C_BUSY_POLLS; ++poll) {
        if (!XIicPs_BusIsBusy(i2c)) {
            return 0;
        }
    }
    return -2;
}

static int wait_i2c_idle(XIicPs *i2c)
{
    uint32_t poll;
    for (poll = 0U; poll < MT_I2C_BUSY_POLLS; ++poll) {
        if (!XIicPs_BusIsBusy(i2c)) return 0;
    }
    return -1;
}

static int board_i2c_read(XIicPs *i2c, uint8_t address, uint8_t reg,
                          uint8_t *data, int length)
{
    if (XIicPs_MasterSendPolled(i2c, &reg, 1, address) != XST_SUCCESS ||
        wait_i2c_idle(i2c) != 0) {
        return -1;
    }
    if (XIicPs_MasterRecvPolled(i2c, data, length, address) != XST_SUCCESS ||
        wait_i2c_idle(i2c) != 0) {
        return -2;
    }
    return 0;
}

static int refresh_fatfs_time_from_rtc(void)
{
    uint8_t registers[MT_PCF8563_TIME_REGISTER_COUNT + 2U];
    if (board_i2c_read(&i2c_instance, MT_PCF8563_I2C_ADDRESS,
                       0U, registers,
                       (int)sizeof(registers)) != 0) {
        board_clock_available = board_clock_valid = false;
        return -1;
    }
    board_clock_available = true;
    board_clock_valid = (registers[0] & UINT8_C(0x20)) == 0U &&
        mt_clock_decode(registers + 2U, &board_clock);
    return board_clock_valid && mt_fatfs_time_update_from_pcf8563(registers + 2U) ? 0 : -2;
}

static bool set_board_clock(const mt_clock_time_t *time)
{
    uint8_t payload[10] = {0};
    uint8_t controls[2];
    /* Write normal running mode and all seven time registers in one transfer.
     * Preserve unrelated alarm/timer control bits. A failed transfer is
     * reported; the editor remains open for retry. */
    if (!i2c_ready || !mt_clock_encode(time, payload + 3U) ||
        board_i2c_read(&i2c_instance, MT_PCF8563_I2C_ADDRESS, 0U, controls, 2) != 0)
        return false;
    payload[1] = (uint8_t)(controls[0] & (uint8_t)~UINT8_C(0x20));
    payload[2] = controls[1];
    if (XIicPs_MasterSendPolled(&i2c_instance, payload, (int)sizeof(payload),
                              MT_PCF8563_I2C_ADDRESS) != XST_SUCCESS ||
        wait_i2c_idle(&i2c_instance) != 0) {
        board_clock_valid = false;
        return false;
    }
    return refresh_fatfs_time_from_rtc() == 0 && mt_clock_readback_matches(time, &board_clock);
}

static int service_fatfs_time(void)
{
    XTime now = 0U;
    if (!i2c_ready) return -1;
    XTime_GetTime(&now);
    if (last_fatfs_time_poll != 0U &&
        (now - last_fatfs_time_poll) < (XTime)COUNTS_PER_SECOND) {
        return 1;
    }
    last_fatfs_time_poll = now;
    return refresh_fatfs_time_from_rtc();
}

static void print_temperature(void)
{
    uint8_t raw[2];
    int32_t micro_c;
    uint32_t magnitude;
    char sign = '+';
    if (board_i2c_read(&i2c_instance, MT_TMP102_I2C_ADDRESS,
                       MT_TMP102_TEMPERATURE_REGISTER, raw, 2) != 0 ||
        mt_tmp102_decode_micro_c(raw, &micro_c) != 0) {
        xil_printf("TMP102 read failed\r\n");
        return;
    }
    if (micro_c < 0) {
        sign = '-';
        magnitude = (uint32_t)(-micro_c);
    } else {
        magnitude = (uint32_t)micro_c;
    }
    xil_printf("temperature=%c%u.%02u C\r\n", sign,
               (unsigned int)(magnitude / UINT32_C(1000000)),
               (unsigned int)((magnitude % UINT32_C(1000000)) /
                              UINT32_C(10000)));
}

static void board_delay_us(void *context, uint32_t microseconds)
{
    (void)context;
    usleep(microseconds);
}

static int initialize_i2c(void)
{
    XIicPs_Config *const config =
        XIicPs_LookupConfig(XPAR_XIICPS_0_BASEADDR);
    if (config == NULL) {
        return -1;
    }
    if (XIicPs_CfgInitialize(&i2c_instance, config, config->BaseAddress) !=
        XST_SUCCESS) {
        return -2;
    }
    return XIicPs_SetSClk(&i2c_instance, MT_I2C_CLOCK_HZ) == XST_SUCCESS ?
           0 : -3;
}

static uint32_t shell_read(uint32_t offset)
{
    return Xil_In32(MT_SHELL_BASE_ADDRESS + offset);
}

static bool shell_is_c64(void)
{
    return shell_read(MT_REG_CORE_ID) == MT_C64_CORE_ID;
}

/* This must inspect the live image, including before content services are
 * initialized. A disabled REU can still have posted DDR writes to drain.
 * Older C64 images have no DDR drain contract at VIDEO_FRAME_STATUS. */
static bool core_requires_ddr_drain(void)
{
    const uint32_t core = shell_read(MT_REG_CORE_ID);
    if (core == MT_GENESIS_CORE_ID || core == MT_NES_CORE_ID || core == MT_IIGS_CORE_ID ||
        core == MT_AMIGA_CORE_ID || core == MT_SPECTRUM_CORE_ID)
        return true;
    if (core != MT_C64_CORE_ID) return false;
    const uint32_t abi = shell_read(MT_REG_ABI_VERSION);
    if ((abi >> 16U) != MT_ABI_MAJOR || (uint16_t)abi < MT_C64_EXPANSION_MIN_ABI_MINOR)
        return false;
    const uint32_t caps = shell_read(MT_REG_C64_EXPANSION_CAPS);
    const uint32_t required = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_DDR;
    return (caps & required) == required;
}

static void service_usb_input(void)
{
    const uint32_t status = shell_read(MT_REG_STATUS);
    const uint32_t control = shell_read(MT_REG_CONTROL);
    usb_hid_service_set_core_ready(
        (status & MT_STATUS_CORE_READY) != 0U &&
        (control & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    usb_hid_service_poll();
}

static bool c64_iec_path_protected(void *context, const char *path)
{
    (void)context;
    return c64_d64_service_path_protected(path);
}

static int quiesce_c64_directory(void)
{
    if (!c64_iec_initialized || !c64_iec_service_available()) return 0;
    const int result = c64_iec_service_quiesce();
    if (c64_uci_initialized) c64_uci_service_refresh_control();
    if (result != 0)
        xil_printf("[C64] drive9 could not close safely: %s\r\n",
                   c64_iec_service_status());
    return result;
}

static int resume_c64_directory(void)
{
    if (!c64_iec_initialized || !c64_iec_service_available()) return 0;
    /* Off is a bus preference, not a storage shutdown. Repeated reset/load
     * resumes must leave UCI's shared DOS channels alone. */
    const int result = persistent_settings.c64_directory_drive_off ? 0 :
        c64_iec_service_enable(true);
    if (c64_uci_initialized) c64_uci_service_refresh_control();
    if (result != 0)
        xil_printf("[C64] drive9 could not resume: %s\r\n",
                   c64_iec_service_status());
    return result;
}

static void queue_amiga_reset_shortcut(void)
{
    if(!supervisor_menu_action_active && !supervisor_menu.visible &&
        pending_menu_action.kind==MT_MENU_ACTION_NONE &&
        shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && usb_hid_service_take_amiga_reset())
        pending_menu_action=(mt_menu_action_t){.kind=MT_MENU_ACTION_RESET_CORE};
}
static void service_runtime(void)
{
    static bool uci_fault_reported, uci_cleanup_reported;
    const uint32_t control = shell_read(MT_REG_CONTROL);
    const bool c64_active = shell_is_c64();
    const bool gameboy_active =
        shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID;
    (void)service_fatfs_time();
    if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized) {
        static mt_link_result_t last_rtc_result;
        const uint32_t now_ms=(uint32_t)monotonic_ms(NULL);
        const bool menu_paused=mt_ui_status_menu_shown(shell_read(MT_REG_UI_STATUS));
        amiga_display_service_set_paused(menu_paused,now_ms);
        if(!menu_paused) {
            const mt_link_result_t result=amiga_content_service_set_clock(board_clock_valid ? &board_clock:NULL,now_ms);
            if(result!=last_rtc_result) {
                xil_printf("[Amiga] RTC refresh: %s\r\n",mt_link_result_name(result));last_rtc_result=result;
            }
        }
        const bool display_was_pending=amiga_display_service_pending();
        const bool display_was_available=amiga_display_service_available();
        amiga_display_service_poll(now_ms);
        if(display_was_pending!=amiga_display_service_pending() ||
           display_was_available!=amiga_display_service_available())supervisor_menu_dirty=true;
    }
    service_usb_input();
    /* HID callbacks only latch an edge. Queue the existing deferred action;
     * it drains disk writes and uses the ordinary core reset lifecycle. */
    queue_amiga_reset_shortcut();
    {
        static const char *last_effect_status;
        const char *status;
        mt_video_effect_service_poll();
        status = mt_video_effect_service_status();
        if (status != last_effect_status) {
            last_effect_status = status;
            supervisor_menu_dirty = true;
        }
    }
    if ((control & MT_CONTROL_CORE_RESET_REQUEST) != 0U) {
        if (c64_active && c64_services_initialized &&
            !c64_core_reset_active) {
            c64_d64_service_notify_core_reset();
            if (c64_uci_initialized)
                c64_uci_service_reset();
            mt_net_service_guest_reset();
            if (c64_iec_initialized && c64_iec_service_reset() != 0)
                xil_printf("[C64] drive9 reset recovery failed: %s\r\n",
                           c64_iec_service_status());
        }
        c64_core_reset_active = true;
        return;
    }
    if (c64_active && c64_services_initialized && c64_core_reset_active &&
        c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] drive8 remount after reset failed; retry with "
                   ":diskretry\r\n");
    }
    if (c64_active && c64_core_reset_active) (void)resume_c64_directory();
    c64_core_reset_active = false;
    if (c64_active && c64_services_initialized) {
        c64_d64_service_poll();
        c64_content_service_poll();
        if (c64_iec_initialized && c64_iec_service_available() &&
            c64_iec_service_active()) {
            const bool faulted = c64_iec_service_poll(32U) < 0;
            if (faulted && !c64_iec_fault_displayed)
                xil_printf("[C64] drive9 transport error: %s\r\n",
                           c64_iec_service_status());
            c64_iec_fault_displayed = faulted;
        }
        mt_net_service_poll();
        if (!c64_title_foreground && c64_uci_initialized) {
            const bool faulted = c64_uci_service_poll(32U) < 0;
            const bool cleanup = c64_uci_service_cleanup_warning();
            if (faulted && !uci_fault_reported)
                xil_printf("[C64] command interface fault: %s\r\n",
                    c64_uci_service_reload_required() ? "reload the C64 core" :
                                                       "retry with Reset or :uciretry");
            if (cleanup && !uci_cleanup_reported)
                xil_printf("[C64] temporary file cleanup incomplete; check SD and use :uciretry\r\n");
            uci_fault_reported = faulted;
            uci_cleanup_reported = cleanup;
            service_c64_uci_action();
        }
    }
    if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized) {
        amiga_content_service_poll();
        if(amiga_fault_displayed!=amiga_content_service_faulted()) {
            amiga_fault_displayed=amiga_content_service_faulted();supervisor_menu_dirty=true;
        }
    }
    if(shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID && iigs_service_initialized) {
        iigs_content_service_poll();
        if(iigs_fault_displayed!=iigs_content_service_faulted()) {
            iigs_fault_displayed=iigs_content_service_faulted(); supervisor_menu_dirty=true;
        }
    }
    if (gameboy_active && gameboy_service_initialized) {
        bool dirty;
        bool faulted;
        gameboy_content_service_poll();
        dirty = gameboy_content_service_save_dirty();
        faulted = gameboy_content_service_save_faulted();
        if (dirty != gameboy_save_dirty_displayed ||
            faulted != gameboy_save_fault_displayed) {
            gameboy_save_dirty_displayed = dirty;
            gameboy_save_fault_displayed = faulted;
            supervisor_menu_dirty = true;
        }
    }
    if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID && genesis_service_initialized) {
        bool dirty;
        bool faulted;
        genesis_content_service_poll();
        dirty = genesis_content_service_save_dirty();
        faulted = genesis_content_service_save_faulted();
        if (dirty != genesis_save_dirty_displayed ||
            faulted != genesis_save_fault_displayed) {
            genesis_save_dirty_displayed = dirty;
            genesis_save_fault_displayed = faulted;
            supervisor_menu_dirty = true;
        }
    }
    if (shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID && nes_service_initialized) {
        bool dirty;
        bool faulted;
        nes_content_service_poll();
        dirty = nes_content_service_save_dirty();
        faulted = nes_content_service_save_faulted();
        if (dirty != nes_save_dirty_displayed ||
            faulted != nes_save_fault_displayed) {
            nes_save_dirty_displayed = dirty;
            nes_save_fault_displayed = faulted;
            supervisor_menu_dirty = true;
        }
    }
}

static void print_status(void)
{
    xil_printf("magic=%08x abi=%08x shell=%08x build=%08x\r\n",
               (unsigned int)shell_read(MT_REG_MAGIC),
               (unsigned int)shell_read(MT_REG_ABI_VERSION),
               (unsigned int)shell_read(MT_REG_SHELL_ID),
               (unsigned int)shell_read(MT_REG_SHELL_BUILD_ID));
    xil_printf("core=%08x status=%08x error=%08x control=%08x\r\n",
               (unsigned int)shell_read(MT_REG_CORE_ID),
               (unsigned int)shell_read(MT_REG_STATUS),
               (unsigned int)shell_read(MT_REG_ERROR),
               (unsigned int)shell_read(MT_REG_CONTROL));
}

static int command(uint32_t opcode, uint32_t arg0, uint32_t arg1)
{
    const uint32_t args[4] = {arg0, arg1, 0U, 0U};
    mt_command_response_t response;
    const mt_link_result_t result = mt_core_link_command(
        &shell_link, opcode, args, MT_LINK_COMMAND_POLLS, NULL, NULL,
        &response);
    if (result != MT_LINK_OK) {
        xil_printf("command failed: %s", mt_link_result_name(result));
        if (result == MT_LINK_REMOTE_ERROR) {
            xil_printf(" status=%u", (unsigned int)response.status);
        }
        xil_printf("\r\n");
        return -1;
    }
    xil_printf("response seq=%u arg0=%08x arg1=%08x\r\n",
               (unsigned int)response.sequence,
               (unsigned int)response.arg0,
               (unsigned int)response.arg1);
    return 0;
}

static void set_video_test(bool enabled)
{
    uint32_t control = shell_read(MT_REG_CONTROL);
    if (enabled)
        control |= MT_CONTROL_VIDEO_TEST_ENABLE;
    else
        control &= ~MT_CONTROL_VIDEO_TEST_ENABLE;
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL, control);
}

static void set_audio(bool mute, bool test_tone)
{
    int status;
    if (mute) {
        (void)command(MT_COMMAND_SET_AUDIO, test_tone ? 1U : 0U, 1U);
        board_delay_us(NULL, 50U);
        if (dac_ready) {
            status = mt_ak4493_set_mute(&dac_bus, true);
            if (status != 0) {
                xil_printf("DAC mute failed: %d\r\n", status);
            }
        }
        return;
    }

    if (!dac_ready) {
        xil_printf("DAC is unavailable; audio remains muted\r\n");
        return;
    }
    if (command(MT_COMMAND_SET_AUDIO, test_tone ? 1U : 0U, 0U) != 0) {
        return;
    }
    // Let at least two complete 48 kHz PCM frames reach the pins before the
    // converter's analog outputs leave soft mute.
    board_delay_us(NULL, 50U);
    status = mt_ak4493_set_mute(&dac_bus, false);
    if (status != 0) {
        xil_printf("DAC unmute failed: %d\r\n", status);
        (void)command(MT_COMMAND_SET_AUDIO, test_tone ? 1U : 0U, 1U);
    }
}

static uint8_t hex_digit_value(char input, uint8_t *value)
{
    if (input >= '0' && input <= '9') {
        *value = (uint8_t)(input - '0');
        return 1U;
    }
    if (input >= 'a' && input <= 'f') {
        *value = (uint8_t)(input - 'a' + 10);
        return 1U;
    }
    if (input >= 'A' && input <= 'F') {
        *value = (uint8_t)(input - 'A' + 10);
        return 1U;
    }
    return 0U;
}

static int parse_u32(const char *text, uint32_t *value_out)
{
    uint32_t base = 10U;
    uint64_t value = 0U;
    uint32_t digits = 0U;

    if (text == NULL || *text == '\0' || value_out == NULL) return -1;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16U;
        text += 2;
    }
    while (*text != '\0') {
        uint8_t digit;
        if (hex_digit_value(*text, &digit) == 0U || digit >= base) return -1;
        value = value * base + digit;
        if (value > UINT32_MAX) return -1;
        ++digits;
        ++text;
    }
    if (digits == 0U) return -1;
    *value_out = (uint32_t)value;
    return 0;
}

static int split_arguments(char *line, char **arguments, int capacity)
{
    char *cursor = line;
    int count = 0;

    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        if (count >= capacity) return -1;
        arguments[count++] = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
            ++cursor;
        }
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return count;
}

static XTime ticks_from_ms(uint32_t milliseconds)
{
    return (XTime)(((uint64_t)COUNTS_PER_SECOND * milliseconds) /
                   UINT64_C(1000));
}

static uint64_t monotonic_ms(void *context)
{
    XTime now = 0U;
    const uint64_t counts_per_second = (uint64_t)COUNTS_PER_SECOND;
    (void)context;
    XTime_GetTime(&now);
    /* Convert before multiplying so the 64-bit global timer remains usable
     * well beyond the first ~15 hours of uptime. */
    return ((uint64_t)now / counts_per_second) * UINT64_C(1000) +
           (((uint64_t)now % counts_per_second) * UINT64_C(1000)) /
               counts_per_second;
}

static int uart0_try_read_byte(uint8_t *value)
{
    const uint32_t status = XUartPs_ReadReg(
        XPAR_XUARTPS_0_BASEADDR, XUARTPS_SR_OFFSET);
    if (value == NULL) return -1;
    if ((status & XUARTPS_SR_RXEMPTY) != 0U) return 0;
    *value = (uint8_t)XUartPs_ReadReg(
        XPAR_XUARTPS_0_BASEADDR, XUARTPS_FIFO_OFFSET);
    return 1;
}

static int uart0_read_byte(void *context, uint8_t *value,
                           uint32_t timeout_ms)
{
    XTime start = 0U;
    XTime now = 0U;
    const XTime timeout_ticks = ticks_from_ms(timeout_ms);
    (void)context;

    if (value == NULL) return -1;
    XTime_GetTime(&start);
    do {
        const int read_result = uart0_try_read_byte(value);
        if (read_result != 0) return read_result;
        service_usb_input();
        XTime_GetTime(&now);
    } while ((now - start) < timeout_ticks);
    return 0;
}

static int uart0_write_byte(void *context, uint8_t value)
{
    uint32_t poll;
    (void)context;

    for (poll = 0U; poll < MT_UART_DRAIN_POLLS; ++poll) {
        const uint32_t status = XUartPs_ReadReg(
            XPAR_XUARTPS_0_BASEADDR, XUARTPS_SR_OFFSET);
        if ((status & XUARTPS_SR_TXFULL) == 0U) {
            XUartPs_WriteReg(XPAR_XUARTPS_0_BASEADDR, XUARTPS_FIFO_OFFSET,
                             value);
            return 0;
        }
    }
    return -1;
}

static int set_core_reset_request(bool asserted)
{
    const bool currently_asserted =
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) != 0U;
    const bool c64_active = shell_is_c64();
    const bool gameboy_active =
        shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID;
    int result;

    if (!asserted && c64_active && c64_uci_service_reload_required()) {
        xil_printf("[C64] reset remains asserted; reload the C64 core\r\n");
        return -1;
    }
    if (!hide_menu_for_core_action()) return -1;
    if (asserted && !currently_asserted && c64_active && retry_c64_uci() != 0)
        return -1;
    if (asserted && !currently_asserted && c64_active &&
        quiesce_c64_directory() != 0) return -1;
    if (asserted && !currently_asserted && c64_active &&
        c64_services_initialized &&
        c64_d64_service_prepare_reset() != 0) {
        xil_printf("[C64] reset refused; drive8 could not flush/eject safely\r\n");
        (void)resume_c64_directory();
        return -1;
    }
    if (asserted && !currently_asserted && gameboy_active &&
        gameboy_service_initialized &&
        gameboy_content_service_prepare_reset() != 0) {
        xil_printf("[GBC] reset refused; save RAM could not be committed\r\n");
        return -1;
    }
    if (!asserted && currently_asserted && gameboy_active &&
        gameboy_service_initialized &&
        gameboy_content_service_resume_after_reset() != 0) {
        xil_printf("[GBC] reset release refused; save RAM remount failed\r\n");
        return -1;
    }
    if (asserted && !currently_asserted && shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID &&
        genesis_service_initialized &&
        genesis_content_service_prepare_reset() != 0) {
        xil_printf("[MD] reset refused; save RAM could not be committed\r\n");
        return -1;
    }
    if (!asserted && currently_asserted && shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID &&
        genesis_service_initialized &&
        genesis_content_service_resume_after_reset() != 0) {
        xil_printf("[MD] reset release refused; save RAM remount failed\r\n");
        return -1;
    }
    if (asserted && !currently_asserted && shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID &&
        nes_service_initialized &&
        nes_content_service_prepare_reset() != 0) {
        xil_printf("[NES] reset refused; save RAM could not be committed\r\n");
        return -1;
    }
    if (!asserted && currently_asserted && shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID &&
        nes_service_initialized &&
        nes_content_service_resume_after_reset() != 0) {
        xil_printf("[NES] reset release refused; save RAM remount failed\r\n");
        return -1;
    }
    if(asserted && !currently_asserted && shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID &&
        amiga_service_initialized && amiga_content_service_prepare_reset()!=0)return -1;
    if(!asserted && currently_asserted && shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID &&
        amiga_service_initialized && amiga_content_service_resume_after_reset()!=0)return -1;
    if(asserted && !currently_asserted && shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID &&
        iigs_service_initialized && iigs_content_service_prepare_reset()!=0)return -1;
    if(!asserted && currently_asserted && shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID &&
        iigs_service_initialized && iigs_content_service_resume_after_reset()!=0)return -1;
    result = command(MT_COMMAND_CORE_RESET, asserted ? 1U : 0U, 0U);
    if(result==0 && !asserted && currently_asserted &&
       shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized)
        amiga_display_guest_restarted();
    /* A synchronous assert/release can finish before the USB service observes
     * reset. Invalidate the cached Vectrex delivery state explicitly. */
    if (result == 0 && asserted && (shell_read(MT_REG_CORE_ID) == MT_VECTREX_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_IIGS_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_SPECTRUM_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_AMIGA_CORE_ID))
        usb_hid_service_set_core_ready(false);
    if (result != 0 && asserted && !currently_asserted &&
        c64_active && c64_services_initialized) {
        (void)c64_d64_service_resume_after_reset();
        (void)resume_c64_directory();
    } else if(result!=0 && asserted && !currently_asserted &&
        shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID && iigs_service_initialized) {
        (void)iigs_content_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               gameboy_active && gameboy_service_initialized) {
        (void)gameboy_content_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID && genesis_service_initialized) {
        (void)genesis_content_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID && nes_service_initialized) {
        (void)nes_content_service_resume_after_reset();
    } else if (result == 0 && asserted && !currently_asserted &&
               c64_active && c64_services_initialized) {
        /* Record the transition synchronously. Some reset users perform the
         * complete assert/load/release sequence without calling
         * service_runtime() while the reset level is high. */
        c64_d64_service_notify_core_reset();
        if (c64_iec_initialized && c64_iec_service_reset() != 0) return -1;
        if (c64_uci_initialized)
            c64_uci_service_reset();
        mt_net_service_guest_reset();
        c64_core_reset_active = true;
    }
    if (result == 0 && !asserted && c64_active &&
        resume_c64_directory() != 0) return -1;
    return result;
}

static void leave_core_in_reset(void)
{
    if (set_core_reset_request(true) != 0) {
        xil_printf("[C64] error: core reset request failed\r\n");
    }
}

static bool validate_c64_rom_slice(mt_c64_rom_kind_t kind,
                                   const uint8_t *data, size_t size,
                                   const char *name)
{
    const mt_content_validation_t validation =
        mt_c64_rom_validate(kind, data, size);
    if (validation == MT_CONTENT_VALID) return true;
    xil_printf("[C64] error: %s validation: %s\r\n", name,
               mt_content_validation_name(validation));
    return false;
}

static bool wait_for_c64_core_ready(uint32_t *status_out)
{
    uint32_t poll;
    uint32_t status = 0U;

    for (poll = 0U; poll < MT_CORE_READY_POLLS; ++poll) {
        status = shell_read(MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U) break;
        if ((status & MT_STATUS_CORE_READY) != 0U) {
            if (status_out != NULL) *status_out = status;
            return true;
        }
    }
    if (status_out != NULL) *status_out = status;
    return false;
}

static mt_c64_rom_file_read_result_t read_c64_sd_rom(
    void *context, const char *path, uint8_t *destination,
    size_t expected_size)
{
    const mt_sd_result_t sd_result = mt_sd_storage_read_file_exact(
        path, destination, expected_size);
    mt_c64_rom_file_read_result_t result;
    (void)context;
    result.status = (int)sd_result.status;
    result.backend_status = sd_result.backend_status;
    result.size = sd_result.size;
    return result;
}

static void print_c64_sd_fallback(void)
{
    xil_printf("[C64] core held in reset; install ROMs under %s or use "
               ":c64rx 20480 0xCRC32\r\n", MT_C64_SD_ROM_ROOT);
}

static void print_c64_rom_file_error(const char *set_name,
                                     const mt_c64_rom_files_result_t *result)
{
    const mt_sd_status_t read_status = (mt_sd_status_t)result->read.status;
    xil_printf("[C64] %s ROM load failed status=%s path=%s read=%s "
               "backend=%u size=%u validation=%s\r\n",
               set_name,
               mt_c64_rom_files_status_name(result->status),
               result->path == NULL ? "(none)" : result->path,
               mt_sd_status_name(read_status), result->read.backend_status,
               (unsigned int)result->read.size,
               mt_content_validation_name(result->validation));
}

static bool read_c64_rom_profile(bool fast, bool require_drive,
                                 bool *drive_available)
{
    const mt_c64_rom_paths_t *const paths = fast ?
        mt_c64_fast_rom_paths() : mt_c64_stock_rom_paths();
    const char *const set_name = fast ? "fast-pair" : "stock";
    mt_c64_rom_files_result_t result;
    mt_c64_system_roms_t roms;

    if (drive_available == NULL) return false;
    *drive_available = false;
    result = mt_c64_rom_files_load_system_from(
        paths, read_c64_sd_rom, NULL, c64_candidate_rom_bundle,
        sizeof(c64_candidate_rom_bundle), &roms);
    if (result.status != MT_C64_ROM_FILES_OK) {
        print_c64_rom_file_error(set_name, &result);
        return false;
    }

    result = mt_c64_rom_file_load(
        MT_C64_ROM_DRIVE_1541, paths->drive_1541, read_c64_sd_rom, NULL,
        c64_candidate_drive_rom, sizeof(c64_candidate_drive_rom));
    if (result.status != MT_C64_ROM_FILES_OK) {
        print_c64_rom_file_error(set_name, &result);
        return !require_drive;
    }
    *drive_available = true;
    return true;
}

static bool stream_c64_rom_profile(const uint8_t *system_bundle,
                                   const uint8_t *drive_rom,
                                   bool drive_available, bool fast,
                                   const char *set_name,
                                   uint32_t *core_status)
{
    mt_c64_system_roms_t roms;
    mt_link_result_t load_result;

    roms.basic.data = system_bundle;
    roms.basic.size = MT_SIZE_C64_BASIC;
    roms.kernal.data = system_bundle + MT_SIZE_C64_BASIC;
    roms.kernal.size = MT_SIZE_C64_KERNAL;
    roms.character.data = system_bundle + MT_SIZE_C64_BASIC +
                          MT_SIZE_C64_KERNAL;
    roms.character.size = MT_SIZE_C64_CHARACTER;

    /* Decided here, beside the ROM streams, so the cable can never disagree
     * with the installed pair. Every caller holds the core in reset. */
    set_c64_drive_parallel_cable(fast && drive_available);
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    if (drive_available) {
        load_result = mt_core_link_stream(
            &shell_link, MT_LOAD_C64_1541_ROM, drive_rom,
            MT_SIZE_C64_1541_ROM, MT_LINK_COMMAND_POLLS, NULL, NULL);
        if (load_result != MT_LINK_OK) {
            xil_printf("[C64] %s drive ROM stream failed: %s\r\n",
                       set_name, mt_link_result_name(load_result));
            return false;
        }
    }

    load_result = mt_c64_load_system_roms(
        &shell_link, &roms, MT_LINK_COMMAND_POLLS, NULL, NULL);
    if (load_result != MT_LINK_OK) {
        leave_core_in_reset();
        xil_printf("[C64] %s system ROM stream failed: %s\r\n",
                   set_name, mt_link_result_name(load_result));
        return false;
    }
    if (!wait_for_c64_core_ready(core_status)) {
        leave_core_in_reset();
        xil_printf("[C64] %s core-ready timeout status=%08x\r\n",
                   set_name,
                   (unsigned int)(core_status == NULL ? 0U : *core_status));
        return false;
    }
    return resume_c64_directory() == 0;
}

static void remember_active_c64_rom_profile(bool fast, bool drive_available)
{
    memcpy(c64_rom_bundle, c64_candidate_rom_bundle,
           sizeof(c64_rom_bundle));
    if (drive_available) {
        memcpy(c64_drive_rom, c64_candidate_drive_rom,
               sizeof(c64_drive_rom));
    }
    c64_rom_profile_active_valid = true;
    c64_active_drive_rom_available = drive_available;
    c64_fast_roms_active = fast;
}

static c64_rom_restore_result_t restore_active_c64_rom_profile(void)
{
    uint32_t core_status = 0U;

    if (!c64_rom_profile_active_valid || set_core_reset_request(true) != 0) {
        return C64_ROM_RESTORE_FAILED;
    }
    if (!stream_c64_rom_profile(
            c64_rom_bundle, c64_drive_rom,
            c64_active_drive_rom_available, c64_fast_roms_active,
            c64_fast_roms_active ? "fast-pair rollback" : "stock rollback",
            &core_status)) {
        leave_core_in_reset();
        xil_printf("[C64] ROM rollback failed; core held in reset\r\n");
        return C64_ROM_RESTORE_FAILED;
    }
    c64_drive_ready = c64_active_drive_rom_available;
    if (c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] ROM rollback succeeded; drive8 remount pending\r\n");
        c64_core_reset_active = false;
        return C64_ROM_RESTORE_MEDIA_PENDING;
    }
    c64_core_reset_active = false;
    return C64_ROM_RESTORE_OK;
}

static bool attempt_c64_sd_boot(bool request_fast_roms)
{
    mt_sd_result_t mount_result;
    uint32_t core_status = 0U;
    uint32_t actual_crc32;
    bool drive_available = false;
    bool fast = request_fast_roms;

    mount_result = mt_sd_storage_mount();
    xil_printf("[SD] mount status=%s backend=%u\r\n",
               mt_sd_status_name(mount_result.status),
               mount_result.backend_status);
    if (mount_result.status != MT_SD_OK) {
        print_c64_sd_fallback();
        return false;
    }

    c64_fast_roms_fallback = false;
    if (fast) {
        if (!read_c64_rom_profile(true, true, &drive_available)) {
            xil_printf("[C64] fast ROM pair unavailable; using stock ROMs\r\n");
            c64_fast_roms_fallback = true;
            fast = false;
            if (!read_c64_rom_profile(false, false, &drive_available)) {
                print_c64_sd_fallback();
                return false;
            }
        }
    } else if (!read_c64_rom_profile(false, false, &drive_available)) {
        print_c64_sd_fallback();
        return false;
    }

    if (set_core_reset_request(true) != 0) {
        xil_printf("[C64] error: cannot enter reset for SD ROM load\r\n");
        return false;
    }
    if (!stream_c64_rom_profile(
            c64_candidate_rom_bundle, c64_candidate_drive_rom,
            drive_available, fast, fast ? "fast-pair" : "stock",
            &core_status)) {
        if (!fast) {
            print_c64_sd_fallback();
            return false;
        }
        xil_printf("[C64] fast ROM pair could not start; using stock ROMs\r\n");
        c64_fast_roms_fallback = true;
        /* The candidate drive ROM may already be live. A fallback after a
         * failed fast start must overwrite it with a complete stock pair;
         * otherwise stock system ROMs could run beside the fast drive ROM. */
        if (!read_c64_rom_profile(false, true, &drive_available) ||
            !stream_c64_rom_profile(
                c64_candidate_rom_bundle, c64_candidate_drive_rom,
                drive_available, false, "stock", &core_status)) {
            print_c64_sd_fallback();
            return false;
        }
        fast = false;
    }

    remember_active_c64_rom_profile(fast, drive_available);
    c64_drive_ready = drive_available;
    set_video_test(false);
    actual_crc32 = mt_xmodem_crc32_ieee(
        c64_rom_bundle, sizeof(c64_rom_bundle));
    xil_printf("[C64] SD boot ok ROMs=%s root=%s crc32=%08x status=%08x\r\n",
               fast ? "fast-pair" : "stock", MT_C64_SD_ROM_ROOT,
               (unsigned int)actual_crc32, (unsigned int)core_status);
    return true;
}

/* Load a candidate pair without replacing the in-memory rollback image. The
 * caller commits candidate buffers only after its settings write succeeds. */
static c64_rom_stage_result_t stage_c64_rom_profile(bool fast)
{
    c64_rom_restore_result_t restore_result;
    uint32_t core_status = 0U;
    bool drive_available = false;

    /* A live switch is committed only after its settings write succeeds. The
     * current profile therefore needs a complete drive ROM as well as system
     * ROMs so every failure path can restore a full, non-mixed pair. Stock
     * system-only boot remains supported; it simply cannot be a rollback
     * source for a live ROM-pair switch. */
    if (!c64_rom_profile_active_valid ||
        !c64_active_drive_rom_available) {
        return C64_ROM_STAGE_NO_ROLLBACK_PAIR;
    }
    if (!read_c64_rom_profile(fast, true, &drive_available))
        return C64_ROM_STAGE_PROFILE_UNAVAILABLE;
    if (set_core_reset_request(true) != 0) {
        if (c64_d64_service_resume_after_reset() != 0)
            return C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING;
        return C64_ROM_STAGE_SWITCH_FAILED;
    }
    if (stream_c64_rom_profile(
            c64_candidate_rom_bundle, c64_candidate_drive_rom, true, fast,
            fast ? "fast-pair" : "stock", &core_status)) {
        c64_drive_ready = true;
        if (c64_d64_service_resume_after_reset() != 0) {
            xil_printf("[C64] ROM switch drive8 remount failed; restoring "
                       "previous ROM pair\r\n");
            restore_result = restore_active_c64_rom_profile();
            if (restore_result == C64_ROM_RESTORE_FAILED)
                return C64_ROM_STAGE_ROLLBACK_FAILED;
            if (restore_result == C64_ROM_RESTORE_MEDIA_PENDING)
                return C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING;
            return C64_ROM_STAGE_MEDIA_FAILED;
        }
        c64_core_reset_active = false;
        return C64_ROM_STAGE_OK;
    }

    xil_printf("[C64] ROM switch failed; restoring previous ROM pair\r\n");
    restore_result = restore_active_c64_rom_profile();
    if (restore_result == C64_ROM_RESTORE_FAILED)
        return C64_ROM_STAGE_ROLLBACK_FAILED;
    if (restore_result == C64_ROM_RESTORE_MEDIA_PENDING)
        return C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING;
    return C64_ROM_STAGE_SWITCH_FAILED;
}

static void handle_c64_rx_command(char *line)
{
    char *arguments[MT_EXTENDED_ARGUMENT_CAPACITY];
    const int argument_count = split_arguments(
        line, arguments, MT_EXTENDED_ARGUMENT_CAPACITY);
    uint32_t requested_size = 0U;
    uint32_t expected_crc32 = 0U;
    uint32_t actual_crc32;
    uint32_t core_status = 0U;
    size_t received_size = 0U;
    mt_xmodem_result_t receive_result;
    mt_link_result_t load_result;
    mt_c64_system_roms_t roms;
    mt_xmodem_io_t io;

    if (argument_count != 3 || strcmp(arguments[0], "c64rx") != 0 ||
        parse_u32(arguments[1], &requested_size) != 0 ||
        parse_u32(arguments[2], &expected_crc32) != 0 ||
        requested_size != (uint32_t)sizeof(c64_rom_bundle)) {
        leave_core_in_reset();
        xil_printf("[C64] error: usage :c64rx 20480 0xXXXXXXXX\r\n");
        return;
    }

    if (set_core_reset_request(true) != 0) {
        xil_printf("[C64] error: cannot enter reset for upload\r\n");
        return;
    }
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_ERROR, MT_ERROR_CORE_LOAD);

    io.context = NULL;
    io.read_byte = uart0_read_byte;
    io.write_byte = uart0_write_byte;
    xil_printf("[C64] XMODEM-CRC ready\r\n");
    receive_result = mt_xmodem_receive_1k(
        &io, c64_rom_bundle, sizeof(c64_rom_bundle),
        MT_UART_BYTE_TIMEOUT_MS, MT_UART_XMODEM_RETRIES, &received_size);
    if (receive_result != MT_XMODEM_OK ||
        received_size != sizeof(c64_rom_bundle)) {
        leave_core_in_reset();
        xil_printf("[C64] error: receive %s count=%u\r\n",
                   mt_xmodem_result_name(receive_result),
                   (unsigned int)received_size);
        return;
    }

    actual_crc32 = mt_xmodem_crc32_ieee(c64_rom_bundle,
                                         sizeof(c64_rom_bundle));
    if (actual_crc32 != expected_crc32) {
        leave_core_in_reset();
        xil_printf("[C64] error: CRC32 expected=%08x actual=%08x\r\n",
                   (unsigned int)expected_crc32,
                   (unsigned int)actual_crc32);
        return;
    }

    if (!validate_c64_rom_slice(MT_C64_ROM_BASIC, c64_rom_bundle,
                                MT_SIZE_C64_BASIC, "BASIC") ||
        !validate_c64_rom_slice(
            MT_C64_ROM_KERNAL, c64_rom_bundle + MT_SIZE_C64_BASIC,
            MT_SIZE_C64_KERNAL, "KERNAL") ||
        !validate_c64_rom_slice(
            MT_C64_ROM_CHARACTER,
            c64_rom_bundle + MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL,
            MT_SIZE_C64_CHARACTER, "character")) {
        leave_core_in_reset();
        return;
    }

    roms.basic.data = c64_rom_bundle;
    roms.basic.size = MT_SIZE_C64_BASIC;
    roms.kernal.data = c64_rom_bundle + MT_SIZE_C64_BASIC;
    roms.kernal.size = MT_SIZE_C64_KERNAL;
    roms.character.data =
        c64_rom_bundle + MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL;
    roms.character.size = MT_SIZE_C64_CHARACTER;
    load_result = mt_c64_load_system_roms(
        &shell_link, &roms, MT_LINK_COMMAND_POLLS, NULL, NULL);
    if (load_result != MT_LINK_OK) {
        leave_core_in_reset();
        xil_printf("[C64] error: ROM stream %s\r\n",
                   mt_link_result_name(load_result));
        return;
    }
    if (!wait_for_c64_core_ready(&core_status)) {
        leave_core_in_reset();
        xil_printf("[C64] error: core-ready timeout status=%08x\r\n",
                   (unsigned int)core_status);
        return;
    }

    xil_printf("[C64] boot ok crc32=%08x status=%08x\r\n",
               (unsigned int)actual_crc32, (unsigned int)core_status);
    set_audio(false, false);
}

static void print_help(void)
{
    xil_printf("commands: h help, o menu, s status, p ping, a live audio, ");
    xil_printf("t test tone, m mute, ");
    xil_printf("v toggle video, r toggle core reset, u USB status, ");
    xil_printf(":cores list, :core ID [VARIANT] select, :usbsd export SD, :shot [source|output]\r\n");
    if(supervisor.has_active && supervisor.active.package.core_id==MT_AMIGA_CORE_ID) {
        xil_printf("Amiga: :disk status, :diskretry retries I/O, :romload retries roms/kick.rom\r\n");
        xil_printf("       :amigadisplay status, :amigadisplay 0..3 selects a supported Workbench mode\r\n");
        xil_printf("F12 opens Media and CPU options; q or Escape exits USB SD export\r\n");return;
    }
    if(supervisor.has_active && supervisor.active.package.core_id==MT_IIGS_CORE_ID) {
        xil_printf("F12 opens the Multitini menu\r\n");
        xil_printf("Apple IIgs: :disk or :dir status, :diskretry or :saveretry retry SD I/O\r\n");
        xil_printf("Mount/eject/ROM/PRAM actions are in Media; controllers are in Input\r\n");
        xil_printf("Keyboard: Ctrl+F11 warm reset, Ctrl+Alt+F11 cold reset\r\n");
        xil_printf("Recovery: :reboot to golden, :selfupdate stage BOOT.BIN\r\n");
        return;
    }
    xil_printf("j swap joystick port, k toggle keyboard map\r\n");
    xil_printf("US map: Alt/GUI=C=, F9=up-arrow, F10=+, F11=RESTORE; ");
    xil_printf("F12 opens the Multitini menu\r\n");
    xil_printf("extended: :load FILE.prg|FILE.crt, :cartoff, ");
    xil_printf(":mount FILE.d64|FILE.g64, :eject, :dir, :disk, ");
    xil_printf(":diskretry, :saveretry\r\n");
    xil_printf("C64 directory drive: :disk9 status, :disk9retry retry storage and return home\r\n");
    xil_printf("C64 command interface: :uciretry retry storage cleanup; reload core after a DMA fault\r\n");
    xil_printf("Genesis: :load FILE.md|FILE.gen|FILE.bin|FILE.smd, :cartoff\r\n");
    xil_printf("NES: :load FILE.nes, :cartoff, :saveretry\r\n");
    xil_printf("recovery: :c64rx 20480 0xCRC32, :reboot to golden\r\n");
}

static void print_c64_content_result(
    const char *operation, const char *file_name,
    const mt_c64_content_service_result_t *result)
{
    if (result->status == MT_C64_CONTENT_SERVICE_OK) {
        xil_printf("[C64] %s %s ok", operation,
                   file_name == NULL ? "" : file_name);
        if (file_name != NULL &&
            mt_content_classify(file_name) == MT_CONTENT_CRT) {
            xil_printf(" hardware=%s (%u)",
                       mt_c64_crt_hardware_name(
                           result->content.hardware_type),
                       (unsigned int)result->content.hardware_type);
        }
        xil_printf("\r\n");
        return;
    }
    xil_printf("[C64] %s %s failed: %s", operation,
               file_name == NULL ? "" : file_name,
               c64_content_service_status_name(result->status));
    if (result->status == MT_C64_CONTENT_SERVICE_READ_FAILED) {
        xil_printf(" sd=%s backend=%u size=%u",
                   mt_sd_status_name(result->sd.status),
                   result->sd.backend_status,
                   (unsigned int)result->sd.size);
    } else if (result->status == MT_C64_CONTENT_SERVICE_REJECTED) {
        xil_printf(" reason=%s validation=%s hardware=%s (%u) subtype=%u",
                   mt_c64_content_status_name(result->content.status),
                   mt_content_validation_name(result->content.validation),
                   mt_c64_crt_hardware_name(result->content.hardware_type),
                   (unsigned int)result->content.hardware_type,
                   (unsigned int)result->content.subtype);
        if (result->content.status == MT_C64_CONTENT_UNSUPPORTED_CRT &&
            result->content.hardware_type == MT_CRT_FINAL_CARTRIDGE_3 &&
            !(c64_content_service_expansion_caps() & MT_C64_EXPANSION_CAPS_CART_COEXIST))
            xil_printf("; install the C64 FPGA image with cartridge coexistence support");
    } else if (result->status ==
               MT_C64_CONTENT_SERVICE_TRANSFER_FAILED) {
        xil_printf(" link=%s", mt_link_result_name(result->link));
    }
    xil_printf("\r\n");
}

static bool prepare_c64_content_change(void)
{
    const char *disk = c64_d64_service_mounted_name();
    (void)snprintf(c64_launch_disk_name, sizeof(c64_launch_disk_name), "%s", disk ? disk : "");
    if (shell_read(MT_REG_CORE_ID) != MT_C64_CORE_ID) {
        xil_printf("[C64] content commands require the C64 core\r\n");
        return false;
    }
    if (quiesce_c64_uci() != 0)
        return false;
    if (!hide_menu_for_core_action()) return false;
    if (quiesce_c64_directory() != 0) return false;
    if (c64_d64_service_prepare_reset() != 0) {
        xil_printf("[C64] content change refused; drive8 could not "
                   "flush/eject safely\r\n");
        (void)resume_c64_directory();
        return false;
    }
    c64_title_foreground = true;
    return true;
}

static bool finish_c64_content_change(void)
{
    c64_title_foreground = false;
    if (c64_iec_initialized && c64_iec_service_reset() != 0) return false;
    const bool directory_ready = resume_c64_directory() == 0;
    if (c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] content change completed, but drive8 remount "
                   "failed; retry with :diskretry\r\n");
        return false;
    }
    return directory_ready;
}

static int mount_c64_disk(const char *name)
{
    if (quiesce_c64_uci() != 0)
        return -1;
    if (quiesce_c64_directory() != 0) return -1;
    const int result = c64_d64_service_mount(name);
    const int resumed = resume_c64_directory();
    return result != 0 ? result : resumed;
}
static int restore_c64_reboot_settings(void *context)
{
    return c64_title_service_reboot(*(const bool *)context);
}
static void service_c64_uci_action(void)
{
    static bool executing;
    if (executing || c64_title_foreground)
        return;
    if (c64_uci_freeze_pending) {
        if (!supervisor_menu.visible) {
            c64_uci_freeze_pending = false;
            c64_uci_service_action_done(0);
        }
        return;
    }
    c64_uci_action_t action;
    char path[512];
    unsigned device;
    if (!c64_uci_service_action(&action, path, sizeof(path), &device))
        return;
    executing = true;
    c64_title_foreground = true;
    int status = 0;
    if (action == C64_UCI_ACTION_FREEZE) {
        supervisor_menu.visible = true;
        supervisor_menu_dirty = true;
        c64_uci_freeze_pending = true;
    } else if (action == C64_UCI_ACTION_REBOOT) {
        /* Retire the posted action before reset quiesces its own UCI target.
         * Guest reset naturally discards any unacknowledged reply. */
        c64_uci_service_action_done(0);
        const int asserted = set_core_reset_request(true);
        if (asserted == 0) {
            refresh_c64_title(&c64_extension_selection);
            const mt_core_settings_profile_t *profile = mt_supervisor_settings_find(
                &persistent_settings, c64_extension_selection.package.id);
            const bool default_cartridge = profile && profile->cartridge_name[0];
            const mt_c64_content_service_result_t reboot = c64_content_service_reboot(
                profile ? profile->cartridge_name : NULL, restore_c64_reboot_settings,
                (void *)&default_cartridge);
            status = reboot.status == MT_C64_CONTENT_SERVICE_OK ? 0 : -1;
            if (status) print_c64_content_result("reboot", NULL, &reboot);
            usleep(1000U);
        }
        status = asserted == 0 && status == 0 ? set_core_reset_request(false) : -1;
        if (status)
            xil_printf("[UCI] reboot failed; check media and saved settings\r\n");
    } else if (action == C64_UCI_ACTION_MOUNT) {
        const char *root = c64_extension_selection.package.content_root;
        size_t n = strlen(root);
        if (device != 8 || strncmp(path, root, n) || path[n] != '/')
            status = -1;
        else if (quiesce_c64_directory() != 0)
            status = -1;
        else {
            status = c64_d64_service_mount(path + n + 1);
            if (resume_c64_directory() != 0)
                status = -1;
        }
    } else if (action == C64_UCI_ACTION_SWAP) {
        if (device != 8 || quiesce_c64_directory() != 0)
            status = -1;
        else {
            status = c64_d64_service_swap();
            if (resume_c64_directory() != 0) status = -1;
        }
    } else if (action == C64_UCI_ACTION_UNMOUNT) {
        if (device != 8 || quiesce_c64_directory() != 0)
            status = -1;
        else {
            status = c64_d64_service_eject();
            if (resume_c64_directory() != 0)
                status = -1;
        }
    } else
        status = -1;
    c64_title_foreground = false;
    executing = false;
    if (!c64_uci_freeze_pending)
        c64_uci_service_action_done(status == 0 ? 0 : 74);
}
static void network_console(void *context, const char *text)
{
    (void)context;
    xil_printf("%s\r\n", text);
}
static bool dispatch_c64_title(const char *line)
{
    if (shell_is_c64() && !strcmp(line, "sidstat")) {
        if (!c64_debug.session) {
            xil_printf("[SID] :debug on is required\r\n");
            return true;
        }
        const mt_c64_title_overlay_t *o = c64_title_service_overlay();
        xil_printf(
            "[SID] commit=%08x mapped=%u dropped=%u unavailable-models=%u owned=%08x%08x\r\n",
            shell_read(MT_REG_C64_SID_STATUS), c64_live_sid_table.mapped,
            c64_live_sid_table.dropped, c64_live_sid_table.unsupported_models,
            (unsigned)(o->sid_fields >> 32), (unsigned)o->sid_fields);
        for (unsigned i = 0; i < 8; ++i)
            xil_printf("[SID] chip%u base=$%04x params=%08x\r\n", i + 1, c64_live_sid_table.base[i],
                       c64_live_sid_table.words[48 + i]);
        for (unsigned i = 0; i < 48; ++i)
            if (c64_live_sid_table.words[i])
                xil_printf("[SID] slot%u owners=%08x\r\n", i, c64_live_sid_table.words[i]);
        xil_printf("[SID] output=%08x; clip counters unavailable in this image\r\n",
                   c64_live_sid_table.words[56]);
        return true;
    }
    if (!shell_is_c64() || !c64_title_initialized || strncmp(line, "cfg", 3) ||
        (line[3] && line[3] != ' '))
        return false;
    const char *arg = line + 3;
    while (*arg == ' ')
        ++arg;
    if (!*arg) {
        mt_c64_title_values_t requested, applied;
        refresh_c64_title(&c64_extension_selection);
        c64_title_service_effective(&requested, &applied, c64_content_service_crt_attached());
        xil_printf("[cfg] auto=%s session=%s source=%s owned=%08x CPU=%uMHz REU=%u UA=%u UCI=%u "
                   "SCPU=%u\r\n",
                   persistent_settings.c64_title_cfg_off ? "off" : "on",
                   c64_title_service_session(), c64_title_service_overlay()->source,
                   (unsigned)c64_title_service_overlay()->owned,
                   mt_c64_speed_mhz(requested.detail & 15), requested.reu, requested.ua,
                   requested.uci, (requested.flags & 8) != 0);
        return true;
    }
    if (!strncmp(arg, "test ", 5)) {
        char report[2048];
        refresh_c64_title(&c64_extension_selection);
        (void)c64_title_service_test(arg + 5, report, sizeof report);
        xil_printf("%s", report);
        return true;
    }
    if (quiesce_c64_uci() != 0) {
        xil_printf("[cfg] command interface is busy\r\n");
        return true;
    }
    c64_title_foreground = true;
    int result = -1;
    char report[2048];
    if (!strcmp(arg, "save"))
        result = c64_title_service_save();
    else if (!strcmp(arg, "revert"))
        result = c64_title_service_revert();
    else if (!strncmp(arg, "apply ", 6)) {
        result = c64_title_service_apply(arg + 6, false, report, sizeof(report));
        xil_printf("%s", report);
    } else
        xil_printf("Usage: :cfg [test relative.cfg|apply relative.cfg|save|revert]\r\n");
    c64_title_foreground = false;
    supervisor_menu_dirty = true;
    set_supervisor_outcome(result == 0, c64_title_service_status(), c64_title_service_status());
    return true;
}

static void c64_title_launched(mt_c64_content_service_status_t result)
{
    c64_title_service_finish(result);
    if (result == MT_C64_CONTENT_SERVICE_OK && c64_uci_initialized) {
        char directory[512];
        if (mt_media_browser_join(c64_extension_selection.package.content_root,
                                  c64_title_service_session(), directory, sizeof(directory))) {
            char *slash = strrchr(directory, '/');
            if (slash)
                *slash = 0;
            c64_uci_service_set_home(directory);
        }
    }
    if (*c64_title_service_status())
        xil_printf("[C64] %s\r\n", c64_title_service_status());
}
static mt_c64_content_service_result_t c64_title_load(const char *path, bool event)
{
    refresh_c64_title(&c64_extension_selection);
    const mt_c64_launch_hook_t *hook = c64_title_service_prepare(
        path, mt_content_classify(path) == MT_CONTENT_CRT ? MT_TITLE_CRT : MT_TITLE_PRG, event);
    if (!c64_title_service_launch_allowed()) {
        c64_title_launched(MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
        return (mt_c64_content_service_result_t){.status = MT_C64_CONTENT_SERVICE_CONFIG_FAILED};
    }
    const mt_c64_content_service_result_t r = c64_content_service_load_hooked(path, hook);
    c64_title_launched(r.status);
    return r;
}
static mt_c64_content_service_result_t c64_title_disk(const uint8_t *data, size_t size)
{
    refresh_c64_title(&c64_extension_selection);
    const mt_c64_launch_hook_t *hook =
        c64_title_service_prepare(c64_launch_disk_name, MT_TITLE_DISK, true);
    if (!c64_title_service_launch_allowed()) {
        c64_title_launched(MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
        return (mt_c64_content_service_result_t){.status = MT_C64_CONTENT_SERVICE_CONFIG_FAILED};
    }
    const mt_c64_content_service_result_t r =
        data ? c64_content_service_stage_prg_image_hooked(data, size, hook)
             : c64_content_service_prepare_disk_load_hooked(hook);
    c64_title_launched(r.status);
    return r;
}
static mt_c64_content_service_result_t c64_title_detach(void)
{
    refresh_c64_title(&c64_extension_selection);
    const mt_c64_content_service_result_t r =
        c64_content_service_detach_crt_hooked(c64_title_service_prepare_detach());
    c64_title_launched(r.status);
    return r;
}

static const char *c64_content_failure_message(mt_c64_content_service_status_t status,
                                               bool remounted)
{
    if (!remounted) return "Content load and drive remount failed";
    if (status == MT_C64_CONTENT_SERVICE_CONFIG_FAILED && *c64_title_service_status())
        return c64_title_service_status();
    if (status == MT_C64_CONTENT_SERVICE_REJECTED)
        return "Content type or layout rejected; see UART details";
    if (status == MT_C64_CONTENT_SERVICE_REU_CONFLICT)
        return "This C64 FPGA requires REU off for cartridges";
    return "Content load failed";
}

static void handle_c64_content_load(const char *file_name)
{
    mt_c64_content_service_result_t result;
    if (!prepare_c64_content_change()) return;
    result = c64_title_load(file_name, true);
    print_c64_content_result("load", file_name, &result);
    finish_c64_content_change();
    if (result.status == MT_C64_CONTENT_SERVICE_OK) {
        const bool cartridge = mt_content_classify(file_name) == MT_CONTENT_CRT;
        const bool too_long = cartridge && strlen(file_name) >= MT_SETTINGS_MEDIA_NAME_MAX;
        if (!remember_active_cartridge(cartridge && !too_long ? file_name : NULL))
            set_supervisor_error("Content loaded; settings write failed");
        else if (too_long)
            set_supervisor_warning("Cartridge loaded; long path cannot be remembered");
    }
}

static void handle_c64_cart_detach(void)
{
    mt_c64_content_service_result_t result;
    if (!prepare_c64_content_change()) return;
    result = c64_title_detach();
    print_c64_content_result("cartridge detach", NULL, &result);
    finish_c64_content_change();
    if (result.status == MT_C64_CONTENT_SERVICE_OK)
        (void)remember_active_cartridge(NULL);
}

static bool read_extended_command(char *buffer, size_t capacity,
                                  bool recovery_only)
{
    size_t length = 0U;
    bool overflow = false;

    for (;;) {
        uint8_t byte;
        char input;
        while (uart0_try_read_byte(&byte) == 0) {
            /* Recovery starts before the shell and board services are usable. */
            if (!recovery_only) service_runtime();
        }
        input = (char)byte;
        if (input == '\r' || input == '\n') {
            break;
        }
        if (length + 1U < capacity) {
            buffer[length++] = input;
        } else {
            overflow = true;
        }
    }
    buffer[length] = '\0';
    return !overflow;
}

static bool wait_uart0_transmit_idle(void)
{
    uint32_t poll;
    for (poll = 0U; poll < MT_UART_DRAIN_POLLS; ++poll) {
        const uint32_t status = XUartPs_ReadReg(
            XPAR_XUARTPS_0_BASEADDR, XUARTPS_SR_OFFSET);
        if ((status & XUARTPS_SR_TXEMPTY) != 0U &&
            (status & XUARTPS_SR_TACTIVE) == 0U) {
            return true;
        }
    }
    return false;
}

static void reboot_to_qspi(uint32_t offset, bool recovery_only)
{
    int usb_stop_result;
    mt_sd_result_t unmount_result;

    if (offset != GOLDEN_SELF_UPDATE_PRIMARY_OFFSET &&
        offset != GOLDEN_SELF_UPDATE_TRIAL_OFFSET) {
        xil_printf("invalid golden boot offset; reboot cancelled\r\n");
        return;
    }
    if (!recovery_only && set_core_reset_request(true) != 0) {
        xil_printf("media flush or core reset failed; reboot cancelled\r\n");
        set_supervisor_error("Reboot cancelled; media flush or core reset failed");
        return;
    }
    if(!recovery_only && shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID &&
        amiga_service_initialized && amiga_content_service_shutdown()!=0) {
        if(!golden_update_worker_held)(void)set_core_reset_request(false);
        set_supervisor_error("Reboot cancelled; Amiga media could not close");return;
    }
    xil_printf("rebooting to golden at QSPI offset 0x%08x\r\n", offset);
    if (!wait_uart0_transmit_idle()) {
        xil_printf("UART did not drain; reboot cancelled\r\n");
        if (!recovery_only) {
            if (!golden_update_worker_held) (void)set_core_reset_request(false);
            set_supervisor_error("Reboot cancelled; UART did not drain");
        }
        return;
    }

    Xil_Out32(MT_DEVCFG_MULTIBOOT, offset / UINT32_C(0x8000));
    DATA_SYNC;
    if ((Xil_In32(MT_DEVCFG_MULTIBOOT) & MT_DEVCFG_MULTIBOOT_MASK) !=
        offset / UINT32_C(0x8000)) {
        xil_printf("multiboot readback failed; reboot cancelled\r\n");
        if (!recovery_only) {
            if (!golden_update_worker_held) (void)set_core_reset_request(false);
            set_supervisor_error("Reboot cancelled; multiboot readback failed");
        }
        return;
    }

    if (!recovery_only) {
        usb_stop_result = usb_hid_service_stop();
        if (usb_stop_result != 0) {
            xil_printf("USB1 VBUS shutdown failed (%d); reboot cancelled\r\n",
                       usb_stop_result);
            if (!golden_update_worker_held) (void)set_core_reset_request(false);
            set_supervisor_error("Reboot cancelled; USB shutdown failed");
            return;
        }
        unmount_result = mt_sd_storage_unmount();
        if (unmount_result.status != MT_SD_OK) {
            xil_printf("SD unmount warning: %s backend=%u\r\n",
                       mt_sd_status_name(unmount_result.status),
                       unmount_result.backend_status);
        }
    }

    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    DATA_SYNC;
    Xil_Out32(MT_PS_RESET_CONTROL, MT_PS_SOFTWARE_RESET);
    DATA_SYNC;
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    for (;;) {
#if defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("wfi");
#endif
    }
}

static void reboot_to_golden(bool recovery_only)
{
    reboot_to_qspi(GOLDEN_SELF_UPDATE_PRIMARY_OFFSET, recovery_only);
}

/* Handoff through a verified Golden image. Before attempting CPU1 reset,
 * failures resume this runtime; after an attempted hold they stay safely
 * stopped rather than warm restarting CPU1 through a stale reset vector. */
static void switch_to_appletini(void)
{
    uint32_t golden_offset;
    const mb_flash_t *flash;
    const uint32_t previous_boot = Xil_In32(MT_DEVCFG_MULTIBOOT);
    const bool previously_reset = (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) != 0U;
    const bool ddr_source = core_requires_ddr_drain();
    bool core_stopped = false, worker_stopped = false, usb_stopped = false;
    bool storage_unmounted = false, request_attempted = false, boot_changed = false;
    bool restore_safe = true;
    uint64_t started;
    const char *error = "Switch cancelled; firmware unavailable";

    if (supervisor_menu.visible)
        show_handoff_progress("Checking Appletini firmware; please wait");
    flash = mb_platform_flash();
    /* Presence is advisory. The request validates Golden after draining
     * owners; Golden checks target integrity and fallback after reset. */
    if (flash == NULL || mb_slot_probe(flash, MB_PRODUCT_APPLETINI, NULL) != MB_OK)
        goto restore;
    if (!mt_screenshot_service_cancel()) {
        error = "Switch cancelled; screenshot is busy";
        goto restore;
    }
    if (set_core_reset_request(true) != 0) {
        error = "Switch cancelled; media flush or core reset failed";
        goto restore;
    }
    core_stopped = true;
    if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized &&
        amiga_content_service_shutdown()!=0) {
        error="Switch cancelled; Amiga media could not close";goto restore;
    }
    usb_hid_service_set_core_ready(false);
    mt_video_effect_service_suspend();
    worker_stopped = true;
    started = monotonic_ms(NULL);
    while (!mt_video_effect_service_quiescent() || !mt_video_worker_quiesce() ||
           (ddr_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
                              MT_VIDEO_FRAME_STATUS_QUIESCENT) == 0U)) {
        usb_hid_service_poll_deferred();
        if (monotonic_ms(NULL) - started >= 1000U) {
            error = "Switch cancelled; video transport did not drain";
            goto restore;
        }
    }
    usb_stopped = true; /* Also retry start if a partial shutdown fails. */
    if (usb_hid_service_stop() != 0) {
        error = "Switch cancelled; USB shutdown failed";
        goto restore;
    }
    {
        const mt_sd_result_t storage = mt_sd_storage_unmount();
        if (storage.status != MT_SD_OK) {
            xil_printf("switch SD unmount failed: %s backend=%u\r\n",
                       mt_sd_status_name(storage.status), storage.backend_status);
            error = storage.status == MT_SD_FILE_BUSY ?
                "Switch cancelled; an SD file is still open" :
                "Switch cancelled; SD unmount failed";
            goto restore;
        }
    }
    storage_unmounted = true;
    request_attempted = true;
    started = monotonic_ms(NULL);
    if (mb_platform_prepare_request(MB_PRODUCT_APPLETINI, &golden_offset) != MB_OK) {
        error = "Switch cancelled; boot request could not be saved";
        goto restore;
    }
    xil_printf("switch: Golden validation and request ms=%u\r\n",
               (unsigned)(monotonic_ms(NULL)-started));
    {
        char read_report[192];
        if (mb_platform_read_report(read_report,sizeof read_report)==MB_OK)
            xil_printf("%s",read_report);
    }
    xil_printf("switching to Appletini through golden at 0x%08x\r\n", golden_offset);
    if (!wait_uart0_transmit_idle()) {
        error = "Switch cancelled; UART did not drain";
        goto restore;
    }
    boot_changed = true;
    Xil_Out32(MT_DEVCFG_MULTIBOOT, golden_offset / UINT32_C(0x8000));
    DATA_SYNC;
    if ((Xil_In32(MT_DEVCFG_MULTIBOOT) & MT_DEVCFG_MULTIBOOT_MASK) !=
        golden_offset / UINT32_C(0x8000)) {
        error = "Switch cancelled; boot register verification failed";
        goto restore;
    }
    /* Complete all fallible metadata/SD/register work while the drained
     * worker can still resume safely. Only hold CPU1 at the final reset. */
    golden_update_worker_held = true;
    if (!mt_video_worker_hold_for_reset()) {
        error = "Switch cancelled; CPU1 could not stop safely";
        goto restore;
    }
    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    DATA_SYNC;
    Xil_Out32(MT_PS_RESET_CONTROL, MT_PS_SOFTWARE_RESET);
    DATA_SYNC;
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    for (;;) {
#if defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("wfi");
#endif
    }
restore:
    if (boot_changed) Xil_Out32(MT_DEVCFG_MULTIBOOT, previous_boot);
    if (request_attempted && mb_platform_clear_request() != MB_OK)
        error = "Switch cancelled; pending request could not be cleared";
    if (storage_unmounted && mt_sd_storage_mount().status != MT_SD_OK) {
        error = "Switch cancelled; SD could not remount";
        restore_safe = false;
    }
    if (golden_update_worker_held) restore_safe = false;
    else if (worker_stopped && !mt_video_worker_resume()) {
        error = "Switch cancelled; CPU1 could not resume";
        restore_safe = false;
    }
    if (usb_stopped && usb_hid_service_start() != 0) {
        error = "Switch cancelled; USB could not restart";
        restore_safe = false;
    }
    if (core_stopped) {
        if (restore_safe && !previously_reset && set_core_reset_request(false) != 0) {
            error = "Switch cancelled; core could not resume";
            restore_safe = false;
        }
        usb_hid_service_set_core_ready(restore_safe && !previously_reset && supervisor.has_active);
        if (restore_safe)
            configure_selection_effects(supervisor.has_active ? &supervisor.active : NULL);
    }
    refresh_multiboot_status();
    set_supervisor_error(error);
}

/* Explicit confirmed menu action or serial command only. FatFs is borrowed
 * after runtime media and rendering owners stop. Golden availability is not
 * a prerequisite: this is also the migration path from Golden 1.x. */
static void self_update_golden(bool recovery_only)
{
    golden_self_update_result_t result = {0};
    mt_sd_result_t storage;
    const uint32_t previous_boot = Xil_In32(MT_DEVCFG_MULTIBOOT);
    const bool previously_reset = !recovery_only &&
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) != 0U;
    const bool menu_was_visible = !recovery_only && supervisor_menu.visible;
    const bool ddr_source = !recovery_only && core_requires_ddr_drain();
    bool core_stopped = false, worker_stopped = false;
    bool stopped_usb = false, restore_safe = true, boot_changed = false;
    const char *error = "Golden update cancelled";
    char update_error[96];
    uint64_t started;
    if (!recovery_only) {
        if (!mt_screenshot_service_cancel()) {
            error = "Golden update cancelled; screenshot is busy";
            goto restore;
        }
        mt_text_reader_close(&document_reader);
        if (set_core_reset_request(true) != 0) {
            error = "Golden update cancelled; media flush failed";
            goto restore;
        }
        core_stopped = true;
        if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized &&
            amiga_content_service_shutdown()!=0) {
            error="Golden update cancelled; Amiga media could not close";goto restore;
        }
        usb_hid_service_set_core_ready(false);
        mt_video_effect_service_suspend();
        worker_stopped = true;
        started = monotonic_ms(NULL);
        while (!mt_video_effect_service_quiescent() || !mt_video_worker_quiesce() ||
               (ddr_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
                                  MT_VIDEO_FRAME_STATUS_QUIESCENT) == 0U)) {
            usb_hid_service_poll_deferred();
            if (monotonic_ms(NULL) - started >= 1000U) {
                error = "Golden update cancelled; video transport is busy";
                goto restore;
            }
        }
        if (menu_was_visible)
            show_handoff_progress("Updating Golden Boot; keep the card powered");
        stopped_usb = true;
        if (usb_hid_service_stop() != 0) {
            error = "Golden update cancelled; USB shutdown failed";
            goto restore;
        }
    }
    storage = mt_sd_storage_unmount();
    if (storage.status == MT_SD_OK) storage = mt_sd_storage_mount();
    if (storage.status != MT_SD_OK) {
        xil_printf("golden update SD handoff failed: %s backend=%u\r\n",
                   mt_sd_status_name(storage.status), storage.backend_status);
        error = "Golden update cancelled; SD unavailable or busy";
        goto restore;
    }
    if (!recovery_only) set_supervisor_status("Updating Golden Boot; keep the card powered");
    if (golden_self_update_run("0:/BOOT.BIN", XPAR_XUARTPS_0_BASEADDR,
                               XPAR_XUARTPS_0_BASEADDR, &result) == 0 &&
        result.updated && result.verified && result.reset_required &&
        result.boot_offset == GOLDEN_SELF_UPDATE_TRIAL_OFFSET) {
        if (mt_sd_storage_unmount().status != MT_SD_OK) {
            error = "Golden staged; SD unmount failed. Retry update";
            goto restore;
        }
        xil_printf("rebooting to verified Golden trial at 0x%08x\r\n", result.boot_offset);
        if (!wait_uart0_transmit_idle()) {
            error = "Golden staged; UART did not drain. Retry update";
            goto restore;
        }
        /* Do not modify the persistent startup choice or one-shot product
         * request. The replacement Golden completes its normal repair. */
        boot_changed = true;
        Xil_Out32(MT_DEVCFG_MULTIBOOT, result.boot_offset / UINT32_C(0x8000));
        DATA_SYNC;
        if ((Xil_In32(MT_DEVCFG_MULTIBOOT) & MT_DEVCFG_MULTIBOOT_MASK) !=
            result.boot_offset / UINT32_C(0x8000)) {
            error = "Golden staged; boot register failed. Retry update";
            goto restore;
        }
        if (!recovery_only) {
            /* Complete SD/UART/register checks before attempting CPU1 reset.
             * Earlier failures resume the drained worker without reset. */
            golden_update_worker_held = true;
            if (!mt_video_worker_hold_for_reset()) {
                error = "Golden staged; CPU1 could not stop safely";
                goto restore;
            }
        }
        Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
        DATA_SYNC;
        Xil_Out32(MT_PS_RESET_CONTROL, MT_PS_SOFTWARE_RESET);
        DATA_SYNC;
        Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
        for (;;) {
#if defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("wfi");
#endif
        }
    } else {
        xil_printf("golden update failed (%d): %s\r\n",
                   result.error_code, result.error_msg);
        (void)snprintf(update_error, sizeof(update_error), "Golden update failed: %.73s",
                       result.error_msg[0] != '\0' ? result.error_msg : "verification failed");
        error = update_error;
    }
restore:
    if (boot_changed) {
        Xil_Out32(MT_DEVCFG_MULTIBOOT, previous_boot);
        DATA_SYNC;
    }
    xil_printf("%s\r\n", error);
    if (!recovery_only) {
        if (mt_sd_storage_mount().status != MT_SD_OK) restore_safe = false;
        /* Never attempt an AMP warm restart after a reset write. Its reset
         * vector may reenter an FSBL. Keep the core stopped and allow retry. */
        if (golden_update_worker_held) restore_safe = false;
        else if (worker_stopped && !mt_video_worker_resume()) restore_safe = false;
        if (stopped_usb && usb_hid_service_start() != 0) restore_safe = false;
        if (core_stopped && restore_safe && !previously_reset &&
            set_core_reset_request(false) != 0) restore_safe = false;
        usb_hid_service_set_core_ready(restore_safe && !previously_reset && supervisor.has_active);
        if (worker_stopped && restore_safe)
            configure_selection_effects(supervisor.has_active ? &supervisor.active : NULL);
        supervisor_menu.visible = menu_was_visible;
        refresh_multiboot_status();
        refresh_golden_update_file();
        set_supervisor_error(error);
    }
}

static void change_display_mode(size_t mode);

static void print_iigs_media_status(void)
{
    const mt_iigs_config_t *options=iigs_content_service_config();
    xil_printf("[IIgs] ready=%u fault=%u ROM=%u speed=%u card=%u error=%s\r\n",
        iigs_content_service_ready() ? 1U:0U,iigs_content_service_faulted() ? 1U:0U,
        options->rom3 ? 3U:1U,(unsigned)options->speed,(unsigned)options->card,
        iigs_content_service_error());
    for(unsigned d=0U;d<MT_IIGS_DRIVE_COUNT;++d) {
        const char *name=iigs_content_service_name(d);
        xil_printf("[IIgs] drive%u: %s\r\n",d,name!=NULL ? name:"ejected");
    }
}

static bool legacy_c64_media_command(const char *text)
{
    return strcmp(text,"eject")==0 || strcmp(text,"cartoff")==0 ||
        ((strncmp(text,"load",4U)==0) && (text[4]=='\0' || text[4]==' ' || text[4]=='\t')) ||
        ((strncmp(text,"mount",5U)==0) && (text[5]=='\0' || text[5]==' ' || text[5]=='\t')) ||
        ((strncmp(text,"c64rx",5U)==0) && (text[5]=='\0' || text[5]==' ' || text[5]=='\t'));
}

static void handle_extended_command(bool recovery_only)
{
    char extended[MT_EXTENDED_COMMAND_CAPACITY];
    char *argument;
    char *argument_end;
    const bool iigs_active=!recovery_only && shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID;

    if (!read_extended_command(extended, sizeof(extended), recovery_only)) {
        if (!recovery_only && shell_is_c64() && strncmp(extended, "c64rx", 5U) == 0) {
            leave_core_in_reset();
        }
        xil_printf("extended command is too long\r\n");
    } else if (strcmp(extended, "reboot") == 0) {
        reboot_to_golden(recovery_only);
    } else if (strcmp(extended, "selfupdate") == 0) {
        self_update_golden(recovery_only);
    } else if (golden_update_worker_held && strcmp(extended, "diag") != 0) {
        xil_printf("CPU1 is safely held; retry :selfupdate or :reboot\r\n");
        if (!recovery_only)
            set_supervisor_error("CPU1 held; retry Golden update or reboot");
    } else if (!recovery_only && strncmp(extended, "display ", 8U) == 0 &&
               extended[8]>='0' && extended[8]<='5' && extended[9]=='\0') {
        change_display_mode((size_t)(extended[8]-'0'));
    } else if (strcmp(extended, "diag") == 0) {
        print_read_only_diagnostics("command");
    } else if (!recovery_only && supervisor_serial_command(extended)) {
    } else if (!recovery_only && amiga_serial_command(extended)) {
    } else if (!recovery_only && mt_net_service_command(extended, network_console, NULL)) {
    } else if (!recovery_only && shell_is_c64() && strcmp(extended, "uciretry") == 0) {
        const int result = retry_c64_uci();
        const char *failure = c64_uci_service_reload_required()
                                  ? "Command DMA fault; reload the C64 core"
                                  : "Command cleanup retry failed; check SD card";
        set_supervisor_outcome(result == 0, "Command interface cleanup completed", failure);
        xil_printf("[C64] %s\r\n", result == 0 ? "Command interface cleanup completed" : failure);
    } else if (!recovery_only && dispatch_c64_title(extended)) {
    } else if (!recovery_only && dispatch_c64_debug(extended)) {
        /* Commands qualify the image before accessing the new registers. */
    } else if (!recovery_only && c64_debug.reu_host.reload_required) {
        xil_printf("REU cache did not clear; reload the C64 core\r\n");
    } else if (!recovery_only && strcmp(extended, "hid") == 0) {
        usb_hid_service_dump_descriptors();
    } else if (iigs_active && (strcmp(extended, "disk") == 0 || strcmp(extended, "dir") == 0)) {
        print_iigs_media_status();
    } else if (iigs_active &&
               (strcmp(extended, "diskretry") == 0 || strcmp(extended, "saveretry") == 0)) {
        if(!hide_menu_for_core_action())return;
        const int result=iigs_content_service_retry();
        if(result!=0)set_supervisor_error(iigs_content_service_error());
        else set_supervisor_status("Apple IIgs media retry completed");
        print_iigs_media_status();
    } else if (iigs_active && legacy_c64_media_command(extended)) {
        xil_printf("[IIgs] C64 command unavailable; use the Apple IIgs Media menu\r\n");
        set_supervisor_warning("Use the Apple IIgs Media menu");
    } else if (!recovery_only && strncmp(extended, "c64rx", 5U) == 0 &&
               (extended[5] == '\0' || extended[5] == ' ' || extended[5] == '\t')) {
        handle_c64_rx_command(extended);
    } else if (!recovery_only && strcmp(extended, "dir") == 0) {
        c64_d64_service_list();
    } else if (!recovery_only && strcmp(extended, "disk") == 0) {
        c64_d64_service_dump_status();
    } else if (!recovery_only && shell_is_c64() && strcmp(extended, "disk9") == 0) {
        xil_printf("[C64] drive9 %s cwd=%s status=%s\r\n",
                   c64_iec_service_active() ? "active" : "inactive",
                   c64_iec_service_cwd(), c64_iec_service_status());
    } else if (!recovery_only && shell_is_c64() && strcmp(extended, "disk9retry") == 0) {
        if (!hide_menu_for_core_action()) return;
        const bool recovered = c64_iec_initialized &&
            c64_iec_service_available() && c64_iec_service_reset() == 0 &&
            resume_c64_directory() == 0;
        set_supervisor_outcome(recovered, persistent_settings.c64_directory_drive_off ?
                               "SD directory recovered; drive remains Off" :
                               "Directory drive ready at media root",
                               "Directory drive retry failed; check SD card");
        xil_printf("[C64] drive9 retry %s: %s\r\n",
                   recovered ? "completed" : "failed", c64_iec_service_status());
    } else if (!recovery_only && strcmp(extended, "saveretry") == 0) {
        if (!hide_menu_for_core_action()) return;
        const int status = shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID ?
            nes_content_service_retry_save() :
            shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID ?
            genesis_content_service_retry_save() :
            (shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID ? gameboy_content_service_retry_save() : -1);
        set_supervisor_outcome(status == 0, "Save retry accepted", "Save retry unavailable or failed");
    } else if (!recovery_only && strcmp(extended, "diskretry") == 0) {
        if (!hide_menu_for_core_action()) return;
        if (c64_d64_service_retry() == 0) {
            if (c64_d64_service_resume_after_reset() != 0)
                xil_printf("[C64] drive8 retry pending\r\n");
            else if (c64_content_service_prg_staged()) {
                const mt_c64_content_service_result_t run =
                    c64_content_service_run_staged_prg();
                print_c64_content_result("deferred mount-and-run", NULL,
                                         &run);
                set_supervisor_outcome(
                    run.status == MT_C64_CONTENT_SERVICE_OK,
                    "Program running; D64 remounted",
                    "D64 remounted; staged RUN failed");
            } else {
                xil_printf("[C64] drive8 retry accepted\r\n");
            }
        } else {
            xil_printf("[C64] drive8 retry failed\r\n");
        }
    } else if (!recovery_only && strcmp(extended, "eject") == 0) {
        if (!hide_menu_for_core_action()) return;
        if (c64_d64_service_eject() == 0)
            (void)remember_active_drive8(NULL);
    } else if (!recovery_only && strcmp(extended, "cartoff") == 0) {
        if (shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID) {
            if (hide_menu_for_core_action()) handle_nes_cart_detach();
        } else if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
            if (hide_menu_for_core_action()) handle_genesis_cart_detach();
        } else handle_c64_cart_detach();
    } else if (!recovery_only && strncmp(extended, "load", 4U) == 0 &&
               (extended[4] == ' ' || extended[4] == '\t')) {
        argument = extended + 4;
        while (*argument == ' ' || *argument == '\t') ++argument;
        argument_end = argument + strlen(argument);
        while (argument_end > argument &&
               (argument_end[-1] == ' ' || argument_end[-1] == '\t')) {
            --argument_end;
        }
        *argument_end = '\0';
        if (*argument == '\0')
            xil_printf("usage: :load FILE.prg|FILE.crt\r\n");
        else if (shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID) {
            if (hide_menu_for_core_action()) handle_nes_content_load(argument);
        } else if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
            if (hide_menu_for_core_action()) handle_genesis_content_load(argument);
        } else
            handle_c64_content_load(argument);
    } else if (!recovery_only && strncmp(extended, "mount", 5U) == 0 &&
               (extended[5] == ' ' || extended[5] == '\t')) {
        argument = extended + 5;
        while (*argument == ' ' || *argument == '\t') ++argument;
        argument_end = argument + strlen(argument);
        while (argument_end > argument &&
               (argument_end[-1] == ' ' || argument_end[-1] == '\t')) {
            --argument_end;
        }
        *argument_end = '\0';
        if (*argument == '\0') {
            xil_printf("usage: :mount FILE.d64|FILE.g64\r\n");
        } else if (!c64_drive_ready) {
            xil_printf("[C64] drive8 unavailable; install %s\r\n",
                       MT_C64_SD_1541_PATH);
        } else {
            if (!hide_menu_for_core_action()) return;
            if (mount_c64_disk(argument) == 0) {
                const bool too_long = strlen(argument) >= MT_SETTINGS_MEDIA_NAME_MAX;
                if (!remember_active_drive8(too_long ? NULL : argument))
                    set_supervisor_error("Drive mounted; settings write failed");
                else if (too_long)
                    set_supervisor_warning("Drive mounted; long path cannot be remembered");
            }
        }
    } else {
        xil_printf("unknown extended command ':%s'\r\n", extended);
    }

    if (recovery_only) {
        xil_printf("recovery commands: :diag read status; :reboot return to golden updater; :selfupdate stage BOOT.BIN\r\n");
    } else {
        print_help();
    }
}

static uint32_t supervisor_ui_read(void *context, uint32_t offset)
{
    (void)context;
    return shell_read(offset);
}

static void supervisor_ui_write(void *context, uint32_t offset,
                                uint32_t value)
{
    (void)context;
    Xil_Out32(MT_SHELL_BASE_ADDRESS + offset, value);
    DATA_SYNC;
}

static void set_supervisor_status(const char *text)
{
    size_t size;
    if (text == NULL) text = "";
    size = strlen(text);
    if (size >= sizeof(supervisor_status_text))
        size = sizeof(supervisor_status_text) - 1U;
    memcpy(supervisor_status_text, text, size);
    supervisor_status_text[size] = '\0';
    supervisor_status_color = MT_UI_COLOR_MUTED;
    supervisor_menu_dirty = true;
}

static void set_supervisor_warning(const char *text)
{
    set_supervisor_status(text);
    supervisor_status_color = MT_UI_COLOR_WARNING;
}

static void set_supervisor_error(const char *text)
{
    set_supervisor_status(text);
    supervisor_status_color = MT_UI_COLOR_RED;
}

static void set_supervisor_outcome(bool succeeded, const char *success,
                                    const char *failure)
{
    if (succeeded) set_supervisor_status(success);
    else set_supervisor_error(failure);
}

static const char *active_settings_core_id(void)
{
    return supervisor.has_active ? supervisor.active.package.id : NULL;
}

static bool save_persistent_settings(void)
{
    const mt_settings_result_t result =
        mt_supervisor_settings_save(&persistent_settings);
    settings_faulted = result.status != MT_SETTINGS_OK;
    supervisor_menu_dirty = true;
    xil_printf("[settings] save=%s sd=%s backend=%u auto-boot=%u "
               "selected=%s\r\n",
               mt_settings_status_name(result.status),
               mt_sd_status_name(result.sd.status), result.sd.backend_status,
               persistent_settings.auto_boot ? 1U : 0U,
               persistent_settings.selected_core_id[0] == '\0' ?
                   "none" : persistent_settings.selected_core_id);
    return !settings_faulted;
}

static bool ensure_active_settings_profile(void)
{
    return supervisor.has_active &&
           mt_supervisor_settings_remember_variant(
               &persistent_settings, supervisor.active.package.id,
               supervisor.active.package
                   .variants[supervisor.active.variant_index].id);
}

static bool selection_supports_borders(const mt_core_selection_t *selection)
{
    uint32_t version;
    if (selection == NULL || selection->package.core_id == MT_SUPERVISOR_CORE_ID ||
        shell_read(MT_REG_CORE_ID) != selection->package.core_id) return false;
    version = shell_read(MT_REG_ABI_VERSION);
    return (uint16_t)(version >> 16U) == MT_ABI_MAJOR &&
           (uint16_t)version >= MT_BORDER_MIN_ABI_MINOR;
}

static bool selection_supports_vectrex_video(const mt_core_selection_t *selection)
{
    uint32_t version;
    if (selection == NULL || selection->package.core_id != MT_VECTREX_CORE_ID ||
        shell_read(MT_REG_CORE_ID) != MT_VECTREX_CORE_ID) return false;
    version = shell_read(MT_REG_ABI_VERSION);
    return (uint16_t)(version >> 16U) == MT_ABI_MAJOR &&
           (uint16_t)version >= MT_VECTREX_VIDEO_MIN_ABI_MINOR;
}

static bool selection_supports_effects(const mt_core_selection_t *selection)
{
    uint32_t version;
    if (selection == NULL || selection->package.core_id == MT_SUPERVISOR_CORE_ID ||
        selection->package.abi_major != MT_ABI_MAJOR ||
        selection->package.abi_minor < MT_VIDEO_PHOSPHOR_MIN_ABI_MINOR ||
        shell_read(MT_REG_CORE_ID) != selection->package.core_id) return false;
    version = shell_read(MT_REG_ABI_VERSION);
    return (uint16_t)(version >> 16U) == MT_ABI_MAJOR &&
           (uint16_t)version >= MT_VIDEO_PHOSPHOR_MIN_ABI_MINOR;
}

static void configure_selection_effects(const mt_core_selection_t *selection)
{
    const mt_core_settings_profile_t *const profile = selection != NULL ?
        mt_supervisor_settings_find(&persistent_settings, selection->package.id) : NULL;
    mt_video_effect_service_configure(selection_supports_effects(selection),
        profile != NULL ? &profile->video_effects : NULL);
    supervisor_menu_dirty = true;
}

static bool amiga_apply_display(void *context,uint8_t mode)
{
    (void)context;
    if(mode>=MT_AMIGA_DISPLAY_MODES || shell_read(MT_REG_CORE_ID)!=MT_AMIGA_CORE_ID ||
       !(shell_read(MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_DISPLAY) || mt_screenshot_service_busy())return false;
    if(mode==3U && !(shell_read(MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_SHRES))return false;
    const uint32_t display_status=shell_read(MT_REG_DISPLAY_STATUS);
    if(display_status&(MT_DISPLAY_BUSY|MT_DISPLAY_HELD|MT_DISPLAY_ERROR))return false;
    mt_video_effect_service_suspend();
    const uint64_t began=monotonic_ms(NULL);
    while(!mt_video_effect_service_quiescent()) {
        usb_hid_service_poll_deferred();
        if(monotonic_ms(NULL)-began>=1000U) {
            configure_selection_effects(supervisor.has_active ? &supervisor.active:NULL);
            return false;
        }
    }
    const uint32_t host_status=shell_read(MT_AMIGA_HOST_PORT);
    if(host_status&(MT_AMIGA_HOST_BUSY|MT_AMIGA_HOST_ERROR)) {
        configure_selection_effects(supervisor.has_active ? &supervisor.active:NULL);
        return false;
    }
    uint32_t response=0U;
    const mt_link_result_t result=mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_DISPLAY_APPLY,
        mode,20000000U,&response);
    const uint32_t geometry=shell_read(MT_REG_VIDEO_FRAME_GEOMETRY);
    const uint32_t stride=shell_read(MT_REG_VIDEO_FRAME_STRIDE);
    const uint32_t expected_geometry=mode==3U ? (512U<<16)|1280U:
        mode==2U ? (566U<<16)|724U:(512U<<16)|640U;
    const bool applied=result==MT_LINK_OK && (response&0xffffU)==mode &&
        geometry==expected_geometry && stride==(mode==3U ? 2560U:mode==2U ? 1536U:1280U);
    if(result==MT_LINK_REMOTE_ERROR && !(shell_read(MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_BUSY))
        (void)mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_CLEAR_ERROR,0U,100000U,NULL);
    configure_selection_effects(supervisor.has_active ? &supervisor.active:NULL);
    mt_ui_presenter_invalidate(&supervisor_presenter);supervisor_menu_dirty=true;
    xil_printf("[Amiga] display profile=%u result=%s geometry=%08x stride=%u\r\n",
        (unsigned)mode,mt_link_result_name(result),(unsigned)geometry,(unsigned)stride);
    return applied;
}

static bool amiga_request_display(uint8_t mode)
{
    if(!amiga_service_initialized || shell_read(MT_REG_CORE_ID)!=MT_AMIGA_CORE_ID ||
       !amiga_display_service_mode_available(mode) || mt_screenshot_service_busy()) {
        set_supervisor_warning(amiga_display_service_status());return false;
    }
    const bool was_visible=supervisor_menu.visible;
    /* Dismiss the overlay and release guest input before publishing a request.
     * ScreenMode can ask for windows to close or display an OS requester. */
    if(!hide_menu_for_core_action())return false;
    supervisor_menu.visible=false;supervisor_menu_dirty=true;
    usb_hid_service_set_menu_capture(false);
    const uint32_t now_ms=(uint32_t)monotonic_ms(NULL);
    amiga_display_service_set_paused(false,now_ms);
    const bool accepted=amiga_display_service_request(mode,now_ms);
    if(accepted)set_supervisor_status("Changing Workbench resolution; answer any Amiga requester");
    else {
        supervisor_menu.visible=was_visible;
        usb_hid_service_set_menu_capture(was_visible);
        set_supervisor_warning(amiga_display_service_status());
    }
    return accepted;
}

static void video_effect_write(uint32_t offset, uint32_t value);
static uint32_t video_effect_milliseconds(void);
static const mt_display_ops_t display_bus = {
    shell_read, video_effect_write, video_effect_milliseconds
};

static bool selection_supports_video_multiplier(const mt_core_selection_t *selection)
{
    return selection != NULL && selection->package.core_id != MT_SUPERVISOR_CORE_ID &&
        selection->package.abi_major == MT_ABI_MAJOR &&
        selection->package.abi_minor >= MT_DISPLAY_SCALE_MIN_ABI_MINOR &&
        shell_read(MT_REG_CORE_ID) == selection->package.core_id &&
        mt_display_scale_supported(&display_bus);
}

static bool apply_display_geometry(uint8_t value, bool multiplier)
{
    if (multiplier ? !mt_display_scale_supported(&display_bus) : !mt_display_supported(&display_bus))
        return value == 0U;
    const uint32_t status = shell_read(MT_REG_DISPLAY_STATUS);
    const uint32_t mask = multiplier ? MT_DISPLAY_SCALE_MASK : UINT32_C(15);
    const uint32_t requested = multiplier ? (uint32_t)value << MT_DISPLAY_SCALE_SHIFT : value;
    if ((status & (MT_DISPLAY_BUSY | MT_DISPLAY_ERROR | MT_DISPLAY_HELD)) == 0U &&
        (status & mask) == requested) return true;
    mt_video_effect_service_suspend();
    const uint64_t started = monotonic_ms(NULL);
    while (!mt_video_effect_service_quiescent()) {
        usb_hid_service_poll_deferred();
        if (monotonic_ms(NULL) - started >= 1000U) return false;
    }
    const mt_display_result_t result = multiplier ? mt_display_apply_scale(&display_bus, value) :
                                                   mt_display_apply(&display_bus, value);
    /* Geometry commits reset the pixel overlay and effect tables. */
    mt_ui_presenter_invalidate(&supervisor_presenter);
    supervisor_menu_dirty = true;
    xil_printf("[display] %s=%u result=%u status=%08x\r\n", multiplier ? "size" : "mode",
               (unsigned)value, (unsigned)result, shell_read(MT_REG_DISPLAY_STATUS));
    return result == MT_DISPLAY_OK;
}

static bool apply_display_mode(uint8_t mode)
{
    return apply_display_geometry(mode, false);
}

static bool apply_video_multiplier(uint8_t multiplier)
{
    return apply_display_geometry(multiplier, true);
}

static void change_display_mode(size_t mode)
{
    if (mode >= MT_DISPLAY_MODE_COUNT || !mt_display_supported(&display_bus)) {
        set_supervisor_warning("Update the core package for display modes");
        return;
    }
    const uint8_t previous = persistent_settings.display_mode;
    bool ok = apply_display_mode((uint8_t)mode);
    if (ok) {
        persistent_settings.display_mode = (uint8_t)mode;
        if (!save_persistent_settings()) {
            persistent_settings.display_mode = previous;
            (void)apply_display_mode(previous);
            ok = false;
        }
    }
    configure_selection_effects(supervisor.has_active ? &supervisor.active : NULL);
    set_supervisor_outcome(ok, "Display resolution saved",
                          "Display change failed; see serial diagnostics");
}

static void change_active_video_multiplier(size_t multiplier)
{
    if (multiplier > MT_DISPLAY_SCALE_MAX || !supervisor.has_active ||
        !selection_supports_video_multiplier(&supervisor.active)) {
        set_supervisor_warning("Update the core package for video size");
        return;
    }
    const uint8_t previous = (uint8_t)((shell_read(MT_REG_DISPLAY_STATUS) &
        MT_DISPLAY_SCALE_MASK) >> MT_DISPLAY_SCALE_SHIFT);
    settings_before_update = persistent_settings;
    bool ok = apply_video_multiplier((uint8_t)multiplier);
    if (ok && (!ensure_active_settings_profile() ||
        !mt_supervisor_settings_remember_video_multiplier(&persistent_settings,
            supervisor.active.package.id, (uint8_t)multiplier) || !save_persistent_settings())) {
        persistent_settings = settings_before_update;
        if (!apply_video_multiplier(previous))
            xil_printf("[display] video size rollback failed\r\n");
        ok = false;
    }
    configure_selection_effects(&supervisor.active);
    set_supervisor_outcome(ok, "Video size saved", "Video size change failed; see serial diagnostics");
}

static void change_active_video_effect(size_t field, size_t direction)
{
    const mt_core_settings_profile_t *profile;
    mt_video_effects_settings_t effects;
    if (!supervisor.has_active || !selection_supports_effects(&supervisor.active) ||
        !mt_video_effect_service_available()) {
        set_supervisor_error("Video effects are unavailable");
        return;
    }
    profile = mt_supervisor_settings_find(&persistent_settings, supervisor.active.package.id);
    if (profile != NULL) effects = profile->video_effects;
    else mt_video_effects_defaults(&effects);
    if (!mt_video_effects_adjust(&effects, field, direction)) return;
    settings_before_update = persistent_settings;
    if (!ensure_active_settings_profile() ||
        !mt_supervisor_settings_remember_video_effects(&persistent_settings,
            supervisor.active.package.id, &effects) || !save_persistent_settings()) {
        persistent_settings = settings_before_update;
        set_supervisor_error("Effects unchanged; settings write failed");
        return;
    }
    mt_video_effect_service_configure(true, &effects);
    set_supervisor_status("Video effects saved for this core");
}

static uint32_t profile_vectrex_video_config(const mt_core_settings_profile_t *profile)
{
    const uint32_t gain = profile != NULL && profile->vectrex_beam_gain < 4U ?
        profile->vectrex_beam_gain : MT_SETTINGS_VECTREX_BEAM_GAIN_DEFAULT;
    const uint32_t opacity = profile != NULL && profile->vectrex_overlay_opacity < 4U ?
        profile->vectrex_overlay_opacity : MT_SETTINGS_VECTREX_OVERLAY_OPACITY_DEFAULT;
    return (profile == NULL || profile->vectrex_overlay_enabled ?
            MT_VECTREX_OVERLAY_ENABLE_MASK : UINT32_C(0)) |
           (gain << MT_VECTREX_BEAM_GAIN_SHIFT) | (opacity << MT_VECTREX_OVERLAY_OPACITY_SHIFT);
}

static void change_active_vectrex_video(size_t packed)
{
    const uint32_t value = (uint32_t)packed;
    if (!supervisor.has_active || !selection_supports_vectrex_video(&supervisor.active)) {
        set_supervisor_error("Vectrex video controls are unavailable");
        return;
    }
    if ((size_t)value != packed || (value & ~MT_VECTREX_VIDEO_CONFIG_MASK) != 0U) {
        set_supervisor_error("Invalid Vectrex video setting");
        return;
    }
    settings_before_update = persistent_settings;
    if (!ensure_active_settings_profile() ||
        !mt_supervisor_settings_remember_vectrex_video(&persistent_settings,
            supervisor.active.package.id, (value & MT_VECTREX_OVERLAY_ENABLE_MASK) != 0U,
            (uint8_t)((value & MT_VECTREX_BEAM_GAIN_MASK) >> MT_VECTREX_BEAM_GAIN_SHIFT),
            (uint8_t)((value & MT_VECTREX_OVERLAY_OPACITY_MASK) >> MT_VECTREX_OVERLAY_OPACITY_SHIFT)) ||
        !save_persistent_settings()) {
        persistent_settings = settings_before_update;
        set_supervisor_error("Vectrex video unchanged; settings write failed");
        return;
    }
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_VECTREX_VIDEO_CONFIG, value);
    set_supervisor_status("Video settings saved for Vectrex");
}

static uint32_t profile_border_config(const mt_core_settings_profile_t *profile)
{
    const uint32_t style = profile != NULL && profile->border_style < 5U ?
        profile->border_style : MT_SETTINGS_BORDER_STYLE_DEFAULT;
    const uint32_t color = profile != NULL && profile->border_color < 16U ?
        profile->border_color : MT_SETTINGS_BORDER_COLOR_DEFAULT;
    const uint32_t padding = profile != NULL && profile->border_padding >= 1U &&
        profile->border_padding <= 20U ? profile->border_padding :
        MT_SETTINGS_BORDER_PADDING_DEFAULT;
    return (profile != NULL && profile->border_enabled ? MT_BORDER_ENABLE_MASK : UINT32_C(0)) |
           (style << MT_BORDER_STYLE_SHIFT) | (color << MT_BORDER_COLOR_SHIFT) |
           (padding << MT_BORDER_PADDING_SHIFT);
}

static void change_active_border(size_t packed)
{
    const uint32_t value = (uint32_t)packed;
    const uint8_t style = (uint8_t)((value & MT_BORDER_STYLE_MASK) >> MT_BORDER_STYLE_SHIFT);
    const uint8_t color = (uint8_t)((value & MT_BORDER_COLOR_MASK) >> MT_BORDER_COLOR_SHIFT);
    const uint8_t padding = (uint8_t)((value & MT_BORDER_PADDING_MASK) >> MT_BORDER_PADDING_SHIFT);
    if (!supervisor.has_active || !selection_supports_borders(&supervisor.active)) {
        set_supervisor_error("Borders are unavailable for this core");
        return;
    }
    if ((size_t)value != packed ||
        (value & ~(MT_BORDER_ENABLE_MASK | MT_BORDER_STYLE_MASK |
                   MT_BORDER_COLOR_MASK | MT_BORDER_PADDING_MASK)) != 0U ||
        style > MT_BORDER_STYLE_MAX || padding < MT_BORDER_MIN_PADDING ||
        padding > MT_BORDER_MAX_PADDING) {
        set_supervisor_error("Invalid border setting");
        return;
    }
    settings_before_update = persistent_settings;
    if (!ensure_active_settings_profile() ||
        !mt_supervisor_settings_remember_borders(&persistent_settings,
            supervisor.active.package.id, (value & MT_BORDER_ENABLE_MASK) != 0U,
            style, color, padding) ||
        !save_persistent_settings()) {
        persistent_settings = settings_before_update;
        set_supervisor_error("Border unchanged; settings write failed");
        return;
    }
    /* The shell transfers this complete configuration at the next vblank. */
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_BORDER_CONFIG, value);
    set_supervisor_status("Border saved for this core");
}

static bool remember_active_drive8(const char *file_name)
{
    const char *const core_id = active_settings_core_id();
    return core_id != NULL && ensure_active_settings_profile() &&
           mt_supervisor_settings_remember_drive8(
               &persistent_settings, core_id, file_name) &&
           save_persistent_settings();
}

static bool remember_active_cartridge(const char *file_name)
{
    const char *const core_id = active_settings_core_id();
    return core_id != NULL && ensure_active_settings_profile() &&
           mt_supervisor_settings_remember_cartridge(
               &persistent_settings, core_id, file_name) &&
           save_persistent_settings();
}

static bool remember_c64_input_settings(void)
{
    const char *const core_id = active_settings_core_id();
    return core_id != NULL && ensure_active_settings_profile() &&
           mt_supervisor_settings_remember_c64_input(&persistent_settings, core_id,
                                                     usb_hid_service_symbolic_keyboard(),
                                                     stored_c64_joystick()) &&
           save_persistent_settings();
}

static bool remember_manual_selection(const mt_core_selection_t *selection)
{
    if (selection == NULL ||
        !mt_supervisor_settings_remember_variant(
            &persistent_settings, selection->package.id,
            selection->package.variants[selection->variant_index].id)) {
        return false;
    }
    if (selection->package.core_id == MT_C64_CORE_ID &&
        !mt_supervisor_settings_remember_c64_input(&persistent_settings, selection->package.id,
                                                   usb_hid_service_symbolic_keyboard(),
                                                   stored_c64_joystick())) {
        return false;
    }
    return save_persistent_settings();
}

static bool configured_auto_boot_selection(mt_core_selection_t *selection)
{
    const mt_core_settings_profile_t *profile;
    size_t core_index;
    if (selection == NULL || !persistent_settings.auto_boot ||
        persistent_settings.selected_core_id[0] == '\0') {
        return false;
    }
    profile = mt_supervisor_settings_find(
        &persistent_settings, persistent_settings.selected_core_id);
    if (profile == NULL) return false;
    for (core_index = 0U; core_index < core_catalog.count; ++core_index) {
        size_t variant_index;
        if (strcmp(core_catalog.cores[core_index].package.id,
                   persistent_settings.selected_core_id) != 0) {
            continue;
        }
        *selection = core_catalog.cores[core_index];
        for (variant_index = 0U;
             variant_index < selection->package.variant_count;
             ++variant_index) {
            if (strcmp(selection->package.variants[variant_index].id,
                       profile->variant_id) == 0) {
                selection->variant_index = variant_index;
                return true;
            }
        }
        return false;
    }
    return false;
}

static uint32_t selection_c64_expansion_caps(const mt_core_selection_t *selection)
{
    if (selection == NULL || selection->package.core_id != MT_C64_CORE_ID ||
        selection->package.abi_major != MT_ABI_MAJOR ||
        selection->package.abi_minor < MT_C64_EXPANSION_MIN_ABI_MINOR) return 0U;
    return c64_content_service_expansion_caps();
}

static mt_c64_capabilities_t selection_c64_features(const mt_core_selection_t *selection)
{
    if (selection == NULL) return (mt_c64_capabilities_t){0};
    return mt_c64_probe_capabilities(&shell_link, selection->package.core_id,
        selection->package.abi_major, selection->package.abi_minor);
}

static mt_c64_title_values_t c64_stored_values(const mt_core_selection_t *selection)
{
    mt_c64_title_values_t v;
    mt_c64_title_defaults(&v);
    const mt_core_settings_profile_t *p =
        selection ? mt_supervisor_settings_find(&persistent_settings, selection->package.id) : NULL;
    v.detail = persistent_settings.c64_turbo_detail;
    v.flags = persistent_settings.c64_turbo_flags;
    v.ua = persistent_settings.c64_ultimate_audio;
    v.uci = persistent_settings.c64_command_interface;
    v.turbo = p && p->c64_cpu_turbo;
    v.reu = p ? p->c64_reu_enabled : MT_SETTINGS_C64_REU_DEFAULT;
    v.sid_mode = p ? p->c64_sid_mode : MT_SETTINGS_C64_SID_MODE_DEFAULT;
    v.joystick = p ? p->joystick_port : 2;
    v.sid = c64_stored_sid;
    mt_c64_sid_table_t table;
    v.sid_custom = mt_c64_sid_build(&v.sid, 8, true, &table) && table.legacy_mode == 255;
    return v;
}
static uint8_t stored_c64_joystick(void)
{
    return c64_title_initialized
               ? c64_stored_values(supervisor.has_active ? &supervisor.active : NULL).joystick
               : usb_hid_service_joystick_port();
}
static void refresh_c64_title(const mt_core_selection_t *selection)
{
    if (!c64_title_initialized || !selection)
        return;
    const mt_c64_title_values_t v = c64_stored_values(selection);
    const mt_c64_capabilities_t caps = selection_c64_features(selection);
    c64_title_caps = caps;
    c64_legacy_turbo_available = selection->package.abi_minor >= 8U;
    const bool ntsc =
        strstr(selection->package.variants[selection->variant_index].id, "ntsc") != NULL;
    c64_title_service_refresh(&v, &caps, ntsc, !persistent_settings.c64_title_cfg_off);
}
static mt_c64_title_values_t c64_requested_values(const mt_core_selection_t *selection,
                                                  bool cartridge)
{
    mt_c64_title_values_t requested = c64_stored_values(selection), applied;
    refresh_c64_title(selection);
    if (c64_title_initialized)
        c64_title_service_effective(&requested, &applied, cartridge);
    return requested;
}
static int quiesce_c64_uci(void)
{
    if (!c64_uci_initialized)
        return 0;
    const uint64_t start = monotonic_ms(NULL);
    for (;;) {
        int r = c64_uci_service_quiesce();
        if (r <= 0)
            return r;
        service_usb_input();
        if (monotonic_ms(NULL) - start >= 1000U)
            return -1;
    }
}
static int retry_c64_uci(void)
{
    if (!c64_uci_initialized) return 0;
    const uint64_t start = monotonic_ms(NULL);
    for (;;) {
        const int result = c64_uci_service_retry();
        if (result <= 0) return result;
        usb_hid_service_poll_deferred();
        if (monotonic_ms(NULL) - start >= 1000U) return -1;
    }
}
static bool change_c64_directory_drive(void)
{
    if (!c64_iec_initialized || !c64_iec_service_available()) {
        set_supervisor_error("SD directory drive is unavailable");
        return false;
    }
    if (quiesce_c64_uci() != 0) {
        set_supervisor_warning("Command interface is busy");
        return false;
    }
    if (quiesce_c64_directory() != 0) {
        set_supervisor_error("SD directory could not close safely; retry storage");
        return false;
    }
    const bool previous = persistent_settings.c64_directory_drive_off;
    persistent_settings.c64_directory_drive_off = !previous;
    const bool applied = resume_c64_directory() == 0;
    if (applied && save_persistent_settings()) {
        set_supervisor_status(persistent_settings.c64_directory_drive_off ?
            "SD directory drive Off; UCI storage remains available" :
            "SD directory drive On");
        return true;
    }
    /* Restore the requested choice and bus state after an apply/save failure.
     * A failed close must not be hidden by automatically reopening storage. */
    const int closed = quiesce_c64_directory();
    persistent_settings.c64_directory_drive_off = previous;
    const bool restored = closed == 0 && resume_c64_directory() == 0;
    set_supervisor_error(!restored ? "SD directory restore failed; retry storage" :
        applied ? "SD directory drive setting was not saved" :
                  "SD directory drive could not start; check SD card");
    return false;
}
static int retry_c64_storage_for_reset(void)
{
    if (!shell_is_c64()) return 0;
    /* SoftIEC owns device9 channel leases. Its failed operation must close
     * before device9 can perform the user's explicit storage retry. */
    if (retry_c64_uci() != 0) return 1;
    if (c64_iec_initialized && c64_iec_service_available() &&
        c64_iec_service_reset() != 0) return 2;
    return 0;
}
static int apply_c64_title(void *context, const mt_c64_title_values_t *v, bool live, bool event)
{
    (void)context;
    if (c64_sid_commit_fault)
        return -1;
    const mt_c64_capabilities_t caps = c64_title_caps;
    if (mt_c64_title_io_conflict(v, &caps, c64_content_service_crt_attached()))
        return -1;
    const bool map = (caps.expansion & MT_C64_EXPANSION_CAPS_MULTI_SID) != 0;
    mt_c64_sid_table_t table;
    if (!mt_c64_sid_build(&v->sid, map ? ((caps.expansion >> 8) & 7) + 1 : 2,
                          (caps.expansion & MT_C64_EXPANSION_CAPS_SID_MODEL_8580) != 0, &table))
        return -1;
    if (!map && table.legacy_mode == 255)
        return -1;
    bool held = false, menu_hold = false;
    if (live) {
        const uint64_t start = monotonic_ms(NULL);
        if (c64_uci_initialized && c64_uci_service_available()) {
            for (;;) {
                int r = c64_uci_service_hold_begin();
                if (r == 0) {
                    held = true;
                    break;
                }
                if (r < 0 || monotonic_ms(NULL) - start >= 1000U) {
                    c64_uci_service_hold_end();
                    return -1;
                }
                service_usb_input();
            }
        } else {
            c64_debug_pause(NULL, true);
            menu_hold = true;
            while (caps.turbo && !(shell_read(MT_REG_C64_CPU_STATUS) & MT_C64_CPU_STATUS_PAUSE)) {
                service_usb_input();
                if (monotonic_ms(NULL) - start >= 1000U) {
                    c64_debug_pause(NULL, false);
                    return -1;
                }
            }
        }
    }
    int result = 0;
    if (map && table.legacy_mode == 255) {
        const uint32_t old = shell_read(MT_REG_C64_SID_STATUS);
        if (old & MT_C64_SID_STATUS_BUSY) {
            result = -1;
            goto done;
        }
        const uint8_t sequence = (uint8_t)(old + 1U);
        for (unsigned i = 0; i < MT_C64_SID_WORDS; ++i) {
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_SID_INDEX, i);
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_SID_DATA, table.words[i]);
        }
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_SID_COMMIT, sequence);
        const uint64_t start = monotonic_ms(NULL);
        for (;;) {
            const uint32_t s = shell_read(MT_REG_C64_SID_STATUS);
            if ((s & MT_C64_SID_STATUS_VALID) && !(s & MT_C64_SID_STATUS_BUSY) &&
                (uint8_t)s == sequence)
                break;
            if (s & MT_C64_SID_STATUS_REJECTED) {
                result = -1;
                goto done;
            }
            if (monotonic_ms(NULL) - start >= 1000U) {
                /* An accepted bank can still become active after a delayed ACK.
                 * Preserve HOLD; only reloading the FPGA cancels this ambiguity. */
                c64_sid_commit_fault = true;
                c64_uci_service_hold_fault();
                xil_printf(
                    "[C64] SID commit timed out; core remains held. Reload the C64 core.\r\n");
                result = -1;
                goto done;
            }
            service_usb_input();
        }
    }
    {
        uint32_t expansion =
            mt_c64_expansion_restore(table.legacy_mode == 255 ? v->sid_mode : table.legacy_mode,
                                     v->reu, v->ua, false, &caps);
        if (v->uci && (caps.expansion & MT_C64_EXPANSION_CAPS_UCI))
            expansion |= MT_C64_EXPANSION_UCI_ENABLE;
        if (map && table.legacy_mode == 255)
            expansion |= MT_C64_EXPANSION_SID_MAP_ENABLE;
        if (caps.expansion & MT_C64_EXPANSION_CAPS_VALID)
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_EXPANSION_CONFIG, expansion);
        if (!c64_uci_applying_config && (caps.expansion & MT_C64_EXPANSION_CAPS_UCI_UNLOCK) &&
            (shell_read(MT_REG_ABI_VERSION) & 0xffff) >= MT_C64_UCI315_MIN_ABI_MINOR)
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_PERIPHERAL_CONTROL,
                shell_read(MT_REG_C64_PERIPHERAL_CONTROL) ^ MT_C64_PERIPHERAL_UCI_CONFIG_EPOCH);
        if (c64_uci_initialized) c64_uci_service_refresh_control();
        if (caps.turbo) {
            const uint32_t previous = shell_read(MT_REG_C64_CPU_CONFIG);
            const uint32_t config =
                mt_c64_turbo_config(v->detail, v->flags, v->turbo, &caps, previous, false);
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_CPU_CONFIG,
                      mt_c64_debug_effective_overlay(&c64_debug, config, previous, event));
        }
        set_c64_cpu_turbo(c64_legacy_turbo_available && v->turbo);
        (void)usb_hid_service_set_joystick_port(v->joystick);
        c64_live_sid_table = table;
    }
done:
    if (!c64_sid_commit_fault) {
        if (held)
            c64_uci_service_hold_end();
        if (menu_hold)
            c64_debug_pause(NULL, false);
    }
    return result;
}
static uint64_t uci_microseconds(void *context)
{
    (void)context;
    return monotonic_ms(NULL) * 1000U;
}
static bool uci_get_time(void *context, mt_clock_time_t *value)
{
    (void)context;
    if (service_fatfs_time() != 0 || !board_clock_valid)
        return false;
    *value = board_clock;
    return true;
}
static bool uci_set_time(void *context, const mt_clock_time_t *value)
{
    (void)context;
    return set_board_clock(value);
}
static int uci_apply_config(void *context, const char *path, char *report, size_t capacity)
{
    (void)context;
    refresh_c64_title(supervisor.has_active ? &supervisor.active : NULL);
    /* A software-unlocked interface must survive its own LOAD_CONFIG reply.
     * Explicit menu/title changes still revoke the unlock through the epoch. */
    c64_uci_applying_config = true;
    const int result = c64_title_service_apply(path, true, report, capacity);
    c64_uci_applying_config = false;
    return result;
}
static size_t uci_sid_info(void *context, unsigned command, uint8_t *data, size_t capacity)
{
    (void)context;
    return mt_c64_sid_hwinfo(&c64_live_sid_table, command, data, capacity);
}
static int initialize_c64_extensions(const mt_core_selection_t *selection)
{
    c64_extension_selection = *selection;
    c64_sid_commit_fault = false;
    const mt_core_settings_profile_t *profile =
        mt_supervisor_settings_find(&persistent_settings, selection->package.id);
    if (!mt_supervisor_sid_map_load(
            selection->package.id,
            profile ? profile->c64_sid_mode : MT_SETTINGS_C64_SID_MODE_DEFAULT, &c64_stored_sid))
        xil_printf("[C64] SID companion unavailable; legacy preset retained\r\n");
    if (!(selection_c64_features(selection).expansion & MT_C64_EXPANSION_CAPS_MULTI_SID)) {
        const unsigned legacy = profile ? profile->c64_sid_mode : MT_SETTINGS_C64_SID_MODE_DEFAULT;
        (void)mt_c64_sid_preset(&c64_stored_sid, legacy == 0   ? MT_SID_SINGLE
                                                 : legacy == 2 ? MT_SID_DUAL420
                                                               : MT_SID_DUAL500);
        /* Keep the companion file intact for the next capable image. */
    }
    const c64_title_callbacks_t title_callbacks = {NULL, apply_c64_title};
    if (c64_title_service_init(selection->package.content_root, &title_callbacks) != 0)
        return -1;
    c64_title_initialized = true;
    c64_title_foreground = false;
    refresh_c64_title(selection);
    const c64_uci_callbacks_t uci_callbacks = {
        NULL,         uci_microseconds, c64_iec_path_protected,
        uci_get_time, uci_set_time,     uci_apply_config,
        uci_sid_info};
    if (c64_uci_service_init(&shell_link, selection->package.content_root, &uci_callbacks) != 0)
        return -1;
    c64_uci_initialized = true;
    uint8_t card_identity[16];
    if (mt_sd_storage_card_identity(card_identity))
        mt_net_service_seed(card_identity, sizeof(card_identity));
    mt_net_service_init(&shell_link);
    return 0;
}

static void configure_c64_turbo(const mt_core_selection_t *selection,
    const mt_core_settings_profile_t *profile, bool apply)
{
    const mt_c64_capabilities_t caps = selection_c64_features(selection);
    (void)profile;
    const mt_c64_title_values_t values =
        c64_requested_values(selection, c64_content_service_crt_attached());
    const bool enabled = values.turbo;
    if (caps.turbo) {
        const uint32_t previous = shell_read(MT_REG_C64_CPU_CONFIG);
        const uint32_t stored =
            mt_c64_turbo_config(values.detail, values.flags, enabled, &caps, previous, false);
        const uint32_t config = mt_c64_debug_effective_overlay(&c64_debug,
            stored, previous, apply);
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_C64_CPU_CONFIG, config);
    }
    set_c64_cpu_turbo(selection->package.abi_minor >= 8U && enabled);
}

static void configure_c64_expansion(const mt_core_selection_t *selection,
                                     const mt_core_settings_profile_t *profile)
{
    (void)profile;
    refresh_c64_title(selection);
    mt_c64_title_values_t requested, applied;
    c64_title_service_effective(&requested, &applied, c64_content_service_crt_attached());
    if (apply_c64_title(NULL, &applied, false, false) != 0)
        xil_printf("[C64] expansion configuration was not applied\r\n");
}

static bool restore_selection_settings(
    const mt_core_selection_t *selection)
{
    const mt_core_settings_profile_t *profile;
    bool ok = true;
    if (selection == NULL) return false;
    profile = mt_supervisor_settings_find(&persistent_settings,
                                          selection->package.id);
    if (mt_display_supported(&display_bus) && !apply_display_mode(persistent_settings.display_mode)) {
        xil_printf("[display] saved resolution failed; using hardware fallback\r\n");
        ok = false;
    }
    if (selection_supports_video_multiplier(selection) &&
        !apply_video_multiplier(profile != NULL ? profile->video_multiplier : 0U)) {
        xil_printf("[display] saved video size failed; using hardware fallback\r\n");
        ok = false;
    }
    configure_selection_effects(selection);
    if (selection_supports_borders(selection)) {
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_BORDER_CONFIG,
                  profile_border_config(profile));
    }
    if (selection_supports_vectrex_video(selection)) {
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_VECTREX_VIDEO_CONFIG,
                  profile_vectrex_video_config(profile));
    }
    if (selection->package.core_id == MT_C64_CORE_ID) {
        mt_c64_debug_clear_override(&c64_debug);
        configure_c64_expansion(selection, profile);
        configure_c64_turbo(selection, profile, false);
        if (profile != NULL) {
            if (usb_hid_service_set_symbolic_keyboard(profile->symbolic_keyboard) != 0 ||
                usb_hid_service_set_joystick_port(
                    c64_requested_values(selection, c64_content_service_crt_attached()).joystick) !=
                    0) {
                xil_printf("[settings] C64 input restore failed\r\n");
                ok = false;
            }
            if (profile->cartridge_name[0] != '\0') {
                mt_c64_content_service_result_t content;
                if (!prepare_c64_content_change()) {
                    ok = false;
                } else {
                    content = c64_title_load(profile->cartridge_name, false);
                    print_c64_content_result(
                        "settings restore", profile->cartridge_name,
                        &content);
                    finish_c64_content_change();
                    if (content.status != MT_C64_CONTENT_SERVICE_OK)
                        ok = false;
                }
            }
            if (profile->drive8_name[0] != '\0') {
                if (!c64_drive_ready ||
                    c64_d64_service_ensure_mounted(
                        profile->drive8_name) != 0) {
                    xil_printf("[settings] drive8 restore failed: %s\r\n",
                               profile->drive8_name);
                    ok = false;
                }
            }
        } else if (settings_missing_on_boot && c64_drive_ready &&
                   c64_d64_service_mount_default() == 0) {
            /* Preserve first-run convenience when the first manually chosen
             * core is C64, without making C64 the boot core. The subsequent
             * manual-selection save persists this mount. */
            (void)mt_supervisor_settings_remember_variant(
                &persistent_settings, selection->package.id,
                selection->package.variants[selection->variant_index].id);
            (void)mt_supervisor_settings_remember_drive8(
                &persistent_settings, selection->package.id,
                MT_C64_DEFAULT_D64_NAME);
        }
        return ok;
    }
    if (selection->package.core_id == MT_GBC_CORE_ID) {
        if (profile != NULL && profile->cartridge_name[0] != '\0') {
            const mt_gameboy_content_service_result_t content =
                gameboy_content_service_load(profile->cartridge_name);
            if (content.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                usb_hid_service_set_core_ready(true);
                if (dac_ready) set_audio(false, false);
                set_video_test(false);
                xil_printf("[settings] GBC cartridge restored: %s\r\n",
                           profile->cartridge_name);
                return true;
            }
            xil_printf("[settings] GBC cartridge restore failed: %s (%s)\r\n",
                       profile->cartridge_name,
                       gameboy_content_service_status_name(content.status));
            ok = false;
        }
        usb_hid_service_set_core_ready(false);
        if (dac_ready) set_audio(true, false);
        set_video_test(true);
        return ok;
    }
    if (selection->package.core_id == MT_GENESIS_CORE_ID) {
        mt_genesis_config_t options = *genesis_content_service_options();
        options.six_button_mask = (uint8_t)(3U ^ (profile != NULL ?
            profile->genesis_three_button_mask : 0U));
        if (genesis_content_service_set_options(&options) != 0) return false;
        if (profile != NULL && profile->cartridge_name[0] != '\0') {
            const mt_genesis_content_service_result_t content =
                load_genesis_content_with_input(profile->cartridge_name);
            if (content.status == MT_GENESIS_CONTENT_SERVICE_OK) {
                usb_hid_service_set_core_ready(true);
                if (dac_ready) set_audio(false, false);
                set_video_test(false);
                xil_printf("[settings] GENESIS cartridge restored: %s\r\n",
                           profile->cartridge_name);
                return true;
            }
            xil_printf("[settings] GENESIS cartridge restore failed: %s (%s)\r\n",
                       profile->cartridge_name,
                       genesis_content_service_status_name(content.status));
            ok = false;
        }
        usb_hid_service_set_core_ready(false);
        if (dac_ready) set_audio(true, false);
        set_video_test(true);
        return ok;
    }
    if (selection->package.core_id == MT_NES_CORE_ID) {
        if (profile != NULL && profile->cartridge_name[0] != '\0') {
            const mt_nes_content_service_result_t content =
                load_nes_content_with_input(profile->cartridge_name);
            if (content.status == MT_NES_CONTENT_SERVICE_OK) {
                usb_hid_service_set_core_ready(true);
                if (dac_ready) set_audio(false, false);
                set_video_test(false);
                xil_printf("[settings] NES cartridge restored: %s\r\n",
                           profile->cartridge_name);
                return true;
            }
            xil_printf("[settings] NES cartridge restore failed: %s (%s)\r\n",
                       profile->cartridge_name,
                       nes_content_service_status_name(content.status));
            ok = false;
        }
        usb_hid_service_set_core_ready(false);
        if (dac_ready) set_audio(true, false);
        set_video_test(true);
        return ok;
    }
    if (selection->package.core_id == MT_VECTREX_CORE_ID && profile != NULL &&
        profile->cartridge_name[0] != '\0') {
        const mt_vectrex_content_result_t content =
            vectrex_content_service_load(profile->cartridge_name);
        const bool loaded = content.status == MT_VECTREX_CONTENT_OK;
        usb_hid_service_set_core_ready(loaded);
        if (dac_ready) set_audio(!loaded, false);
        set_video_test(!loaded);
        return loaded;
    }
    return true;
}

static mt_menu_view_t supervisor_menu_view(void)
{
    mt_menu_view_t view;
    mt_core_frontend_state_t state = {0};
    memset(&view, 0, sizeof(view));
    view.cores = core_catalog.cores;
    view.core_count = core_catalog.count;
    if (supervisor.has_active) {
        const mt_core_variant_t *const variant =
            &supervisor.active.package.variants[supervisor.active.variant_index];
        view.active_core_id = supervisor.active.package.id;
        view.active_variant_id = variant->id;
        view.content_root = supervisor.active.package.content_root;
        state.core_id = supervisor.active.package.core_id;
        state.abi_minor = supervisor.active.package.abi_minor;
    }
    if (supervisor.has_active &&
        supervisor.active.package.core_id == MT_C64_CORE_ID &&
        c64_services_initialized) {
        state.services_ready = true;
        state.drive8_ready = c64_drive_ready;
        state.drive8_mounted = c64_d64_service_mounted();
        state.drive8_d64_mounted = c64_d64_service_mounted_d64();
        state.drive8_name = c64_d64_service_mounted_name();
        state.cartridge_attached = c64_content_service_crt_attached();
        state.cartridge_name = c64_content_service_crt_name();
        state.save_active = c64_content_service_save_active();
        state.save_dirty = c64_content_service_save_dirty();
        state.save_faulted = c64_content_service_save_faulted();
        state.c64_ef_jumper_disabled = c64_content_service_ef_jumper_disabled();
        state.c64_cartridge_policy = c64_content_service_crt_expansion_policy();
        if (!c64_content_service_reu_load_allowed())
            state.c64_cartridge_policy =
                (uint8_t)(state.c64_cartridge_policy & ~MT_C64_CRT_ALLOW_REU);
        state.c64_expansion_caps = selection_c64_expansion_caps(&supervisor.active);
        const mt_c64_capabilities_t features = selection_c64_features(&supervisor.active);
        if (features.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST)
            state.c64_uci_active = (shell_read(MT_REG_C64_REU_HOST) &
                                    MT_C64_REU_HOST_UCI_ACTIVE) != 0U;
        state.c64_turbo_available = features.turbo;
        state.c64_ua_available = features.ultimate_audio;
        state.c64_reu_host_available = features.reu_host;
        state.c64_cpu_caps = features.cpu;
        state.c64_cpu_caps2 = features.cpu2;
        state.c64_turbo_detail = persistent_settings.c64_turbo_detail;
        state.c64_turbo_flags = persistent_settings.c64_turbo_flags;
        if (features.turbo) {
            state.c64_cpu_config = shell_read(MT_REG_C64_CPU_CONFIG);
            state.c64_cpu_status = shell_read(MT_REG_C64_CPU_STATUS);
        }
        if (features.ultimate_audio)
            state.c64_ua_fault = (shell_read(MT_REG_C64_UA_STATUS) & MT_C64_UA_STATUS_FAULT) != 0U;
        if (state.c64_expansion_caps != 0U) {
            const uint32_t config = shell_read(MT_REG_C64_EXPANSION_CONFIG);
            state.c64_sid_mode = (uint8_t)(config & MT_C64_EXPANSION_SID_MODE_MASK);
            state.c64_reu_enabled = (config & MT_C64_EXPANSION_REU_ENABLE) != 0U;
            state.c64_ua_enabled = features.ultimate_audio &&
                (config & MT_C64_EXPANSION_ULTIMATE_AUDIO_ENABLE) != 0U;
        }
        state.fast_roms_active = c64_fast_roms_active;
        state.fast_roms_fallback = c64_fast_roms_fallback;
        const mt_core_settings_profile_t *const c64_profile =
            mt_supervisor_settings_find(&persistent_settings, supervisor.active.package.id);
        state.cpu_turbo_enabled = features.turbo ? (c64_profile != NULL && c64_profile->c64_cpu_turbo) :
            (shell_read(MT_REG_CONTROL) & MT_CONTROL_C64_CPU_TURBO_ENABLE) != 0U;
        state.symbolic_keyboard = usb_hid_service_symbolic_keyboard();
        state.joystick_port = usb_hid_service_joystick_port();
        const mt_c64_title_values_t values =
            c64_requested_values(&supervisor.active, state.cartridge_attached);
        state.c64_turbo_detail = values.detail;
        state.c64_turbo_flags = values.flags;
        state.cpu_turbo_enabled = values.turbo;
        state.c64_reu_enabled = values.reu;
        state.c64_ua_enabled = values.ua;
        state.c64_sid_mode = values.sid_mode;
        state.c64_sid = values.sid;
        state.c64_uci_enabled = values.uci;
        state.c64_directory_drive_available = c64_iec_initialized && c64_iec_service_available();
        state.c64_directory_drive_enabled = !persistent_settings.c64_directory_drive_off;
        state.c64_title_automatic = !persistent_settings.c64_title_cfg_off;
        state.c64_title_session = *c64_title_service_session() != 0;
        state.c64_title_source = c64_title_service_overlay()->source;
        state.c64_title_owned = c64_title_service_overlay()->owned;
        state.c64_sid_owned = c64_title_service_overlay()->sid_fields;
        state.c64_sid_classes = c64_title_service_overlay()->sid_classes;
        state.c64_sid_setup = supervisor_menu.core_subpage;
        state.c64_selected_sid = c64_selected_sid;
    } else if (supervisor.has_active && state.core_id == MT_SPECTRUM_CORE_ID) {
        state.services_ready = true;
        state.spectrum_ready = (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U;
        state.spectrum_boot_error = spectrum_boot_error;
        state.spectrum_tape_name = spectrum_content_tape_name();
    } else if (supervisor.has_active &&
               supervisor.active.package.core_id == MT_GBC_CORE_ID &&
               gameboy_service_initialized) {
        state.services_ready = true;
        state.cartridge_attached =
            gameboy_content_service_cartridge_attached();
        state.cartridge_name = gameboy_content_service_cartridge_name();
        state.save_active = gameboy_content_service_save_active();
        state.save_dirty = gameboy_content_service_save_dirty();
        state.save_faulted = gameboy_content_service_save_faulted();
    } else if (supervisor.has_active &&
               supervisor.active.package.core_id == MT_GENESIS_CORE_ID &&
               genesis_service_initialized) {
        state.services_ready = true;
        state.cartridge_attached =
            genesis_content_service_cartridge_attached();
        state.cartridge_name = genesis_content_service_cartridge_name();
        state.save_active = genesis_content_service_save_active();
        state.save_dirty = genesis_content_service_save_dirty();
        state.save_faulted = genesis_content_service_save_faulted();

        state.genesis_three_button_mask = (uint8_t)(3U ^ genesis_content_service_options()->six_button_mask);
    } else if (supervisor.has_active &&
               supervisor.active.package.core_id == MT_NES_CORE_ID &&
               nes_service_initialized) {
        state.services_ready = true;
        state.cartridge_attached =
            nes_content_service_cartridge_attached();
        state.cartridge_name = nes_content_service_cartridge_name();
        state.save_active = nes_content_service_save_active();
        state.save_dirty = nes_content_service_save_dirty();
        state.save_faulted = nes_content_service_save_faulted();

    } else if(supervisor.has_active && supervisor.active.package.core_id==MT_AMIGA_CORE_ID &&
        amiga_service_initialized) {
        state.services_ready=true;state.amiga_ready=amiga_content_service_ready();
        state.amiga_config=*amiga_content_service_config();state.save_faulted=amiga_content_service_faulted();
        state.amiga_aga_pal=amiga_content_service_aga_pal();
        state.amiga_ap68040=amiga_content_service_ap68040();
        state.amiga_ap68040_2x_available=amiga_content_service_ap68040_2x_available();
        state.amiga_cpu14_available=amiga_content_service_cpu14_available();
        state.amiga_cpu14_fallback=amiga_content_service_cpu14_fallback();
        state.amiga_fast_ide_available=amiga_content_service_fast_ide_available();
        state.amiga_fast_ide_expected=amiga_content_service_fast_ide_expected();
        state.amiga_display_available=amiga_display_service_available();
        state.amiga_display_mode_count=amiga_display_service_mode_count();
        state.amiga_display_pending=amiga_display_service_pending();
        state.amiga_display_requested=amiga_display_service_requested();
        state.amiga_display_applied=amiga_display_service_applied();
        state.amiga_display_status=amiga_display_service_status();
        for(unsigned d=0U;d<MT_AMIGA_DRIVE_COUNT;++d)state.amiga_drive_name[d]=amiga_content_service_name(d);
    } else if(supervisor.has_active && supervisor.active.package.core_id==MT_IIGS_CORE_ID &&
        iigs_service_initialized) {
        state.services_ready=true;
        state.iigs_ready=iigs_content_service_ready();
        state.iigs_config=*iigs_content_service_config();
        state.iigs_speed_mask=iigs_content_service_speed_mask();
        state.iigs_fast_floppy_supported=iigs_content_service_fast_floppy_supported();
        state.iigs_mouse_sensitivity=iigs_content_service_mouse_sensitivity();
        state.save_faulted=iigs_content_service_faulted();
        for(unsigned d=0U;d<MT_IIGS_DRIVE_COUNT;++d)state.iigs_drive_name[d]=iigs_content_service_name(d);
    } else if (supervisor.has_active &&
               supervisor.active.package.core_id == MT_VECTREX_CORE_ID &&
               vectrex_service_initialized) {
        state.services_ready = true;
        state.cartridge_attached = vectrex_content_service_cartridge_attached();
        state.cartridge_name = vectrex_content_service_cartridge_name();
    }
    view.vectrex_video_supported = supervisor.has_active &&
        selection_supports_vectrex_video(&supervisor.active);
    mt_core_frontend_apply(&supervisor_frontend, &state, &view);
    if (view.input_item_count < MT_MENU_MAX_ITEMS) {
        mt_menu_item_t *item = &supervisor_frontend.input_items[view.input_item_count++];
        memset(item, 0, sizeof(*item));
        item->label = "Controllers";
        item->value = "Buttons, players and sticks";
        item->help = "Choose a connected controller, then press an input to assign it. Changes apply only to the running core. F12 always opens the menu.";
        item->action = MT_MENU_ACTION_EDIT_GAMEPADS;
        item->enabled = supervisor.has_active;
        item->value_color = MT_UI_COLOR_BLUE;
    }
    view.gamepad_core_id = state.core_id;
    view.gamepad_now_ms = monotonic_ms(NULL);
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        (void)usb_hid_service_controller_device(slot, &view.gamepad_devices[slot]);
        view.gamepad_player[slot] = usb_hid_service_controller_player(slot);
        if (supervisor_menu.gamepad_edit && !controller_edit_devices[slot].connected &&
            view.gamepad_devices[slot].connected) {
            /* An empty editor entry can safely adopt a newly connected pad. */
            controller_edit_devices[slot] = view.gamepad_devices[slot];
            (void)usb_hid_service_get_controller_config(slot, &supervisor_menu.gamepad_config[slot]);
            supervisor_menu.gamepad_original[slot] = supervisor_menu.gamepad_config[slot];
        }
        if (supervisor_menu.gamepad_edit && controller_edit_devices[slot].connected &&
            !mt_controls_identity_equal(&controller_edit_devices[slot].identity,
                                       &view.gamepad_devices[slot].identity)) {
            if (view.gamepad_devices[slot].connected && slot == supervisor_menu.gamepad_slot)
                (void)snprintf(supervisor_menu.gamepad_notice, sizeof(supervisor_menu.gamepad_notice),
                    "Controller replaced. Save or discard these changes, then reopen Controllers for the new device.");
            view.gamepad_devices[slot] = controller_edit_devices[slot];
            view.gamepad_devices[slot].connected = 0U;
        }
    }
    usb_hid_service_controller_snapshot(supervisor_menu.gamepad_slot, &view.gamepad_preview, NULL);
    if (!view.gamepad_devices[supervisor_menu.gamepad_slot].connected)
        memset(&view.gamepad_preview, 0, sizeof(view.gamepad_preview));
    view.status_text = supervisor_status_text;
    view.status_color = supervisor_status_color;
    if (supervisor_menu.page == MT_MENU_PAGE_EFFECTS &&
        supervisor_status_color != MT_UI_COLOR_RED && supervisor_status_color != MT_UI_COLOR_WARNING) {
        view.status_text = mt_video_effect_service_status();
        view.status_color = MT_UI_COLOR_MUTED;
    }
    if (supervisor.has_active && supervisor.active.package.core_id == MT_AMIGA_CORE_ID &&
        amiga_service_initialized && !state.amiga_ready && state.save_faulted &&
        view.status_color != MT_UI_COLOR_RED) {
        view.status_text = amiga_content_service_error();
        view.status_color = MT_UI_COLOR_RED;
    }
    view.files = supervisor_menu_files;
    view.file_count = supervisor_menu_file_count;
    if (mt_core_frontend_browser_folders(supervisor.has_active ? supervisor.active.package.core_id:0U,
        supervisor_menu.browser) &&
        supervisor_menu.browser != MT_MENU_BROWSER_NONE &&
        vectrex_browser_path[0] != '\0')
        view.content_root = vectrex_browser_path;
    view.display_supported = mt_display_supported(&display_bus);
    view.display_mode = view.display_supported ?
        (uint8_t)(shell_read(MT_REG_DISPLAY_STATUS) & 15U) : 0U;
    view.video_multiplier_supported = supervisor.has_active &&
        selection_supports_video_multiplier(&supervisor.active);
    view.video_multiplier = view.video_multiplier_supported ?
        (uint8_t)((shell_read(MT_REG_DISPLAY_STATUS) & MT_DISPLAY_SCALE_MASK) >> MT_DISPLAY_SCALE_SHIFT) : 0U;
    view.video_test_enabled =
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_VIDEO_TEST_ENABLE) != 0U;
    view.settings_auto_boot = persistent_settings.auto_boot &&
        supervisor.has_active &&
        strcmp(persistent_settings.selected_core_id,
               supervisor.active.package.id) == 0;
    view.settings_faulted = settings_faulted;
    view.settings_drive_activity = !persistent_settings.drive_activity_off;
    {
        const mt_core_settings_profile_t *const profile = supervisor.has_active ?
            mt_supervisor_settings_find(&persistent_settings, supervisor.active.package.id) : NULL;
        const uint32_t border = profile_border_config(profile);
        mt_video_effects_frontend_apply(&effects_frontend,
            profile != NULL ? &profile->video_effects : NULL,
            supervisor.has_active && mt_video_effect_service_available(),
            mt_video_effect_service_status(), &view);
        view.border_supported = supervisor.has_active &&
            selection_supports_borders(&supervisor.active);
        view.border_enabled = (border & MT_BORDER_ENABLE_MASK) != 0U;
        view.border_style = (uint8_t)((border & MT_BORDER_STYLE_MASK) >> MT_BORDER_STYLE_SHIFT);
        view.border_color = (uint8_t)((border & MT_BORDER_COLOR_MASK) >> MT_BORDER_COLOR_SHIFT);
        view.border_padding = (uint8_t)((border & MT_BORDER_PADDING_MASK) >> MT_BORDER_PADDING_SHIFT);
        {
            const uint32_t video = profile_vectrex_video_config(profile);
            uint32_t overlay_status = 0U;
            if (view.vectrex_video_supported)
                overlay_status = shell_read(MT_REG_VECTREX_OVERLAY_STATUS);
            view.vectrex_overlay_enabled = (video & MT_VECTREX_OVERLAY_ENABLE_MASK) != 0U;
            view.vectrex_beam_gain = (uint8_t)((video & MT_VECTREX_BEAM_GAIN_MASK) >> MT_VECTREX_BEAM_GAIN_SHIFT);
            view.vectrex_overlay_opacity = (uint8_t)((video & MT_VECTREX_OVERLAY_OPACITY_MASK) >> MT_VECTREX_OVERLAY_OPACITY_SHIFT);
            view.vectrex_overlay_loaded = (overlay_status & MT_VECTREX_OVERLAY_STATUS_LOADED) != 0U;
            view.vectrex_overlay_error = (overlay_status & MT_VECTREX_OVERLAY_STATUS_ERROR) != 0U ||
                (view.vectrex_video_supported && vectrex_content_service_overlay_warning()[0] != '\0');
        }
    }
    {
        mt_usb_hid_status_t usb;
        mt_system_snapshot_t snapshot = {0};
        usb_hid_service_get_status(&usb);
        snapshot.firmware_build = MT_FIRMWARE_BUILD_ID;
        snapshot.firmware_version = MT_FIRMWARE_VERSION;
        snapshot.golden_version = installed_golden_version;
        snapshot.golden_update_present = golden_update_present;
        snapshot.shell_build = shell_read(MT_REG_SHELL_BUILD_ID);
        snapshot.abi = shell_read(MT_REG_ABI_VERSION);
        snapshot.uptime_seconds = monotonic_ms(NULL) / 1000U;
        snapshot.temperature_valid = board_temperature_valid;
        snapshot.temperature_micro_c = board_temperature_micro_c;
        snapshot.clock_available = board_clock_available;
        snapshot.clock_valid = board_clock_valid;
        snapshot.clock = board_clock;
        static char network_status[96];
        mt_net_service_status(network_status, sizeof(network_status));
        snapshot.network_available = c64_uci_initialized && shell_is_c64();
        snapshot.network_status = network_status;
        snapshot.usb_started = usb.started != 0U;
        snapshot.keyboards = usb.keyboards;
        snapshot.gamepads = usb.gamepads;
        snapshot.usb_submit_errors = usb.submit_errors;
        snapshot.usb_transfer_errors = usb.transfer_errors;
        snapshot.usb_input_errors = usb.input_errors;
        snapshot.usb_last_error = usb.last_error;
        snapshot.catalog_status = mt_sd_status_name(core_catalog.sd.status);
        snapshot.catalog_count = core_catalog.count;
        snapshot.catalog_rejected = core_catalog.rejected;
        snapshot.dac_ready = dac_ready;
        snapshot.multiboot_ready = multiboot_status.golden_available != 0;
        snapshot.appletini_present = multiboot_status.appletini_present != 0;
        snapshot.multitini_present = multiboot_status.multitini_present != 0;
        snapshot.startup_product = multiboot_status.default_product;
        snapshot.audio_muted = supervisor_menu.visible ||
            (shell_read(MT_REG_CONTROL) & MT_CONTROL_AUDIO_MUTE) != 0U;
        mt_system_frontend_apply(&system_frontend, &snapshot, &view);
        view.text_reader = &document_reader;
        view.text_title = document_title;
    }
    return view;
}

static void show_handoff_progress(const char *message)
{
    const mt_ui_bus_t bus = {NULL, supervisor_ui_read, supervisor_ui_write};
    const uint64_t started = monotonic_ms(NULL);
    mt_menu_view_t view;
    set_supervisor_status(message);
    view = supervisor_menu_view();
    mt_menu_render(&supervisor_menu, &view, &supervisor_frame);
    do {
        const mt_ui_present_result_t presented = mt_ui_present_cached(
            &supervisor_presenter, &bus, &supervisor_frame, true);
        if (presented == MT_UI_PRESENT_OK && !supervisor_presenter.pending) break;
        if (presented != MT_UI_PRESENT_OK && presented != MT_UI_PRESENT_BUSY) break;
        /* This function runs inside the deferred menu action. Keep ordinary
         * USB polling alive without dispatching nested UI or storage work. */
        usb_hid_service_poll_deferred();
        usleep(1000U);
    } while (monotonic_ms(NULL) - started < MT_MENU_HIDE_TIMEOUT_MS);
}

static bool is_text_document(const char *name)
{
    const size_t length = name == NULL ? 0U : strlen(name);
    if (length < 4U) return false;
    name += length - 4U;
    return name[0] == '.' && (name[1] == 't' || name[1] == 'T') &&
        (name[2] == 'x' || name[2] == 'X') && (name[3] == 't' || name[3] == 'T');
}

static int collect_menu_file(void *context, const char *name, size_t size,
                             bool is_directory)
{
    const mt_menu_browser_t browser = *(const mt_menu_browser_t *)context;
    const mt_content_type_t type = mt_content_classify_for_core(
        supervisor.has_active ? supervisor.active.package.core_id : 0U, name);
    size_t name_size;
    bool accepted;
    const bool document = !is_directory && is_text_document(name);
    const bool folders = mt_core_frontend_browser_folders(
        supervisor.has_active ? supervisor.active.package.core_id:0U,browser);
    if (!mt_media_browser_visible_component(name)) return 0;
    const char *extension = strrchr(name, '.');
    const bool config = extension && strlen(extension) == 4 &&
                        (extension[1] == 'c' || extension[1] == 'C') &&
                        (extension[2] == 'f' || extension[2] == 'F') &&
                        (extension[3] == 'g' || extension[3] == 'G');
    accepted =
        is_directory ? folders
        : browser == MT_MENU_BROWSER_CONFIG
            ? config
            : document ||
                  (supervisor.has_active && browser != MT_MENU_BROWSER_DOCUMENTS &&
                   mt_core_frontend_accepts_drive_content(supervisor.active.package.core_id,
                                                          browser, iigs_browser_drive, type));
    if (!accepted) return 0;
    /* The shared IIgs block-disk class also includes HDV and 2MG. Only
     * 140 KiB PO images in that class belong in the 5.25-inch browser. */
    if (!is_directory && browser==MT_MENU_BROWSER_DRIVE8 && supervisor.has_active &&
        supervisor.active.package.core_id==MT_IIGS_CORE_ID && iigs_browser_drive==3U &&
        type==MT_CONTENT_IIGS_HDD &&
        (size!=143360U || extension==NULL ||
         (extension[1]!='p' && extension[1]!='P') || (extension[2]!='o' && extension[2]!='O') ||
         extension[3]!='\0')) return 0;
    name_size = strlen(name);
    if (name_size == 0U || name_size >= MT_MENU_FILE_NAME_MAX) return 0;
    /* Documents cannot crowd usable media or folders out of a full list. */
    if (supervisor_menu_file_count >= MT_MENU_MAX_FILES) {
        size_t index;
        if (document) return 0;
        for (index = supervisor_menu_file_count; index > 0U; --index)
            if ((supervisor_menu_files[index - 1U].cookie & MT_MENU_FILE_TEXT) != 0U) break;
        if (index == 0U) return 0;
        --supervisor_menu_file_count;
        supervisor_menu_files[index - 1U] = supervisor_menu_files[supervisor_menu_file_count];
    }
    memcpy(supervisor_menu_files[supervisor_menu_file_count].name,
           name, name_size + 1U);
    supervisor_menu_files[supervisor_menu_file_count].size = size;
    supervisor_menu_files[supervisor_menu_file_count].cookie =
        is_directory ? MT_MENU_FILE_DIRECTORY : document ? MT_MENU_FILE_TEXT : 0U;
    ++supervisor_menu_file_count;
    return 0;
}

typedef struct {
    bool visited;
    size_t first; /* Program index loaded by LOAD"*", or SIZE_MAX. */
} d64_prg_collection_t;

static bool collect_d64_prg(void *context,
                            const mt_c64_d64_prg_entry_t *entry)
{
    mt_c64_d64_result_t result;
    mt_c64_content_image_t image;
    size_t extracted_size = 0U;
    size_t name_size;
    const size_t index = supervisor_d64_prg_count;
    d64_prg_collection_t *const collection = context;
    bool first;

    if (entry == NULL || index >= MT_MENU_MAX_FILES) return false;
    /* Entries arrive in directory order. After the launch reset, LOAD"*"
     * takes the first PRG-type entry even when it cannot be listed here. */
    first = !collection->visited;
    collection->visited = true;
    /* Unclosed (splat) files are visible in a raw directory but are not safe
     * Mount & Run targets. Extraction also validates the complete chain. */
    if ((entry->file_type & UINT8_C(0x80)) == 0U) return true;
    result = c64_d64_service_extract_prg(
        entry, supervisor_disk_prg_buffer,
        sizeof(supervisor_disk_prg_buffer), &extracted_size);
    image.data = supervisor_disk_prg_buffer;
    image.size = extracted_size;
    if (result != MT_C64_D64_OK ||
        mt_c64_validate_prg(&image).status != MT_C64_CONTENT_OK) {
        return true;
    }

    supervisor_d64_prgs[index] = *entry;
    name_size = strlen(entry->display_name);
    if (name_size == 0U) {
        memcpy(supervisor_menu_files[index].name, "<unnamed>", 10U);
    } else {
        memcpy(supervisor_menu_files[index].name, entry->display_name,
               name_size + 1U);
    }
    if (!mt_c64_prg_has_basic_program(&image))
        strcat(supervisor_menu_files[index].name, " [load only]");
    supervisor_menu_files[index].size = extracted_size;
    supervisor_menu_files[index].cookie = (uint32_t)index;
    if (first) collection->first = index;
    ++supervisor_d64_prg_count;
    supervisor_menu_file_count = supervisor_d64_prg_count;
    return supervisor_d64_prg_count < MT_MENU_MAX_FILES;
}

static void sort_menu_files(void)
{
    size_t index;
    for (index = 1U; index < supervisor_menu_file_count; ++index) {
        mt_menu_file_t value = supervisor_menu_files[index];
        size_t position = index;
        while (position != 0U) {
            const mt_menu_file_t *const previous = &supervisor_menu_files[position - 1U];
            const unsigned value_group = (value.cookie & MT_MENU_FILE_PARENT) != 0U ? 0U :
                ((value.cookie & MT_MENU_FILE_DIRECTORY) != 0U ? 1U : 2U);
            const unsigned previous_group = (previous->cookie & MT_MENU_FILE_PARENT) != 0U ? 0U :
                ((previous->cookie & MT_MENU_FILE_DIRECTORY) != 0U ? 1U : 2U);
            if (previous_group < value_group ||
                (previous_group == value_group && strcmp(previous->name, value.name) <= 0))
                break;
            supervisor_menu_files[position] =
                supervisor_menu_files[position - 1U];
            --position;
        }
        supervisor_menu_files[position] = value;
    }
}

/* Put the LOAD"*" row above the sorted programs. It reuses the target's
 * label and cookie, so Enter, Right and Left keep their usual meaning. */
static void add_first_program_row(size_t program)
{
    static const char prefix[] = "* First program: ";
    size_t row = 0U;
    size_t name_size;

    if (program == SIZE_MAX ||
        supervisor_menu_file_count >= MT_MENU_MAX_FILES) return;
    while (row < supervisor_menu_file_count &&
           supervisor_menu_files[row].cookie != (uint32_t)program) ++row;
    if (row == supervisor_menu_file_count) return;
    name_size = strlen(supervisor_menu_files[row].name);
    if (sizeof(prefix) - 1U + name_size >= MT_MENU_FILE_NAME_MAX) return;
    memmove(&supervisor_menu_files[1], &supervisor_menu_files[0],
            supervisor_menu_file_count * sizeof(supervisor_menu_files[0]));
    ++row;
    memcpy(supervisor_menu_files[0].name, prefix, sizeof(prefix) - 1U);
    memcpy(supervisor_menu_files[0].name + sizeof(prefix) - 1U,
           supervisor_menu_files[row].name, name_size + 1U);
    supervisor_menu_files[0].size = supervisor_menu_files[row].size;
    supervisor_menu_files[0].cookie =
        MT_MENU_FILE_FIRST_PROGRAM | (uint32_t)program;
    ++supervisor_menu_file_count;
}

static void list_vectrex_browser(mt_menu_browser_t browser,
                                 const mt_media_browser_t *directory)
{
    char path[512];
    mt_sd_result_t listed;
    if (!mt_media_browser_path(directory, path, sizeof(path))) {
        set_supervisor_error("Media folder path is too long");
        return;
    }
    supervisor_menu_file_count = 0U;
    supervisor_d64_prg_count = 0U;
    if (mt_media_browser_can_up(directory)) {
        memcpy(supervisor_menu_files[0].name, "..", 3U);
        supervisor_menu_files[0].size = 0U;
        supervisor_menu_files[0].cookie = MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_PARENT;
        supervisor_menu_file_count = 1U;
    }
    listed = mt_sd_storage_list(path, collect_menu_file, &browser);
    if (listed.status != MT_SD_OK) {
        /* A partial list must never be interpreted relative to the old path. */
        supervisor_menu_file_count = 0U;
        mt_menu_close_browser(&supervisor_menu);
        set_supervisor_error("Could not open the media folder");
        return;
    }
    vectrex_media_browser = *directory;
    memcpy(vectrex_browser_path, path, strlen(path) + 1U);
    sort_menu_files();
    mt_menu_open_browser(&supervisor_menu, browser);
    set_supervisor_status(supervisor_menu_file_count == 0U ?
        "No matching files or folders" : "Enter opens; Backspace goes up; Esc closes");
}

static void open_menu_browser(mt_menu_browser_t browser)
{
    mt_sd_result_t list_result;
    if (browser == MT_MENU_BROWSER_DOCUMENTS) {
        mt_media_browser_t directory;
        if (mt_media_browser_init(&directory, "0:/multitini"))
            list_vectrex_browser(browser, &directory);
        return;
    }
    if (!supervisor.has_active || !mt_core_frontend_browser_supported(
            supervisor.active.package.core_id, browser)) {
        set_supervisor_status("This core has no media browser yet");
        return;
    }
    if (mt_core_frontend_browser_folders(supervisor.active.package.core_id,browser)) {
        mt_media_browser_t directory;
        if (!mt_media_browser_init(&directory, supervisor.active.package.content_root)) {
            set_supervisor_error("Invalid core media folder");
            return;
        }
        list_vectrex_browser(browser, &directory);
        return;
    }
    supervisor_menu_file_count = 0U;
    supervisor_d64_prg_count = 0U;
    if (browser == MT_MENU_BROWSER_DISK_PROGRAM) {
        size_t directory_count = 0U;
        d64_prg_collection_t collection = {false, SIZE_MAX};
        const mt_c64_d64_result_t result = c64_d64_service_list_prgs(
            collect_d64_prg, &collection, &directory_count);
        if (result != MT_C64_D64_OK) {
            xil_printf("[C64] D64 directory: %s\r\n",
                       mt_c64_d64_result_name(result));
            set_supervisor_error("Could not read the D64 directory");
            return;
        }
        sort_menu_files();
        add_first_program_row(collection.first);
        mt_menu_open_browser(&supervisor_menu, browser);
        set_supervisor_status(supervisor_menu_file_count == 0U ?
                              "No loadable PRGs in this D64" :
                              "Enter launches via drive; Right loads; Left loads directly");
        return;
    }
    list_result = mt_sd_storage_list(
        supervisor.active.package.content_root, collect_menu_file, &browser);
    if (list_result.status != MT_SD_OK) {
        set_supervisor_error("Could not list the core media folder");
        return;
    }
    sort_menu_files();
    mt_menu_open_browser(&supervisor_menu, browser);
    set_supervisor_status(supervisor_menu_file_count == 0U ?
                          "No matching files" : "Choose a file");
}

static int quiesce_c64_for_reload(void)
{
    /* Only a requested PL replacement may abandon an ambiguous UCI token.
     * Flush drive8 first, then stop the guest and drain every HP master.
     * Any later failure leaves this core in reset until a reload succeeds. */
    if (!shell_is_c64() || usb_sd_modal || !c64_uci_service_reload_required()) return -1;
    if (c64_services_initialized && c64_d64_service_prepare_reset() != 0) return -1;
    if (command(MT_COMMAND_CORE_RESET, 1U, 0U) != 0) return -1;
    usb_hid_service_set_core_ready(false);
    mt_video_effect_service_suspend();
    const uint64_t start = monotonic_ms(NULL);
    for (;;) {
        const int result = c64_uci_service_prepare_reload();
        if (result < 0) return -1;
        /* UCI cleanup releases any SoftIEC channel lease before drive9 can
         * quiesce. Both file owners must be closed before PCAP may start. */
        if (result == 0 && mt_video_effect_service_quiescent())
            return quiesce_c64_directory();
        usb_hid_service_poll_deferred();
        if (monotonic_ms(NULL) - start >= 1000U) return -1;
    }
}

static int supervisor_quiesce(void *context)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    (void)context;
    if (!mt_screenshot_service_cancel()) return -1;
    mt_c64_debug_deactivate(&c64_debug);
    c64_debug_pause_active = false;
    if (shell_is_c64() && c64_uci_service_reload_required()) {
        if (quiesce_c64_for_reload() != 0) return -1;
    } else if (set_core_reset_request(true) != 0) return -1;
    if(!usb_sd_modal && shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID &&
        amiga_service_initialized && amiga_content_service_shutdown()!=0)return -1;
    /* The guest is held, so the EasyFlash journal cannot grow during this save. */
    if (shell_is_c64() && c64_content_service_flush() != 0) return -1;
    if (c64_uci_initialized && !c64_uci_service_reload_required())
        c64_uci_service_reset();
    /* USB export can still be refused by the DDR drain or SD ownership gate.
     * Keep the live session until acquire succeeds so cancellation can resume
     * its title overlay and effective network configuration unchanged. */
    if (!usb_sd_modal) {
        mt_net_service_shutdown();
        c64_title_service_clear();
        c64_title_initialized = false;
        c64_uci_initialized = false;
    }
    set_audio(true, (control & MT_CONTROL_AUDIO_TEST_ENABLE) != 0U);
    usb_hid_service_set_core_ready(false);
    return 0;
}

static int supervisor_load(void *context,
                           const mt_core_selection_t *selection)
{
    mt_pl_load_result_t load;
    uint64_t drain_started;
    const bool ddr_source = core_requires_ddr_drain();
    mt_video_effect_service_suspend();
    drain_started = monotonic_ms(NULL);
    while (!mt_video_effect_service_quiescent() ||
           (ddr_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
                               MT_VIDEO_FRAME_STATUS_QUIESCENT) == 0U)) {
        /* All active HP masters must drain before PCAP resets the fabric.
         * With core reset asserted this includes source transports, the
         * IIgs memory bridge and capable C64 REU DDR caches/bridges.
         * CPU1 only touches reserved DDR and can finish an old job safely. */
        usb_hid_service_poll_deferred();
        if (monotonic_ms(NULL) - drain_started >= 1000U) {
            if (supervisor.has_active) configure_selection_effects(&supervisor.active);
            xil_printf("[video] DDR transport did not drain; core switch cancelled\r\n");
            return -1;
        }
    }
    load = mt_pl_load_selection(
        selection->directory,
        &selection->package.variants[selection->variant_index]);
    (void)context;
    /* Early SD/CRC failures leave the current overlay, including any pending
     * commit, intact. Once fabric reset was reached, both bank caches are stale.
     * Rollback reloads use this same path. */
    if (load.status == MT_PL_LOAD_OK ||
        load.status >= MT_PL_LOAD_FABRIC_INIT_FAILED) {
        mt_ui_presenter_invalidate(&supervisor_presenter);
        supervisor_menu_dirty = true;
    } else if (supervisor.has_active) {
        configure_selection_effects(&supervisor.active);
    }
    xil_printf("[supervisor] PCAP %s bytes=%u crc32=%08x backend=%u\r\n",
               mt_pl_load_status_name(load.status), (unsigned int)load.size,
               (unsigned int)load.actual_crc32, load.backend_status);
    return load.status == MT_PL_LOAD_OK ? 0 : -(int)load.status - 1;
}

static int supervisor_probe(void *context, uint32_t core_id,
                            uint16_t abi_major, uint16_t abi_minor)
{
    uint32_t poll;
    (void)context;
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);
    for (poll = 0U; poll < MT_CORE_READY_POLLS; ++poll) {
        const uint32_t version = shell_read(MT_REG_ABI_VERSION);
        if (shell_read(MT_REG_MAGIC) == MT_MAGIC &&
            shell_read(MT_REG_SHELL_ID) == MT_SHELL_ID &&
            shell_read(MT_REG_CORE_ID) == core_id &&
            (uint16_t)(version >> 16U) == abi_major &&
            (uint16_t)version >= abi_minor &&
            (uint16_t)version >= MT_UI_MIN_ABI_MINOR &&
            (shell_read(MT_REG_UI_STATUS) & MT_UI_STATUS_DENSE_LAYOUT) != 0U) {
            return 0;
        }
    }
    return -1;
}

static int vectrex_basic_service(void *context)
{
    (void)context;
    service_usb_input();
    mt_video_effect_service_poll();
    service_screenshots();
    return vectrex_basic_stop ? 0 : 1;
}

static int vectrex_basic_stream_service(void *context)
{
    (void)context;
    usb_hid_service_poll_deferred();
    return vectrex_basic_stop ? 0 : 1;
}

static void vectrex_basic_controls(void *context, int controls[2][6])
{
    (void)context;
    usb_hid_service_vectrex_controls(controls);
}

static void vectrex_basic_text(void *context, const char *text)
{
    (void)context;
    xil_printf("%s", text);
}

static int vectrex_basic_console_read(void *context)
{
    uint8_t byte;
    const int result = uart0_try_read_byte(&byte);
    (void)context;
    return result > 0 ? byte : (result == 0 ? -1 : -3);
}

static int vectrex_basic_console_write(void *context, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    size_t offset;
    (void)context;
    for (offset = 0U; offset < size; ++offset) {
        if ((offset & 63U) == 0U && !vectrex_basic_service(NULL)) return -2;
        if (uart0_write_byte(NULL, bytes[offset]) != 0) return -1;
    }
    return 0;
}

static int run_vectrex_basic(void *context, const char *root,
                             const char *name, const char *source)
{
    const mt_vectrex32_platform_ops_t ops = {
        NULL, vectrex_basic_service, monotonic_ms,
        vectrex_basic_controls, vectrex_basic_text, vectrex_basic_stream_service,
        vectrex_basic_console_read, vectrex_basic_console_write
    };
    int status;
    (void)context;
    if (vectrex_basic_running) return -1;
    if (!hide_menu_for_core_action()) return -1;
    usb_hid_service_set_core_ready(false);
    vectrex32_service_init(&shell_link, &ops);
    vectrex_basic_stop = false;
    vectrex_basic_running = true;
    set_video_test(false);
    if (dac_ready) set_audio(false, false);
    status = vectrex32_service_run(NULL, root, name, source);
    vectrex_basic_running = false;
    supervisor_menu.visible = true;
    supervisor_menu_dirty = true;
    if (dac_ready) set_audio(true, false);
    return status;
}

static void genesis_transfer_progress(void *context)
{
    (void)context;
    /* The stream or save command owns the mailbox. Preserve USB releases
     * while forbidding nested input commands or FatFs operations. */
    usb_hid_service_poll_deferred();
}

static void iigs_transfer_progress(void *context)
{
    (void)context;
    /* The normal runtime loop already services USB between disk requests.
     * Poll inside a transfer only when no full USB poll has completed in
     * the last millisecond. Long transfers still collect input safely. */
    usb_hid_service_poll_deferred_if_due(1000U);
}

static uint32_t iigs_clock_timestamp(void)
{
    if(!board_clock_valid || board_clock.year<1970U)return 0U;
    uint32_t days=0U;
    static const uint8_t month_days[]={31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    for(uint32_t y=1970U;y<board_clock.year;++y)days+=365U+((y%4U==0U && (y%100U!=0U || y%400U==0U)) ? 1U:0U);
    for(unsigned m=1U;m<board_clock.month;++m)days+=month_days[m-1U]+((m==2U && board_clock.year%4U==0U &&
        (board_clock.year%100U!=0U || board_clock.year%400U==0U)) ? 1U:0U);
    days+=(uint32_t)board_clock.day-1U;
    return days*86400U+(uint32_t)board_clock.hour*3600U+(uint32_t)board_clock.minute*60U+board_clock.second;
}

static int iigs_boot_with_input(void)
{
    usb_hid_service_set_core_ready(false);
    const int result=iigs_content_service_boot();
    const bool loaded=result==0;
    (void)usb_hid_service_set_iigs_mouse_sensitivity(iigs_content_service_mouse_sensitivity());
    usb_hid_service_set_core_ready(loaded);
    if(dac_ready)set_audio(!loaded,false);
    set_video_test(!loaded);
    return result;
}

static const char *spectrum_boot_with_input(const mt_core_selection_t *selection)
{
    usb_hid_service_set_core_ready(false);
    spectrum_boot_error = spectrum_content_boot(&shell_link, selection->directory,
                                                iigs_transfer_progress);
    const bool ready = (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U;
    usb_hid_service_set_core_ready(ready);
    if (dac_ready) set_audio(!ready, false);
    set_video_test(!ready);
    return spectrum_boot_error;
}

static int supervisor_activate(void *context,
                               const mt_core_selection_t *selection)
{
    const mt_core_settings_profile_t *profile;
    bool request_fast_roms;
    (void)context;
    usb_hid_service_set_active_core(selection->package.core_id);
    if (selection->package.core_id == MT_SPECTRUM_CORE_ID) {
        spectrum_content_reset_selection();
        const char *const error = spectrum_boot_with_input(selection);
        last_settings_restore_ok = restore_selection_settings(selection);
        if (error != NULL) {
            xil_printf("[Spectrum] %s\r\n", error);
        }
        /* Keep the ROM retry control available when no ROM is installed. */
        return 0;
    }
    if(selection->package.core_id==MT_AMIGA_CORE_ID) {
        const char *const variant=selection->package.variants[selection->variant_index].id;
        const mt_amiga_model_t model=strcmp(variant,"aga-040-pal")==0 ? MT_AMIGA_AGA_040:
            strcmp(variant,"aga-pal")==0 ? MT_AMIGA_AGA:MT_AMIGA_ECS;
        amiga_content_service_init(&shell_link,selection->package.content_root,model,iigs_transfer_progress,NULL);
        amiga_service_initialized=true;amiga_fault_displayed=false;
        amiga_display_service_init(&shell_link,selection->package.content_root,amiga_apply_display,NULL);
        (void)amiga_content_service_set_clock(board_clock_valid ? &board_clock:NULL,(uint32_t)monotonic_ms(NULL));
        const int boot=amiga_content_service_boot();
        usb_hid_service_set_core_ready(boot==0);
        if(dac_ready)set_audio(boot!=0,false);
        set_video_test(boot!=0);
        if(boot!=0)xil_printf("[Amiga] ROM/media activation: %s\r\n",amiga_content_service_error());
        if(amiga_content_service_clock_status()!=MT_LINK_OK)
            xil_printf("[Amiga] RTC seed: %s\r\n",mt_link_result_name(amiga_content_service_clock_status()));
        last_settings_restore_ok=restore_selection_settings(selection);
        return 0;
    }
    if(selection->package.core_id==MT_IIGS_CORE_ID) {
        iigs_content_service_init(&shell_link,selection->package.content_root,iigs_transfer_progress,NULL);
        iigs_service_initialized=true;
        iigs_content_service_set_timestamp(iigs_clock_timestamp());
        const int boot=iigs_boot_with_input();
        if(boot!=0 || iigs_content_service_faulted())
            xil_printf("[IIgs] ROM/media activation: %s\r\n",iigs_content_service_error());
        last_settings_restore_ok=restore_selection_settings(selection);
        /* Keep the ROM selector and retry controls available for user ROMs. */
        return 0;
    }
    if (selection->package.core_id == MT_VECTREX_CORE_ID) {
        mt_vectrex_content_result_t boot;
        vectrex_content_service_init(&shell_link, selection->package.content_root,
            NULL, NULL, run_vectrex_basic, NULL);
        vectrex_service_initialized = true;
        boot = vectrex_content_service_boot_bios();
        usb_hid_service_set_core_ready(boot.status == MT_VECTREX_CONTENT_OK);
        if (dac_ready) set_audio(boot.status != MT_VECTREX_CONTENT_OK, false);
        set_video_test(boot.status != MT_VECTREX_CONTENT_OK);
        if (boot.status != MT_VECTREX_CONTENT_OK)
            xil_printf("[Vectrex] BIOS unavailable: %s (%s); BASIC remains available\r\n",
                vectrex_content_status_name(boot.status), mt_sd_status_name(boot.sd.status));
        last_settings_restore_ok = restore_selection_settings(selection);
        return boot.status == MT_VECTREX_CONTENT_TRANSFER_FAILED ? -1 : 0;
    }
    if (selection->package.core_id == MT_GBC_CORE_ID) {
        gameboy_content_service_init(
            &shell_link, selection->package.content_root,
            selection->package.abi_minor >= 7U, NULL, NULL);
        gameboy_service_initialized = true;
        gameboy_save_dirty_displayed = false;
        gameboy_save_fault_displayed = false;
        c64_core_reset_active = true;
        usb_hid_service_set_core_ready(false);
        last_settings_restore_ok = restore_selection_settings(selection);
        return 0;
    }
    if (selection->package.core_id == MT_GENESIS_CORE_ID) {
        genesis_content_service_init(
            &shell_link, selection->package.content_root,
            selection->package.abi_minor >= 16U,
            shell_read(MT_REG_CORE_VARIANT_INDEX) == 1U, genesis_transfer_progress, NULL);
        genesis_service_initialized = true;
        genesis_save_dirty_displayed = false;
        genesis_save_fault_displayed = false;
        c64_core_reset_active = true;
        usb_hid_service_set_core_ready(false);
        last_settings_restore_ok = restore_selection_settings(selection);
        return 0;
    }
    if (selection->package.core_id == MT_NES_CORE_ID) {
        nes_content_service_init(
            &shell_link, selection->package.content_root,
            true, genesis_transfer_progress, NULL);
        nes_service_initialized = true;
        nes_save_dirty_displayed = false;
        nes_save_fault_displayed = false;
        c64_core_reset_active = true;
        usb_hid_service_set_core_ready(false);
        last_settings_restore_ok = restore_selection_settings(selection);
        return 0;
    }
    if (selection->package.core_id != MT_C64_CORE_ID) {
        xil_printf("[supervisor] no activation service for core=%08x\r\n",
                   (unsigned int)selection->package.core_id);
        return -1;
    }
    c64_core_reset_active = true;
    c64_content_service_init(&shell_link, NULL, NULL, monotonic_ms, NULL);
    /* Activation follows successful quiesce and full PL replacement. The
     * previous disk has been flushed/ejected; restore this selection from its
     * saved settings below instead of carrying a path from another root. */
    c64_d64_service_init(&shell_link, NULL, NULL);
    c64_services_initialized = true;
    if (c64_content_service_set_media_root(selection->package.content_root) != 0 ||
        c64_d64_service_set_media_root(selection->package.content_root) != 0) {
        xil_printf("[C64] invalid or busy media root\r\n");
        return -1;
    }
    if (c64_iec_service_init(&shell_link, selection->package.content_root,
                            c64_iec_path_protected, NULL) != 0) return -1;
    c64_iec_initialized = true;
    c64_iec_fault_displayed = false;
    if (initialize_c64_extensions(selection) != 0)
        return -1;
    profile = mt_supervisor_settings_find(&persistent_settings,
                                          selection->package.id);
    request_fast_roms = selection->package.abi_minor >= 8U &&
                        profile != NULL && profile->c64_fast_roms;
    if (!attempt_c64_sd_boot(request_fast_roms)) return -2;
    if (c64_services_initialized &&
        c64_d64_service_resume_after_reset() != 0) {
        return -3;
    }
    c64_core_reset_active = false;
    if (resume_c64_directory() != 0) return -3;
    usb_hid_service_set_core_ready(
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U);
    if (dac_ready) set_audio(false, false);
    const mt_c64_capabilities_t caps = selection_c64_features(selection);
    c64_debug_restoring = true;
    mt_c64_debug_activate(&c64_debug, &caps, (unsigned)selection->variant_index);
    c64_debug_restoring = false;
    last_settings_restore_ok = restore_selection_settings(selection);
    return 0;
}

static int supervisor_enter_standby(void)
{
    if (set_core_reset_request(true) != 0) return -1;
    set_audio(true, false);
    set_video_test(true);
    usb_hid_service_set_active_core(MT_SUPERVISOR_CORE_ID);
    usb_hid_service_set_core_ready(false);
    c64_drive_ready = false;
    c64_core_reset_active = true;
    gameboy_service_initialized = false;
    iigs_service_initialized = false;
    amiga_service_initialized = false;
    genesis_service_initialized = false;
    nes_service_initialized = false;
    nes_save_dirty_displayed = false;
    nes_save_fault_displayed = false;
    genesis_save_dirty_displayed = false;
    genesis_save_fault_displayed = false;
    vectrex_service_initialized = false;
    gameboy_save_dirty_displayed = false;
    gameboy_save_fault_displayed = false;
    return 0;
}

static int supervisor_recover(void *context)
{
    (void)context;
    /* This path only resumes an intact standby shell. It does not reset PL;
     * preserve any pending UI commit after a load's preflight failure. Actual
     * reconfigurations, including rollback reloads, invalidate in load(). */
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);
    if (mt_core_link_probe(&shell_link) != MT_LINK_OK ||
        (shell_read(MT_REG_UI_STATUS) & MT_UI_STATUS_DENSE_LAYOUT) == 0U ||
        shell_read(MT_REG_CORE_ID) != MT_SUPERVISOR_CORE_ID) {
        return -1;
    }
    return supervisor_enter_standby();
}

static bool key_bitmap_get(const uint8_t *bitmap, uint8_t usage)
{
    return (bitmap[usage >> 3U] & (uint8_t)(1U << (usage & 7U))) != 0U;
}

static void controller_preview_capture(void)
{
    static bool captured;
    bool requested = supervisor_menu.gamepad_edit &&
        (supervisor_menu.gamepad_capture || supervisor_menu.gamepad_test);
    if (captured && !requested) {
        mt_gamepad_state_t raw;
        usb_hid_service_controller_snapshot(supervisor_menu.gamepad_slot, &raw, NULL);
        requested = !mt_controls_neutral(&raw);
    }
    captured = requested;
    usb_hid_service_set_gamepad_preview(captured);
}

static void supervisor_controller_report(void *context, uint8_t slot, const mt_gamepad_state_t *raw)
{
    (void)context;
    (void)raw;
    if (!supervisor_menu_ready || supervisor_menu_action_active || !supervisor_menu.gamepad_edit ||
        slot != supervisor_menu.gamepad_slot || usb_hid_service_controller_menu_chord(slot)) return;
    const mt_menu_view_t view = supervisor_menu_view();
    if (mt_menu_gamepad_update(&supervisor_menu, &view)) supervisor_menu_dirty = true;
    controller_preview_capture();
}

/* While an Amiga ScreenMode request is pending, guest input stays with
 * Workbench so the user can answer its requesters. The request closed the
 * menu before it was published; the menu key does not reopen it until the
 * request finishes. The dispatcher itself never changes menu visibility. */
static bool amiga_display_holds_menu_closed(void)
{
    return !supervisor_menu.visible && amiga_service_initialized &&
        shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_display_service_pending();
}

static void supervisor_controller_action(void *context, unsigned action)
{
    (void)context;
    if(usb_sd_modal)return;
    if (action == MT_CONTROL_MENU) {
        if (vectrex_basic_running) { vectrex_basic_stop = true; return; }
        if (!supervisor_menu_ready || supervisor_menu_action_active) return;
        if (amiga_display_holds_menu_closed()) {
            set_supervisor_status("Changing Workbench resolution; answer any Amiga requester");
            return;
        }
        const mt_menu_view_t view = supervisor_menu_view();
        const mt_menu_action_t selected = mt_menu_key_with_modifiers(
            &supervisor_menu, &view, MT_MENU_HID_F12, true, 0U);
        (void)mt_menu_key_with_modifiers(&supervisor_menu, &view, MT_MENU_HID_F12, false, 0U);
        if (selected.kind != MT_MENU_ACTION_NONE && pending_menu_action.kind == MT_MENU_ACTION_NONE)
            pending_menu_action = selected;
        supervisor_menu_dirty = true;
        usb_hid_service_set_menu_capture(supervisor_menu.visible);
        controller_preview_capture();
        return;
    }
    if (action == MT_CONTROL_SCREENSHOT || action == MT_CONTROL_SCREENSHOT_OUTPUT)
        (void)mt_screenshot_service_request(action == MT_CONTROL_SCREENSHOT_OUTPUT);
    if (action >= MT_CONTROL_TURBO_TOGGLE && action <= MT_CONTROL_CPU_SLOWER &&
        supervisor.has_active && supervisor.active.package.core_id == MT_C64_CORE_ID &&
        pending_menu_action.kind == MT_MENU_ACTION_NONE) {
        pending_menu_action = (mt_menu_action_t){.kind = MT_MENU_ACTION_TOGGLE_CORE_OPTION,
            .file_index = action == MT_CONTROL_TURBO_TOGGLE ? 9U :
                action == MT_CONTROL_CPU_FASTER ? 12U : 13U};
    }
}

static bool supervisor_controller_config(void *context, uint32_t core_id, uint8_t slot,
    const mt_controller_device_t *device, mt_controller_config_t *config)
{
    static const uint32_t cores[] = {MT_C64_CORE_ID, MT_GBC_CORE_ID, MT_GENESIS_CORE_ID, MT_VECTREX_CORE_ID, MT_IIGS_CORE_ID, MT_NES_CORE_ID, MT_AMIGA_CORE_ID};
    (void)context;
    if (slot >= MT_GAMEPAD_SLOTS || device == NULL || config == NULL) return false;
    const mt_controller_settings_profile_t *saved = mt_supervisor_settings_find_controller(
        &persistent_settings, core_id, &device->identity);
    if (saved != NULL) { *config = saved->config; return true; }
    if (core_id != MT_SUPERVISOR_CORE_ID && core_id != MT_SPECTRUM_CORE_ID &&
        (persistent_settings.legacy_gamepad_pending & (1U << slot)) != 0U) {
        /* Seed the legacy core set before consuming the old global slot map.
         * Keep migration bounded to fit nine slots in the existing wire format. */
        const size_t previous_count = persistent_settings.controller_profile_count;
        bool migrated = true;
        for (unsigned i = 0U; i < sizeof(cores) / sizeof(cores[0]); ++i) {
            if (mt_supervisor_settings_find_controller(&persistent_settings, cores[i], &device->identity) != NULL) continue;
            mt_controller_config_t converted;
            mt_controls_migrate_legacy(cores[i], device, &persistent_settings.gamepads[slot], &converted);
            if (!mt_supervisor_settings_remember_controller(&persistent_settings, cores[i], &device->identity, &converted)) {
                migrated = false;
                break;
            }
        }
        if (migrated) persistent_settings.legacy_gamepad_pending &= (uint16_t)~(1U << slot);
        else persistent_settings.controller_profile_count = previous_count;
        saved = mt_supervisor_settings_find_controller(&persistent_settings, core_id, &device->identity);
        if (saved != NULL) { *config = saved->config; return true; }
        mt_controls_migrate_legacy(core_id, device, &persistent_settings.gamepads[slot], config);
        return true;
    }
    mt_controls_default(core_id, device, config);
    return true;
}

static int usb_sd_quiesce(void *context)
{
    (void)context;
    usb_sd_core_stopped=false;
    usb_sd_previously_reset=(shell_read(MT_REG_CONTROL)&MT_CONTROL_CORE_RESET_REQUEST)!=0U;
    if(!mt_screenshot_service_cancel())return -1;
    mt_text_reader_close(&document_reader);
    if(supervisor_quiesce(NULL)!=0)return -1;
    usb_sd_core_stopped=true;
    mt_video_effect_service_suspend();
    const bool source=core_requires_ddr_drain();
    const uint64_t started=monotonic_ms(NULL);
    while(!mt_video_effect_service_quiescent() || !mt_video_worker_quiesce() ||
        (source && (shell_read(MT_REG_VIDEO_FRAME_STATUS)&MT_VIDEO_FRAME_STATUS_QUIESCENT)==0U)) {
        usb_hid_service_poll_deferred();
        if(monotonic_ms(NULL)-started>=1000U)return -1;
    }
    return 0;
}
static void usb_sd_cancel_quiesce(void *context)
{
    (void)context;
    if(usb_sd_core_stopped && !usb_sd_previously_reset)(void)set_core_reset_request(false);
    (void)mt_video_worker_resume();
    configure_selection_effects(supervisor.has_active ? &supervisor.active:NULL);
    usb_hid_service_set_core_ready((shell_read(MT_REG_CONTROL)&MT_CONTROL_CORE_RESET_REQUEST)==0U);
}
static int usb_sd_acquire(void *context)
{
    (void)context;
    if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID && amiga_service_initialized &&
        amiga_content_service_shutdown()!=0)return -1;
    if (mt_sd_storage_external_acquire().status != MT_SD_OK)
        return -1;
    mt_net_service_shutdown();
    c64_title_service_clear();
    c64_title_initialized = false;
    c64_uci_initialized = false;
    return 0;
}
static int usb_sd_connect(void *context)
{
    (void)context;
    if(usb_storage_service_init()!=0)return -1;
    return usb_storage_service_connect()==0 ? 0:-1;
}
static void usb_sd_poll(void *context) { (void)context;usb_storage_service_poll(); }
static bool usb_sd_host_ejected(void *context)
{ (void)context;return usb_storage_service_consume_host_eject_request()!=0U; }
static int usb_sd_disconnect(void *context)
{ (void)context;return usb_storage_service_disconnect(); }
static int usb_sd_release(void *context)
{ (void)context;return mt_sd_storage_external_release().status==MT_SD_OK ? 0:-1; }
static int usb_sd_restart(void *context)
{
    (void)context;
    usb_sd_restart_failed=false;
    mt_core_catalog_sd_scan(&core_catalog);
    const mt_settings_result_t settings=mt_supervisor_settings_load(&persistent_settings);
    if (!mt_supervisor_turbo_controls_load(&persistent_settings))
        xil_printf("[settings] turbo bindings unavailable; left unassigned\r\n");
    settings_faulted=settings.status!=MT_SETTINGS_OK && settings.status!=MT_SETTINGS_MISSING;
    supervisor_menu_file_count=supervisor_d64_prg_count=0U;
    mt_menu_close_browser(&supervisor_menu);
    if(!mt_video_worker_resume())usb_sd_restart_failed=true;
    if(usb_sd_had_active && !usb_sd_restart_failed) {
        const mt_core_selection_t *selected=NULL;
        for(size_t i=0U;i<core_catalog.count && selected==NULL;++i) {
            mt_core_selection_t *candidate=&core_catalog.cores[i];
            if(strcmp(candidate->package.id,usb_sd_previous.package.id)!=0)continue;
            for(size_t v=0U;v<candidate->package.variant_count;++v)
                if(strcmp(candidate->package.variants[v].id,usb_sd_previous.package.variants[usb_sd_previous.variant_index].id)==0) {
                    candidate->variant_index=v;selected=candidate;break;
                }
        }
        if(selected==NULL || selected->package.abi_major!=MT_ABI_MAJOR || selected->package.abi_minor<MT_UI_MIN_ABI_MINOR ||
            supervisor_load(NULL,selected)!=0 || supervisor_probe(NULL,selected->package.core_id,
                selected->package.abi_major,selected->package.abi_minor)!=0)usb_sd_restart_failed=true;
        else {
            supervisor.active=*selected;supervisor.has_active=true;
            c64_d64_service_init(&shell_link,NULL,NULL);
            c64_services_initialized=true;
            if(supervisor_activate(NULL,selected)!=0)usb_sd_restart_failed=true;
        }
    } else if(!usb_sd_had_active) {
        if(supervisor_enter_standby()!=0)usb_sd_restart_failed=true;
        configure_selection_effects(NULL);
    }
    if(usb_sd_restart_failed) {
        (void)command(MT_COMMAND_CORE_RESET,1U,0U);
        usb_hid_service_set_core_ready(false);
        supervisor.has_active=false;supervisor.state=MT_SUPERVISOR_FAILED;
    } else if(usb_sd_had_active)supervisor.state=MT_SUPERVISOR_ACTIVE;
    supervisor_menu_dirty=true;last_system_refresh_ms=0U;
    /* A changed/missing package leaves the machine reset and returns to the
     * menu so the user can replace it or choose another core. */
    return 0;
}
static void mount_sd_over_usb(bool serial_entry)
{
    const mt_usb_sd_ops_t ops={NULL,usb_sd_quiesce,usb_sd_acquire,usb_sd_connect,usb_sd_poll,
        usb_sd_host_ejected,usb_sd_disconnect,usb_sd_release,usb_sd_restart,usb_sd_cancel_quiesce};
    mt_usb_sd_mode_t mode;mt_usb_sd_mode_init(&mode,&ops);
    usb_sd_had_active=supervisor.has_active;
    if(usb_sd_had_active)usb_sd_previous=supervisor.active;
    usb_sd_modal=true;usb_sd_stop_requested=false;usb_sd_restart_failed=false;
    const int entered=mt_usb_sd_mode_enter(&mode);
    if(entered!=0 && mode.state==MT_USB_SD_IDLE) {
        usb_sd_modal=false;set_supervisor_error("USB SD refused; pending media or open file");return;
    }
    supervisor_menu.visible=true;
    usb_hid_service_set_menu_capture(true);
    mt_usb_sd_state_t displayed=MT_USB_SD_IDLE;
    bool ignore_initial_line_end=serial_entry;
    do {
        if(mode.state!=displayed) { show_handoff_progress(mt_usb_sd_mode_status(&mode));displayed=mode.state; }
        usb_hid_service_poll_deferred();
        uint8_t byte;
        if(uart0_try_read_byte(&byte)>0) {
            if(ignore_initial_line_end && (byte=='\r' || byte=='\n')) {
                /* A terminal may send CRLF for :usbsd. The command reader
                 * consumed CR; its already queued LF is not an eject. */
            } else if(byte==27U || byte=='\r' || byte=='\n' || byte=='q')usb_sd_stop_requested=true;
            else if(byte=='u')usb_storage_service_dump_status();
        }
        ignore_initial_line_end=false;
        if(usb_sd_stop_requested)mt_usb_sd_mode_stop(&mode);
        mt_usb_sd_mode_poll(&mode);
        if(mode.state==MT_USB_SD_REMOUNT_FAILED || mode.state==MT_USB_SD_STOP_FAILED)usleep(100000U);
    } while(mode.state!=MT_USB_SD_IDLE);
    usb_sd_modal=false;
    usb_storage_service_dump_status();
    usb_hid_service_set_menu_capture(true);
    memset(supervisor_menu_keys,0,sizeof(supervisor_menu_keys));
    if(usb_storage_service_transfer_interrupted())
        set_supervisor_error("USB SD stopped after transfer error; check copied files");
    else set_supervisor_outcome(!usb_sd_restart_failed,
        "USB SD stopped; active core restarted", "USB SD stopped; choose a core to recover");
}

static void print_amiga_media_status(void)
{
    const mt_amiga_config_t *c=amiga_content_service_config();
    xil_printf("[Amiga] ready=%u fault=%u cpu=%u chip=2MiB fast=%uMiB PAL host=%08x error=%s\r\n",
        amiga_content_service_ready() ? 1U:0U,amiga_content_service_faulted() ? 1U:0U,
        c->cpu,c->fast_mib,(unsigned)shell_read(MT_AMIGA_HOST_PORT),amiga_content_service_error());
    xil_printf("[Amiga IDE] saved_policy=%s expected=%s fast_available=%u; use :amigaide status for native readback\r\n",
        c->ide_compat==MT_AMIGA_IDE_CLASSIC ? "Classic":"Auto",
        amiga_content_service_fast_ide_expected() ? "Fast":"Classic",amiga_content_service_fast_ide_available()?1U:0U);
    for(unsigned d=0U;d<MT_AMIGA_DRIVE_COUNT;++d) {
        uint32_t reads,writes;amiga_content_service_activity(d,&reads,&writes);
        const char *name=amiga_content_service_name(d);
        xil_printf("[Amiga] drive%u: %s reads=%u writes=%u\r\n",d,name!=NULL ? name:"ejected",(unsigned)reads,(unsigned)writes);
    }
}
static void amiga_display_guest_restarted(void)
{
    amiga_display_service_guest_restarted();
    supervisor_menu_dirty=true;
}
static int apply_amiga_config(const mt_amiga_config_t *changed)
{
    usb_hid_service_set_core_ready(false);
    const int result=amiga_content_service_configure(changed);
    if(result!=0)set_supervisor_error(amiga_content_service_error());
    else { amiga_display_guest_restarted();set_supervisor_status("Amiga settings saved"); }
    usb_hid_service_set_core_ready(amiga_content_service_ready());
    if(dac_ready)set_audio(!amiga_content_service_ready(),false);
    set_video_test(!amiga_content_service_ready());return result;
}
static bool amiga_cpu_command(const char *line)
{
    if(strcmp(line,"amigacpu")==0 || strcmp(line,"amigacpu status")==0) {
        const mt_amiga_config_t *c=amiga_content_service_config();
        xil_printf("[Amiga CPU] active=%s cpu14_available=%u saved14_fallback=%u\r\n",
            amiga_content_service_ap68040() ?
                (c->cpu==MT_AMIGA_CPU_AP68040_2X ? "68040 56.75 MHz":"68040 28 MHz"):
                c->cpu==MT_AMIGA_CPU_STOCK ? (amiga_content_service_aga_pal() ? "68EC020 stock":"68000 stock"):
                c->cpu==MT_AMIGA_CPU_68020_14 ? "68020 14 MHz":"68020 28 MHz",
            amiga_content_service_cpu14_available()?1U:0U,amiga_content_service_cpu14_fallback()?1U:0U);
        if(amiga_content_service_cpu14_available() || amiga_content_service_aga_pal()) {
            uint32_t native=0U;
            const mt_link_result_t result=mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_READ_CONFIG,0U,100000U,&native);
            if(result==MT_LINK_OK)xil_printf("[Amiga CPU] native_cpucfg=%02x native_memcfg=%02x aga_pal=%u cpu_2x=%u\r\n",
                (unsigned)(native&31U),(unsigned)((native>>5U)&127U),(unsigned)((native>>13U)&1U),
                (native&MT_AMIGA_NATIVE_AP68040_2X)!=0U ? 1U:0U);
            else xil_printf("[Amiga CPU] native readback failed: %s\r\n",mt_link_result_name(result));
        } else xil_printf("[Amiga CPU] native readback unsupported by this core\r\n");
        return true;
    }
    if(strncmp(line,"amigacpu ",9U)!=0)return false;
    mt_amiga_config_t changed=*amiga_content_service_config();
    if(amiga_content_service_ap68040()) {
        if(strcmp(line+9U,"28")==0)changed.cpu=MT_AMIGA_CPU_68020_TURBO;
        else if(strcmp(line+9U,"56.75")==0 && amiga_content_service_ap68040_2x_available())
            changed.cpu=MT_AMIGA_CPU_AP68040_2X;
        else {
            xil_printf(amiga_content_service_ap68040_2x_available() ?
                "usage: :amigacpu [status|28|56.75]\r\n":"[Amiga CPU] 56.75 MHz requires an updated AP68040 core\r\n");
            return true;
        }
    } else {
        if(strcmp(line+9U,"stock")==0)changed.cpu=MT_AMIGA_CPU_STOCK;
        else if(strcmp(line+9U,"14")==0)changed.cpu=MT_AMIGA_CPU_68020_14;
        else if(strcmp(line+9U,"28")==0)changed.cpu=MT_AMIGA_CPU_68020_TURBO;
        else { xil_printf("usage: :amigacpu [status|stock|14|28]\r\n");return true; }
    }
    if(changed.cpu==MT_AMIGA_CPU_68020_14 && !amiga_content_service_cpu14_available()) {
        set_supervisor_warning("14 MHz requires an updated Amiga core");return true;
    }
    if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() || amiga_display_service_pending()) {
        set_supervisor_warning("CPU change unavailable while storage/display is busy");return true;
    }
    if(!hide_menu_for_core_action())return true;
    supervisor_menu.visible=false;supervisor_menu_dirty=true;usb_hid_service_set_menu_capture(false);
    (void)apply_amiga_config(&changed);print_amiga_media_status();return true;
}
static bool amiga_ide_command(const char *line)
{
    if(strcmp(line,"amigaide")==0 || strcmp(line,"amigaide status")==0) {
        const mt_amiga_config_t *c=amiga_content_service_config();
        const bool capable=amiga_content_service_fast_ide_available();
        xil_printf("[Amiga IDE] saved_policy=%s expected=%s fast_available=%u\r\n",
            c->ide_compat==MT_AMIGA_IDE_CLASSIC ? "Classic":"Auto",
            amiga_content_service_fast_ide_expected() ? "Fast":"Classic",capable?1U:0U);
        if(capable) {
            uint32_t native=0U;
            const mt_link_result_t result=mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_READ_CONFIG,0U,100000U,&native);
            if(result==MT_LINK_OK)xil_printf("[Amiga IDE] native_actual=%s native_cpucfg=%02x native_memcfg=%02x\r\n",
                (native&0x1000U)!=0U ? "Fast":"Classic",(unsigned)(native&31U),(unsigned)((native>>5U)&127U));
            else xil_printf("[Amiga IDE] native readback failed: %s\r\n",mt_link_result_name(result));
        } else xil_printf("[Amiga IDE] native fast readback unsupported by this core; Classic fallback\r\n");
        return true;
    }
    if(strncmp(line,"amigaide ",9U)!=0)return false;
    mt_amiga_config_t changed=*amiga_content_service_config();
    if(strcmp(line+9U,"auto")==0)changed.ide_compat=MT_AMIGA_IDE_AUTO;
    else if(strcmp(line+9U,"classic")==0)changed.ide_compat=MT_AMIGA_IDE_CLASSIC;
    else { xil_printf("usage: :amigaide [status|auto|classic]\r\n");return true; }
    /* An identical full profile is an explicit CPU reapply to configure().
     * An IDE no-op must retain an unavailable saved 14 MHz CPU preference. */
    if(changed.ide_compat==amiga_content_service_config()->ide_compat) {
        set_supervisor_status("IDE policy already selected");return true;
    }
    if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() || amiga_display_service_pending()) {
        set_supervisor_warning("IDE change unavailable while storage/display is busy");return true;
    }
    if(!hide_menu_for_core_action())return true;
    supervisor_menu.visible=false;supervisor_menu_dirty=true;usb_hid_service_set_menu_capture(false);
    (void)apply_amiga_config(&changed);print_amiga_media_status();return true;
}
static uint64_t amiga_bench_ticks(void *context)
{ XTime now;(void)context;XTime_GetTime(&now);return (uint64_t)now; }
static const char *amiga_profile_decimal(char text[21],uint64_t value)
{
    char *at=text+20;*at='\0';
    do { *--at=(char)('0'+value%10U);value/=10U; } while(value!=0U);
    return at;
}
static uint64_t amiga_profile_microseconds(uint64_t ticks)
{
    const uint64_t frequency=(uint64_t)(COUNTS_PER_SECOND);
    /* Quotient/remainder avoids ticks*1000000 overflow on long captures.
     * Parentheses also preserve the BSP's CPU-clock/2 macro expression. */
    return (ticks/frequency)*UINT64_C(1000000)+
        ((ticks%frequency)*UINT64_C(1000000))/frequency;
}
static void amiga_profile_metric(const char *name,uint64_t ticks,uint64_t count,
                                 uint64_t bytes,uint64_t maximum)
{
    char a[21],b[21],c[21],d[21],e[21];
    xil_printf("[Amiga IDE profile] %s ticks=%s us=%s count=%s bytes=%s max_us=%s\r\n",
        name,amiga_profile_decimal(a,ticks),amiga_profile_decimal(b,amiga_profile_microseconds(ticks)),
        amiga_profile_decimal(c,count),amiga_profile_decimal(d,bytes),
        amiga_profile_decimal(e,amiga_profile_microseconds(maximum)));
}
static uint64_t amiga_profile_native_microseconds(uint64_t cycles,uint32_t hz)
{
    if(hz==0U)return 0U;
    const uint64_t whole=cycles/hz,part=((cycles%hz)*UINT64_C(1000000))/hz;
    if(whole>(UINT64_MAX-part)/UINT64_C(1000000))return UINT64_MAX;
    return whole*UINT64_C(1000000)+part;
}
static void amiga_profile_native_report(const mt_amiga_ide_profile_t *p)
{
    if(!p->native_available) {
        xil_printf("[Amiga IDE native] unavailable on this core; ARM-only capture\r\n");return;
    }
    const mt_amiga_diag_snapshot_t *n=&p->native;
    xil_printf("[Amiga IDE native] valid=%u transport_result=%u sequence=%u status=%04x epoch=%u hz=%u config_begin=%04x config_end=%04x\r\n",
        p->native_valid ? 1U:0U,(unsigned)p->native_result,(unsigned)n->sequence,(unsigned)n->status,
        (unsigned)n->epoch,(unsigned)n->service_hz,(unsigned)n->config_begin,(unsigned)n->config_end);
    if(p->native_result!=MT_LINK_OK) {
        xil_printf("[Amiga IDE native] snapshot unavailable; restart Amiga before another capture\r\n");return;
    }
    static const char *const names[MT_AMIGA_DIAG_COUNTERS]={
        "capture_service","capture_native_accepted","capture_native_held","read_DRQ_publications",
        "closed_read_intervals","closed_gap","gap_held","gap_cpu_write_req_wait","gap_cpu_read_req_wait",
        "gap_IDE_active","gap_other","gap_PIO_read_words","gap_PIO_write_words","gap_cpu_read_tx",
        "gap_cpu_write_tx","gap_cpu_DDR_send","gap_cpu_DDR_wait","gap_chip_DDR_send","gap_chip_DDR_wait",
        "gap_cpu_read_req_wait_wall","gap_cpu_write_req_wait_wall","max_closed_gap",
        "first_DRQ_capture_cycle_stamp","last_REQ_capture_cycle_stamp","REQ4_events","REQ5_events",
        "gap_cpu_DDR_read_tx","gap_cpu_DDR_write_tx"};
    for(unsigned i=0U;i<MT_AMIGA_DIAG_COUNTERS;++i) {
        char value[21],us[21];
        const bool cycles=i<=2U || (i>=5U && i<=10U) || (i>=15U && i<=23U);
        if(cycles)xil_printf("[Amiga IDE native] %s cycles=%s us=%s\r\n",names[i],
            amiga_profile_decimal(value,n->counters[i]),
            amiga_profile_decimal(us,amiga_profile_native_microseconds(n->counters[i],n->service_hz)));
        else xil_printf("[Amiga IDE native] %s count=%s\r\n",names[i],amiga_profile_decimal(value,n->counters[i]));
    }
    xil_printf("[Amiga IDE native] exclusive CLOSED-gap buckets: held + accepted write wait + accepted read wait + IDE active + other; detail counters overlap\r\n");
    xil_printf("[Amiga IDE native] native and ARM intervals have different publication/observation endpoints; do not exactly subtract them or raw cross-domain timestamps\r\n");
}
static bool amiga_ide_profile_command(const char *line)
{
    if(strncmp(line,"amigaideprof",12U)!=0 || (line[12]!='\0' && line[12]!=' '))return false;
    const bool start=strcmp(line,"amigaideprof start")==0;
    const bool status=strcmp(line,"amigaideprof")==0 || strcmp(line,"amigaideprof status")==0;
    const bool stop=strcmp(line,"amigaideprof stop")==0;
    mt_amiga_ide_profile_t p;amiga_content_service_profile_snapshot(&p);
    /* Freeze before any console output, including error/usage text. UART
     * reporting must not appear as a measured outside-service delay. */
    amiga_content_service_profile_stop();
    if(!start && !status && !stop) {
        xil_printf("usage: :amigaideprof start|status|stop (status freezes a running capture)\r\n");return true;
    }
    if(start) {
        if(p.running) {
            set_supervisor_warning("IDE profile was already running; stopped before console output");return true;
        }
        if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() ||
           amiga_display_service_pending() || supervisor_menu.visible ||
           mt_ui_status_menu_shown(shell_read(MT_REG_UI_STATUS)) ||
           (shell_read(MT_REG_CONTROL)&MT_CONTROL_CORE_RESET_REQUEST)!=0U ||
           (shell_read(MT_REG_STATUS)&MT_STATUS_CORE_READY)==0U || !amiga_content_service_ready() ||
           amiga_content_service_faulted()) {
            set_supervisor_warning("IDE profile requires a ready Amiga with menu/reset/storage/display idle");return true;
        }
        xil_printf("[Amiga IDE profile] starting after this message; run disk test, then stop/status; no guest reset or media changes\r\n");
        if(amiga_content_service_profile_start(amiga_bench_ticks,NULL)!=0)
            set_supervisor_warning("IDE profile could not start");
        return true;
    }
    amiga_content_service_profile_snapshot(&p);
    xil_printf("[Amiga IDE profile] stopped valid=%u reason=%u; host timer only; byte counts are backend/FIFO bytes\r\n",
        p.valid?1U:0U,(unsigned)p.reason);
    amiga_profile_metric("wall",p.elapsed_ticks,0U,0U,0U);
    xil_printf("[Amiga IDE profile] read-window offsets from capture start; active span excludes idle before first/after last READ, includes gaps inside\r\n");
    amiga_profile_metric("read_window_first",p.first_read_ticks,p.read_window_started?1U:0U,0U,0U);
    amiga_profile_metric("read_window_last",p.last_read_ticks,0U,0U,0U);
    amiga_profile_metric("active_read_window",p.active_read_ticks,p.read_window_bytes/512U,p.read_window_bytes,0U);
    amiga_profile_metric("service_inclusive",p.service_ticks,p.polls,0U,p.service_max_ticks);
    amiga_profile_metric("outside_service",p.outside_ticks,p.polls,0U,p.max_gap_ticks);
    xil_printf("[Amiga IDE profile] exclusive service stages follow; service_inclusive also includes instrumentation overhead\r\n");
    amiga_profile_metric("SD_read",p.sd_read_ticks,p.sd_read_calls,p.read_bytes,p.sd_read_max_ticks);
    amiga_profile_metric("FIFO",p.fifo_ticks,p.fifo_calls,p.fifo_bytes,p.fifo_max_ticks);
    amiga_profile_metric("ATA_other",p.ata_other_ticks,p.polls,0U,p.ata_other_max_ticks);
    amiga_profile_metric("FDD_poll",p.fdd_ticks,p.polls,0U,p.fdd_max_ticks);
    amiga_profile_metric("idle_ATA_polls",0U,p.idle_ata_polls,0U,0U);
    amiga_profile_metric("REQ4",0U,p.req4,0U,0U);
    amiga_profile_metric("REQ5",0U,p.req5,0U,0U);
    amiga_profile_metric("REQ6",0U,p.req6,0U,0U);
    amiga_profile_metric("read_commands",0U,p.read_commands,0U,0U);
    amiga_profile_metric("write_commands",0U,p.write_commands,0U,0U);
    amiga_profile_metric("requested_read_sectors",0U,p.requested_read_sectors,0U,0U);
    amiga_profile_metric("successful_read_sectors",0U,p.read_sectors,0U,0U);
    amiga_profile_metric("sequential_reads",0U,p.sequential_reads,0U,0U);
    amiga_profile_metric("nonsequential_reads",0U,p.nonsequential_reads,0U,0U);
    xil_printf("[Amiga IDE profile] ATA_other includes command/control and any write-journal work; only SD reads are split out\r\n");
    static const char *const bins[]={"1","2","3..4","5..8","9..16","17..32","33..64","65..128","129..256"};
    for(unsigned i=0U;i<9U;++i) {
        char value[21];xil_printf("[Amiga IDE profile] command_sectors=%s count=%s\r\n",
            bins[i],amiga_profile_decimal(value,p.read_command_hist[i]));
    }
    amiga_profile_metric("DRQ_to_REQ_combined_NON_ADDITIVE",p.drq_to_request_ticks,p.drq_intervals,0U,p.max_drq_to_request_ticks);
    xil_printf("[Amiga IDE profile] DRQ gap overlaps other totals: guest work + CDC visibility + ARM polling; not pure guest PIO time\r\n");
    amiga_profile_metric("poll_tail_control_visible_UPPER_BOUND",p.poll_tail_upper_ticks,p.poll_bracketed_intervals,0U,p.max_poll_tail_upper_ticks);
    amiga_profile_metric("poll_unbracketed_intervals",0U,p.poll_unbracketed_intervals,0U,0U);
    xil_printf("[Amiga IDE profile] bracketed polling tail lies in [0,upper]; no-zero intervals are unbracketed, not zero delay; native assertion/CDC excluded; NON_ADDITIVE\r\n");
    amiga_profile_native_report(&p);
    return true;
}
static bool amiga_ide_bench_command(const char *line)
{
    if(strncmp(line,"amigaidebench",13U)!=0)return false;
    unsigned iterations=32U;
    const char *at=line+13U;
    if(*at!='\0') {
        if(*at++!=' ' || *at<'0' || *at>'9')goto usage;
        iterations=0U;unsigned digits=0U;
        while(*at>='0' && *at<='9') {
            if(++digits>3U)goto usage;
            iterations=iterations*10U+(unsigned)(*at++-'0');
        }
        if(*at!='\0' || iterations==0U || iterations>256U)goto usage;
    }
    if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() || amiga_display_service_pending()) {
        set_supervisor_warning("IDE benchmark unavailable while storage/display is busy");return true;
    }
    if((shell_read(MT_REG_STATUS)&MT_STATUS_CORE_READY)==0U || !amiga_content_service_ready()) {
        set_supervisor_warning("IDE benchmark requires a running Amiga core");return true;
    }
    if(!(shell_read(MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_BULK)) {
        set_supervisor_warning("IDE benchmark requires a bulk-capable Amiga core");return true;
    }
    if(!hide_menu_for_core_action())return true;
    supervisor_menu.visible=false;supervisor_menu_dirty=true;usb_hid_service_set_menu_capture(false);
    xil_printf("[Amiga IDE transport] guest restarts after verified completion; current session is lost\r\n");
    usb_hid_service_set_core_ready(false);
    mt_amiga_ide_bench_t result;
    if(amiga_content_service_ide_bench(iterations,amiga_bench_ticks,NULL,&result)!=0) {
        set_supervisor_warning(amiga_content_service_error());
        xil_printf("[Amiga IDE transport] failed: %s\r\n",amiga_content_service_error());return true;
    }
    usb_hid_service_set_core_ready(amiga_content_service_ready());
    amiga_display_guest_restarted();
    const uint64_t counts_per_second=(uint64_t)(COUNTS_PER_SECOND);
    for(unsigned backend=0U;backend<2U;++backend)for(unsigned direction=0U;direction<2U;++direction) {
        const uint64_t ticks=result.ticks[backend][direction];
        const uint32_t elapsed=(uint32_t)(ticks*UINT64_C(1000000)/counts_per_second);
        const uint32_t kib=ticks!=0U ? (uint32_t)((uint64_t)result.bytes_per_leg*counts_per_second/ticks/1024U):0U;
        xil_printf("[Amiga IDE transport] %s %s bytes=%u us=%u KiB/s=%u verified=1 (no HDF I/O timed)\r\n",
            backend==0U ? "legacy":"bulk",direction==0U ? "ARM->IDE":"IDE->ARM",
            (unsigned)result.bytes_per_leg,(unsigned)elapsed,(unsigned)kib);
    }
    xil_printf("[Amiga IDE transport] verified; Amiga restarted with current settings and media\r\n");
    return true;
usage:
    xil_printf("usage: :amigaidebench [1..256] (default32; FIFO transport only; restarts Amiga)\r\n");return true;
}
static bool amiga_serial_command(const char *line)
{
    if(shell_read(MT_REG_CORE_ID)!=MT_AMIGA_CORE_ID || !amiga_service_initialized)return false;
    if(amiga_cpu_command(line))return true;
    if(amiga_ide_command(line))return true;
    if(amiga_ide_profile_command(line))return true;
    if(amiga_ide_bench_command(line))return true;
    if(strcmp(line,"disk")==0 || strcmp(line,"dir")==0) { print_amiga_media_status();return true; }
    if(strcmp(line,"amigadisplay")==0) {
        const mt_amiga_display_info_t *const info=amiga_display_service_info();
        xil_printf("[Amiga display] available=%u modes=%u pending=%u requested=%u applied=%u saved=%u status=%s\r\n",
            amiga_display_service_available()?1U:0U,(unsigned)amiga_display_service_mode_count(),
            amiga_display_service_pending()?1U:0U,(unsigned)amiga_display_service_requested(),
            (unsigned)amiga_display_service_applied(),(unsigned)amiga_display_service_saved(),amiga_display_service_status());
        if(info!=NULL)xil_printf("[Amiga display] width=%u height=%u depth=%u mode=%08x flags=%04x\r\n",
            (unsigned)info->width,(unsigned)info->height,(unsigned)info->depth,(unsigned)info->mode_id,(unsigned)info->flags);
        const uint32_t host_status=shell_read(MT_AMIGA_HOST_PORT);
        if((host_status&MT_AMIGA_HOST_DISPLAY) && !(host_status&(MT_AMIGA_HOST_BUSY|MT_AMIGA_HOST_ERROR))) {
            for(unsigned i=0U;i<18U;++i) {
                uint32_t word=0U;
                const mt_link_result_t result=mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_DISPLAY_READ,i,100000U,&word);
                if(result!=MT_LINK_OK) {
                    if(result==MT_LINK_REMOTE_ERROR && !(shell_read(MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_BUSY))
                        (void)mt_amiga_host_access(&shell_link,MT_AMIGA_HOST_CLEAR_ERROR,0U,100000U,NULL);
                    xil_printf("[Amiga display] mailbox read%u failed=%s\r\n",i,mt_link_result_name(result));break;
                }
                xil_printf("[Amiga display] word%u=%04x\r\n",i,(unsigned)(word&0xffffU));
            }
        }
        return true;
    }
    if(strncmp(line,"amigadisplay ",13U)==0 && line[13]>='0' && line[13]<='3' && line[14]=='\0') {
        (void)amiga_request_display((uint8_t)(line[13]-'0'));return true;
    }
    if(strcmp(line,"diskretry")==0 || strcmp(line,"saveretry")==0 || strcmp(line,"romload")==0) {
        const mt_menu_action_t action={.kind=strcmp(line,"romload")==0 ? MT_MENU_ACTION_AMIGA_RELOAD:MT_MENU_ACTION_AMIGA_RETRY};
        handle_menu_action(action);
        print_amiga_media_status();return true;
    }
    if(legacy_c64_media_command(line)) {
        xil_printf("[Amiga] Use the Amiga Media menu for mount/eject; :romload retries Kickstart\r\n");return true;
    }
    return false;
}
static bool supervisor_serial_command(const char *line)
{
    const bool listing=strcmp(line,"cores")==0;
    const bool selection=strncmp(line,"core ",5U)==0;
    const bool usb=strcmp(line,"usbsd")==0;
    const bool shot=strcmp(line,"shot")==0 || strncmp(line,"shot ",5U)==0;
    if(!listing && !selection && !usb && !shot)return false;
    if(usb_sd_modal || mt_sd_storage_external_active()) {
        xil_printf("[supervisor] USB SD owns storage; finish export first\r\n");return true;
    }
    if(shot) {
        const bool output=strcmp(line,"shot output")==0;
        if(strcmp(line,"shot")!=0 && strcmp(line,"shot source")!=0 && !output) {
            xil_printf("usage: :shot [source|output]\r\n");return true;
        }
        if(mt_screenshot_service_busy()) {
            xil_printf("[screenshot] Capture already busy\r\n");return true;
        }
        if(!mt_screenshot_service_request(output)) {
            xil_printf("[screenshot] Capture unavailable\r\n");return true;
        }
        const char *status=mt_screenshot_service_status();
        if(status!=NULL)xil_printf("[screenshot] %s\r\n",status);
        return true;
    }
    if(usb) {
        xil_printf("[supervisor] USB SD export requested; eject on PC, then q or Escape exits\r\n");
        mount_sd_over_usb(true);return true;
    }
    if(listing) {
        for(size_t i=0U;i<core_catalog.count;++i)for(size_t v=0U;v<core_catalog.cores[i].package.variant_count;++v)
            xil_printf("[core] %s %s\r\n",core_catalog.cores[i].package.id,core_catalog.cores[i].package.variants[v].id);
        return true;
    }
    char id[32],variant[32],extra;
    const int fields=sscanf(line+5U,"%31s %31s %c",id,variant,&extra);
    if(fields<1 || fields>2) { xil_printf("usage: :core <exact-id> [exact-variant]\r\n");return true; }
    for(size_t i=0U;i<core_catalog.count;++i)if(strcmp(core_catalog.cores[i].package.id,id)==0) {
        for(size_t v=0U;v<core_catalog.cores[i].package.variant_count;++v)if(fields==1 || strcmp(core_catalog.cores[i].package.variants[v].id,variant)==0) {
            const mt_menu_action_t action={.kind=MT_MENU_ACTION_SWITCH_CORE,.core_index=i,.variant_index=v};
            handle_menu_action(action);print_status();return true;
        }
        xil_printf("[supervisor] Unknown variant; use :cores\r\n");return true;
    }
    xil_printf("[supervisor] Unknown core ID; use :cores\r\n");return true;
}

static bool supervisor_keyboard_filter(void *context, uint8_t slot,
                                       uint8_t modifiers,
                                       const uint8_t *keys,
                                       size_t key_count)
{
    static const uint8_t menu_keys[] = {
        MT_MENU_HID_ENTER, MT_MENU_HID_ESCAPE, MT_MENU_HID_TAB,
        MT_MENU_HID_BACKSPACE, MT_MENU_HID_DELETE,
        MT_MENU_HID_F12, MT_MENU_HID_RIGHT, MT_MENU_HID_LEFT,
        MT_MENU_HID_DOWN, MT_MENU_HID_UP, MT_MENU_HID_HOME, MT_MENU_HID_END,
        MT_MENU_HID_PAGE_UP, MT_MENU_HID_PAGE_DOWN
    };
    uint8_t next[MT_C64_HID_USAGE_BYTES] = {0};
    const bool was_visible = supervisor_menu.visible;
    size_t index;
    (void)context;
    if (!supervisor_menu_ready || slot >= MT_C64_HID_SLOT_COUNT)
        return false;
    for (index = 0U; index < key_count; ++index) {
        const uint8_t usage = keys == NULL ? 0U : keys[index];
        if (usage > 3U)
            next[usage >> 3U] |= (uint8_t)(1U << (usage & 7U));
    }
    if(usb_sd_modal && !vectrex_basic_running) {
        const uint8_t stop_keys[]={MT_MENU_HID_ENTER,MT_MENU_HID_ESCAPE,MT_MENU_HID_F12};
        for(size_t i=0U;i<sizeof(stop_keys);++i)
            if(key_bitmap_get(next,stop_keys[i]) && !key_bitmap_get(supervisor_menu_keys[slot],stop_keys[i]))usb_sd_stop_requested=true;
        memcpy(supervisor_menu_keys[slot],next,sizeof(next));return true;
    }
    if (key_bitmap_get(next, 0x46U) && !key_bitmap_get(supervisor_menu_keys[slot], 0x46U))
        (void)mt_screenshot_service_request((modifiers & 0x22U) != 0U);
    if (vectrex_basic_running) {
        if (key_bitmap_get(next, MT_MENU_HID_F12) &&
            !key_bitmap_get(supervisor_menu_keys[slot], MT_MENU_HID_F12)) {
            vectrex_basic_stop = true;
        }
        memcpy(supervisor_menu_keys[slot], next, sizeof(next));
        return vectrex_basic_stop;
    }
    if (supervisor_menu_action_active) {
        /* The USB capture path submits an empty keyboard state, releasing
         * held keys without allowing menu input to interleave a launch. */
        memcpy(supervisor_menu_keys[slot], next, sizeof(next));
        return true;
    }
    for (index = 0U; index < sizeof(menu_keys); ++index) {
        const uint8_t usage = menu_keys[index];
        const bool pressed = key_bitmap_get(next, usage);
        if (pressed != key_bitmap_get(supervisor_menu_keys[slot], usage)) {
            if (usage == MT_MENU_HID_F12 && pressed && amiga_display_holds_menu_closed()) {
                set_supervisor_status("Changing Workbench resolution; answer any Amiga requester");
                continue;
            }
            const mt_menu_view_t view = supervisor_menu_view();
            const mt_menu_action_t action = mt_menu_key_with_modifiers(
                &supervisor_menu, &view, usage, pressed, modifiers);
            supervisor_menu_dirty = true;
            if (action.kind != MT_MENU_ACTION_NONE &&
                pending_menu_action.kind == MT_MENU_ACTION_NONE) {
                pending_menu_action = action;
            }
        }
    }
    memcpy(supervisor_menu_keys[slot], next, sizeof(next));
    usb_hid_service_set_menu_capture(supervisor_menu.visible);
    controller_preview_capture();
    return was_visible || supervisor_menu.visible;
}

static bool hide_menu_for_core_action(void)
{
    const mt_ui_bus_t bus = {
        NULL, supervisor_ui_read, supervisor_ui_write
    };
    const bool was_action_active = supervisor_menu_action_active;
    const uint64_t started = monotonic_ms(NULL);
    mt_menu_view_t view;

    if (!supervisor_menu_ready) return true;
    /* The activity badge is a visible legacy frame; only the dense menu
     * pauses the core, so only it must be hidden here. */
    if (!supervisor_menu.visible && !supervisor_presenter.pending &&
        !mt_ui_status_menu_shown(shell_read(MT_REG_UI_STATUS)))
        return true;

    /* Temporarily hide the video overlay without closing the user's menu.
     * Wait for the hidden acknowledgement before work that needs CPU/drive
     * progress, including BASIC startup and typed disk commands. Keep the
     * frame dirty so the outer menu service restores it after the operation,
     * including UART commands and nested reset/content helpers. */
    supervisor_menu_action_active = true;
    supervisor_menu_dirty = true;
    view = supervisor_menu_view();
    mt_menu_render(&supervisor_menu, &view, &supervisor_frame);
    do {
        const mt_ui_present_result_t result = mt_ui_present_cached(
            &supervisor_presenter, &bus, &supervisor_frame, false);
        if (result == MT_UI_PRESENT_OK && !supervisor_presenter.pending &&
            !mt_ui_status_menu_shown(shell_read(MT_REG_UI_STATUS))) {
            supervisor_menu_action_active = was_action_active;
            return true;
        }
        if (result != MT_UI_PRESENT_OK && result != MT_UI_PRESENT_BUSY)
            break;
        /* No menu service here: it could recurse into the pending action.
         * ARM disk/save service and keyboard releases remain available. */
        service_runtime();
        usleep(1000U);
    } while (monotonic_ms(NULL) - started < MT_MENU_HIDE_TIMEOUT_MS);

    supervisor_menu_action_active = was_action_active;
    set_supervisor_error("Menu could not close; action cancelled");
    return false;
}

static bool menu_action_needs_core_progress(mt_menu_action_t action)
{
    switch (action.kind) {
    case MT_MENU_ACTION_SWITCH_CORE:
    case MT_MENU_ACTION_MOUNT_DRIVE8:
    case MT_MENU_ACTION_MOUNT_AND_RUN:
    case MT_MENU_ACTION_LOAD_DISK_PROGRAM:
    case MT_MENU_ACTION_DIRECT_DISK_PROGRAM:
    case MT_MENU_ACTION_EJECT_DRIVE8:
    case MT_MENU_ACTION_EJECT_IIGS_DRIVE:
    case MT_MENU_ACTION_EJECT_AMIGA_DRIVE:
    case MT_MENU_ACTION_AMIGA_RELOAD:
    case MT_MENU_ACTION_SPECTRUM_RELOAD:
    case MT_MENU_ACTION_AMIGA_RETRY:
    case MT_MENU_ACTION_AMIGA_DISPLAY:
    case MT_MENU_ACTION_IIGS_PRAM:
    case MT_MENU_ACTION_IIGS_RELOAD:
    case MT_MENU_ACTION_DETACH_CARTRIDGE:
    case MT_MENU_ACTION_RESET_CORE:
    case MT_MENU_ACTION_REBOOT_GOLDEN:
    case MT_MENU_ACTION_SWITCH_FIRMWARE:
        return true;
    case MT_MENU_ACTION_LOAD_CONTENT:
        /* Artwork uploads do not need the CPU to run. Keep the visible menu
         * pausing the game while the independent overlay engine transfers. */
        return action.file_index >= supervisor_menu_file_count ||
               (supervisor_menu_files[action.file_index].cookie & MT_MENU_FILE_DIRECTORY) != 0U ||
               mt_content_classify(supervisor_menu_files[action.file_index].name) !=
                   MT_CONTENT_VECTREX_OVERLAY ||
               shell_read(MT_REG_CORE_ID) != MT_VECTREX_CORE_ID;
    case MT_MENU_ACTION_TOGGLE_CORE_OPTION:
        /* C64 option zero swaps the ROM pair and restarts the machine.
         * Turbo, input, video and startup settings apply while paused. */
        return supervisor.has_active &&
               (supervisor.active.package.core_id == MT_GENESIS_CORE_ID ||
                supervisor.active.package.core_id == MT_AMIGA_CORE_ID ||
                (supervisor.active.package.core_id == MT_IIGS_CORE_ID &&
                 (action.file_index==1U || action.file_index==2U || action.file_index==10U)) ||
                (supervisor.active.package.core_id == MT_C64_CORE_ID &&
                 action.file_index == 0U));
    default:
        return false;
    }
}

static mt_genesis_content_service_result_t load_genesis_content_with_input(const char *name)
{
    mt_genesis_content_service_result_t result;
    usb_hid_service_set_core_ready(false);
    result = genesis_content_service_load(name);
    usb_hid_service_set_core_ready(
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U &&
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    if (result.snapshot_valid) {
        const mt_genesis_transfer_snapshot_t *const snapshot = &result.snapshot;
        xil_printf("[MD] failure phase=%s link=%s live status=%08x error=%08x control=%08x stream=%08x\r\n",
            genesis_content_service_phase_name(result.phase), mt_link_result_name(result.link),
            (unsigned int)snapshot->status, (unsigned int)snapshot->error,
            (unsigned int)snapshot->control, (unsigned int)snapshot->stream_status);
        xil_printf("[MD] live opcode=%08x response=%08x core=%08x abi=%08x variant=%u build=%08x\r\n",
            (unsigned int)snapshot->command_opcode, (unsigned int)snapshot->response_status,
            (unsigned int)snapshot->core_id, (unsigned int)snapshot->abi,
            (unsigned int)snapshot->core_variant, (unsigned int)snapshot->build_id);
    }
    return result;
}

static void handle_genesis_content_load(const char *name)
{
    const mt_genesis_content_service_result_t result = load_genesis_content_with_input(name);
    xil_printf("[MD] load %s: %s validation=%s phase=%s link=%s save=%u\r\n",
        name, genesis_content_service_status_name(result.status),
        mt_content_validation_name(result.validation),
        genesis_content_service_phase_name(result.phase), mt_link_result_name(result.link),
        (unsigned int)result.save_size);
    if (result.status != MT_GENESIS_CONTENT_SERVICE_OK) {
        if (result.phase != MT_GENESIS_TRANSFER_NONE &&
            (result.status == MT_GENESIS_CONTENT_SERVICE_TRANSFER_FAILED ||
             result.status == MT_GENESIS_CONTENT_SERVICE_SAVE_MOUNT_FAILED)) {
            char failure[sizeof(supervisor_status_text)];
            (void)snprintf(failure, sizeof(failure), "%s: %s",
                genesis_content_service_phase_name(result.phase), mt_link_result_name(result.link));
            set_supervisor_error(failure);
            return;
        }
        set_supervisor_error(result.validation == MT_CONTENT_UNSUPPORTED_VERSION ?
            "Cartridge requires unsupported hardware" :
            genesis_content_service_status_name(result.status));
        return;
    }
    mt_menu_close_browser(&supervisor_menu);
    if (dac_ready) set_audio(false, false);
    set_video_test(false);
    set_supervisor_outcome(remember_active_cartridge(name),
        result.save_existed ? "Genesis loaded; save restored" : "Genesis cartridge loaded",
        "Cartridge loaded; settings write failed");
}

static void handle_genesis_cart_detach(void)
{
    const mt_genesis_content_service_result_t result = genesis_content_service_detach();
    if (result.status != MT_GENESIS_CONTENT_SERVICE_OK) {
        set_supervisor_error(genesis_content_service_status_name(result.status));
        return;
    }
    usb_hid_service_set_core_ready(false);
    if (dac_ready) set_audio(true, false);
    set_video_test(true);
    set_supervisor_outcome(remember_active_cartridge(NULL),
        "Cartridge unloaded", "Cartridge unloaded; settings write failed");
}

static mt_nes_content_service_result_t load_nes_content_with_input(const char *name)
{
    mt_nes_content_service_result_t result;
    usb_hid_service_set_core_ready(false);
    result = nes_content_service_load(name);
    usb_hid_service_set_core_ready(
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U &&
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    if (result.snapshot_valid) {
        const mt_nes_transfer_snapshot_t *const snapshot = &result.snapshot;
        xil_printf("[NES] failure phase=%s link=%s live status=%08x error=%08x control=%08x stream=%08x\r\n",
            nes_content_service_phase_name(result.phase), mt_link_result_name(result.link),
            (unsigned int)snapshot->status, (unsigned int)snapshot->error,
            (unsigned int)snapshot->control, (unsigned int)snapshot->stream_status);
        xil_printf("[NES] live opcode=%08x response=%08x core=%08x abi=%08x variant=%u build=%08x\r\n",
            (unsigned int)snapshot->command_opcode, (unsigned int)snapshot->response_status,
            (unsigned int)snapshot->core_id, (unsigned int)snapshot->abi,
            (unsigned int)snapshot->core_variant, (unsigned int)snapshot->build_id);
    }
    return result;
}

static void handle_nes_content_load(const char *name)
{
    const mt_nes_content_service_result_t result = load_nes_content_with_input(name);
    xil_printf("[NES] load %s: %s validation=%s phase=%s link=%s save=%u\r\n",
        name, nes_content_service_status_name(result.status),
        mt_content_validation_name(result.validation),
        nes_content_service_phase_name(result.phase), mt_link_result_name(result.link),
        (unsigned int)result.save_size);
    if (result.status != MT_NES_CONTENT_SERVICE_OK) {
        if (result.phase != MT_NES_TRANSFER_NONE &&
            (result.status == MT_NES_CONTENT_SERVICE_TRANSFER_FAILED ||
             result.status == MT_NES_CONTENT_SERVICE_SAVE_MOUNT_FAILED)) {
            char failure[sizeof(supervisor_status_text)];
            (void)snprintf(failure, sizeof(failure), "%s: %s",
                nes_content_service_phase_name(result.phase), mt_link_result_name(result.link));
            set_supervisor_error(failure);
            return;
        }
        set_supervisor_error(result.rejection_reason != NULL ? result.rejection_reason :
            nes_content_service_status_name(result.status));
        return;
    }
    mt_menu_close_browser(&supervisor_menu);
    if (dac_ready) set_audio(false, false);
    set_video_test(false);
    set_supervisor_outcome(remember_active_cartridge(name),
        result.save_existed ? "NES loaded; save restored" : "NES cartridge loaded",
        "Cartridge loaded; settings write failed");
}

static void handle_nes_cart_detach(void)
{
    const mt_nes_content_service_result_t result = nes_content_service_detach();
    if (result.status != MT_NES_CONTENT_SERVICE_OK) {
        set_supervisor_error(nes_content_service_status_name(result.status));
        return;
    }
    usb_hid_service_set_core_ready(false);
    if (dac_ready) set_audio(true, false);
    set_video_test(true);
    set_supervisor_outcome(remember_active_cartridge(NULL),
        "Cartridge unloaded", "Cartridge unloaded; settings write failed");
}

static void change_genesis_controller(size_t player)
{
    const mt_genesis_config_t previous = *genesis_content_service_options();
    mt_genesis_config_t changed = previous;
    const char *active_name = genesis_content_service_cartridge_name();
    char name[MT_SETTINGS_MEDIA_NAME_MAX];
    bool loaded = false;
    if (player > 1U || !genesis_service_initialized || !supervisor.has_active) {
        set_supervisor_warning("Controller option unavailable");
        return;
    }
    name[0] = '\0';
    if (active_name != NULL) {
        if (strlen(active_name) >= sizeof(name)) {
            set_supervisor_error("Cartridge name is too long to restart");
            return;
        }
        memcpy(name, active_name, strlen(active_name) + 1U);
    }
    changed.six_button_mask ^= (uint8_t)(1U << player);
    if (genesis_content_service_set_options(&changed) != 0) return;
    if (name[0] != '\0') {
        const mt_genesis_content_service_result_t result = load_genesis_content_with_input(name);
        if (result.status != MT_GENESIS_CONTENT_SERVICE_OK) {
            (void)genesis_content_service_set_options(&previous);
            if (!genesis_content_service_cartridge_attached())
                (void)load_genesis_content_with_input(name);
            set_supervisor_error("Controller change failed; previous setting retained");
            return;
        }
        loaded = true;
    }
    settings_before_update = persistent_settings;
    if (!ensure_active_settings_profile() ||
        !mt_supervisor_settings_remember_genesis_pads(&persistent_settings,
            supervisor.active.package.id, (uint8_t)(3U ^ changed.six_button_mask)) ||
        !save_persistent_settings()) {
        bool restored = true;
        persistent_settings = settings_before_update;
        (void)genesis_content_service_set_options(&previous);
        if (loaded)
            restored = load_genesis_content_with_input(name).status == MT_GENESIS_CONTENT_SERVICE_OK;
        set_supervisor_error(restored ? "Settings write failed; previous controller restored" :
                                       "Settings write and cartridge restore failed");
        return;
    }
    set_supervisor_status("Controller protocol saved");
}

static mt_vectrex_content_result_t load_vectrex_content_with_input(const char *name)
{
    const bool overlay = mt_content_classify(name) == MT_CONTENT_VECTREX_OVERLAY;
    mt_vectrex_content_result_t result;
    if (!overlay) usb_hid_service_set_core_ready(false);
    result = vectrex_content_service_load(name);
    if (!overlay &&
        (result.status == MT_VECTREX_CONTENT_OK ||
         ((result.status == MT_VECTREX_CONTENT_READ_FAILED ||
           result.status == MT_VECTREX_CONTENT_REJECTED ||
           result.status == MT_VECTREX_CONTENT_INVALID_ARGUMENT) &&
          (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U &&
          (shell_read(MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) == 0U))) {
        /* Early file/validation failures preserve the running machine. A
         * transfer or BASIC failure may have changed it, so leave those held. */
        usb_hid_service_set_core_ready(true);
    }
    return result;
}

static int native_load_key(void *context, uint8_t scan, bool pressed)
{
    (void)context;
    return mt_core_link_input_event(&shell_link, scan, pressed ? 1U : 0U,
                                    MT_LINK_COMMAND_POLLS, NULL, NULL) ==
           MT_LINK_OK ? 0 : -1;
}

static int native_load_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    /* Commands have completed before this callback. Disk service may use the
     * same link here; USB/menu input must not interleave the typed command. */
    while (milliseconds-- != 0U) {
        usleep(1000U);
        c64_d64_service_poll();
    }
    return 0;
}

typedef struct {
    const uint8_t *raw_name;
    size_t matches;
} native_name_search_t;

static bool count_native_name(void *context,
                              const mt_c64_d64_prg_entry_t *entry)
{
    native_name_search_t *const search = context;
    if (memcmp(entry->raw_name, search->raw_name,
               MT_C64_D64_NAME_SIZE) == 0) ++search->matches;
    return true;
}

static bool native_load_name_unique(uint32_t cookie)
{
    native_name_search_t search = {
        supervisor_d64_prgs[cookie].raw_name, 0U
    };
    size_t count = 0U;
    /* Include unclosed/invalid PRGs excluded from the launch browser: the
     * drive resolves the name itself and could otherwise choose those. */
    return c64_d64_service_list_prgs(count_native_name, &search, &count) ==
           MT_C64_D64_OK && search.matches == 1U;
}

typedef struct {
    bool seen;
    mt_c64_d64_prg_entry_t entry;
} first_prg_search_t;

static bool find_first_prg(void *context,
                           const mt_c64_d64_prg_entry_t *entry)
{
    first_prg_search_t *const search = context;
    search->seen = true;
    search->entry = *entry;
    return false;
}

/* LOAD"*" goes to the drive's first PRG-type entry, so the disk must not
 * have gained an earlier one since the browser listed it. */
static bool first_program_current(uint32_t cookie)
{
    const mt_c64_d64_prg_entry_t *const target = &supervisor_d64_prgs[cookie];
    first_prg_search_t search = {false, {0}};
    size_t count = 0U;
    return c64_d64_service_list_prgs(find_first_prg, &search, &count) ==
               MT_C64_D64_OK && search.seen &&
           search.entry.directory_sector == target->directory_sector &&
           search.entry.directory_slot == target->directory_slot &&
           search.entry.file_type == target->file_type &&
           search.entry.start_track == target->start_track &&
           search.entry.start_sector == target->start_sector &&
           search.entry.blocks == target->blocks &&
           memcmp(search.entry.raw_name, target->raw_name,
                  MT_C64_D64_NAME_SIZE) == 0;
}

static void launch_disk_program_native(uint32_t cookie, bool first_program)
{
    static const mt_c64_native_load_io_t io = {
        native_load_key, native_load_delay
    };
    const mt_c64_d64_prg_entry_t *const entry = &supervisor_d64_prgs[cookie];
    mt_c64_content_service_result_t result;
    mt_c64_native_load_status_t typed;
    bool remounted;
    bool settings_saved;
    if (!first_program &&
        (!mt_c64_native_load_name_supported(entry->raw_name) ||
         !native_load_name_unique(cookie))) {
        set_supervisor_error("Name cannot be selected exactly by a drive LOAD");
        return;
    }
    if (!prepare_c64_content_change()) {
        set_supervisor_error("Disk load could not start");
        return;
    }
    result = c64_title_disk(NULL, 0);
    print_c64_content_result("native load prepare", entry->display_name,
                             &result);
    remounted = finish_c64_content_change();
    if (result.status != MT_C64_CONTENT_SERVICE_OK) {
        set_supervisor_error("Disk load preparation failed");
        return;
    }
    settings_saved = remember_active_cartridge(NULL);
    if (!remounted) {
        set_supervisor_warning("Retry disk remount, then select the program again");
        return;
    }
    typed = first_program ? mt_c64_native_load_first(&io, NULL) :
                            mt_c64_native_load(entry->raw_name, &io, NULL);
    xil_printf("[C64] native LOAD %s%s: %s\r\n",
               first_program ? "* for " : "", entry->display_name,
               typed == MT_C64_NATIVE_LOAD_OK ? "command sent" :
                                               "keyboard transfer failed");
    if (typed != MT_C64_NATIVE_LOAD_OK) {
        set_supervisor_error("LOAD typing failed; restart before retrying");
        return;
    }
    mt_menu_close_browser(&supervisor_menu);
    set_supervisor_outcome(settings_saved,
        "LOAD sent through drive; program controls startup",
        "LOAD sent; settings write failed");
}

static void document_read_progress(void *context)
{
    (void)context;
    /* FatFs owns the current read. Poll USB without re-entering SD or the
     * shared core command channel while loading a document. */
    usb_hid_service_poll_deferred();
}

static uint32_t c64_debug_read(void *context, uint32_t reg)
{ (void)context; return shell_read(reg); }
static void c64_debug_write(void *context, uint32_t reg, uint32_t value)
{ (void)context; Xil_Out32(MT_SHELL_BASE_ADDRESS + reg, value); }
static void c64_debug_output(void *context, const char *text)
{
    (void)context;
    xil_printf("%s\r\n", text);
    set_supervisor_status(text);
}
static void c64_debug_pause(void *context, bool pause)
{
    (void)context;
    if (pause) {
        if (!c64_debug_pause_active) c64_debug_was_visible = supervisor_menu.visible;
        c64_debug_pause_active = true;
        show_handoff_progress("C64 memory operation; please wait");
    } else if (c64_debug_pause_active && !c64_debug.reu_host.reload_required) {
        c64_debug_pause_active = false;
        supervisor_menu.visible = c64_debug_was_visible;
        if (!c64_debug_was_visible) (void)hide_menu_for_core_action();
        supervisor_menu_dirty = true;
    }
}
static bool c64_debug_paused(void *context)
{
    (void)context;
    /* A future host-only image must expose a qualified pause acknowledgement. */
    return c64_debug.caps.turbo &&
        (shell_read(MT_REG_C64_CPU_STATUS) & MT_C64_CPU_STATUS_PAUSE) != 0U;
}
static void c64_debug_apply(void *context, bool change)
{
    (void)context;
    if (!c64_debug_restoring && supervisor.has_active &&
        supervisor.active.package.core_id == MT_C64_CORE_ID)
        configure_c64_turbo(&supervisor.active, mt_supervisor_settings_find(
            &persistent_settings, supervisor.active.package.id), change);
}
static bool c64_debug_cartridge_blocks_reu(void *context)
{
    (void)context;
    return !c64_content_service_reu_load_allowed();
}
static bool c64_debug_content_busy(void *context)
{
    (void)context;
    return c64_core_reset_active || usb_sd_modal ||
        (shell_read(MT_REG_STATUS) & MT_STATUS_STREAM_ACTIVE) != 0U;
}
static bool c64_debug_reu_enabled(void *context)
{ (void)context; return c64_content_service_reu_enabled(); }
static bool c64_debug_open(void *context, const char *name, size_t *size)
{
    char path[640];
    (void)context;
    if (c64_debug_file != NULL) return false;
    const int length = snprintf(path, sizeof path, "%s%s%s",
        strchr(name, ':') != NULL ? "" : supervisor.active.package.content_root,
        strchr(name, ':') != NULL ? "" : "/", name);
    if (length < 0 || (size_t)length >= sizeof path ||
        mt_sd_file_open(path, "rb", &c64_debug_file).status != MT_SD_OK) return false;
    bool ok = mt_sd_file_seek(c64_debug_file, 0, SEEK_END).status == MT_SD_OK;
    const int64_t bytes = mt_sd_file_tell(c64_debug_file);
    ok = ok && bytes >= 0 && (uint64_t)bytes <= SIZE_MAX &&
        mt_sd_file_seek(c64_debug_file, 0, SEEK_SET).status == MT_SD_OK;
    if (!ok) { (void)mt_sd_file_close(c64_debug_file); c64_debug_file = NULL; return false; }
    *size = (size_t)bytes;
    return true;
}
static bool c64_debug_file_read(void *context, size_t offset, void *data, size_t size)
{
    (void)context;
    if (c64_debug_file == NULL ||
        mt_sd_file_seek(c64_debug_file, (int64_t)offset, SEEK_SET).status != MT_SD_OK) return false;
    const mt_sd_result_t result = mt_sd_file_read(c64_debug_file, data, size);
    return result.status == MT_SD_OK && result.size == size;
}
static void c64_debug_close(void *context)
{
    (void)context;
    if (c64_debug_file != NULL) (void)mt_sd_file_close(c64_debug_file);
    c64_debug_file = NULL;
}
static bool c64_debug_file_write(void *context, const char *path, const void *data, size_t size)
{
    mt_sd_file_t *file = NULL;
    (void)context;
    if (mt_sd_storage_ensure_directory("0:/multitini/capture").status != MT_SD_OK ||
        mt_sd_file_open(path, "wb", &file).status != MT_SD_OK) return false;
    const mt_sd_result_t result = mt_sd_file_write(file, data, size);
    const mt_sd_result_t closed = mt_sd_file_close(file);
    return result.status == MT_SD_OK && result.size == size && closed.status == MT_SD_OK;
}
static bool c64_debug_reu_read(void *context, uint32_t offset, void *data, size_t size)
{
    (void)context;
    if (offset > MT_C64_REU_DDR_SIZE || size > MT_C64_REU_DDR_SIZE - offset) return false;
    memcpy(data, (const void *)(uintptr_t)(MT_C64_REU_DDR_BASE + offset), size);
    return true;
}
static bool c64_debug_reu_write(void *context, uint32_t offset, const void *data, size_t size)
{
    (void)context;
    if (offset > MT_C64_REU_DDR_SIZE || size > MT_C64_REU_DDR_SIZE - offset) return false;
    memcpy((void *)(uintptr_t)(MT_C64_REU_DDR_BASE + offset), data, size);
    return true;
}
static void c64_debug_drain(void *context)
{
    (void)context;
    /* PL310 Cache Sync drains early-BRESP buffers without touching cache lines. */
    DATA_SYNC;
    Xil_Out32(UINT32_C(0xF8F02730), 0U);
    DATA_SYNC;
}
static void *c64_debug_allocate(void *context, size_t size)
{ (void)context; return malloc(size); }
static void c64_debug_release(void *context, void *data)
{ (void)context; free(data); }
static void initialize_c64_debug(void)
{
    const mt_c64_debug_ops_t ops = {
        .read32=c64_debug_read, .write32=c64_debug_write, .milliseconds=monotonic_ms,
        .progress=document_read_progress, .pause=c64_debug_pause, .paused=c64_debug_paused,
        .output=c64_debug_output, .apply_turbo=c64_debug_apply, .cartridge_blocks_reu=c64_debug_cartridge_blocks_reu,
        .content_busy=c64_debug_content_busy, .reu_enabled=c64_debug_reu_enabled,
        .file_open=c64_debug_open, .file_read=c64_debug_file_read, .file_close=c64_debug_close,
        .file_write=c64_debug_file_write, .reu_read=c64_debug_reu_read, .reu_write=c64_debug_reu_write,
        .drain_writes=c64_debug_drain, .allocate=c64_debug_allocate, .release=c64_debug_release
    };
    mt_c64_debug_init(&c64_debug, &ops, false);
}
static bool dispatch_c64_debug(const char *line)
{
    const bool handled = mt_c64_debug_command(&c64_debug, line);
    if (c64_debug_pause_active && !c64_debug.reu_host.reload_required)
        c64_debug_pause(NULL, false);
    if (c64_debug.reu_host.reload_required) {
        supervisor_menu.visible = true;
        set_supervisor_error("REU cache did not clear; reload the C64 core");
    }
    return handled;
}

static bool commit_c64_edit(const mt_c64_title_values_t *value, uint32_t groups,
                            uint64_t sid_fields, uint8_t sid_classes)
{
    const mt_c64_title_values_t previous =
        c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
    mt_c64_title_values_t applied = *value;
    if (c64_content_service_crt_attached() &&
        !(c64_title_caps.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST)) {
        applied.reu = false;
        applied.ua = false;
    }
    const char *conflict =
        mt_c64_title_io_conflict(&applied, &c64_title_caps, c64_content_service_crt_attached());
    if (conflict) {
        set_supervisor_warning(conflict);
        return false;
    }
    const bool transient = (groups == MT_TITLE_SID)
                               ? c64_title_service_sid_owned(sid_fields)
                               : (c64_title_service_overlay()->owned & groups) != 0;
    if (apply_c64_title(NULL, &applied, true, true) != 0) {
        set_supervisor_warning("Settings could not reach a safe apply point");
        return false;
    }
    if (transient) {
        if (groups == MT_TITLE_SID)
            c64_title_service_edit_sid(&value->sid, sid_fields, sid_classes);
        else
            (void)c64_title_service_edit(groups, value);
        set_supervisor_status("Changed for this title; Save makes it permanent");
        supervisor_menu_dirty = true;
        return true;
    }
    const mt_c64_title_values_t saved = c64_stored_values(&supervisor.active);
    mt_c64_title_overlay_t edit = {0};
    edit.values = *value;
    edit.owned = groups;
    edit.sid_fields = sid_fields;
    edit.sid_classes = sid_classes;
    mt_c64_title_values_t next, unused;
    mt_c64_title_effective(&next, &unused, &saved, &edit, false);
    settings_before_update = persistent_settings;
    const mt_c64_sid_model_t old_sid = c64_stored_sid;
    bool valid = ensure_active_settings_profile();
    if (valid && (groups & MT_TITLE_TURBO))
        valid = mt_supervisor_settings_remember_c64_cpu_turbo(
            &persistent_settings, supervisor.active.package.id, next.turbo);
    if (valid && (groups & (MT_TITLE_SID | MT_TITLE_MEMORY))) {
        uint8_t legacy = next.sid_mode;
        if (groups & MT_TITLE_SID) {
            uint8_t record[128];
            valid = mt_c64_sid_encode(&next.sid, supervisor.active.package.id, record);
            if (valid) {
                legacy = record[24];
                valid = mt_supervisor_sid_map_save(supervisor.active.package.id, &next.sid);
            }
            if (valid)
                c64_stored_sid = next.sid;
        }
        if (valid)
            valid = mt_supervisor_settings_remember_c64_expansion(
                &persistent_settings, supervisor.active.package.id, legacy, next.reu);
    }
    if (valid && (groups & MT_TITLE_JOYSTICK))
        valid = mt_supervisor_settings_remember_c64_input(
            &persistent_settings, supervisor.active.package.id, usb_hid_service_symbolic_keyboard(),
            next.joystick);
    persistent_settings.c64_turbo_detail = next.detail;
    persistent_settings.c64_turbo_flags = next.flags;
    persistent_settings.c64_ultimate_audio = next.ua;
    persistent_settings.c64_command_interface = next.uci;
    if (!valid || !save_persistent_settings()) {
        persistent_settings = settings_before_update;
        bool map_restored = true;
        if (groups & MT_TITLE_SID) {
            map_restored = mt_supervisor_sid_map_save(supervisor.active.package.id, &old_sid);
            c64_stored_sid = old_sid;
        }
        mt_c64_title_values_t rollback = previous;
        if (c64_content_service_crt_attached() &&
            !(c64_title_caps.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST)) {
            rollback.reu = false;
            rollback.ua = false;
        }
        const bool restored = apply_c64_title(NULL, &rollback, true, false) == 0;
        set_supervisor_warning(
            !map_restored ? "SID settings recovery failed; retry Save before reloading"
            : restored    ? "Setting was not saved; previous value restored"
                          : "Setting was not saved; hardware restore failed. Reset the core.");
        return false;
    }
    refresh_c64_title(&supervisor.active);
    supervisor_menu_dirty = true;
    set_supervisor_status("Setting saved");
    return true;
}
static void change_c64_expansion(size_t option)
{
    const mt_c64_capabilities_t caps =
        selection_c64_features(supervisor.has_active ? &supervisor.active : NULL);
    if (!supervisor.has_active ||
        (option == 2 && !(caps.expansion & MT_C64_EXPANSION_CAPS_DUAL_SID)) ||
        (option == 3 && !(caps.expansion & MT_C64_EXPANSION_CAPS_REU_16MB))) {
        set_supervisor_warning("Core option unavailable");
        return;
    }
    mt_c64_title_values_t value =
        c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
    uint32_t group = MT_TITLE_MEMORY;
    if (option == 2) {
        unsigned id = mt_c64_sid_preset_id(&value.sid);
        unsigned maximum =
            (caps.expansion & MT_C64_EXPANSION_CAPS_MULTI_SID) ? MT_SID_EIGHT : MT_SID_DUAL500;
        id = id < 1 || id >= maximum ? 1 : id + 1;
        (void)mt_c64_sid_preset(&value.sid, id);
        group = MT_TITLE_SID;
    } else if (option == 3) {
        value.reu = !value.reu;
        if (!value.reu)
            value.ua = false;
    } else if (option == 8 && caps.ultimate_audio) {
        value.ua = !value.ua;
        if (value.ua)
            value.reu = true;
    } else if (option == 14 && (caps.cpu2 & MT_C64_CPU_CAPS2_SCPU_DETECT)) {
        value.flags ^= 8;
        group = MT_TITLE_SCPU;
    } else if (option == 15 && (caps.expansion & MT_C64_EXPANSION_CAPS_UCI)) {
        value.uci = !value.uci;
        group = MT_TITLE_UCI;
    } else
        return;
    if (group == MT_TITLE_MEMORY && value.reu && c64_content_service_crt_attached() &&
        !(caps.expansion & MT_C64_EXPANSION_CAPS_CART_COEXIST)) {
        set_supervisor_warning("Eject the cartridge before enabling expansion memory");
        return;
    }
    if (group == MT_TITLE_MEMORY && caps.ultimate_audio &&
        (shell_read(MT_REG_C64_UA_STATUS) & MT_C64_UA_STATUS_FAULT)) {
        set_supervisor_warning("Reload the C64 core to clear the memory fault");
        return;
    }
    (void)commit_c64_edit(&value, group, UINT64_MAX, 7);
}
static void change_c64_turbo(size_t option)
{
    const mt_c64_capabilities_t caps =
        selection_c64_features(supervisor.has_active ? &supervisor.active : NULL);
    if (!caps.turbo) {
        set_supervisor_warning("Core option unavailable");
        return;
    }
    if (option == 7 && !(caps.cpu & MT_C64_CPU_CAPS_CARTRIDGE_TURBO))
        return;
    if (option == 10 && !(caps.cpu2 & MT_C64_CPU_CAPS2_FOLLOW_CPU))
        return;
    mt_c64_title_values_t value =
        c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
    uint32_t group = MT_TITLE_TURBO;
    switch (option) {
    case 1:
        if (!value.turbo) {
            value.turbo = true;
            value.detail = (value.detail & 0x3f) | 0x40;
        } else if ((value.detail >> 6) == 3)
            value.turbo = false;
        else
            value.detail += 0x40;
        break;
    case 9:
        value.turbo = !value.turbo;
        break;
    case 4:
        value.detail = (uint8_t)((value.detail & 0xf0) |
                                 mt_c64_next_speed(value.detail & 15, (uint16_t)caps.cpu));
        group = MT_TITLE_SPEED;
        break;
    case 5:
        value.detail ^= 0x20;
        group = MT_TITLE_BADLINE;
        break;
    case 6:
        value.flags ^= 1;
        group = MT_TITLE_AUTOSLOW;
        break;
    case 7:
        value.flags ^= 2;
        group = MT_TITLE_CART;
        break;
    case 10:
        value.detail ^= 0x10;
        group = MT_TITLE_FOLLOW;
        break;
    case 12:
    case 13:
        if (option == 13 && !value.turbo) {
            /* A slower action also ends a temporary :speed session when the
             * stored/title mode is Off. Apply that mode without saving a no-op.
             * A failed HOLD leaves both the live configuration and override. */
            if (c64_debug.override_active) {
                mt_c64_debug_clear_override(&c64_debug);
                if (apply_c64_title(NULL, &value, true, true) != 0) {
                    c64_debug.override_active = true;
                    set_supervisor_warning("Settings could not reach a safe apply point");
                    return;
                }
            }
            set_supervisor_status("Turbo is off");
            return;
        }
        if (!value.turbo) {
            value.turbo = true;
            value.detail = (value.detail & 0x3f) | 0x40;
        }
        value.detail =
            (uint8_t)((value.detail & 0xf0) |
                      mt_c64_step_speed(value.detail & 15, (uint16_t)caps.cpu, option == 12));
        group = MT_TITLE_TURBO | MT_TITLE_SPEED;
        break;
    default:
        return;
    }
    const bool had_override = c64_debug.override_active;
    const uint32_t before = shell_read(MT_REG_C64_CPU_CONFIG);
    mt_c64_debug_clear_override(&c64_debug);
    const bool committed = commit_c64_edit(&value, group, 0, 0);
    if (!committed && had_override && shell_read(MT_REG_C64_CPU_CONFIG) == before) {
        /* Nothing reached the hardware (for example, HOLD failed). Keep the
         * session description consistent with the still-running override. */
        c64_debug.override_active = true;
    } else if (committed) {
        const uint32_t live = shell_read(MT_REG_C64_CPU_STATUS);
        const unsigned requested = value.turbo
            ? mt_c64_speed_mhz(mt_c64_clamp_speed(value.detail & 15U, (uint16_t)caps.cpu)) : 1U;
        char text[128];
        (void)snprintf(text, sizeof text, "%u MHz %s; now %u MHz (%s)", requested,
                       (c64_title_service_overlay()->owned & group) ? "for this title" : "saved",
                       mt_c64_speed_mhz(live & 15U), mt_c64_turbo_reason(live));
        set_supervisor_status(text);
    }
}
static unsigned cycle_setting(unsigned value, unsigned maximum, int delta)
{
    return (value + maximum + 1U + (unsigned)(delta < 0 ? -1 : 1)) % (maximum + 1U);
}
static void change_c64_sid_setup(unsigned item, int delta)
{
    const mt_c64_capabilities_t caps = selection_c64_features(&supervisor.active);
    if (!(caps.expansion & MT_C64_EXPANSION_CAPS_MULTI_SID))
        return;
    const unsigned count = ((caps.expansion >> 8) & 7) + 1;
    if (item == 3) {
        c64_selected_sid = (uint8_t)cycle_setting(c64_selected_sid, count - 1, delta);
        supervisor_menu_dirty = true;
        return;
    }
    mt_c64_title_values_t value =
        c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
    mt_c64_sid_model_t *m = &value.sid;
    uint64_t fields = 0;
    uint8_t classes = 0;
    unsigned n = c64_selected_sid;
    if (n >= count)
        n = 0;
    unsigned base = 25 + 4 * n;
    uint8_t *field = NULL;
    unsigned maximum = 0;
    switch (item) {
    case 0: {
        unsigned id = mt_c64_sid_preset_id(m);
        id = delta < 0 ? (id <= 1 ? MT_SID_EIGHT : id - 1) : (id >= MT_SID_EIGHT ? 1 : id + 1);
        (void)mt_c64_sid_preset(m, id);
        fields = UINT64_MAX;
        classes = 7;
        break;
    }
    case 2:
        m->mirror ^= 1;
        fields = UINT64_C(1) << 3;
        break;
    case 4:
        field = &m->sid_address[n];
        maximum = (caps.expansion & MT_C64_EXPANSION_CAPS_SID_IO_SLOTS) ? 48 : 32;
        fields = UINT64_C(1) << base;
        break;
    case 5:
        field = &m->sid_volume[n];
        maximum = 30;
        fields = UINT64_C(1) << (base + 2);
        break;
    case 6:
        field = &m->sid_pan[n];
        maximum = 10;
        fields = UINT64_C(1) << (base + 3);
        break;
    case 7:
        field = &m->sid_model[n];
        maximum = (caps.expansion & MT_C64_EXPANSION_CAPS_SID_MODEL_8580) ? 1 : 0;
        fields = UINT64_C(1) << (base + 1);
        break;
    case 8:
        m->master = (uint8_t)cycle_setting(m->master, 30, delta);
        fields = UINT64_C(1) << 24;
        break;
    case 9:
        m->headroom = (uint8_t)cycle_setting(m->headroom, 3, delta);
        fields = UINT64_C(1) << 57;
        break;
    case 10:
        m->dc_remove = (uint8_t)cycle_setting(m->dc_remove, 2, delta);
        fields = UINT64_C(1) << 58;
        break;
    case 11:
        m->resample ^= 1;
        fields = UINT64_C(1) << 59;
        break;
    case 12:
        (void)mt_c64_sid_preset(m, MT_SID_DUAL500);
        fields = UINT64_MAX;
        classes = 7;
        break;
    default:
        return;
    }
    if (field) {
        unsigned index = *field == 255 ? maximum + 1 : *field;
        index = cycle_setting(index, maximum + 1, delta);
        *field = index > maximum ? 255 : (uint8_t)index;
    }
    (void)commit_c64_edit(&value, MT_TITLE_SID, fields, classes);
}

static void handle_menu_action(mt_menu_action_t action)
{
    if (c64_debug.reu_host.reload_required && menu_action_needs_core_progress(action) &&
        action.kind != MT_MENU_ACTION_SWITCH_CORE && action.kind != MT_MENU_ACTION_REBOOT_GOLDEN) {
        set_supervisor_error("REU cache did not clear; reload the C64 core");
        return;
    }
    if (golden_update_worker_held && action.kind != MT_MENU_ACTION_UPDATE_GOLDEN &&
        action.kind != MT_MENU_ACTION_REBOOT_GOLDEN &&
        action.kind != MT_MENU_ACTION_SWITCH_FIRMWARE) {
        set_supervisor_error("CPU1 held; retry Golden update or reboot");
        return;
    }
    if (action.kind == MT_MENU_ACTION_SWITCH_CORE) {
        mt_core_selection_t selection;
        mt_supervisor_result_t result;
        if (action.core_index >= core_catalog.count ||
            action.variant_index >= core_catalog.cores[action.core_index]
                                      .package.variant_count) {
            set_supervisor_error("Invalid core selection");
            return;
        }
        selection = core_catalog.cores[action.core_index];
        selection.variant_index = action.variant_index;
        set_supervisor_status("Switching core...");
        result = shell_is_c64() && (c64_uci_service_reload_required() ||
                                   c64_debug.reu_host.reload_required)
                     ? mt_supervisor_reload(&supervisor, &selection)
                     : mt_supervisor_switch(&supervisor, &selection);
        if (result == MT_SUPERVISOR_INCOMPATIBLE_ABI) {
            set_supervisor_error("Update core package for the smaller text interface");
            return;
        }
        if (result == MT_SUPERVISOR_OK) {
            if (!remember_manual_selection(&supervisor.active))
                set_supervisor_error("Core active; settings write failed");
            else if (!last_settings_restore_ok)
                set_supervisor_error("Core active; media restore failed");
            else
                set_supervisor_status("Core switched");
        } else if (result == MT_SUPERVISOR_NO_CHANGE) {
            set_supervisor_outcome(
                remember_manual_selection(&supervisor.active),
                "Core already active",
                "Core active; settings write failed");
        } else {
            set_supervisor_error(mt_supervisor_result_name(result));
        }
        xil_printf("[supervisor] switch %s state=%s backend=%d recovered=%u\r\n",
                   mt_supervisor_result_name(result),
                   mt_supervisor_state_name(supervisor.state),
                   supervisor.last_backend_error,
                   supervisor.recovered ? 1U : 0U);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_DRIVE8) {
        open_menu_browser(MT_MENU_BROWSER_DRIVE8);
    } else if(action.kind==MT_MENU_ACTION_BROWSE_AMIGA_DRIVE) {
        if(action.file_index>=MT_AMIGA_DRIVE_COUNT || shell_read(MT_REG_CORE_ID)!=MT_AMIGA_CORE_ID)return;
        iigs_browser_drive=(unsigned)action.file_index;
        open_menu_browser(MT_MENU_BROWSER_DRIVE8);
    } else if(action.kind==MT_MENU_ACTION_EJECT_AMIGA_DRIVE) {
        const int result=amiga_content_service_eject((unsigned)action.file_index);
        if(result!=0)set_supervisor_error(amiga_content_service_error());
        else set_supervisor_status("Amiga disk ejected");
    } else if(action.kind==MT_MENU_ACTION_AMIGA_DISPLAY) {
        if(shell_read(MT_REG_CORE_ID)!=MT_AMIGA_CORE_ID || !amiga_service_initialized)return;
        const unsigned delta=action.variant_index==1U ? MT_AMIGA_DISPLAY_MODES-1U:1U;
        uint8_t next=amiga_display_service_requested();
        for(unsigned i=0U;i<MT_AMIGA_DISPLAY_MODES;++i) {
            next=(uint8_t)((next+delta)%MT_AMIGA_DISPLAY_MODES);
            if(amiga_display_service_mode_available(next))break;
        }
        (void)amiga_request_display(next);
    } else if (action.kind == MT_MENU_ACTION_SPECTRUM_RELOAD) {
        if (!supervisor.has_active || supervisor.active.package.core_id != MT_SPECTRUM_CORE_ID)
            return;
        const char *const error = spectrum_boot_with_input(&supervisor.active);
        if (error != NULL) set_supervisor_error(error);
        else set_supervisor_status("Spectrum ROM loaded");
    } else if(action.kind==MT_MENU_ACTION_AMIGA_RELOAD || action.kind==MT_MENU_ACTION_AMIGA_RETRY) {
        usb_hid_service_set_core_ready(false);
        const int result=action.kind==MT_MENU_ACTION_AMIGA_RELOAD ? amiga_content_service_boot():amiga_content_service_retry();
        if(result!=0)set_supervisor_error(amiga_content_service_error());
        else {
            if(action.kind==MT_MENU_ACTION_AMIGA_RELOAD)amiga_display_guest_restarted();
            set_supervisor_status("Amiga ROM/media ready");
        }
        usb_hid_service_set_core_ready(result==0 && amiga_content_service_ready());
        if(dac_ready)set_audio(!amiga_content_service_ready(),false);
        set_video_test(!amiga_content_service_ready());
    } else if(action.kind==MT_MENU_ACTION_BROWSE_IIGS_DRIVE) {
        if(action.file_index>=MT_IIGS_DRIVE_COUNT || shell_read(MT_REG_CORE_ID)!=MT_IIGS_CORE_ID)return;
        iigs_browser_drive=(unsigned)action.file_index;
        open_menu_browser(MT_MENU_BROWSER_DRIVE8);
    } else if(action.kind==MT_MENU_ACTION_EJECT_IIGS_DRIVE) {
        const int result=iigs_content_service_eject((unsigned)action.file_index);
        if(result!=0)set_supervisor_error(iigs_content_service_error());
        else set_supervisor_status("Apple IIgs disk ejected");
    } else if(action.kind==MT_MENU_ACTION_IIGS_PRAM) {
        const bool restore=action.file_index==1U;
        if(restore)usb_hid_service_set_core_ready(false);
        const int result=action.file_index==2U ? iigs_content_service_retry():iigs_content_service_pram(action.file_index==0U);
        if(restore)service_usb_input();
        if(result!=0)set_supervisor_error(iigs_content_service_error());
        else set_supervisor_status(action.file_index==2U ? "Apple IIgs media recovered":action.file_index==0U ? "Apple IIgs PRAM saved":"Apple IIgs PRAM restored");
    } else if(action.kind==MT_MENU_ACTION_IIGS_RELOAD) {
        if(iigs_boot_with_input()!=0)set_supervisor_error(iigs_content_service_error());
        else if(iigs_content_service_faulted())set_supervisor_warning(iigs_content_service_error());
        else set_supervisor_status("Apple IIgs ROMs loaded");
    } else if (action.kind == MT_MENU_ACTION_BROWSE_DISK_PROGRAM) {
        open_menu_browser(MT_MENU_BROWSER_DISK_PROGRAM);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_CARTRIDGE) {
        open_menu_browser(MT_MENU_BROWSER_CARTRIDGE);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_CONFIG) {
        if (shell_is_c64())
            open_menu_browser(MT_MENU_BROWSER_CONFIG);
    } else if (action.kind == MT_MENU_ACTION_APPLY_CONFIG) {
        char relative[256], report[2048];
        if (!shell_is_c64() || action.file_index >= supervisor_menu_file_count ||
            !mt_media_browser_resolve(&vectrex_media_browser,
                                      supervisor_menu_files[action.file_index].name, relative,
                                      sizeof(relative)))
            return;
        if (quiesce_c64_uci() != 0) {
            set_supervisor_warning("Command interface is busy");
            return;
        }
        c64_title_foreground = true;
        refresh_c64_title(&supervisor.active);
        int result = c64_title_service_apply(relative, false, report, sizeof(report));
        c64_title_foreground = false;
        xil_printf("%s", report);
        if (result == 0)
            mt_menu_close_browser(&supervisor_menu);
        set_supervisor_outcome(result == 0, c64_title_service_status(), c64_title_service_status());
    } else if (action.kind == MT_MENU_ACTION_BROWSE_PROGRAM) {
        open_menu_browser(MT_MENU_BROWSER_PROGRAM);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_REU) {
        if (c64_debug.active && c64_debug.caps.reu_host)
            open_menu_browser(MT_MENU_BROWSER_REU);
    } else if (action.kind == MT_MENU_ACTION_LOAD_REU) {
        char path[640];
        if (action.file_index >= supervisor_menu_file_count ||
            !c64_debug.active || !c64_debug.caps.reu_host) return;
        const int length = snprintf(path, sizeof path, "%s/%s", vectrex_browser_path,
            supervisor_menu_files[action.file_index].name);
        if (length < 0 || (size_t)length >= sizeof path) {
            set_supervisor_error("REU image path is too long"); return;
        }
        const mt_c64_reu_load_result_t loaded = mt_c64_debug_load_reu(&c64_debug, path, 0U);
        if (loaded == MT_REU_LOAD_OK) {
            mt_menu_close_browser(&supervisor_menu);
            if (c64_uci_initialized)
                c64_uci_service_set_reu_image(path);
        } else
            set_supervisor_error(mt_c64_reu_load_message(loaded));
    } else if (action.kind == MT_MENU_ACTION_BROWSE_DOCUMENTS) {
        open_menu_browser(MT_MENU_BROWSER_DOCUMENTS);
    } else if (action.kind == MT_MENU_ACTION_OPEN_TEXT) {
        char path[640];
        const mt_menu_view_t view = supervisor_menu_view();
        const mt_menu_file_t *entry;
        int length;
        mt_text_document_result_t result;
        if (action.file_index >= supervisor_menu_file_count ||
            supervisor_menu.browser == MT_MENU_BROWSER_NONE ||
            supervisor_menu.browser == MT_MENU_BROWSER_DISK_PROGRAM || view.content_root == NULL)
            return;
        entry = &supervisor_menu_files[action.file_index];
        if ((entry->cookie & MT_MENU_FILE_TEXT) == 0U ||
            !mt_media_browser_valid_component(entry->name) || !is_text_document(entry->name)) return;
        length = snprintf(path, sizeof(path), "%s/%s", view.content_root, entry->name);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            set_supervisor_error("Document path is too long");
            return;
        }
        result = mt_text_document_load(&document_reader, path,
            MT_MENU_TEXT_COLUMNS, MT_MENU_TEXT_ROWS, document_read_progress, NULL);
        if (result == MT_TEXT_DOCUMENT_OK) {
            memcpy(document_title, entry->name, strlen(entry->name) + 1U);
            set_supervisor_status("Reading document; Escape returns to the browser");
        } else {
            set_supervisor_error(result == MT_TEXT_DOCUMENT_NO_MEMORY ? "Not enough memory for this document" :
                result == MT_TEXT_DOCUMENT_DECODE_FAILED ? "Unsupported document encoding" :
                result == MT_TEXT_DOCUMENT_CLOSE_FAILED ? "Could not close the document on SD" :
                "Could not read the document from SD");
        }
    } else if (action.kind == MT_MENU_ACTION_EDIT_GAMEPADS) {
        if (!supervisor.has_active) return;
        const mt_menu_view_t view = supervisor_menu_view();
        mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
        controller_edit_core = supervisor.active.package.core_id;
        memcpy(controller_edit_devices, view.gamepad_devices, sizeof(controller_edit_devices));
        for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
            mt_controls_default(controller_edit_core, &controller_edit_devices[slot], &configs[slot]);
            (void)usb_hid_service_get_controller_config(slot, &configs[slot]);
        }
        mt_menu_gamepad_begin(&supervisor_menu, &view, configs);
    } else if (action.kind == MT_MENU_ACTION_SET_GAMEPADS) {
        if (!supervisor.has_active || supervisor.active.package.core_id != controller_edit_core) {
            set_supervisor_error("Core changed; reopen Controllers before saving");
            return;
        }
        mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
        for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
            mt_controller_device_t device;
            mt_controls_default(controller_edit_core, NULL, &configs[slot]);
            (void)usb_hid_service_get_controller_config(slot, &configs[slot]);
            if (usb_hid_service_controller_device(slot, &device) && controller_edit_devices[slot].connected &&
                mt_controls_identity_equal(&device.identity, &controller_edit_devices[slot].identity))
                configs[slot] = supervisor_menu.gamepad_config[slot];
        }
        if (!usb_hid_service_validate_controller_configs(configs)) {
            set_supervisor_error("Player assignment conflict; choose another player");
            return;
        }
        settings_before_update = persistent_settings;
        for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
            if (!controller_edit_devices[slot].connected) continue;
            if (!mt_supervisor_settings_remember_controller(&persistent_settings, controller_edit_core,
                &controller_edit_devices[slot].identity, &supervisor_menu.gamepad_config[slot])) {
                persistent_settings = settings_before_update;
                set_supervisor_error("Controller profile storage is full or invalid");
                return;
            }
        }
        if (save_persistent_settings() && mt_supervisor_turbo_controls_save(&persistent_settings)) {
            (void)usb_hid_service_set_controller_configs(configs);
            mt_menu_gamepad_saved(&supervisor_menu);
            supervisor_menu.gamepad_edit = false;
            set_supervisor_status("Controller settings saved for this core");
        } else {
            persistent_settings = settings_before_update;
            set_supervisor_error("SD write failed; changes kept here. Retry Save changes");
        }
    } else if (action.kind == MT_MENU_ACTION_SET_CLOCK) {
        if (supervisor_menu.clock_edit && set_board_clock(&supervisor_menu.clock)) {
            supervisor_menu.clock_edit = false;
            set_supervisor_status("Board clock saved");
        } else set_supervisor_error("Clock write or verification failed; retry Save time");
    } else if (action.kind == MT_MENU_ACTION_USB_SD) {
        mount_sd_over_usb(false);
    } else if (action.kind == MT_MENU_ACTION_RESTART_USB) {
        int status = usb_hid_service_stop();
        if (status == 0) status = usb_hid_service_start();
        memset(supervisor_menu_keys, 0, sizeof(supervisor_menu_keys));
        set_supervisor_outcome(status == 0, "USB restarted; detecting devices", "USB restart failed; check connections");
    } else if (action.kind == MT_MENU_ACTION_BROWSE_OVERLAY) {
        if (supervisor.has_active && selection_supports_vectrex_video(&supervisor.active))
            open_menu_browser(MT_MENU_BROWSER_OVERLAY);
        else set_supervisor_warning("Update the Vectrex core for overlays");
    } else if (action.kind == MT_MENU_ACTION_OPEN_DIRECTORY) {
        mt_media_browser_t directory = vectrex_media_browser;
        const mt_menu_file_t *entry;
        bool changed;
        if (!mt_core_frontend_browser_folders(supervisor.has_active ? supervisor.active.package.core_id:0U,
            supervisor_menu.browser) ||
            supervisor_menu.browser == MT_MENU_BROWSER_NONE ||
            action.file_index >= supervisor_menu_file_count) return;
        entry = &supervisor_menu_files[action.file_index];
        if ((entry->cookie & MT_MENU_FILE_DIRECTORY) == 0U) return;
        changed = (entry->cookie & MT_MENU_FILE_PARENT) != 0U ?
            mt_media_browser_up(&directory) : mt_media_browser_enter(&directory, entry->name);
        if (!changed) {
            set_supervisor_error("Cannot enter that folder; path is too long");
            return;
        }
        list_vectrex_browser(supervisor_menu.browser, &directory);
    } else if (action.kind == MT_MENU_ACTION_MOUNT_DRIVE8) {
        int status;
        char c64_relative[MT_MEDIA_BROWSER_PATH_MAX + 1U];
        if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID) {
            char relative[MT_AMIGA_MEDIA_NAME_MAX+1U];
            if(action.file_index>=supervisor_menu_file_count)return;
            if((supervisor_menu_files[action.file_index].cookie&(MT_MENU_FILE_DIRECTORY|MT_MENU_FILE_TEXT))!=0U ||
                !mt_media_browser_resolve(&vectrex_media_browser,supervisor_menu_files[action.file_index].name,relative,sizeof(relative))) {
                set_supervisor_error("Disk path is too long or invalid");return;
            }
            status=amiga_content_service_mount(iigs_browser_drive,relative);
            if(status!=0)set_supervisor_error(amiga_content_service_error());
            else { mt_menu_close_browser(&supervisor_menu);set_supervisor_status("Amiga disk mounted"); }
            return;
        }
        if(shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID) {
            char relative[MT_IIGS_MEDIA_NAME_MAX+1U];
            if(action.file_index>=supervisor_menu_file_count)return;
            if((supervisor_menu_files[action.file_index].cookie&(MT_MENU_FILE_DIRECTORY|MT_MENU_FILE_TEXT))!=0U ||
                !mt_media_browser_resolve(&vectrex_media_browser,
                    supervisor_menu_files[action.file_index].name,relative,sizeof(relative)) ||
                !mt_iigs_media_path_valid(relative)) {
                set_supervisor_error("Disk path is too long or invalid");return;
            }
            status=iigs_content_service_mount(iigs_browser_drive,relative);
            if(status!=0)set_supervisor_error(iigs_content_service_error());
            else { mt_menu_close_browser(&supervisor_menu); set_supervisor_status("Apple IIgs disk mounted"); }
            return;
        }
        if (action.file_index >= supervisor_menu_file_count ||
            !c64_drive_ready) {
            set_supervisor_warning("Drive 8 is unavailable");
            return;
        }
        if ((supervisor_menu_files[action.file_index].cookie &
                (MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_TEXT)) != 0U ||
            !mt_media_browser_resolve(&vectrex_media_browser,
                supervisor_menu_files[action.file_index].name,
                c64_relative, sizeof(c64_relative))) {
            set_supervisor_error("Disk path is too long or invalid");
            return;
        }
        status = mount_c64_disk(c64_relative);
        if (status == 0) mt_menu_close_browser(&supervisor_menu);
        if (status != 0)
            set_supervisor_error("Drive 8 mount failed");
        else if (strlen(c64_relative) >= MT_SETTINGS_MEDIA_NAME_MAX) {
            if (remember_active_drive8(NULL))
                set_supervisor_warning("Drive mounted; long path cannot be remembered");
            else
                set_supervisor_error("Mounted; could not clear previous drive setting");
        }
        else if (!remember_active_drive8(c64_relative))
            set_supervisor_error("Drive mounted; settings write failed");
        else
            set_supervisor_status("Drive 8 mounted");
    } else if (action.kind == MT_MENU_ACTION_MOUNT_AND_RUN ||
               action.kind == MT_MENU_ACTION_LOAD_DISK_PROGRAM ||
               action.kind == MT_MENU_ACTION_DIRECT_DISK_PROGRAM) {
        mt_c64_content_service_result_t result;
        mt_c64_content_service_result_t run_result;
        mt_c64_d64_result_t extract_result;
        bool remounted;
        bool settings_saved = true;
        const bool drive_load = action.kind == MT_MENU_ACTION_MOUNT_AND_RUN;
        size_t extracted_size = 0U;
        uint32_t cookie;
        bool first_program;
        if (action.file_index >= supervisor_menu_file_count) {
            set_supervisor_error("Mount & Run could not start");
            return;
        }
        cookie = supervisor_menu_files[action.file_index].cookie;
        first_program = (cookie & MT_MENU_FILE_FIRST_PROGRAM) != 0U;
        cookie &= ~MT_MENU_FILE_FIRST_PROGRAM;
        if (cookie >= supervisor_d64_prg_count ||
            (first_program && !first_program_current(cookie))) {
            set_supervisor_warning("D64 program selection is stale");
            return;
        }
        extract_result = c64_d64_service_extract_prg(
            &supervisor_d64_prgs[cookie], supervisor_disk_prg_buffer,
            sizeof(supervisor_disk_prg_buffer), &extracted_size);
        if (extract_result != MT_C64_D64_OK) {
            xil_printf("[C64] Mount & Run extract %s: %s\r\n",
                       supervisor_menu_files[action.file_index].name,
                       mt_c64_d64_result_name(extract_result));
            set_supervisor_error("Could not extract that D64 program");
            return;
        }
        {
            const mt_c64_content_image_t image = {
                supervisor_disk_prg_buffer, extracted_size
            };
            if (mt_c64_validate_prg(&image).status != MT_C64_CONTENT_OK) {
                set_supervisor_error("Invalid PRG; disk remains mounted");
                return;
            }
            if (action.kind == MT_MENU_ACTION_LOAD_DISK_PROGRAM ||
                !mt_c64_prg_has_basic_program(&image)) {
                if (action.kind == MT_MENU_ACTION_DIRECT_DISK_PROGRAM) {
                    set_supervisor_warning("No BASIC start; use Enter or Right to load");
                    return;
                }
                launch_disk_program_native(cookie, first_program);
                return;
            }
        }
        if (drive_load) {
            const uint8_t *const name = supervisor_d64_prgs[cookie].raw_name;
            const size_t program_size = extracted_size;
            if (!first_program &&
                (!mt_c64_native_load_name_supported(name) ||
                 !native_load_name_unique(cookie))) {
                set_supervisor_error("Name cannot be selected exactly by a drive LOAD");
                return;
            }
            /* An in-program LOAD makes BASIC start the loaded program after
             * the real disk transfer succeeds. Preserve KERNAL/drive state
             * without guessing a delay or queuing RUN after a failed LOAD. */
            if ((first_program ?
                    mt_c64_native_load_build_first_basic_launcher(
                        supervisor_disk_prg_buffer,
                        sizeof(supervisor_disk_prg_buffer), &extracted_size) :
                    mt_c64_native_load_build_basic_launcher(
                        name, supervisor_disk_prg_buffer,
                        sizeof(supervisor_disk_prg_buffer), &extracted_size)) !=
                MT_C64_NATIVE_LOAD_OK) {
                set_supervisor_error("Could not prepare disk LOAD launcher");
                return;
            }
            /* In-program LOAD retains VARTAB. Reserve the target's full
             * extent before RUN so its variables cannot overwrite loaded
             * BASIC/code. Short targets retain the larger launcher extent. */
            if (extracted_size < program_size) {
                memset(supervisor_disk_prg_buffer + extracted_size, 0,
                       program_size - extracted_size);
                extracted_size = program_size;
            }
        }
        if (!prepare_c64_content_change()) {
            set_supervisor_error("Mount & Run could not start");
            return;
        }
        result = c64_title_disk(supervisor_disk_prg_buffer, extracted_size);
        print_c64_content_result(
            drive_load ? "disk LOAD launcher stage" : "direct stage",
            supervisor_menu_files[action.file_index].name,
            &result);
        remounted = finish_c64_content_change();
        if (result.status != MT_C64_CONTENT_SERVICE_OK) {
            set_supervisor_error("Mount & Run failed");
        } else {
            mt_menu_close_browser(&supervisor_menu);
            settings_saved = remember_active_cartridge(NULL);
            if (!remounted) {
                set_supervisor_warning(
                    "Program staged; retry disk remount");
                return;
            }
            run_result = c64_content_service_run_staged_prg();
            print_c64_content_result(
                drive_load ? "disk LOAD launcher start" : "direct start",
                supervisor_menu_files[action.file_index].name,
                &run_result);
            if (run_result.status != MT_C64_CONTENT_SERVICE_OK) {
                set_supervisor_error("Program staged; RUN failed");
            } else if (!settings_saved) {
                set_supervisor_error(
                    "Program running; settings write failed");
            } else {
                set_supervisor_status(
                    drive_load ? "Disk LOAD started; close F12 to view" :
                                 "Program running; D64 remains mounted");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_LOAD_CONTENT) {
        if (action.file_index >= supervisor_menu_file_count) {
            set_supervisor_error("Content load could not start");
            return;
        }
        if (shell_read(MT_REG_CORE_ID) == MT_SPECTRUM_CORE_ID) {
            char name[MT_MEDIA_BROWSER_PATH_MAX + 1U];
            if (!supervisor.has_active ||
                (supervisor_menu_files[action.file_index].cookie & MT_MENU_FILE_DIRECTORY) != 0U ||
                !mt_media_browser_resolve(&vectrex_media_browser,
                    supervisor_menu_files[action.file_index].name, name, sizeof(name))) {
                set_supervisor_error("Invalid tape path");
                return;
            }
            const char *const error = spectrum_content_insert_tape(&shell_link,
                supervisor.active.package.content_root, name, iigs_transfer_progress);
            if (error != NULL) set_supervisor_error(error);
            else {
                mt_menu_close_browser(&supervisor_menu);
                set_supervisor_status("Tape inserted. Close F12 and press F1 to play.");
            }
        } else if (shell_read(MT_REG_CORE_ID) == MT_VECTREX_CORE_ID) {
            char name[MT_MEDIA_BROWSER_PATH_MAX + 1U];
            if ((supervisor_menu_files[action.file_index].cookie & MT_MENU_FILE_DIRECTORY) != 0U ||
                !mt_media_browser_resolve(&vectrex_media_browser,
                    supervisor_menu_files[action.file_index].name, name, sizeof(name))) {
                set_supervisor_error("Content path is too long or invalid");
                return;
            }
            const bool basic = mt_content_classify(name) == MT_CONTENT_VECTREX_BASIC;
            const bool overlay = mt_content_classify(name) == MT_CONTENT_VECTREX_OVERLAY;
            const mt_vectrex_content_result_t result = load_vectrex_content_with_input(name);
            if (result.status == MT_VECTREX_CONTENT_OK) {
                mt_menu_close_browser(&supervisor_menu);
                if (!overlay) {
                    if (dac_ready) set_audio(basic, false);
                    set_video_test(false);
                }
                if (overlay) {
                    set_supervisor_status("Vectrex overlay loaded");
                } else if (!basic && strlen(name) >= MT_SETTINGS_MEDIA_NAME_MAX) {
                    if (remember_active_cartridge(NULL))
                        set_supervisor_warning("Cartridge loaded; long path cannot be remembered");
                    else
                        set_supervisor_error("Loaded; could not clear previous cartridge setting");
                } else {
                    set_supervisor_outcome(remember_active_cartridge(basic ? NULL : name),
                        basic ? "BASIC finished; choose a program to run again" : "Vectrex cartridge loaded",
                        "Loaded; settings write failed");
                }
                if (supervisor_status_color != MT_UI_COLOR_RED &&
                    vectrex_content_service_overlay_warning()[0] != '\0')
                    set_supervisor_warning(vectrex_content_service_overlay_warning());
            } else {
                xil_printf("[Vectrex] load failed: %s sd=%s link=%s\r\n",
                    vectrex_content_status_name(result.status), mt_sd_status_name(result.sd.status),
                    mt_link_result_name(result.link));
                set_supervisor_error(overlay ? vectrex_content_status_name(result.status) :
                    basic && vectrex32_service_error()[0] != '\0' ?
                    vectrex32_service_error() : "Vectrex load failed; check BIOS and file");
            }
        } else if (shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID) {
            handle_nes_content_load(supervisor_menu_files[action.file_index].name);
        } else if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
            handle_genesis_content_load(supervisor_menu_files[action.file_index].name);
        } else if (shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID) {
            const mt_gameboy_content_service_result_t result =
                gameboy_content_service_load(
                    supervisor_menu_files[action.file_index].name);
            xil_printf("[GBC] load %s: %s validation=%s link=%s "
                       "mapper=%02X rom=%02X ram=%02X cgb=%u required=%u "
                       "save=%u existed=%u path=%s\r\n",
                       supervisor_menu_files[action.file_index].name,
                       gameboy_content_service_status_name(result.status),
                       mt_content_validation_name(result.validation),
                       mt_link_result_name(result.link),
                       (unsigned int)result.info.mapper_type,
                       (unsigned int)result.info.rom_size_code,
                       (unsigned int)result.info.ram_size_code,
                       result.info.color_capable ? 1U : 0U,
                       result.info.color_required ? 1U : 0U,
                       (unsigned int)result.save_size,
                       result.save_existed ? 1U : 0U,
                       gameboy_content_service_save_path() != NULL ?
                           gameboy_content_service_save_path() : "none");
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                mt_menu_close_browser(&supervisor_menu);
                usb_hid_service_set_core_ready(true);
                if (dac_ready) set_audio(false, false);
                set_video_test(false);
            }
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                if (!remember_active_cartridge(
                        supervisor_menu_files[action.file_index].name)) {
                    set_supervisor_error(
                        "Game Boy loaded; settings write failed");
                } else {
                    set_supervisor_status(
                        !result.save_active ? "Game Boy cartridge loaded" :
                        (result.save_existed ?
                         "Game Boy loaded; save restored" :
                         "Game Boy loaded; new save ready"));
                }
            } else {
                set_supervisor_error(
                    result.status == MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED ?
                    "Load blocked: save write failed" :
                    "Cartridge load failed");
            }
        } else {
            mt_c64_content_service_result_t result;
            bool remounted;
            char relative[MT_MEDIA_BROWSER_PATH_MAX + 1U];
            if ((supervisor_menu_files[action.file_index].cookie &
                    (MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_TEXT)) != 0U ||
                !mt_media_browser_resolve(&vectrex_media_browser,
                    supervisor_menu_files[action.file_index].name,
                    relative, sizeof(relative))) {
                set_supervisor_error("Content path is too long or invalid");
                return;
            }
            if (!prepare_c64_content_change()) {
                set_supervisor_error("Content load could not start");
                return;
            }
            result = c64_title_load(relative, true);
            print_c64_content_result("load", relative, &result);
            remounted = finish_c64_content_change();
            if (result.status == MT_C64_CONTENT_SERVICE_OK)
                mt_menu_close_browser(&supervisor_menu);
            if (result.status != MT_C64_CONTENT_SERVICE_OK) {
                set_supervisor_error(c64_content_failure_message(result.status, remounted));
            } else if (mt_content_classify(relative) == MT_CONTENT_CRT &&
                       strlen(relative) >= MT_SETTINGS_MEDIA_NAME_MAX) {
                if (!remember_active_cartridge(NULL))
                    set_supervisor_error("Loaded; could not clear previous cartridge setting");
                else if (!remounted)
                    set_supervisor_error("Content loaded; drive remount failed");
                else
                    set_supervisor_warning("Cartridge loaded; long path cannot be remembered");
            } else if (!remember_active_cartridge(
                           mt_content_classify(relative) == MT_CONTENT_CRT ?
                           relative : NULL)) {
                set_supervisor_error(remounted ?
                    "Content loaded; settings write failed" :
                    "Content loaded; drive remount and settings write failed");
            } else if (!remounted) {
                set_supervisor_error("Content loaded; drive remount failed");
            } else {
                set_supervisor_status("Content loaded");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_EJECT_DRIVE8) {
        if (c64_d64_service_eject() != 0)
            set_supervisor_error("Drive 8 eject failed");
        else if (!remember_active_drive8(NULL))
            set_supervisor_error("Drive ejected; settings write failed");
        else
            set_supervisor_status("Drive 8 ejected");
    } else if (action.kind == MT_MENU_ACTION_DETACH_CARTRIDGE) {
        if (shell_read(MT_REG_CORE_ID) == MT_VECTREX_CORE_ID) {
            usb_hid_service_set_core_ready(false);
            const mt_vectrex_content_result_t result = vectrex_content_service_detach();
            if (result.status == MT_VECTREX_CONTENT_OK) {
                usb_hid_service_set_core_ready(true);
                set_supervisor_outcome(remember_active_cartridge(NULL),
                    "Vectrex cartridge unloaded", "Cart unloaded; settings write failed");
            } else set_supervisor_error("Vectrex cartridge unload failed");
        } else if (shell_read(MT_REG_CORE_ID) == MT_NES_CORE_ID) {
            handle_nes_cart_detach();
        } else if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
            handle_genesis_cart_detach();
        } else if (shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID) {
            const mt_gameboy_content_service_result_t result =
                gameboy_content_service_detach();
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                usb_hid_service_set_core_ready(false);
                if (dac_ready) set_audio(true, false);
                set_video_test(true);
            }
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                set_supervisor_outcome(
                    remember_active_cartridge(NULL),
                    "Cartridge unloaded",
                    "Cart unloaded; settings write failed");
            } else {
                set_supervisor_error(
                    result.status == MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED ?
                    "Unload blocked: save write failed" :
                    "Cartridge unload failed");
            }
        } else {
            mt_c64_content_service_result_t result;
            bool remounted;
            if (!prepare_c64_content_change()) {
                set_supervisor_error("Cartridge detach could not start");
                return;
            }
            result = c64_title_detach();
            remounted = finish_c64_content_change();
            if (result.status == MT_C64_CONTENT_SERVICE_OK) {
                const bool settings_saved = remember_active_cartridge(NULL);
                if (!remounted) {
                    set_supervisor_error(settings_saved ?
                        "Cartridge detached; drive remount failed" :
                        "Cart detached; drive remount and settings write failed");
                } else {
                    set_supervisor_outcome(settings_saved,
                        "Cartridge detached",
                        "Cart detached; settings write failed");
                }
            } else {
                set_supervisor_error(remounted ? "Cartridge detach failed" :
                    "Cartridge detach and drive remount failed");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_SET_BORDER) {
        change_active_border(action.file_index);
    } else if (action.kind == MT_MENU_ACTION_SET_VECTREX_VIDEO) {
        change_active_vectrex_video(action.file_index);
    } else if (action.kind == MT_MENU_ACTION_SET_DISPLAY_MODE) {
        change_display_mode(action.file_index);
    } else if (action.kind == MT_MENU_ACTION_SET_VIDEO_MULTIPLIER) {
        change_active_video_multiplier(action.file_index);
    } else if (action.kind == MT_MENU_ACTION_SET_VIDEO_EFFECT) {
        change_active_video_effect(action.file_index, action.variant_index);
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE) {
        const uint32_t control = shell_read(MT_REG_CONTROL);
        const bool test_enabled =
            (control & MT_CONTROL_VIDEO_TEST_ENABLE) != 0U;
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL,
                  control ^ MT_CONTROL_VIDEO_TEST_ENABLE);
        set_supervisor_status(test_enabled ? "Core video selected" :
                              "Multitini standby selected");
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP) {
        const bool symbolic = !usb_hid_service_symbolic_keyboard();
        if (usb_hid_service_set_symbolic_keyboard(symbolic) != 0)
            set_supervisor_error("Keyboard map change failed");
        else if (!remember_c64_input_settings())
            set_supervisor_error("Keyboard changed; settings write failed");
        else
            set_supervisor_status("Keyboard map changed");
    } else if (action.kind == MT_MENU_ACTION_SWAP_JOYSTICK_PORT) {
        mt_c64_title_values_t value =
            c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
        value.joystick = value.joystick == 2 ? 1 : 2;
        (void)commit_c64_edit(&value, MT_TITLE_JOYSTICK, 0, 0);
    } else if (action.kind == MT_MENU_ACTION_SID_SETUP) {
        if (shell_is_c64())
            change_c64_sid_setup((unsigned)action.file_index, action.variant_index == 1 ? -1 : 1);
    } else if (action.kind == MT_MENU_ACTION_NETWORK_MODE) {
        set_supervisor_outcome(mt_net_service_cycle_mode(), "Network mode saved",
                               "Network mode could not be saved");
    } else if (action.kind == MT_MENU_ACTION_NETWORK_RENEW) {
        mt_net_service_renew();
        set_supervisor_status("Network renewal requested");
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_DRIVE_ACTIVITY) {
        const bool previous = persistent_settings.drive_activity_off;
        persistent_settings.drive_activity_off = !previous;
        if (!save_persistent_settings()) {
            persistent_settings.drive_activity_off = previous;
            set_supervisor_error("Drive activity setting was not saved");
        } else {
            set_supervisor_status(persistent_settings.drive_activity_off ?
                "Drive activity badge Off" : "Drive activity badge On");
        }
    } else if (action.kind == MT_MENU_ACTION_EXPORT_CARTRIDGE) {
        char exported[MT_MEDIA_BROWSER_PATH_MAX + 16U];
        char message[sizeof(exported) + 16U];
        if (supervisor_menu.visible)
            show_handoff_progress("Writing cartridge CRT; please wait");
        const mt_c64_content_service_result_t result =
            c64_content_service_export_crt(exported, sizeof(exported));
        xil_printf("[C64] EasyFlash export: %s sd=%s bytes=%u\r\n",
                   c64_content_service_status_name(result.status),
                   mt_sd_status_name(result.sd.status), (unsigned)result.sd.size);
        if (result.status == MT_C64_CONTENT_SERVICE_OK) {
            (void)snprintf(message, sizeof(message), "Exported %s", exported);
            set_supervisor_status(message);
        } else {
            set_supervisor_error(c64_content_service_status_name(result.status));
        }
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_CORE_OPTION) {
        if (shell_is_c64() && action.file_index == 20U) {
            (void)change_c64_directory_drive();
            return;
        }
        if (shell_is_c64() && action.file_index == 21U) {
            const bool disable = !c64_content_service_ef_jumper_disabled();
            set_supervisor_outcome(c64_content_service_set_ef_jumper(disable) == 0,
                disable ? "EasyFlash jumper Disable; reset to start BASIC" :
                          "EasyFlash jumper Boot; reset to start the cartridge",
                "EasyFlash jumper is unavailable in this C64 image");
            return;
        }
        if (shell_is_c64() && action.file_index >= 16 && action.file_index <= 19) {
            if (action.file_index == 16) {
                supervisor_menu.core_subpage = true;
                supervisor_menu.row = 0;
                supervisor_menu.first_row = 0;
                return;
            }
            if (action.file_index == 17) {
                settings_before_update = persistent_settings;
                persistent_settings.c64_title_cfg_off = !persistent_settings.c64_title_cfg_off;
                if (!save_persistent_settings()) {
                    persistent_settings = settings_before_update;
                    set_supervisor_warning("Automatic title setting not saved");
                } else {
                    refresh_c64_title(&supervisor.active);
                    set_supervisor_status(
                        persistent_settings.c64_title_cfg_off
                            ? "Automatic title files off; manual Apply remains available"
                            : "Automatic title files on");
                }
                return;
            }
            if (quiesce_c64_uci() != 0) {
                set_supervisor_warning("Command interface is busy");
                return;
            }
            c64_title_foreground = true;
            int result =
                action.file_index == 18 ? c64_title_service_revert() : c64_title_service_save();
            c64_title_foreground = false;
            set_supervisor_outcome(result == 0, c64_title_service_status(),
                                   c64_title_service_status());
            return;
        }
        if(supervisor.has_active && supervisor.active.package.core_id==MT_AMIGA_CORE_ID) {
            mt_amiga_config_t changed=*amiga_content_service_config();
            if(action.file_index==0U) {
                if(amiga_content_service_ap68040()) {
                    if(!amiga_content_service_ap68040_2x_available())return;
                    if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() || amiga_display_service_pending()) {
                        set_supervisor_warning("CPU change unavailable while storage/display is busy");return;
                    }
                    changed.cpu=changed.cpu==MT_AMIGA_CPU_AP68040_2X ? MT_AMIGA_CPU_68020_TURBO:MT_AMIGA_CPU_AP68040_2X;
                } else changed.cpu=changed.cpu==MT_AMIGA_CPU_STOCK ?
                    (amiga_content_service_cpu14_available() ? MT_AMIGA_CPU_68020_14:MT_AMIGA_CPU_68020_TURBO):
                    changed.cpu==MT_AMIGA_CPU_68020_14 ? MT_AMIGA_CPU_68020_TURBO:MT_AMIGA_CPU_STOCK;
            }
            else if(action.file_index==1U)return;
            else if(action.file_index==2U)changed.fast_mib=changed.fast_mib==4U ? 8U:4U;
            else if(action.file_index==3U)changed.floppy_turbo^=1U;
            else if(action.file_index==4U) {
                if(usb_sd_modal || mt_sd_storage_external_active() || mt_screenshot_service_busy() || amiga_display_service_pending()) {
                    set_supervisor_warning("IDE change unavailable while storage/display is busy");return;
                }
                changed.ide_compat^=1U;
            }
            else return;
            (void)apply_amiga_config(&changed);return;
        }
        if(supervisor.has_active && supervisor.active.package.core_id==MT_IIGS_CORE_ID) {
            if(action.file_index==9U) {
                const uint16_t next=mt_iigs_mouse_next_sensitivity(iigs_content_service_mouse_sensitivity());
                if(iigs_content_service_set_mouse_sensitivity(next)!=0)
                    set_supervisor_error(iigs_content_service_error());
                else {
                    (void)usb_hid_service_set_iigs_mouse_sensitivity(next);
                    set_supervisor_status("Apple IIgs mouse sensitivity saved");
                }
                return;
            }
            mt_iigs_config_t changed=*iigs_content_service_config();
            if(action.file_index==0U) {
                if(changed.card!=0U)return;
                changed.speed=mt_iigs_next_speed(changed.speed,iigs_content_service_speed_mask());
            }
            else if(action.file_index==1U)changed.rom3^=1U;
            else if(action.file_index==2U)changed.card=changed.card==0U ? 2U:0U;
            else if(action.file_index==3U)changed.pulses^=1U;
            else if(action.file_index<9U)changed.flags^=(uint8_t)(1U<<(action.file_index-4U));
            else if(action.file_index==10U) {
                if(!iigs_content_service_fast_floppy_supported())return;
                changed.flags^=MT_IIGS_FLAG_FAST_FLOPPY;
            }
            else return;
            usb_hid_service_set_core_ready(false);
            if(iigs_content_service_configure(&changed)!=0)set_supervisor_error(iigs_content_service_error());
            else if(iigs_content_service_faulted())set_supervisor_warning(iigs_content_service_error());
            else set_supervisor_status("Apple IIgs settings saved");
            usb_hid_service_set_core_ready(iigs_content_service_ready());
            if(dac_ready)set_audio(!iigs_content_service_ready(),false);
            set_video_test(!iigs_content_service_ready());
            return;
        }
        if (supervisor.has_active && supervisor.active.package.core_id == MT_GENESIS_CORE_ID) {
            change_genesis_controller(action.file_index);
            return;
        }
        if (supervisor.has_active && supervisor.active.package.core_id == MT_C64_CORE_ID &&
            (action.file_index == 4U || action.file_index == 5U || action.file_index == 6U ||
             action.file_index == 7U || action.file_index == 9U || action.file_index == 10U ||
             action.file_index == 12U || action.file_index == 13U ||
             (action.file_index == 1U && selection_c64_features(&supervisor.active).turbo))) {
            change_c64_turbo(action.file_index);
            return;
        }
        if (supervisor.has_active && supervisor.active.package.core_id == MT_C64_CORE_ID &&
            action.file_index >= 2U) {
            change_c64_expansion(action.file_index);
            return;
        }
        const uint32_t control = shell_read(MT_REG_CONTROL);
        bool enabled;
        bool valid;
        if (!supervisor.has_active ||
            supervisor.active.package.core_id != MT_C64_CORE_ID ||
            supervisor.active.package.abi_minor < 8U ||
            action.file_index > 1U) {
            set_supervisor_warning("Core option unavailable");
            return;
        }
        if (action.file_index == 0U) {
            const mt_core_settings_profile_t *const profile =
                mt_supervisor_settings_find(
                    &persistent_settings, supervisor.active.package.id);
            const bool previous_active = c64_fast_roms_active;
            c64_rom_stage_result_t stage_result = C64_ROM_STAGE_OK;

            enabled = !(profile != NULL && profile->c64_fast_roms);
            if (enabled != previous_active) {
                stage_result = stage_c64_rom_profile(enabled);
                if (stage_result != C64_ROM_STAGE_OK) {
                    switch (stage_result) {
                    case C64_ROM_STAGE_PROFILE_UNAVAILABLE:
                        set_supervisor_warning(enabled ?
                            "Fast ROM pair unavailable" :
                            "Stock ROM pair unavailable");
                        break;
                    case C64_ROM_STAGE_NO_ROLLBACK_PAIR:
                        set_supervisor_warning(
                            "Restart with a stock drive ROM before switching");
                        break;
                    case C64_ROM_STAGE_MEDIA_FAILED:
                        set_supervisor_error(
                            "Drive remount failed; previous ROMs restored");
                        break;
                    case C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING:
                        set_supervisor_warning(
                            "Previous ROMs restored; drive remount pending");
                        break;
                    case C64_ROM_STAGE_ROLLBACK_FAILED:
                        set_supervisor_error(
                            "ROM rollback failed; restart the core");
                        break;
                    case C64_ROM_STAGE_SWITCH_FAILED:
                    default:
                        set_supervisor_error(
                            "ROM switch failed; previous ROMs active");
                        break;
                    }
                    return;
                }
            }
            settings_before_update = persistent_settings;
            valid = ensure_active_settings_profile() &&
                mt_supervisor_settings_remember_c64_fast_roms(
                    &persistent_settings, supervisor.active.package.id,
                    enabled);
            if (!valid || !save_persistent_settings()) {
                c64_rom_restore_result_t restore_result =
                    C64_ROM_RESTORE_OK;
                persistent_settings = settings_before_update;
                if (enabled != previous_active)
                    restore_result = restore_active_c64_rom_profile();
                if (restore_result == C64_ROM_RESTORE_FAILED) {
                    set_supervisor_error(
                        "Settings failed; ROM rollback failed");
                } else if (restore_result ==
                           C64_ROM_RESTORE_MEDIA_PENDING) {
                    set_supervisor_error(
                        "Settings failed; ROM restored; drive remount pending");
                } else {
                    set_supervisor_warning("Fast ROM setting not saved");
                }
                return;
            }
            if (enabled != previous_active) {
                remember_active_c64_rom_profile(enabled, true);
            }
            c64_fast_roms_fallback = false;
            if (enabled != previous_active) {
                c64_title_service_reset(c64_content_service_crt_attached(),
                                        c64_d64_service_mounted());
                (void)restore_selection_settings(&supervisor.active);
            }
            set_supervisor_status(enabled ?
                "Fast ROM pair active" : "Stock ROMs active");
            return;
        }

        enabled = (control & MT_CONTROL_C64_CPU_TURBO_ENABLE) == 0U;
        settings_before_update = persistent_settings;
        valid = ensure_active_settings_profile();
        if (valid) {
            valid = mt_supervisor_settings_remember_c64_cpu_turbo(
                &persistent_settings, supervisor.active.package.id, enabled);
        }
        if (!valid || !save_persistent_settings()) {
            persistent_settings = settings_before_update;
            set_supervisor_warning("CPU turbo setting not saved");
        } else {
            set_c64_cpu_turbo(enabled);
            set_supervisor_status(enabled ?
                "CPU turbo enabled" : "CPU turbo disabled");
        }
    } else if (action.kind == MT_MENU_ACTION_RESET_CORE) {
        if (!supervisor.has_active) {
            set_supervisor_warning("No active core");
            return;
        }
        if (supervisor.active.package.core_id == MT_VECTREX_CORE_ID &&
            vectrex_content_service_basic_selected()) {
            const mt_vectrex_content_result_t restarted = vectrex_content_service_restart_basic();
            if (restarted.status == MT_VECTREX_CONTENT_OK)
                set_supervisor_status("BASIC restarted and finished");
            else if (restarted.status == MT_VECTREX_CONTENT_NO_BASIC_PROGRAM)
                set_supervisor_warning("Select a BASIC program to run");
            else
                set_supervisor_error(restarted.status == MT_VECTREX_CONTENT_BASIC_FAILED ?
                    vectrex32_service_error() : "BASIC restart failed; check source file");
            return;
        }
        /* A user-requested reset is also an explicit retry of a failed
         * directory-drive commit. Automatic storage handoffs never clear it. */
        if (supervisor.active.package.core_id == MT_C64_CORE_ID) {
            const int retry = retry_c64_storage_for_reset();
            if (retry != 0) {
                set_supervisor_error(retry == 2 ? "Directory drive retry failed; check SD card" :
                    c64_uci_service_reload_required() ? "UCI completion unknown; reload the C64 core" :
                    "UCI cleanup failed; check SD card");
                return;
            }
        }
        const bool title_disk_present = c64_d64_service_mounted();
        const int asserted = set_core_reset_request(true);
        int released = -1;
        bool restored = true;
        if (asserted == 0) {
            if (shell_is_c64())
                c64_title_service_reset(c64_content_service_crt_attached(), title_disk_present);
            usleep(1000U);
        }
        if (asserted == 0) released = set_core_reset_request(false);
        if (released == 0 &&
            supervisor.active.package.core_id == MT_C64_CORE_ID) {
            last_settings_restore_ok = restore_selection_settings(
                &supervisor.active);
            restored = last_settings_restore_ok;
        }
        if (asserted != 0 || released != 0) {
            set_supervisor_error("Core restart failed");
        } else if (!restored) {
            set_supervisor_error("Core restarted; settings restore failed");
        } else {
            set_supervisor_status("Core restarted");
        }
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_AUTO_BOOT) {
        const bool active_is_target = persistent_settings.auto_boot &&
            supervisor.has_active &&
            strcmp(persistent_settings.selected_core_id,
                   supervisor.active.package.id) == 0;
        const bool enabled = !active_is_target;
        bool valid = true;
        settings_before_update = persistent_settings;
        if (enabled) {
            valid = supervisor.has_active &&
                mt_supervisor_settings_remember_core(
                    &persistent_settings, supervisor.active.package.id,
                    supervisor.active.package
                        .variants[supervisor.active.variant_index].id);
            if (valid && supervisor.active.package.core_id ==
                             MT_C64_CORE_ID) {
                valid = mt_supervisor_settings_remember_c64_input(
                    &persistent_settings, supervisor.active.package.id,
                    usb_hid_service_symbolic_keyboard(), stored_c64_joystick());
            }
        }
        if (!valid) {
            set_supervisor_error("Cannot enable auto-boot");
        } else {
            persistent_settings.auto_boot = enabled;
            if (save_persistent_settings()) {
                set_supervisor_status(enabled ? "Autoboot enabled" :
                                      "Autoboot disabled");
            } else {
                persistent_settings = settings_before_update;
                set_supervisor_warning("Autoboot change not saved");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_REBOOT_GOLDEN) {
        reboot_to_golden(false);
    } else if (action.kind == MT_MENU_ACTION_UPDATE_GOLDEN) {
        self_update_golden(false);
    } else if (action.kind == MT_MENU_ACTION_SET_BOOT_DEFAULT) {
        if (supervisor_menu.visible)
            show_handoff_progress("Checking startup firmware; please wait");
        const int status = mb_platform_set_default((uint32_t)action.file_index);
        refresh_multiboot_status();
        set_supervisor_outcome(status == MB_OK, "Startup firmware saved",
            "Startup choice failed; check golden and installed firmware");
    } else if (action.kind == MT_MENU_ACTION_SWITCH_FIRMWARE) {
        if (action.file_index == MB_PRODUCT_APPLETINI) switch_to_appletini();
    }
}

static const char *const iigs_activity_labels[MT_IIGS_DRIVE_COUNT] = {
    "HD1", "HD2", "3.5", "5.25", "HD3", "HD4", "HD5", "HD6", "HD7", "HD8"
};

/* Sample every drive service and the core's own lights into one badge. */
static void sample_drive_activity(uint64_t now)
{
    uint32_t reads, writes;
    if (shell_is_c64()) {
        const bool lights = (c64_content_service_expansion_caps() &
                             MT_C64_EXPANSION_CAPS_DRIVE_ACTIVITY) != 0U;
        const uint32_t status = lights ? shell_read(MT_REG_C64_DRIVE_STATUS) : 0U;
        c64_d64_service_activity(&reads, &writes);
        mt_activity_sample(&drive_activity, 0U, "D8", reads, writes, now);
        if (lights)
            mt_activity_lights(&drive_activity, 0U, "D8",
                (status & MT_C64_DRIVE_STATUS_LED) != 0U,
                (status & MT_C64_DRIVE_STATUS_MOTOR) != 0U, now);
        if (c64_iec_initialized) {
            c64_iec_service_activity(&reads, &writes);
            mt_activity_sample(&drive_activity, 1U, "D9", reads, writes, now);
        }
        if (c64_content_service_save_active()) {
            mt_activity_sample(&drive_activity, 2U, "EF", 0U,
                               c64_content_service_flash_writes(), now);
            if (lights)
                mt_activity_lights(&drive_activity, 2U, "EF",
                    (status & MT_C64_DRIVE_STATUS_CART_LED) != 0U, false, now);
        }
    } else if (supervisor.active.package.core_id == MT_AMIGA_CORE_ID) {
        static const char *const labels[]={"DF0","DF1","HD0","HD1"};
        for(unsigned drive=0U;drive<MT_AMIGA_DRIVE_COUNT;++drive) {
            amiga_content_service_activity(drive,&reads,&writes);
            mt_activity_sample(&drive_activity,drive,labels[drive],reads,writes,now);
        }
    } else if (supervisor.active.package.core_id == MT_IIGS_CORE_ID) {
        for (unsigned drive = 0U; drive < MT_IIGS_DRIVE_COUNT; ++drive) {
            iigs_content_service_activity(drive, &reads, &writes);
            mt_activity_sample(&drive_activity, drive, iigs_activity_labels[drive],
                               reads, writes, now);
        }
    }
}

static void service_drive_activity(const mt_ui_bus_t *bus)
{
    static uint64_t sampled;
    const uint64_t now = monotonic_ms(NULL);
    bool visible = false;
    if (supervisor_menu.visible || supervisor_menu_action_active ||
        now - sampled < 16U) return;
    sampled = now;
    if (!supervisor.has_active || activity_core != supervisor.active.package.core_id) {
        mt_activity_reset(&drive_activity);
        activity_core = supervisor.has_active ? supervisor.active.package.core_id : 0U;
    }
    if (supervisor.has_active && !persistent_settings.drive_activity_off) {
        sample_drive_activity(now);
        visible = mt_activity_render(&drive_activity, now, &activity_frame);
    } else {
        mt_activity_reset(&drive_activity);
        (void)mt_activity_render(&drive_activity, now, &activity_frame);
        visible = false;
    }
    if (mt_screenshot_service_render_feedback(&activity_frame)) visible = true;
    if (!visible && !activity_badge_shown) return;
    if (mt_ui_present_legacy_cached(&supervisor_presenter, bus, &activity_frame, visible) ==
        MT_UI_PRESENT_OK)
        activity_badge_shown = visible;
}

static void service_supervisor_menu(void)
{
    const mt_ui_bus_t bus = {
        NULL, supervisor_ui_read, supervisor_ui_write
    };
    mt_menu_view_t view;
    if (!supervisor_menu_ready) return;
    if (c64_debug.reu_host.reload_required) {
        supervisor_menu.visible = true;
        supervisor_menu_dirty = true;
    }
    if (pending_menu_action.kind != MT_MENU_ACTION_NONE) {
        const mt_menu_action_t action = pending_menu_action;
        pending_menu_action.kind = MT_MENU_ACTION_NONE;
        supervisor_menu_action_active = true;
        if (menu_action_needs_core_progress(action) && !c64_debug.reu_host.reload_required) {
            if (hide_menu_for_core_action()) {
                set_supervisor_status("Applying selection...");
                handle_menu_action(action);
            }
        } else {
            handle_menu_action(action);
        }
        supervisor_menu_action_active = false;
    }
    usb_hid_service_set_menu_capture(supervisor_menu.visible);
    controller_preview_capture();
    if (supervisor_menu.gamepad_edit) {
        static uint64_t preview_updated;
        const uint64_t now = monotonic_ms(NULL);
        if (now - preview_updated >= 100U) {
            preview_updated = now;
            supervisor_menu_dirty = true;
        }
        view = supervisor_menu_view();
        if (!usb_hid_service_controller_menu_chord(supervisor_menu.gamepad_slot) &&
            mt_menu_gamepad_update(&supervisor_menu, &view)) supervisor_menu_dirty = true;
        controller_preview_capture();
    }
    /* A closed menu need not replace a shown activity badge; the dense frame
     * is rendered again once the badge hides or the menu opens. */
    if (!supervisor_menu_dirty ||
        (activity_badge_shown && !supervisor_menu.visible &&
         !mt_ui_status_menu_shown(shell_read(MT_REG_UI_STATUS)))) {
        service_drive_activity(&bus);
        return;
    }
    view = supervisor_menu_view();
    mt_menu_render(&supervisor_menu, &view, &supervisor_frame);
    if (mt_ui_present_cached(&supervisor_presenter, &bus, &supervisor_frame,
                              supervisor_menu.visible) ==
        MT_UI_PRESENT_OK) {
        supervisor_menu_dirty = false;
        activity_badge_shown = false;
    }
}

static void initialize_supervisor_menu(void)
{
    const mt_supervisor_ops_t ops = {
        supervisor_quiesce, supervisor_load, supervisor_probe,
        supervisor_activate, supervisor_recover
    };
    const uint32_t current_core = shell_read(MT_REG_CORE_ID);
    mt_settings_result_t load_result;
    mt_supervisor_result_t switch_result;
    const bool boot_shell_valid = current_core == MT_SUPERVISOR_CORE_ID;
    bool standby_ready;
    mt_menu_view_t view;

    mt_ui_presenter_init(&supervisor_presenter);
    mt_core_catalog_sd_scan(&core_catalog);
    xil_printf("[supervisor] catalog cores=%u rejected=%u sd=%s backend=%u\r\n",
               (unsigned int)core_catalog.count,
               (unsigned int)core_catalog.rejected,
               mt_sd_status_name(core_catalog.sd.status),
               core_catalog.sd.backend_status);
    mt_supervisor_init(&supervisor, &ops, NULL);
    standby_ready = supervisor_enter_standby() == 0;
    xil_printf("[supervisor] boot shell core=%08x valid=%u standby=%u\r\n",
               (unsigned int)current_core, boot_shell_valid ? 1U : 0U,
               standby_ready ? 1U : 0U);

    load_result = mt_supervisor_settings_load(&persistent_settings);
    if (!mt_supervisor_turbo_controls_load(&persistent_settings))
        xil_printf("[settings] turbo bindings unavailable; left unassigned\r\n");
    settings_missing_on_boot = load_result.status == MT_SETTINGS_MISSING;
    settings_faulted = load_result.status != MT_SETTINGS_OK &&
                       load_result.status != MT_SETTINGS_MISSING;
    xil_printf("[settings] load=%s sd=%s backend=%u auto-boot=%u "
               "selected=%s\r\n",
               mt_settings_status_name(load_result.status),
               mt_sd_status_name(load_result.sd.status),
               load_result.sd.backend_status,
               persistent_settings.auto_boot ? 1U : 0U,
               persistent_settings.selected_core_id[0] == '\0' ?
                   "none" : persistent_settings.selected_core_id);
    if (!boot_shell_valid)
        set_supervisor_error("Invalid boot image; core held in reset");
    else if (!standby_ready)
        set_supervisor_error("Supervisor standby failed");
    else if (settings_faulted)
        set_supervisor_warning("Settings invalid; using safe defaults");
    else
        set_supervisor_status("Ready; press F12 to choose a core");

    usb_hid_service_set_controller_config_loader(supervisor_controller_config, NULL);

    if (mt_display_supported(&display_bus) && !apply_display_mode(persistent_settings.display_mode))
        set_supervisor_warning("Saved display resolution failed; using fallback");
    if (mt_display_scale_supported(&display_bus)) {
        const mt_core_settings_profile_t *const profile = supervisor.has_active ?
            mt_supervisor_settings_find(&persistent_settings, supervisor.active.package.id) : NULL;
        if (!apply_video_multiplier(profile != NULL ? profile->video_multiplier : 0U))
            set_supervisor_warning("Saved video size failed; using fallback");
    }

    if (persistent_settings.auto_boot) {
        if (!configured_auto_boot_selection(&startup_auto_selection)) {
            set_supervisor_warning("Autoboot target unavailable");
        } else {
            switch_result =
                mt_supervisor_switch(&supervisor, &startup_auto_selection);
            if (switch_result == MT_SUPERVISOR_INCOMPATIBLE_ABI) {
                set_supervisor_error("Autoboot needs an updated core package");
            } else if (switch_result != MT_SUPERVISOR_OK) {
                set_supervisor_error(
                    supervisor.recovered ?
                    "Autoboot failed; standby restored" :
                    "Autoboot and recovery failed");
            } else {
                set_supervisor_outcome(last_settings_restore_ok,
                                       "Autoboot complete",
                                       "Autoboot media restore failed");
            }
        }
    }
    view = supervisor_menu_view();
    mt_menu_init(&supervisor_menu, &view);
    {
        size_t core_index;
        for (core_index = 0U; core_index < core_catalog.count;
             ++core_index) {
            const mt_core_settings_profile_t *const profile =
                mt_supervisor_settings_find(
                    &persistent_settings,
                    core_catalog.cores[core_index].package.id);
            size_t variant_index;
            if (profile == NULL) continue;
            for (variant_index = 0U;
                 variant_index < core_catalog.cores[core_index]
                                      .package.variant_count;
                 ++variant_index) {
                if (strcmp(core_catalog.cores[core_index]
                               .package.variants[variant_index].id,
                           profile->variant_id) == 0) {
                    supervisor_menu.selected_variant[core_index] =
                        variant_index;
                    break;
                }
            }
        }
    }
    pending_menu_action.kind = MT_MENU_ACTION_NONE;
    supervisor_menu_ready = true;
    supervisor_menu_dirty = true;
    usb_hid_service_set_keyboard_filter(supervisor_keyboard_filter, NULL);
    usb_hid_service_set_controller_report_handler(supervisor_controller_report, NULL);
    usb_hid_service_set_controller_action_handler(supervisor_controller_action, NULL);
}

static void service_system_page(void)
{
    uint64_t now;
    uint8_t raw[2];
    if (!supervisor_menu_ready || !supervisor_menu.visible ||
        (supervisor_menu.page != MT_MENU_PAGE_SYSTEM && supervisor_menu.page != MT_MENU_PAGE_ABOUT) ||
        supervisor_menu.browser != MT_MENU_BROWSER_NONE || supervisor_menu.clock_edit) {
        last_system_refresh_ms = 0U;
        return;
    }
    now = monotonic_ms(NULL);
    /* Cached Golden identity and bounded slot probes refresh on entry.
     * Whole-image validation belongs to the explicit boot actions. */
    if (last_system_refresh_ms == 0U) refresh_multiboot_status();
    if (last_system_refresh_ms != 0U && now - last_system_refresh_ms < 1000U) return;
    last_system_refresh_ms = now;
    refresh_golden_update_file();
    board_temperature_valid = i2c_ready &&
        board_i2c_read(&i2c_instance, MT_TMP102_I2C_ADDRESS,
                       MT_TMP102_TEMPERATURE_REGISTER, raw, 2) == 0 &&
        mt_tmp102_decode_micro_c(raw, &board_temperature_micro_c) == 0;
    supervisor_menu_dirty = true;
}

static void recovery_console(void)
{
    xil_printf("recovery commands: :diag read status; :reboot return to golden updater; :selfupdate stage BOOT.BIN\r\n");
    for (;;) {
        const char input = inbyte();
        if (input == ':') {
            handle_extended_command(true);
        } else if (input != '\r' && input != '\n') {
            xil_printf("shell unavailable; use :diag, :reboot, or :selfupdate\r\n");
        }
    }
}

static void video_effect_write(uint32_t offset, uint32_t value)
{
    Xil_Out32(MT_SHELL_BASE_ADDRESS + offset, value);
    DATA_SYNC;
}

static uint32_t video_effect_milliseconds(void)
{
    return (uint32_t)monotonic_ms(NULL);
}

static void screenshot_progress(void)
{
    usb_hid_service_poll_deferred();
}

static void service_screenshots(void)
{
    if (!mt_screenshot_service_busy()) return;
    const bool active = supervisor.has_active;
    const uint32_t ui_status = shell_read(MT_REG_UI_STATUS);
    /* An unchanged menu may not poll its presenter again after submission.
     * Acknowledge that commit here before selecting the displayed bank. */
    if (supervisor_presenter.pending) {
        const uint32_t mask = MT_UI_STATUS_BANK | MT_UI_STATUS_VISIBLE | MT_UI_STATUS_DENSE;
        if ((ui_status & MT_UI_STATUS_READY) == 0U ||
            (ui_status & mask) != supervisor_presenter.pending_status) return;
        supervisor_presenter.pending = false;
    }
    const unsigned ui_bank = (ui_status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    mt_screenshot_service_poll(active ? supervisor.active.package.core_id : 0U,
        active ? supervisor.active.package.id : "supervisor",
        (uint8_t)(shell_read(MT_REG_DISPLAY_STATUS) & 15U),
        active && selection_supports_borders(&supervisor.active) ? shell_read(MT_REG_BORDER_CONFIG) : 0U,
        &supervisor_presenter.banks[ui_bank], supervisor_presenter.bank_valid[ui_bank] &&
        mt_ui_status_menu_shown(ui_status));
    if (!mt_screenshot_service_busy()) {
        const char *status = mt_screenshot_service_status();
        if (status != NULL) {
            xil_printf("[screenshot] %s\r\n", status);
            if (strncmp(status, "Screenshot saved:", 17U) == 0) set_supervisor_status(status);
            else set_supervisor_warning(status);
        }
    }
}

static void print_boot_ps_status(void)
{
    const uint32_t interrupts = Xil_In32(
        XPAR_XDEVCFG_0_BASEADDR + XDCFG_INT_STS_OFFSET);
    /* PS registers only: safe to report before attempting any PL access. */
    xil_printf("boot PS: fpga_rst=%08x lvl_shftr=%08x multiboot=%08x\r\n",
               (unsigned int)Xil_In32(MT_SLCR_FPGA_RESET),
               (unsigned int)Xil_In32(MT_SLCR_LEVEL_SHIFTER),
               (unsigned int)Xil_In32(MT_DEVCFG_MULTIBOOT));
    xil_printf("boot DevCfg: ctrl=%08x status=%08x irq=%08x pcfg_done_irq=%u\r\n",
               (unsigned int)Xil_In32(XPAR_XDEVCFG_0_BASEADDR + XDCFG_CTRL_OFFSET),
               (unsigned int)Xil_In32(XPAR_XDEVCFG_0_BASEADDR + XDCFG_STATUS_OFFSET),
               (unsigned int)interrupts,
               (interrupts & XDCFG_IXR_PCFG_DONE_MASK) != 0U ? 1U : 0U);
    xil_printf("boot clocks: fclk0_ctrl=%08x fclk2_ctrl=%08x io_pll_ctrl=%08x\r\n",
               (unsigned int)Xil_In32(MT_SLCR_FCLK0_CONTROL),
               (unsigned int)Xil_In32(MT_SLCR_FCLK2_CONTROL),
               (unsigned int)Xil_In32(MT_SLCR_IO_PLL_CONTROL));
    xil_printf("boot PLL: arm_ctrl=%08x ddr_ctrl=%08x\r\n",
               (unsigned int)Xil_In32(MT_SLCR_ARM_PLL_CONTROL),
               (unsigned int)Xil_In32(MT_SLCR_DDR_PLL_CONTROL));
}

static void print_read_only_diagnostics(const char *stage)
{
    static const uint32_t offsets[] = {
        MT_REG_MAGIC, MT_REG_ABI_VERSION, MT_REG_SHELL_ID,
        MT_REG_SHELL_BUILD_ID, MT_REG_CORE_ID, MT_REG_CONTROL, MT_REG_UI_STATUS,
        MT_REG_STATUS, MT_REG_ERROR, MT_REG_COMMAND_OPCODE,
        MT_REG_RESPONSE_STATUS, MT_REG_COUNT_BAD_WRITE, MT_REG_COUNT_BAD_READ
    };
    mt_video_worker_status_t worker;
    unsigned int i;
    xil_printf("diagnostic stage: %s\r\n", stage);
    print_boot_ps_status();
    /* Fixed status addresses only. No runtime polling or peripheral writes. */
    for (i = 0U; i < 4U; ++i) {
        const uint32_t value = Xil_In32(MT_SHELL_BASE_ADDRESS + MT_REG_MAGIC);
        xil_printf("magic repeat %u=%08x\r\n", i, (unsigned int)value);
    }
    for (i = 0U; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        const uint32_t value = Xil_In32(MT_SHELL_BASE_ADDRESS + offsets[i]);
        xil_printf("shell read +%08x=%08x\r\n",
                   (unsigned int)offsets[i], (unsigned int)value);
    }
    const uint32_t abi = shell_read(MT_REG_ABI_VERSION);
    if (shell_read(MT_REG_MAGIC) == MT_MAGIC && (abi >> 16U) == MT_ABI_MAJOR) {
        if ((abi & UINT32_C(0xffff)) >= 8U)
            xil_printf("stream status=%08x variant=%u\r\n",
                (unsigned int)shell_read(MT_REG_STREAM_STATUS),
                (unsigned int)shell_read(MT_REG_CORE_VARIANT_INDEX));
        if ((abi & UINT32_C(0xffff)) >= 8U) {
            /* Compare both ends of this read-only snapshot. MEDIA_DATA is
             * deliberately excluded because reading it consumes save bytes. */
            const uint32_t id_before = shell_read(MT_REG_MEDIA_REQUEST_ID);
            const uint32_t request_before = shell_read(MT_REG_MEDIA_REQUEST);
            const uint32_t lba = shell_read(MT_REG_MEDIA_REQUEST_LBA);
            const uint32_t blocks = shell_read(MT_REG_MEDIA_REQUEST_BLOCK_COUNT);
            const uint32_t generation = shell_read(MT_REG_MEDIA_REQUEST_GENERATION);
            const uint32_t state = shell_read(MT_REG_MEDIA_STATE);
            const uint32_t request_after = shell_read(MT_REG_MEDIA_REQUEST);
            const uint32_t id_after = shell_read(MT_REG_MEDIA_REQUEST_ID);
            xil_printf("media request=%08x/%08x id=%08x/%08x lba=%08x blocks=%08x generation=%08x state=%08x\r\n",
                (unsigned int)request_before, (unsigned int)request_after,
                (unsigned int)id_before, (unsigned int)id_after,
                (unsigned int)lba, (unsigned int)blocks,
                (unsigned int)generation, (unsigned int)state);
        }
        if ((abi & UINT32_C(0xffff)) >= MT_VIDEO_PHOSPHOR_MIN_ABI_MINOR) {
            /* Status/counter reads have no register side effects. In
             * particular, never read MEDIA_DATA here: that pops save bytes. */
            xil_printf("video effect status=%08x sequence=%08x frame control=%08x options=%08x status=%08x\r\n",
                (unsigned int)shell_read(MT_REG_VIDEO_EFFECT_STATUS),
                (unsigned int)shell_read(MT_REG_VIDEO_EFFECT_SEQUENCE),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_CONTROL),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_OPTIONS),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_STATUS));
            xil_printf("video geometry=%08x stride=%u captured=%u consumed=%u active=%08x processed=%u\r\n",
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_GEOMETRY),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_STRIDE),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_CAPTURE_PRODUCER),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_CAPTURE_CONSUMER),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_DISPLAY_ACTIVE),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_PROCESSED));
            xil_printf("video drops=%u underruns=%u axi=%u deadlines=%u engine_errors=%u cycles=%u stall=%u\r\n",
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_CAPTURE_DROPS),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_AXI_ERRORS),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_DEADLINES),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_ENGINE_ERRORS),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_ENGINE_CYCLES),
                (unsigned int)shell_read(MT_REG_VIDEO_FRAME_ENGINE_MAX_STALL));
        }
        if ((abi & UINT32_C(0xffff)) >= MT_DISPLAY_MIN_ABI_MINOR)
            xil_printf("display status=%08x modes=%08x\r\n",
                (unsigned int)shell_read(MT_REG_DISPLAY_STATUS),
                (unsigned int)shell_read(MT_REG_DISPLAY_MODES));
    }
    worker = mt_video_worker_status();
    xil_printf("CPU1 state=%u seq=%08x epoch=%08x bytes=%u result=%d\r\n",
               (unsigned int)worker.state, (unsigned int)worker.sequence,
               (unsigned int)worker.epoch, (unsigned int)worker.output_size,
               worker.result);
}

static uint32_t boot_probe_read32(void *context, uint32_t offset)
{
    const uint32_t value = Xil_In32((uintptr_t)context + offset);
    /* Log only reads the probe already performs, after each read completes. */
    xil_printf("shell probe read +%08x=%08x\r\n",
               (unsigned int)offset, (unsigned int)value);
    return value;
}

int main(void)
{
    mt_link_result_t link_status;
    int status;
    int usb_status;
    uint32_t ui_status;

    init_platform();
    xil_printf("\r\nMultitini One firmware " MT_FIRMWARE_VERSION " build=%08x\r\n",
               (unsigned int)MT_FIRMWARE_BUILD_ID);
    xil_printf(MB_MULTITINI_MARKER "\r\n");
    Xil_SetTlbAttributes(MT_SHELL_BASE_ADDRESS, DEVICE_MEMORY);
    /* CPU1 is still stopped. No cacheable alias may survive REU DMA writes.
     * Normal memory permits the unaligned first/last bytes of raw .reu loads. */
    for (uintptr_t section = MT_C64_REU_DDR_BASE;
         section < MT_C64_REU_DDR_BASE + MT_C64_REU_DDR_SIZE; section += UINT32_C(0x100000))
        Xil_SetTlbAttributes(section, NORM_NONCACHE);
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);
    print_read_only_diagnostics("before CPU1 release");
    {
        const mt_video_effect_service_ops_t effects_ops = {
            shell_read, video_effect_write, video_effect_milliseconds
        };
        mt_video_effect_service_init(&effects_ops);
        const mt_screenshot_service_ops_t screenshot_ops = {
            shell_read, video_effect_milliseconds, screenshot_progress
        };
        mt_screenshot_service_init(&screenshot_ops);
        xil_printf("CPU1 settings worker release: %s\r\n",
                   mt_video_worker_start() ? "ok" : "failed");
    }
    print_read_only_diagnostics("after CPU1 release");

    shell_link.read32 = boot_probe_read32;
    link_status = mt_core_link_probe(&shell_link);
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);
    xil_printf("shell probe: %s\r\n", mt_link_result_name(link_status));
    if (link_status != MT_LINK_OK) {
        xil_printf("probe expected: magic=%08x abi-major=%u\r\n",
                   (unsigned int)MT_MAGIC,
                   (unsigned int)MT_ABI_MAJOR);
        xil_printf("shell unavailable; diagnostic outputs remain in reset defaults\r\n");
        recovery_console();
    }
    ui_status = shell_read(MT_REG_UI_STATUS);
    if ((ui_status & MT_UI_STATUS_DENSE_LAYOUT) == 0U) {
        xil_printf("dense text probe: ui_status=%08x required=%08x\r\n",
                   (unsigned int)ui_status,
                   (unsigned int)MT_UI_STATUS_DENSE_LAYOUT);
        xil_printf("shell lacks dense text; install matching firmware/FPGA package\r\n");
        recovery_console();
    }
    status = initialize_i2c();
    i2c_ready = status == 0;
    if (i2c_ready) {
        const int rtc_status = service_fatfs_time();
        xil_printf("RTC-backed FatFs timestamps: %s (%d)\r\n",
                   rtc_status == 0 ? "ok" : "fixed-date fallback",
                   rtc_status);
        print_temperature();
        dac_bus.context = &i2c_instance;
        dac_bus.write = board_i2c_write;
        dac_bus.delay_us = board_delay_us;
        status = mt_ak4493_initialize(&dac_bus, false);
    }
    dac_ready = status == 0;
    xil_printf("DAC initialization (muted): %s (%d)\r\n",
               dac_ready ? "ok" : "failed", status);
    print_status();
    usb_hid_service_init(&shell_link);
    usb_hid_service_set_active_core(MT_SUPERVISOR_CORE_ID);
    usb_hid_service_set_core_ready(false);
    usb_status = usb_hid_service_start();
    xil_printf("USB1 keyboard/gamepad initialization: %s (%d)\r\n",
               usb_status == 0 ? "ok" : "failed", usb_status);
    usb_hid_service_dump_status();
    /* A progress callback must not re-enter shell_link through USB input. */
    c64_d64_service_init(&shell_link, NULL, NULL);
    c64_content_service_init(&shell_link, NULL, NULL, monotonic_ms, NULL);
    c64_services_initialized = true;
    initialize_c64_debug();
    initialize_supervisor_menu();
    service_supervisor_menu();
    /* The first screen is now available. Warm the verified Golden cache
     * here so entering System later performs only bounded status reads. */
    refresh_multiboot_status();
    print_help();

    for (;;) {
        uint8_t byte;
        char input;
        service_runtime();
        service_system_page();
        service_supervisor_menu();
        service_screenshots();
        mt_c64_debug_tick(&c64_debug);
        if (uart0_try_read_byte(&byte) == 0) continue;
        input = (char)byte;
        if (c64_debug.reu_host.reload_required && input != ':' && input != 'h' &&
            input != 'o' && input != 's' && input != '\r' && input != '\n') {
            xil_printf("REU cache did not clear; reload the C64 core\r\n");
            continue;
        }
        if (golden_update_worker_held && input != ':' && input != 'h' &&
            input != 'o' && input != 's' && input != 'u' &&
            input != '\r' && input != '\n') {
            xil_printf("CPU1 is safely held; retry :selfupdate or :reboot\r\n");
            continue;
        }
        const uint32_t control = shell_read(MT_REG_CONTROL);
        /* UART media/reset commands share the menu's temporary hide path.
         * Capture keyboard releases throughout the command and restore the
         * requested menu frame on the next outer service pass. */
        supervisor_menu_action_active = true;
        switch (input) {
        case 'h': print_help(); break;
        case 'o':
            supervisor_menu.visible = !supervisor_menu.visible;
            supervisor_menu_dirty = true;
            break;
        case 's':
            print_status();
            if (shell_is_c64()) c64_d64_service_dump_status();
            else if(shell_read(MT_REG_CORE_ID)==MT_AMIGA_CORE_ID)print_amiga_media_status();
            else if(shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID)print_iigs_media_status();
            break;
        case 'p': (void)command(MT_COMMAND_PING, 0U, 0U); break;
        case 'a': set_audio(false, false); break;
        case 't': set_audio(false, true); break;
        case 'm':
            set_audio(true,
                      (control & MT_CONTROL_AUDIO_TEST_ENABLE) != 0U);
            break;
        case 'v':
            Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL,
                      control ^ MT_CONTROL_VIDEO_TEST_ENABLE);
            print_status();
            break;
        case 'r':
            (void)set_core_reset_request(
                (control & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
            print_status();
            break;
        case 'u': usb_hid_service_dump_status(); break;
        case 'k': {
            if(shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID) {
                xil_printf("[IIgs] keyboard and controller settings are in the Input menu\r\n");break;
            }
            const bool symbolic =
                !usb_hid_service_symbolic_keyboard();
            if (usb_hid_service_set_symbolic_keyboard(symbolic) == 0) {
                xil_printf("USB keyboard map: %s\r\n",
                           symbolic ? "US symbolic" : "MiSTer positional");
                if (!remember_c64_input_settings())
                    xil_printf("[settings] keyboard map was not saved\r\n");
            } else {
                xil_printf("USB keyboard map change failed\r\n");
            }
            break;
        }
        case 'j': {
            if(shell_read(MT_REG_CORE_ID)==MT_IIGS_CORE_ID) {
                xil_printf("[IIgs] controller settings are in the Input menu\r\n");break;
            }
            if (shell_is_c64() && supervisor.has_active) {
                mt_c64_title_values_t value =
                    c64_requested_values(&supervisor.active, c64_content_service_crt_attached());
                value.joystick = value.joystick == 2 ? 1 : 2;
                (void)commit_c64_edit(&value, MT_TITLE_JOYSTICK, 0, 0);
            }
            break;
        }
        case ':': handle_extended_command(false); break;
        case '\r':
        case '\n':
            break;
        default:
            xil_printf("unknown command '%c'\r\n", input);
            print_help();
            break;
        }
        supervisor_menu_action_active = false;
    }
}
