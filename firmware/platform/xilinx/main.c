/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"
#include "c64_content_service.h"
#include "core_catalog_sd.h"
#include "gameboy_content_service.h"
#include "multitini_ak4493.h"
#include "multitini_c64_boot.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"
#include "multitini_core_link.h"
#include "multitini_fatfs_time.h"
#include "multitini_gameboy_interface.h"
#include "multitini_menu.h"
#include "multitini_regs.h"
#include "multitini_supervisor.h"
#include "multitini_tmp102.h"
#include "multitini_ui.h"
#include "multitini_xmodem.h"
#include "pl_loader.h"
#include "sd_storage.h"
#include "supervisor_settings.h"
#include "usb_hid_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "sleep.h"
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
#define MT_FIRMWARE_BUILD_ID UINT32_C(0x20260910)
#define MT_I2C_CLOCK_HZ UINT32_C(100000)
#define MT_I2C_BUSY_POLLS UINT32_C(1000000)
#define MT_UART_DRAIN_POLLS UINT32_C(10000000)
#define MT_UART_BYTE_TIMEOUT_MS UINT32_C(1000)
#define MT_UART_XMODEM_RETRIES UINT32_C(20)
#define MT_CORE_READY_POLLS UINT32_C(1000000)
#define MT_LINK_COMMAND_POLLS UINT32_C(100000)
#define MT_EXTENDED_COMMAND_CAPACITY 272U
#define MT_EXTENDED_ARGUMENT_CAPACITY 4
#define MT_MENU_MAX_FILES 64U

/* Zynq-7000 UG585 multiboot and PS software-reset registers. */
#define MT_DEVCFG_MULTIBOOT UINT32_C(0xF800702C)
#define MT_DEVCFG_MULTIBOOT_MASK UINT32_C(0x00001FFF)
#define MT_SLCR_LOCK UINT32_C(0xF8000004)
#define MT_SLCR_UNLOCK UINT32_C(0xF8000008)
#define MT_SLCR_LOCK_KEY UINT32_C(0x0000767B)
#define MT_SLCR_UNLOCK_KEY UINT32_C(0x0000DF0D)
#define MT_PS_RESET_CONTROL UINT32_C(0xF8000200)
#define MT_PS_SOFTWARE_RESET UINT32_C(0x00000001)

static XIicPs i2c_instance;
static mt_core_link_t shell_link;
static mt_ak4493_bus_t dac_bus;
static bool i2c_ready;
static bool dac_ready;
static bool c64_drive_ready;
static bool c64_core_reset_active;
static bool c64_services_initialized;
static bool gameboy_service_initialized;
static bool gameboy_save_dirty_displayed;
static bool gameboy_save_fault_displayed;
static uint8_t c64_rom_bundle[MT_C64_SYSTEM_ROM_BUNDLE_SIZE];
static uint8_t c64_drive_rom[MT_SIZE_C64_1541_ROM];
static XTime last_fatfs_time_poll;
static mt_core_catalog_sd_t core_catalog;
static mt_supervisor_t supervisor;
static mt_menu_t supervisor_menu;
static mt_ui_frame_t supervisor_frame;
static mt_menu_action_t pending_menu_action;
static bool supervisor_menu_ready;
static bool supervisor_menu_dirty;
static uint8_t supervisor_menu_keys[MT_C64_HID_SLOT_COUNT]
                                   [MT_C64_HID_USAGE_BYTES];
static char supervisor_status_text[64] = "Ready";
static mt_menu_file_t supervisor_menu_files[MT_MENU_MAX_FILES];
static size_t supervisor_menu_file_count;
static mt_supervisor_settings_t persistent_settings;
static mt_supervisor_settings_t settings_before_update;
static mt_core_selection_t startup_auto_selection;
static bool settings_faulted;
static bool settings_missing_on_boot;
static bool last_settings_restore_ok = true;

static bool save_persistent_settings(void);
static bool remember_active_drive8(const char *file_name);
static bool remember_active_cartridge(const char *file_name);
static bool remember_c64_input_settings(void);

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
    uint8_t registers[MT_PCF8563_TIME_REGISTER_COUNT];
    if (board_i2c_read(&i2c_instance, MT_PCF8563_I2C_ADDRESS,
                       MT_PCF8563_SECONDS_REGISTER, registers,
                       (int)sizeof(registers)) != 0) {
        return -1;
    }
    return mt_fatfs_time_update_from_pcf8563(registers) ? 0 : -2;
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
    result = command(MT_COMMAND_CORE_RESET, asserted ? 1U : 0U, 0U);
    if (result != 0 && asserted && !currently_asserted &&
        c64_active && c64_services_initialized) {
        (void)c64_d64_service_resume_after_reset();
    } else if (result != 0 && asserted && !currently_asserted &&
               gameboy_active && gameboy_service_initialized) {
        (void)gameboy_content_service_resume_after_reset();
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

static bool load_c64_sd_drive_rom(void)
{
    const mt_sd_result_t read_result = mt_sd_storage_read_file_exact(
        MT_C64_SD_1541_PATH, c64_drive_rom, sizeof(c64_drive_rom));
    mt_content_validation_t validation;
    mt_link_result_t load_result;

    if (read_result.status != MT_SD_OK) {
        xil_printf("[C64] drive8 ROM unavailable path=%s read=%s "
                   "backend=%u size=%u\r\n",
                   MT_C64_SD_1541_PATH,
                   mt_sd_status_name(read_result.status),
                   read_result.backend_status,
                   (unsigned int)read_result.size);
        return false;
    }
    validation = mt_c64_rom_validate(
        MT_C64_ROM_DRIVE_1541, c64_drive_rom, sizeof(c64_drive_rom));
    if (validation != MT_CONTENT_VALID) {
        xil_printf("[C64] drive8 ROM rejected path=%s validation=%s\r\n",
                   MT_C64_SD_1541_PATH,
                   mt_content_validation_name(validation));
        return false;
    }

    load_result = mt_core_link_stream(
        &shell_link, MT_LOAD_C64_1541_ROM, c64_drive_rom,
        sizeof(c64_drive_rom), MT_LINK_COMMAND_POLLS, NULL, NULL);
    if (load_result != MT_LINK_OK) {
        xil_printf("[C64] drive8 ROM stream failed: %s\r\n",
                   mt_link_result_name(load_result));
        return false;
    }
    xil_printf("[C64] drive8 ROM loaded path=%s crc32=%08x\r\n",
               MT_C64_SD_1541_PATH,
               (unsigned int)mt_xmodem_crc32_ieee(
                   c64_drive_rom, sizeof(c64_drive_rom)));
    return true;
}

static bool attempt_c64_sd_boot(void)
{
    mt_sd_result_t mount_result;
    mt_c64_rom_files_result_t files_result;
    mt_c64_system_roms_t roms;
    mt_link_result_t load_result;
    uint32_t core_status = 0U;
    uint32_t actual_crc32;

    if (set_core_reset_request(true) != 0) {
        xil_printf("[C64] error: cannot enter reset for SD ROM load\r\n");
        return false;
    }

    mount_result = mt_sd_storage_mount();
    xil_printf("[SD] mount status=%s backend=%u\r\n",
               mt_sd_status_name(mount_result.status),
               mount_result.backend_status);
    if (mount_result.status != MT_SD_OK) {
        print_c64_sd_fallback();
        return false;
    }

    files_result = mt_c64_rom_files_load_system(
        read_c64_sd_rom, NULL, c64_rom_bundle, sizeof(c64_rom_bundle), &roms);
    if (files_result.status != MT_C64_ROM_FILES_OK) {
        const mt_sd_status_t read_status =
            (mt_sd_status_t)files_result.read.status;
        xil_printf("[C64] SD ROM load failed status=%s path=%s "
                   "read=%s backend=%u size=%u validation=%s\r\n",
                   mt_c64_rom_files_status_name(files_result.status),
                   files_result.path == NULL ? "(none)" : files_result.path,
                   mt_sd_status_name(read_status),
                   files_result.read.backend_status,
                   (unsigned int)files_result.read.size,
                   mt_content_validation_name(files_result.validation));
        print_c64_sd_fallback();
        return false;
    }

    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    c64_drive_ready = load_c64_sd_drive_rom();
    if (!c64_drive_ready) {
        /* Keep system-ROM boot independent of the optional drive ROM. */
        Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    }
    load_result = mt_c64_load_system_roms(
        &shell_link, &roms, MT_LINK_COMMAND_POLLS, NULL, NULL);
    if (load_result != MT_LINK_OK) {
        leave_core_in_reset();
        xil_printf("[C64] error: SD ROM stream %s\r\n",
                   mt_link_result_name(load_result));
        print_c64_sd_fallback();
        return false;
    }
    if (!wait_for_c64_core_ready(&core_status)) {
        leave_core_in_reset();
        xil_printf("[C64] error: core-ready timeout status=%08x\r\n",
                   (unsigned int)core_status);
        print_c64_sd_fallback();
        return false;
    }

    set_video_test(false);
    actual_crc32 = mt_xmodem_crc32_ieee(c64_rom_bundle,
                                         sizeof(c64_rom_bundle));
    xil_printf("[C64] SD boot ok root=%s crc32=%08x status=%08x\r\n",
               MT_C64_SD_ROM_ROOT, (unsigned int)actual_crc32,
               (unsigned int)core_status);
    return true;
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
    xil_printf(":diskretry\r\n");
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
    if (c64_d64_service_prepare_reset() != 0) {
        xil_printf("[C64] content change refused; drive8 could not "
                   "flush/eject safely\r\n");
        return false;
    }
    return true;
}

static void finish_c64_content_change(void)
{
    if (c64_d64_service_resume_after_reset() != 0) {
        xil_printf("[C64] content change completed, but drive8 remount "
                   "failed; retry with :diskretry\r\n");
    }
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

static bool read_extended_command(char *buffer, size_t capacity)
{
    size_t length = 0U;
    bool overflow = false;

    for (;;) {
        uint8_t byte;
        char input;
        while (uart0_try_read_byte(&byte) == 0) service_runtime();
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

static void reboot_to_golden(void)
{
    int usb_stop_result;
    mt_sd_result_t unmount_result;

    if (set_core_reset_request(true) != 0) {
        xil_printf("media flush or core reset failed; reboot cancelled\r\n");
        return;
    }
    xil_printf("rebooting to golden at QSPI offset 0x00000000\r\n");
    if (!wait_uart0_transmit_idle()) {
        xil_printf("UART did not drain; reboot cancelled\r\n");
        (void)set_core_reset_request(false);
        return;
    }

    Xil_Out32(MT_DEVCFG_MULTIBOOT, 0U);
    DATA_SYNC;
    if ((Xil_In32(MT_DEVCFG_MULTIBOOT) & MT_DEVCFG_MULTIBOOT_MASK) != 0U) {
        xil_printf("multiboot readback failed; reboot cancelled\r\n");
        (void)set_core_reset_request(false);
        return;
    }

    usb_stop_result = usb_hid_service_stop();
    if (usb_stop_result != 0) {
        xil_printf("USB1 VBUS shutdown failed (%d); reboot cancelled\r\n",
                   usb_stop_result);
        (void)set_core_reset_request(false);
        return;
    }
    unmount_result = mt_sd_storage_unmount();
    if (unmount_result.status != MT_SD_OK) {
        xil_printf("SD unmount warning: %s backend=%u\r\n",
                   mt_sd_status_name(unmount_result.status),
                   unmount_result.backend_status);
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

static void handle_extended_command(bool recovery_only)
{
    char extended[MT_EXTENDED_COMMAND_CAPACITY];
    char *argument;
    char *argument_end;

    if (!read_extended_command(extended, sizeof(extended))) {
        if (!recovery_only && strncmp(extended, "c64rx", 5U) == 0) {
            leave_core_in_reset();
        }
        xil_printf("extended command is too long\r\n");
    } else if (strcmp(extended, "reboot") == 0) {
        reboot_to_golden();
    } else if (!recovery_only &&
               strncmp(extended, "c64rx", 5U) == 0 &&
               (extended[5] == '\0' || extended[5] == ' ' ||
                extended[5] == '\t')) {
        handle_c64_rx_command(extended);
    } else if (!recovery_only && strcmp(extended, "dir") == 0) {
        c64_d64_service_list();
    } else if (!recovery_only && strcmp(extended, "disk") == 0) {
        c64_d64_service_dump_status();
    } else if (!recovery_only && strcmp(extended, "diskretry") == 0) {
        if (c64_d64_service_retry() == 0) {
            if (c64_d64_service_resume_after_reset() != 0)
                xil_printf("[C64] drive8 retry pending\r\n");
            else
                xil_printf("[C64] drive8 retry accepted\r\n");
        } else {
            xil_printf("[C64] drive8 retry failed\r\n");
        }
    } else if (!recovery_only && strcmp(extended, "eject") == 0) {
        if (c64_d64_service_eject() == 0)
            (void)remember_active_drive8(NULL);
    } else if (!recovery_only && strcmp(extended, "cartoff") == 0) {
        handle_c64_cart_detach();
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
        else
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
            if (c64_d64_service_mount(argument) == 0)
                (void)remember_active_drive8(argument);
        }
    } else {
        xil_printf("unknown extended command ':%s'\r\n", extended);
    }

    if (recovery_only) {
        xil_printf("recovery command: :reboot return to golden updater\r\n");
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
    supervisor_menu_dirty = true;
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
    if (selection->package.core_id == MT_C64_CORE_ID) {
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
                const bool already_mounted = c64_d64_service_mounted() &&
                    strcmp(c64_d64_service_mounted_name(),
                           profile->drive8_name) == 0;
                if (!c64_drive_ready ||
                    (!already_mounted &&
                     c64_d64_service_mount(profile->drive8_name) != 0)) {
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
    return true;
}

static mt_menu_view_t supervisor_menu_view(void)
{
    mt_menu_view_t view;
    memset(&view, 0, sizeof(view));
    view.cores = core_catalog.cores;
    view.core_count = core_catalog.count;
    if (supervisor.has_active) {
        const mt_core_variant_t *const variant =
            &supervisor.active.package.variants[supervisor.active.variant_index];
        view.active_core_id = supervisor.active.package.id;
        view.active_variant_id = variant->id;
    }
    if (supervisor.has_active &&
        supervisor.active.package.core_id == MT_C64_CORE_ID &&
        c64_services_initialized) {
        view.drive8_supported = true;
        view.cartridge_supported = true;
        view.program_supported = true;
        view.c64_input_settings = true;
        view.drive8_mounted = c64_d64_service_mounted();
        view.drive8_name = c64_d64_service_mounted_name();
        view.cartridge_attached = c64_content_service_crt_attached();
        view.content_name = c64_content_service_crt_name();
    } else if (supervisor.has_active &&
               supervisor.active.package.core_id == MT_GBC_CORE_ID &&
               gameboy_service_initialized) {
        view.cartridge_supported = true;
        view.cartridge_attached =
            gameboy_content_service_cartridge_attached();
        view.content_name = gameboy_content_service_cartridge_name();
        view.save_supported = supervisor.active.package.abi_minor >= 7U;
        view.save_active = gameboy_content_service_save_active();
        view.save_dirty = gameboy_content_service_save_dirty();
        view.save_faulted = gameboy_content_service_save_faulted();
    }
    view.status_text = supervisor_status_text;
    view.files = supervisor_menu_files;
    view.file_count = supervisor_menu_file_count;
    view.video_test_enabled =
        (shell_read(MT_REG_CONTROL) & MT_CONTROL_VIDEO_TEST_ENABLE) != 0U;
    view.settings_auto_boot = persistent_settings.auto_boot &&
        supervisor.has_active &&
        strcmp(persistent_settings.selected_core_id,
               supervisor.active.package.id) == 0;
    view.settings_faulted = settings_faulted;
    view.symbolic_keyboard = usb_hid_service_symbolic_keyboard();
    view.joystick_port = usb_hid_service_joystick_port();
    return view;
}

static int collect_menu_file(void *context, const char *name, size_t size,
                             bool is_directory)
{
    const mt_menu_browser_t browser = *(const mt_menu_browser_t *)context;
    const mt_content_type_t type = mt_content_classify(name);
    size_t name_size;
    bool accepted;
    if (is_directory) return 0;
    accepted = supervisor.has_active &&
               ((browser == MT_MENU_BROWSER_DRIVE8 &&
                 (type == MT_CONTENT_D64 || type == MT_CONTENT_G64)) ||
               (browser == MT_MENU_BROWSER_CARTRIDGE &&
                 ((supervisor.active.package.core_id == MT_C64_CORE_ID &&
                   type == MT_CONTENT_CRT) ||
                  (supervisor.active.package.core_id == MT_GBC_CORE_ID &&
                   (type == MT_CONTENT_GB_CART ||
                    type == MT_CONTENT_GBC_CART)))) ||
               (browser == MT_MENU_BROWSER_PROGRAM &&
                 type == MT_CONTENT_PRG));
    if (!accepted || supervisor_menu_file_count >= MT_MENU_MAX_FILES)
        return 0;
    name_size = strlen(name);
    if (name_size == 0U || name_size >= MT_MENU_FILE_NAME_MAX) return 0;
    memcpy(supervisor_menu_files[supervisor_menu_file_count].name,
           name, name_size + 1U);
    supervisor_menu_files[supervisor_menu_file_count].size = size;
    ++supervisor_menu_file_count;
    return 0;
}

static void sort_menu_files(void)
{
    size_t index;
    for (index = 1U; index < supervisor_menu_file_count; ++index) {
        mt_menu_file_t value = supervisor_menu_files[index];
        size_t position = index;
        while (position != 0U &&
               strcmp(supervisor_menu_files[position - 1U].name,
                      value.name) > 0) {
            supervisor_menu_files[position] =
                supervisor_menu_files[position - 1U];
            --position;
        }
        supervisor_menu_files[position] = value;
    }
}

static void open_menu_browser(mt_menu_browser_t browser)
{
    mt_sd_result_t list_result;
    if (!supervisor.has_active ||
        (supervisor.active.package.core_id != MT_C64_CORE_ID &&
         !(supervisor.active.package.core_id == MT_GBC_CORE_ID &&
           browser == MT_MENU_BROWSER_CARTRIDGE))) {
        set_supervisor_status("This core has no media browser yet");
        return;
    }
    supervisor_menu_file_count = 0U;
    list_result = mt_sd_storage_list(
        supervisor.active.package.content_root, collect_menu_file, &browser);
    if (list_result.status != MT_SD_OK) {
        set_supervisor_status("Could not list the core media folder");
        return;
    }
    sort_menu_files();
    supervisor_menu.browser = browser;
    supervisor_menu.row = 0U;
    set_supervisor_status(supervisor_menu_file_count == 0U ?
                          "No matching files" : "Choose a file");
}

static int supervisor_quiesce(void *context)
{
    const uint32_t control = shell_read(MT_REG_CONTROL);
    (void)context;
    if (set_core_reset_request(true) != 0) return -1;
    set_audio(true, (control & MT_CONTROL_AUDIO_TEST_ENABLE) != 0U);
    usb_hid_service_set_core_ready(false);
    return 0;
}

static int supervisor_load(void *context,
                           const mt_core_selection_t *selection)
{
    const mt_pl_load_result_t load = mt_pl_load_selection(
        selection->directory,
        &selection->package.variants[selection->variant_index]);
    (void)context;
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
            (uint16_t)version >= abi_minor) {
            return 0;
        }
    }
    return -1;
}

static int supervisor_activate(void *context,
                               const mt_core_selection_t *selection)
{
    (void)context;
    usb_hid_service_set_active_core(selection->package.core_id);
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
    if (selection->package.core_id != MT_C64_CORE_ID) {
        xil_printf("[supervisor] no activation service for core=%08x\r\n",
                   (unsigned int)selection->package.core_id);
        return -1;
    }
    c64_core_reset_active = true;
    c64_content_service_init(&shell_link, NULL, NULL);
    if (!attempt_c64_sd_boot()) return -2;
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
    gameboy_save_dirty_displayed = false;
    gameboy_save_fault_displayed = false;
    return 0;
}

static int supervisor_recover(void *context)
{
    (void)context;
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);
    if (mt_core_link_probe(&shell_link) != MT_LINK_OK ||
        shell_read(MT_REG_CORE_ID) != MT_SUPERVISOR_CORE_ID) {
        return -1;
    }
    return supervisor_enter_standby();
}

static bool key_bitmap_get(const uint8_t *bitmap, uint8_t usage)
{
    return (bitmap[usage >> 3U] & (uint8_t)(1U << (usage & 7U))) != 0U;
}

static bool supervisor_keyboard_filter(void *context, uint8_t slot,
                                       uint8_t modifiers,
                                       const uint8_t *keys,
                                       size_t key_count)
{
    static const uint8_t menu_keys[] = {
        MT_MENU_HID_ENTER, MT_MENU_HID_ESCAPE, MT_MENU_HID_TAB,
        MT_MENU_HID_F12, MT_MENU_HID_RIGHT, MT_MENU_HID_LEFT,
        MT_MENU_HID_DOWN, MT_MENU_HID_UP
    };
    uint8_t next[MT_C64_HID_USAGE_BYTES] = {0};
    const bool was_visible = supervisor_menu.visible;
    size_t index;
    (void)context;
    (void)modifiers;
    if (!supervisor_menu_ready || slot >= MT_C64_HID_SLOT_COUNT)
        return false;
    for (index = 0U; index < key_count; ++index) {
        const uint8_t usage = keys == NULL ? 0U : keys[index];
        if (usage > 3U)
            next[usage >> 3U] |= (uint8_t)(1U << (usage & 7U));
    }
    for (index = 0U; index < sizeof(menu_keys); ++index) {
        const uint8_t usage = menu_keys[index];
        const bool pressed = key_bitmap_get(next, usage);
        if (pressed != key_bitmap_get(supervisor_menu_keys[slot], usage)) {
            const mt_menu_view_t view = supervisor_menu_view();
            const mt_menu_action_t action = mt_menu_key(
                &supervisor_menu, &view, usage, pressed);
            supervisor_menu_dirty = true;
            if (action.kind != MT_MENU_ACTION_NONE &&
                pending_menu_action.kind == MT_MENU_ACTION_NONE) {
                pending_menu_action = action;
            }
        }
    }
    memcpy(supervisor_menu_keys[slot], next, sizeof(next));
    return was_visible || supervisor_menu.visible;
}

static void handle_menu_action(mt_menu_action_t action)
{
    if (action.kind == MT_MENU_ACTION_SWITCH_CORE) {
        mt_core_selection_t selection;
        mt_supervisor_result_t result;
        if (action.core_index >= core_catalog.count ||
            action.variant_index >= core_catalog.cores[action.core_index]
                                      .package.variant_count) {
            set_supervisor_status("Invalid core selection");
            return;
        }
        selection = core_catalog.cores[action.core_index];
        selection.variant_index = action.variant_index;
        set_supervisor_status("Switching core...");
        result = mt_supervisor_switch(&supervisor, &selection);
        if (result == MT_SUPERVISOR_OK) {
            if (!remember_manual_selection(&supervisor.active))
                set_supervisor_status("Core active; settings write failed");
            else if (!last_settings_restore_ok)
                set_supervisor_status("Core active; media restore failed");
            else
                set_supervisor_status("Core switched");
        } else if (result == MT_SUPERVISOR_NO_CHANGE) {
            set_supervisor_status(
                remember_manual_selection(&supervisor.active) ?
                "Core already active" :
                "Core active; settings write failed");
        } else {
            set_supervisor_status(mt_supervisor_result_name(result));
        }
        xil_printf("[supervisor] switch %s state=%s backend=%d recovered=%u\r\n",
                   mt_supervisor_result_name(result),
                   mt_supervisor_state_name(supervisor.state),
                   supervisor.last_backend_error,
                   supervisor.recovered ? 1U : 0U);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_DRIVE8) {
        open_menu_browser(MT_MENU_BROWSER_DRIVE8);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_CARTRIDGE) {
        open_menu_browser(MT_MENU_BROWSER_CARTRIDGE);
    } else if (action.kind == MT_MENU_ACTION_BROWSE_PROGRAM) {
        open_menu_browser(MT_MENU_BROWSER_PROGRAM);
    } else if (action.kind == MT_MENU_ACTION_MOUNT_DRIVE8) {
        int status;
        if (action.file_index >= supervisor_menu_file_count ||
            !c64_drive_ready) {
            set_supervisor_status("Drive 8 is unavailable");
            return;
        }
        status = c64_d64_service_mount(
            supervisor_menu_files[action.file_index].name);
        if (status == 0) supervisor_menu.browser = MT_MENU_BROWSER_NONE;
        if (status != 0)
            set_supervisor_status("Drive 8 mount failed");
        else if (!remember_active_drive8(
                     supervisor_menu_files[action.file_index].name))
            set_supervisor_status("Drive mounted; settings write failed");
        else
            set_supervisor_status("Drive 8 mounted");
    } else if (action.kind == MT_MENU_ACTION_LOAD_CONTENT) {
        if (action.file_index >= supervisor_menu_file_count) {
            set_supervisor_status("Content load could not start");
            return;
        }
        if (shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID) {
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
                supervisor_menu.browser = MT_MENU_BROWSER_NONE;
                usb_hid_service_set_core_ready(true);
                if (dac_ready) set_audio(false, false);
                set_video_test(false);
            }
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                if (!remember_active_cartridge(
                        supervisor_menu_files[action.file_index].name)) {
                    set_supervisor_status(
                        "Game Boy loaded; settings write failed");
                } else {
                    set_supervisor_status(
                        !result.save_active ? "Game Boy cartridge loaded" :
                        (result.save_existed ?
                         "Game Boy loaded; save restored" :
                         "Game Boy loaded; new save ready"));
                }
            } else {
                set_supervisor_status(
                    result.status == MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED ?
                    "Load blocked: save write failed" :
                    "Cartridge load failed");
            }
        } else {
            mt_c64_content_service_result_t result;
            if (!prepare_c64_content_change()) {
                set_supervisor_status("Content load could not start");
                return;
            }
            result = c64_content_service_load(
                supervisor_menu_files[action.file_index].name);
            print_c64_content_result(
                "load", supervisor_menu_files[action.file_index].name,
                &result);
            finish_c64_content_change();
            if (result.status == MT_C64_CONTENT_SERVICE_OK)
                supervisor_menu.browser = MT_MENU_BROWSER_NONE;
            if (result.status != MT_C64_CONTENT_SERVICE_OK) {
                set_supervisor_status("Content load failed");
            } else if (!remember_active_cartridge(
                           mt_content_classify(
                               supervisor_menu_files[action.file_index].name) ==
                                   MT_CONTENT_CRT ?
                           supervisor_menu_files[action.file_index].name :
                           NULL)) {
                set_supervisor_status("Content loaded; settings write failed");
            } else {
                set_supervisor_status("Content loaded");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_EJECT_DRIVE8) {
        if (c64_d64_service_eject() != 0)
            set_supervisor_status("Drive 8 eject failed");
        else if (!remember_active_drive8(NULL))
            set_supervisor_status("Drive ejected; settings write failed");
        else
            set_supervisor_status("Drive 8 ejected");
    } else if (action.kind == MT_MENU_ACTION_DETACH_CARTRIDGE) {
        if (shell_read(MT_REG_CORE_ID) == MT_GBC_CORE_ID) {
            const mt_gameboy_content_service_result_t result =
                gameboy_content_service_detach();
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                usb_hid_service_set_core_ready(false);
                if (dac_ready) set_audio(true, false);
                set_video_test(true);
            }
            if (result.status == MT_GAMEBOY_CONTENT_SERVICE_OK) {
                set_supervisor_status(
                    remember_active_cartridge(NULL) ?
                    "Cartridge unloaded" :
                    "Cart unloaded; settings write failed");
            } else {
                set_supervisor_status(
                    result.status == MT_GAMEBOY_CONTENT_SERVICE_FLUSH_FAILED ?
                    "Unload blocked: save write failed" :
                    "Cartridge unload failed");
            }
        } else {
            mt_c64_content_service_result_t result;
            if (!prepare_c64_content_change()) {
                set_supervisor_status("Cartridge detach could not start");
                return;
            }
            result = c64_content_service_detach_crt();
            finish_c64_content_change();
            if (result.status == MT_C64_CONTENT_SERVICE_OK) {
                set_supervisor_status(
                    remember_active_cartridge(NULL) ?
                    "Cartridge detached" :
                    "Cart detached; settings write failed");
            } else {
                set_supervisor_status("Cartridge detach failed");
            }
        }
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
            set_supervisor_status("Keyboard map change failed");
        else if (!remember_c64_input_settings())
            set_supervisor_status("Keyboard changed; settings write failed");
        else
            set_supervisor_status("Keyboard map changed");
    } else if (action.kind == MT_MENU_ACTION_SWAP_JOYSTICK_PORT) {
        const uint8_t port = usb_hid_service_joystick_port() == 2U ? 1U : 2U;
        if (usb_hid_service_set_joystick_port(port) != 0)
            set_supervisor_status("Gamepad port change failed");
        else if (!remember_c64_input_settings())
            set_supervisor_status("Gamepad changed; settings write failed");
        else
            set_supervisor_status("Gamepad port changed");
    } else if (action.kind == MT_MENU_ACTION_RESET_CORE) {
        if (!supervisor.has_active) {
            set_supervisor_status("No active core");
            return;
        }
        const int asserted = set_core_reset_request(true);
        if (asserted == 0) usleep(1000U);
        set_supervisor_status(
            asserted == 0 && set_core_reset_request(false) == 0 ?
            "Core restarted" : "Core restart failed");
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
            set_supervisor_status("Cannot enable auto-boot");
        } else {
            persistent_settings.auto_boot = enabled;
            if (save_persistent_settings()) {
                set_supervisor_status(enabled ? "Autoboot enabled" :
                                      "Autoboot disabled");
            } else {
                persistent_settings = settings_before_update;
                set_supervisor_status("Autoboot change not saved");
            }
        }
    } else if (action.kind == MT_MENU_ACTION_REBOOT_GOLDEN) {
        reboot_to_golden();
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
        handle_menu_action(action);
    }
    if (!supervisor_menu_dirty) return;
    view = supervisor_menu_view();
    mt_menu_render(&supervisor_menu, &view, &supervisor_frame);
    if (mt_ui_present(&bus, &supervisor_frame, supervisor_menu.visible) ==
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
        set_supervisor_status("Invalid boot image; core held in reset");
    else if (!standby_ready)
        set_supervisor_status("Supervisor standby failed");
    else if (settings_faulted)
        set_supervisor_status("Settings invalid; using safe defaults");
    else
        set_supervisor_status("Ready; press F12 to choose a core");

    if (persistent_settings.auto_boot) {
        if (!configured_auto_boot_selection(&startup_auto_selection)) {
            set_supervisor_status("Autoboot target unavailable");
        } else {
            switch_result =
                mt_supervisor_switch(&supervisor, &startup_auto_selection);
            if (switch_result != MT_SUPERVISOR_OK) {
                set_supervisor_status(
                    supervisor.recovered ?
                    "Autoboot failed; standby restored" :
                    "Autoboot and recovery failed");
            } else {
                set_supervisor_status(last_settings_restore_ok ?
                                      "Autoboot complete" :
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
}

static void recovery_console(void)
{
    xil_printf("recovery command: :reboot return to golden updater\r\n");
    for (;;) {
        const char input = inbyte();
        if (input == ':') {
            handle_extended_command(true);
        } else if (input != '\r' && input != '\n') {
            xil_printf("shell unavailable; only :reboot is accepted\r\n");
        }
    }
}

int main(void)
{
    mt_link_result_t link_status;
    int status;
    int usb_status;

    init_platform();
    Xil_SetTlbAttributes(MT_SHELL_BASE_ADDRESS, DEVICE_MEMORY);
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);

    xil_printf("\r\nMultitini One diagnostic firmware build=%08x\r\n",
               (unsigned int)MT_FIRMWARE_BUILD_ID);
    link_status = mt_core_link_probe(&shell_link);
    xil_printf("shell probe: %s\r\n", mt_link_result_name(link_status));
    if (link_status != MT_LINK_OK) {
        xil_printf("shell unavailable; diagnostic outputs remain in reset defaults\r\n");
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
    c64_content_service_init(&shell_link, NULL, NULL);
    c64_services_initialized = true;
    initialize_supervisor_menu();
    service_supervisor_menu();
    print_help();

    for (;;) {
        uint8_t byte;
        char input;
        service_runtime();
        service_supervisor_menu();
        if (uart0_try_read_byte(&byte) == 0) continue;
        input = (char)byte;
        const uint32_t control = shell_read(MT_REG_CONTROL);
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
    }
}
