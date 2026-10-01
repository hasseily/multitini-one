/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SCREENSHOT_SERVICE_H
#define MULTITINI_SCREENSHOT_SERVICE_H
#include "multitini_screenshot.h"
typedef struct {
    uint32_t (*read)(uint32_t offset);
    uint32_t (*milliseconds)(void);
    /* Invoked only between complete row writes; must not reenter storage. */
    void (*progress)(void);
} mt_screenshot_service_ops_t;
void mt_screenshot_service_init(const mt_screenshot_service_ops_t *ops);
/* false: filtered video at its displayed size; true: the complete output,
 * including letterbox, borders and visible UI. */
bool mt_screenshot_service_request(bool full_output);
/* Cancel before a core switch/unmount. False means an owned file handle still
 * needs closing; callers retry without unmounting the storage owner. */
bool mt_screenshot_service_cancel(void);
void mt_screenshot_service_poll(uint32_t core_id, const char *core_name,
    uint8_t display_mode, uint32_t border_config,
    const mt_ui_frame_t *ui, bool ui_visible);
bool mt_screenshot_service_busy(void);
const char *mt_screenshot_service_status(void);
const char *mt_screenshot_service_last_path(void);
#endif
