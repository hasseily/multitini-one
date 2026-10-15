/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_USB_DISKIO_H
#define TEST_USB_DISKIO_H
#include "xil_types.h"
typedef unsigned UINT;
typedef u8 BYTE;
typedef u32 DWORD;
typedef u32 LBA_t;
typedef u8 DSTATUS;
typedef enum { RES_OK,RES_ERROR } DRESULT;
#define STA_NOINIT 1U
#define STA_NODISK 2U
#define CTRL_SYNC 0U
#define GET_SECTOR_COUNT 1U
DSTATUS disk_initialize(BYTE);
DSTATUS disk_status(BYTE);
DRESULT disk_ioctl(BYTE,BYTE,void *);
DRESULT disk_read(BYTE,BYTE *,LBA_t,UINT);
DRESULT disk_write(BYTE,const BYTE *,LBA_t,UINT);
#endif
