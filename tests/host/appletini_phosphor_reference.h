/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact reference functions from Appletini ONE compositor.c at
 * 886ea50e7c5a98c28c7e37b5eff7548a8ea416ce. Used by host regression only.
 * Channel positions are immaterial to these channel-local spatial kernels. */
#ifndef APPLETINI_PHOSPHOR_REFERENCE_H
#define APPLETINI_PHOSPHOR_REFERENCE_H
#define APPLETINI_VIDEO_BLUR_OFF 0U
#define APPLETINI_VIDEO_BLUR_LIGHT 1U
#define APPLETINI_VIDEO_BLUR_MEDIUM 2U
#define APPLETINI_VIDEO_BLUR_STRONG 3U
static inline uint32_t effect_rgb_half(uint32_t p)
{
    return (p >> 1) & 0x007F7F7FU;
}

static inline uint32_t effect_rgb_quarter(uint32_t p)
{
    return (p >> 2) & 0x003F3F3FU;
}

static inline uint32_t effect_rgb_eighth(uint32_t p)
{
    return (p >> 3) & 0x001F1F1FU;
}

static void effect_blur_v_row(const uint32_t *up, const uint32_t *mid,
                              const uint32_t *dn, uint32_t *dst, int w,
                              int tent)
{
    int x = 0;

    if (!tent) {
        memcpy(dst, mid, (size_t)w * sizeof(uint32_t));
        return;
    }
#if defined(__ARM_NEON)
    for (; x + 4 <= w; x += 4) {
        const uint32x4_t vu = vld1q_u32(up + x);
        const uint32x4_t vm = vld1q_u32(mid + x);
        const uint32x4_t vd = vld1q_u32(dn + x);
        const uint32x4_t half = vandq_u32(vshrq_n_u32(vm, 1),
                                          vdupq_n_u32(0x007F7F7FU));
        const uint32x4_t up_q = vandq_u32(vshrq_n_u32(vu, 2),
                                          vdupq_n_u32(0x003F3F3FU));
        const uint32x4_t dn_q = vandq_u32(vshrq_n_u32(vd, 2),
                                          vdupq_n_u32(0x003F3F3FU));
        vst1q_u32(dst + x, vaddq_u32(vaddq_u32(half, up_q), dn_q));
    }
#endif
    for (; x < w; ++x) {
        dst[x] = effect_rgb_half(mid[x]) + effect_rgb_quarter(up[x]) +
                 effect_rgb_quarter(dn[x]);
    }
}

static void effect_blur_h_row(const uint32_t *src,
                              uint32_t *dst,
                              int w,
                              uint8_t blur)
{
    if (blur == APPLETINI_VIDEO_BLUR_OFF || w < 4) {
        memcpy(dst, src, (size_t)w * sizeof(uint32_t));
        return;
    }

    if (blur == APPLETINI_VIDEO_BLUR_STRONG) {
        /* Wide 1/8,1/4,1/4,1/4,1/8 tap. Edge pixels clamp. */
        int x = 0;

        for (; x < w && x < 2; ++x) {
            const uint32_t c  = src[x];
            const uint32_t l1 = (x > 0) ? src[x - 1] : c;
            const uint32_t l2 = (x > 1) ? src[x - 2] : l1;
            const uint32_t r1 = (x + 1 < w) ? src[x + 1] : c;
            const uint32_t r2 = (x + 2 < w) ? src[x + 2] : r1;
            dst[x] = effect_rgb_quarter(c) +
                     effect_rgb_quarter(l1) + effect_rgb_quarter(r1) +
                     effect_rgb_eighth(l2) + effect_rgb_eighth(r2);
        }
#if defined(__ARM_NEON)
        for (; x + 5 < w; x += 4) {
            const uint32x4_t l2 = vld1q_u32(src + x - 2);
            const uint32x4_t l1 = vld1q_u32(src + x - 1);
            const uint32x4_t c  = vld1q_u32(src + x);
            const uint32x4_t r1 = vld1q_u32(src + x + 1);
            const uint32x4_t r2 = vld1q_u32(src + x + 2);
            const uint32x4_t quarter_mask = vdupq_n_u32(0x003F3F3FU);
            const uint32x4_t eighth_mask = vdupq_n_u32(0x001F1F1FU);
            uint32x4_t out = vandq_u32(vshrq_n_u32(c, 2), quarter_mask);

            out = vaddq_u32(out,
                vandq_u32(vshrq_n_u32(l1, 2), quarter_mask));
            out = vaddq_u32(out,
                vandq_u32(vshrq_n_u32(r1, 2), quarter_mask));
            out = vaddq_u32(out,
                vandq_u32(vshrq_n_u32(l2, 3), eighth_mask));
            out = vaddq_u32(out,
                vandq_u32(vshrq_n_u32(r2, 3), eighth_mask));
            vst1q_u32(dst + x, out);
        }
#endif
        for (; x < w; ++x) {
            const uint32_t c  = src[x];
            const uint32_t l1 = src[x - 1];
            const uint32_t l2 = src[x - 2];
            const uint32_t r1 = (x + 1 < w) ? src[x + 1] : c;
            const uint32_t r2 = (x + 2 < w) ? src[x + 2] : r1;
            dst[x] = effect_rgb_quarter(c) +
                     effect_rgb_quarter(l1) + effect_rgb_quarter(r1) +
                     effect_rgb_eighth(l2) + effect_rgb_eighth(r2);
        }
        return;
    }

    /* LIGHT and MEDIUM share the 1/4,1/2,1/4 horizontal tap. */
    {
        int x = 0;

        dst[0] = effect_rgb_half(src[0]) +
                 effect_rgb_quarter(src[0]) + effect_rgb_quarter(src[1]);
        x = 1;
#if defined(__ARM_NEON)
        for (; x + 4 < w; x += 4) {
            const uint32x4_t left = vld1q_u32(src + x - 1);
            const uint32x4_t c = vld1q_u32(src + x);
            const uint32x4_t right = vld1q_u32(src + x + 1);
            const uint32x4_t half = vandq_u32(vshrq_n_u32(c, 1),
                                              vdupq_n_u32(0x007F7F7FU));
            const uint32x4_t left_q = vandq_u32(vshrq_n_u32(left, 2),
                                                vdupq_n_u32(0x003F3F3FU));
            const uint32x4_t right_q = vandq_u32(vshrq_n_u32(right, 2),
                                                 vdupq_n_u32(0x003F3F3FU));
            vst1q_u32(dst + x,
                      vaddq_u32(vaddq_u32(half, left_q), right_q));
        }
#endif
        for (; x < w; ++x) {
            const uint32_t c = src[x];
            const uint32_t left = src[x - 1];
            const uint32_t r = (x + 1 < w) ? src[x + 1] : c;
            dst[x] = effect_rgb_half(c) +
                     effect_rgb_quarter(left) + effect_rgb_quarter(r);
        }
    }
}

static inline uint32_t effect_glow_tent(uint32_t a, uint32_t b, uint32_t c)
{
    return ((((a & 0x00FF00FFU) + 2U * (b & 0x00FF00FFU) +
               (c & 0x00FF00FFU)) >> 2) & 0x00FF00FFU) |
           ((((a & 0x0000FF00U) + 2U * (b & 0x0000FF00U) +
               (c & 0x0000FF00U)) >> 2) & 0x0000FF00U);
}

static void effect_glow_h_row(const uint32_t *src, uint32_t *dst, int width)
{
    int x = 0;
#if defined(__ARM_NEON)
    for (; x + 4 <= width; x += 4) {
        const uint32x4_t c = vld1q_u32(src + x);
        const uint32x4_t a = x ? vld1q_u32(src + x - 1) :
            vextq_u32(vdupq_n_u32(src[0]), c, 3);
        const uint32x4_t b = x + 4 < width ? vld1q_u32(src + x + 1) :
            vextq_u32(c, vdupq_n_u32(src[width - 1]), 1);
        vst1q_u32(dst + x, effect_glow_tent4(a, c, b));
    }
#endif
    for (; x < width; ++x) {
        dst[x] = effect_glow_tent(src[x ? x - 1 : 0], src[x],
                                 src[x + 1 < width ? x + 1 : x]);
    }
}

static void effect_glow_v_row(const uint32_t *up, const uint32_t *mid,
                               const uint32_t *dn, uint32_t *dst, int width)
{
    int x = 0;
#if defined(__ARM_NEON)
    for (; x + 4 <= width; x += 4) {
        vst1q_u32(dst + x, effect_glow_tent4(vld1q_u32(up + x),
            vld1q_u32(mid + x), vld1q_u32(dn + x)));
    }
#endif
    for (; x < width; ++x) {
        dst[x] = effect_glow_tent(up[x], mid[x], dn[x]);
    }
}

#endif
