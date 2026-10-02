/* SPDX-License-Identifier: GPL-3.0-only */
#include "video_effect_service.h"
#include "video_frame_service.h"
#include "video_worker.h"
#include "multitini_video_frame.h"
#include "multitini_regs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t registers[0x200U / 4U];
static uint32_t milliseconds;
static unsigned reads, writes, submissions, collections, data_writes, commits;
static unsigned display_submits;
static unsigned error_clears;
static unsigned writes_before;
static bool forbidden_mmio, reject_collect, corrupt_size;
static mt_video_worker_status_t worker;
static uint8_t submitted[MT_VIDEO_WORKER_PAYLOAD_BYTES];
static uint32_t submitted_size;
static uint8_t completion[MT_VIDEO_WORKER_PAYLOAD_BYTES];
static uint32_t completion_size;
static uint32_t addresses[MT_VIDEO_EFFECTS_WRITE_COUNT];
static uint32_t words[MT_VIDEO_EFFECTS_WRITE_COUNT];

mt_video_worker_status_t mt_video_worker_status(void) { return worker; }
bool mt_video_worker_start(void) { return true; }
bool mt_video_worker_resume(void) { return worker.state != MT_VIDEO_WORKER_FAILED; }
bool mt_video_worker_quiesce(void) { return worker.state != MT_VIDEO_WORKER_BUSY; }
bool mt_video_worker_submit(uint32_t epoch, const void *input, uint32_t size)
{
    if (worker.state != MT_VIDEO_WORKER_IDLE && worker.state != MT_VIDEO_WORKER_COMPLETE)
        return false;
    assert(size <= sizeof(submitted));
    memcpy(submitted, input, size);
    submitted_size = size;
    worker.epoch = epoch;
    worker.sequence++;
    worker.state = MT_VIDEO_WORKER_BUSY;
    submissions++;
    return true;
}
bool mt_video_worker_collect(uint32_t epoch, void *output, uint32_t capacity,
                              uint32_t *size)
{
    *size = 0U;
    if (reject_collect || worker.state != MT_VIDEO_WORKER_COMPLETE || epoch != worker.epoch)
        return false;
    assert(capacity >= completion_size);
    memcpy(output, completion, completion_size);
    *size = corrupt_size ? completion_size - 1U : completion_size;
    worker.state = MT_VIDEO_WORKER_IDLE;
    collections++;
    return true;
}

static uint32_t read_register(uint32_t offset)
{
    assert(!forbidden_mmio && (offset & 3U) == 0U && offset < sizeof(registers));
    reads++;
    return registers[offset / 4U];
}
static void write_register(uint32_t offset, uint32_t value)
{
    assert(!forbidden_mmio && (offset & 3U) == 0U && offset < sizeof(registers));
    writes++;
    registers[offset / 4U] = value;
    if (offset == MT_REG_VIDEO_EFFECT_DATA) {
        addresses[data_writes % MT_VIDEO_EFFECTS_WRITE_COUNT] = registers[MT_REG_VIDEO_EFFECT_ADDRESS / 4U];
        words[data_writes++ % MT_VIDEO_EFFECTS_WRITE_COUNT] = value;
    } else if (offset == MT_REG_VIDEO_EFFECT_COMMIT) {
        commits++;
        registers[MT_REG_VIDEO_EFFECT_STATUS / 4U] |= MT_VIDEO_EFFECT_STATUS_BUSY;
    } else if (offset == MT_REG_VIDEO_FRAME_CONTROL && value != 0U) {
        registers[MT_REG_VIDEO_FRAME_STATUS / 4U] &= ~MT_VIDEO_FRAME_STATUS_QUIESCENT;
    } else if (offset == MT_REG_VIDEO_FRAME_DISPLAY_SUBMIT) {
        display_submits++;
        registers[MT_REG_VIDEO_FRAME_STATUS / 4U] |= MT_VIDEO_FRAME_STATUS_DISPLAY_PENDING;
    } else if (offset == MT_REG_VIDEO_FRAME_ERROR_CLEAR) {
        assert(value == 1U && registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == 0U);
        assert((registers[MT_REG_VIDEO_FRAME_STATUS / 4U] & MT_VIDEO_FRAME_STATUS_QUIESCENT) != 0U);
        error_clears++;
        registers[MT_REG_VIDEO_FRAME_STATUS / 4U] &= ~MT_VIDEO_FRAME_STATUS_ERROR;
        registers[MT_REG_VIDEO_FRAME_CAPTURE_DROPS / 4U] = 0U;
        registers[MT_REG_VIDEO_FRAME_AXI_ERRORS / 4U] = 0U;
        registers[MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS / 4U] = 0U;
        registers[MT_REG_VIDEO_FRAME_DEADLINES / 4U] = 0U;
    }
}
static uint32_t get_milliseconds(void) { return milliseconds; }

static void reset_service(void)
{
    const mt_video_effect_service_ops_t ops = {
        read_register, write_register, get_milliseconds
    };
    memset(registers, 0, sizeof(registers));
    memset(&worker, 0, sizeof(worker));
    worker.state = MT_VIDEO_WORKER_IDLE;
    registers[MT_REG_VIDEO_EFFECT_STATUS / 4U] = MT_VIDEO_EFFECT_STATUS_SUPPORTED;
    registers[MT_REG_VIDEO_FRAME_STATUS / 4U] = MT_VIDEO_FRAME_STATUS_QUIESCENT |
        MT_VIDEO_FRAME_STATUS_AUTONOMOUS_SUPPORTED;
    registers[MT_REG_VIDEO_FRAME_GEOMETRY / 4U] = (144U << 16U) | 160U;
    registers[MT_REG_VIDEO_FRAME_STRIDE / 4U] = 384U;
    reads = writes = submissions = collections = data_writes = commits = 0U;
    display_submits = error_clears = 0U;
    milliseconds = submitted_size = completion_size = 0U;
    forbidden_mmio = reject_collect = corrupt_size = false;
    mt_video_effect_service_init(&ops);
}

static void poll_bounded(void)
{
    writes_before = writes;
    mt_video_effect_service_poll();
    /* Eight two-register coefficient writes plus a final commit. Frame
     * relay may use a few extra ownership writes, but never a full LUT loop. */
    assert(writes - writes_before <= 20U);
}

static void finish_settings(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_tables_t tables;
    assert(worker.state == MT_VIDEO_WORKER_BUSY && submitted_size == sizeof(settings));
    memcpy(&settings, submitted, sizeof(settings));
    assert(mt_video_effects_build(&settings, &tables));
    memcpy(completion, &tables, sizeof(tables));
    completion_size = sizeof(tables);
    worker.output_size = completion_size;
    worker.result = 0;
    worker.state = MT_VIDEO_WORKER_COMPLETE;
}

static void acknowledge_commit(void)
{
    assert(commits != 0U);
    registers[MT_REG_VIDEO_EFFECT_SEQUENCE / 4U] = registers[MT_REG_VIDEO_EFFECT_COMMIT / 4U];
    registers[MT_REG_VIDEO_EFFECT_STATUS / 4U] =
        MT_VIDEO_EFFECT_STATUS_SUPPORTED | MT_VIDEO_EFFECT_STATUS_VALID;
}

static void finish_apply(void)
{
    const unsigned next_commit = commits + 1U;
    finish_settings();
    for (unsigned i = 0U; i < 100U && commits != next_commit; ++i) poll_bounded();
    assert(commits == next_commit);
    acknowledge_commit();
    poll_bounded();
}

static void start_frames(void)
{
    mt_video_effects_settings_t settings;
    reset_service();
    mt_video_effects_defaults(&settings);
    settings.enabled = 1U;
    settings.persistence = 2U;
    settings.blur = 1U;
    settings.bloom = 3U;
    settings.softness = 3U;
    mt_video_effect_service_configure(true, &settings);
    poll_bounded();
    finish_apply();
    poll_bounded();
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] ==
           (MT_VIDEO_FRAME_CAPTURE_ENABLE | MT_VIDEO_FRAME_DISPLAY_ENABLE |
            MT_VIDEO_FRAME_AUTONOMOUS_ENABLE));
    assert(error_clears == 1U);
    assert(registers[MT_REG_VIDEO_FRAME_OPTIONS / 4U]==(2U|(1U<<2U)|(3U<<4U)|(3U<<6U)));
}

static void test_complete_upload(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_tables_t expected;
    reset_service();
    mt_video_effects_defaults(&settings);
    settings.enabled = 1U;
    settings.gamma = 150U;
    assert(mt_video_effects_build(&settings, &expected));
    mt_video_effect_service_configure(true, &settings);
    poll_bounded();
    assert(submissions == 1U && data_writes == 0U);
    finish_settings();
    for (unsigned i = 0U; i < 100U && commits == 0U; ++i) poll_bounded();
    assert(data_writes == MT_VIDEO_EFFECTS_WRITE_COUNT && commits == 1U);
    for (uint32_t i = 0U; i < MT_VIDEO_EFFECTS_WRITE_COUNT; ++i) {
        uint16_t address;
        uint32_t value;
        assert(mt_video_effects_write(&expected, i, &address, &value));
        assert(addresses[i] == address && words[i] == value);
    }
    for (unsigned i = 0; i < 3U; ++i) poll_bounded();
    assert(commits == 1U);
    acknowledge_commit();
    poll_bounded();
    assert(mt_video_effect_service_available());
}

static void test_suspend_and_reconfigure(void)
{
    mt_video_effects_settings_t settings;
    uint32_t old_epoch;
    reset_service();
    mt_video_effects_defaults(&settings);
    mt_video_effect_service_configure(true, &settings);
    poll_bounded();
    old_epoch = worker.epoch;
    mt_video_effect_service_suspend();
    forbidden_mmio = true;
    for (unsigned i = 0; i < 5U; ++i) poll_bounded();
    finish_settings();
    poll_bounded();
    assert(!mt_video_effect_service_available() && commits == 0U);
    settings.gamma = 200U;
    forbidden_mmio = false;
    mt_video_effect_service_configure(true, &settings);
    poll_bounded();
    assert(worker.epoch != old_epoch && submissions == 2U && data_writes == 0U);
    assert(((mt_video_effects_settings_t *)submitted)->gamma == 200U);
}

static void test_failures_and_unsupported(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_defaults(&settings);
    reset_service();
    mt_video_effect_service_configure(false, &settings);
    forbidden_mmio = true;
    poll_bounded();
    assert(!mt_video_effect_service_available() && submissions == 0U);
    for (unsigned kind = 0; kind < 4U; ++kind) {
        reset_service();
        mt_video_effect_service_configure(true, &settings);
        poll_bounded();
        if (kind == 0U) worker.state = MT_VIDEO_WORKER_FAILED;
        if (kind == 1U) milliseconds = 3000U;
        if (kind >= 2U) {
            finish_settings();
            reject_collect = kind == 2U;
            corrupt_size = kind == 3U;
        }
        poll_bounded();
        assert(!mt_video_effect_service_available() && data_writes == 0U && commits == 0U);
    }
}

static void test_autonomous_ownership(void)
{
    start_frames();
    const unsigned before = submissions;
    const unsigned control = registers[MT_REG_VIDEO_FRAME_CONTROL / 4U];
    registers[MT_REG_VIDEO_FRAME_CAPTURE_CONSUMER / 4U] = 0xabcdef01U;
    /* CPU scheduling can pause while hardware advances all frame counters.
     * It must never submit pixel jobs, skip captures, or publish output slots. */
    for (unsigned i = 0U; i < 60U; ++i) {
        registers[MT_REG_VIDEO_FRAME_CAPTURE_PRODUCER / 4U] = i;
        registers[MT_REG_VIDEO_FRAME_PROCESSED / 4U] = i;
        milliseconds += 17U;
        poll_bounded();
    }
    assert(submissions == before && display_submits == 0U);
    assert(registers[MT_REG_VIDEO_FRAME_CAPTURE_CONSUMER / 4U] == 0xabcdef01U);
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == control);
}

static void test_live_settings_preserve_frame_schedule(void)
{
    mt_video_effects_settings_t settings;
    start_frames();
    const unsigned control = registers[MT_REG_VIDEO_FRAME_CONTROL / 4U];
    const unsigned clears = error_clears;
    mt_video_effects_defaults(&settings);
    settings.enabled = 1U; settings.bloom = 2U; settings.softness = 2U;
    mt_video_effect_service_configure(true, &settings);
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == control);
    poll_bounded();
    finish_apply();
    poll_bounded();
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == control);
    assert(registers[MT_REG_VIDEO_FRAME_OPTIONS / 4U] == ((2U << 4U)|(2U<<6U)));
    assert(error_clears == clears && display_submits == 0U);
    settings.enabled = 0U;
    mt_video_effect_service_configure(true, &settings);
    poll_bounded(); finish_apply(); poll_bounded();
    assert(registers[MT_REG_VIDEO_FRAME_OPTIONS / 4U] == 0U);
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == control);
    assert(error_clears == clears); /* Effects off retains fixed latency. */

    mt_video_effect_service_suspend();
    assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == 0U);
    assert(!mt_video_effect_service_quiescent());
    registers[MT_REG_VIDEO_FRAME_STATUS / 4U] = MT_VIDEO_FRAME_STATUS_QUIESCENT |
        MT_VIDEO_FRAME_STATUS_AUTONOMOUS_SUPPORTED;
    assert(mt_video_effect_service_quiescent());
    forbidden_mmio = true;
    for (unsigned i = 0U; i < 5U; ++i) {
        poll_bounded();
        assert(mt_video_effect_service_quiescent());
    }
}

static void test_frame_transport_errors(void)
{
    for (unsigned kind = 0U; kind < 4U; ++kind) {
        start_frames();
        if (kind == 0U) registers[MT_REG_VIDEO_FRAME_STATUS / 4U] |= MT_VIDEO_FRAME_STATUS_ERROR;
        if (kind == 1U) registers[MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS / 4U] = 1U;
        if (kind == 2U) registers[MT_REG_VIDEO_FRAME_CAPTURE_DROPS / 4U] = 1U;
        if (kind == 3U) registers[MT_REG_VIDEO_FRAME_DEADLINES / 4U] = 1U;
        poll_bounded();
        assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == 0U);
        assert(strstr(mt_video_effect_service_status(), "stopped") != NULL);
        if (kind == 3U) assert(strstr(mt_video_effect_service_status(), "deadline") != NULL);
        assert(display_submits == 0U);
    }
}

static void test_horizontal_frame_options(void)
{
    mt_video_effects_settings_t settings;
    start_frames();
    const unsigned control=registers[MT_REG_VIDEO_FRAME_CONTROL/4U],clears=error_clears;
    for(unsigned level=0U;level<4U;++level) {
        mt_video_effects_defaults(&settings);
        settings.enabled=1U;settings.persistence=1U;settings.blur=2U;settings.bloom=3U;
        settings.softness=(uint8_t)level;
        mt_video_effect_service_configure(true,&settings);
        poll_bounded();finish_apply();poll_bounded();
        assert(registers[MT_REG_VIDEO_FRAME_OPTIONS/4U]==(1U|(2U<<2U)|(3U<<4U)|(level<<6U)));
        assert(words[12U]==((1U<<MT_VIDEO_EFFECTS_ENABLE_SHIFT)|(level<<MT_VIDEO_EFFECTS_SOFTNESS_SHIFT)));
        assert(registers[MT_REG_VIDEO_FRAME_CONTROL/4U]==control && error_clears==clears);
    }
}

static void test_initial_drain_and_error_retry(void)
{
    mt_video_effects_settings_t settings;
    reset_service();
    mt_video_effects_defaults(&settings);
    settings.enabled = 1U; settings.bloom = 1U;
    mt_video_effect_service_configure(true, &settings);
    poll_bounded(); finish_apply();
    registers[MT_REG_VIDEO_FRAME_STATUS / 4U] = MT_VIDEO_FRAME_STATUS_CAPTURE_BUSY |
        MT_VIDEO_FRAME_STATUS_AUTONOMOUS_SUPPORTED;
    poll_bounded();
    assert(error_clears == 0U && registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == 0U);
    milliseconds = 1000U;
    poll_bounded();
    assert(strstr(mt_video_effect_service_status(), "stopped") != NULL);
    registers[MT_REG_VIDEO_FRAME_STATUS / 4U] = MT_VIDEO_FRAME_STATUS_QUIESCENT |
        MT_VIDEO_FRAME_STATUS_AUTONOMOUS_SUPPORTED | MT_VIDEO_FRAME_STATUS_ERROR;
    registers[MT_REG_VIDEO_FRAME_AXI_ERRORS / 4U] = 5U;
    registers[MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS / 4U] = 15U;
    mt_video_effect_service_configure(true, &settings);
    poll_bounded(); finish_apply(); poll_bounded();
    assert(error_clears == 1U && registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] != 0U);
    assert(registers[MT_REG_VIDEO_FRAME_AXI_ERRORS / 4U] == 0U);
    assert(registers[MT_REG_VIDEO_FRAME_DISPLAY_UNDERRUNS / 4U] == 0U);
}

static void test_frame_fault_precedes_control_update_status(void)
{
    for (unsigned deadline = 0U; deadline < 2U; ++deadline) {
        for (unsigned update = 0U; update < 3U; ++update) {
            mt_video_effects_settings_t settings;
            start_frames();
            assert(!mt_video_frame_service_failed());
            mt_video_effects_defaults(&settings);
            settings.gamma = 150U;
            mt_video_effect_service_configure(true, &settings);
            poll_bounded();
            if (update == 0U) worker.state = MT_VIDEO_WORKER_FAILED;
            else if (update == 1U) {
                finish_settings();
                reject_collect = true;
                poll_bounded();
                assert(strstr(mt_video_effect_service_status(), "update failed") != NULL);
            } else {
                assert(strstr(mt_video_effect_service_status(), "Applying") != NULL);
            }
            if (deadline) registers[MT_REG_VIDEO_FRAME_DEADLINES / 4U] = 1U;
            else registers[MT_REG_VIDEO_FRAME_STATUS / 4U] |= MT_VIDEO_FRAME_STATUS_ERROR;
            poll_bounded();
            assert(registers[MT_REG_VIDEO_FRAME_CONTROL / 4U] == 0U);
            assert(mt_video_frame_service_failed());
            forbidden_mmio = true;
            const char *status = mt_video_effect_service_status();
            assert(strstr(status, deadline ? "deadline missed" : "transport fault") != NULL);
            assert(strstr(status, "stopped") != NULL);
            assert(strstr(status, "remains active") == NULL);
            /* A cached status query remains safe during a later PL switch. */
            mt_video_effect_service_suspend();
            assert(!mt_video_frame_service_failed());
            forbidden_mmio = false;
        }
    }
}

int main(void)
{
    test_complete_upload();
    test_suspend_and_reconfigure();
    test_failures_and_unsupported();
    test_autonomous_ownership();
    test_live_settings_preserve_frame_schedule();
    test_frame_transport_errors();
    test_horizontal_frame_options();
    test_initial_drain_and_error_retry();
    test_frame_fault_precedes_control_update_status();
    puts("Video controls preserve autonomous frame scheduling");
    return 0;
}
