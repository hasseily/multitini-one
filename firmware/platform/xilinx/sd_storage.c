/* SPDX-License-Identifier: GPL-3.0-only */
#include "sd_storage.h"

#include <limits.h>
#include <string.h>

#include "ff.h"

#define MT_SD_VOLUME "0:/"
#define MT_SD_JOURNAL_HEADER_SIZE 24U
#define MT_SD_JOURNAL_TRAILER_SIZE 16U

static const uint8_t journal_magic[8] = {
    'M', 'T', 'J', 'R', 'N', '0', '0', '1'
};
static const uint8_t journal_end_magic[8] = {
    'M', 'T', 'E', 'N', 'D', '0', '0', '1'
};

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

static size_t fsize_to_size_saturated(FSIZE_t value)
{
    return value > (FSIZE_t)SIZE_MAX ? SIZE_MAX : (size_t)value;
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

static mt_sd_result_t write_open_file(FIL *file, const uint8_t *src,
                                      size_t size)
{
    size_t total = 0U;
    while (total < size) {
        const size_t remaining = size - total;
        const UINT request = remaining > (size_t)UINT_MAX ? UINT_MAX :
                             (UINT)remaining;
        UINT written = 0U;
        const FRESULT backend_status =
            f_write(file, src + total, request, &written);
        total += (size_t)written;
        if (backend_status != FR_OK || written != request) {
            return result(MT_SD_WRITE_FAILED, backend_status, total);
        }
    }
    return result(MT_SD_OK, FR_OK, total);
}

static uint32_t load_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static int paths_equal(const char *left, const char *right)
{
    for (;;) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
        if (a == '\0') return 1;
    }
}

static mt_sd_result_t write_file_synced(const char *path,
                                        const uint8_t *src, size_t size)
{
    FIL file;
    FRESULT backend_status = f_open(&file, path,
                                    FA_WRITE | FA_CREATE_ALWAYS);
    FRESULT close_status;
    mt_sd_result_t write_result;
    if (backend_status != FR_OK) {
        return result(MT_SD_OPEN_FAILED, backend_status, 0U);
    }
    write_result = write_open_file(&file, src, size);
    if (write_result.status == MT_SD_OK) {
        backend_status = f_sync(&file);
        if (backend_status != FR_OK) {
            write_result = result(MT_SD_SYNC_FAILED, backend_status,
                                  write_result.size);
        }
    }
    close_status = f_close(&file);
    if (write_result.status == MT_SD_OK && close_status != FR_OK) {
        write_result = result(MT_SD_CLOSE_FAILED, close_status,
                              write_result.size);
    }
    return write_result;
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
        read_result = result(MT_SD_TOO_LARGE, FR_OK,
                             fsize_to_size_saturated(backend_size));
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
                             fsize_to_size_saturated(backend_size));
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
        if (info.fsize > (FSIZE_t)SIZE_MAX) {
            const mt_sd_result_t size_result =
                result(MT_SD_TOO_LARGE, FR_OK, SIZE_MAX);
            (void)f_closedir(&directory);
            return size_result;
        }
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

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    uint8_t header[MT_SD_JOURNAL_HEADER_SIZE];
    uint8_t trailer[MT_SD_JOURNAL_TRAILER_SIZE];
    FIL file;
    FRESULT backend_status;
    FSIZE_t file_size;
    uint32_t payload_size;
    uint32_t payload_crc;
    mt_sd_result_t mount_result;
    mt_sd_result_t read_result;
    mt_sd_result_t write_result;

    if (path == NULL || journal_path == NULL || recovery_buffer == NULL ||
        path[0] == '\0' || journal_path[0] == '\0' ||
        paths_equal(path, journal_path)) {
        return result(MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0U);
    }
    mount_result = mt_sd_storage_mount();
    if (mount_result.status != MT_SD_OK) return mount_result;

    backend_status = f_open(&file, journal_path, FA_READ | FA_OPEN_EXISTING);
    if (backend_status == FR_NO_FILE) {
        return result(MT_SD_OK, FR_OK, 0U);
    }
    if (backend_status != FR_OK) {
        return result(MT_SD_OPEN_FAILED, backend_status, 0U);
    }

    file_size = f_size(&file);
    if (file_size < (FSIZE_t)(MT_SD_JOURNAL_HEADER_SIZE +
                              MT_SD_JOURNAL_TRAILER_SIZE)) {
        return close_read_file(
            &file, result(MT_SD_JOURNAL_INVALID, FR_OK, (size_t)file_size));
    }
    read_result = read_open_file(&file, header, sizeof(header));
    if (read_result.status != MT_SD_OK) {
        return close_read_file(&file, read_result);
    }
    payload_size = load_u32_le(header + 8U);
    payload_crc = load_u32_le(header + 16U);
    if (memcmp(header, journal_magic, sizeof(journal_magic)) != 0 ||
        load_u32_le(header + 12U) != ~payload_size ||
        load_u32_le(header + 20U) != ~payload_crc ||
        file_size != (FSIZE_t)MT_SD_JOURNAL_HEADER_SIZE + payload_size +
                     MT_SD_JOURNAL_TRAILER_SIZE) {
        return close_read_file(
            &file, result(MT_SD_JOURNAL_INVALID, FR_OK, payload_size));
    }
    if (payload_size > capacity) {
        return close_read_file(
            &file, result(MT_SD_TOO_LARGE, FR_OK, payload_size));
    }
    read_result = read_open_file(&file, recovery_buffer, payload_size);
    if (read_result.status == MT_SD_OK) {
        read_result = read_open_file(&file, trailer, sizeof(trailer));
    }
    read_result = close_read_file(&file, read_result);
    if (read_result.status != MT_SD_OK) return read_result;
    if (memcmp(trailer, journal_end_magic, sizeof(journal_end_magic)) != 0 ||
        load_u32_le(trailer + 8U) != payload_size ||
        load_u32_le(trailer + 12U) != payload_crc ||
        crc32_bytes(recovery_buffer, payload_size) != payload_crc) {
        return result(MT_SD_JOURNAL_INVALID, FR_OK, payload_size);
    }
    write_result = write_file_synced(path, recovery_buffer, payload_size);
    if (write_result.status != MT_SD_OK) return write_result;
    backend_status = f_unlink(journal_path);
    if (backend_status != FR_OK && backend_status != FR_NO_FILE) {
        return result(MT_SD_UNLINK_FAILED, backend_status, payload_size);
    }
    return result(MT_SD_OK, FR_OK, payload_size);
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size)
{
    uint8_t header[MT_SD_JOURNAL_HEADER_SIZE];
    uint8_t trailer[MT_SD_JOURNAL_TRAILER_SIZE];
    uint32_t payload_crc;
    FIL file;
    FRESULT backend_status;
    FRESULT close_status;
    mt_sd_result_t write_result;

    if (path == NULL || journal_path == NULL || src == NULL ||
        path[0] == '\0' || journal_path[0] == '\0' ||
        paths_equal(path, journal_path)) {
        return result(MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0U);
    }
    if (size > UINT32_MAX) return result(MT_SD_TOO_LARGE, FR_OK, size);
    write_result = mt_sd_storage_mount();
    if (write_result.status != MT_SD_OK) return write_result;

    payload_crc = crc32_bytes(src, size);
    memcpy(header, journal_magic, sizeof(journal_magic));
    store_u32_le(header + 8U, (uint32_t)size);
    store_u32_le(header + 12U, ~(uint32_t)size);
    store_u32_le(header + 16U, payload_crc);
    store_u32_le(header + 20U, ~payload_crc);
    memcpy(trailer, journal_end_magic, sizeof(journal_end_magic));
    store_u32_le(trailer + 8U, (uint32_t)size);
    store_u32_le(trailer + 12U, payload_crc);

    backend_status = f_open(&file, journal_path,
                            FA_WRITE | FA_CREATE_ALWAYS);
    if (backend_status != FR_OK) {
        return result(MT_SD_OPEN_FAILED, backend_status, 0U);
    }
    write_result = write_open_file(&file, header, sizeof(header));
    if (write_result.status == MT_SD_OK)
        write_result = write_open_file(&file, src, size);
    if (write_result.status == MT_SD_OK)
        write_result = write_open_file(&file, trailer, sizeof(trailer));
    if (write_result.status == MT_SD_OK) {
        backend_status = f_sync(&file);
        if (backend_status != FR_OK) {
            write_result = result(MT_SD_SYNC_FAILED, backend_status, size);
        }
    }
    close_status = f_close(&file);
    if (write_result.status == MT_SD_OK && close_status != FR_OK) {
        write_result = result(MT_SD_CLOSE_FAILED, close_status, size);
    }
    if (write_result.status != MT_SD_OK) return write_result;

    /* The synced journal remains valid until the ordinary file is complete. */
    write_result = write_file_synced(path, src, size);
    if (write_result.status != MT_SD_OK) return write_result;
    backend_status = f_unlink(journal_path);
    if (backend_status != FR_OK && backend_status != FR_NO_FILE) {
        return result(MT_SD_UNLINK_FAILED, backend_status, size);
    }
    return result(MT_SD_OK, FR_OK, size);
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
    case MT_SD_WRITE_FAILED: return "write failed";
    case MT_SD_SYNC_FAILED: return "sync failed";
    case MT_SD_CLOSE_FAILED: return "close failed";
    case MT_SD_UNLINK_FAILED: return "unlink failed";
    case MT_SD_JOURNAL_INVALID: return "journal invalid";
    case MT_SD_DIR_OPEN_FAILED: return "directory open failed";
    case MT_SD_DIR_READ_FAILED: return "directory read failed";
    case MT_SD_DIR_CLOSE_FAILED: return "directory close failed";
    case MT_SD_UNMOUNT_FAILED: return "unmount failed";
    }
    return "unknown";
}
