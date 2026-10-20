/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_INTERFACE_H
#define MULTITINI_IIGS_INTERFACE_H
#include <stdint.h>
#define MT_IIGS_CORE_ID UINT32_C(0x49494753)
enum { MT_LOAD_IIGS_ROM3=0x40, MT_LOAD_IIGS_ROM1=0x41,
    MT_LOAD_IIGS_CHARACTER=0x42, MT_LOAD_IIGS_CONFIG=0x43,
    MT_LOAD_IIGS_ADB3=0x44, MT_LOAD_IIGS_HDD_ROM_LEGACY=0x45, MT_LOAD_IIGS_ADB1=0x46,
    MT_LOAD_IIGS_PRAM=0x47, MT_LOAD_IIGS_SECTOR=0x48, MT_LOAD_IIGS_HDD_ROM=0x49 };
enum { MT_IIGS_MEDIA_BLOCK=0x40, MT_IIGS_MEDIA_WOZ=0x41, MT_IIGS_MEDIA_PRAM=0x42,
    MT_IIGS_MEDIA_BLOCK32=0x43 };
enum { MT_INPUT_IIGS_MOUSE=0x40, MT_INPUT_IIGS_JOYSTICK=0x41 };
#define MT_SIZE_IIGS_ROM3 262144U
#define MT_SIZE_IIGS_ROM1 131072U
#define MT_SIZE_IIGS_CHARACTER 4096U
#define MT_SIZE_IIGS_ADB 4096U
#define MT_SIZE_IIGS_HDD_ROM 2304U
#define MT_IIGS_CONFIG_BYTES 16U
#define MT_IIGS_SECTOR_BYTES 512U
#define MT_IIGS_HDD_COUNT 8U
#define MT_IIGS_DRIVE_COUNT 10U
#define MT_IIGS_FLOPPY35_DRIVE 2U
#define MT_IIGS_FLOPPY525_DRIVE 3U
#define MT_IIGS_PRAM_SLOT 4U
#define MT_IIGS_MEDIA_SLOT_COUNT 11U
#define MT_IIGS_MAX_HDD_BYTES (UINT64_C(512) * UINT32_MAX)
/* Saved drive indices 0/1 and 2/3 retain their HDD/floppy meanings. Extra
 * hard disks use indices4..9; wire slot4 remains the private PRAM device. */
static inline unsigned mt_iigs_hdd_drive(unsigned unit)
{ return unit<2U ? unit:unit<MT_IIGS_HDD_COUNT ? unit+2U:MT_IIGS_DRIVE_COUNT; }
static inline unsigned mt_iigs_drive_hdd_unit(unsigned drive)
{ return drive<2U ? drive:drive>=4U && drive<MT_IIGS_DRIVE_COUNT ? drive-2U:MT_IIGS_HDD_COUNT; }
static inline unsigned mt_iigs_drive_wire_slot(unsigned drive)
{ return drive<4U ? drive:drive<MT_IIGS_DRIVE_COUNT ? drive+1U:MT_IIGS_MEDIA_SLOT_COUNT; }
static inline unsigned mt_iigs_wire_slot_drive(unsigned slot)
{ return slot<4U ? slot:slot>MT_IIGS_PRAM_SLOT && slot<MT_IIGS_MEDIA_SLOT_COUNT ? slot-1U:MT_IIGS_DRIVE_COUNT; }
#define MT_IIGS_SPEED_COUNT 7U
#define MT_IIGS_FLAG_FAST_FLOPPY UINT8_C(0x80)
/* Configuration wire bytes: ROM3, speed (0..6), card (Zip/TW/none), flags;
 * Unix timestamp LE32; pulse byte (selftest/warm/cold/save/load); seven zeros.
 * Flags disable speaker/paddle/counter delay, CPS, disable IRQ delay,
 * disable TW startup graphics/sound, Fast floppy. Fast floppy defaults Off,
 * requires MT_IIGS_FAST_FLOPPY_SUPPORTED and changes only through a restart.
 * Pulse bits1..4 are never persisted. */
typedef struct { uint8_t rom3, speed, card, flags; uint32_t timestamp;
    uint8_t pulses; } mt_iigs_config_t;
#endif
