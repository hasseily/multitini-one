/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_PL_LOADER_H
#define MULTITINI_PL_LOADER_H

#include "multitini_core_package.h"

typedef enum {
    MT_PL_LOAD_OK = 0,
    MT_PL_LOAD_INVALID_ARGUMENT,
    MT_PL_LOAD_PATH_FAILED,
    MT_PL_LOAD_READ_FAILED,
    MT_PL_LOAD_SIZE_MISMATCH,
    MT_PL_LOAD_CRC_MISMATCH,
    MT_PL_LOAD_DEVCFG_INIT_FAILED,
    MT_PL_LOAD_FABRIC_INIT_FAILED,
    MT_PL_LOAD_TRANSFER_FAILED,
    MT_PL_LOAD_DMA_TIMEOUT,
    MT_PL_LOAD_DONE_TIMEOUT,
    MT_PL_LOAD_DEVCFG_ERROR
} mt_pl_load_status_t;

typedef struct {
    mt_pl_load_status_t status;
    unsigned backend_status;
    uint32_t actual_crc32;
    size_t size;
} mt_pl_load_result_t;

mt_pl_load_result_t mt_pl_load_selection(
    const char *directory, const mt_core_variant_t *variant);
const char *mt_pl_load_status_name(mt_pl_load_status_t status);

#endif
