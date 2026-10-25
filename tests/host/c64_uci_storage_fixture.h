/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef C64_UCI_STORAGE_FIXTURE_H
#define C64_UCI_STORAGE_FIXTURE_H
#ifndef F_CAP
#define F_CAP 64
#endif
#ifndef F_BYTES
#define F_BYTES 65536
#endif
#include "c64_iec_fixture.h"
#include "sd_storage.h"
#ifndef UCI_STORAGE_OBSERVE
#define UCI_STORAGE_OBSERVE() ((void)0)
#endif
struct mt_sd_directory
{
    bool used;
    unsigned cursor;
    char path[512];
};
static struct mt_sd_directory directories[4];
static unsigned file_close_failures, directory_close_failures;
static unsigned file_close_released_failures;
static unsigned file_close_calls, directory_close_calls;
static unsigned remove_failures, rename_failures;
static mt_sd_result_t sd_result(int error, size_t bytes)
{
    return (mt_sd_result_t){error == 0    ? MT_SD_OK
                            : error == 70 ? MT_SD_FILE_BUSY
                            : error == 63 ? MT_SD_FILE_EXISTS
                                          : MT_SD_OPEN_FAILED,
                            error == 62   ? 4U
                            : error == 63 ? 8U
                            : error       ? 1U
                                          : 0U,
                            bytes};
}
bool mt_sd_file_path_busy(const char *path, bool exclusive)
{
    fixture_file *f = fixture_find(path);
    for (unsigned i = 0; i < 16; i++)
        if (fixture.handles[i].used && fixture.handles[i].file == f &&
            (exclusive || fixture.handles[i].write))
            return true;
    return false;
}
mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **out)
{
    size_t size = 0;
    void *handle = NULL;
    int e = fixture_open(NULL, path, mode, &handle, &size);
    *out = (mt_sd_file_t *)handle;
    return sd_result(e, size);
}
mt_sd_result_t mt_sd_file_open_flags(const char *path, uint8_t flags, mt_sd_file_t **out)
{
    if (!(flags & 3) || (flags & 0xe0))
        return sd_result(30, 0);
    const char *mode = (flags & 8)    ? "wb+"
                       : (flags & 4)  ? "xb"
                       : (flags & 16) ? "ab+"
                       : (flags & 2)  ? "rb+"
                                      : "rb";
    mt_sd_result_t r = mt_sd_file_open(path, mode, out);
    if (r.status == MT_SD_OK)
        ((fixture_handle *)*out)->cursor = 0;
    return r;
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *file, void *data, size_t cap)
{
    UCI_STORAGE_OBSERVE();
    size_t got = 0;
    int e = fixture_read(NULL, file, data, cap, &got);
    return sd_result(e, got);
}
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *file, const void *data, size_t n)
{
    UCI_STORAGE_OBSERVE();
    size_t got = 0;
    int e = fixture_write(NULL, file, data, n, &got);
    return sd_result(e, got);
}
mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *file, const void *data, size_t n)
{
    return mt_sd_file_write(file, data, n);
}
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file)
{
    UCI_STORAGE_OBSERVE();
    file_close_calls++;
    if (file_close_failures) {
        file_close_failures--;
        return (mt_sd_result_t){MT_SD_CLOSE_FAILED, 1, 0};
    }
    void *p = file;
    if (file_close_released_failures) {
        file_close_released_failures--;
        assert(fixture_close(NULL, &p) == 0 && p == NULL);
        return (mt_sd_result_t){MT_SD_SYNC_FAILED, 1, 0};
    }
    return sd_result(fixture_close(NULL, &p), 0);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *file, int64_t offset, int origin)
{
    fixture_handle *h = (fixture_handle *)file;
    int64_t p = origin == SEEK_CUR   ? (int64_t)h->cursor
                : origin == SEEK_END ? (int64_t)h->file->size
                                     : 0;
    p += offset;
    if (p < 0 || (uint64_t)p > h->file->size)
        return sd_result(30, 0);
    h->cursor = (size_t)p;
    return sd_result(0, 0);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *file)
{
    const fixture_handle *h = (const fixture_handle *)file;
    return h && h->used ? (int64_t)h->cursor : -1;
}
int mt_sd_file_eof(const mt_sd_file_t *file)
{
    const fixture_handle *h = (const fixture_handle *)file;
    return h->cursor == h->file->size;
}
mt_sd_result_t mt_sd_file_remove(const char *path)
{
    if (remove_failures) {
        remove_failures--;
        return sd_result(25, 0);
    }
    return sd_result(fixture_remove(NULL, path), 0);
}
mt_sd_result_t mt_sd_file_rename(const char *from, const char *to)
{
    if (rename_failures) {
        rename_failures--;
        return sd_result(25, 0);
    }
    return sd_result(fixture_rename(NULL, from, to), 0);
}
mt_sd_result_t mt_sd_storage_ensure_directory(const char *path)
{
    fixture_file *f = fixture_find(path);
    return sd_result(f ? (f->directory ? 0 : 63) : fixture_mkdir(NULL, path), 0);
}
static void sd_info(fixture_file *f, mt_sd_info_t *info)
{
    memset(info, 0, sizeof *info);
    const char *base = strrchr(f->path, '/');
    strcpy(info->name, base ? base + 1 : f->path);
    info->size = f->size;
    info->attributes = f->directory ? 16 : 32;
    info->date = 0x5d18;
    info->time = 0x6322;
}
mt_sd_result_t mt_sd_file_stat(const char *path, mt_sd_info_t *info)
{
    fixture_file *f = fixture_find(path);
    if (!f)
        return sd_result(62, 0);
    sd_info(f, info);
    return sd_result(0, 0);
}
/* Backend identity fixture. The real component walker is tested separately
 * with the real sd_storage.c and a FatFs f_stat alias fixture. */
mt_sd_result_t mt_sd_file_canonical_path(const char *path, char *out, size_t capacity)
{
    static const char *const aliases[][2] = {
        {"0:/media/LONGDI~1", "0:/media/Long Directory"},
        {"0:/media/MOUNTE~1.D64", "0:/media/Mounted Disk.d64"},
        {"0:/media/MTIECS~1.TMP", "0:/media/.mtiec-save"}
    };
    char text[512];
    if (!path || strlen(path) >= sizeof text) return sd_result(30, 0);
    strcpy(text, path);
    for (unsigned i = 0; i < 3; i++) {
        size_t n = strlen(aliases[i][0]);
        if (!strncmp(text, aliases[i][0], n) && (!text[n] || text[n] == '/')) {
            char tail[512];
            strcpy(tail, text + n);
            assert(strlen(aliases[i][1]) + strlen(tail) < sizeof text);
            strcpy(text, aliases[i][1]);
            strcat(text, tail);
            break;
        }
    }
    for (const char *part = text; *part;) {
        size_t n = strcspn(part, "/");
        if (n && (part[n - 1] == '.' || part[n - 1] == ' ')) return sd_result(30, 0);
        part += n;
        if (*part) part++;
    }
    if (strlen(text) >= capacity) return sd_result(30, 0);
    memmove(out, text, strlen(text) + 1);
    return sd_result(0, 0);
}
mt_sd_result_t mt_sd_directory_open(const char *path, mt_sd_directory_t **out)
{
    *out = NULL;
    fixture_file *f = fixture_find(path);
    if (!f || !f->directory)
        return sd_result(62, 0);
    for (unsigned i = 0; i < 4; i++)
        if (!directories[i].used) {
            directories[i].used = true;
            directories[i].cursor = 0;
            strcpy(directories[i].path, path);
            *out = &directories[i];
            return sd_result(0, 0);
        }
    return sd_result(70, 0);
}
mt_sd_result_t mt_sd_directory_next(mt_sd_directory_t *dir, mt_sd_info_t *info)
{
    assert(dir && dir->used);
    memset(info, 0, sizeof *info);
    size_t n = strlen(dir->path);
    while (dir->cursor < F_CAP) {
        fixture_file *f = &fixture.files[dir->cursor++];
        if (f->exists && !strncmp(f->path, dir->path, n) && f->path[n] == '/' &&
            !strchr(f->path + n + 1, '/')) {
            sd_info(f, info);
            break;
        }
    }
    return sd_result(0, 0);
}
mt_sd_result_t mt_sd_directory_close(mt_sd_directory_t *dir)
{
    assert(dir && dir->used);
    directory_close_calls++;
    if (directory_close_failures) {
        directory_close_failures--;
        return (mt_sd_result_t){MT_SD_DIR_CLOSE_FAILED, 1, 0};
    }
    dir->used = false;
    return sd_result(0, 0);
}
#ifndef UCI_STORAGE_CUSTOM_JOURNAL
mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal,
                                             const uint8_t *data, size_t size)
{
    (void)journal;
    fixture_file *f = fixture_find(path);
    if (!f || size > F_BYTES) return sd_result(25, 0);
    memcpy(f->bytes, data, size);
    f->size = size;
    return sd_result(0, size);
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal,
                                             uint8_t *data, size_t size)
{
    (void)path; (void)journal; (void)data; (void)size;
    return (mt_sd_result_t){MT_SD_JOURNAL_INVALID, 1, 0};
}
#endif
#endif
