/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_supervisor.h"

#include <string.h>

void mt_supervisor_init(mt_supervisor_t *supervisor,
                        const mt_supervisor_ops_t *ops, void *context)
{
    if (supervisor == NULL) return;
    memset(supervisor, 0, sizeof(*supervisor));
    if (ops != NULL) supervisor->ops = *ops;
    supervisor->context = context;
    supervisor->state = MT_SUPERVISOR_IDLE;
    supervisor->last_result = MT_SUPERVISOR_OK;
}

bool mt_supervisor_selection_valid(const mt_core_selection_t *selection)
{
    return selection != NULL && selection->directory[0] != '\0' &&
           selection->package.id[0] != '\0' &&
           selection->package.name[0] != '\0' &&
           selection->package.core_id != 0U &&
           selection->package.variant_count != 0U &&
           selection->package.variant_count <= MT_CORE_PACKAGE_MAX_VARIANTS &&
           selection->variant_index < selection->package.variant_count;
}

static bool selections_equal(const mt_core_selection_t *left,
                             const mt_core_selection_t *right)
{
    return strcmp(left->package.id, right->package.id) == 0 &&
           strcmp(left->package.variants[left->variant_index].id,
                  right->package.variants[right->variant_index].id) == 0;
}

mt_supervisor_result_t mt_supervisor_set_initial(
    mt_supervisor_t *supervisor, const mt_core_selection_t *selection)
{
    if (supervisor == NULL || !mt_supervisor_selection_valid(selection))
        return MT_SUPERVISOR_INVALID_ARGUMENT;
    supervisor->active = *selection;
    supervisor->has_active = true;
    supervisor->state = MT_SUPERVISOR_ACTIVE;
    supervisor->last_result = MT_SUPERVISOR_OK;
    supervisor->last_backend_error = 0;
    supervisor->recovered = false;
    return MT_SUPERVISOR_OK;
}

static int restore_previous(mt_supervisor_t *supervisor,
                            const mt_core_selection_t *previous,
                            bool had_previous)
{
    int status;
    supervisor->state = MT_SUPERVISOR_ROLLING_BACK;
    if (!had_previous) {
        supervisor->state = MT_SUPERVISOR_RECOVERY;
        return supervisor->ops.recover == NULL ? -1 :
               supervisor->ops.recover(supervisor->context);
    }
    status = supervisor->ops.load(supervisor->context, previous);
    if (status == 0) {
        status = supervisor->ops.probe(
            supervisor->context, previous->package.core_id,
            previous->package.abi_major, previous->package.abi_minor);
    }
    if (status == 0)
        status = supervisor->ops.activate(supervisor->context, previous);
    return status;
}

static mt_supervisor_result_t switch_failed(
    mt_supervisor_t *supervisor, mt_supervisor_result_t failure,
    int backend_error, const mt_core_selection_t *previous,
    bool had_previous)
{
    const int recovery_status = restore_previous(
        supervisor, previous, had_previous);
    supervisor->last_backend_error = backend_error;
    if (recovery_status != 0) {
        supervisor->state = MT_SUPERVISOR_FAILED;
        supervisor->last_result = MT_SUPERVISOR_ROLLBACK_FAILED;
        supervisor->recovered = false;
        supervisor->has_active = false;
        return MT_SUPERVISOR_ROLLBACK_FAILED;
    }
    if (had_previous) supervisor->active = *previous;
    else memset(&supervisor->active, 0, sizeof(supervisor->active));
    supervisor->has_active = had_previous;
    supervisor->state = had_previous ? MT_SUPERVISOR_ACTIVE :
                        MT_SUPERVISOR_IDLE;
    supervisor->last_result = failure;
    supervisor->recovered = true;
    return failure;
}

mt_supervisor_result_t mt_supervisor_switch(
    mt_supervisor_t *supervisor, const mt_core_selection_t *selection)
{
    mt_core_selection_t previous;
    bool had_previous;
    int status;

    if (supervisor == NULL || !mt_supervisor_selection_valid(selection) ||
        supervisor->ops.quiesce == NULL || supervisor->ops.load == NULL ||
        supervisor->ops.probe == NULL || supervisor->ops.activate == NULL) {
        return MT_SUPERVISOR_INVALID_ARGUMENT;
    }
    if (supervisor->has_active &&
        selections_equal(&supervisor->active, selection)) {
        supervisor->last_result = MT_SUPERVISOR_NO_CHANGE;
        supervisor->last_backend_error = 0;
        supervisor->recovered = false;
        return MT_SUPERVISOR_NO_CHANGE;
    }

    memset(&previous, 0, sizeof(previous));
    if (supervisor->has_active) previous = supervisor->active;
    had_previous = supervisor->has_active;
    supervisor->recovered = false;
    supervisor->state = MT_SUPERVISOR_QUIESCING;
    status = supervisor->ops.quiesce(supervisor->context);
    if (status != 0) {
        supervisor->state = had_previous ? MT_SUPERVISOR_ACTIVE :
                            MT_SUPERVISOR_IDLE;
        supervisor->last_result = MT_SUPERVISOR_QUIESCE_FAILED;
        supervisor->last_backend_error = status;
        return supervisor->last_result;
    }

    supervisor->state = MT_SUPERVISOR_LOADING;
    status = supervisor->ops.load(supervisor->context, selection);
    if (status != 0)
        return switch_failed(supervisor, MT_SUPERVISOR_LOAD_FAILED, status,
                             &previous, had_previous);

    supervisor->state = MT_SUPERVISOR_PROBING;
    status = supervisor->ops.probe(
        supervisor->context, selection->package.core_id,
        selection->package.abi_major, selection->package.abi_minor);
    if (status != 0)
        return switch_failed(supervisor, MT_SUPERVISOR_PROBE_FAILED, status,
                             &previous, had_previous);

    supervisor->state = MT_SUPERVISOR_ACTIVATING;
    status = supervisor->ops.activate(supervisor->context, selection);
    if (status != 0)
        return switch_failed(supervisor, MT_SUPERVISOR_ACTIVATE_FAILED, status,
                             &previous, had_previous);

    supervisor->active = *selection;
    supervisor->has_active = true;
    supervisor->state = MT_SUPERVISOR_ACTIVE;
    supervisor->last_result = MT_SUPERVISOR_OK;
    supervisor->last_backend_error = 0;
    return MT_SUPERVISOR_OK;
}

const char *mt_supervisor_state_name(mt_supervisor_state_t state)
{
    switch (state) {
    case MT_SUPERVISOR_IDLE: return "idle";
    case MT_SUPERVISOR_ACTIVE: return "active";
    case MT_SUPERVISOR_QUIESCING: return "quiescing";
    case MT_SUPERVISOR_LOADING: return "loading";
    case MT_SUPERVISOR_PROBING: return "probing";
    case MT_SUPERVISOR_ACTIVATING: return "activating";
    case MT_SUPERVISOR_ROLLING_BACK: return "rolling back";
    case MT_SUPERVISOR_RECOVERY: return "recovery";
    case MT_SUPERVISOR_FAILED: return "failed";
    default: return "unknown";
    }
}

const char *mt_supervisor_result_name(mt_supervisor_result_t result)
{
    switch (result) {
    case MT_SUPERVISOR_OK: return "ok";
    case MT_SUPERVISOR_NO_CHANGE: return "no change";
    case MT_SUPERVISOR_INVALID_ARGUMENT: return "invalid argument";
    case MT_SUPERVISOR_QUIESCE_FAILED: return "quiesce failed";
    case MT_SUPERVISOR_LOAD_FAILED: return "load failed";
    case MT_SUPERVISOR_PROBE_FAILED: return "probe failed";
    case MT_SUPERVISOR_ACTIVATE_FAILED: return "activate failed";
    case MT_SUPERVISOR_ROLLBACK_FAILED: return "rollback failed";
    default: return "unknown";
    }
}
