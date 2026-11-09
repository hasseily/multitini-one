/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SPECTRUM_TZX_H
#define MULTITINI_SPECTRUM_TZX_H

#include "multitini_spectrum_interface.h"

#include <stddef.h>

typedef enum {
    MT_SPECTRUM_TZX_OK,
    MT_SPECTRUM_TZX_BAD_ARGUMENT,
    MT_SPECTRUM_TZX_BAD_HEADER,
    MT_SPECTRUM_TZX_UNSUPPORTED_VERSION,
    MT_SPECTRUM_TZX_TRUNCATED,
    MT_SPECTRUM_TZX_INVALID_BLOCK,
    MT_SPECTRUM_TZX_UNSUPPORTED_BLOCK,
    MT_SPECTRUM_TZX_UNSUPPORTED_CSW,
    MT_SPECTRUM_TZX_UNSUPPORTED_GENERALIZED,
    MT_SPECTRUM_TZX_UNSUPPORTED_NAVIGATION,
    MT_SPECTRUM_TZX_TOO_LARGE,
    MT_SPECTRUM_TZX_EMPTY,
} mt_spectrum_tzx_status_t;

/* Compile into a separate caller-owned buffer. Multi-byte operands are little
 * endian. Data and samples are played most significant bit first.
 * 01 u16 duration, u16 count
 * 02 u8 count, u16 duration[count]
 * 03 u16 zero, u16 one, u8 last bits, u24 length, u8 data[length]
 * 04 u16 milliseconds (zero stops the tape)
 * 05 u16 sample period, u8 last bits, u24 length, u8 data[length]
 * 06 u8 level
 * 07 stop if 48K
 * Stream size marks the end. On failure output_size is zero and output may
 * have been modified. The FPGA limit applies even to a larger buffer. */
mt_spectrum_tzx_status_t mt_spectrum_tzx_compile(
    const uint8_t *input, size_t size, uint8_t *output, size_t capacity,
    size_t *output_size);
const char *mt_spectrum_tzx_status_name(mt_spectrum_tzx_status_t status);

#endif
