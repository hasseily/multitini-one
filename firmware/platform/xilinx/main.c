/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_d64_service.h"
#include "multitini_ak4493.h"
#include "multitini_c64_boot.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_rom_files.h"
#include "multitini_content.h"
#include "multitini_core_link.h"
#include "multitini_regs.h"
#include "multitini_tmp102.h"
#include "multitini_xmodem.h"
#include "sd_storage.h"
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
#define MT_FIRMWARE_BUILD_ID UINT32_C(0x20260902)
#define MT_I2C_CLOCK_HZ UINT32_C(100000)
#define MT_I2C_BUSY_POLLS UINT32_C(1000000)
#define MT_UART_DRAIN_POLLS UINT32_C(10000000)
#define MT_UART_BYTE_TIMEOUT_MS UINT32_C(1000)
#define MT_UART_XMODEM_RETRIES UINT32_C(20)
#define MT_CORE_READY_POLLS UINT32_C(1000000)
#define MT_LINK_COMMAND_POLLS UINT32_C(100000)
#define MT_EXTENDED_COMMAND_CAPACITY 272U
#define MT_EXTENDED_ARGUMENT_CAPACITY 4

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
static bool dac_ready;
static bool c64_drive_ready;
static bool c64_core_reset_active;
static uint8_t c64_rom_bundle[MT_C64_SYSTEM_ROM_BUNDLE_SIZE];
static uint8_t c64_drive_rom[MT_SIZE_C64_1541_ROM];

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
    service_usb_input();
    if ((control & MT_CONTROL_CORE_RESET_REQUEST) != 0U) {
        if (!c64_core_reset_active) {
            c64_d64_service_notify_core_reset();
        }
        c64_core_reset_active = true;
        return;
    }
    c64_core_reset_active = false;
    c64_d64_service_poll();
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
    return command(MT_COMMAND_CORE_RESET, asserted ? 1U : 0U, 0U);
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

static void attempt_c64_sd_boot(void)
{
    mt_sd_result_t mount_result;
    mt_c64_rom_files_result_t files_result;
    mt_c64_system_roms_t roms;
    mt_link_result_t load_result;
    uint32_t core_status = 0U;
    uint32_t actual_crc32;
    uint32_t control;

    if (set_core_reset_request(true) != 0) {
        xil_printf("[C64] error: cannot enter reset for SD ROM load\r\n");
        return;
    }

    mount_result = mt_sd_storage_mount();
    xil_printf("[SD] mount status=%s backend=%u\r\n",
               mt_sd_status_name(mount_result.status),
               mount_result.backend_status);
    if (mount_result.status != MT_SD_OK) {
        print_c64_sd_fallback();
        return;
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
        return;
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
        return;
    }
    if (!wait_for_c64_core_ready(&core_status)) {
        leave_core_in_reset();
        xil_printf("[C64] error: core-ready timeout status=%08x\r\n",
                   (unsigned int)core_status);
        print_c64_sd_fallback();
        return;
    }

    control = shell_read(MT_REG_CONTROL);
    Xil_Out32(MT_SHELL_BASE_ADDRESS + MT_REG_CONTROL,
              control & ~MT_CONTROL_VIDEO_TEST_ENABLE);
    actual_crc32 = mt_xmodem_crc32_ieee(c64_rom_bundle,
                                         sizeof(c64_rom_bundle));
    xil_printf("[C64] SD boot ok root=%s crc32=%08x status=%08x\r\n",
               MT_C64_SD_ROM_ROOT, (unsigned int)actual_crc32,
               (unsigned int)core_status);
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
    xil_printf("commands: h help, s status, p ping, a live audio, ");
    xil_printf("t test tone, m mute, ");
    xil_printf("v toggle video, r toggle core reset, u USB status, ");
    xil_printf("j swap joystick port, k toggle keyboard map\r\n");
    xil_printf("US map: Alt/GUI=C=, F9=up-arrow, F10=+, F11=RESTORE, ");
    xil_printf("F12=C64 equals; k restores MiSTer positional mapping\r\n");
    xil_printf("extended: :mount FILE.d64, :eject, :dir, :disk\r\n");
    xil_printf("recovery: :c64rx 20480 0xCRC32, :reboot to golden\r\n");
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
    const int usb_stop_result = usb_hid_service_stop();
    mt_sd_result_t unmount_result;
    if (usb_stop_result != 0) {
        xil_printf("USB1 VBUS shutdown failed (%d); reboot cancelled\r\n",
                   usb_stop_result);
        return;
    }
    unmount_result = mt_sd_storage_unmount();
    if (unmount_result.status != MT_SD_OK) {
        xil_printf("SD unmount warning: %s backend=%u\r\n",
                   mt_sd_status_name(unmount_result.status),
                   unmount_result.backend_status);
    }
    xil_printf("rebooting to golden at QSPI offset 0x00000000\r\n");
    if (!wait_uart0_transmit_idle()) {
        xil_printf("UART did not drain; reboot cancelled\r\n");
        return;
    }

    Xil_Out32(MT_DEVCFG_MULTIBOOT, 0U);
    DATA_SYNC;
    if ((Xil_In32(MT_DEVCFG_MULTIBOOT) & MT_DEVCFG_MULTIBOOT_MASK) != 0U) {
        xil_printf("multiboot readback failed; reboot cancelled\r\n");
        return;
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
    char *mount_name;
    char *mount_end;

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
    } else if (!recovery_only && strcmp(extended, "eject") == 0) {
        (void)c64_d64_service_eject();
    } else if (!recovery_only && strncmp(extended, "mount", 5U) == 0 &&
               (extended[5] == ' ' || extended[5] == '\t')) {
        mount_name = extended + 5;
        while (*mount_name == ' ' || *mount_name == '\t') ++mount_name;
        mount_end = mount_name + strlen(mount_name);
        while (mount_end > mount_name &&
               (mount_end[-1] == ' ' || mount_end[-1] == '\t')) {
            --mount_end;
        }
        *mount_end = '\0';
        if (*mount_name == '\0') {
            xil_printf("usage: :mount FILE.d64\r\n");
        } else if (!c64_drive_ready) {
            xil_printf("[C64] drive8 unavailable; install %s\r\n",
                       MT_C64_SD_1541_PATH);
        } else {
            (void)c64_d64_service_mount(mount_name);
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
    if (shell_read(MT_REG_CORE_ID) == MT_C64_CORE_ID) {
        attempt_c64_sd_boot();
    }

    status = initialize_i2c();
    if (status == 0) {
        print_temperature();
        dac_bus.context = &i2c_instance;
        dac_bus.write = board_i2c_write;
        dac_bus.delay_us = board_delay_us;
        status = mt_ak4493_initialize(&dac_bus, false);
    }
    dac_ready = status == 0;
    xil_printf("DAC initialization (muted): %s (%d)\r\n",
               dac_ready ? "ok" : "failed", status);
    if (dac_ready &&
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U) {
        set_audio(false, false);
    }
    print_status();
    usb_hid_service_init(&shell_link);
    usb_hid_service_set_core_ready(
        (shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U);
    usb_status = usb_hid_service_start();
    xil_printf("USB1 keyboard/gamepad initialization: %s (%d)\r\n",
               usb_status == 0 ? "ok" : "failed", usb_status);
    usb_hid_service_dump_status();
    /* A progress callback must not re-enter shell_link through USB input. */
    c64_d64_service_init(&shell_link, NULL, NULL);
    if (shell_read(MT_REG_CORE_ID) == MT_C64_CORE_ID && c64_drive_ready) {
        (void)c64_d64_service_mount_default();
    }
    print_help();

    for (;;) {
        uint8_t byte;
        char input;
        service_runtime();
        if (uart0_try_read_byte(&byte) == 0) continue;
        input = (char)byte;
        const uint32_t control = shell_read(MT_REG_CONTROL);
        switch (input) {
        case 'h': print_help(); break;
        case 's':
            print_status();
            c64_d64_service_dump_status();
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
            (void)command(MT_COMMAND_CORE_RESET,
                          (control & MT_CONTROL_CORE_RESET_REQUEST) == 0U,
                          0U);
            print_status();
            break;
        case 'u': usb_hid_service_dump_status(); break;
        case 'k': {
            const bool symbolic =
                !usb_hid_service_symbolic_keyboard();
            if (usb_hid_service_set_symbolic_keyboard(symbolic) == 0) {
                xil_printf("USB keyboard map: %s\r\n",
                           symbolic ? "US symbolic" : "MiSTer positional");
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
