/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SUPERVISOR_H
#define MULTITINI_SUPERVISOR_H

#include "multitini_core_package.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MT_SUPERVISOR_IDLE = 0,
    MT_SUPERVISOR_ACTIVE,
    MT_SUPERVISOR_QUIESCING,
    MT_SUPERVISOR_LOADING,
    MT_SUPERVISOR_PROBING,
    MT_SUPERVISOR_ACTIVATING,
    MT_SUPERVISOR_ROLLING_BACK,
    MT_SUPERVISOR_RECOVERY,
    MT_SUPERVISOR_FAILED
} mt_supervisor_state_t;

typedef enum {
    MT_SUPERVISOR_OK = 0,
    MT_SUPERVISOR_NO_CHANGE,
    MT_SUPERVISOR_INVALID_ARGUMENT,
    MT_SUPERVISOR_QUIESCE_FAILED,
    MT_SUPERVISOR_LOAD_FAILED,
    MT_SUPERVISOR_PROBE_FAILED,
    MT_SUPERVISOR_ACTIVATE_FAILED,
    MT_SUPERVISOR_ROLLBACK_FAILED,
    MT_SUPERVISOR_INCOMPATIBLE_ABI
} mt_supervisor_result_t;

typedef struct {
    mt_core_package_t package;
    char directory[MT_CORE_DIRECTORY_MAX];
    size_t variant_index;
} mt_core_selection_t;

typedef struct {
    int (*quiesce)(void *context);
    int (*load)(void *context, const mt_core_selection_t *selection);
    int (*probe)(void *context, uint32_t core_id,
                 uint16_t abi_major, uint16_t abi_minor);
    int (*activate)(void *context, const mt_core_selection_t *selection);
    int (*recover)(void *context);
} mt_supervisor_ops_t;

typedef struct {
    mt_supervisor_ops_t ops;
    void *context;
    mt_supervisor_state_t state;
    mt_supervisor_result_t last_result;
    int last_backend_error;
    bool recovered;
    bool has_active;
    mt_core_selection_t active;
} mt_supervisor_t;

void mt_supervisor_init(mt_supervisor_t *supervisor,
                        const mt_supervisor_ops_t *ops, void *context);
bool mt_supervisor_selection_valid(const mt_core_selection_t *selection);
mt_supervisor_result_t mt_supervisor_set_initial(
    mt_supervisor_t *supervisor, const mt_core_selection_t *selection);
mt_supervisor_result_t mt_supervisor_switch(
    mt_supervisor_t *supervisor, const mt_core_selection_t *selection);
const char *mt_supervisor_state_name(mt_supervisor_state_t state);
const char *mt_supervisor_result_name(mt_supervisor_result_t result);

#endif
