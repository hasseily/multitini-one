/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_UCI_H
#define MULTITINI_C64_UCI_H
#include "multitini_core_link.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_UCI_COMMAND_CAPACITY 896U
#define MT_UCI_DATA_CAPACITY 896U
#define MT_UCI_STATUS_CAPACITY 255U
#define MT_UCI_TARGET_COUNT 16U

typedef struct {
    uint8_t data[MT_UCI_DATA_CAPACITY];
    uint8_t status[MT_UCI_STATUS_CAPACITY];
    size_t data_length, status_length;
    bool more;
} mt_uci_reply_t;

/* A target never waits for I/O in a callback. WAIT is retried by poll on a
 * later foreground pass. An available reply is immutable until next/abort.
 * The command array retains Ultimate's stale parameter bytes beyond length;
 * filenames and variable payloads must still be bounded by length. */
typedef enum { MT_UCI_WAIT = 0, MT_UCI_REPLY = 1, MT_UCI_FAILED = -1 } mt_uci_result_t;
typedef struct {
    void *context;
    mt_uci_result_t (*begin)(void *, const uint8_t command[MT_UCI_COMMAND_CAPACITY],
                             size_t length, mt_uci_reply_t *);
    mt_uci_result_t (*poll)(void *, mt_uci_reply_t *);
    mt_uci_result_t (*next)(void *, size_t consumed, mt_uci_reply_t *);
    /* Cancellation may need bounded asynchronous cleanup. Return WAIT until
     * buffers/writes are coherent, then REPLY; the engine stays abortable. */
    mt_uci_result_t (*abort)(void *, size_t consumed);
    void (*reset)(void *);
} mt_uci_target_t;

typedef struct {
    mt_core_link_t *link;
    mt_uci_target_t targets[MT_UCI_TARGET_COUNT];
    uint8_t command[MT_UCI_COMMAND_CAPACITY];
    mt_uci_reply_t reply;
    uint32_t snapshot, snapshot2, pending_op;
    uint8_t sequence, epoch, op_ack, target;
    bool initialized, active, waiting, no_reply, canceling, reset_pending;
    bool op_pending, reply_pending, awaiting_ack, idle_abort_ack;
    /* Failed cancellation keeps ownership and blocks lifecycle completion. */
    bool faulted, quiesced;
    size_t response_consumed;
} mt_uci_engine_t;

void mt_uci_engine_init(mt_uci_engine_t *, mt_core_link_t *);
void mt_uci_engine_target(mt_uci_engine_t *, unsigned, const mt_uci_target_t *);
/* One bounded foreground service step, no recursion into service_runtime. */
void mt_uci_engine_poll(mt_uci_engine_t *);
/* Repeated until true before closing storage or changing the FPGA image. */
bool mt_uci_engine_quiesce(mt_uci_engine_t *);
void mt_uci_reply_clear(mt_uci_reply_t *);
void mt_uci_reply_status(mt_uci_reply_t *, const char *);
void mt_uci_reply_text(mt_uci_reply_t *, const char *, const char *);
#endif
