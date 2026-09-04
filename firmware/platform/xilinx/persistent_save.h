/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_PERSISTENT_SAVE_H
#define MULTITINI_XILINX_PERSISTENT_SAVE_H

#include "sd_storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_PERSISTENT_SAVE_PATH_CAPACITY 384U

typedef enum {
    MT_PERSISTENT_SAVE_OK = 0,
    MT_PERSISTENT_SAVE_INVALID_ARGUMENT,
    MT_PERSISTENT_SAVE_RECOVERY_FAILED,
    MT_PERSISTENT_SAVE_READ_FAILED,
    MT_PERSISTENT_SAVE_COMMIT_FAILED
} mt_persistent_save_status_t;

typedef struct {
    char path[MT_PERSISTENT_SAVE_PATH_CAPACITY];
    char journal_path[MT_PERSISTENT_SAVE_PATH_CAPACITY];
    size_t size;
    bool open;
    bool existed;
} mt_persistent_save_t;

typedef struct {
    mt_persistent_save_status_t status;
    mt_sd_result_t sd;
} mt_persistent_save_result_t;

/* Save files live beside user content as NAME.sav. The media browser filters
 * them out, while this layout needs no pre-created per-core save directory. */
mt_persistent_save_result_t mt_persistent_save_open(
    mt_persistent_save_t *save, const char *content_root,
    const char *content_name, uint8_t *data, size_t size,
    uint8_t erased_value);
mt_persistent_save_result_t mt_persistent_save_commit(
    mt_persistent_save_t *save, const uint8_t *data, size_t size);
void mt_persistent_save_close(mt_persistent_save_t *save);
const char *mt_persistent_save_status_name(mt_persistent_save_status_t status);

#endif
