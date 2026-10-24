/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_C64_D64_SERVICE_H
#define MULTITINI_XILINX_C64_D64_SERVICE_H

#include "multitini_c64_d64.h"
#include "multitini_c64_rom_files.h"
#include "multitini_core_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_C64_MEDIA_DIRECTORY MT_C64_SD_MEDIA_ROOT
#define MT_C64_DEFAULT_D64_NAME "drive8.d64"

void c64_d64_service_init(mt_core_link_t *link,
                          mt_link_progress_fn progress,
                          void *progress_context);
/* Paths supplied to mount/ensure_mounted are relative to this package root.
 * init restores the default. Root changes require no mounted or reset-held
 * selection; setting the same normalized root remains valid. */
int c64_d64_service_set_media_root(const char *root);
/* The caller supplies a normalized absolute path. Protect active image,
 * journal and their parent directories, including a reset-remount selection,
 * against another storage client's overwrite, delete or rename. */
bool c64_d64_service_path_protected(const char *absolute_path);
/*
 * Flush and eject mounted media before asserting core reset.  On success the
 * file name is retained for c64_d64_service_resume_after_reset().  The caller
 * must not assert reset when this returns an error: a dirty D64 is still
 * mounted so that persistence can be retried without discarding its data.
 */
int c64_d64_service_prepare_reset(void);
/* Wait until the FPGA reports a stable mounted state with no eject pending. */
int c64_d64_service_wait_mounted(void);
int c64_d64_service_resume_after_reset(void);

/*
 * Clear a media-transfer fault and allow the FPGA to reissue its held dirty
 * track.  A healthy in-flight request is never cleared. Eject and reset
 * preparation call this automatically when retrying a failed journal commit.
 */
int c64_d64_service_retry(void);

/* Forget only in-flight request de-duplication after reset was asserted. */
void c64_d64_service_notify_core_reset(void);
int c64_d64_service_mount(const char *file_name);
/* Restore a selection only after confirming the FPGA still owns the mount. */
int c64_d64_service_ensure_mounted(const char *file_name);
int c64_d64_service_mount_default(void);
int c64_d64_service_eject(void);
void c64_d64_service_poll(void);
void c64_d64_service_list(void);
void c64_d64_service_dump_status(void);
bool c64_d64_service_mounted(void);
bool c64_d64_service_mounted_d64(void);
const char *c64_d64_service_mounted_name(void);

/* Inspect and extract programs from the stable in-memory copy of the mounted
 * D64. G64 images deliberately remain on the normal drive-emulation path. */
mt_c64_d64_result_t c64_d64_service_list_prgs(
    mt_c64_d64_prg_visitor_fn visitor, void *visitor_context,
    size_t *entry_count);
mt_c64_d64_result_t c64_d64_service_extract_prg(
    const mt_c64_d64_prg_entry_t *entry,
    uint8_t *destination, size_t capacity, size_t *extracted_size);

#endif
