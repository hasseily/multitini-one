/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"
#include "c64_content_service.h"
#include "core_catalog_sd.h"
#include "gameboy_content_service.h"
#include "genesis_content_service.h"
#include "vectrex_content_service.h"
#include "vectrex32_service.h"
#include "multitini_ak4493.h"
#include "multitini_c64_boot.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_native_load.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"
#include "multitini_display.h"
#include "multitini_core_frontend.h"
#include "multitini_core_link.h"
#include "multitini_fatfs_time.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_menu.h"
#include "multitini_media_browser.h"
#include "multitini_regs.h"
#include "multitini_supervisor.h"
#include "multitini_system.h"
#include "multitini_tmp102.h"
#include "multitini_ui.h"
#include "multitini_xmodem.h"
#include "multitini_version.h"
#include "../../golden/lib/golden_self_update.h"
#include "../../golden/lib/multiboot_platform.h"
#include "pl_loader.h"
#include "sd_storage.h"
#include "screenshot_service.h"
#include "supervisor_settings.h"
#include "text_document.h"
#include "usb_hid_service.h"
#include "multitini_video_effects_frontend.h"
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
static mt_ak4493_bus_t dac_bus;
static bool i2c_ready;
static bool dac_ready;
static bool c64_drive_ready;
static bool c64_core_reset_active;
static bool c64_services_initialized;
static bool genesis_service_initialized;
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
static mt_core_catalog_sd_t core_catalog;
static mt_supervisor_t supervisor;
static mt_menu_t supervisor_menu;
static mt_ui_frame_t supervisor_frame;
static mt_ui_presenter_t supervisor_presenter;
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

static void service_usb_input(void)
{
    const uint32_t status = shell_read(MT_REG_STATUS);
    const uint32_t control = shell_read(MT_REG_CONTROL);
    usb_hid_service_set_core_ready(
        (status & MT_STATUS_CORE_READY) != 0U &&
        (control & MT_CONTROL_CORE_RESET_REQUEST) == 0U);
    usb_hid_service_poll();
}

static void service_runtime(void)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    const bool c64_active = shell_is_c64();
    const bool gameboy_active =
        shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID;
    (void)service_fatfs_time();
    service_usb_input();
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
        }
        c64_core_reset_active = true;
        return;
    }
    if (c64_active && c64_services_initialized && c64_core_reset_active &&
        c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] drive8 remount after reset failed; retry with "
                   ":diskretry\r\n");
    }
    c64_core_reset_active = false;
    if (c64_active && c64_services_initialized) c64_d64_service_poll();
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

    if (!hide_menu_for_core_action()) return -1;
    if (asserted && !currently_asserted && c64_active &&
        c64_services_initialized &&
        c64_d64_service_prepare_reset() != 0) {
        xil_printf("[C64] reset refused; drive8 could not flush/eject safely\r\n");
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
    result = command(MT_COMMAND_CORE_RESET, asserted ? 1U : 0U, 0U);
    /* A synchronous assert/release can finish before the USB service observes
     * reset. Invalidate the cached Vectrex delivery state explicitly. */
    if (result == 0 && asserted && (shell_read(MT_REG_CORE_ID) == MT_VECTREX_CORE_ID ||
                                  shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID))
        usb_hid_service_set_core_ready(false);
    if (result != 0 && asserted && !currently_asserted &&
        c64_active && c64_services_initialized) {
        (void)c64_d64_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               gameboy_active && gameboy_service_initialized) {
        (void)gameboy_content_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID && genesis_service_initialized) {
        (void)genesis_content_service_resume_after_reset();
    } else if (result == 0 && asserted && !currently_asserted &&
               c64_active && c64_services_initialized) {
        /* Record the transition synchronously. Some reset users perform the
         * complete assert/load/release sequence without calling
         * service_runtime() while the reset level is high. */
        c64_d64_service_notify_core_reset();
        c64_core_reset_active = true;
    }
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
    return true;
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
    xil_printf("j swap joystick port, k toggle keyboard map\r\n");
    xil_printf("US map: Alt/GUI=C=, F9=up-arrow, F10=+, F11=RESTORE; ");
    xil_printf("F12 opens the Multitini menu\r\n");
    xil_printf("extended: :load FILE.prg|FILE.crt, :cartoff, ");
    xil_printf(":mount FILE.d64|FILE.g64, :eject, :dir, :disk, ");
    xil_printf(":diskretry, :saveretry\r\n");
    xil_printf("Genesis: :load FILE.md|FILE.gen|FILE.bin|FILE.smd, :cartoff\r\n");
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
        xil_printf(" validation=%s hardware=%u",
                   mt_content_validation_name(result->content.validation),
                   (unsigned int)result->content.hardware_type);
    } else if (result->status ==
               MT_C64_CONTENT_SERVICE_TRANSFER_FAILED) {
        xil_printf(" link=%s", mt_link_result_name(result->link));
    }
    xil_printf("\r\n");
}

static bool prepare_c64_content_change(void)
{
    if (shell_read(MT_REG_CORE_ID) != MT_C64_CORE_ID) {
        xil_printf("[C64] content commands require the C64 core\r\n");
        return false;
    }
    if (!hide_menu_for_core_action()) return false;
    if (c64_d64_service_prepare_reset() != 0) {
        xil_printf("[C64] content change refused; drive8 could not "
                   "flush/eject safely\r\n");
        return false;
    }
    return true;
}

static bool finish_c64_content_change(void)
{
    if (c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] content change completed, but drive8 remount "
                   "failed; retry with :diskretry\r\n");
        return false;
    }
    return true;
}

static void handle_c64_content_load(const char *file_name)
{
    mt_c64_content_service_result_t result;
    if (!prepare_c64_content_change()) return;
    result = c64_content_service_load(file_name);
    print_c64_content_result("load", file_name, &result);
    finish_c64_content_change();
    if (result.status == MT_C64_CONTENT_SERVICE_OK) {
        (void)remember_active_cartridge(
            mt_content_classify(file_name) == MT_CONTENT_CRT ?
            file_name : NULL);
    }
}

static void handle_c64_cart_detach(void)
{
    mt_c64_content_service_result_t result;
    if (!prepare_c64_content_change()) return;
    result = c64_content_service_detach_crt();
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
    const bool genesis_source = shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID;
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
    usb_hid_service_set_core_ready(false);
    mt_video_effect_service_suspend();
    worker_stopped = true;
    started = monotonic_ms(NULL);
    while (!mt_video_effect_service_quiescent() || !mt_video_worker_quiesce() ||
           (genesis_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
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
    const bool genesis_source = !recovery_only && shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID;
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
        usb_hid_service_set_core_ready(false);
        mt_video_effect_service_suspend();
        worker_stopped = true;
        started = monotonic_ms(NULL);
        while (!mt_video_effect_service_quiescent() || !mt_video_worker_quiesce() ||
               (genesis_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
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

static void handle_extended_command(bool recovery_only)
{
    char extended[MT_EXTENDED_COMMAND_CAPACITY];
    char *argument;
    char *argument_end;

    if (!read_extended_command(extended, sizeof(extended), recovery_only)) {
        if (!recovery_only && strncmp(extended, "c64rx", 5U) == 0) {
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
    } else if (!recovery_only &&
               strncmp(extended, "c64rx", 5U) == 0 &&
               (extended[5] == '\0' || extended[5] == ' ' ||
                extended[5] == '\t')) {
        handle_c64_rx_command(extended);
    } else if (!recovery_only && strcmp(extended, "dir") == 0) {
        c64_d64_service_list();
    } else if (!recovery_only && strcmp(extended, "disk") == 0) {
        c64_d64_service_dump_status();
    } else if (!recovery_only && strcmp(extended, "saveretry") == 0) {
        if (!hide_menu_for_core_action()) return;
        const int status = shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID ?
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
        if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
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
        else if (shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID) {
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
            if (c64_d64_service_mount(argument) == 0)
                (void)remember_active_drive8(argument);
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
           mt_supervisor_settings_remember_c64_input(
               &persistent_settings, core_id,
               usb_hid_service_symbolic_keyboard(),
               usb_hid_service_joystick_port()) &&
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
        !mt_supervisor_settings_remember_c64_input(
            &persistent_settings, selection->package.id,
            usb_hid_service_symbolic_keyboard(),
            usb_hid_service_joystick_port())) {
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
        set_c64_cpu_turbo(selection->package.abi_minor >= 8U &&
                          profile != NULL && profile->c64_cpu_turbo);
        if (profile != NULL) {
            if (usb_hid_service_set_symbolic_keyboard(
                    profile->symbolic_keyboard) != 0 ||
                usb_hid_service_set_joystick_port(
                    profile->joystick_port) != 0) {
                xil_printf("[settings] C64 input restore failed\r\n");
                ok = false;
            }
            if (profile->cartridge_name[0] != '\0') {
                mt_c64_content_service_result_t content;
                if (!prepare_c64_content_change()) {
                    ok = false;
                } else {
                    content = c64_content_service_load(
                        profile->cartridge_name);
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
        state.fast_roms_active = c64_fast_roms_active;
        state.fast_roms_fallback = c64_fast_roms_fallback;
        state.cpu_turbo_enabled = (shell_read(MT_REG_CONTROL) &
                                  MT_CONTROL_C64_CPU_TURBO_ENABLE) != 0U;
        state.symbolic_keyboard = usb_hid_service_symbolic_keyboard();
        state.joystick_port = usb_hid_service_joystick_port();
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
    view.files = supervisor_menu_files;
    view.file_count = supervisor_menu_file_count;
    if ((supervisor_menu.browser == MT_MENU_BROWSER_DOCUMENTS || (supervisor.has_active &&
        supervisor.active.package.core_id == MT_VECTREX_CORE_ID)) &&
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
    const bool folders = browser == MT_MENU_BROWSER_DOCUMENTS || (supervisor.has_active &&
        supervisor.active.package.core_id == MT_VECTREX_CORE_ID);
    if (!mt_media_browser_visible_component(name)) return 0;
    accepted = is_directory ? folders :
        document || (supervisor.has_active && browser != MT_MENU_BROWSER_DOCUMENTS &&
            mt_core_frontend_accepts_content(supervisor.active.package.core_id, browser, type));
    if (!accepted) return 0;
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

static bool collect_d64_prg(void *context,
                            const mt_c64_d64_prg_entry_t *entry)
{
    mt_c64_d64_result_t result;
    mt_c64_content_image_t image;
    size_t extracted_size = 0U;
    size_t name_size;
    const size_t index = supervisor_d64_prg_count;
    (void)context;

    if (entry == NULL || index >= MT_MENU_MAX_FILES) return false;
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
    if (supervisor.active.package.core_id == MT_VECTREX_CORE_ID) {
        mt_media_browser_t directory;
        if (!mt_media_browser_init(&directory, supervisor.active.package.content_root)) {
            set_supervisor_error("Invalid Vectrex media folder");
            return;
        }
        list_vectrex_browser(browser, &directory);
        return;
    }
    supervisor_menu_file_count = 0U;
    supervisor_d64_prg_count = 0U;
    if (browser == MT_MENU_BROWSER_DISK_PROGRAM) {
        size_t directory_count = 0U;
        const mt_c64_d64_result_t result = c64_d64_service_list_prgs(
            collect_d64_prg, NULL, &directory_count);
        if (result != MT_C64_D64_OK) {
            xil_printf("[C64] D64 directory: %s\r\n",
                       mt_c64_d64_result_name(result));
            set_supervisor_error("Could not read the D64 directory");
            return;
        }
        sort_menu_files();
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

static int supervisor_quiesce(void *context)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    (void)context;
    if (!mt_screenshot_service_cancel()) return -1;
    if (set_core_reset_request(true) != 0) return -1;
    set_audio(true, (control & MT_CONTROL_AUDIO_TEST_ENABLE) != 0U);
    usb_hid_service_set_core_ready(false);
    return 0;
}

static int supervisor_load(void *context,
                           const mt_core_selection_t *selection)
{
    mt_pl_load_result_t load;
    uint64_t drain_started;
    const bool genesis_source = shell_read(MT_REG_CORE_ID) == MT_GENESIS_CORE_ID;
    mt_video_effect_service_suspend();
    drain_started = monotonic_ms(NULL);
    while (!mt_video_effect_service_quiescent() ||
           (genesis_source && (shell_read(MT_REG_VIDEO_FRAME_STATUS) &
                               MT_VIDEO_FRAME_STATUS_QUIESCENT) == 0U)) {
        /* All active HP masters must drain before PCAP resets the fabric.
         * With core reset asserted this also includes Genesis source HP2.
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

static int supervisor_activate(void *context,
                               const mt_core_selection_t *selection)
{
    const mt_core_settings_profile_t *profile;
    bool request_fast_roms;
    (void)context;
    usb_hid_service_set_active_core(selection->package.core_id);
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
    if (selection->package.core_id != MT_C64_CORE_ID) {
        xil_printf("[supervisor] no activation service for core=%08x\r\n",
                   (unsigned int)selection->package.core_id);
        return -1;
    }
    c64_core_reset_active = true;
    c64_content_service_init(&shell_link, NULL, NULL, monotonic_ms, NULL);
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
    usb_hid_service_set_core_ready(
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U);
    if (dac_ready) set_audio(false, false);
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
    genesis_service_initialized = false;
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

static void supervisor_controller_action(void *context, unsigned action)
{
    (void)context;
    if (action == MT_CONTROL_SCREENSHOT || action == MT_CONTROL_SCREENSHOT_OUTPUT)
        (void)mt_screenshot_service_request(action == MT_CONTROL_SCREENSHOT_OUTPUT);
}

static bool supervisor_controller_config(void *context, uint32_t core_id, uint8_t slot,
    const mt_controller_device_t *device, mt_controller_config_t *config)
{
    static const uint32_t cores[] = {MT_C64_CORE_ID, MT_GBC_CORE_ID, MT_GENESIS_CORE_ID, MT_VECTREX_CORE_ID};
    (void)context;
    if (slot >= MT_GAMEPAD_SLOTS || device == NULL || config == NULL) return false;
    const mt_controller_settings_profile_t *saved = mt_supervisor_settings_find_controller(
        &persistent_settings, core_id, &device->identity);
    if (saved != NULL) { *config = saved->config; return true; }
    if (core_id != MT_SUPERVISOR_CORE_ID &&
        (persistent_settings.legacy_gamepad_pending & (1U << slot)) != 0U) {
        /* Seed missing profiles for each supported core before consuming the
         * old global slot map. No I/O or existing-profile replacement here. */
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
    if (!supervisor_menu.visible && !supervisor_presenter.pending &&
        (shell_read(MT_REG_UI_STATUS) & MT_UI_STATUS_VISIBLE) == 0U)
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
            (shell_read(MT_REG_UI_STATUS) & MT_UI_STATUS_VISIBLE) == 0U) {
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

static void launch_disk_program_native(uint32_t cookie)
{
    static const mt_c64_native_load_io_t io = {
        native_load_key, native_load_delay
    };
    const mt_c64_d64_prg_entry_t *const entry = &supervisor_d64_prgs[cookie];
    mt_c64_content_service_result_t result;
    mt_c64_native_load_status_t typed;
    bool remounted;
    bool settings_saved;
    if (!mt_c64_native_load_name_supported(entry->raw_name) ||
        !native_load_name_unique(cookie)) {
        set_supervisor_error("Name cannot be selected exactly by a drive LOAD");
        return;
    }
    if (!prepare_c64_content_change()) {
        set_supervisor_error("Disk load could not start");
        return;
    }
    result = c64_content_service_prepare_disk_load();
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
    typed = mt_c64_native_load(entry->raw_name, &io, NULL);
    xil_printf("[C64] native LOAD %s: %s\r\n", entry->display_name,
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

static void handle_menu_action(mt_menu_action_t action)
{
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
        result = mt_supervisor_switch(&supervisor, &selection);
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
    } else if (action.kind == MT_MENU_ACTION_BROWSE_DISK_PROGRAM) {
        open_menu_browser(MT_MENU_BROWSER_DISK_PROGRAM);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_CARTRIDGE) {
        open_menu_browser(MT_MENU_BROWSER_CARTRIDGE);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_PROGRAM) {
        open_menu_browser(MT_MENU_BROWSER_PROGRAM);
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
        if (save_persistent_settings()) {
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
        if ((supervisor_menu.browser != MT_MENU_BROWSER_DOCUMENTS && (!supervisor.has_active ||
            supervisor.active.package.core_id != MT_VECTREX_CORE_ID)) ||
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
        if (action.file_index >= supervisor_menu_file_count ||
            !c64_drive_ready) {
            set_supervisor_warning("Drive 8 is unavailable");
            return;
        }
        status = c64_d64_service_mount(
            supervisor_menu_files[action.file_index].name);
        if (status == 0) mt_menu_close_browser(&supervisor_menu);
        if (status != 0)
            set_supervisor_error("Drive 8 mount failed");
        else if (!remember_active_drive8(
                     supervisor_menu_files[action.file_index].name))
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
        if (action.file_index >= supervisor_menu_file_count) {
            set_supervisor_error("Mount & Run could not start");
            return;
        }
        cookie = supervisor_menu_files[action.file_index].cookie;
        if (cookie >= supervisor_d64_prg_count) {
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
                launch_disk_program_native(cookie);
                return;
            }
        }
        if (drive_load) {
            const uint8_t *const name = supervisor_d64_prgs[cookie].raw_name;
            const size_t program_size = extracted_size;
            if (!mt_c64_native_load_name_supported(name) ||
                !native_load_name_unique(cookie)) {
                set_supervisor_error("Name cannot be selected exactly by a drive LOAD");
                return;
            }
            /* An in-program LOAD makes BASIC start the loaded program after
             * the real disk transfer succeeds. Preserve KERNAL/drive state
             * without guessing a delay or queuing RUN after a failed LOAD. */
            if (mt_c64_native_load_build_basic_launcher(
                    name, supervisor_disk_prg_buffer,
                    sizeof(supervisor_disk_prg_buffer), &extracted_size) !=
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
        result = c64_content_service_stage_prg_image(
            supervisor_disk_prg_buffer, extracted_size);
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
        if (shell_read(MT_REG_CORE_ID) == MT_VECTREX_CORE_ID) {
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
            if (!prepare_c64_content_change()) {
                set_supervisor_error("Content load could not start");
                return;
            }
            result = c64_content_service_load(
                supervisor_menu_files[action.file_index].name);
            print_c64_content_result(
                "load", supervisor_menu_files[action.file_index].name,
                &result);
            remounted = finish_c64_content_change();
            if (result.status == MT_C64_CONTENT_SERVICE_OK)
                mt_menu_close_browser(&supervisor_menu);
            if (result.status != MT_C64_CONTENT_SERVICE_OK) {
                set_supervisor_error(remounted ? "Content load failed" :
                    "Content load and drive remount failed");
            } else if (!remember_active_cartridge(
                           mt_content_classify(
                               supervisor_menu_files[action.file_index].name) ==
                                   MT_CONTENT_CRT ?
                           supervisor_menu_files[action.file_index].name :
                           NULL)) {
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
            result = c64_content_service_detach_crt();
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
        const uint8_t port = usb_hid_service_joystick_port() == 2U ? 1U : 2U;
        if (usb_hid_service_set_joystick_port(port) != 0)
            set_supervisor_error("Gamepad port change failed");
        else if (!remember_c64_input_settings())
            set_supervisor_error("Gamepad changed; settings write failed");
        else
            set_supervisor_status("Gamepad port changed");
    } else if (action.kind == MT_MENU_ACTION_TOGGLE_CORE_OPTION) {
        if (supervisor.has_active && supervisor.active.package.core_id == MT_GENESIS_CORE_ID) {
            change_genesis_controller(action.file_index);
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
        const int asserted = set_core_reset_request(true);
        int released = -1;
        bool restored = true;
        if (asserted == 0) usleep(1000U);
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
                    usb_hid_service_symbolic_keyboard(),
                    usb_hid_service_joystick_port());
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

static void service_supervisor_menu(void)
{
    const mt_ui_bus_t bus = {
        NULL, supervisor_ui_read, supervisor_ui_write
    };
    mt_menu_view_t view;
    if (!supervisor_menu_ready) return;
    if (pending_menu_action.kind != MT_MENU_ACTION_NONE) {
        const mt_menu_action_t action = pending_menu_action;
        pending_menu_action.kind = MT_MENU_ACTION_NONE;
        supervisor_menu_action_active = true;
        if (menu_action_needs_core_progress(action)) {
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
    if (!supervisor_menu_dirty) return;
    view = supervisor_menu_view();
    mt_menu_render(&supervisor_menu, &view, &supervisor_frame);
    if (mt_ui_present_cached(&supervisor_presenter, &bus, &supervisor_frame,
                              supervisor_menu.visible) ==
        MT_UI_PRESENT_OK) {
        supervisor_menu_dirty = false;
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
        supervisor_menu.page != MT_MENU_PAGE_SYSTEM ||
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
        (ui_status & MT_UI_STATUS_VISIBLE) != 0U);
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
        if (uart0_try_read_byte(&byte) == 0) continue;
        input = (char)byte;
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
            const uint8_t port =
                usb_hid_service_joystick_port() == 2U ? 1U : 2U;
            if (usb_hid_service_set_joystick_port(port) == 0) {
                xil_printf("USB gamepad mapped to C64 joystick port %u\r\n",
                           (unsigned int)port);
                if (!remember_c64_input_settings())
                    xil_printf("[settings] gamepad port was not saved\r\n");
            } else {
                xil_printf("USB gamepad port swap failed\r\n");
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
