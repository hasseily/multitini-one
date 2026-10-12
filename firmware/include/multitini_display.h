/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_DISPLAY_H
#define MULTITINI_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#define MT_DISPLAY_MODE_COUNT 6U
#define MT_DISPLAY_MIN_ABI_MINOR 16U
#define MT_DISPLAY_SCALE_MIN_ABI_MINOR 21U
#define MT_DISPLAY_SCALE_MAX 4U
#define MT_DISPLAY_SCALE_SHIFT 4U
#define MT_DISPLAY_SCALE_MASK UINT32_C(0x70)
#define MT_DISPLAY_BUSY (UINT32_C(1)<<8)
#define MT_DISPLAY_ERROR (UINT32_C(1)<<9)
#define MT_DISPLAY_HELD (UINT32_C(1)<<10)
typedef struct {
    uint32_t (*read)(uint32_t offset);
    void (*write)(uint32_t offset, uint32_t value);
    uint32_t (*milliseconds)(void);
} mt_display_ops_t;
typedef enum {
    MT_DISPLAY_OK=0, MT_DISPLAY_UNSUPPORTED, MT_DISPLAY_INVALID,
    MT_DISPLAY_IN_PROGRESS, MT_DISPLAY_FAILED, MT_DISPLAY_TIMEOUT
} mt_display_result_t;
const char *mt_display_mode_name(uint8_t mode);
/* Zero is Auto; manual values are the native-pixel integer multiplier. */
const char *mt_display_scale_name(uint8_t multiplier);
bool mt_display_supported(const mt_display_ops_t *ops);
bool mt_display_scale_supported(const mt_display_ops_t *ops);
/* Bounded synchronous transaction. No runtime/USB/file callbacks are invoked.
 * Success means geometry and one frame have committed, not monitor detection. */
mt_display_result_t mt_display_apply(const mt_display_ops_t *ops, uint8_t mode);
mt_display_result_t mt_display_apply_scale(const mt_display_ops_t *ops, uint8_t multiplier);
#endif
