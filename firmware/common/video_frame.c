/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include <stddef.h>
#include <string.h>

#if defined(__ARM_NEON) && !defined(MT_VIDEO_FRAME_SCALAR)
#include <arm_neon.h>
#define FRAME_NEON 1
#else
#define FRAME_NEON 0
#endif

/* History stays RGB888, before spatial processing. Keeping glow/blur out of
 * the feedback path avoids growing halos and retaining a stationary smear. */
static uint32_t expand565(uint16_t pixel)
{
    const uint32_t r = pixel >> 11;
    const uint32_t g = (pixel >> 5) & 63U;
    const uint32_t b = pixel & 31U;
    return ((r << 3) | (r >> 2)) |
           (((g << 2) | (g >> 4)) << 8) |
           (((b << 3) | (b >> 2)) << 16);
}

static uint16_t pack565(uint32_t pixel)
{
    return (uint16_t)(((pixel & 0xf8U) << 8) |
                      ((pixel & 0xfc00U) >> 5) |
                      ((pixel & 0xf80000U) >> 19));
}

static uint32_t decay_pixel(uint32_t pixel, unsigned strength, uint32_t steps)
{
    static const uint8_t bright[3] = {14U, 18U, 24U};
    static const uint8_t knee[3] = {40U, 48U, 56U};
    static const uint8_t tail[3] = {58U, 60U, 61U};
    uint32_t r = pixel & 255U, g = (pixel >> 8) & 255U, b = pixel >> 16;
    while (steps-- != 0U) {
        const uint32_t maximum = r > g ? (r > b ? r : b) : (g > b ? g : b);
        const uint32_t coefficient = maximum >= 128U ? bright[strength - 1U] :
                                     maximum >= 32U ? knee[strength - 1U] :
                                                      tail[strength - 1U];
        r = r * coefficient >> 6; g = g * coefficient >> 6; b = b * coefficient >> 6;
        if (r < 4U && g < 4U && b < 4U) { r = g = b = 0U; break; }
    }
    return r | (g << 8) | (b << 16);
}

static uint32_t brighter(uint32_t current, uint32_t prior)
{
    uint32_t result = 0U;
    for (unsigned shift = 0U; shift < 24U; shift += 8U) {
        const uint32_t a = (current >> shift) & 255U;
        const uint32_t b = (prior >> shift) & 255U;
        result |= (a > b ? a : b) << shift;
    }
    return result;
}

#if FRAME_NEON
static uint8x8x4_t expand8(uint16x8_t pixel)
{
    uint8x8x4_t rgb;
    const uint8x8_t r = vmovn_u16(vshrq_n_u16(pixel, 11));
    const uint8x8_t g = vmovn_u16(vandq_u16(vshrq_n_u16(pixel, 5), vdupq_n_u16(63)));
    const uint8x8_t b = vmovn_u16(vandq_u16(pixel, vdupq_n_u16(31)));
    rgb.val[0] = vorr_u8(vshl_n_u8(r, 3), vshr_n_u8(r, 2));
    rgb.val[1] = vorr_u8(vshl_n_u8(g, 2), vshr_n_u8(g, 4));
    rgb.val[2] = vorr_u8(vshl_n_u8(b, 3), vshr_n_u8(b, 2));
    rgb.val[3] = vdup_n_u8(0);
    return rgb;
}

static uint16x8_t pack8(uint8x8x4_t rgb)
{
    return vorrq_u16(vshlq_n_u16(vmovl_u8(vshr_n_u8(rgb.val[0], 3)), 11),
        vorrq_u16(vshlq_n_u16(vmovl_u8(vshr_n_u8(rgb.val[1], 2)), 5),
                  vmovl_u8(vshr_n_u8(rgb.val[2], 3))));
}

static uint8x8x4_t decay8(uint8x8x4_t old, unsigned strength, uint32_t steps)
{
    static const uint8_t bright[3] = {14U, 18U, 24U};
    static const uint8_t knee[3] = {40U, 48U, 56U};
    static const uint8_t tail[3] = {58U, 60U, 61U};
    while (steps-- != 0U) {
        const uint8x8_t maximum = vmax_u8(vmax_u8(old.val[0], old.val[1]), old.val[2]);
        const uint8x8_t coefficient = vbsl_u8(vcge_u8(maximum, vdup_n_u8(128)),
            vdup_n_u8(bright[strength - 1U]), vbsl_u8(vcge_u8(maximum, vdup_n_u8(32)),
                vdup_n_u8(knee[strength - 1U]), vdup_n_u8(tail[strength - 1U])));
        for (unsigned c = 0U; c < 3U; ++c)
            old.val[c] = vshrn_n_u16(vmull_u8(old.val[c], coefficient), 6);
        const uint8x8_t visible = vcge_u8(vmax_u8(vmax_u8(old.val[0], old.val[1]), old.val[2]),
                                         vdup_n_u8(4));
        for (unsigned c = 0U; c < 3U; ++c) old.val[c] = vand_u8(old.val[c], visible);
    }
    old.val[3] = vdup_n_u8(0);
    return old;
}

/* Kernels ported from Appletini ONE 886ea50e compositor.c.
 * PHO2 Blur rounds normalized sums. Glow floors the weighted sum once.
 * The channel order is Multitini's BBGGRR; all operations are channel local. */
static uint8x8x4_t filter8(const uint32_t *a, const uint32_t *b,
                          const uint32_t *c, const uint32_t *d,
                          const uint32_t *e, bool wide, unsigned kernel)
{
    const uint8x8x4_t va=vld4_u8((const uint8_t *)a), vb=vld4_u8((const uint8_t *)b);
    const uint8x8x4_t vc=vld4_u8((const uint8_t *)c);
    uint8x8x4_t out;
    if (kernel == 2U) return wide ? vc : vb;
    const uint8x8x4_t vd=wide ? vld4_u8((const uint8_t *)d) : vc;
    const uint8x8x4_t ve=wide ? vld4_u8((const uint8_t *)e) : vc;
    for (unsigned channel=0U; channel<3U; ++channel) {
        if (kernel == 1U) {
            if (wide) {
                uint16x8_t sum=vaddl_u8(va.val[channel],ve.val[channel]);
                sum=vmlal_u8(sum,vb.val[channel],vdup_n_u8(2));
                sum=vmlal_u8(sum,vc.val[channel],vdup_n_u8(2));
                sum=vmlal_u8(sum,vd.val[channel],vdup_n_u8(2));
                out.val[channel]=vshrn_n_u16(vaddq_u16(sum,vdupq_n_u16(4)),3);
            } else {
                uint16x8_t sum=vaddl_u8(va.val[channel],vc.val[channel]);
                sum=vmlal_u8(sum,vb.val[channel],vdup_n_u8(2));
                out.val[channel]=vshrn_n_u16(vaddq_u16(sum,vdupq_n_u16(2)),2);
            }
        } else {
            uint16x8_t sum=vaddl_u8(va.val[channel],vc.val[channel]);
            sum=vmlal_u8(sum,vb.val[channel],vdup_n_u8(2));
            out.val[channel]=vshrn_n_u16(sum,2);
        }
    }
    out.val[3]=vdup_n_u8(0);
    return out;
}

static uint8x8x4_t weaken8(uint8x8x4_t original, uint8x8x4_t filtered, unsigned level)
{
    const unsigned shift=4U-level;
    const int16x8_t left=vdupq_n_s16((int16_t)shift),right=vdupq_n_s16(-(int16_t)shift);
    const uint16x8_t bias=vdupq_n_u16((uint16_t)(1U<<(shift-1U)));
    for(unsigned channel=0U;channel<3U;++channel) {
        const uint16x8_t raw=vmovl_u8(original.val[channel]);
        uint16x8_t sum=vsubq_u16(vshlq_u16(raw,left),raw);
        sum=vaddq_u16(sum,vmovl_u8(filtered.val[channel]));
        filtered.val[channel]=vmovn_u16(vshlq_u16(vaddq_u16(sum,bias),right));
    }
    return filtered;
}
#endif

static uint32_t filter1(uint32_t a,uint32_t b,uint32_t c,
                        uint32_t d,uint32_t e,bool wide,unsigned kernel)
{
    if (kernel == 2U) return wide ? c : b;
    uint32_t output=0U;
    for (unsigned shift=0U; shift<24U; shift+=8U) {
        const unsigned av=(a>>shift)&255U,bv=(b>>shift)&255U,cv=(c>>shift)&255U;
        const unsigned value=kernel == 0U ? (av+2U*bv+cv)/4U :
            wide ? (av+2U*bv+2U*cv+2U*((d>>shift)&255U)+((e>>shift)&255U)+4U)/8U :
                   (av+2U*bv+cv+2U)/4U;
        output |= value<<shift;
    }
    return output;
}

static uint32_t weaken1(uint32_t original,uint32_t filtered,unsigned level)
{
    const unsigned bits=4U-level,denominator=1U<<bits;
    uint32_t result=0U;
    for(unsigned channel=0U;channel<24U;channel+=8U) {
        const unsigned raw=(original>>channel)&255U,value=(filtered>>channel)&255U;
        result|=((raw*(denominator-1U)+value+denominator/2U)>>bits)<<channel;
    }
    return result;
}

static uint32_t clamp_index(int32_t index, uint32_t extent)
{
    return index < 0 ? 0U : (uint32_t)index >= extent ? extent - 1U : (uint32_t)index;
}

static void horizontal(const uint32_t *input, uint32_t *output,
                       uint32_t width, uint32_t height, bool wide, unsigned kernel,
                       unsigned blend)
{
    const uint32_t radius = wide ? 2U : 1U;
    for (uint32_t y = 0U; y < height; ++y) {
        const uint32_t *const row = input + y*width;
        uint32_t x = 0U;
        while (x < width) {
#if FRAME_NEON
            if (x >= radius && x + 8U + radius <= width) {
                uint8x8x4_t value = filter8(row + x - radius, row + x - radius + 1U,
                    row + x - radius + 2U, wide ? row + x + 1U : row,
                    wide ? row + x + 2U : row, wide, kernel);
                if(blend)value=weaken8(vld4_u8((const uint8_t *)(row+x)),value,blend);
                vst4_u8((uint8_t *)(output + y*width + x), value);
                x += 8U;
                continue;
            }
#endif
            const int32_t first = (int32_t)x - (int32_t)radius;
            const uint32_t filtered = filter1(row[clamp_index(first, width)],
                row[clamp_index(first + 1, width)], row[clamp_index(first + 2, width)],
                row[clamp_index(first + 3, width)], row[clamp_index(first + 4, width)], wide, kernel);
            output[y*width+x]=blend ? weaken1(row[x],filtered,blend) : filtered;
            ++x;
        }
    }
}

static uint32_t glow(uint32_t base, uint32_t halo, unsigned strength)
{
    const unsigned shift = 4U - strength; /* Light 1/8, medium 1/4, strong 1/2. */
    uint32_t result = 0U;
    for (unsigned c = 0U; c < 24U; c += 8U) {
        const uint32_t value = ((base >> c) & 255U) + (((halo >> c) & 255U) >> shift);
        result |= (value > 255U ? 255U : value) << c;
    }
    return result;
}

/* A vertical pass can write RGB888 or fuse bloom addition and RGB565 packing.
 * Fusing the final pass avoids another full-size halo/output memory traversal. */
static void vertical(const uint32_t *input, uint32_t *rgb_output, uint16_t *output,
                     const uint32_t *base, uint32_t width, uint32_t height,
                     bool wide, unsigned bloom, unsigned kernel,unsigned blend)
{
    const int32_t radius = wide ? 2 : 1;
    for (uint32_t y = 0U; y < height; ++y) {
        const int32_t first = (int32_t)y - radius;
        const uint32_t *const a = input + clamp_index(first, height)*width;
        const uint32_t *const b = input + clamp_index(first + 1, height)*width;
        const uint32_t *const c = input + clamp_index(first + 2, height)*width;
        const uint32_t *const d = input + clamp_index(first + 3, height)*width;
        const uint32_t *const e = input + clamp_index(first + 4, height)*width;
        uint32_t x = 0U;
#if FRAME_NEON
        for (; x + 8U <= width; x += 8U) {
            uint8x8x4_t value = filter8(a+x, b+x, c+x, d+x, e+x, wide, kernel);
            if(blend)value=weaken8(vld4_u8((const uint8_t *)(base+y*width+x)),value,blend);
            if (bloom != 0U) {
                const uint8x8x4_t center = vld4_u8((const uint8_t *)(base+y*width+x));
                const int8x8_t amount = vdup_n_s8((int8_t)bloom - 4);
                for (unsigned channel = 0U; channel < 3U; ++channel)
                    value.val[channel] = vqadd_u8(center.val[channel], vshl_u8(value.val[channel], amount));
                vst1q_u16(output+y*width+x, pack8(value));
            } else {
                vst4_u8((uint8_t *)(rgb_output+y*width+x), value);
            }
        }
#endif
        for (; x < width; ++x) {
            uint32_t filtered = filter1(a[x], b[x], c[x], d[x], e[x], wide, kernel);
            if(blend)filtered=weaken1(base[y*width+x],filtered,blend);
            if (bloom != 0U) output[y*width+x] = pack565(glow(base[y*width+x], filtered, bloom));
            else rgb_output[y*width+x] = filtered;
        }
    }
}

bool mt_video_frame_process_scaled(const uint16_t *input, uint16_t *output,
                           uint32_t *history, uint32_t *base, uint32_t *scratch,
                           uint32_t width, uint32_t height,
                           uint32_t scale_num, uint32_t scale_den,
                           const mt_video_frame_options_t *options,
                           uint32_t decay_steps, bool reset_history)
{
    uint32_t i = 0U;
    if (input == NULL || output == NULL || history == NULL || base == NULL || scratch == NULL ||
        options == NULL || !mt_video_frame_geometry_valid(width, height) ||
        scale_num == 0U || scale_num > 64U || scale_den == 0U || scale_den > 64U ||
        options->enabled > 1U || options->persistence > 3U || options->blur > 3U ||
        options->bloom > 3U || options->softness > 3U)
        return false;
    const uint32_t pixels = width * height;
    /* Calibrated Appletini blur is defined in native pixels, independent of enlargement.
     * The legacy scale arguments remain validated for caller compatibility. */
    if (decay_steps > 64U) decay_steps = 64U;
    /* The maximum channel follows the same integer recurrence as the RGB
     * components. Exhausting all 256 maxima gives these exact extinction
     * bounds. After a long scheduling gap no old pixel can survive, so avoid
     * reading history or running dozens of decay rounds for every vector. */
    static const uint8_t extinction_steps[3] = {18U, 24U, 33U};
    if (options->persistence != 0U &&
        decay_steps >= extinction_steps[options->persistence - 1U])
        reset_history = true;
    if (options->enabled == 0U) {
        memmove(output, input, (size_t)pixels*sizeof(*input));
        return true;
    }
#if FRAME_NEON
    for (; i + 8U <= pixels; i += 8U) {
        uint8x8x4_t current = expand8(vld1q_u16(input+i));
        if (options->persistence != 0U && !reset_history) {
            const uint8x8x4_t prior = decay8(vld4_u8((const uint8_t *)(history+i)),
                                            options->persistence, decay_steps);
            for (unsigned c = 0U; c < 3U; ++c) current.val[c] = vmax_u8(current.val[c], prior.val[c]);
        }
        vst4_u8((uint8_t *)(history+i), current);
        vst4_u8((uint8_t *)(base+i), current);
    }
#endif
    for (; i < pixels; ++i) {
        uint32_t current = expand565(input[i]);
        if (options->persistence != 0U && !reset_history)
            current = brighter(current, decay_pixel(history[i], options->persistence, decay_steps));
        history[i] = base[i] = current;
    }
    if (options->blur != 0U) {
        horizontal(history, options->blur >= 2U ? scratch : base, width, height,
                   options->blur == 3U, width < 4U ? 2U : 1U,
                   options->blur==1U ? 1U : 0U);
        if (options->blur >= 2U)
            vertical(scratch, base, NULL, history, width, height, false, 0U, 1U,options->blur);
    }
    /* Dot bleed is emitted by the cached scanout scaler, after blur/glow.
     * Its settings byte remains in this wire format but never enters history. */
    if (options->bloom != 0U) {
        /* Every strength uses the same 3x3 tent from persisted pixels.
         * Strength changes emitted light, independently of smoothing. */
        horizontal(history, scratch, width, height, false, 0U,0U);
        vertical(scratch, NULL, output, base, width, height, false, options->bloom, 0U,0U);
    } else {
        i = 0U;
#if FRAME_NEON
        for (; i + 8U <= pixels; i += 8U)
            vst1q_u16(output+i, pack8(vld4_u8((const uint8_t *)(base+i))));
#endif
        for (; i < pixels; ++i) output[i] = pack565(base[i]);
    }
    return true;
}

bool mt_video_frame_process(const uint16_t *input, uint16_t *output,
                           uint32_t *history, uint32_t *base, uint32_t *scratch,
                           uint32_t width, uint32_t height,
                           const mt_video_frame_options_t *options,
                           uint32_t decay_steps, bool reset_history)
{
    return mt_video_frame_process_scaled(input, output, history, base, scratch,
        width, height, 3U, 2U, options, decay_steps, reset_history);
}
