/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_link.h"

#include <stddef.h>

static uint32_t mmio_read32(void *context, uint32_t offset)
{
    volatile uint32_t *const base = (volatile uint32_t *)context;
    return base[offset / sizeof(uint32_t)];
}

static void mmio_write32(void *context, uint32_t offset, uint32_t value)
{
    volatile uint32_t *const base = (volatile uint32_t *)context;
    base[offset / sizeof(uint32_t)] = value;
}

void mt_core_link_init(mt_core_link_t *link, void *context,
                       mt_link_read32_fn read32, mt_link_write32_fn write32)
{
    if (link == NULL) {
        return;
    }
    link->context = context;
    link->read32 = read32;
    link->write32 = write32;
    link->next_sequence = 1U;
}

void mt_core_link_init_mmio(mt_core_link_t *link, volatile void *base)
{
    mt_core_link_init(link, (void *)base, mmio_read32, mmio_write32);
}

static int link_valid(const mt_core_link_t *link)
{
    return link != NULL && link->read32 != NULL && link->write32 != NULL;
}

mt_link_result_t mt_core_link_probe(mt_core_link_t *link)
{
    uint32_t version;
    if (!link_valid(link)) {
        return MT_LINK_BAD_ARGUMENT;
    }
    if (link->read32(link->context, MT_REG_MAGIC) != MT_MAGIC) {
        return MT_LINK_NO_SHELL;
    }
    version = link->read32(link->context, MT_REG_ABI_VERSION);
    if ((version >> 16) != MT_ABI_MAJOR) {
        return MT_LINK_ABI_MISMATCH;
    }
    return MT_LINK_OK;
}

mt_link_result_t mt_core_link_command(mt_core_link_t *link, uint32_t opcode,
                                      const uint32_t args[4], uint32_t max_polls,
                                      mt_link_progress_fn progress,
                                      void *progress_context,
                                      mt_command_response_t *response)
{
    uint32_t sequence;
    uint32_t poll;
    uint32_t response_status;

    if (!link_valid(link) || args == NULL || response == NULL || max_polls == 0U) {
        return MT_LINK_BAD_ARGUMENT;
    }

    sequence = link->next_sequence++;
    if (sequence == 0U) {
        sequence = link->next_sequence++;
    }

    link->write32(link->context, MT_REG_COMMAND_ARG0, args[0]);
    link->write32(link->context, MT_REG_COMMAND_ARG1, args[1]);
    link->write32(link->context, MT_REG_COMMAND_ARG2, args[2]);
    link->write32(link->context, MT_REG_COMMAND_ARG3, args[3]);
    link->write32(link->context, MT_REG_COMMAND_OPCODE, opcode);
    link->write32(link->context, MT_REG_COMMAND_SEQ, sequence);
    link->write32(link->context, MT_REG_COMMAND_DOORBELL, sequence);

    for (poll = 0U; poll < max_polls; ++poll) {
        if (progress != NULL) {
            progress(progress_context);
        }
        if (link->read32(link->context, MT_REG_RESPONSE_SEQ) == sequence) {
            response_status = link->read32(link->context, MT_REG_RESPONSE_STATUS);
            response->sequence = sequence;
            response->status = response_status;
            response->arg0 = link->read32(link->context, MT_REG_RESPONSE_ARG0);
            response->arg1 = link->read32(link->context, MT_REG_RESPONSE_ARG1);
            return response_status == MT_RESPONSE_OK ? MT_LINK_OK : MT_LINK_REMOTE_ERROR;
        }
    }
    return MT_LINK_TIMEOUT;
}

const char *mt_link_result_name(mt_link_result_t result)
{
    switch (result) {
    case MT_LINK_OK: return "ok";
    case MT_LINK_BAD_ARGUMENT: return "bad argument";
    case MT_LINK_NO_SHELL: return "shell not found";
    case MT_LINK_ABI_MISMATCH: return "ABI mismatch";
    case MT_LINK_TIMEOUT: return "timeout";
    case MT_LINK_REMOTE_ERROR: return "remote error";
    }
    return "unknown";
}
