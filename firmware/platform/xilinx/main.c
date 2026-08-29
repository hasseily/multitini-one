/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ak4493.h"
#include "multitini_core_link.h"
#include "multitini_regs.h"
#include "multitini_tmp102.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"
#include "sleep.h"
#include "xiicps.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"

#define MT_SHELL_BASE_ADDRESS UINT32_C(0x40000000)
#define MT_I2C_CLOCK_HZ UINT32_C(100000)
#define MT_I2C_BUSY_POLLS UINT32_C(1000000)

static XIicPs i2c_instance;
static mt_core_link_t shell_link;
static mt_ak4493_bus_t dac_bus;
static bool dac_ready;

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
    xil_printf("temperature=%c%lu.%02lu C\r\n", sign,
               (unsigned long)(magnitude / UINT32_C(1000000)),
               (unsigned long)((magnitude % UINT32_C(1000000)) /
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

static void print_status(void)
{
    xil_printf("magic=%08lx abi=%08lx shell=%08lx build=%08lx\r\n",
               (unsigned long)shell_read(MT_REG_MAGIC),
               (unsigned long)shell_read(MT_REG_ABI_VERSION),
               (unsigned long)shell_read(MT_REG_SHELL_ID),
               (unsigned long)shell_read(MT_REG_SHELL_BUILD_ID));
    xil_printf("core=%08lx status=%08lx error=%08lx control=%08lx\r\n",
               (unsigned long)shell_read(MT_REG_CORE_ID),
               (unsigned long)shell_read(MT_REG_STATUS),
               (unsigned long)shell_read(MT_REG_ERROR),
               (unsigned long)shell_read(MT_REG_CONTROL));
}

static int command(uint32_t opcode, uint32_t arg0, uint32_t arg1)
{
    const uint32_t args[4] = {arg0, arg1, 0U, 0U};
    mt_command_response_t response;
    const mt_link_result_t result = mt_core_link_command(
        &shell_link, opcode, args, UINT32_C(100000), NULL, NULL, &response);
    if (result != MT_LINK_OK) {
        xil_printf("command failed: %s", mt_link_result_name(result));
        if (result == MT_LINK_REMOTE_ERROR) {
            xil_printf(" status=%lu", (unsigned long)response.status);
        }
        xil_printf("\r\n");
        return -1;
    }
    xil_printf("response seq=%lu arg0=%08lx arg1=%08lx\r\n",
               (unsigned long)response.sequence,
               (unsigned long)response.arg0,
               (unsigned long)response.arg1);
    return 0;
}

static void set_audio(bool mute)
{
    int status;
    if (mute) {
        (void)command(MT_COMMAND_SET_AUDIO, 1U, 1U);
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
    status = mt_ak4493_set_mute(&dac_bus, false);
    if (status != 0) {
        xil_printf("DAC unmute failed: %d\r\n", status);
        return;
    }
    if (command(MT_COMMAND_SET_AUDIO, 1U, 0U) != 0) {
        (void)mt_ak4493_set_mute(&dac_bus, true);
    }
}

static void print_help(void)
{
    xil_printf("commands: h help, s status, p ping, a audio on, m mute, ");
    xil_printf("v toggle video, r toggle core reset\r\n");
}

int main(void)
{
    mt_link_result_t link_status;
    int status;

    init_platform();
    Xil_SetTlbAttributes(MT_SHELL_BASE_ADDRESS, DEVICE_MEMORY);
    mt_core_link_init_mmio(&shell_link,
        (volatile void *)(uintptr_t)MT_SHELL_BASE_ADDRESS);

    xil_printf("\r\nMultitini One source-only diagnostic firmware\r\n");
    link_status = mt_core_link_probe(&shell_link);
    xil_printf("shell probe: %s\r\n", mt_link_result_name(link_status));
    if (link_status != MT_LINK_OK) {
        xil_printf("halting with all diagnostic outputs in reset defaults\r\n");
        for (;;) { }
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
    print_status();
    print_help();

    for (;;) {
        const char input = inbyte();
        const uint32_t control = shell_read(MT_REG_CONTROL);
        switch (input) {
        case 'h': print_help(); break;
        case 's': print_status(); break;
        case 'p': (void)command(MT_COMMAND_PING, 0U, 0U); break;
        case 'a': set_audio(false); break;
        case 'm': set_audio(true); break;
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
