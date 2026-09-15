/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX32_DEBUG_H
#define MULTITINI_VECTREX32_DEBUG_H
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only test/debug observation, valid only inside the frame callback.
 * Array index is flat and zero based; -1 selects a scalar. No mutation API. */
int mt_v32_debug_number(const char *name, int array_index, double *value);
unsigned mt_v32_debug_line(void);
#ifdef __cplusplus
}
#endif
#endif
