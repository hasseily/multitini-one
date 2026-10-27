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
    MT_C64_CONTENT_SERVICE_REU_CONFLICT,
    MT_C64_CONTENT_SERVICE_CONFIG_FAILED,
    MT_C64_CONTENT_SERVICE_SAVE_FAILED,
    MT_C64_CONTENT_SERVICE_EXPORT_FAILED
} mt_c64_content_service_status_t;

typedef struct {
    mt_c64_content_service_status_t status;
    mt_sd_result_t sd;
    mt_c64_content_result_t content;
    mt_link_result_t link;
} mt_c64_content_service_result_t;

/* An optional title transaction. commit runs after validation, while reset is
 * acknowledged when hold_reset is true. A failed hold never calls commit.
 * The hold is retained across cartridge detach and transferred only after the
 * new loader accepts its first byte. The caller owns rollback after a later
 * transfer failure. A NULL hook preserves the legacy command trace. */
typedef struct {
    bool reu_requested;
    bool hold_reset;
    int (*commit)(void *context);
    void *context;
} mt_c64_launch_hook_t;

/* Monotonic milliseconds used to bound the C64 reset-to-KERNAL and RUN
 * injector waits by elapsed time. */
typedef uint64_t (*mt_c64_content_time_ms_fn)(void *context);

void c64_content_service_init(mt_core_link_t *link,
                              mt_link_progress_fn progress,
                              void *progress_context,
                              mt_c64_content_time_ms_fn time_ms,
                              void *time_context);
/* Configure the package content root before loading. A different root is
 * refused while a cartridge is attached. init restores the default SD root. */
int c64_content_service_set_media_root(const char *root);
/* file_name is a validated path relative to the configured media root. */
mt_c64_content_service_result_t c64_content_service_load(
    const char *file_name);
mt_c64_content_service_result_t c64_content_service_load_hooked(
    const char *file_name, const mt_c64_launch_hook_t *hook);
/* Stage an already-extracted PRG without typing RUN, then launch it after its
 * parent disk has been remounted. */
mt_c64_content_service_result_t c64_content_service_stage_prg_image(
    const uint8_t *data, size_t size);
mt_c64_content_service_result_t c64_content_service_stage_prg_image_hooked(
    const uint8_t *data, size_t size, const mt_c64_launch_hook_t *hook);
/* Reset to a ready, empty BASIC session for a subsequent native disk LOAD.
 * No deferred RUN is armed: a disk-remount retry must not run the empty stub. */
mt_c64_content_service_result_t c64_content_service_prepare_disk_load(void);
mt_c64_content_service_result_t c64_content_service_prepare_disk_load_hooked(
    const mt_c64_launch_hook_t *hook);
/* Start without clearing shared load/media errors after remount. An existing
 * core load fault is returned with the staged program available for recovery. */
mt_c64_content_service_result_t c64_content_service_run_staged_prg(void);
bool c64_content_service_prg_staged(void);
mt_c64_content_service_result_t c64_content_service_detach_crt(void);
mt_c64_content_service_result_t c64_content_service_detach_crt_hooked(
    const mt_c64_launch_hook_t *hook);
/* Reboot after the caller quiesces media and clears RAM $8005. Detach the
 * transient cartridge before restoring saved settings, then reload the saved
 * CRT without its title sidecar. A failure retains reset for recovery. */
mt_c64_content_service_result_t c64_content_service_reboot(
    const char *default_cartridge, int (*restore_settings)(void *), void *context);
bool c64_content_service_crt_attached(void);
/* Read the expansion registers only after a matching C64 ABI has been verified. */
uint32_t c64_content_service_expansion_caps(void);
bool c64_content_service_reu_enabled(void);
/* Effective cartridge permissions, including the legacy image exclusion.
 * Requested expansion settings remain stored while a mapper inhibits them. */
uint8_t c64_content_service_crt_expansion_policy(void);
bool c64_content_service_reu_load_allowed(void);
const char *c64_content_service_crt_name(void);
/* EasyFlash flash writes are mirrored into NAME.sav beside the CRT. The FPGA
 * journals each program/erase; poll drains it and saves after a quiet
 * interval. Flush drains and saves now; loads, detach and reboot flush first
 * and refuse with SAVE_FAILED rather than discard unsaved flash changes. */
void c64_content_service_poll(void);
int c64_content_service_flush(void);
bool c64_content_service_save_active(void);
bool c64_content_service_save_dirty(void);
bool c64_content_service_save_faulted(void);
/* Cumulative flash program/erase operations mirrored from the journal. */
uint32_t c64_content_service_flash_writes(void);
/* The EasyFlash boot jumper. Disable takes effect at the next C64 reset and
 * lasts until the C64 image is reloaded, like a jumper left in place. */
bool c64_content_service_ef_jumper_disabled(void);
int c64_content_service_set_ef_jumper(bool disabled);
/* Write the attached flash as NAME-saved.crt beside the CRT (replacing an
 * earlier export of that name); NAME.sav and the CRT stay unchanged.
 * exported receives the file name. */
mt_c64_content_service_result_t c64_content_service_export_crt(
    char *exported, size_t capacity);
const char *c64_content_service_status_name(
    mt_c64_content_service_status_t status);

#endif
