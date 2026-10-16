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
    MT_SD_UNMOUNT_FAILED,
    MT_SD_SEEK_FAILED,
    MT_SD_RENAME_FAILED,
    MT_SD_FILE_BUSY,
    MT_SD_FILE_LIMIT,
    MT_SD_FILE_EXISTS,
    MT_SD_CREATE_DIR_FAILED,
    MT_SD_EXTERNAL_ACCESS
} mt_sd_status_t;

typedef struct {
    mt_sd_status_t status;
    unsigned backend_status;
    size_t size;
} mt_sd_result_t;

/* Optional diagnostic clock, in monotonic microseconds. No UART or allocation.
 * These counters cover managed-file reads only; seek measures the actual
 * FatFs f_lseek performed by mt_sd_file_read, not the logical seek API. */
typedef uint64_t (*mt_sd_profile_clock_fn)(void *context);
typedef struct {
    uint32_t count, failures;
    uint64_t total_us, max_us;
} mt_sd_profile_stage_t;
typedef struct {
    bool enabled;
    uint64_t bytes;
    mt_sd_profile_stage_t seek, read;
} mt_sd_read_profile_t;
void mt_sd_read_profile_start(mt_sd_profile_clock_fn clock_us, void *context);
void mt_sd_read_profile_stop(void);
void mt_sd_read_profile_reset(void);
void mt_sd_read_profile_snapshot(mt_sd_read_profile_t *snapshot);

typedef int (*mt_sd_list_fn)(void *context, const char *name, size_t size,
                             bool is_directory);

/*
 * One cooperative, non-reentrant FatFs owner for the application. Calls mount
 * lazily. Whole-file operations close each FIL/DIR before returning; the
 * managed file API below retains explicitly owned handles. General reads and listings
 * never mutate the card. Journaled replacement keeps a complete, checksummed
 * write-ahead copy until the ordinary target file has been synced, so an
 * interrupted commit can replay the new complete file on the next mount.
 */
mt_sd_result_t mt_sd_storage_mount(void);
mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity);
/* Read at most capacity bytes, setting truncated when the file is larger.
 * Uses a temporary read-only file and closes it before returning. Successful
 * reads call progress after each chunk of at most 4 KiB. The callback must
 * not reenter storage or initiate core/menu actions. */
mt_sd_result_t mt_sd_storage_read_file_prefix(const char *path, uint8_t *dst,
                                              size_t capacity, bool *truncated,
                                              void (*progress)(void *),
                                              void *context);
mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *dst,
                                             size_t expected_size);
/* A missing file is a successful empty result with *found=false. Other open,
 * size, read, and close failures remain errors. */
mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found);
mt_sd_result_t mt_sd_storage_read_file_optional_max(
    const char *path, uint8_t *dst, size_t capacity, bool *found);
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
/* USB export owns the card exclusively. Acquire refuses live files/callbacks
 * and unregisters FatFs. Release follows USB DMA stop and successful CTRL_SYNC;
 * failed remount retains the lease and blocks all local filesystem access. */
mt_sd_result_t mt_sd_storage_external_acquire(void);
mt_sd_result_t mt_sd_storage_external_release(void);
bool mt_sd_storage_external_active(void);
/* Ensure a directory through the existing mount/owner; an existing ordinary
 * file at the path is an error. */
mt_sd_result_t mt_sd_storage_ensure_directory(const char *path);

/* Managed synchronous files share the same FatFs owner. A maximum of 16 may
 * stay open. Unmount refuses open files; callers close them before switching
 * cores. Duplicate access involving a writer, and rename/remove of open files,
 * are rejected. Text and binary modes both preserve bytes on disk.
 * r/w/a with optional b and + follow C stdio creation/access semantics.
 * x (optionally b) creates a new file and never truncates an existing file.
 * Writes sync before returning; they are not journaled atomic replacements. */
typedef struct mt_sd_file mt_sd_file_t;
mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **file);
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *file, void *data, size_t capacity);
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *file, const void *data, size_t size);
/* Streaming temporary files may defer synchronization until close. Each
 * write still reports short writes/errors; a successful close is required
 * before renaming the temporary file to its final name. */
mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *file, const void *data, size_t size);
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *file, int64_t offset, int origin);
int64_t mt_sd_file_tell(const mt_sd_file_t *file);
int mt_sd_file_eof(const mt_sd_file_t *file); /* 0/1; -1 invalid handle */
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file);
mt_sd_result_t mt_sd_file_remove(const char *path);
mt_sd_result_t mt_sd_file_rename(const char *old_path, const char *new_path);

const char *mt_sd_status_name(mt_sd_status_t status);

#endif
