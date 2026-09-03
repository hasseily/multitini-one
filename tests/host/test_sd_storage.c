/* SPDX-License-Identifier: GPL-3.0-only */
#include "sd_storage.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ff.h"

static FRESULT mount_status;
static FRESULT unmount_status;
static FRESULT open_status;
static FRESULT read_status;
static FRESULT write_status;
static FRESULT sync_status;
static FRESULT close_status;
static FRESULT unlink_status;
static FRESULT dir_open_status;
static FRESULT dir_read_status;
static FRESULT dir_close_status;
static unsigned mount_calls;
static unsigned unmount_calls;
static unsigned open_calls;
static unsigned read_calls;
static unsigned write_calls;
static unsigned sync_calls;
static unsigned close_calls;
static unsigned unlink_calls;
static unsigned dir_close_calls;
static FSIZE_t fake_file_size;
static const uint8_t *fake_file_data;
static bool short_read;
static bool short_write;
static unsigned write_failure_call;
static unsigned sync_failure_call;
static unsigned close_failure_call;
static bool target_exists;
static bool journal_exists;
static uint8_t target_data[256];
static size_t target_size;
static uint8_t journal_data[512];
static size_t journal_size;
static unsigned dir_entry_count;
static const char *dir_names[3];
static FSIZE_t dir_sizes[3];
static BYTE dir_attributes[3];

#define JOURNAL_TARGET "0:/multitini/cores/c64/media/writeback.d64"
#define JOURNAL_PATH "0:/multitini/cores/c64/media/writeback.d64.mtj"

static unsigned path_kind(const char *path)
{
    if (strcmp(path, JOURNAL_TARGET) == 0) return 1U;
    if (strcmp(path, JOURNAL_PATH) == 0) return 2U;
    return 0U;
}

FRESULT f_mount(FATFS *file_system, const char *path, BYTE option)
{
    assert(strcmp(path, "0:/") == 0);
    assert(option == 1U);
    if (file_system == NULL) {
        ++unmount_calls;
        return unmount_status;
    }
    ++mount_calls;
    return mount_status;
}

FRESULT f_open(FIL *file, const char *path, BYTE mode)
{
    assert(file != NULL);
    assert(path != NULL);
    ++open_calls;
    file->placeholder = 0U;
    file->kind = path_kind(path);
    file->mode = mode;
    if (open_status != FR_OK) return open_status;
    if (mode == (FA_WRITE | FA_CREATE_ALWAYS)) {
        assert(file->kind != 0U);
        if (file->kind == 1U) {
            target_exists = true;
            target_size = 0U;
        } else {
            journal_exists = true;
            journal_size = 0U;
        }
    } else {
        assert(mode == FA_READ);
        if (file->kind == 1U && !target_exists) return FR_NO_FILE;
        if (file->kind == 2U && !journal_exists) return FR_NO_FILE;
    }
    return FR_OK;
}

FSIZE_t f_size(const FIL *file)
{
    assert(file != NULL);
    if (file->kind == 1U) return target_size;
    if (file->kind == 2U) return journal_size;
    return fake_file_size;
}

FRESULT f_read(FIL *file, void *buffer, UINT bytes_to_read,
               UINT *bytes_read)
{
    UINT delivered = bytes_to_read;
    assert(file != NULL);
    assert(buffer != NULL);
    assert(bytes_read != NULL);
    ++read_calls;
    if (read_status != FR_OK) {
        *bytes_read = 0U;
        return read_status;
    }
    if (short_read && delivered != 0U) --delivered;
    if (file->kind == 1U) {
        assert(file->placeholder + delivered <= target_size);
        memcpy(buffer, target_data + file->placeholder, delivered);
    } else if (file->kind == 2U) {
        assert(file->placeholder + delivered <= journal_size);
        memcpy(buffer, journal_data + file->placeholder, delivered);
    } else {
        memcpy(buffer, fake_file_data + file->placeholder, delivered);
    }
    file->placeholder += delivered;
    *bytes_read = delivered;
    return FR_OK;
}

FRESULT f_write(FIL *file, const void *buffer, UINT bytes_to_write,
                UINT *bytes_written)
{
    UINT delivered = bytes_to_write;
    assert(file != NULL);
    assert(buffer != NULL);
    assert(bytes_written != NULL);
    ++write_calls;
    if (write_status != FR_OK ||
        (write_failure_call != 0U && write_calls == write_failure_call)) {
        *bytes_written = 0U;
        return write_status == FR_OK ? FR_DISK_ERR : write_status;
    }
    if ((short_write || write_failure_call == write_calls) && delivered != 0U)
        --delivered;
    if (file->kind == 1U) {
        assert(target_size + delivered <= sizeof(target_data));
        memcpy(target_data + target_size, buffer, delivered);
        target_size += delivered;
    } else {
        assert(file->kind == 2U);
        assert(journal_size + delivered <= sizeof(journal_data));
        memcpy(journal_data + journal_size, buffer, delivered);
        journal_size += delivered;
    }
    file->placeholder += delivered;
    *bytes_written = delivered;
    return FR_OK;
}

FRESULT f_sync(FIL *file)
{
    assert(file != NULL);
    ++sync_calls;
    if (sync_failure_call != 0U && sync_calls == sync_failure_call)
        return FR_DISK_ERR;
    return sync_status;
}

FRESULT f_close(FIL *file)
{
    assert(file != NULL);
    ++close_calls;
    if (close_failure_call != 0U && close_calls == close_failure_call)
        return FR_DISK_ERR;
    return close_status;
}

FRESULT f_unlink(const char *path)
{
    assert(path != NULL);
    ++unlink_calls;
    if (unlink_status != FR_OK) return unlink_status;
    assert(strcmp(path, JOURNAL_PATH) == 0);
    if (!journal_exists) return FR_NO_FILE;
    journal_exists = false;
    journal_size = 0U;
    return FR_OK;
}

FRESULT f_opendir(DIR *directory, const char *path)
{
    assert(directory != NULL);
    assert(path != NULL);
    directory->placeholder = 0U;
    return dir_open_status;
}

FRESULT f_readdir(DIR *directory, FILINFO *info)
{
    const unsigned index = directory->placeholder++;
    assert(info != NULL);
    if (dir_read_status != FR_OK) return dir_read_status;
    if (index >= dir_entry_count) {
        info->fname[0] = '\0';
        return FR_OK;
    }
    assert(strlen(dir_names[index]) < sizeof(info->fname));
    strcpy(info->fname, dir_names[index]);
    info->fsize = dir_sizes[index];
    info->fattrib = dir_attributes[index];
    return FR_OK;
}

FRESULT f_closedir(DIR *directory)
{
    assert(directory != NULL);
    ++dir_close_calls;
    return dir_close_status;
}

static void reset_fake(void)
{
    unmount_status = FR_OK;
    (void)mt_sd_storage_unmount();
    mount_status = FR_OK;
    unmount_status = FR_OK;
    open_status = FR_OK;
    read_status = FR_OK;
    write_status = FR_OK;
    sync_status = FR_OK;
    close_status = FR_OK;
    unlink_status = FR_OK;
    dir_open_status = FR_OK;
    dir_read_status = FR_OK;
    dir_close_status = FR_OK;
    mount_calls = 0U;
    unmount_calls = 0U;
    open_calls = 0U;
    read_calls = 0U;
    write_calls = 0U;
    sync_calls = 0U;
    close_calls = 0U;
    unlink_calls = 0U;
    dir_close_calls = 0U;
    fake_file_size = 0U;
    fake_file_data = NULL;
    short_read = false;
    short_write = false;
    write_failure_call = 0U;
    sync_failure_call = 0U;
    close_failure_call = 0U;
    target_exists = true;
    journal_exists = false;
    target_data[0] = 0xA5U;
    target_size = 1U;
    journal_size = 0U;
    dir_entry_count = 0U;
}

typedef struct {
    unsigned count;
    size_t total_size;
    bool saw_directory;
    unsigned stop_after;
} list_context_t;

static int collect_entry(void *context, const char *name, size_t size,
                         bool is_directory)
{
    list_context_t *const list = context;
    assert(name != NULL);
    ++list->count;
    list->total_size += size;
    list->saw_directory = list->saw_directory || is_directory;
    return list->stop_after != 0U && list->count == list->stop_after;
}

int main(void)
{
    static const uint8_t data[] = {1U, 2U, 3U, 4U, 5U, 6U};
    uint8_t destination[sizeof(data)] = {0U};
    mt_sd_result_t result;
    list_context_t list = {0};

    reset_fake();
    result = mt_sd_storage_mount();
    assert(result.status == MT_SD_OK);
    assert(mt_sd_storage_mount().status == MT_SD_OK);
    assert(mount_calls == 1U);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);
    assert(unmount_calls == 1U);

    reset_fake();
    fake_file_data = data;
    fake_file_size = sizeof(data);
    result = mt_sd_storage_read_file_exact(
        "0:/multitini/cores/c64/roms/basic.bin", destination,
        sizeof(destination));
    assert(result.status == MT_SD_OK);
    assert(result.size == sizeof(data));
    assert(memcmp(destination, data, sizeof(data)) == 0);
    assert(mount_calls == 1U && open_calls == 1U && read_calls == 1U &&
           close_calls == 1U);

    fake_file_size = sizeof(data) - 1U;
    result = mt_sd_storage_read_file_exact(
        "0:/multitini/cores/c64/roms/basic.bin", destination,
        sizeof(destination));
    assert(result.status == MT_SD_SIZE_MISMATCH);
    assert(result.size == sizeof(data) - 1U);
    assert(read_calls == 1U && close_calls == 2U);

    fake_file_size = sizeof(data);
    result = mt_sd_storage_read_file_max(
        "0:/multitini/cores/c64/media/disk.d64", destination,
        sizeof(destination) - 1U);
    assert(result.status == MT_SD_TOO_LARGE);
    assert(result.size == sizeof(data));

    short_read = true;
    result = mt_sd_storage_read_file_max(
        "0:/multitini/cores/c64/media/disk.d64", destination,
        sizeof(destination));
    assert(result.status == MT_SD_READ_FAILED);
    assert(result.size == sizeof(data) - 1U);
    short_read = false;

    close_status = FR_DISK_ERR;
    result = mt_sd_storage_read_file_max(
        "0:/multitini/cores/c64/media/disk.d64", destination,
        sizeof(destination));
    assert(result.status == MT_SD_CLOSE_FAILED);
    assert(result.backend_status == (unsigned)FR_DISK_ERR);
    close_status = FR_OK;

    assert(mt_sd_storage_read_file_max(NULL, destination,
                                       sizeof(destination)).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_storage_read_file_exact("0:/x", NULL, 1U).status ==
           MT_SD_INVALID_ARGUMENT);

    dir_names[0] = "disk.d64";
    dir_names[1] = "subdir";
    dir_sizes[0] = 174848U;
    dir_sizes[1] = 0U;
    dir_attributes[0] = 0U;
    dir_attributes[1] = AM_DIR;
    dir_entry_count = 2U;
    result = mt_sd_storage_list("0:/multitini/cores/c64/media",
                                collect_entry, &list);
    assert(result.status == MT_SD_OK);
    assert(result.size == 2U);
    assert(list.count == 2U);
    assert(list.total_size == 174848U);
    assert(list.saw_directory);
    assert(dir_close_calls == 1U);

    memset(&list, 0, sizeof(list));
    list.stop_after = 1U;
    result = mt_sd_storage_list("0:/multitini/cores/c64/media",
                                collect_entry, &list);
    assert(result.status == MT_SD_OK);
    assert(result.size == 1U);
    assert(dir_close_calls == 2U);

    assert(strcmp(mt_sd_status_name(MT_SD_MOUNT_FAILED), "mount failed") ==
           0);

    reset_fake();
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_OK && result.size == sizeof(data));
    assert(target_exists && !journal_exists);
    assert(target_size == sizeof(data));
    assert(memcmp(target_data, data, sizeof(data)) == 0);
    assert(write_calls == 4U && sync_calls == 2U && unlink_calls == 1U);

    /* Once the complete journal is synced, any interrupted primary write can
     * be replayed without rename or shared-cluster ambiguity. */
    reset_fake();
    write_failure_call = 4U;
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_WRITE_FAILED);
    assert(journal_exists && target_exists && target_size == 0U);
    write_failure_call = 0U;
    result = mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_PATH,
                                           destination,
                                           sizeof(destination));
    assert(result.status == MT_SD_OK && result.size == sizeof(data));
    assert(target_exists && !journal_exists);
    assert(memcmp(target_data, data, sizeof(data)) == 0);

    reset_fake();
    sync_failure_call = 1U;
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_SYNC_FAILED);
    assert(journal_exists && target_size == 1U && target_data[0] == 0xA5U);

    reset_fake();
    sync_failure_call = 2U;
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_SYNC_FAILED && journal_exists);
    sync_failure_call = 0U;
    result = mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_PATH,
                                           destination,
                                           sizeof(destination));
    assert(result.status == MT_SD_OK && !journal_exists);
    assert(memcmp(target_data, data, sizeof(data)) == 0);

    reset_fake();
    write_failure_call = 4U;
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_WRITE_FAILED && journal_exists);
    journal_data[24U] ^= 0x80U;
    write_failure_call = 0U;
    result = mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_PATH,
                                           destination,
                                           sizeof(destination));
    assert(result.status == MT_SD_JOURNAL_INVALID);
    assert(journal_exists && target_size == 0U);

    reset_fake();
    unlink_status = FR_DISK_ERR;
    result = mt_sd_storage_journal_replace(JOURNAL_TARGET, JOURNAL_PATH,
                                           data, sizeof(data));
    assert(result.status == MT_SD_UNLINK_FAILED);
    assert(journal_exists && target_size == sizeof(data));
    unlink_status = FR_OK;
    result = mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_PATH,
                                           destination,
                                           sizeof(destination));
    assert(result.status == MT_SD_OK && !journal_exists);

    reset_fake();
    result = mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_PATH,
                                           destination,
                                           sizeof(destination));
    assert(result.status == MT_SD_OK && result.size == 0U);
    assert(target_size == 1U && target_data[0] == 0xA5U);

    assert(mt_sd_storage_journal_replace(JOURNAL_TARGET,
                                         "0:/MULTITINI/CORES/C64/MEDIA/WRITEBACK.D64",
                                         data, sizeof(data)).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_storage_journal_recover(JOURNAL_TARGET, JOURNAL_TARGET,
                                         destination,
                                         sizeof(destination)).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(strcmp(mt_sd_status_name(MT_SD_JOURNAL_INVALID),
                  "journal invalid") == 0);
    assert(strcmp(mt_sd_status_name((mt_sd_status_t)99), "unknown") == 0);
    return 0;
}
