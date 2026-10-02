/* SPDX-License-Identifier: GPL-3.0-only */
#include "video_effect_service.h"
#include "video_worker.h"
#include "video_frame_service.h"
#include "multitini_regs.h"
#include <string.h>

enum { WAIT_COMPUTE, UPLOAD, WAIT_COMMIT, APPLIED, FAULT };
static mt_video_effect_service_ops_t bus;
static mt_video_effects_settings_t desired;
static mt_video_effects_tables_t tables;
static uint32_t epoch, submitted_epoch, sequence, sent_sequence, cursor, started;
static unsigned phase;
static bool compatible;

void mt_video_effect_service_init(const mt_video_effect_service_ops_t *ops)
{
    memset(&bus, 0, sizeof(bus));
    if (ops != NULL) bus = *ops;
    mt_video_frame_service_init(ops);
    compatible = false; epoch = submitted_epoch = sequence = 0U;
    phase = WAIT_COMPUTE;
    mt_video_effects_defaults(&desired);
}

void mt_video_effect_service_suspend(void)
{
    mt_video_frame_service_stop();
    compatible = false;
    ++epoch;
    if (epoch == 0U) ++epoch;
    phase = WAIT_COMPUTE;
}

bool mt_video_effect_service_quiescent(void)
{
    return mt_video_frame_service_quiescent();
}

void mt_video_effect_service_configure(bool supported,
    const mt_video_effects_settings_t *settings)
{
    /* A live control change must not stop the autonomous frame schedule. */
    if (!supported || !compatible) mt_video_effect_service_suspend();
    else {
        ++epoch;
        if (epoch == 0U) ++epoch;
        phase = WAIT_COMPUTE;
    }
    if (settings != NULL && mt_video_effects_settings_valid(settings)) desired = *settings;
    else mt_video_effects_defaults(&desired);
    compatible = supported && bus.read != NULL && bus.write != NULL && bus.milliseconds != NULL;
    started = compatible ? bus.milliseconds() : 0U;
}

bool mt_video_effect_service_available(void)
{
    const mt_video_worker_state_t state = mt_video_worker_status().state;
    return compatible && state != MT_VIDEO_WORKER_STOPPED && state != MT_VIDEO_WORKER_STARTING &&
           state != MT_VIDEO_WORKER_FAILED && phase != FAULT;
}

const char *mt_video_effect_service_status(void)
{
    const mt_video_worker_state_t state = mt_video_worker_status().state;
    if (!compatible) return "Update the core package to use these video effects.";
    /* A stopped frame pipeline takes precedence over a separate control
     * update. Never claim that video remains active after a transport fault. */
    if (mt_video_frame_service_failed()) return mt_video_frame_service_status();
    if (state == MT_VIDEO_WORKER_STOPPED || state == MT_VIDEO_WORKER_FAILED)
        return "CPU1 unavailable. The previous complete video configuration remains active.";
    if (state == MT_VIDEO_WORKER_STARTING) return "CPU1 is starting.";
    if (phase == FAULT) return "Video update failed. The previous complete configuration remains active.";
    if (phase != APPLIED) return "Applying effects at the next complete display frame.";
    if (mt_video_frame_service_status() != NULL) return mt_video_frame_service_status();
    return desired.enabled != 0U ? "FPGA effects active; CPU1 ready" : "CPU1 ready; video effects off";
}

bool mt_video_effect_service_snapshot(mt_video_effects_tables_t *snapshot)
{
    if(snapshot == NULL || !compatible || phase != APPLIED ||
       (bus.read(MT_REG_VIDEO_EFFECT_STATUS) &
        (MT_VIDEO_EFFECT_STATUS_BUSY | MT_VIDEO_EFFECT_STATUS_VALID)) != MT_VIDEO_EFFECT_STATUS_VALID ||
       bus.read(MT_REG_VIDEO_EFFECT_SEQUENCE) != sent_sequence) return false;
    *snapshot=tables;
    return true;
}

void mt_video_effect_service_poll(void)
{
    mt_video_worker_status_t worker;
    uint32_t status;
    if (!compatible) return;
    mt_video_frame_service_poll();
    if (phase == FAULT || phase == APPLIED) return;
    worker = mt_video_worker_status();
    if (worker.state == MT_VIDEO_WORKER_FAILED || worker.state == MT_VIDEO_WORKER_STOPPED ||
        bus.milliseconds() - started >= 3000U) { phase = FAULT; return; }
    if (phase == WAIT_COMPUTE) {
        if (submitted_epoch == epoch && worker.state == MT_VIDEO_WORKER_COMPLETE) {
            uint32_t size = 0U;
            if (!mt_video_worker_collect(epoch, &tables, (uint32_t)sizeof(tables), &size) ||
                size != sizeof(tables)) { phase = FAULT; return; }
            phase = UPLOAD; cursor = 0U;
        } else if (submitted_epoch != epoch &&
                   (worker.state == MT_VIDEO_WORKER_IDLE || worker.state == MT_VIDEO_WORKER_COMPLETE)) {
            if (mt_video_worker_submit(epoch, &desired, (uint32_t)sizeof(desired)))
                submitted_epoch = epoch;
        }
        return;
    }
    status = bus.read(MT_REG_VIDEO_EFFECT_STATUS);
    if ((status & MT_VIDEO_EFFECT_STATUS_SUPPORTED) == 0U) { phase = FAULT; return; }
    if (phase == WAIT_COMMIT) {
        if ((status & (MT_VIDEO_EFFECT_STATUS_BUSY | MT_VIDEO_EFFECT_STATUS_VALID)) ==
                MT_VIDEO_EFFECT_STATUS_VALID &&
            bus.read(MT_REG_VIDEO_EFFECT_SEQUENCE) == sent_sequence) {
            const mt_video_frame_options_t options = {
                desired.enabled, desired.persistence, desired.blur, desired.bloom, desired.softness
            };
            mt_video_frame_service_start(epoch, &options);
            phase = APPLIED;
        }
        return;
    }
    if ((status & MT_VIDEO_EFFECT_STATUS_BUSY) != 0U) return;
    /* Cap MMIO work so USB, disk and save polling retain their service time. */
    for (unsigned count = 0U; count < 8U && cursor < MT_VIDEO_EFFECTS_WRITE_COUNT; ++count) {
        uint16_t address;
        uint32_t word;
        if (!mt_video_effects_write(&tables, cursor, &address, &word)) { phase = FAULT; return; }
        bus.write(MT_REG_VIDEO_EFFECT_ADDRESS, address);
        bus.write(MT_REG_VIDEO_EFFECT_DATA, word);
        ++cursor;
    }
    if (cursor == MT_VIDEO_EFFECTS_WRITE_COUNT) {
        ++sequence;
        if (sequence == 0U) ++sequence;
        sent_sequence = sequence;
        bus.write(MT_REG_VIDEO_EFFECT_COMMIT, sent_sequence);
        phase = WAIT_COMMIT;
    }
}
