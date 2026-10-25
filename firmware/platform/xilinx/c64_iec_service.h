/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_IEC_SERVICE_H
#define MULTITINI_C64_IEC_SERVICE_H
#include "multitini_c64_iec_dos.h"
#include "multitini_core_link.h"
#include <stdbool.h>
#include <stddef.h>

/* Called on the foreground FatFs owner, never from an IRQ or progress
 * callback. Paths are normalized absolute paths below the configured root.
 * Return true for mounted images/journals and protected parent directories. */
typedef bool (*c64_iec_protected_fn)(void *context, const char *absolute_path);

/* Initializes software only. The endpoint remains disabled until enable.
 * root is the active C64 package's content_root, not a process-wide cwd.
 * Return zero on success, otherwise a Commodore DOS error code. */
int c64_iec_service_init(mt_core_link_t *link, const char *root, c64_iec_protected_fn protected_path, void *context);
int c64_iec_service_configure_root(const char *root);
bool c64_iec_service_available(void);
int c64_iec_service_enable(bool enabled);
/* Process at most budget received events, with one foreground storage owner.
 * Returns processed count, or -1 for an unavailable/faulted transport. Guest
 * DOS errors are returned through channel15, not as transport failures. */
int c64_iec_service_poll(unsigned budget);
/* Stop new bus bytes, drain already accepted events, then disable and durably
 * close complete writes. Incomplete writes retain the old target. A failure
 * preserves recovery files and must block storage handoff/core switching. */
int c64_iec_service_quiesce(void);
/* Reset command/channel state and cwd only after durable close succeeds. */
int c64_iec_service_reset(void);
bool c64_iec_service_active(void);
const char *c64_iec_service_cwd(void);
const char *c64_iec_service_status(void);
/* UCI uses the same DOS channels on the same foreground storage owner.
 * A lease prevents native IEC from consuming/mutating that channel while a
 * non-consuming UCI reply is in flight. Reset the target before quiesce. */
mt_iec_dos_t *c64_iec_service_dos(void);
/* Cleanup only: available after a storage fault, never for a new operation. */
mt_iec_dos_t *c64_iec_service_cleanup_dos(void);
bool c64_iec_service_channel_acquire(unsigned channel);
void c64_iec_service_channel_release(unsigned channel);
void c64_iec_service_write_failure(int code);
#endif
