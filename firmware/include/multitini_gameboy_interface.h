/* SPDX-License-Identifier: GPL-3.0-only */
/* Generated from spec/gameboy_interface_v0.json. Do not edit. */
#ifndef MULTITINI_GAMEBOY_INTERFACE_H
#define MULTITINI_GAMEBOY_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#define MT_GBC_CORE_ID UINT32_C(0x47424320)
#define MT_GBC_CORE_HZ UINT32_C(33554432)
#define MT_GBC_CPU_HZ UINT32_C(4194304)
#define MT_GBC_AUDIO_SAMPLE_HZ UINT32_C(48000)

typedef enum {
    MT_LOAD_GBC_BOOT = 0x20,
    MT_LOAD_GB_CART = 0x21,
    MT_LOAD_GB_SAVE = 0x22,
} mt_gameboy_load_kind_t;

typedef enum {
    MT_INPUT_GB_GAMEPAD = 0x10,
} mt_gameboy_input_namespace_t;

typedef enum {
    MT_GB_BUTTON_RIGHT = 0,
    MT_GB_BUTTON_LEFT = 1,
    MT_GB_BUTTON_DOWN = 2,
    MT_GB_BUTTON_UP = 3,
    MT_GB_BUTTON_A = 4,
    MT_GB_BUTTON_B = 5,
    MT_GB_BUTTON_SELECT = 6,
    MT_GB_BUTTON_START = 7,
} mt_gameboy_button_t;

#define MT_SIZE_GBC_BOOT ((size_t)2304)
#define MT_SIZE_GB_CART_MIN ((size_t)32768)
#define MT_SIZE_GB_CART_MAX ((size_t)8388608)
#define MT_SIZE_GB_CART_RAM_MAX ((size_t)131072)

#define MT_GB_MEMORY_ROM_BASE UINT32_C(0x000000)
#define MT_GB_MEMORY_CART_RAM_BASE UINT32_C(0x800000)

#endif
