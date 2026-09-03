/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_fatfs_time.h"

#include "ff.h"

/* Strong application override for the weak hook installed in BSP diskio.c. */
DWORD multitini_fatfs_get_fattime(void)
{
    return (DWORD)mt_fatfs_time_current();
}
