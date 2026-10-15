/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_INTERFACE_H
#define MULTITINI_IIGS_INTERFACE_H
#include <stdint.h>
#define MT_IIGS_CORE_ID UINT32_C(0x49494753)
enum { MT_LOAD_IIGS_ROM3=0x40, MT_LOAD_IIGS_ROM1=0x41,
    MT_LOAD_IIGS_CHARACTER=0x42, MT_LOAD_IIGS_CONFIG=0x43,
    MT_LOAD_IIGS_ADB3=0x44, MT_LOAD_IIGS_HDD_ROM=0x45, MT_LOAD_IIGS_ADB1=0x46,
    MT_LOAD_IIGS_PRAM=0x47, MT_LOAD_IIGS_SECTOR=0x48 };
enum { MT_IIGS_MEDIA_BLOCK=0x40, MT_IIGS_MEDIA_WOZ=0x41, MT_IIGS_MEDIA_PRAM=0x42 };
enum { MT_INPUT_IIGS_MOUSE=0x40, MT_INPUT_IIGS_JOYSTICK=0x41 };
#define MT_SIZE_IIGS_ROM3 262144U
#define MT_SIZE_IIGS_ROM1 131072U
#define MT_SIZE_IIGS_CHARACTER 4096U
#define MT_SIZE_IIGS_ADB 4096U
#define MT_SIZE_IIGS_HDD_ROM 256U
#define MT_IIGS_CONFIG_BYTES 16U
#define MT_IIGS_SECTOR_BYTES 512U
#define MT_IIGS_DRIVE_COUNT 4U
#define MT_IIGS_PRAM_SLOT 4U
#define MT_IIGS_SPEED_COUNT 7U
/* Configuration wire bytes: ROM3, speed (0..6), card (Zip/TW/none), flags;
 * Unix timestamp LE32; pulse byte (selftest/warm/cold/save/load); seven zeros.
 * Flags disable speaker/paddle/counter delay, CPS, disable IRQ delay,
 * disable TW startup graphics/sound. Pulse bits1..4 are never persisted. */
typedef struct { uint8_t rom3, speed, card, flags; uint32_t timestamp;
    uint8_t pulses; } mt_iigs_config_t;
#endif
