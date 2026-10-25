/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_UCI_SERVICE_H
#define MULTITINI_C64_UCI_SERVICE_H
#include "multitini_c64_uci.h"
#include "multitini_clock.h"

typedef enum {
    C64_UCI_ACTION_NONE=0, C64_UCI_ACTION_FREEZE, C64_UCI_ACTION_REBOOT,
    C64_UCI_ACTION_MOUNT, C64_UCI_ACTION_UNMOUNT, C64_UCI_ACTION_SWAP
} c64_uci_action_t;
typedef struct {
    void *context;
    uint64_t (*microseconds)(void *);
    bool (*protected_path)(void *,const char *absolute_path);
    bool (*get_time)(void *,mt_clock_time_t *);
    bool (*set_time)(void *,const mt_clock_time_t *);
    /* Returns 0, 87, 88 or 89; log is bounded and need not fill capacity.
     * A UCI application merges existing title fields and ignores UCI=Off. */
    int (*apply_config)(void *,const char *absolute_path,char *log,size_t capacity);
    size_t (*sid_hwinfo)(void *,unsigned subcommand,uint8_t *data,size_t capacity);
} c64_uci_callbacks_t;

/* Software-only initialization; all MMIO stays gated by ABI and live caps. */
int c64_uci_service_init(mt_core_link_t *,const char *root,const c64_uci_callbacks_t *);
int c64_uci_service_configure_root(const char *root);
void c64_uci_service_set_home(const char *absolute_directory);
void c64_uci_service_set_reu_image(const char *absolute_path);
bool c64_uci_service_available(void);
bool c64_uci_service_active(void);
/* Drive A may change its device number through DOS M-W. Configured number is8. */
unsigned c64_uci_service_drive_address(void);
/* Refresh device9 identity and capability-gated Freeze/IRQ policy while
 * preserving HOLD. Call after expansion or device9 enable changes. */
void c64_uci_service_refresh_control(void);
bool c64_uci_service_cleanup_warning(void);
/* Explicit storage-cleanup retry: 0 complete,1 pending,-1 failed. Ambiguous
 * hardware completion remains latched and requires fabric reinitialization. */
int c64_uci_service_retry(void);
bool c64_uci_service_reload_required(void);
/* Explicit reload only: caller asserts guest reset and drains all memory
 * masters first. 0 means storage resources closed,1 pending,-1 error. This
 * retains HOLD and disables service until init after PL replacement. Failed
 * PCAP preflight must never resume the abandoned core. */
int c64_uci_service_prepare_reload(void);
int c64_uci_service_poll(unsigned budget);
/* Returns 0 once coherent and closed, 1 for cleanup in progress, -1 on error.
 * Storage/core handoff must wait for zero without recursively polling targets. */
int c64_uci_service_quiesce(void);
void c64_uci_service_reset(void);
/* Execute these only after service_poll returns, on the foreground owner.
 * FREEZE completes on menu close. REBOOT naturally loses its reply on reset. */
bool c64_uci_service_action(c64_uci_action_t *,char *path,size_t capacity,unsigned *device);
void c64_uci_service_action_done(int status);
/* No menu is opened. First call requests HOLD; callers poll until 0 (paused
 * and REU idle), 1 (waiting), or -1 (bounded timeout). end restores prior HOLD. */
int c64_uci_service_hold_begin(void);
void c64_uci_service_hold_end(void);
/* Ambiguous hardware completion must not release the running guest. A fresh
 * core/service initialization is required to clear this latched fault. */
void c64_uci_service_hold_fault(void);
#endif
