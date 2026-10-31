/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_BOOT_H
#define MULTITINI_AMIGA_BOOT_H
#include "multitini_core_link.h"
#include "multitini_amiga_content.h"
mt_link_result_t mt_amiga_host_access(mt_core_link_t *,uint8_t opcode,uint32_t payload,
    uint32_t polls,uint32_t *response);
mt_link_result_t mt_amiga_stage(mt_core_link_t *,const mt_amiga_config_t *,const uint8_t *rom,
    size_t bytes,uint32_t polls,mt_link_progress_fn,void *);
mt_link_result_t mt_amiga_native_configure(mt_core_link_t *,const mt_amiga_config_t *,uint32_t polls);
#endif
