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

static mt_link_result_t command_retry_busy(
    mt_core_link_t *link, uint32_t opcode, const uint32_t args[4],
    uint32_t max_polls, mt_link_progress_fn progress, void *progress_context)
{
    uint32_t attempt;
    mt_command_response_t response;
    mt_link_result_t result;

    for (attempt = 0U; attempt < max_polls; ++attempt) {
        result = mt_core_link_command(link, opcode, args, max_polls,
                                      progress, progress_context, &response);
        if (result != MT_LINK_REMOTE_ERROR ||
            response.status != MT_RESPONSE_BUSY) {
            return result;
        }
    }
    return MT_LINK_TIMEOUT;
}

static void abort_stream(mt_core_link_t *link, uint32_t max_polls,
                         mt_link_progress_fn progress, void *progress_context)
{
    const uint32_t args[4] = {0U, 0U, 0U, 0U};
    mt_command_response_t response;
    (void)mt_core_link_command(link, MT_COMMAND_STREAM_ABORT, args, max_polls,
                               progress, progress_context, &response);
}

mt_link_result_t mt_core_link_input_event(mt_core_link_t *link,
                                          uint16_t code, uint16_t value,
                                          uint32_t max_polls,
                                          mt_link_progress_fn progress,
                                          void *progress_context)
{
    const uint32_t args[4] = {code, value, 0U, 0U};
    if (max_polls == 0U) {
        return MT_LINK_BAD_ARGUMENT;
    }
    return command_retry_busy(link, MT_COMMAND_INPUT_EVENT, args, max_polls,
                              progress, progress_context);
}

mt_link_result_t mt_core_link_stream(mt_core_link_t *link, uint8_t kind,
                                     const uint8_t *data, size_t size,
                                     uint32_t max_polls,
                                     mt_link_progress_fn progress,
                                     void *progress_context)
{
    uint32_t args[4] = {0U, 0U, 0U, 0U};
    mt_command_response_t response;
    mt_link_result_t result;
    size_t offset;
    uint32_t abi_version;

    if (!link_valid(link) || data == NULL || size == 0U || max_polls == 0U) {
        return MT_LINK_BAD_ARGUMENT;
    }
#if SIZE_MAX > UINT32_MAX
    if (size > (size_t)UINT32_MAX) {
        return MT_LINK_BAD_ARGUMENT;
    }
#endif

    abi_version = link->read32(link->context, MT_REG_ABI_VERSION);

    args[0] = kind;
    args[1] = (uint32_t)size;
    result = mt_core_link_command(link, MT_COMMAND_STREAM_BEGIN, args,
                                  max_polls, progress, progress_context,
                                  &response);
    if (result != MT_LINK_OK) {
        return result;
    }

    if ((uint16_t)(abi_version >> 16U) == MT_ABI_MAJOR &&
        (uint16_t)abi_version >= 8U) {
        for (offset = 0U; offset < size;) {
            uint32_t word = 0U;
            uint32_t poll;
            size_t lane;
            const size_t remaining = size - offset;
            const size_t count = remaining < MT_STREAM_BURST_BYTES ?
                remaining : MT_STREAM_BURST_BYTES;
            for (lane = 0U; lane < count; ++lane)
                word |= (uint32_t)data[offset + lane] << (lane * 8U);
            for (poll = 0U; poll < max_polls; ++poll) {
                if ((link->read32(link->context, MT_REG_STREAM_STATUS) &
                     MT_STREAM_STATUS_READY) != 0U) {
                    link->write32(link->context, MT_REG_STREAM_DATA_BURST,
                                  word);
                    offset += count;
                    break;
                }
                if (progress != NULL) progress(progress_context);
            }
            if (poll == max_polls) {
                abort_stream(link, max_polls, progress, progress_context);
                return MT_LINK_TIMEOUT;
            }
        }
        for (offset = 0U; offset < max_polls; ++offset) {
            if ((link->read32(link->context, MT_REG_STREAM_STATUS) &
                 MT_STREAM_STATUS_DRAINED) != 0U)
                break;
            if (progress != NULL) progress(progress_context);
        }
        if (offset == max_polls) {
            abort_stream(link, max_polls, progress, progress_context);
            return MT_LINK_TIMEOUT;
        }

        args[0] = 0U;
        result = mt_core_link_command(link, MT_COMMAND_STREAM_END, args,
                                      max_polls, progress, progress_context,
                                      &response);
        if (result != MT_LINK_OK) {
            abort_stream(link, max_polls, progress, progress_context);
        }
        return result;
    }

    args[1] = 0U;
    for (offset = 0U; offset < size; ++offset) {
        args[0] = data[offset];
        result = command_retry_busy(link, MT_COMMAND_STREAM_DATA, args,
                                    max_polls, progress, progress_context);
        if (result != MT_LINK_OK) {
            abort_stream(link, max_polls, progress, progress_context);
            return result;
        }
    }

    args[0] = 0U;
    result = mt_core_link_command(link, MT_COMMAND_STREAM_END, args,
                                  max_polls, progress, progress_context,
                                  &response);
    if (result != MT_LINK_OK) {
        abort_stream(link, max_polls, progress, progress_context);
    }
    return result;
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
