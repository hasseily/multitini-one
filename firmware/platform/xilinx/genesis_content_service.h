/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_GENESIS_CONTENT_SERVICE_H
#define MULTITINI_XILINX_GENESIS_CONTENT_SERVICE_H

#include "multitini_content.h"
#include "multitini_genesis_boot.h"
#include "multitini_genesis_content.h"
#include "multitini_core_link.h"
#include "persistent_save.h"
#include "sd_storage.h"

#include <stdbool.h>

typedef enum {
    MT_GENESIS_CONTENT_SERVICE_OK = 0,
    MT_GENESIS_CONTENT_SERVICE_INVALID_ARGUMENT,
    MT_GENESIS_CONTENT_SERVICE_READ_FAILED,
    MT_GENESIS_CONTENT_SERVICE_REJECTED,
    MT_GENESIS_CONTENT_SERVICE_REGION_MISMATCH,
    MT_GENESIS_CONTENT_SERVICE_TRANSFER_FAILED,
    MT_GENESIS_CONTENT_SERVICE_SAVE_FAILED,
    MT_GENESIS_CONTENT_SERVICE_SAVE_MOUNT_FAILED,
    MT_GENESIS_CONTENT_SERVICE_FLUSH_FAILED
} mt_genesis_content_service_status_t;

typedef struct {
    uint32_t status,error,control,stream_status;
    uint32_t command_opcode,response_status,core_variant,build_id,abi,core_id;
} mt_genesis_transfer_snapshot_t;

typedef struct {
    mt_genesis_content_service_status_t status;
    mt_sd_result_t sd;
    mt_content_validation_t validation;
    mt_content_info_t info;
    mt_genesis_content_info_t cartridge;
    mt_link_result_t link;
    mt_persistent_save_result_t save;
    size_t save_size;
    bool save_active;
    bool save_existed;
    mt_genesis_transfer_phase_t phase;
    bool snapshot_valid;
    /* One live, nondestructive snapshot after failure. Stream abort or save
     * cleanup may already have replaced the command/response registers. */
    mt_genesis_transfer_snapshot_t snapshot;
} mt_genesis_content_service_result_t;

void genesis_content_service_init(mt_core_link_t *link,
                                  const char *content_root,
                                  bool persistent_save_supported,
                                  bool pal_variant,
                                  mt_link_progress_fn progress,
                                  void *progress_context);
/* Valid settings take effect on the next cartridge load. AUTO follows the
 * header within the selected PAL/NTSC image. */
int genesis_content_service_set_options(const mt_genesis_config_t *options);
const mt_genesis_config_t *genesis_content_service_options(void);
mt_genesis_content_service_result_t genesis_content_service_load(
    const char *file_name);
mt_genesis_content_service_result_t genesis_content_service_detach(void);
void genesis_content_service_poll(void);
/* Flush and unmount save RAM before reset. A failure is a hard barrier: the
 * caller must keep the running core intact so the save can be retried. */
int genesis_content_service_prepare_reset(void);
/* Restore the save-RAM media mount while reset remains asserted. */
int genesis_content_service_resume_after_reset(void);
int genesis_content_service_retry_save(void);
bool genesis_content_service_cartridge_attached(void);
const char *genesis_content_service_cartridge_name(void);
bool genesis_content_service_save_active(void);
bool genesis_content_service_save_dirty(void);
bool genesis_content_service_save_faulted(void);
const char *genesis_content_service_save_path(void);
const char *genesis_content_service_status_name(
    mt_genesis_content_service_status_t status);
const char *genesis_content_service_phase_name(mt_genesis_transfer_phase_t phase);

#endif
