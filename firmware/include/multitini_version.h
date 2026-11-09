/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VERSION_H
#define MULTITINI_VERSION_H

#include <stdint.h>

/* Multitini releases use M<major>.<minor>.<patch>, with an optional dev suffix.
 * Development builds can use a separate numeric identity. */
#define MT_FIRMWARE_VERSION "M0.8.3dev2"
#define MT_FIRMWARE_BUILD_ID UINT32_C(0x4D000871)

#endif
