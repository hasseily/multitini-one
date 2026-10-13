/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VERSION_H
#define MULTITINI_VERSION_H

#include <stdint.h>

/* Multitini releases use M<major>.<minor>.<patch>.
 * The numeric identity packs 'M', major, minor and patch into four bytes. */
#define MT_FIRMWARE_VERSION "M0.2.2"
#define MT_FIRMWARE_BUILD_ID UINT32_C(0x4D000202)

#endif
