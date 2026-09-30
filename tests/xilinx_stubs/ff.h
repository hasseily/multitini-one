/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XILINX_FF_H
#define TEST_XILINX_FF_H

#include <stdint.h>

typedef unsigned int UINT;
typedef uint8_t BYTE;
typedef uint64_t FSIZE_t;

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR = 1,
    FR_NO_FILE = 4,
    FR_EXIST = 8,
    FR_INVALID_PARAMETER = 19
} FRESULT;

typedef struct {
    unsigned placeholder;
} FATFS;

typedef struct {
    unsigned placeholder;
    unsigned kind;
    BYTE mode;
} FIL;

typedef struct {
    unsigned placeholder;
} DIR;

typedef struct {
    FSIZE_t fsize;
    BYTE fattrib;
    char fname[256];
} FILINFO;

#define FA_READ UINT32_C(0x01)
#define FA_WRITE UINT32_C(0x02)
#define FA_CREATE_ALWAYS UINT32_C(0x08)
#define FA_OPEN_EXISTING UINT32_C(0x00)
#define FA_OPEN_ALWAYS UINT32_C(0x10)
#define AM_DIR UINT32_C(0x10)

FRESULT f_stat(const char *path, FILINFO *info);
FRESULT f_mount(FATFS *file_system, const char *path, BYTE option);
FRESULT f_open(FIL *file, const char *path, BYTE mode);
FSIZE_t f_size(const FIL *file);
FRESULT f_read(FIL *file, void *buffer, UINT bytes_to_read,
               UINT *bytes_read);
FRESULT f_write(FIL *file, const void *buffer, UINT bytes_to_write,
                UINT *bytes_written);
FRESULT f_sync(FIL *file);
FRESULT f_close(FIL *file);
FRESULT f_unlink(const char *path);
FRESULT f_lseek(FIL *file, FSIZE_t offset);
FRESULT f_rename(const char *old_path, const char *new_path);
FRESULT f_opendir(DIR *directory, const char *path);
FRESULT f_readdir(DIR *directory, FILINFO *info);
FRESULT f_closedir(DIR *directory);

#endif
