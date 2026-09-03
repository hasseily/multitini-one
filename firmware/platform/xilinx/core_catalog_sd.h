/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_CORE_CATALOG_SD_H
#define MULTITINI_XILINX_CORE_CATALOG_SD_H

#include "multitini_supervisor.h"
#include "sd_storage.h"

#include <stddef.h>
#include <stdint.h>

#define MT_CORE_CATALOG_ROOT "0:/multitini/cores"
#define MT_CORE_CATALOG_MANIFEST "core.cfg"
#define MT_CORE_CATALOG_MAX_CORES 16U

typedef struct {
    mt_core_selection_t cores[MT_CORE_CATALOG_MAX_CORES];
    size_t count;
    size_t rejected;
    mt_sd_result_t sd;
    mt_core_package_status_t last_manifest_status;
} mt_core_catalog_sd_t;

void mt_core_catalog_sd_scan(mt_core_catalog_sd_t *catalog);
int mt_core_catalog_sd_find(const mt_core_catalog_sd_t *catalog,
                            uint32_t core_id);

#endif
