/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_GAMEBOY_CONTENT_SERVICE_H
#define MULTITINI_XILINX_GAMEBOY_CONTENT_SERVICE_H

#include "multitini_content.h"
#include "multitini_core_link.h"
#include "sd_storage.h"

#include <stdbool.h>

typedef enum {
    MT_GAMEBOY_CONTENT_SERVICE_OK = 0,
    MT_GAMEBOY_CONTENT_SERVICE_INVALID_ARGUMENT,
    MT_GAMEBOY_CONTENT_SERVICE_READ_FAILED,
    MT_GAMEBOY_CONTENT_SERVICE_REJECTED,
    MT_GAMEBOY_CONTENT_SERVICE_TRANSFER_FAILED
} mt_gameboy_content_service_status_t;

typedef struct {
    mt_gameboy_content_service_status_t status;
    mt_sd_result_t sd;
    mt_content_validation_t validation;
    mt_content_info_t info;
    mt_link_result_t link;
} mt_gameboy_content_service_result_t;

void gameboy_content_service_init(mt_core_link_t *link,
                                  const char *content_root,
                                  mt_link_progress_fn progress,
                                  void *progress_context);
mt_gameboy_content_service_result_t gameboy_content_service_load(
    const char *file_name);
mt_gameboy_content_service_result_t gameboy_content_service_detach(void);
bool gameboy_content_service_cartridge_attached(void);
const char *gameboy_content_service_cartridge_name(void);
const char *gameboy_content_service_status_name(
    mt_gameboy_content_service_status_t status);

#endif
