/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_USB_CLANG_CL_COMPAT_H
#define MULTITINI_USB_CLANG_CL_COMPAT_H
/* CherryUSB's compiler table predates clang's MSVC target. These attributes
 * are supported by clang-cl; scope them to host fixtures, never the ARM BSP. */
#if defined(__clang__) && defined(_MSC_VER)
#ifndef __USED
#define __USED __attribute__((used))
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed, aligned(1)))
#endif
#ifndef __PACKED_STRUCT
#define __PACKED_STRUCT struct __attribute__((packed, aligned(1)))
#endif
#ifndef __PACKED_UNION
#define __PACKED_UNION union __attribute__((packed, aligned(1)))
#endif
#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif
#ifndef CLASS_INFO_DEFINE
/* Host fixtures call these descriptors directly; no embedded linker scan. */
#define CLASS_INFO_DEFINE __attribute__((used))
#endif
#endif
#endif
