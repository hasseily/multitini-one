/* SPDX-License-Identifier: GPL-3.0-only */
#include "sd_storage.h"
#include "supervisor_settings.h"

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
static FRESULT seek_status;
static FRESULT rename_status;
static FRESULT dir_open_status;
static FRESULT dir_read_status;
static FRESULT dir_close_status;
static unsigned mount_calls;
static unsigned unmount_calls;
static unsigned open_calls;
static unsigned read_calls;
static unsigned read_failure_call;
static UINT largest_read_request;
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
static uint8_t target_data[MT_SETTINGS_WIRE_SIZE];
static size_t target_size;
static uint8_t journal_data[MT_SETTINGS_WIRE_SIZE + 40U];
static size_t journal_size;
static unsigned dir_entry_count;
static const char *dir_names[3];
static FSIZE_t dir_sizes[3];
static BYTE dir_attributes[3];

#define JOURNAL_TARGET "0:/multitini/cores/c64/media/writeback.d64"
#define JOURNAL_PATH "0:/multitini/cores/c64/media/writeback.d64.mtj"

static unsigned path_kind(const char *path)
{
    if (strcmp(path, JOURNAL_TARGET) == 0 || strcmp(path, MT_SETTINGS_PATH) == 0) return 1U;
    if (strcmp(path, JOURNAL_PATH) == 0 || strcmp(path, MT_SETTINGS_JOURNAL_PATH) == 0) return 2U;
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
    if ((mode & FA_CREATE_ALWAYS) != 0U) {
        assert(file->kind != 0U);
        if (file->kind == 1U) {
            target_exists = true;
            target_size = 0U;
        } else {
            journal_exists = true;
            journal_size = 0U;
        }
    } else {
        if (file->kind == 1U && !target_exists) {
            if ((mode & FA_OPEN_ALWAYS) == 0U) return FR_NO_FILE;
            target_exists = true; target_size = 0U;
        }
        if (file->kind == 2U && !journal_exists) {
            if ((mode & FA_OPEN_ALWAYS) == 0U) return FR_NO_FILE;
            journal_exists = true; journal_size = 0U;
        }
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
    if (bytes_to_read > largest_read_request) largest_read_request = bytes_to_read;
    if (read_status != FR_OK || read_calls == read_failure_call) {
        *bytes_read = 0U;
        return read_status == FR_OK ? FR_DISK_ERR : read_status;
    }
    if (short_read && delivered != 0U) --delivered;
    if ((FSIZE_t)delivered > f_size(file) - file->placeholder)
        delivered = (UINT)(f_size(file) - file->placeholder);
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
        assert(file->placeholder + delivered <= sizeof(target_data));
        memcpy(target_data + file->placeholder, buffer, delivered);
        if (file->placeholder + delivered > target_size) target_size = file->placeholder + delivered;
    } else {
        assert(file->kind == 2U);
        assert(file->placeholder + delivered <= sizeof(journal_data));
        memcpy(journal_data + file->placeholder, buffer, delivered);
        if (file->placeholder + delivered > journal_size) journal_size = file->placeholder + delivered;
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
    if (path_kind(path) == 1U) {
        if (!target_exists) return FR_NO_FILE;
        target_exists = false; target_size = 0U; return FR_OK;
    }
    assert(path_kind(path) == 2U);
    if (!journal_exists) return FR_NO_FILE;
    journal_exists = false;
    journal_size = 0U;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset)
{
    if (seek_status != FR_OK) return seek_status;
    assert(offset <= f_size(file));
    file->placeholder = (unsigned)offset;
    return FR_OK;
}

FRESULT f_rename(const char *old_path, const char *new_path)
{
    if (rename_status != FR_OK) return rename_status;
    assert(strcmp(old_path, JOURNAL_TARGET) == 0 && strcmp(new_path, JOURNAL_PATH) == 0);
    if (!target_exists) return FR_NO_FILE;
    if (journal_exists) return FR_EXIST;
    memcpy(journal_data, target_data, target_size); journal_size = target_size;
    target_exists = false; target_size = 0U; journal_exists = true;
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
    seek_status = FR_OK;
    rename_status = FR_OK;
    dir_open_status = FR_OK;
    dir_read_status = FR_OK;
    dir_close_status = FR_OK;
    mount_calls = 0U;
    unmount_calls = 0U;
    open_calls = 0U;
    read_calls = 0U;
    read_failure_call = 0U;
    largest_read_request = 0U;
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

static void prefix_progress(void *context)
{
    unsigned *const calls = context;
    assert(calls != NULL && read_calls == *calls + 1U && close_calls == 0U);
    assert(largest_read_request <= 4096U);
    ++*calls;
}

static void test_prefix_reads(void)
{
    uint8_t data[10000], destination[10000];
    bool truncated = true;
    unsigned progress_calls = 0U;
    size_t index;
    mt_sd_result_t value;
    for (index = 0U; index < sizeof(data); ++index) data[index] = (uint8_t)index;

    reset_fake();
    fake_file_size = sizeof(data); fake_file_data = data;
    memset(destination, 0xA5, sizeof(destination));
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, 5000U,
                                            &truncated, prefix_progress, &progress_calls);
    assert(value.status == MT_SD_OK && value.size == 5000U && truncated);
    assert(read_calls == 2U && progress_calls == 2U && close_calls == 1U);
    assert(largest_read_request == 4096U && memcmp(destination, data, 5000U) == 0);
    assert(destination[5000] == 0xA5U);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);

    reset_fake(); progress_calls = 0U;
    fake_file_size = sizeof(data); fake_file_data = data;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, prefix_progress, &progress_calls);
    assert(value.status == MT_SD_OK && value.size == sizeof(data) && !truncated);
    assert(read_calls == 3U && progress_calls == 3U && close_calls == 1U);
    assert(memcmp(destination, data, sizeof(data)) == 0);

    reset_fake(); progress_calls = 0U;
    value = mt_sd_storage_read_file_prefix("empty.txt", destination, sizeof(destination),
                                            &truncated, prefix_progress, &progress_calls);
    assert(value.status == MT_SD_OK && value.size == 0U && !truncated);
    assert(read_calls == 0U && progress_calls == 0U && close_calls == 1U);

    reset_fake();
    fake_file_size = sizeof(data); fake_file_data = data;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, 0U,
                                            &truncated, NULL, NULL);
    assert(value.status == MT_SD_OK && value.size == 0U && truncated);
    assert(read_calls == 0U && close_calls == 1U);

    reset_fake();
    fake_file_size = UINT64_MAX; fake_file_data = data;
    value = mt_sd_storage_read_file_prefix("large.txt", destination, 8U,
                                            &truncated, NULL, NULL);
    assert(value.status == MT_SD_OK && value.size == 8U && truncated);
    assert(read_calls == 1U && close_calls == 1U && memcmp(destination, data, 8U) == 0);

    reset_fake(); progress_calls = 0U;
    fake_file_size = sizeof(data); fake_file_data = data;
    read_failure_call = 2U; close_status = FR_INVALID_PARAMETER;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, prefix_progress, &progress_calls);
    assert(value.status == MT_SD_READ_FAILED && value.backend_status == FR_DISK_ERR && value.size == 4096U);
    assert(read_calls == 2U && progress_calls == 1U && close_calls == 1U);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);

    reset_fake(); progress_calls = 0U;
    fake_file_size = sizeof(data); fake_file_data = data; short_read = true;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, prefix_progress, &progress_calls);
    assert(value.status == MT_SD_READ_FAILED && value.size == 4095U);
    assert(read_calls == 1U && progress_calls == 0U && close_calls == 1U);

    reset_fake();
    fake_file_size = 4U; fake_file_data = data; close_status = FR_DISK_ERR;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, NULL, NULL);
    assert(value.status == MT_SD_CLOSE_FAILED && value.size == 4U && close_calls == 1U);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);

    reset_fake(); mount_status = FR_DISK_ERR;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, NULL, NULL);
    assert(value.status == MT_SD_MOUNT_FAILED && !truncated && open_calls == 0U && close_calls == 0U);
    reset_fake(); open_status = FR_NO_FILE;
    value = mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination),
                                            &truncated, NULL, NULL);
    assert(value.status == MT_SD_OPEN_FAILED && !truncated && read_calls == 0U && close_calls == 0U);
    reset_fake();
    assert(mt_sd_storage_read_file_prefix(NULL, destination, sizeof(destination), &truncated, NULL, NULL).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_storage_read_file_prefix("prefix.txt", NULL, 0U, &truncated, NULL, NULL).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_storage_read_file_prefix("prefix.txt", destination, sizeof(destination), NULL, NULL, NULL).status ==
           MT_SD_INVALID_ARGUMENT);
    assert(mount_calls == 0U && open_calls == 0U);
}

static void test_managed_files(void)
{
    mt_sd_file_t *file, *other, *files[16];
    mt_sd_result_t value;
    uint8_t data[16];
    const char *modes[] = {"r", "rb", "r+", "rb+", "r+b", "w", "wb", "w+", "wb+", "w+b",
        "a", "ab", "a+", "ab+", "a+b"};
    unsigned index;
    reset_fake();
    assert(mt_sd_file_open(JOURNAL_TARGET, "rb", &file).status == MT_SD_OK);
    assert(mt_sd_file_tell(file) == 0 && mt_sd_file_eof(file) == 0);
    assert(mt_sd_file_write(file, "x", 1U).status == MT_SD_INVALID_ARGUMENT);
    value = mt_sd_file_read(file, data, 1U);
    assert(value.status == MT_SD_OK && value.size == 1U && data[0] == 0xa5U);
    assert(mt_sd_file_tell(file) == 1 && mt_sd_file_eof(file) == 0);
    assert(mt_sd_file_read(file, data, 1U).size == 0U && mt_sd_file_eof(file) == 1);
    assert(mt_sd_file_seek(file, 0, 0).status == MT_SD_OK && mt_sd_file_eof(file) == 0);
    assert(mt_sd_file_seek(file, -1, 0).status == MT_SD_SEEK_FAILED);
    assert(mt_sd_file_seek(file, INT64_MIN, 1).status == MT_SD_SEEK_FAILED);
    assert(mt_sd_file_seek(file, 0, 3).status == MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_file_open(JOURNAL_TARGET, "w", &other).status == MT_SD_FILE_BUSY);
    assert(mt_sd_file_remove(JOURNAL_TARGET).status == MT_SD_FILE_BUSY);
    assert(mt_sd_file_rename(JOURNAL_TARGET, JOURNAL_PATH).status == MT_SD_FILE_BUSY);
    assert(mt_sd_storage_unmount().status == MT_SD_FILE_BUSY);
    assert(mt_sd_file_close(file).status == MT_SD_OK);
    assert(mt_sd_file_eof(file) == -1 && mt_sd_file_tell(file) == -1);
    assert(mt_sd_file_close(file).status == MT_SD_INVALID_ARGUMENT);
    for (index = 0U; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        assert(mt_sd_file_open(JOURNAL_TARGET, modes[index], &file).status == MT_SD_OK);
        assert(mt_sd_file_close(file).status == MT_SD_OK);
    }
    assert(mt_sd_file_open(JOURNAL_TARGET, "rbb", &file).status == MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_file_open(JOURNAL_TARGET, "r++", &file).status == MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_file_open(JOURNAL_TARGET, "x", &file).status == MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_file_open(JOURNAL_TARGET, "", &file).status == MT_SD_INVALID_ARGUMENT);
    assert(mt_sd_file_open(JOURNAL_TARGET, "wb+", &file).status == MT_SD_OK && target_size == 0U);
    value = mt_sd_file_write(file, "abc", 3U);
    assert(value.status == MT_SD_OK && value.size == 3U && target_size == 3U);
    assert(mt_sd_file_seek(file, -2, 1).status == MT_SD_OK && mt_sd_file_tell(file) == 1);
    assert(mt_sd_file_write(file, "Z", 1U).status == MT_SD_OK);
    assert(memcmp(target_data, "aZc", 3U) == 0);
    assert(mt_sd_file_seek(file, 2, 2).status == MT_SD_OK && mt_sd_file_tell(file) == 5);
    assert(target_size == 3U); /* Seek cannot extend a file. */
    assert(mt_sd_file_write(file, "!", 1U).status == MT_SD_OK);
    assert(target_size == 6U && target_data[3] == 0 && target_data[4] == 0 && target_data[5] == '!');
    assert(mt_sd_file_seek(file, 0, 0).status == MT_SD_OK);
    value = mt_sd_file_read(file, data, sizeof(data));
    assert(value.status == MT_SD_OK && value.size == 6U && mt_sd_file_eof(file) == 1);
    assert(mt_sd_file_close(file).status == MT_SD_OK);
    assert(mt_sd_file_open(JOURNAL_TARGET, "a+", &file).status == MT_SD_OK && mt_sd_file_tell(file) == 0);
    assert(mt_sd_file_write(file, "?", 1U).status == MT_SD_OK && mt_sd_file_tell(file) == 7);
    assert(target_data[6] == '?' && mt_sd_file_seek(file, 0, 0).status == MT_SD_OK);
    assert(mt_sd_file_read(file, data, 1U).size == 1U && data[0] == 'a');
    assert(mt_sd_file_write(file, "$", 1U).status == MT_SD_OK && target_data[7] == '$');
    assert(mt_sd_file_close(file).status == MT_SD_OK);
    assert(mt_sd_file_rename(JOURNAL_TARGET, JOURNAL_PATH).status == MT_SD_OK && !target_exists && journal_exists);
    assert(mt_sd_file_remove(JOURNAL_PATH).status == MT_SD_OK && !journal_exists);
    assert(mt_sd_file_open(JOURNAL_TARGET, "r+", &file).status == MT_SD_OPEN_FAILED);
    assert(mt_sd_file_open(JOURNAL_TARGET, "ab", &file).status == MT_SD_OK && target_exists);
    short_write = true;
    assert(mt_sd_file_write(file, "abc", 3U).status == MT_SD_WRITE_FAILED);
    short_write = false; sync_status = FR_DISK_ERR;
    assert(mt_sd_file_write(file, "q", 1U).status == MT_SD_SYNC_FAILED);
    sync_status = FR_OK; close_status = FR_DISK_ERR;
    assert(mt_sd_file_close(file).status == MT_SD_CLOSE_FAILED);
    close_status = FR_OK;
    assert(mt_sd_file_close(file).status == MT_SD_OK);
    assert(mt_sd_file_open(JOURNAL_TARGET, "rb", &file).status == MT_SD_OK);
    seek_status = FR_DISK_ERR;
    assert(mt_sd_file_read(file, data, 1U).status == MT_SD_SEEK_FAILED);
    seek_status = FR_OK; read_status = FR_DISK_ERR;
    assert(mt_sd_file_read(file, data, 1U).status == MT_SD_READ_FAILED);
    read_status = FR_OK;
    assert(mt_sd_file_close(file).status == MT_SD_OK);
    rename_status = FR_DISK_ERR;
    assert(mt_sd_file_rename(JOURNAL_TARGET, JOURNAL_PATH).status == MT_SD_RENAME_FAILED);
    rename_status = FR_OK; unlink_status = FR_DISK_ERR;
    assert(mt_sd_file_remove(JOURNAL_TARGET).status == MT_SD_UNLINK_FAILED);
    unlink_status = FR_OK;
    for (index = 0U; index < 16U; ++index)
        assert(mt_sd_file_open(JOURNAL_TARGET, "rb", &files[index]).status == MT_SD_OK);
    assert(mt_sd_file_open(JOURNAL_TARGET, "rb", &file).status == MT_SD_FILE_LIMIT);
    for (index = 0U; index < 16U; ++index) assert(mt_sd_file_close(files[index]).status == MT_SD_OK);
    assert(mt_sd_storage_unmount().status == MT_SD_OK);
}

static void settings_store_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t settings_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void test_settings_journal_migration(void)
{
    mt_supervisor_settings_t original;
    mt_supervisor_settings_t expected;
    mt_supervisor_settings_t loaded;
    uint8_t current_wire[MT_SETTINGS_WIRE_SIZE];
    uint8_t wire[MT_SETTINGS_WIRE_SIZE];
    uint32_t schema;
    size_t index;

    reset_fake();
    mt_supervisor_settings_defaults(&original);
    assert(mt_supervisor_settings_remember_core(&original, "c64", "pal"));
    assert(mt_supervisor_settings_remember_c64_input(&original, "c64", false, 1U));
    assert(mt_supervisor_settings_remember_c64_fast_roms(&original, "c64", true));
    assert(mt_supervisor_settings_remember_c64_cpu_turbo(&original, "c64", true));
    assert(mt_supervisor_settings_remember_drive8(&original, "c64", "Writable Demo.d64"));
    assert(mt_supervisor_settings_remember_cartridge(&original, "c64", "Fast Load.crt"));
    assert(mt_supervisor_settings_remember_borders(&original, "c64", true, 4U, 15U, 20U));
    assert(mt_supervisor_settings_remember_core(&original, "gbc", "color"));
    assert(mt_supervisor_settings_remember_cartridge(&original, "gbc", "Yars' Revenge.gbc"));
    assert(mt_supervisor_settings_remember_core(&original, "vectrex", "native"));
    assert(mt_supervisor_settings_remember_cartridge(&original, "vectrex",
        "Classics/Arcade Games/Star Castle.vec"));
    assert(mt_supervisor_settings_remember_borders(&original, "vectrex", true, 2U, 9U, 3U));
    assert(mt_supervisor_settings_remember_vectrex_video(&original, "vectrex", false, 3U, 0U));
    original.auto_boot = true;
    assert(mt_supervisor_settings_save(&original).status == MT_SETTINGS_OK);
    assert(target_size == MT_SETTINGS_WIRE_SIZE && !journal_exists);
    memcpy(current_wire, target_data, sizeof(current_wire));
    for (schema = 1U; schema <= 7U; ++schema) {
        const size_t wire_size = schema == 7U ? MT_SETTINGS_WIRE_SIZE : schema == 6U ? 5038U : schema == 5U ? 4696U :
            (schema == 4U ? 4088U : (schema == 3U ? 4024U : 3960U));
        unsigned fault;
        memcpy(wire, current_wire, sizeof(wire));
        expected = original;
        if (schema != 7U) {
            uint32_t crc;
            const size_t profile_size = schema >= 5U ? 290U : schema == 4U ? 252U : schema == 3U ? 248U : 244U;
            for (index = 0U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
                memcpy(wire + 56U + index * profile_size,
                       current_wire + 56U + index * 290U, profile_size);
            }
            settings_store_u32(wire + 8U, schema);
            settings_store_u32(wire + 12U, (uint32_t)wire_size);
            crc = settings_crc32(wire + 24U, wire_size - 24U);
            settings_store_u32(wire + 16U, crc);
            settings_store_u32(wire + 20U, ~crc);
            for (index = 0U; index < expected.profile_count; ++index) {
                if (schema < 3U) {
                    expected.profiles[index].border_enabled = false;
                    expected.profiles[index].border_style = 0U;
                    expected.profiles[index].border_color = 7U;
                    expected.profiles[index].border_padding = 6U;
                }
                if (schema < 4U) {
                    expected.profiles[index].vectrex_overlay_enabled = true;
                    expected.profiles[index].vectrex_beam_gain = 1U;
                    expected.profiles[index].vectrex_overlay_opacity = 1U;
                }
                if (schema < 5U) mt_video_effects_defaults(&expected.profiles[index].video_effects);
            }
            expected.profiles[0].c64_fast_roms = schema != 1U;
        }
        /* Real storage optional/exact readers must accept each on-card length
         * without writing anything when there is no outstanding journal. */
        reset_fake();
        memcpy(target_data, wire, wire_size);
        target_size = wire_size;
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        assert(write_calls == 0U && sync_calls == 0U && unlink_calls == 0U);
        for (fault = 0U; fault < 3U; ++fault) {
            mt_sd_result_t replacement;
            reset_fake();
            if (fault == 0U) write_failure_call = 4U; /* Target write interrupted. */
            if (fault == 1U) sync_failure_call = 2U; /* Target sync failed. */
            if (fault == 2U) unlink_status = FR_DISK_ERR; /* Journal still present. */
            replacement = mt_sd_storage_journal_replace(
                MT_SETTINGS_PATH, MT_SETTINGS_JOURNAL_PATH, wire, wire_size);
            assert(replacement.status != MT_SD_OK && journal_exists);
            write_failure_call = 0U;
            sync_failure_call = 0U;
            unlink_status = FR_OK;
            assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
            assert(!journal_exists && target_size == wire_size);
            assert(memcmp(target_data, wire, wire_size) == 0);
            assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
            /* Saving a migrated file always writes the complete new format. */
            assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_OK);
            assert(target_size == MT_SETTINGS_WIRE_SIZE && target_data[8U] == 7U);
            assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
            assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        }
        reset_fake();
        write_failure_call = 4U;
        assert(mt_sd_storage_journal_replace(MT_SETTINGS_PATH, MT_SETTINGS_JOURNAL_PATH,
            wire, wire_size).status == MT_SD_WRITE_FAILED);
        write_failure_call = 0U;
        journal_data[24U + 100U] ^= 1U;
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_RECOVERY_FAILED);
        assert(loaded.profile_count == 0U && !loaded.auto_boot);
        assert(journal_exists && target_size == 0U);
    }
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

    open_status = FR_NO_FILE;
    {
        bool found = true;
        result = mt_sd_storage_read_file_optional_exact(
            "0:/multitini/cores/gbc/media/demo.gbc.sav",
            destination, sizeof(destination), &found);
        assert(result.status == MT_SD_OK && !found && result.size == 0U);
    }
    open_status = FR_OK;
    {
        bool found = false;
        fake_file_data = data;
        fake_file_size = sizeof(data);
        result = mt_sd_storage_read_file_optional_exact(
            "0:/multitini/cores/gbc/media/demo.gbc.sav",
            destination, sizeof(destination), &found);
        assert(result.status == MT_SD_OK && found);
        assert(memcmp(destination, data, sizeof(data)) == 0);
    }

    {
        bool found = true;
        open_status = FR_NO_FILE;
        result = mt_sd_storage_read_file_optional_max("0:/art.ovr", destination,
            sizeof(destination), &found);
        assert(result.status == MT_SD_OK && !found && result.size == 0U);
        open_status = FR_DISK_ERR;
        result = mt_sd_storage_read_file_optional_max("0:/art.ovr", destination,
            sizeof(destination), &found);
        assert(result.status == MT_SD_OPEN_FAILED && !found);
        open_status = FR_OK;
        fake_file_size = sizeof(data) - 2U;
        result = mt_sd_storage_read_file_optional_max("0:/art.ovr", destination,
            sizeof(destination), &found);
        assert(result.status == MT_SD_OK && found && result.size == sizeof(data) - 2U);
        assert(memcmp(destination, data, result.size) == 0);
        fake_file_size = sizeof(data) + 1U;
        result = mt_sd_storage_read_file_optional_max("0:/art.ovr", destination,
            sizeof(destination), &found);
        assert(result.status == MT_SD_TOO_LARGE && !found);
        assert(mt_sd_storage_read_file_optional_max("0:/art.ovr", destination,
            sizeof(destination), NULL).status == MT_SD_INVALID_ARGUMENT);
        fake_file_size = sizeof(data);
    }

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
    test_prefix_reads();
    test_managed_files();
    test_settings_journal_migration();
    return 0;
}
