/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_uci_internal.h"
#include "multitini_c64_interface.h"
#include "xil_io.h"
#include "xpseudo_asm.h"
#include <string.h>

/* This aperture is configured uncached by the C64 launch path. HOLD must
 * already be acknowledged before a caller touches it. */
void c64_uci_reu_read(uint32_t address, void *data, size_t bytes)
{
    memcpy(data, (const void *)(uintptr_t)(MT_C64_REU_DDR_BASE + address), bytes);
}
void c64_uci_reu_write(uint32_t address, const void *data, size_t bytes)
{
    memcpy((void *)(uintptr_t)(MT_C64_REU_DDR_BASE + address), data, bytes);
}
void c64_uci_reu_drain(void)
{
    DATA_SYNC;
    Xil_Out32(UINT32_C(0xF8F02730), 0);
    DATA_SYNC;
}
