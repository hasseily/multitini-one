/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX_CONTENT_SERVICE_H
#define MULTITINI_VECTREX_CONTENT_SERVICE_H
#include "multitini_core_link.h"
#include "multitini_content.h"
#include "sd_storage.h"
#include <stdbool.h>

/* Byte capacities include the terminating NUL. Paths are relative to the
 * configured media root; returned cartridge names retain all components. */
#define MT_VECTREX_CONTENT_PATH_CAPACITY 256U
#define MT_VECTREX_CONTENT_ABSOLUTE_CAPACITY 512U
typedef enum {
    MT_VECTREX_CONTENT_OK = 0, MT_VECTREX_CONTENT_INVALID_ARGUMENT,
    MT_VECTREX_CONTENT_READ_FAILED, MT_VECTREX_CONTENT_REJECTED,
    MT_VECTREX_CONTENT_TRANSFER_FAILED, MT_VECTREX_CONTENT_BASIC_FAILED,
    MT_VECTREX_CONTENT_NO_BASIC_PROGRAM, MT_VECTREX_CONTENT_OVERLAY_UNSUPPORTED
} mt_vectrex_content_status_t;
typedef struct {
    mt_vectrex_content_status_t status;
    mt_sd_result_t sd;
    mt_link_result_t link;
    mt_content_validation_t validation;
} mt_vectrex_content_result_t;
/* BASIC callbacks receive the selected file's containing directory and
 * basename, so its asset and INCLUDE paths resolve within that directory. */
typedef int (*mt_vectrex_basic_run_fn)(void *context, const char *root,
    const char *name, const char *source);
void vectrex_content_service_init(mt_core_link_t *, const char *root,
    mt_link_progress_fn, void *, mt_vectrex_basic_run_fn, void *);
mt_vectrex_content_result_t vectrex_content_service_boot_bios(void);
mt_vectrex_content_result_t vectrex_content_service_load(const char *relative_path);
mt_vectrex_content_result_t vectrex_content_service_detach(void);
mt_vectrex_content_result_t vectrex_content_service_restart_basic(void);
bool vectrex_content_service_cartridge_attached(void);
bool vectrex_content_service_basic_selected(void);
const char *vectrex_content_service_cartridge_name(void);
/* Empty when the last accepted content operation had no artwork warning. */
const char *vectrex_content_service_overlay_warning(void);
int vectrex_content_service_read_bios(uint16_t address, uint8_t *data, size_t count);
const char *vectrex_content_status_name(mt_vectrex_content_status_t);
#endif
