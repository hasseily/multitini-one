/* Original Multitini One code, GPL-3.0-only. CPU1 builds compact parameters;
 * the common FPGA shell applies them without frame-sized ARM image passes. */
#include "multitini_video_effects.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef double matrix_t[3][3];

static const int field_min[MT_VIDEO_EFFECT_COUNT] = {
    0, -100, 0, 50, 0, -180, -100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const int field_max[MT_VIDEO_EFFECT_COUNT] = {
    1, 100, 200, 250, 200, 180, 100, 200, 200, 200, 4, 1, 8, 3, 3, 3, 3, 3, 3
};
static const int field_step[MT_VIDEO_EFFECT_COUNT] = {
    1, 5, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

void mt_video_effects_defaults(mt_video_effects_settings_t *s)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->contrast = 100U;
    s->gamma = 100U;
    s->saturation = 100U;
    s->red_gain = s->green_gain = s->blue_gain = 100U;
}

int mt_video_effects_get(const mt_video_effects_settings_t *s,
                        mt_video_effect_id_t id)
{
    if (s == NULL) return 0;
    switch (id) {
    case MT_VIDEO_EFFECT_ENABLED: return s->enabled;
    case MT_VIDEO_EFFECT_BRIGHTNESS: return s->brightness;
    case MT_VIDEO_EFFECT_CONTRAST: return s->contrast;
    case MT_VIDEO_EFFECT_GAMMA: return s->gamma;
    case MT_VIDEO_EFFECT_SATURATION: return s->saturation;
    case MT_VIDEO_EFFECT_HUE: return s->hue;
    case MT_VIDEO_EFFECT_TEMPERATURE: return s->temperature;
    case MT_VIDEO_EFFECT_RED_GAIN: return s->red_gain;
    case MT_VIDEO_EFFECT_GREEN_GAIN: return s->green_gain;
    case MT_VIDEO_EFFECT_BLUE_GAIN: return s->blue_gain;
    case MT_VIDEO_EFFECT_TINT: return s->tint;
    case MT_VIDEO_EFFECT_NEGATIVE: return s->negative;
    case MT_VIDEO_EFFECT_POSTERIZE: return s->posterize;
    case MT_VIDEO_EFFECT_SCANLINES: return s->scanlines;
    case MT_VIDEO_EFFECT_MASK: return s->mask;
    case MT_VIDEO_EFFECT_SOFTNESS: return s->softness;
    case MT_VIDEO_EFFECT_PERSISTENCE: return s->persistence;
    case MT_VIDEO_EFFECT_BLUR: return s->blur;
    case MT_VIDEO_EFFECT_BLOOM: return s->bloom;
    default: return 0;
    }
}

bool mt_video_effects_range(mt_video_effect_id_t id,
                           int *minimum, int *maximum, int *step)
{
    if ((unsigned)id >= MT_VIDEO_EFFECT_COUNT || minimum == NULL ||
        maximum == NULL || step == NULL) return false;
    *minimum = field_min[id];
    *maximum = field_max[id];
    *step = field_step[id];
    return true;
}

static bool valid_field(mt_video_effect_id_t id, int value)
{
    return (unsigned)id < MT_VIDEO_EFFECT_COUNT &&
           value >= field_min[id] && value <= field_max[id] &&
           (id != MT_VIDEO_EFFECT_POSTERIZE || value != 1);
}

bool mt_video_effects_set(mt_video_effects_settings_t *s,
                         mt_video_effect_id_t id, int value)
{
    if (s == NULL || !valid_field(id, value)) return false;
    switch (id) {
    case MT_VIDEO_EFFECT_ENABLED: s->enabled = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_BRIGHTNESS: s->brightness = (int16_t)value; break;
    case MT_VIDEO_EFFECT_CONTRAST: s->contrast = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_GAMMA: s->gamma = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_SATURATION: s->saturation = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_HUE: s->hue = (int16_t)value; break;
    case MT_VIDEO_EFFECT_TEMPERATURE: s->temperature = (int16_t)value; break;
    case MT_VIDEO_EFFECT_RED_GAIN: s->red_gain = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_GREEN_GAIN: s->green_gain = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_BLUE_GAIN: s->blue_gain = (uint16_t)value; break;
    case MT_VIDEO_EFFECT_TINT: s->tint = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_NEGATIVE: s->negative = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_POSTERIZE: s->posterize = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_SCANLINES: s->scanlines = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_MASK: s->mask = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_SOFTNESS: s->softness = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_PERSISTENCE: s->persistence = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_BLUR: s->blur = (uint8_t)value; break;
    case MT_VIDEO_EFFECT_BLOOM: s->bloom = (uint8_t)value; break;
    default: return false;
    }
    return true;
}

bool mt_video_effects_settings_valid(const mt_video_effects_settings_t *s)
{
    if (s == NULL || s->reserved != 0U || s->reserved2 != 0U) return false;
    for (unsigned i = 0; i < MT_VIDEO_EFFECT_COUNT; ++i) {
        const mt_video_effect_id_t id = (mt_video_effect_id_t)i;
        if (!valid_field(id, mt_video_effects_get(s, id))) return false;
    }
    return true;
}

/* C11 does not permit the implicit nested-array const conversion. Accept
 * storage pointers here while retaining const array access in the helper. */
static void multiply(matrix_t out, const void *a_storage, const void *b_storage)
{
    const double (*a)[3] = (const double (*)[3])a_storage;
    const double (*b)[3] = (const double (*)[3])b_storage;
    matrix_t result = {{0}};
    for (unsigned r = 0; r < 3U; ++r)
        for (unsigned c = 0; c < 3U; ++c)
            for (unsigned k = 0; k < 3U; ++k)
                result[r][c] += a[r][k] * b[k][c];
    memcpy(out, result, sizeof(result));
}

static void build_matrix(const mt_video_effects_settings_t *s,
                         mt_video_effects_tables_t *out)
{
    static const double luma[3] = {0.299, 0.587, 0.114};
    matrix_t result = {{0}};
    const double saturation = (double)s->saturation / 100.0;
    for (unsigned r = 0; r < 3U; ++r)
        for (unsigned c = 0; c < 3U; ++c)
            result[r][c] = (1.0 - saturation) * luma[c] +
                          (r == c ? saturation : 0.0);

    if (s->hue != 0) {
        /* Rotate the two chroma axes in YIQ. Computing the inverse of this
         * constant RGB->YIQ matrix avoids a rounded inverse changing gray. */
        static const matrix_t yiq = {
            {0.299, 0.587, 0.114},
            {0.595716, -0.274453, -0.321263},
            {0.211456, -0.522591, 0.311135}
        };
        matrix_t inverse, rotation = {{1.0, 0.0, 0.0}, {0}, {0}};
        matrix_t hue;
        const double angle = (double)s->hue *
                             (3.14159265358979323846 / 180.0);
        const double cosine = cos(angle), sine = sin(angle);
        const double determinant =
            yiq[0][0] * (yiq[1][1] * yiq[2][2] - yiq[1][2] * yiq[2][1]) -
            yiq[0][1] * (yiq[1][0] * yiq[2][2] - yiq[1][2] * yiq[2][0]) +
            yiq[0][2] * (yiq[1][0] * yiq[2][1] - yiq[1][1] * yiq[2][0]);
        for (unsigned r = 0; r < 3U; ++r) {
            for (unsigned c = 0; c < 3U; ++c) {
                const unsigned r1 = (c + 1U) % 3U, r2 = (c + 2U) % 3U;
                const unsigned c1 = (r + 1U) % 3U, c2 = (r + 2U) % 3U;
                inverse[r][c] = (yiq[r1][c1] * yiq[r2][c2] -
                                 yiq[r1][c2] * yiq[r2][c1]) / determinant;
            }
        }
        rotation[1][1] = rotation[2][2] = cosine;
        rotation[1][2] = -sine;
        rotation[2][1] = sine;
        multiply(hue, rotation, yiq);
        multiply(hue, inverse, hue);
        multiply(result, hue, result);
    }

    if (s->tint != MT_VIDEO_TINT_COLOR) {
        matrix_t tint;
        if (s->tint == MT_VIDEO_TINT_SEPIA) {
            static const matrix_t sepia = {
                {0.393, 0.769, 0.189},
                {0.349, 0.686, 0.168},
                {0.272, 0.534, 0.131}
            };
            memcpy(tint, sepia, sizeof(tint));
        } else {
            double gains[3] = {1.0, 1.0, 1.0};
            if (s->tint == MT_VIDEO_TINT_GREEN) {
                gains[0] = 0.18; gains[2] = 0.25;
            } else if (s->tint == MT_VIDEO_TINT_AMBER) {
                gains[1] = 0.55; gains[2] = 0.04;
            }
            for (unsigned r = 0; r < 3U; ++r)
                for (unsigned c = 0; c < 3U; ++c)
                    tint[r][c] = gains[r] * luma[c];
        }
        multiply(result, tint, result);
    }

    {
        const double warmth = (double)s->temperature / 400.0;
        const double gains[3] = {
            (double)s->red_gain / 100.0 * (1.0 + warmth),
            (double)s->green_gain / 100.0,
            (double)s->blue_gain / 100.0 * (1.0 - warmth)
        };
        for (unsigned r = 0; r < 3U; ++r) {
            for (unsigned c = 0; c < 3U; ++c) {
                const double coefficient = floor(
                    result[r][c] * gains[r] * MT_VIDEO_EFFECTS_MATRIX_ONE + 0.5);
                /* All valid controls fit comfortably in signed Q10. */
                out->matrix[r][c] = (int16_t)coefficient;
            }
        }
    }
}

bool mt_video_effects_build(const mt_video_effects_settings_t *s,
                           mt_video_effects_tables_t *tables)
{
    mt_video_effects_tables_t result;
    if (tables == NULL || !mt_video_effects_settings_valid(s)) return false;
    memset(&result, 0, sizeof(result));
    build_matrix(s, &result);
    for (unsigned i = 0; i < 256U; ++i) {
        double value = ((double)i - 127.5) * (double)s->contrast / 100.0 +
                       127.5 + (double)s->brightness * 2.55;
        unsigned quantized;
        if (value < 0.0) value = 0.0;
        if (value > 255.0) value = 255.0;
        if (s->gamma != 100U)
            value = 255.0 * pow(value / 255.0, 100.0 / (double)s->gamma);
        if (s->posterize != 0U && s->posterize != 8U) {
            const unsigned levels = (1U << s->posterize) - 1U;
            value = floor(value * (double)levels / 255.0 + 0.5) *
                    255.0 / (double)levels;
        }
        quantized = (unsigned)floor(value + 0.5);
        if (s->negative) quantized = 255U - quantized;
        for (unsigned c = 0; c < 3U; ++c)
            result.lut[c][i] = (uint8_t)quantized;
    }
    result.spatial = ((uint32_t)s->scanlines << MT_VIDEO_EFFECTS_SCANLINES_SHIFT) |
                     ((uint32_t)s->mask << MT_VIDEO_EFFECTS_MASK_SHIFT) |
                     ((uint32_t)s->softness << MT_VIDEO_EFFECTS_SOFTNESS_SHIFT) |
                     ((uint32_t)s->enabled << MT_VIDEO_EFFECTS_ENABLE_SHIFT);
    *tables = result;
    return true;
}

uint32_t mt_video_effects_word(const mt_video_effects_tables_t *t,
                             uint16_t address)
{
    if (t == NULL) return 0U;
    if (address < 9U) return (uint16_t)t->matrix[address / 3U][address % 3U];
    if (address < 12U) return (uint16_t)t->offset[address - 9U];
    if (address == 12U) return t->spatial;
    if (address >= MT_VIDEO_EFFECTS_LUT_BASE &&
        address < MT_VIDEO_EFFECTS_LUT_BASE + 256U) {
        const unsigned i = address - MT_VIDEO_EFFECTS_LUT_BASE;
        return (uint32_t)t->lut[0][i] | ((uint32_t)t->lut[1][i] << 8) |
               ((uint32_t)t->lut[2][i] << 16);
    }
    return 0U;
}

bool mt_video_effects_write(const mt_video_effects_tables_t *t,
                           uint32_t index, uint16_t *address, uint32_t *value)
{
    if (t == NULL || address == NULL || value == NULL ||
        index >= MT_VIDEO_EFFECTS_WRITE_COUNT) return false;
    *address = (uint16_t)(index < 13U ? index : MT_VIDEO_EFFECTS_LUT_BASE + index - 13U);
    *value = mt_video_effects_word(t, *address);
    return true;
}

static void color_rgb888(const mt_video_effects_tables_t *t,
                         uint16_t pixel, uint8_t out[3])
{
    const unsigned input[3] = {
        ((unsigned)pixel >> 11) * 8U + ((unsigned)pixel >> 13),
        (((unsigned)pixel >> 5) & 63U) * 4U + (((unsigned)pixel >> 9) & 3U),
        ((unsigned)pixel & 31U) * 8U + (((unsigned)pixel & 31U) >> 2)
    };
    for (unsigned r = 0; r < 3U; ++r) {
        int32_t sum = (int32_t)t->offset[r] * MT_VIDEO_EFFECTS_MATRIX_ONE;
        unsigned index;
        for (unsigned c = 0; c < 3U; ++c)
            sum += (int32_t)t->matrix[r][c] * (int32_t)input[c];
        if (sum <= 0) index = 0U;
        else if (sum >= 255 * MT_VIDEO_EFFECTS_MATRIX_ONE) index = 255U;
        else index = (unsigned)(sum + 512) >> 10;
        out[r] = t->lut[r][index];
    }
}

static uint16_t pack_rgb565(const uint8_t color[3])
{
    return (uint16_t)(((uint16_t)(color[0] & 248U) << 8) |
                      ((uint16_t)(color[1] & 252U) << 3) |
                      ((uint16_t)color[2] >> 3));
}

uint16_t mt_video_effects_reference_rgb565(const mt_video_effects_tables_t *t,
                                        uint16_t pixel)
{
    uint8_t color[3];
    if (t == NULL || (t->spatial & (1U << MT_VIDEO_EFFECTS_ENABLE_SHIFT)) == 0U)
        return pixel;
    color_rgb888(t, pixel, color);
    return pack_rgb565(color);
}

uint16_t mt_video_effects_reference_spatial_rgb565(
    const mt_video_effects_tables_t *t, uint16_t left, uint16_t center,
    uint16_t right, bool line_start, bool line_end, bool scanline_dim,
    uint32_t x, uint32_t y)
{
    uint8_t color[3], previous[3], next[3];
    unsigned filter, scanlines, mask, selected;
    if (t == NULL || (t->spatial & (1U << MT_VIDEO_EFFECTS_ENABLE_SHIFT)) == 0U)
        return center;
    color_rgb888(t, center, color);
    color_rgb888(t, line_start ? center : left, previous);
    color_rgb888(t, line_end ? center : right, next);
    filter = (t->spatial >> MT_VIDEO_EFFECTS_SOFTNESS_SHIFT) & 3U;
    scanlines = (t->spatial >> MT_VIDEO_EFFECTS_SCANLINES_SHIFT) & 3U;
    mask = (t->spatial >> MT_VIDEO_EFFECTS_MASK_SHIFT) & 3U;
    selected = (x % 3U + (mask == MT_VIDEO_MASK_SHADOW ? y & 1U : 0U)) % 3U;
    for (unsigned c = 0; c < 3U; ++c) {
        unsigned value = color[c];
        if (filter == MT_VIDEO_FILTER_SOFT) {
            value = ((unsigned)previous[c] + 6U * value + next[c] + 4U) >> 3;
        } else if (filter == MT_VIDEO_FILTER_STRONG) {
            value = ((unsigned)previous[c] + 2U * value + next[c] + 2U) >> 2;
        } else if (filter == MT_VIDEO_FILTER_SHARP) {
            const int sharpened = (int)value * 6 - previous[c] - next[c] + 2;
            value = sharpened <= 0 ? 0U : (unsigned)sharpened >> 2;
            if (value > 255U) value = 255U;
        }
        if (scanline_dim) value = (value * (4U - scanlines)) >> 2;
        if (((mask == MT_VIDEO_MASK_APERTURE || mask == MT_VIDEO_MASK_SHADOW) &&
             c != selected) ||
            (mask == MT_VIDEO_MASK_LCD && (x % 3U == 2U || y % 3U == 2U)))
            value -= value >> 2;
        color[c] = (uint8_t)value;
    }
    return pack_rgb565(color);
}

int mt_video_effects_worker_execute(const void *input, uint32_t input_size,
                            void *output, uint32_t *output_size)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_tables_t tables;
    if (input == NULL || output == NULL || output_size == NULL ||
        input_size != sizeof(settings) || *output_size < sizeof(tables)) return 1;
    memcpy(&settings, input, sizeof(settings));
    if (!mt_video_effects_build(&settings, &tables)) return 2;
    memcpy(output, &tables, sizeof(tables));
    *output_size = (uint32_t)sizeof(tables);
    return 0;
}
