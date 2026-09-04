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
    MT_SD_WRITE_FAILED,
    MT_SD_SYNC_FAILED,
    MT_SD_CLOSE_FAILED,
    MT_SD_UNLINK_FAILED,
    MT_SD_JOURNAL_INVALID,
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
 * lazily and close every FIL/DIR before returning. General reads and listings
 * never mutate the card. Journaled replacement keeps a complete, checksummed
 * write-ahead copy until the ordinary target file has been synced, so an
 * interrupted commit can replay the new complete file on the next mount.
 */
mt_sd_result_t mt_sd_storage_mount(void);
mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity);
mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *dst,
                                             size_t expected_size);
/* A missing file is a successful empty result with *found=false. Other open,
 * size, read, and close failures remain errors. */
mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found);
mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context);
mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity);
mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size);
mt_sd_result_t mt_sd_storage_unmount(void);

const char *mt_sd_status_name(mt_sd_status_t status);

#endif
