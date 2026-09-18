/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_EFFECTS_H
#define MULTITINI_VIDEO_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

/* CPU1 prepares color tables when settings change; the FPGA applies them
 * at pixel rate. Persistence, blur and bloom use a native-frame DDR service
 * before display scaling, with their settings saved in this shared set. */
enum {
    MT_VIDEO_TINT_COLOR = 0,
    MT_VIDEO_TINT_WHITE,
    MT_VIDEO_TINT_GREEN,
    MT_VIDEO_TINT_AMBER,
    MT_VIDEO_TINT_SEPIA,
    MT_VIDEO_TINT_COUNT
};

enum {
    MT_VIDEO_MASK_OFF = 0,
    MT_VIDEO_MASK_APERTURE,
    MT_VIDEO_MASK_SHADOW,
    MT_VIDEO_MASK_LCD,
    MT_VIDEO_MASK_COUNT
};

typedef enum {
    MT_VIDEO_EFFECT_ENABLED = 0,
    MT_VIDEO_EFFECT_BRIGHTNESS,
    MT_VIDEO_EFFECT_CONTRAST,
    MT_VIDEO_EFFECT_GAMMA,
    MT_VIDEO_EFFECT_SATURATION,
    MT_VIDEO_EFFECT_HUE,
    MT_VIDEO_EFFECT_TEMPERATURE,
    MT_VIDEO_EFFECT_RED_GAIN,
    MT_VIDEO_EFFECT_GREEN_GAIN,
    MT_VIDEO_EFFECT_BLUE_GAIN,
    MT_VIDEO_EFFECT_TINT,
    MT_VIDEO_EFFECT_NEGATIVE,
    MT_VIDEO_EFFECT_POSTERIZE,
    MT_VIDEO_EFFECT_SCANLINES,
    MT_VIDEO_EFFECT_MASK,
    MT_VIDEO_EFFECT_SOFTNESS,
    MT_VIDEO_EFFECT_PERSISTENCE,
    MT_VIDEO_EFFECT_BLUR,
    MT_VIDEO_EFFECT_BLOOM,
    MT_VIDEO_EFFECT_COUNT
} mt_video_effect_id_t;

enum {
    MT_VIDEO_FILTER_OFF = 0,
    MT_VIDEO_FILTER_SOFT,
    MT_VIDEO_FILTER_STRONG,
    MT_VIDEO_FILTER_SHARP
};

typedef struct {
    int16_t brightness;       /* -100..100; additive percent of full scale */
    uint16_t contrast;        /* 0..200; 100 is neutral */
    uint16_t gamma;           /* 50..250; 100 is neutral */
    uint16_t saturation;      /* 0..200; 100 is neutral */
    int16_t hue;              /* -180..180 degrees */
    int16_t temperature;      /* -100..100; positive is warmer */
    uint16_t red_gain;        /* 0..200; 100 is neutral */
    uint16_t green_gain;
    uint16_t blue_gain;
    uint8_t tint;             /* MT_VIDEO_TINT_* */
    uint8_t negative;         /* 0 or 1 */
    uint8_t posterize;        /* 0 (off), or 2..8 bits per channel */
    uint8_t scanlines;        /* 0 (off)..3 (strong) */
    uint8_t mask;             /* MT_VIDEO_MASK_* */
    uint8_t softness;         /* MT_VIDEO_FILTER_*; horizontal only */
    uint8_t enabled;          /* master switch; 0 or 1 */
    uint8_t persistence;      /* frame processing: 0 (off)..3 (strong) */
    uint8_t blur;             /* frame processing: 0 (off)..3 (strong) */
    uint8_t bloom;            /* frame processing: 0 (off)..3 (strong) */
    uint8_t reserved;         /* must be zero */
    uint8_t reserved2;        /* explicit alignment byte; must be zero */
} mt_video_effects_settings_t;

/* Color pipeline, in order:
 * 1. Replicate RGB565 high bits to obtain unsigned RGB888 channels.
 * 2. Apply signed Q10 3x3 matrix, add signed integer offsets, round with
 *    +512, shift right 10, then clamp to 0..255.
 * 3. Lookup lut[channel][value], then truncate to RGB565.
 *
 * Matrix combines hue, saturation, tint, temperature and channel gains.
 * LUT combines contrast, brightness, gamma, posterization and negative.
 * Spatial controls operate downstream; they never alter these tables.
 */
typedef struct {
    int16_t matrix[3][3];
    int16_t offset[3];
    uint8_t lut[3][256];       /* R, G, B */
    uint32_t spatial;
} mt_video_effects_tables_t;

#define MT_VIDEO_EFFECTS_MATRIX_ONE 1024
#define MT_VIDEO_EFFECTS_SCANLINES_SHIFT 0U
#define MT_VIDEO_EFFECTS_MASK_SHIFT 2U
#define MT_VIDEO_EFFECTS_SOFTNESS_SHIFT 4U
#define MT_VIDEO_EFFECTS_ENABLE_SHIFT 6U
#define MT_VIDEO_EFFECTS_WRITE_COUNT 269U
#define MT_VIDEO_EFFECTS_LUT_BASE 0x100U

void mt_video_effects_defaults(mt_video_effects_settings_t *settings);
bool mt_video_effects_settings_valid(const mt_video_effects_settings_t *settings);
int mt_video_effects_get(const mt_video_effects_settings_t *settings,
                        mt_video_effect_id_t id);
bool mt_video_effects_set(mt_video_effects_settings_t *settings,
                         mt_video_effect_id_t id, int value);
bool mt_video_effects_range(mt_video_effect_id_t id,
                           int *minimum, int *maximum, int *step);
bool mt_video_effects_build(const mt_video_effects_settings_t *settings,
                           mt_video_effects_tables_t *tables);

/* Enumerate the complete inactive FPGA bank: matrix addresses 0..8,
 * offsets 9..11, spatial control 12, and packed 0x00BBGGRR LUT words
 * 0x100..0x1ff. The caller commits only after all 269 writes succeed. */
bool mt_video_effects_write(const mt_video_effects_tables_t *tables,
                           uint32_t index, uint16_t *address, uint32_t *value);
uint32_t mt_video_effects_word(const mt_video_effects_tables_t *tables,
                             uint16_t address);

/* Portable color-only oracle, including the master bypass. Spatial effects
 * need neighboring pixels/output coordinates and are tested in the HDL. */
uint16_t mt_video_effects_reference_rgb565(const mt_video_effects_tables_t *tables,
                                        uint16_t pixel);
/* Complete streaming oracle. Neighbors are raw RGB565; endpoints repeat the
 * center. Coordinates are relative to the machine viewport. scanline_dim is
 * true for the lower half of a replicated source scanline, as chosen by the
 * core's existing scaling metadata. Filtering never samples the menu. */
uint16_t mt_video_effects_reference_spatial_rgb565(
    const mt_video_effects_tables_t *tables, uint16_t left, uint16_t center,
    uint16_t right, bool line_start, bool line_end, bool scanline_dim,
    uint32_t x, uint32_t y);

/* Shared CPU1 service dispatcher. Initial *output_size is buffer capacity. */
int mt_video_effects_worker_execute(const void *input, uint32_t input_size,
                            void *output, uint32_t *output_size);

#endif
