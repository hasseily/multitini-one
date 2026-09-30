/* SPDX-License-Identifier: GPL-3.0-only */
/* Native fixtures name the class record directly. The production ARM ELF
 * linker still retains CherryUSB's original discovery section unchanged. */
#include "usbh_core.h"
#undef CLASS_INFO_DEFINE
#define CLASS_INFO_DEFINE __USED __ALIGNED(1)
