/* SPDX-License-Identifier: GPL-3.0-only */
/* Generated from spec/c64_interface_v0.json. Do not edit. */
#ifndef MULTITINI_C64_INTERFACE_H
#define MULTITINI_C64_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#define MT_C64_CORE_ID UINT32_C(0x43363420)
#define MT_C64_PAL_HZ UINT32_C(31527954)
#define MT_C64_NTSC_HZ UINT32_C(32727266)
#define MT_C64_AUDIO_SAMPLE_HZ UINT32_C(48000)

typedef enum {
    MT_LOAD_C64_BASIC = 0x01,
    MT_LOAD_C64_KERNAL = 0x02,
    MT_LOAD_C64_CHARACTER = 0x03,
    MT_LOAD_C64_1541_ROM = 0x04,
    MT_LOAD_C64_PRG = 0x10,
    MT_LOAD_C64_CRT = 0x11,
    MT_LOAD_C64_D64 = 0x12,
    MT_LOAD_C64_G64 = 0x13,
} mt_c64_load_kind_t;

typedef enum {
    MT_INPUT_PS2_SET2 = 0x00,
    MT_INPUT_JOYSTICK1 = 0x01,
    MT_INPUT_JOYSTICK2 = 0x02,
} mt_c64_input_namespace_t;

#define MT_SIZE_C64_BASIC ((size_t)8192)
#define MT_SIZE_C64_KERNAL ((size_t)8192)
#define MT_SIZE_C64_CHARACTER ((size_t)4096)
#define MT_SIZE_C64_1541_ROM ((size_t)16384)

#endif
