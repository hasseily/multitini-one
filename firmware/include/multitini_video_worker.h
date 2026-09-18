/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_WORKER_H
#define MULTITINI_VIDEO_WORKER_H

#include <stdint.h>

#define MT_VIDEO_WORKER_ENTRY UINT32_C(0x20000000)
#define MT_VIDEO_WORKER_PRIVATE_BYTES UINT32_C(0x01000000)
#define MT_VIDEO_WORKER_SHARED_BASE UINT32_C(0x21000000)
#define MT_VIDEO_WORKER_SHARED_BYTES UINT32_C(0x00100000)
#define MT_VIDEO_WORKER_MAGIC UINT32_C(0x4D545657)
#define MT_VIDEO_WORKER_ABI UINT32_C(1)
#define MT_VIDEO_WORKER_PAYLOAD_BYTES 4096U

/* One outstanding job. CPU0 owns all request fields and input. CPU1 owns
 * ready, completion fields and output. CPU0 may reuse either buffer only
 * after complete_sequence equals request_sequence. Sequence zero is idle.
 * No pointer crosses CPUs: the worker cannot be sent PL or CPU0 addresses.
 * The whole containing DDR section is normal noncacheable on both CPUs.
 */
typedef struct {
    uint32_t magic;
    uint32_t abi;
    uint32_t request_epoch;
    uint32_t input_size;
    uint32_t request_sequence;
    uint32_t reserved_request[3];
    uint32_t ready;
    uint32_t complete_epoch;
    int32_t result;
    uint32_t output_size;
    uint32_t complete_sequence;
    uint32_t reserved_completion[3];
    uint8_t input[MT_VIDEO_WORKER_PAYLOAD_BYTES];
    uint8_t output[MT_VIDEO_WORKER_PAYLOAD_BYTES];
} mt_video_worker_mailbox_t;

/* Dispatch an immutable byte payload using only CPU1-private memory.
 * On entry *output_size is capacity; on success set it to bytes written.
 * Zero means success. The hook must never perform device or PL I/O. */
int mt_video_worker_execute(const void *input, uint32_t input_size,
                            void *output, uint32_t *output_size);

#endif
