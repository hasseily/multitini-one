/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_CONTENT_H
#define MULTITINI_C64_CONTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "multitini_content.h"
#include "multitini_core_link.h"

typedef struct {
    const uint8_t *data;
    size_t size;
} mt_c64_content_image_t;

typedef enum {
    MT_C64_CONTENT_OK = 0,
    MT_C64_CONTENT_INVALID_ARGUMENT,
    MT_C64_CONTENT_INVALID_IMAGE,
    MT_C64_CONTENT_UNSUPPORTED_CRT
} mt_c64_content_status_t;

typedef struct {
    mt_c64_content_status_t status;
    mt_content_validation_t validation;
    uint16_t hardware_type;
} mt_c64_content_result_t;

/* The hardware list is intentionally narrower than the imported MiSTer
 * mapper. Types requiring writable cartridge RAM/flash, freeze controls, or
 * unimplemented peripherals are rejected before any stream is started. */
bool mt_c64_crt_hardware_supported(uint16_t hardware_type);
const char *mt_c64_crt_hardware_name(uint16_t hardware_type);

mt_c64_content_result_t mt_c64_validate_prg(
    const mt_c64_content_image_t *image);
/* True only for a nonempty, linked BASIC program at $0801. Other valid PRGs
 * may be autostart loaders or data files and must use the emulated drive. */
bool mt_c64_prg_has_basic_program(const mt_c64_content_image_t *image);
mt_c64_content_result_t mt_c64_validate_crt(
    const mt_c64_content_image_t *image);

/* A failed transfer aborts the receiver and pulses core reset so a partial PRG
 * or CRT cannot run; the software reset request is released for recovery. A
 * successful ordinary PRG is reset, injected, and RUN automatically. */
mt_link_result_t mt_c64_load_prg(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context);
/* Stage performs the same reset, injection, and BASIC-pointer repair without
 * typing RUN. A later one-byte run command starts that staged PRG, allowing
 * firmware to remount its parent disk first. */
mt_link_result_t mt_c64_stage_prg(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context);
mt_link_result_t mt_c64_run_prg(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context);
mt_link_result_t mt_c64_load_crt(
    mt_core_link_t *link, const mt_c64_content_image_t *image,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context);
mt_link_result_t mt_c64_detach_crt(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context);

const char *mt_c64_content_status_name(mt_c64_content_status_t status);

#endif
