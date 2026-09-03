/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_C64_CONTENT_SERVICE_H
#define MULTITINI_XILINX_C64_CONTENT_SERVICE_H

#include <stdbool.h>

#include "multitini_c64_content.h"
#include "multitini_core_link.h"
#include "sd_storage.h"

typedef enum {
    MT_C64_CONTENT_SERVICE_OK = 0,
    MT_C64_CONTENT_SERVICE_INVALID_ARGUMENT,
    MT_C64_CONTENT_SERVICE_READ_FAILED,
    MT_C64_CONTENT_SERVICE_REJECTED,
    MT_C64_CONTENT_SERVICE_TRANSFER_FAILED
} mt_c64_content_service_status_t;

typedef struct {
    mt_c64_content_service_status_t status;
    mt_sd_result_t sd;
    mt_c64_content_result_t content;
    mt_link_result_t link;
} mt_c64_content_service_result_t;

void c64_content_service_init(mt_core_link_t *link,
                              mt_link_progress_fn progress,
                              void *progress_context);
mt_c64_content_service_result_t c64_content_service_load(
    const char *file_name);
mt_c64_content_service_result_t c64_content_service_detach_crt(void);
bool c64_content_service_crt_attached(void);
const char *c64_content_service_crt_name(void);
const char *c64_content_service_status_name(
    mt_c64_content_service_status_t status);

#endif
