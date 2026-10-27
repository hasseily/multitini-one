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
    uint8_t subtype;
} mt_c64_content_result_t;

/* The hardware list is intentionally narrower than the imported MiSTer
 * mapper. Types requiring writable cartridge RAM/flash or unimplemented
 * peripherals are rejected before any stream is started, except EasyFlash,
 * whose RAM and flash the FPGA emulates. Final Cartridge III supports its
 * four ROM banks; there is no user freeze-button control. */
bool mt_c64_crt_hardware_supported(uint16_t hardware_type);
bool mt_c64_crt_subtype_supported(uint16_t hardware_type, uint8_t subtype);
const char *mt_c64_crt_hardware_name(uint16_t hardware_type);

enum {
    MT_C64_CRT_ALLOW_REU = 1U,
    MT_C64_CRT_ALLOW_UCI = 2U,
    MT_C64_CRT_ALLOW_ULTIMATE_AUDIO = 4U
};

/* Allowed expansion devices for a supported cartridge/subtype. Unsupported
 * profiles return zero. This is I/O compatibility, not a hardware-test claim;
 * the active FPGA must also advertise cartridge/expansion coexistence. */
uint8_t mt_c64_crt_expansion_policy(uint16_t hardware_type, uint8_t subtype);

mt_c64_content_result_t mt_c64_validate_prg(
    const mt_c64_content_image_t *image);
/* True only for a nonempty, linked BASIC program at $0801. Other valid PRGs
 * may be autostart loaders or data files and must use the emulated drive. */
bool mt_c64_prg_has_basic_program(const mt_c64_content_image_t *image);
mt_c64_content_result_t mt_c64_validate_crt(
    const mt_c64_content_image_t *image);

/* EasyFlash keeps its 1 MiB flash in canonical order: bank b's ROML at
 * b*16 KiB and its ROMH 8 KiB later. The FPGA loader accepts it as a dense
 * CRT of 128 8 KiB packets in that order. */
#define MT_C64_EASYFLASH_IMAGE_SIZE ((size_t)0x100000U)
#define MT_C64_EASYFLASH_STREAM_SIZE ((size_t)(0x40U + 128U * 0x2010U))
/* Expand a validated EasyFlash CRT; unused flash reads $FF (erased). */
void mt_c64_easyflash_expand(const uint8_t *crt, size_t size, uint8_t *image);
/* Returns the stream size, or zero when the output is too small. */
size_t mt_c64_easyflash_stream(const uint8_t *image, uint8_t *out,
                               size_t capacity);
/* Apply one C64_EF_JOURNAL word the way the FPGA applied it to cartridge
 * memory. Returns false for an invalid word. */
bool mt_c64_easyflash_apply(uint8_t *image, uint32_t entry);
/* Export the canonical image as a type 32 CRT named name (at most 32 bytes):
 * one CHIP packet (type 2, flash; ROMH at $A000) per 8 KiB half that is not
 * all $FF. Returns the CRT size, or zero when capacity is too small. */
size_t mt_c64_easyflash_export(const uint8_t *image, const char *name,
                               uint8_t *out, size_t capacity);

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
