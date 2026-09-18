/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_effects.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 248U) << 8) | ((g & 252U) << 3) | (b >> 3));
}

static void test_identity(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_tables_t tables;
    mt_video_effects_defaults(&settings);
    check(sizeof(settings) <= 64U && sizeof(tables) <= 4096U,
          "worker input and output fit bounded mailboxes");
    check(mt_video_effects_settings_valid(&settings) && settings.enabled == 0U,
          "default settings are valid and master bypass is selected");
    check(mt_video_effects_build(&settings, &tables), "default tables build");
    for (unsigned r = 0; r < 3U; ++r) {
        for (unsigned c = 0; c < 3U; ++c)
            check(tables.matrix[r][c] == (r == c ? 1024 : 0),
                  "neutral matrix is exactly Q10 identity");
        for (unsigned i = 0; i < 256U; ++i)
            check(tables.lut[r][i] == i, "neutral LUT is exact identity");
        check(tables.offset[r] == 0, "reserved offsets stay zero");
    }
    settings.enabled = 1U;
    check(mt_video_effects_build(&settings, &tables), "enabled neutral build");
    for (unsigned pixel = 0; pixel <= UINT16_MAX; ++pixel)
        check(mt_video_effects_reference_rgb565(&tables, (uint16_t)pixel) == pixel,
              "all 65536 RGB565 colors retain their exact bits");
    settings.negative = 1U;
    settings.enabled = 0U;
    check(mt_video_effects_build(&settings, &tables), "disabled negative build");
    for (unsigned pixel = 0; pixel <= UINT16_MAX; ++pixel)
        check(mt_video_effects_reference_rgb565(&tables, (uint16_t)pixel) == pixel,
              "master bypass ignores configured color effects");
}

static void test_validation_and_fields(void)
{
    mt_video_effects_settings_t settings, saved;
    mt_video_effects_tables_t tables, sentinel;
    mt_video_effects_defaults(&settings);
    for (unsigned i = 0; i < MT_VIDEO_EFFECT_COUNT; ++i) {
        int low, high, step;
        const mt_video_effect_id_t id = (mt_video_effect_id_t)i;
        check(mt_video_effects_range(id, &low, &high, &step) && step > 0,
              "every UI field exposes a valid range");
        saved = settings;
        check(!mt_video_effects_set(&settings, id, low - 1) &&
              !mt_video_effects_set(&settings, id, high + 1) &&
              memcmp(&saved, &settings, sizeof(settings)) == 0,
              "out-of-range UI writes do not change settings");
        check(mt_video_effects_set(&settings, id, low) &&
              mt_video_effects_get(&settings, id) == low &&
              mt_video_effects_settings_valid(&settings), "minimum roundtrips");
        check(mt_video_effects_set(&settings, id, high) &&
              mt_video_effects_get(&settings, id) == high &&
              mt_video_effects_settings_valid(&settings), "maximum roundtrips");
        settings = saved;
    }
    check(!mt_video_effects_set(&settings, MT_VIDEO_EFFECT_POSTERIZE, 1),
          "unsupported one-bit posterization rejects rather than silently clamp");
    check(!mt_video_effects_set(&settings, (mt_video_effect_id_t)-1, 0) &&
          !mt_video_effects_set(&settings, MT_VIDEO_EFFECT_COUNT, 0),
          "unknown field IDs reject");
    memset(&tables, 0xA5, sizeof(tables));
    sentinel = tables;
    settings.reserved = 1U;
    check(!mt_video_effects_build(&settings, &tables) &&
          memcmp(&tables, &sentinel, sizeof(tables)) == 0,
          "invalid settings preserve the previous complete table");
    check(!mt_video_effects_build(NULL, &tables) &&
          !mt_video_effects_build(&settings, NULL), "null build arguments reject");
}

static void test_tone(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_tables_t t;
    mt_video_effects_defaults(&s);
    s.enabled = 1U;
    s.negative = 1U;
    check(mt_video_effects_build(&s, &t), "negative build");
    for (unsigned pixel = 0; pixel <= UINT16_MAX; ++pixel)
        check(mt_video_effects_reference_rgb565(&t, (uint16_t)pixel) ==
              (uint16_t)(pixel ^ UINT16_MAX), "negative complements every color");
    s.negative = 0U;
    s.brightness = 100;
    check(mt_video_effects_build(&s, &t), "maximum brightness build");
    check(mt_video_effects_reference_rgb565(&t, 0U) == 0xFFFFU,
          "maximum brightness reaches white from black");
    s.brightness = -100;
    check(mt_video_effects_build(&s, &t), "minimum brightness build");
    check(mt_video_effects_reference_rgb565(&t, 0xFFFFU) == 0U,
          "minimum brightness reaches black from white");
    s.brightness = 0;
    s.contrast = 0;
    check(mt_video_effects_build(&s, &t), "zero contrast build");
    for (unsigned i = 0; i < 256U; ++i)
        check(t.lut[0][i] == 128U, "zero contrast is middle gray");
    s.contrast = 100U;
    s.gamma = 200U;
    check(mt_video_effects_build(&s, &t), "gamma build");
    check(t.lut[0][0] == 0U && t.lut[0][255] == 255U && t.lut[0][64] == 128U,
          "gamma preserves endpoints and raises quarter input to half");
    for (unsigned i = 1; i < 256U; ++i)
        check(t.lut[0][i] >= t.lut[0][i - 1U], "gamma LUT is monotonic");
    s.gamma = 100U;
    s.posterize = 2U;
    check(mt_video_effects_build(&s, &t), "posterization build");
    for (unsigned i = 0; i < 256U; ++i)
        check(t.lut[0][i] % 85U == 0U, "two-bit posterization has four exact levels");
    s.posterize = 8U;
    check(mt_video_effects_build(&s, &t), "eight-bit posterization build");
    for (unsigned i = 0; i < 256U; ++i)
        check(t.lut[0][i] == i, "eight-bit posterization retains all levels");
}

static void test_color(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_tables_t t;
    mt_video_effects_defaults(&s);
    s.enabled = 1U;
    s.saturation = 0U;
    check(mt_video_effects_build(&s, &t), "zero saturation build");
    check(mt_video_effects_reference_rgb565(&t, 0xF800U) == rgb565(76U, 76U, 76U) &&
          mt_video_effects_reference_rgb565(&t, 0x07E0U) == rgb565(150U, 150U, 150U) &&
          mt_video_effects_reference_rgb565(&t, 0x001FU) == rgb565(29U, 29U, 29U),
          "grayscale uses known luminance weights on primary colors");
    s.saturation = 100U;
    for (unsigned hue = 0U; hue <= 360U; hue += 5U) {
        s.hue = (int16_t)((int)hue - 180);
        check(mt_video_effects_build(&s, &t), "hue range builds");
        check(mt_video_effects_reference_rgb565(&t, 0xFFFFU) == 0xFFFFU &&
              mt_video_effects_reference_rgb565(&t, 0U) == 0U,
              "hue rotation preserves neutral endpoints");
    }
    s.hue = 0;
    s.tint = MT_VIDEO_TINT_GREEN;
    check(mt_video_effects_build(&s, &t), "green tint build");
    check(mt_video_effects_reference_rgb565(&t, 0xFFFFU) == rgb565(46U, 255U, 64U),
          "green tint maps full luminance to the intended phosphor color");
    s.tint = MT_VIDEO_TINT_AMBER;
    check(mt_video_effects_build(&s, &t), "amber tint build");
    check(mt_video_effects_reference_rgb565(&t, 0xFFFFU) == rgb565(255U, 140U, 10U),
          "amber tint maps full luminance to the intended phosphor color");
    s.tint = MT_VIDEO_TINT_COLOR;
    s.red_gain = 0U;
    check(mt_video_effects_build(&s, &t), "zero red gain build");
    check(mt_video_effects_reference_rgb565(&t, 0xF800U) == 0U &&
          mt_video_effects_reference_rgb565(&t, 0x07FFU) == 0x07FFU,
          "channel gain affects only its output channel");
    s.red_gain = 100U;
    s.temperature = 100;
    check(mt_video_effects_build(&s, &t), "warmth build");
    check(t.matrix[0][0] == 1280 && t.matrix[1][1] == 1024 &&
          t.matrix[2][2] == 768, "warmth uses opposite red and blue gains");
}

static void test_spatial(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_tables_t t;
    mt_video_effects_defaults(&s);
    s.enabled = 1U;
    for (unsigned filter = 0; filter < 4U; ++filter) {
        s.softness = (uint8_t)filter;
        check(mt_video_effects_build(&s, &t), "spatial mode builds");
        for (unsigned pixel = 0; pixel <= UINT16_MAX; ++pixel) {
            const uint16_t p = (uint16_t)pixel;
            check(mt_video_effects_reference_spatial_rgb565(
                      &t, p, p, p, false, false, false, 0U, 0U) == p,
                  "all spatial filters preserve every constant color");
        }
        check(mt_video_effects_reference_spatial_rgb565(
                  &t, 0U, 0xFFFFU, 0U, true, true, false, 0U, 0U) == 0xFFFFU,
              "single-pixel viewport replicates both boundaries");
    }
    s.softness = MT_VIDEO_FILTER_STRONG;
    check(mt_video_effects_build(&s, &t), "strong filter build");
    check(mt_video_effects_reference_spatial_rgb565(
              &t, 0U, 0xFFFFU, 0U, false, false, false, 0U, 0U) == rgb565(128U, 128U, 128U),
          "strong filter has symmetric quarter-half-quarter weights");
    s.softness = MT_VIDEO_FILTER_SHARP;
    check(mt_video_effects_build(&s, &t), "sharp filter build");
    check(mt_video_effects_reference_spatial_rgb565(
              &t, 0xFFFFU, 0U, 0xFFFFU, false, false, false, 0U, 0U) == 0U,
          "sharpen clips negative values instead of wrapping");
    s.softness = MT_VIDEO_FILTER_OFF;
    for (unsigned strength = 1U; strength <= 3U; ++strength) {
        const unsigned expected = 255U * (4U - strength) / 4U;
        s.scanlines = (uint8_t)strength;
        check(mt_video_effects_build(&s, &t), "scanline strength build");
        check(mt_video_effects_reference_spatial_rgb565(
                  &t, 0U, 0xFFFFU, 0U, true, true, true, 0U, 0U) ==
                  rgb565(expected, expected, expected), "scanline attenuation exact");
    }
    s.scanlines = 0U;
    s.mask = MT_VIDEO_MASK_APERTURE;
    check(mt_video_effects_build(&s, &t), "aperture mask build");
    check(mt_video_effects_reference_spatial_rgb565(
              &t, 0U, 0xFFFFU, 0U, true, true, false, 0U, 0U) == rgb565(255U, 192U, 192U),
          "aperture first column selects red");
    s.mask = MT_VIDEO_MASK_SHADOW;
    check(mt_video_effects_build(&s, &t), "shadow mask build");
    check(mt_video_effects_reference_spatial_rgb565(
              &t, 0U, 0xFFFFU, 0U, true, true, false, 0U, 1U) == rgb565(192U, 255U, 192U),
          "shadow mask staggers alternate rows");
    s.mask = MT_VIDEO_MASK_LCD;
    check(mt_video_effects_build(&s, &t), "LCD mask build");
    check(mt_video_effects_reference_spatial_rgb565(
              &t, 0U, 0xFFFFU, 0U, true, true, false, 2U, 0U) == rgb565(192U, 192U, 192U),
          "LCD grid attenuates each third row or column");
}

static void test_transport(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_tables_t t, received;
    uint32_t size = sizeof(received);
    mt_video_effects_defaults(&s);
    s.hue = 45;
    s.enabled = 1U;
    s.mask = MT_VIDEO_MASK_SHADOW;
    s.scanlines = 3U;
    s.softness = MT_VIDEO_FILTER_SHARP;
    check(mt_video_effects_build(&s, &t), "transport table builds");
    check(mt_video_effects_worker_execute(&s, sizeof(s), &received, &size) == 0 &&
          size == sizeof(received) && memcmp(&t, &received, sizeof(t)) == 0,
          "CPU1 dispatcher returns complete identical output");
    for (unsigned index = 0; index < MT_VIDEO_EFFECTS_WRITE_COUNT; ++index) {
        uint16_t address;
        uint32_t value;
        check(mt_video_effects_write(&t, index, &address, &value),
              "every required inactive-bank write is enumerated");
        check(address == (index < 13U ? index : index - 13U + 0x100U),
              "wire addresses are stable and complete");
        if (address < 9U)
            check((value & 0xFFFF0000U) == 0U &&
                  (int16_t)(uint16_t)value == t.matrix[address / 3U][address % 3U],
                  "signed coefficients occupy only the low sixteen bits");
        else if (address < 12U) check(value == 0U, "reserved offsets write zero");
        else if (address == 12U) check(value == 123U, "runtime controls use agreed bit packing");
        else {
            const unsigned entry = address - 0x100U;
            check(value == ((uint32_t)t.lut[0][entry] |
                            ((uint32_t)t.lut[1][entry] << 8) |
                            ((uint32_t)t.lut[2][entry] << 16)),
                  "wire LUT packs red in low byte then green then blue");
        }
    }
    {
        uint16_t address = 0xA55AU;
        uint32_t value = 0xA55AA55AU;
        check(!mt_video_effects_write(&t, MT_VIDEO_EFFECTS_WRITE_COUNT, &address, &value) &&
              address == 0xA55AU && value == 0xA55AA55AU,
              "out-of-range transport write preserves output arguments");
    }
    memset(&received, 0xA5, sizeof(received));
    size = sizeof(received) - 1U;
    check(mt_video_effects_worker_execute(&s, sizeof(s), &received, &size) != 0 &&
          ((const unsigned char *)&received)[0] == 0xA5U,
          "undersized worker output is not overwritten");
    size = sizeof(received);
    check(mt_video_effects_worker_execute(&s, sizeof(s) - 1U, &received, &size) != 0,
          "worker rejects truncated settings");
    s.gamma = 0U;
    check(mt_video_effects_worker_execute(&s, sizeof(s), &received, &size) != 0 &&
          ((const unsigned char *)&received)[0] == 0xA5U,
          "worker failure preserves prior output");
}

static void test_extreme_combinations(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_tables_t t;
    mt_video_effects_defaults(&s);
    s.enabled = 1U;
    s.red_gain = s.green_gain = s.blue_gain = 200U;
    for (unsigned tint = 0; tint < MT_VIDEO_TINT_COUNT; ++tint) {
        s.tint = (uint8_t)tint;
        for (int temperature = -100; temperature <= 100; temperature += 100) {
            s.temperature = (int16_t)temperature;
            for (unsigned saturation = 0; saturation <= 200U; saturation += 100U) {
                s.saturation = (uint16_t)saturation;
                for (int hue = -180; hue <= 180; hue += 5) {
                    s.hue = (int16_t)hue;
                    check(mt_video_effects_build(&s, &t), "extreme matrix combination builds");
                    for (unsigned r = 0; r < 3U; ++r)
                        for (unsigned c = 0; c < 3U; ++c)
                            check(t.matrix[r][c] > -16384 && t.matrix[r][c] < 16384,
                                  "coefficients retain signed hardware headroom");
                    check(mt_video_effects_reference_rgb565(&t, 0U) == 0U,
                          "every linear color combination preserves black");
                }
            }
        }
    }
}

static void emit_vectors(void)
{
    enum { WIDTH = 33, HEIGHT = 8, SCENES = 24 };
    for (unsigned scene = 0; scene < SCENES; ++scene) {
        mt_video_effects_settings_t s;
        mt_video_effects_tables_t t;
        mt_video_effects_defaults(&s);
        s.enabled = 1U;
        switch (scene) {
        case 1: s.enabled = 0U; s.negative = 1U; break;
        case 2: s.hue = 90; break;
        case 3: s.hue = -90; break;
        case 4: s.gamma = 50U; break;
        case 5: s.gamma = 250U; break;
        case 6: s.brightness = -40; break;
        case 7: s.contrast = 200U; break;
        case 8: s.saturation = 0U; break;
        case 9: s.saturation = 200U; break;
        case 10: s.temperature = -100; break;
        case 11: s.temperature = 100; break;
        case 12: s.red_gain = 0U; s.green_gain = 200U; s.blue_gain = 50U; break;
        case 13: s.tint = MT_VIDEO_TINT_WHITE; break;
        case 14: s.tint = MT_VIDEO_TINT_GREEN; break;
        case 15: s.tint = MT_VIDEO_TINT_AMBER; break;
        case 16: s.tint = MT_VIDEO_TINT_SEPIA; break;
        case 17: s.negative = 1U; break;
        case 18: s.posterize = 2U; break;
        case 19: s.posterize = 5U; break;
        case 20: s.softness = MT_VIDEO_FILTER_SOFT; break;
        case 21: s.softness = MT_VIDEO_FILTER_STRONG; break;
        case 22: s.softness = MT_VIDEO_FILTER_SHARP; break;
        case 23:
            s.gamma = 150U; s.brightness = 15; s.contrast = 125U;
            s.saturation = 155U; s.hue = -35; s.temperature = 40;
            s.red_gain = 115U; s.green_gain = 90U; s.blue_gain = 130U;
            s.tint = MT_VIDEO_TINT_SEPIA; s.negative = 1U;
            s.posterize = 5U; s.scanlines = 3U;
            s.mask = MT_VIDEO_MASK_SHADOW; s.softness = MT_VIDEO_FILTER_SHARP;
            break;
        default: break;
        }
        check(mt_video_effects_build(&s, &t), "HDL vector scene builds");
        for (unsigned i = 0; i < MT_VIDEO_EFFECTS_WRITE_COUNT; ++i) {
            uint16_t address;
            uint32_t value;
            check(mt_video_effects_write(&t, i, &address, &value), "HDL bank write");
            printf("W %03x %08x\n", (unsigned)address, (unsigned)value);
        }
        printf("C %08x\n", scene + 1U);
        for (unsigned y = 0; y < HEIGHT; ++y) {
            uint16_t pixels[WIDTH];
            bool enabled[WIDTH];
            for (unsigned x = 0; x < WIDTH; ++x) {
                pixels[x] = (uint16_t)((x * 7919U + y * 371U + scene * 1877U) & 0xFFFFU);
                enabled[x] = x != 0U && x != WIDTH - 1U && x != 15U && x != 16U;
            }
            for (unsigned x = 0; x < WIDTH; ++x) {
                uint16_t expected = pixels[x];
                if (enabled[x]) {
                    const bool first = x == 0U || !enabled[x - 1U];
                    const bool last = x + 1U == WIDTH || !enabled[x + 1U];
                    expected = mt_video_effects_reference_spatial_rgb565(
                        &t, first ? pixels[x] : pixels[x - 1U], pixels[x],
                        last ? pixels[x] : pixels[x + 1U], first, last,
                        y % 4U >= 2U, x, y);
                }
                printf("P %04x %u %u %u %04x\n", (unsigned)pixels[x],
                       enabled[x] ? 1U : 0U, x, y, (unsigned)expected);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--vectors") == 0) {
        emit_vectors();
        return 0;
    }
    test_identity();
    test_validation_and_fields();
    test_tone();
    test_color();
    test_spatial();
    test_transport();
    test_extreme_combinations();
    puts("video effects: color, spatial, CPU1 transport and boundary checks passed");
    return 0;
}
