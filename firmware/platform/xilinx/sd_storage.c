/* SPDX-License-Identifier: GPL-3.0-only */
#include "sd_storage.h"

#include <limits.h>

#include "ff.h"

#define MT_SD_VOLUME "0:/"

static FATFS sd_file_system;
static bool sd_mounted;

static mt_sd_result_t result(mt_sd_status_t status, FRESULT backend_status,
                             size_t size)
{
    mt_sd_result_t value;
    value.status = status;
    value.backend_status = (unsigned)backend_status;
    value.size = size;
    return value;
}

mt_sd_result_t mt_sd_storage_mount(void)
{
    FRESULT backend_status;
    if (sd_mounted) return result(MT_SD_OK, FR_OK, 0U);

    backend_status = f_mount(&sd_file_system, MT_SD_VOLUME, 1U);
    if (backend_status != FR_OK) {
        return result(MT_SD_MOUNT_FAILED, backend_status, 0U);
    }
    sd_mounted = true;
    return result(MT_SD_OK, FR_OK, 0U);
}

static mt_sd_result_t read_open_file(FIL *file, uint8_t *dst,
                                     size_t file_size)
{
    size_t total = 0U;
    while (total < file_size) {
        const size_t remaining = file_size - total;
        const UINT request = remaining > (size_t)UINT_MAX ? UINT_MAX :
                             (UINT)remaining;
        UINT received = 0U;
        const FRESULT backend_status =
            f_read(file, dst + total, request, &received);
        total += (size_t)received;
        if (backend_status != FR_OK || received != request) {
            return result(MT_SD_READ_FAILED, backend_status, total);
        }
    }
    return result(MT_SD_OK, FR_OK, total);
}

static mt_sd_result_t close_read_file(FIL *file, mt_sd_result_t read_result)
{
    const FRESULT close_status = f_close(file);
    if (read_result.status == MT_SD_OK && close_status != FR_OK) {
        return result(MT_SD_CLOSE_FAILED, close_status, read_result.size);
    }
    return read_result;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    FIL file;
    FRESULT backend_status;
    FSIZE_t backend_size;
    mt_sd_result_t mount_result;
    mt_sd_result_t read_result;

    if (path == NULL || dst == NULL) {
        return result(MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0U);
    }
    mount_result = mt_sd_storage_mount();
    if (mount_result.status != MT_SD_OK) return mount_result;

    backend_status = f_open(&file, path, FA_READ | FA_OPEN_EXISTING);
    if (backend_status != FR_OK) {
        return result(MT_SD_OPEN_FAILED, backend_status, 0U);
    }

    backend_size = f_size(&file);
    if (backend_size > (FSIZE_t)capacity) {
        read_result = result(MT_SD_TOO_LARGE, FR_OK, (size_t)backend_size);
        return close_read_file(&file, read_result);
    }
    read_result = read_open_file(&file, dst, (size_t)backend_size);
    return close_read_file(&file, read_result);
}

mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *dst,
                                             size_t expected_size)
{
    FIL file;
    FRESULT backend_status;
    FSIZE_t backend_size;
    mt_sd_result_t mount_result;
    mt_sd_result_t read_result;

    if (path == NULL || dst == NULL) {
        return result(MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0U);
    }
    mount_result = mt_sd_storage_mount();
    if (mount_result.status != MT_SD_OK) return mount_result;

    backend_status = f_open(&file, path, FA_READ | FA_OPEN_EXISTING);
    if (backend_status != FR_OK) {
        return result(MT_SD_OPEN_FAILED, backend_status, 0U);
    }

    backend_size = f_size(&file);
    if (backend_size != (FSIZE_t)expected_size) {
        read_result = result(MT_SD_SIZE_MISMATCH, FR_OK,
                             (size_t)backend_size);
        return close_read_file(&file, read_result);
    }
    read_result = read_open_file(&file, dst, expected_size);
    return close_read_file(&file, read_result);
}

mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context)
{
    DIR directory;
    FILINFO info;
    FRESULT backend_status;
    mt_sd_result_t mount_result;
    size_t count = 0U;

    if (path == NULL || callback == NULL) {
        return result(MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0U);
    }
    mount_result = mt_sd_storage_mount();
    if (mount_result.status != MT_SD_OK) return mount_result;

    backend_status = f_opendir(&directory, path);
    if (backend_status != FR_OK) {
        return result(MT_SD_DIR_OPEN_FAILED, backend_status, 0U);
    }

    for (;;) {
        backend_status = f_readdir(&directory, &info);
        if (backend_status != FR_OK) {
            const mt_sd_result_t read_result =
                result(MT_SD_DIR_READ_FAILED, backend_status, count);
            (void)f_closedir(&directory);
            return read_result;
        }
        if (info.fname[0] == '\0') break;
        ++count;
        if (callback(context, info.fname, (size_t)info.fsize,
                     (info.fattrib & AM_DIR) != 0U) != 0) {
            break;
        }
    }

    backend_status = f_closedir(&directory);
    if (backend_status != FR_OK) {
        return result(MT_SD_DIR_CLOSE_FAILED, backend_status, count);
    }
    return result(MT_SD_OK, FR_OK, count);
}

mt_sd_result_t mt_sd_storage_unmount(void)
{
    FRESULT backend_status;
    if (!sd_mounted) return result(MT_SD_OK, FR_OK, 0U);

    backend_status = f_mount(NULL, MT_SD_VOLUME, 1U);
    if (backend_status != FR_OK) {
        return result(MT_SD_UNMOUNT_FAILED, backend_status, 0U);
    }
    sd_mounted = false;
    return result(MT_SD_OK, FR_OK, 0U);
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    switch (status) {
    case MT_SD_OK: return "ok";
    case MT_SD_INVALID_ARGUMENT: return "invalid argument";
    case MT_SD_MOUNT_FAILED: return "mount failed";
    case MT_SD_OPEN_FAILED: return "open failed";
    case MT_SD_SIZE_MISMATCH: return "size mismatch";
    case MT_SD_TOO_LARGE: return "too large";
    case MT_SD_READ_FAILED: return "read failed";
    case MT_SD_CLOSE_FAILED: return "close failed";
    case MT_SD_DIR_OPEN_FAILED: return "directory open failed";
    case MT_SD_DIR_READ_FAILED: return "directory read failed";
    case MT_SD_DIR_CLOSE_FAILED: return "directory close failed";
    case MT_SD_UNMOUNT_FAILED: return "unmount failed";
    }
    return "unknown";
}
