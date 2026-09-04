/* SPDX-License-Identifier: GPL-3.0-only */
#include "persistent_save.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static mt_sd_status_t recover_status;
static mt_sd_status_t read_status;
static mt_sd_status_t commit_status;
static bool saved_file_exists;
static uint8_t saved_file[512];
static size_t saved_file_size;
static char recovered_target[384];
static char recovered_journal[384];
static char committed_target[384];
static char committed_journal[384];

static mt_sd_result_t result(mt_sd_status_t status, size_t size)
{
    mt_sd_result_t value = {status, status == MT_SD_OK ? 0U : 9U, size};
    return value;
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    (void)recovery_buffer;
    (void)capacity;
    strcpy(recovered_target, path);
    strcpy(recovered_journal, journal_path);
    return result(recover_status, 0U);
}

mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found)
{
    assert(strcmp(path, recovered_target) == 0);
    if (read_status != MT_SD_OK) return result(read_status, 0U);
    *found = saved_file_exists;
    if (!saved_file_exists) return result(MT_SD_OK, 0U);
    assert(expected_size == saved_file_size);
    memcpy(dst, saved_file, saved_file_size);
    return result(MT_SD_OK, saved_file_size);
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size)
{
    strcpy(committed_target, path);
    strcpy(committed_journal, journal_path);
    if (commit_status != MT_SD_OK) return result(commit_status, 0U);
    assert(size <= sizeof(saved_file));
    memcpy(saved_file, src, size);
    saved_file_size = size;
    saved_file_exists = true;
    return result(MT_SD_OK, size);
}

static void reset_fake(void)
{
    recover_status = MT_SD_OK;
    read_status = MT_SD_OK;
    commit_status = MT_SD_OK;
    saved_file_exists = false;
    saved_file_size = 0U;
    recovered_target[0] = '\0';
    recovered_journal[0] = '\0';
    committed_target[0] = '\0';
    committed_journal[0] = '\0';
}

int main(void)
{
    static const char root[] = "0:/multitini/cores/gbc/media";
    static const char target[] =
        "0:/multitini/cores/gbc/media/demo.gbc.sav";
    static const char journal[] =
        "0:/multitini/cores/gbc/media/demo.gbc.sav.mtj";
    mt_persistent_save_t save;
    mt_persistent_save_result_t open_result;
    uint8_t data[512];
    uint8_t update[512];
    size_t index;

    reset_fake();
    memset(data, 0U, sizeof(data));
    open_result = mt_persistent_save_open(
        &save, root, "demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_OK);
    assert(save.open && !save.existed && save.size == sizeof(data));
    assert(strcmp(save.path, target) == 0);
    assert(strcmp(save.journal_path, journal) == 0);
    assert(strcmp(recovered_target, target) == 0);
    assert(strcmp(recovered_journal, journal) == 0);
    for (index = 0U; index < sizeof(data); ++index)
        assert(data[index] == 0xffU);

    for (index = 0U; index < sizeof(update); ++index)
        update[index] = (uint8_t)(index ^ 0x5aU);
    open_result = mt_persistent_save_commit(&save, update, sizeof(update));
    assert(open_result.status == MT_PERSISTENT_SAVE_OK);
    assert(save.existed);
    assert(strcmp(committed_target, target) == 0);
    assert(strcmp(committed_journal, journal) == 0);
    assert(memcmp(saved_file, update, sizeof(update)) == 0);

    memset(data, 0U, sizeof(data));
    open_result = mt_persistent_save_open(
        &save, root, "demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_OK);
    assert(save.existed);
    assert(memcmp(data, update, sizeof(data)) == 0);

    open_result = mt_persistent_save_commit(&save, update,
                                            sizeof(update) - 1U);
    assert(open_result.status == MT_PERSISTENT_SAVE_INVALID_ARGUMENT);
    commit_status = MT_SD_WRITE_FAILED;
    open_result = mt_persistent_save_commit(&save, update, sizeof(update));
    assert(open_result.status == MT_PERSISTENT_SAVE_COMMIT_FAILED);
    assert(open_result.sd.backend_status == 9U);

    reset_fake();
    recover_status = MT_SD_JOURNAL_INVALID;
    open_result = mt_persistent_save_open(
        &save, root, "demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_RECOVERY_FAILED);
    reset_fake();
    read_status = MT_SD_SIZE_MISMATCH;
    open_result = mt_persistent_save_open(
        &save, root, "demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_READ_FAILED);

    open_result = mt_persistent_save_open(
        &save, "relative", "demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_INVALID_ARGUMENT);
    open_result = mt_persistent_save_open(
        &save, root, "../demo.gbc", data, sizeof(data), 0xffU);
    assert(open_result.status == MT_PERSISTENT_SAVE_INVALID_ARGUMENT);
    mt_persistent_save_close(&save);
    assert(!save.open && save.path[0] == '\0');
    assert(strcmp(mt_persistent_save_status_name(
                      MT_PERSISTENT_SAVE_COMMIT_FAILED),
                  "commit failed") == 0);
    return 0;
}
