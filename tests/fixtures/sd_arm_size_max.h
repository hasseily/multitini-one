/* SPDX-License-Identifier: GPL-3.0-only */
/* Model the ARM application's 32-bit metadata ceiling while running the
 * real storage implementation on a 64-bit host. This is not an ARM ABI test. */
#include <stddef.h>
#include <stdint.h>
#undef SIZE_MAX
#define SIZE_MAX UINT32_MAX
