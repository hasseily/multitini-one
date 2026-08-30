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
static FRESULT close_status;
static FRESULT dir_open_status;
static FRESULT dir_read_status;
static FRESULT dir_close_status;
static unsigned mount_calls;
static unsigned unmount_calls;
static unsigned open_calls;
static unsigned read_calls;
static unsigned close_calls;
static unsigned dir_close_calls;
static FSIZE_t fake_file_size;
static const uint8_t *fake_file_data;
static bool short_read;
static unsigned dir_entry_count;
static const char *dir_names[3];
static FSIZE_t dir_sizes[3];
static BYTE dir_attributes[3];

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
    assert(mode == FA_READ);
    ++open_calls;
    file->placeholder = 0U;
    return open_status;
}

FSIZE_t f_size(const FIL *file)
{
    assert(file != NULL);
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
    memcpy(buffer, fake_file_data + file->placeholder, delivered);
    file->placeholder += delivered;
    *bytes_read = delivered;
    return FR_OK;
}

FRESULT f_close(FIL *file)
{
    assert(file != NULL);
    ++close_calls;
    return close_status;
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
    close_status = FR_OK;
    dir_open_status = FR_OK;
    dir_read_status = FR_OK;
    dir_close_status = FR_OK;
    mount_calls = 0U;
    unmount_calls = 0U;
    open_calls = 0U;
    read_calls = 0U;
    close_calls = 0U;
    dir_close_calls = 0U;
    fake_file_size = 0U;
    fake_file_data = NULL;
    short_read = false;
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
        "0:/cores/c64/roms/basic.bin", destination, sizeof(destination));
    assert(result.status == MT_SD_OK);
    assert(result.size == sizeof(data));
    assert(memcmp(destination, data, sizeof(data)) == 0);
    assert(mount_calls == 1U && open_calls == 1U && read_calls == 1U &&
           close_calls == 1U);

    fake_file_size = sizeof(data) - 1U;
    result = mt_sd_storage_read_file_exact(
        "0:/cores/c64/roms/basic.bin", destination, sizeof(destination));
    assert(result.status == MT_SD_SIZE_MISMATCH);
    assert(result.size == sizeof(data) - 1U);
    assert(read_calls == 1U && close_calls == 2U);

    fake_file_size = sizeof(data);
    result = mt_sd_storage_read_file_max(
        "0:/cores/c64/media/disk.d64", destination,
        sizeof(destination) - 1U);
    assert(result.status == MT_SD_TOO_LARGE);
    assert(result.size == sizeof(data));

    short_read = true;
    result = mt_sd_storage_read_file_max(
        "0:/cores/c64/media/disk.d64", destination, sizeof(destination));
    assert(result.status == MT_SD_READ_FAILED);
    assert(result.size == sizeof(data) - 1U);
    short_read = false;

    close_status = FR_DISK_ERR;
    result = mt_sd_storage_read_file_max(
        "0:/cores/c64/media/disk.d64", destination, sizeof(destination));
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
    result = mt_sd_storage_list("0:/cores/c64/media", collect_entry, &list);
    assert(result.status == MT_SD_OK);
    assert(result.size == 2U);
    assert(list.count == 2U);
    assert(list.total_size == 174848U);
    assert(list.saw_directory);
    assert(dir_close_calls == 1U);

    memset(&list, 0, sizeof(list));
    list.stop_after = 1U;
    result = mt_sd_storage_list("0:/cores/c64/media", collect_entry, &list);
    assert(result.status == MT_SD_OK);
    assert(result.size == 1U);
    assert(dir_close_calls == 2U);

    assert(strcmp(mt_sd_status_name(MT_SD_MOUNT_FAILED), "mount failed") ==
           0);
    assert(strcmp(mt_sd_status_name((mt_sd_status_t)99), "unknown") == 0);
    return 0;
}
