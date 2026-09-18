/* SPDX-License-Identifier: GPL-3.0-only */
#include "video_frame_service.h"
#include "multitini_regs.h"
#include <stddef.h>
#include <string.h>

enum { OFF, START, ACTIVE, FAILED };
static mt_video_effect_service_ops_t bus;
static uint32_t packed_options, started;
static unsigned state;
static bool running, draining, display_active, deadline_fault;

void mt_video_frame_service_init(const mt_video_effect_service_ops_t *ops)
{
    memset(&bus, 0, sizeof(bus));
    if (ops != NULL) bus = *ops;
    state = OFF; running = draining = display_active = deadline_fault = false;
}

void mt_video_frame_service_stop(void)
{
    if (running) {
        bus.write(MT_REG_VIDEO_FRAME_CONTROL, 0U);
        draining = true;
    }
    running = display_active = false;
    state = OFF;
}

bool mt_video_frame_service_quiescent(void)
{
    if (draining && (bus.read(MT_REG_VIDEO_FRAME_STATUS) & MT_VIDEO_FRAME_STATUS_QUIESCENT) != 0U)
        draining = false;
    return !draining;
}

void mt_video_frame_service_start(uint32_t next_epoch, const mt_video_frame_options_t *next)
{
    (void)next_epoch;
    if (next == NULL || bus.read == NULL || bus.write == NULL || bus.milliseconds == NULL) return;
    packed_options = next->enabled != 0U ? (uint32_t)next->persistence |
        ((uint32_t)next->blur << 2U) | ((uint32_t)next->bloom << 4U) : 0U;
    if (running && state == ACTIVE) {
        /* Hardware takes the next configuration at a frame boundary. Keep
         * capture, history, and presentation running through UI changes. */
        bus.write(MT_REG_VIDEO_FRAME_OPTIONS, packed_options);
        return;
    }
    /* Identity processing also uses the ordered hardware path. Toggling an
     * effect never changes presentation latency or skips queued frames. */
    bus.write(MT_REG_VIDEO_FRAME_CONTROL, 0U);
    draining = true;
    deadline_fault = false;
    state = START; started = bus.milliseconds();
}

bool mt_video_frame_service_failed(void)
{
    return state == FAILED;
}

const char *mt_video_frame_service_status(void)
{
    if (state == FAILED) return deadline_fault ?
        "Video deadline missed; effects stopped." : "Video transport fault; effects stopped.";
    if (state == OFF) return NULL;
    return display_active ? "60 Hz FPGA video processing; CPU1 prepares controls" :
                            "Preparing the fixed-rate video pipeline";
}

static void fail(bool deadline)
{
    mt_video_frame_service_stop();
    deadline_fault = deadline;
    state = FAILED;
}

void mt_video_frame_service_poll(void)
{
    if (state == OFF || state == FAILED) return;
    if (state == START) {
        if (!mt_video_frame_service_quiescent()) {
            if (bus.milliseconds() - started >= 1000U) fail(false);
            return;
        }
        const uint32_t geometry = bus.read(MT_REG_VIDEO_FRAME_GEOMETRY);
        const uint32_t width = geometry & UINT32_C(0xffff), height = geometry >> 16;
        const uint32_t stride = bus.read(MT_REG_VIDEO_FRAME_STRIDE);
        if ((bus.read(MT_REG_VIDEO_FRAME_STATUS) & MT_VIDEO_FRAME_STATUS_AUTONOMOUS_SUPPORTED) == 0U ||
            width == 0U || width > MT_VIDEO_FRAME_MAX_WIDTH || height == 0U ||
            height > MT_VIDEO_FRAME_MAX_HEIGHT || stride != ((width * 2U + 127U) & ~UINT32_C(127)) ||
            stride * height > MT_VIDEO_FRAME_SLOT_BYTES) { fail(false); return; }
        bus.write(MT_REG_VIDEO_FRAME_ERROR_CLEAR, 1U);
        bus.write(MT_REG_VIDEO_FRAME_OPTIONS, packed_options);
        bus.write(MT_REG_VIDEO_FRAME_CONTROL, MT_VIDEO_FRAME_CAPTURE_ENABLE |
            MT_VIDEO_FRAME_DISPLAY_ENABLE | MT_VIDEO_FRAME_AUTONOMOUS_ENABLE);
        running = true; state = ACTIVE;
        return;
    }
    const uint32_t status = bus.read(MT_REG_VIDEO_FRAME_STATUS);
    display_active = (status & MT_VIDEO_FRAME_STATUS_DISPLAY_ACTIVE) != 0U;
    const bool deadline = bus.read(MT_REG_VIDEO_FRAME_DEADLINES) != 0U;
    if ((status & MT_VIDEO_FRAME_STATUS_ERROR) != 0U || deadline ||
        bus.read(MT_REG_VIDEO_FRAME_CAPTURE_DROPS) != 0U ||
        bus.read(MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS) != 0U)
        fail(deadline);
}
