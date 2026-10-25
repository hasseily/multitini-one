/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_TITLE_SERVICE_H
#define MULTITINI_C64_TITLE_SERVICE_H
#include "c64_content_service.h"
#include "multitini_c64_title.h"
typedef struct
{
    void *context;
    /* live=true requires a coherent pause/REU-idle transaction. Return failure
     * without modifying hardware, restore the previous state, or keep the
     * machine held on an indeterminate hardware acknowledgement. */
    int (*apply)(void *, const mt_c64_title_values_t *, bool live, bool apply_event);
} c64_title_callbacks_t;
int c64_title_service_init(const char *root, const c64_title_callbacks_t *callbacks);
void c64_title_service_refresh(const mt_c64_title_values_t *stored,
                               const mt_c64_capabilities_t *caps, bool ntsc, bool automatic);
const mt_c64_launch_hook_t *
c64_title_service_prepare(const char *relative, mt_c64_title_origin_t origin, bool apply_event);
bool c64_title_service_launch_allowed(void);
/* Restore saved values while the detach caller holds reset. */
const mt_c64_launch_hook_t *c64_title_service_prepare_detach(void);
void c64_title_service_finish(mt_c64_content_service_status_t status);
/* Explicit apply is available even with automatic sidecars off. UCI paths are
 * absolute and confined by its target; this layer additionally permits only
 * the configured media root or the package's uci_temp directory. */
int c64_title_service_apply(const char *path, bool uci, char *report, size_t capacity);
/* Parse only: no recovery writes, MMIO, session changes or persistent writes.
 * A pending journal is reported and left intact for the normal Apply path. */
int c64_title_service_test(const char *relative, char *report, size_t capacity);
int c64_title_service_save(void);
int c64_title_service_revert(void);
void c64_title_service_clear(void);
void c64_title_service_reset(bool cartridge, bool disk_present);
/* UCI reboot caller holds guest reset; clear title overrides only after apply. */
int c64_title_service_reboot(bool default_cartridge);
void c64_title_service_effective(mt_c64_title_values_t *requested, mt_c64_title_values_t *applied,
                                 bool cartridge);
const mt_c64_title_overlay_t *c64_title_service_overlay(void);
const char *c64_title_service_session(void);
const char *c64_title_service_status(void);
bool c64_title_service_edit(uint32_t groups, const mt_c64_title_values_t *values);
bool c64_title_service_sid_owned(uint64_t fields);
void c64_title_service_edit_sid(const mt_c64_sid_model_t *model, uint64_t fields, uint8_t classes);
#endif
