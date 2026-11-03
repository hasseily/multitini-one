/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_CONTENT_SERVICE_H
#define MULTITINI_AMIGA_CONTENT_SERVICE_H
#include "multitini_amiga_boot.h"
#include "multitini_amiga_rtc.h"
void amiga_content_service_init(mt_core_link_t *,const char *media_root,bool aga_pal,mt_link_progress_fn,void *);
bool amiga_content_service_aga_pal(void);
int amiga_content_service_boot(void);
void amiga_content_service_poll(void);
mt_link_result_t amiga_content_service_set_clock(const mt_clock_time_t *,uint32_t now_ms);
mt_link_result_t amiga_content_service_clock_status(void);
int amiga_content_service_mount(unsigned drive,const char *name);
int amiga_content_service_eject(unsigned drive);
int amiga_content_service_prepare_reset(void);
int amiga_content_service_resume_after_reset(void);
int amiga_content_service_shutdown(void);
int amiga_content_service_retry(void);
int amiga_content_service_configure(const mt_amiga_config_t *);
const mt_amiga_config_t *amiga_content_service_config(void);
bool amiga_content_service_cpu14_available(void);
bool amiga_content_service_cpu14_fallback(void);
bool amiga_content_service_fast_ide_available(void);
bool amiga_content_service_fast_ide_expected(void);
typedef struct {
    uint64_t ticks[2][2]; /* legacy/bulk, ARM->IDE/IDE->ARM */
    uint32_t bytes_per_leg;
} mt_amiga_ide_bench_t;
/* Halts/quiesces the guest, verifies FIFO restoration, and times transport
 * only. Success restarts Amiga with its current configuration and mounts.
 * An uncertain transfer/mismatch requires a core reload; it stays halted. */
int amiga_content_service_ide_bench(unsigned iterations,uint64_t (*timer)(void *),
    void *context,mt_amiga_ide_bench_t *result);
typedef enum {
    MT_AMIGA_PROFILE_STOPPED=0,MT_AMIGA_PROFILE_RUNNING=1,
    MT_AMIGA_PROFILE_LIFECYCLE=2,MT_AMIGA_PROFILE_IO_ERROR=3,
    MT_AMIGA_PROFILE_OVERFLOW=4
} mt_amiga_profile_reason_t;
typedef struct {
    bool running,valid,read_window_started;
    mt_amiga_profile_reason_t reason;
    uint64_t elapsed_ticks,service_ticks,service_max_ticks,outside_ticks,max_gap_ticks;
    uint64_t sd_read_ticks,sd_read_max_ticks,fifo_ticks,fifo_max_ticks;
    uint64_t ata_other_ticks,ata_other_max_ticks,fdd_ticks,fdd_max_ticks;
    uint64_t polls,idle_ata_polls,req4,req5,req6;
    uint64_t read_commands,write_commands,requested_read_sectors,read_command_hist[9];
    uint64_t sd_read_calls,read_sectors,read_bytes;
    uint64_t fifo_calls,fifo_bytes,sequential_reads,nonsequential_reads;
    uint64_t drq_intervals,drq_to_request_ticks,max_drq_to_request_ticks;
    uint64_t poll_bracketed_intervals,poll_unbracketed_intervals;
    uint64_t poll_tail_upper_ticks,max_poll_tail_upper_ticks;
    uint64_t first_read_ticks,last_read_ticks,active_read_ticks,read_window_bytes;
    bool native_available,native_valid;
    mt_link_result_t native_result;
    mt_amiga_diag_snapshot_t native;
} mt_amiga_ide_profile_t;
/* Optional host timing only: no reset, settings, media or FIFO operations.
 * Stop before printing. Snapshot of a stopped run never calls the timer.
 * SD calls/time/bytes describe actual backend fills, including partial returns.
 * Read sectors and drive counters count logical 512-byte sectors, including
 * cache hits; FIFO bytes require success. Cache copying is ATA-other work.
 * Histogram: 1,2,3..4,5..8,9..16,17..32,33..64,65..128,129..256 sectors.
 * Read window: first logical READ attempt through last successful sector ACK
 * (also valid if profiling starts during a command with cached sectors);
 * offsets are relative to profile start, bytes are ACK-published full sectors.
 * The window includes inter-command gaps. ATA other includes write journals.
 * DRQ-to-next-request intervals overlap outside ticks and are NON-additive:
 * they include guest work, CDC visibility and ARM polling latency.
 * A post-DRQ zero then positive status brackets control-visible readiness.
 * Its polling tail lies in [0,poll_tail_upper_ticks], summed only over
 * bracketed intervals; no-zero intervals are explicitly unbracketed. These
 * are bounds, not measured native request assertion or exact polling time. */
int amiga_content_service_profile_start(uint64_t (*timer)(void *),void *context);
void amiga_content_service_profile_stop(void);
void amiga_content_service_profile_snapshot(mt_amiga_ide_profile_t *out);
const char *amiga_content_service_name(unsigned drive);
const char *amiga_content_service_error(void);
bool amiga_content_service_ready(void);
bool amiga_content_service_faulted(void);
void amiga_content_service_activity(unsigned drive,uint32_t *reads,uint32_t *writes);
#endif
