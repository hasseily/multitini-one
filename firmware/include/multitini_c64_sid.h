/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_SID_H
#define MULTITINI_C64_SID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MT_C64_SID_COUNT 8U
#define MT_C64_SID_WORDS 57U
#define MT_C64_SID_RECORD_BYTES 128U
#define MT_C64_SID_FOLLOW 255U
typedef enum {
    MT_SID_CUSTOM,
    MT_SID_SINGLE,
    MT_SID_DUAL420,
    MT_SID_DUAL500,
    MT_SID_TRIPLE,
    MT_SID_QUAD,
    MT_SID_EIGHT
} mt_c64_sid_preset_t;
typedef enum {
    MT_SID_UNKNOWN,
    MT_SID_APPLIED,
    MT_SID_BAD_VALUE,
    MT_SID_UNSUPPORTED
} mt_c64_sid_key_result_t;
/* Fields 0..24 are U64 group fields, 25..56 per-SID overrides, 57..59 output.
 * apply_key reports fields even for explicitly assigned unchanged values. */
typedef struct {
    uint8_t address[2], split, mirror, socket_address[2], socket_enable[2], ext_split;
    uint8_t paddle_override, model[2], digi[2], curve[2], resonance[2];
    uint8_t volume[2], pan[2], pan_from_preset[2], master;
    uint8_t sid_address[8], sid_model[8], sid_volume[8], sid_pan[8];
    uint8_t headroom, dc_remove, resample;
    uint8_t socket_volume[2], socket_pan[2];
} mt_c64_sid_model_t;
typedef struct {
    uint32_t words[MT_C64_SID_WORDS];
    uint8_t legacy_mode; /* 0/2/3, or 255 for table mode */
    uint8_t mapped, dropped, unsupported_models;
    uint16_t base[8];
    uint16_t group_base[2]; /* group A bases before optional socket allocation */
    uint8_t socket_chip[2]; /* 255 when not emulated */
} mt_c64_sid_table_t;
typedef bool (*mt_c64_sid_emit_fn)(void *context, const char *section, const char *key,
                                   const char *value);
void mt_c64_sid_default(mt_c64_sid_model_t *model);
bool mt_c64_sid_preset(mt_c64_sid_model_t *model, unsigned preset);
unsigned mt_c64_sid_preset_id(const mt_c64_sid_model_t *model);
const char *mt_c64_sid_preset_name(unsigned preset);
const char *mt_c64_sid_volume_name(unsigned index);
const char *mt_c64_sid_pan_name(unsigned index);
uint16_t mt_c64_sid_address_value(unsigned index);
bool mt_c64_sid_valid(const mt_c64_sid_model_t *model);
mt_c64_sid_key_result_t mt_c64_sid_apply_key(mt_c64_sid_model_t *model, const char *section,
                                             const char *key, const char *value, uint64_t *fields,
                                             uint8_t *classes);
void mt_c64_sid_merge(mt_c64_sid_model_t *destination, const mt_c64_sid_model_t *source,
                      uint64_t fields);
void mt_c64_sid_effective(mt_c64_sid_model_t *result, const mt_c64_sid_model_t *stored,
                          const mt_c64_sid_model_t *overlay, uint64_t fields, uint8_t classes);
bool mt_c64_sid_build(const mt_c64_sid_model_t *model, unsigned count, bool allow8580,
                      mt_c64_sid_table_t *table);
bool mt_c64_sid_encode(const mt_c64_sid_model_t *model, const char *core_id,
                       uint8_t record[MT_C64_SID_RECORD_BYTES]);
bool mt_c64_sid_decode(mt_c64_sid_model_t *model, char core_id[24],
                       const uint8_t record[MT_C64_SID_RECORD_BYTES]);
bool mt_c64_sid_export(const mt_c64_sid_model_t *model, mt_c64_sid_emit_fn emit, void *context);
size_t mt_c64_sid_hwinfo(const mt_c64_sid_table_t *table, unsigned command, uint8_t *output,
                         size_t capacity);
#endif
