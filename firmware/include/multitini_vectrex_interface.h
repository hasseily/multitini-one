/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX_INTERFACE_H
#define MULTITINI_VECTREX_INTERFACE_H
#include <stdint.h>
#define MT_VECTREX_CORE_ID UINT32_C(0x56454358)
#define MT_LOAD_VECTREX_BIOS UINT8_C(0x40)
#define MT_LOAD_VECTREX_CART UINT8_C(0x41)
#define MT_LOAD_VECTREX_LINE UINT8_C(0x42)
#define MT_LOAD_VECTREX_PSG UINT8_C(0x43)
#define MT_LOAD_VECTREX_MODE UINT8_C(0x44)
/* LINE: nine bytes, four little-endian signed 16-bit native coordinates then
 * intensity. PSG: register 0..15 and value. MODE: one byte, 0 = native,
 * 1 = BASIC, 2 = native with cartridge ejected. Change mode under core reset. */
#define MT_SIZE_VECTREX_BIOS 8192U
#define MT_SIZE_VECTREX_CART_MAX 65536U
#define MT_SIZE_VECTREX_BASIC_MAX (1024U * 1024U)
#define MT_INPUT_VECTREX UINT8_C(0x20)
/* Namespace 0x20: 0 = packed active-high buttons (P1 low nibble/P2 high),
 * 1..4 = X1,Y1,X2,Y2 as signed 16-bit full-range coordinates, +Y up. */
#define MT_VECTREX_BIOS_PATH "0:/multitini/cores/vectrex/roms/bios.bin"
#endif
