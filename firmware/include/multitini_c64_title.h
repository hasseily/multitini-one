/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_TITLE_H
#define MULTITINI_C64_TITLE_H
#include "multitini_c64_features.h"
#include "multitini_c64_sid.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MT_C64_CFG_BYTES 16384U
#define MT_C64_CFG_LINE 319U
#define MT_C64_CFG_REPORTS 16U
enum
{
    MT_TITLE_TURBO = 1U << 0,
    MT_TITLE_SPEED = 1U << 1,
    MT_TITLE_BADLINE = 1U << 2,
    MT_TITLE_MEMORY = 1U << 3,
    MT_TITLE_SID = 1U << 4,
    MT_TITLE_JOYSTICK = 1U << 5,
    MT_TITLE_SCPU = 1U << 6,
    MT_TITLE_UCI = 1U << 7,
    MT_TITLE_AUTOSLOW = 1U << 8,
    MT_TITLE_CART = 1U << 9,
    MT_TITLE_FOLLOW = 1U << 10,
    MT_TITLE_ALL = (1U << 11) - 1U
};
typedef struct
{
    uint8_t detail, flags, sid_mode, joystick;
    bool turbo, reu, ua, uci, sid_custom;
    mt_c64_sid_model_t sid;
} mt_c64_title_values_t;
typedef enum
{
    MT_TITLE_NONE,
    MT_TITLE_PRG,
    MT_TITLE_CRT,
    MT_TITLE_DISK,
    MT_TITLE_MANUAL,
    MT_TITLE_UCI_FILE
} mt_c64_title_origin_t;
typedef struct
{
    mt_c64_title_values_t values;
    uint32_t owned;
    uint64_t sid_fields;
    uint8_t sid_classes;
    mt_c64_title_origin_t origin;
    char source[256];
} mt_c64_title_overlay_t;
typedef struct
{
    unsigned applied, adjusted, skipped, errors, entries, known_sections;
    uint32_t mentioned, unavailable;
    char message[MT_C64_CFG_REPORTS][96];
} mt_c64_title_report_t;
typedef struct
{
    const mt_c64_capabilities_t *caps;
    const mt_c64_title_values_t *base;
    bool ntsc, cartridge, strict;
    mt_c64_title_origin_t origin;
} mt_c64_title_parse_context_t;
void mt_c64_title_defaults(mt_c64_title_values_t *values);
/* NULL means every requested I/O SID slot has a reachable owner. */
const char *mt_c64_title_io_conflict(const mt_c64_title_values_t *values,
                                     const mt_c64_capabilities_t *caps, bool cartridge);
/* Values are normalized before individual fields are overlaid. No MMIO or
 * persistent writes occur in any portable function. */
void mt_c64_title_effective(mt_c64_title_values_t *requested, mt_c64_title_values_t *applied,
                            const mt_c64_title_values_t *stored,
                            const mt_c64_title_overlay_t *overlay, bool cartridge);
void mt_c64_title_merge(mt_c64_title_overlay_t *destination, const mt_c64_title_overlay_t *source);
bool mt_c64_title_parse(const uint8_t *data, size_t size,
                        const mt_c64_title_parse_context_t *context,
                        mt_c64_title_overlay_t *candidate, mt_c64_title_report_t *report);
bool mt_c64_title_sidecar(char *output, size_t capacity, const char *relative,
                          const char *extension);
/* Merge supported values into existing text, retaining unknown/unsupported
 * lines and comments byte for byte. Empty input creates a complete snapshot.
 * Never emits debug/session-only controls. */
bool mt_c64_title_write(const uint8_t *existing, size_t size, const mt_c64_title_values_t *values,
                        const mt_c64_capabilities_t *caps, const mt_c64_title_report_t *parsed,
                        uint8_t *output, size_t capacity, size_t *written);
#endif
