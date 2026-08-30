/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SD_STORAGE_H
#define MULTITINI_SD_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MT_SD_OK = 0,
    MT_SD_INVALID_ARGUMENT,
    MT_SD_MOUNT_FAILED,
    MT_SD_OPEN_FAILED,
    MT_SD_SIZE_MISMATCH,
    MT_SD_TOO_LARGE,
    MT_SD_READ_FAILED,
    MT_SD_CLOSE_FAILED,
    MT_SD_DIR_OPEN_FAILED,
    MT_SD_DIR_READ_FAILED,
    MT_SD_DIR_CLOSE_FAILED,
    MT_SD_UNMOUNT_FAILED
} mt_sd_status_t;

typedef struct {
    mt_sd_status_t status;
    unsigned backend_status;
    size_t size;
} mt_sd_result_t;

typedef int (*mt_sd_list_fn)(void *context, const char *name, size_t size,
                             bool is_directory);

/*
 * One cooperative, non-reentrant FatFs owner for the application. Calls mount
 * lazily and close every FIL/DIR before returning. This API never creates,
 * formats, deletes, or renames SD-card content.
 */
mt_sd_result_t mt_sd_storage_mount(void);
mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity);
mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *dst,
                                             size_t expected_size);
mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context);
mt_sd_result_t mt_sd_storage_unmount(void);

const char *mt_sd_status_name(mt_sd_status_t status);

#endif
