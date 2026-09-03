/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_supervisor.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int quiesce_result;
    int load_result;
    int probe_result;
    int activate_result;
    int recover_result;
    unsigned quiesce_calls;
    unsigned load_calls;
    unsigned probe_calls;
    unsigned activate_calls;
    unsigned recover_calls;
    char order[32];
    size_t order_size;
} fake_backend_t;

static void record(fake_backend_t *backend, char value)
{
    backend->order[backend->order_size++] = value;
    backend->order[backend->order_size] = '\0';
}

static int fake_quiesce(void *context)
{
    fake_backend_t *const backend = context;
    ++backend->quiesce_calls;
    record(backend, 'Q');
    return backend->quiesce_result;
}

static int fake_load(void *context, const mt_core_selection_t *selection)
{
    fake_backend_t *const backend = context;
    assert(mt_supervisor_selection_valid(selection));
    ++backend->load_calls;
    record(backend, 'L');
    return backend->load_result;
}

static int fake_probe(void *context, uint32_t core_id,
                      uint16_t abi_major, uint16_t abi_minor)
{
    fake_backend_t *const backend = context;
    assert(core_id != 0U);
    assert(abi_major == 0U && abi_minor == 6U);
    ++backend->probe_calls;
    record(backend, 'P');
    return backend->probe_result;
}

static int fake_activate(void *context,
                         const mt_core_selection_t *selection)
{
    fake_backend_t *const backend = context;
    assert(mt_supervisor_selection_valid(selection));
    ++backend->activate_calls;
    record(backend, 'A');
    return backend->activate_result;
}

static int fake_recover(void *context)
{
    fake_backend_t *const backend = context;
    ++backend->recover_calls;
    record(backend, 'R');
    return backend->recover_result;
}

static mt_core_selection_t selection(const char *id, const char *variant)
{
    mt_core_selection_t value;
    memset(&value, 0, sizeof(value));
    value.package.schema = 1U;
    value.package.core_id = strcmp(id, "c64") == 0 ?
                            UINT32_C(0x43363420) : UINT32_C(0x47424320);
    value.package.abi_major = 0U;
    value.package.abi_minor = 6U;
    strcpy(value.package.id, id);
    strcpy(value.package.name, strcmp(id, "c64") == 0 ?
           "Commodore 64" : "Game Boy Color");
    strcpy(value.package.content_root, "0:/multitini/cores/test");
    value.package.variant_count = 1U;
    strcpy(value.package.variants[0].id, variant);
    strcpy(value.package.variants[0].label, variant);
    strcpy(value.package.variants[0].filename, "test.bin");
    value.package.variants[0].bitstream_size = 4000000U;
    strcpy(value.directory, "0:/multitini/cores/test");
    return value;
}

static mt_supervisor_t make_supervisor(fake_backend_t *backend)
{
    mt_supervisor_t supervisor;
    const mt_supervisor_ops_t ops = {
        fake_quiesce, fake_load, fake_probe, fake_activate, fake_recover
    };
    mt_supervisor_init(&supervisor, &ops, backend);
    return supervisor;
}

int main(void)
{
    fake_backend_t backend = {0};
    mt_supervisor_t supervisor = make_supervisor(&backend);
    mt_core_selection_t c64 = selection("c64", "pal");
    mt_core_selection_t gbc = selection("gbc", "color");

    assert(mt_supervisor_set_initial(&supervisor, &c64) == MT_SUPERVISOR_OK);
    assert(supervisor.state == MT_SUPERVISOR_ACTIVE);
    assert(mt_supervisor_switch(&supervisor, &c64) == MT_SUPERVISOR_NO_CHANGE);
    assert(backend.order_size == 0U);

    assert(mt_supervisor_switch(&supervisor, &gbc) == MT_SUPERVISOR_OK);
    assert(strcmp(backend.order, "QLPA") == 0);
    assert(strcmp(supervisor.active.package.id, "gbc") == 0);
    assert(supervisor.state == MT_SUPERVISOR_ACTIVE);
    assert(!supervisor.recovered);

    memset(&backend, 0, sizeof(backend));
    supervisor = make_supervisor(&backend);
    assert(mt_supervisor_set_initial(&supervisor, &c64) == MT_SUPERVISOR_OK);
    backend.quiesce_result = -11;
    assert(mt_supervisor_switch(&supervisor, &gbc) ==
           MT_SUPERVISOR_QUIESCE_FAILED);
    assert(strcmp(backend.order, "Q") == 0);
    assert(strcmp(supervisor.active.package.id, "c64") == 0);
    assert(!supervisor.recovered);

    memset(&backend, 0, sizeof(backend));
    supervisor = make_supervisor(&backend);
    assert(mt_supervisor_set_initial(&supervisor, &c64) == MT_SUPERVISOR_OK);
    backend.probe_result = -22;
    assert(mt_supervisor_switch(&supervisor, &gbc) ==
           MT_SUPERVISOR_ROLLBACK_FAILED);
    assert(strcmp(backend.order, "QLPLP") == 0);
    assert(supervisor.state == MT_SUPERVISOR_FAILED);
    assert(!supervisor.has_active);

    memset(&backend, 0, sizeof(backend));
    supervisor = make_supervisor(&backend);
    assert(mt_supervisor_set_initial(&supervisor, &c64) == MT_SUPERVISOR_OK);
    backend.activate_result = -33;
    assert(mt_supervisor_switch(&supervisor, &gbc) ==
           MT_SUPERVISOR_ROLLBACK_FAILED);
    assert(strcmp(backend.order, "QLPALPA") == 0);

    memset(&backend, 0, sizeof(backend));
    supervisor = make_supervisor(&backend);
    backend.load_result = -44;
    assert(mt_supervisor_switch(&supervisor, &gbc) ==
           MT_SUPERVISOR_LOAD_FAILED);
    assert(strcmp(backend.order, "QLR") == 0);
    assert(supervisor.recovered);
    assert(supervisor.state == MT_SUPERVISOR_IDLE);

    memset(&backend, 0, sizeof(backend));
    supervisor = make_supervisor(&backend);
    backend.recover_result = -55;
    backend.load_result = -44;
    assert(mt_supervisor_switch(&supervisor, &gbc) ==
           MT_SUPERVISOR_ROLLBACK_FAILED);
    assert(supervisor.state == MT_SUPERVISOR_FAILED);

    assert(mt_supervisor_switch(NULL, &gbc) ==
           MT_SUPERVISOR_INVALID_ARGUMENT);
    gbc.variant_index = 2U;
    assert(!mt_supervisor_selection_valid(&gbc));
    assert(strcmp(mt_supervisor_state_name(MT_SUPERVISOR_LOADING),
                  "loading") == 0);
    assert(strcmp(mt_supervisor_result_name(MT_SUPERVISOR_PROBE_FAILED),
                  "probe failed") == 0);
    return 0;
}
