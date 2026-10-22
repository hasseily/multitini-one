/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_C64_CONTENT_SERVICE_H
#define MULTITINI_XILINX_C64_CONTENT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "multitini_c64_content.h"
#include "multitini_core_link.h"
#include "sd_storage.h"

typedef enum {
    MT_C64_CONTENT_SERVICE_OK = 0,
    MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT,
    MT_C64_CONTENT_SERVICE_READ_FAILED,
    MT_C64_CONTENT_SERVICE_REJECTED,
    MT_C64_CONTENT_SERVICE_TRANSFER_FAILED,
    MT_C64_CONTENT_SERVICE_REU_CONFLICT
} mt_c64_content_service_status_t;

typedef struct {
    mt_c64_content_service_status_t status;
    mt_sd_result_t sd;
    mt_c64_content_result_t content;
    mt_link_result_t link;
} mt_c64_content_service_result_t;

/* Monotonic milliseconds used to bound the C64 reset-to-KERNAL and RUN
 * injector waits by elapsed time. */
typedef uint64_t (*mt_c64_content_time_ms_fn)(void *context);

void c64_content_service_init(mt_core_link_t *link,
                              mt_link_progress_fn progress,
                              void *progress_context,
                              mt_c64_content_time_ms_fn time_ms,
                              void *time_context);
mt_c64_content_service_result_t c64_content_service_load(
    const char *file_name);
/* Stage an already-extracted PRG without typing RUN, then launch it after its
 * parent disk has been remounted. */
mt_c64_content_service_result_t c64_content_service_stage_prg_image(
    const uint8_t *data, size_t size);
/* Reset to a ready, empty BASIC session for a subsequent native disk LOAD.
 * No deferred RUN is armed: a disk-remount retry must not run the empty stub. */
mt_c64_content_service_result_t c64_content_service_prepare_disk_load(void);
/* Start without clearing shared load/media errors after remount. An existing
 * core load fault is returned with the staged program available for recovery. */
mt_c64_content_service_result_t c64_content_service_run_staged_prg(void);
bool c64_content_service_prg_staged(void);
mt_c64_content_service_result_t c64_content_service_detach_crt(void);
bool c64_content_service_crt_attached(void);
/* Read the expansion registers only after a matching C64 ABI has been verified. */
uint32_t c64_content_service_expansion_caps(void);
bool c64_content_service_reu_enabled(void);
const char *c64_content_service_crt_name(void);
const char *c64_content_service_status_name(
    mt_c64_content_service_status_t status);

#endif
