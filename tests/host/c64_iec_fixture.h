/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef C64_IEC_FIXTURE_H
#define C64_IEC_FIXTURE_H
#include "multitini_c64_iec_dos.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define F_CAP 300
#define F_BYTES 16384
typedef struct
{
    char path[512];
    uint8_t bytes[F_BYTES];
    size_t size;
    bool directory, exists;
} fixture_file;
typedef struct
{
    fixture_file *file;
    size_t cursor;
    bool used, write;
} fixture_handle;
typedef struct
{
    fixture_file files[F_CAP];
    fixture_handle handles[16];
    unsigned reads, writes, closes, commits, removes;
    int fail_read, fail_write, fail_close, fail_commit;
    bool keep_handle, short_read, short_write;
    char protected_name[512];
} fixture_fs;
static fixture_fs fixture;
static inline int fixture_equal(const char *a, const char *b)
{
    while (*a && *b)
    {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}
static inline fixture_file *fixture_find(const char *path)
{
    for (unsigned i = 0; i < F_CAP; ++i)
        if (fixture.files[i].exists && fixture_equal(fixture.files[i].path, path))
            return &fixture.files[i];
    return NULL;
}
static inline fixture_file *fixture_add(const char *path, bool directory, const uint8_t *bytes, size_t size)
{
    fixture_file *f = fixture_find(path);
    if (!f)
        for (unsigned i = 0; i < F_CAP; ++i)
            if (!fixture.files[i].exists)
            {
                f = &fixture.files[i];
                break;
            }
    assert(f && size <= F_BYTES);
    memset(f, 0, sizeof *f);
    strcpy(f->path, path);
    f->exists = true;
    f->directory = directory;
    f->size = size;
    if (size)
        memcpy(f->bytes, bytes, size);
    return f;
}
static inline void fixture_reset(void)
{
    memset(&fixture, 0, sizeof fixture);
    fixture_add("0:/media", true, NULL, 0);
    fixture_add("0:/media/SUB", true, NULL, 0);
}
static inline int fixture_list(void *ctx, const char *path, mt_iec_list_fn cb, void *arg)
{
    (void)ctx;
    fixture_file *dir = fixture_find(path);
    if (!dir || !dir->directory)
        return 71;
    size_t n = strlen(path);
    if (n && path[n - 1] == '/')
        --n;
    for (unsigned i = 0; i < F_CAP; ++i)
    {
        fixture_file *f = &fixture.files[i];
        if (!f->exists || strncmp(f->path, path, n) || f->path[n] != '/')
            continue;
        const char *name = f->path + n + 1;
        if (!strchr(name, '/'))
        {
            int rc = cb(arg, name, f->size, f->directory);
            if (rc)
                return rc;
        }
    }
    return 0;
}
static inline int fixture_open(void *ctx, const char *path, const char *mode, void **out, size_t *size)
{
    (void)ctx;
    *out = NULL;
    fixture_file *f = fixture_find(path);
    bool write = mode[0] != 'r' || strchr(mode, '+') != NULL;
    if (mode[0] == 'x' && f)
        return 63;
    if (mode[0] == 'r' && !f)
        return 62;
    if (f && f->directory)
        return 64;
    for (unsigned i = 0; i < 16; ++i)
        if (fixture.handles[i].used && fixture.handles[i].file == f && (write || fixture.handles[i].write))
            return 70;
    fixture_handle *h = NULL;
    for (unsigned i = 0; i < 16; ++i)
        if (!fixture.handles[i].used)
        {
            h = &fixture.handles[i];
            break;
        }
    if (!h)
        return 70;
    if (!f)
        f = fixture_add(path, false, NULL, 0);
    if (mode[0] == 'w')
        f->size = 0;
    h->file = f;
    h->used = true;
    h->write = write;
    h->cursor = mode[0] == 'a' ? f->size : 0;
    *size = f->size;
    *out = h;
    return 0;
}
static inline int fixture_read(void *ctx, void *handle, uint8_t *data, size_t cap, size_t *got)
{
    (void)ctx;
    fixture_handle *h = handle;
    assert(h && h->used);
    ++fixture.reads;
    *got = 0;
    if (fixture.fail_read)
        return fixture.fail_read;
    size_t n = h->file->size - h->cursor;
    if (n > cap)
        n = cap;
    if (fixture.short_read && n)
        --n;
    memcpy(data, h->file->bytes + h->cursor, n);
    h->cursor += n;
    *got = n;
    return 0;
}
static inline int fixture_write(void *ctx, void *handle, const uint8_t *data, size_t n, size_t *wrote)
{
    (void)ctx;
    fixture_handle *h = handle;
    assert(h && h->used && h->write);
    ++fixture.writes;
    *wrote = 0;
    if (fixture.fail_write)
        return fixture.fail_write;
    if (n > F_BYTES - h->cursor)
        return 72;
    if (fixture.short_write && n)
        --n;
    memcpy(h->file->bytes + h->cursor, data, n);
    h->cursor += n;
    if (h->file->size < h->cursor)
        h->file->size = h->cursor;
    *wrote = n;
    return 0;
}
static inline int fixture_close(void *ctx, void **handle)
{
    (void)ctx;
    fixture_handle *h = *handle;
    assert(h && h->used);
    ++fixture.closes;
    if (!fixture.fail_close || !fixture.keep_handle)
    {
        h->used = false;
        *handle = NULL;
    }
    return fixture.fail_close;
}
static inline int fixture_remove(void *ctx, const char *path)
{
    (void)ctx;
    fixture_file *f = fixture_find(path);
    if (!f)
        return 62;
    for (unsigned i = 0; i < 16; ++i)
        if (fixture.handles[i].used && fixture.handles[i].file == f)
            return 70;
    if (f->directory)
    {
        size_t n = strlen(path);
        for (unsigned i = 0; i < F_CAP; ++i)
            if (fixture.files[i].exists && !strncmp(fixture.files[i].path, path, n) && fixture.files[i].path[n] == '/')
                return 25;
    }
    f->exists = false;
    ++fixture.removes;
    return 0;
}
static inline int fixture_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    fixture_file *f = fixture_find(from);
    if (!f)
        return 62;
    if (fixture_find(to))
        return 63;
    strcpy(f->path, to);
    return 0;
}
static inline int fixture_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    if (fixture_find(path))
        return 63;
    fixture_add(path, true, NULL, 0);
    return 0;
}
static inline int fixture_commit(void *ctx, const char *temp, const char *target, bool replace)
{
    (void)ctx;
    ++fixture.commits;
    if (fixture.fail_commit)
        return fixture.fail_commit;
    fixture_file *t = fixture_find(temp);
    assert(t);
    if (fixture_find(target) && !replace)
        return 63;
    fixture_add(target, false, t->bytes, t->size);
    t->exists = false;
    return 0;
}
static inline bool fixture_protected(void *ctx, const char *path)
{
    (void)ctx;
    return fixture.protected_name[0] && fixture_equal(fixture.protected_name, path);
}
static inline mt_iec_fs_t fixture_api(void)
{
    mt_iec_fs_t fs = {NULL,           fixture_list,   fixture_open,  fixture_read,   fixture_write,    fixture_close,
                      fixture_remove, fixture_rename, fixture_mkdir, fixture_commit, fixture_protected};
    return fs;
}
#endif
