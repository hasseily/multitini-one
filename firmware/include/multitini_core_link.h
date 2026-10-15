/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CORE_LINK_H
#define MULTITINI_CORE_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "multitini_regs.h"

typedef uint32_t (*mt_link_read32_fn)(void *context, uint32_t offset);
typedef void (*mt_link_write32_fn)(void *context, uint32_t offset, uint32_t value);
typedef void (*mt_link_progress_fn)(void *context);
typedef int (*mt_link_continue_fn)(void *context);

typedef struct {
    void *context;
    mt_link_read32_fn read32;
    mt_link_write32_fn write32;
    uint32_t next_sequence;
} mt_core_link_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    uint32_t arg0;
    uint32_t arg1;
} mt_command_response_t;

typedef enum {
    MT_LINK_OK = 0,
    MT_LINK_BAD_ARGUMENT = -1,
    MT_LINK_NO_SHELL = -2,
    MT_LINK_ABI_MISMATCH = -3,
    MT_LINK_TIMEOUT = -4,
    MT_LINK_REMOTE_ERROR = -5
} mt_link_result_t;

void mt_core_link_init(mt_core_link_t *link, void *context,
                       mt_link_read32_fn read32, mt_link_write32_fn write32);
void mt_core_link_init_mmio(mt_core_link_t *link, volatile void *base);
mt_link_result_t mt_core_link_probe(mt_core_link_t *link);
mt_link_result_t mt_core_link_command(mt_core_link_t *link, uint32_t opcode,
                                      const uint32_t args[4], uint32_t max_polls,
                                      mt_link_progress_fn progress,
                                      void *progress_context,
                                      mt_command_response_t *response);
mt_link_result_t mt_core_link_stream(mt_core_link_t *link, uint8_t kind,
                                     const uint8_t *data, size_t size,
                                     uint32_t max_polls,
                                     mt_link_progress_fn progress,
                                     void *progress_context);
/* Replace the first prefix_size bytes on the wire while retaining every later
 * byte from data. Both source buffers remain unchanged. Uses the same bounded
 * FIFO, completion and abort behavior as an ordinary stream. */
mt_link_result_t mt_core_link_stream_with_prefix(
    mt_core_link_t *link, uint8_t kind, const uint8_t *data, size_t size,
    const uint8_t *prefix, size_t prefix_size, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context);
/* The optional continuation predicate bounds a stream by an external deadline
 * instead of assuming that a raw MMIO poll count represents elapsed time. A
 * false result aborts an open stream and returns MT_LINK_TIMEOUT. */
mt_link_result_t mt_core_link_stream_until(
    mt_core_link_t *link, uint8_t kind, const uint8_t *data, size_t size,
    uint32_t max_polls, mt_link_progress_fn progress, void *progress_context,
    mt_link_continue_fn continue_wait, void *continue_context);
mt_link_result_t mt_core_link_abort_stream(
    mt_core_link_t *link, uint32_t max_polls,
    mt_link_progress_fn progress, void *progress_context);
mt_link_result_t mt_core_link_input_event(mt_core_link_t *link,
                                          uint16_t code, uint16_t value,
                                          uint32_t max_polls,
                                          mt_link_progress_fn progress,
                                          void *progress_context);
const char *mt_link_result_name(mt_link_result_t result);

#endif
