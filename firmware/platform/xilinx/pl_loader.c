/* SPDX-License-Identifier: GPL-3.0-only */
#include "pl_loader.h"

#include "sd_storage.h"

#include <stdint.h>

#include "sleep.h"
#include "xdevcfg.h"
#include "xdevcfg_hw.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xiltimer.h"

#define MT_SLCR_BASE UINT32_C(0xF8000000)
#define MT_SLCR_LOCK (MT_SLCR_BASE + UINT32_C(0x0004))
#define MT_SLCR_UNLOCK (MT_SLCR_BASE + UINT32_C(0x0008))
#define MT_SLCR_PCAP_CLOCK (MT_SLCR_BASE + UINT32_C(0x0168))
#define MT_SLCR_FPGA_RESET (MT_SLCR_BASE + UINT32_C(0x0240))
#define MT_SLCR_LEVEL_SHIFTER (MT_SLCR_BASE + UINT32_C(0x0900))
#define MT_SLCR_LOCK_KEY UINT32_C(0x0000767B)
#define MT_SLCR_UNLOCK_KEY UINT32_C(0x0000DF0D)
#define MT_FPGA_RESET_ALL UINT32_C(0x0000000F)
#define MT_LEVEL_PS_TO_PL UINT32_C(0x0000000A)
#define MT_LEVEL_ALL UINT32_C(0x0000000F)
#define MT_PCAP_CLOCK_ENABLE UINT32_C(0x00000001)
#define MT_PCAP_LAST_TRANSFER ((uintptr_t)1U)
#define MT_PCAP_TIMEOUT_MS UINT32_C(2000)

static uint8_t bitstream_buffer[MT_CORE_BITSTREAM_MAX_BYTES]
    __attribute__((aligned(64)));
static XDcfg devcfg;

static mt_pl_load_result_t result(mt_pl_load_status_t status,
                                  unsigned backend_status,
                                  uint32_t crc, size_t size)
{
    mt_pl_load_result_t value;
    value.status = status;
    value.backend_status = backend_status;
    value.actual_crc32 = crc;
    value.size = size;
    return value;
}

static void set_fabric_isolation(bool isolated)
{
    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    DATA_SYNC;
    if (isolated) {
        Xil_Out32(MT_SLCR_FPGA_RESET, MT_FPGA_RESET_ALL);
        Xil_Out32(MT_SLCR_LEVEL_SHIFTER, MT_LEVEL_PS_TO_PL);
    } else {
        Xil_Out32(MT_SLCR_LEVEL_SHIFTER, MT_LEVEL_ALL);
        Xil_Out32(MT_SLCR_FPGA_RESET, 0U);
    }
    DATA_SYNC;
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    DATA_SYNC;
}

static void enable_pcap_clock(void)
{
    const uint32_t clock = Xil_In32(MT_SLCR_PCAP_CLOCK);
    if ((clock & MT_PCAP_CLOCK_ENABLE) != 0U) return;
    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    DATA_SYNC;
    Xil_Out32(MT_SLCR_PCAP_CLOCK, clock | MT_PCAP_CLOCK_ENABLE);
    DATA_SYNC;
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    DATA_SYNC;
}

static bool wait_status_mask(uint32_t mask, bool asserted, uint32_t timeout_ms)
{
    XTime start = 0U;
    XTime now = 0U;
    const XTime timeout =
        ((XTime)COUNTS_PER_SECOND * (XTime)timeout_ms) / (XTime)1000U;
    XTime_GetTime(&start);
    do {
        const bool state =
            (XDcfg_GetStatusRegister(&devcfg) & mask) == mask;
        if (state == asserted) return true;
        XTime_GetTime(&now);
    } while ((now - start) < timeout);
    return false;
}

static mt_pl_load_status_t initialize_fabric(void)
{
    unsigned attempt;

    set_fabric_isolation(true);
    for (attempt = 0U; attempt < 2U; ++attempt) {
        XDcfg_SetControlRegister(&devcfg, XDCFG_CTRL_PCFG_PROG_B_MASK);
        usleep(1000U);
        XDcfg_ClearControlRegister(&devcfg, XDCFG_CTRL_PCFG_PROG_B_MASK);
        if (wait_status_mask(XDCFG_STATUS_PCFG_INIT_MASK, false, 30U))
            break;
    }
    if (attempt == 2U) return MT_PL_LOAD_FABRIC_INIT_FAILED;
    XDcfg_SetControlRegister(&devcfg, XDCFG_CTRL_PCFG_PROG_B_MASK);
    if (!wait_status_mask(XDCFG_STATUS_PCFG_INIT_MASK, true, 30U))
        return MT_PL_LOAD_FABRIC_INIT_FAILED;
    return MT_PL_LOAD_OK;
}

static mt_pl_load_result_t program_fabric(uint8_t *data, size_t size,
                                          uint32_t crc)
{
    XDcfg_Config *config;
    mt_pl_load_status_t init_status;
    uint32_t transfer_status;
    uint32_t interrupts;
    void *source;

#ifdef SDT
    config = XDcfg_LookupConfig(XPAR_XDEVCFG_0_BASEADDR);
#else
    config = XDcfg_LookupConfig(XPAR_XDCFG_0_DEVICE_ID);
#endif
    if (config == NULL ||
        XDcfg_CfgInitialize(&devcfg, config, config->BaseAddr) != XST_SUCCESS)
        return result(MT_PL_LOAD_DEVCFG_INIT_FAILED, 0U, crc, size);

    enable_pcap_clock();
    XDcfg_EnablePCAP(&devcfg);
    XDcfg_SelectPcapInterface(&devcfg);
    XDcfg_IntrClear(&devcfg, UINT32_MAX);
    init_status = initialize_fabric();
    if (init_status != MT_PL_LOAD_OK)
        return result(init_status, 0U, crc, size);
    if ((XDcfg_GetStatusRegister(&devcfg) &
         XDCFG_STATUS_DMA_CMD_Q_F_MASK) != 0U) {
        return result(MT_PL_LOAD_TRANSFER_FAILED, XST_DEVICE_BUSY, crc, size);
    }

    Xil_DCacheFlushRange((INTPTR)(uintptr_t)data, (u32)size);
    source = (void *)((uintptr_t)data | MT_PCAP_LAST_TRANSFER);
    transfer_status = XDcfg_Transfer(
        &devcfg, source, (u32)(size / 4U),
        (void *)(uintptr_t)XDCFG_DMA_INVALID_ADDRESS, 0U,
        XDCFG_NON_SECURE_PCAP_WRITE);
    if (transfer_status != XST_SUCCESS)
        return result(MT_PL_LOAD_TRANSFER_FAILED, transfer_status, crc, size);

    {
        XTime start = 0U;
        XTime now = 0U;
        const XTime timeout =
            ((XTime)COUNTS_PER_SECOND * (XTime)MT_PCAP_TIMEOUT_MS) /
            (XTime)1000U;
        XTime_GetTime(&start);
        do {
            interrupts = XDcfg_IntrGetStatus(&devcfg);
            if ((interrupts & XDCFG_IXR_ERROR_FLAGS_MASK) != 0U)
                return result(MT_PL_LOAD_DEVCFG_ERROR, interrupts, crc, size);
            if ((interrupts & XDCFG_IXR_DMA_DONE_MASK) != 0U) break;
            XTime_GetTime(&now);
        } while ((now - start) < timeout);
        if ((interrupts & XDCFG_IXR_DMA_DONE_MASK) == 0U)
            return result(MT_PL_LOAD_DMA_TIMEOUT, interrupts, crc, size);
        XDcfg_IntrClear(&devcfg, XDCFG_IXR_DMA_DONE_MASK);

        XTime_GetTime(&start);
        do {
            interrupts = XDcfg_IntrGetStatus(&devcfg);
            if ((interrupts & XDCFG_IXR_ERROR_FLAGS_MASK) != 0U)
                return result(MT_PL_LOAD_DEVCFG_ERROR, interrupts, crc, size);
            if ((interrupts & XDCFG_IXR_PCFG_DONE_MASK) != 0U) break;
            XTime_GetTime(&now);
        } while ((now - start) < timeout);
        if ((interrupts & XDCFG_IXR_PCFG_DONE_MASK) == 0U)
            return result(MT_PL_LOAD_DONE_TIMEOUT, interrupts, crc, size);
    }

    XDcfg_IntrClear(&devcfg, interrupts);
    set_fabric_isolation(false);
    return result(MT_PL_LOAD_OK, 0U, crc, size);
}

mt_pl_load_result_t mt_pl_load_selection(
    const char *directory, const mt_core_variant_t *variant)
{
    char path[MT_CORE_DIRECTORY_MAX + MT_CORE_FILENAME_MAX + 2U];
    mt_core_package_status_t path_status;
    mt_sd_result_t read_result;
    uint32_t crc;

    if (directory == NULL || variant == NULL ||
        variant->bitstream_size < MT_CORE_BITSTREAM_MIN_BYTES ||
        variant->bitstream_size > MT_CORE_BITSTREAM_MAX_BYTES ||
        (variant->bitstream_size & 3U) != 0U) {
        return result(MT_PL_LOAD_INVALID_ARGUMENT, 0U, 0U, 0U);
    }
    path_status = mt_core_package_bitstream_path(
        directory, variant, path, sizeof(path));
    if (path_status != MT_CORE_PACKAGE_OK)
        return result(MT_PL_LOAD_PATH_FAILED, (unsigned)path_status, 0U, 0U);

    read_result = mt_sd_storage_read_file_exact(
        path, bitstream_buffer, variant->bitstream_size);
    if (read_result.status != MT_SD_OK) {
        const mt_pl_load_status_t status =
            read_result.status == MT_SD_SIZE_MISMATCH ?
            MT_PL_LOAD_SIZE_MISMATCH : MT_PL_LOAD_READ_FAILED;
        return result(status, read_result.backend_status, 0U,
                      read_result.size);
    }
    crc = mt_core_package_crc32(bitstream_buffer, read_result.size);
    if (crc != variant->bitstream_crc32)
        return result(MT_PL_LOAD_CRC_MISMATCH, 0U, crc, read_result.size);
    return program_fabric(bitstream_buffer, read_result.size, crc);
}

const char *mt_pl_load_status_name(mt_pl_load_status_t status)
{
    switch (status) {
    case MT_PL_LOAD_OK: return "ok";
    case MT_PL_LOAD_INVALID_ARGUMENT: return "invalid argument";
    case MT_PL_LOAD_PATH_FAILED: return "path failed";
    case MT_PL_LOAD_READ_FAILED: return "read failed";
    case MT_PL_LOAD_SIZE_MISMATCH: return "size mismatch";
    case MT_PL_LOAD_CRC_MISMATCH: return "CRC mismatch";
    case MT_PL_LOAD_DEVCFG_INIT_FAILED: return "DevCfg init failed";
    case MT_PL_LOAD_FABRIC_INIT_FAILED: return "fabric init failed";
    case MT_PL_LOAD_TRANSFER_FAILED: return "PCAP transfer failed";
    case MT_PL_LOAD_DMA_TIMEOUT: return "PCAP DMA timeout";
    case MT_PL_LOAD_DONE_TIMEOUT: return "FPGA done timeout";
    case MT_PL_LOAD_DEVCFG_ERROR: return "DevCfg error";
    default: return "unknown";
    }
}
