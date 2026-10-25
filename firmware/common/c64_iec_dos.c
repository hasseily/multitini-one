/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_iec_dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fold(unsigned c)
{
    if (c >= 0xc1 && c <= 0xda)
        return c - 0x80;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c == 0xa0 ? ' ' : c;
}
static int compare(const char *a, const char *b)
{
    while (*a && fold((uint8_t)*a) == fold((uint8_t)*b))
    {
        ++a;
        ++b;
    }
    return (int)fold((uint8_t)*a) - (int)fold((uint8_t)*b);
}
static bool suffix(const char *s, const char *tail)
{
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && compare(s + n - t, tail) == 0;
}
bool mt_iec_dos_recovery_name(const char *s)
{
    const char *prefix = ".MTIEC-";
    bool same = strlen(s) >= 7;
    for (unsigned i = 0; i < 7 && same; ++i)
        if (fold((uint8_t)s[i]) != (unsigned)prefix[i])
            same = false;
    return same || suffix(s, ".mtiec") || suffix(s, ".mtj");
}
static bool hidden(const char *s)
{
    return mt_iec_dos_recovery_name(s);
}
static bool component(const char *s, bool wildcard)
{
    size_t n = strlen(s);
    if (!n || n >= MT_IEC_NAME || s[0] == ' ' || s[n - 1] == ' ' || s[n - 1] == '.' || hidden(s))
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned c = (uint8_t)s[i];
        if (c < 32 || strchr("<>:\"/\\|", (int)c) || (!wildcard && (c == '*' || c == '?')))
            return false;
    }
    return true;
}
static bool match(const char *pattern, const char *s)
{
    const char *star = NULL, *retry = NULL;
    while (*s)
    {
        if (*pattern == '?' || fold((uint8_t)*pattern) == fold((uint8_t)*s))
        {
            ++pattern;
            ++s;
        }
        else if (*pattern == '*')
        {
            star = pattern++;
            retry = s;
        }
        else if (star)
        {
            pattern = star + 1;
            s = ++retry;
        }
        else
            return false;
    }
    while (*pattern == '*')
        ++pattern;
    return *pattern == 0;
}
void mt_iec_dos_error(mt_iec_dos_t *d, int code)
{
    ++d->status_generation;
    const char *s = "DRIVE NOT READY";
    switch (code)
    {
    case 0:
        s = "OK";
        break;
    case 1:
        s = "FILES SCRATCHED";
        break;
    case 23:
        s = "READ ERROR";
        break;
    case 25:
        s = "WRITE ERROR";
        break;
    case 26:
        s = "WRITE PROTECT ON";
        break;
    case 30:
        s = "SYNTAX ERROR";
        break;
    case 33:
        s = "ILLEGAL FILENAME";
        break;
    case 50:
        s = "RECORD NOT PRESENT";
        break;
    case 51:
        s = "OVERFLOW IN RECORD";
        break;
    case 65:
        s = "NO BLOCK";
        break;
    case 66:
        s = "ILLEGAL TRACK OR SECTOR";
        break;
    case 77:
        s = "SELECTED PARTITION ILLEGAL";
        break;
    case 2:
        s = "PARTITION SELECTED";
        break;
    case 62:
        s = "FILE NOT FOUND";
        break;
    case 63:
        s = "FILE EXISTS";
        break;
    case 64:
        s = "FILE TYPE MISMATCH";
        break;
    case 70:
        s = "NO CHANNEL";
        break;
    case 71:
        s = "DIRECTORY ERROR";
        break;
    case 72:
        s = "DISK FULL";
        break;
    case 73:
        s = "MULTITINI IEC DOS V1";
        break;
    case 98:
        s = "UNKNOWN DRIVECODE";
        break;
    default:
        code = 74;
        break;
    }
    (void)snprintf(d->status, sizeof d->status, "%02d,%s,00,00\r", code, s);
    d->status_size = strlen(d->status);
    d->status_cursor = 0;
}
static int result(mt_iec_dos_t *d, int rc)
{
    mt_iec_dos_error(d, rc);
    return rc;
}
static bool join(char *out, const char *parent, const char *name)
{
    size_t n = strlen(parent), m = strlen(name);
    if (n + m + 2 > MT_IEC_PATH)
        return false;
    memcpy(out, parent, n);
    if (!n || out[n - 1] != '/')
        out[n++] = '/';
    memcpy(out + n, name, m + 1);
    return true;
}
static bool protected_path(mt_iec_dos_t *d, const char *p)
{
    return d->write_protect || (d->fs.protected_path && d->fs.protected_path(d->fs.context, p));
}
static bool within(const char *path, const char *parent)
{
    while (*parent && *path && fold((uint8_t)*path) == fold((uint8_t)*parent)) {
        ++path; ++parent;
    }
    return !*parent && (!*path || *path == '/');
}
static bool in_use(mt_iec_dos_t *d, const char *p)
{
    for (unsigned i = 0; i < 15; ++i)
        if (d->channels[i].kind && within(d->channels[i].target, p))
            return true;
    return false;
}
static int move_directory(mt_iec_dos_t *d, const char *source, const char *target)
{
    if (within(target, source)) return 26;
    if (in_use(d, source) || in_use(d, target)) return 70;
    size_t old = strlen(source), replacement = strlen(target);
    /* Validate every stored path before changing the filesystem. A successful
     * move must never strand another partition or its current directory. */
    for (unsigned i = 1; i < 256; ++i) {
        const char *paths[] = {d->partitions[i].root, d->partitions[i].cwd};
        for (unsigned j = 0; j < 2; ++j)
            if (within(paths[j], source) && replacement + strlen(paths[j] + old) >= MT_IEC_PATH)
                return 33;
    }
    int rc = d->fs.rename ? d->fs.rename(d->fs.context, source, target) : 30;
    if (!rc) {
        for (unsigned i = 1; i < 256; ++i) {
            char *paths[] = {d->partitions[i].root, d->partitions[i].cwd};
            for (unsigned j = 0; j < 2; ++j) if (within(paths[j], source)) {
                memmove(paths[j] + replacement, paths[j] + old, strlen(paths[j] + old) + 1);
                memcpy(paths[j], target, replacement);
            }
        }
        if (d->active_partition) strcpy(d->cwd, d->partitions[d->active_partition].cwd);
    }
    return rc;
}
static const char *partition_root(const mt_iec_dos_t *d)
{
    unsigned p = d->active_partition;
    return p && p < 256 && d->partitions[p].root[0] ? d->partitions[p].root : d->root;
}
static int validate_partition_entry(void *context, const char *name, size_t size, bool directory)
{
    (void)context; (void)name; (void)size; (void)directory;
    return 0;
}
int mt_iec_dos_partition_add(mt_iec_dos_t *d, unsigned p, const char *path, const char *name)
{
    size_t root = strlen(d->root), n = path ? strlen(path) : 0;
    if (!p || p > 255 || !name || !n || n >= MT_IEC_PATH || n < root ||
        strncmp(path, d->root, root) || (path[root] && path[root] != '/') ||
        strstr(path, "/../") || suffix(path, "/..") || strstr(path, "/./") || suffix(path, "/.") || strchr(path, '\\'))
        return 77;
    int rc = d->fs.list(d->fs.context, path, validate_partition_entry, NULL);
    if (rc) return rc;
    mt_iec_partition_t *part = &d->partitions[p];
    strcpy(part->root, path);
    while (n > root && part->root[n - 1] == '/') part->root[--n] = 0;
    strcpy(part->cwd, part->root);
    (void)snprintf(part->name, sizeof part->name, "%.16s", name);
    if (p == d->current_partition) strcpy(d->cwd, part->cwd);
    return 0;
}
int mt_iec_dos_partition_remove(mt_iec_dos_t *d, unsigned p)
{
    if (!p || p > 255) return 77;
    memset(&d->partitions[p], 0, sizeof d->partitions[p]);
    if (d->current_partition == p) {
        d->current_partition = d->active_partition = 0;
        for (unsigned i = 1; i < 256; ++i) if (d->partitions[i].root[0]) {
            d->current_partition = d->active_partition = i;
            strcpy(d->cwd, d->partitions[i].cwd);
            break;
        }
        if (!d->current_partition) strcpy(d->cwd, d->root);
    }
    return 0;
}
static int parent_up(mt_iec_dos_t *d, char *path)
{
    const char *root = partition_root(d);
    if (!compare(path, root))
        return 26;
    char *last = strrchr(path, '/');
    if (!last)
        return 26;
    if ((size_t)(last - path) < strlen(root))
        strcpy(path, root);
    else
        *last = 0;
    return 0;
}
static int entry(void *ctx, const char *name, size_t size, bool directory)
{
    mt_iec_dos_t *d = ctx;
    if (!strcmp(name, ".") || !strcmp(name, "..") || hidden(name))
        return 0;
    if (strlen(name) >= MT_IEC_NAME || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':'))
        return 71;
    if (d->entry_count == MT_IEC_ENTRIES)
        return 71;
    mt_iec_entry_t *e = &d->entries[d->entry_count++];
    memset(e, 0, sizeof *e);
    strcpy(e->name, name);
    e->size = size;
    e->directory = directory;
    e->type = directory ? 'D' : suffix(name, ".seq") ? 'S' : suffix(name, ".usr") ? 'U' :
              suffix(name, ".rel") ? 'L' : 'P';
    return 0;
}
static int sort_entry(const void *a, const void *b)
{
    const mt_iec_entry_t *x = a, *y = b;
    int c = compare(x->name, y->name);
    return c ? c : strcmp(x->name, y->name);
}
static bool alias_char(unsigned c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.';
}
static bool typed_extension(const char *name)
{
    return suffix(name, ".prg") || suffix(name, ".seq") || suffix(name, ".usr") || suffix(name, ".rel");
}
static bool raw_extension(const char *name)
{
    return suffix(name,".d64") || suffix(name,".d41") || suffix(name,".d71") ||
           suffix(name,".d81") || suffix(name,".dnp") || suffix(name,".crt") || suffix(name,".tcrt");
}
static char wrapper_type(const char *name)
{
    size_t n = strlen(name);
    if (n < 4 || name[n - 4] != '.' || name[n - 2] < '0' || name[n - 2] > '9' ||
        name[n - 1] < '0' || name[n - 1] > '9') return 0;
    unsigned c = fold((uint8_t)name[n - 3]);
    return c == 'P' || c == 'S' || c == 'U' ? (char)c : c == 'R' ? 'L' : 0;
}
static int probe_wrapper(mt_iec_dos_t *d, const char *path, mt_iec_entry_t *e)
{
    char type = wrapper_type(e->name);
    if (!type || e->directory || e->size < 26) return 0;
    if (d->probe_handle) return 70;
    uint8_t h[26];
    size_t size = 0, got = 0;
    int rc = d->fs.open(d->fs.context, path, "r", &d->probe_handle, &size);
    if (!rc) rc = d->fs.read(d->fs.context, d->probe_handle, h, sizeof h, &got);
    if (d->probe_handle) {
        int close = d->fs.close(d->fs.context, &d->probe_handle);
        if (!rc) rc = close;
    }
    if (rc) return rc;
    if (got != sizeof h) return 23;
    if (memcmp(h, "C64File\0", 8)) return 0;
    size_t n = 0;
    while (n < 16 && h[8 + n]) { e->alias[n] = (char)h[8 + n]; ++n; }
    while (n && (uint8_t)e->alias[n - 1] == 0xa0) --n;
    e->alias[n] = 0;
    e->type = type;
    e->data_offset = 26;
    e->record_size = h[25];
    return 0;
}
static int listing(mt_iec_dos_t *d, const char *path)
{
    d->entry_count = 0;
    int rc = d->fs.list(d->fs.context, path, entry, d);
    if (rc)
        return rc;
    qsort(d->entries, d->entry_count, sizeof d->entries[0], sort_entry);
    for (size_t i = 0; i < d->entry_count; ++i)
    {
        mt_iec_entry_t *e = &d->entries[i];
        char full[MT_IEC_PATH];
        if (!join(full, path, e->name)) return 33;
        if (d->fs.metadata) {
            rc = d->fs.metadata(d->fs.context, full, &e->metadata);
            if (rc) return rc;
        }
        rc = probe_wrapper(d, full, e);
        if (rc) return rc;
        if (e->data_offset) continue;
        size_t n = strlen(e->name);
        if (!e->directory && typed_extension(e->name))
            n -= 4;
        bool plain = n > 0 && n <= 16;
        uint32_t hash = 2166136261U;
        for (size_t j = 0; e->name[j]; ++j)
            hash = (hash ^ (uint8_t)e->name[j]) * 16777619U;
        for (size_t j = 0; j < n; ++j)
            if (!alias_char(fold((uint8_t)e->name[j])))
                plain = false;
        if (plain)
        {
            for (size_t j = 0; j < n; ++j)
                e->alias[j] = (char)fold((uint8_t)e->name[j]);
            e->alias[n] = 0;
            if (!component(e->alias, false))
                plain = false;
            for (size_t j = 0; j < i; ++j)
                if (!compare(e->alias, d->entries[j].alias))
                    plain = false;
        }
        if (!plain)
        {
            char prefix[10];
            for (size_t j = 0; j < 9; ++j)
                prefix[j] = j < n && alias_char(fold((uint8_t)e->name[j])) ? (char)fold((uint8_t)e->name[j]) : '_';
            prefix[9] = 0;
            if (prefix[0] == ' ')
                prefix[0] = '_';
            for (unsigned salt = 0;; ++salt)
            {
                (void)snprintf(e->alias, sizeof e->alias, "%s~%06X", prefix, (unsigned)((hash + salt) & 0xffffffU));
                bool collision = false;
                for (size_t j = 0; j < i; ++j)
                    if (!compare(e->alias, d->entries[j].alias))
                        collision = true;
                if (!collision)
                    break;
            }
        }
    }
    return 0;
}
static int lookup(mt_iec_dos_t *d, const char *path, const char *name, bool wild, int type, mt_iec_entry_t *out)
{
    int rc = listing(d, path);
    if (rc)
        return rc;
    for (size_t i = 0; i < d->entry_count; ++i)
    {
        mt_iec_entry_t *e = &d->entries[i];
        bool container = e->directory || suffix(e->name, ".d64") || suffix(e->name, ".d71") ||
                         suffix(e->name, ".d81") || suffix(e->name, ".dnp");
        if ((!type || e->type == type || (type == 'D' && container)) &&
            ((!compare(name, e->alias) || !compare(name, e->name)) || (wild && match(name, e->alias))))
        {
            *out = *e;
            return 0;
        }
    }
    return 62;
}
/* Resolve directory components by the same aliases used by $. Never pass a
 * user-supplied volume, '..', or backslash to the filesystem. */
static int resolve(mt_iec_dos_t *d, const char *text, char *parent, char *leaf)
{
    if (strchr(text, ':') || strchr(text, '\\'))
        return 33;
    strcpy(parent, *text == '/' ? partition_root(d) : d->cwd);
    while (*text == '/')
        ++text;
    while (*text)
    {
        const char *end = strchr(text, '/');
        size_t n = end ? (size_t)(end - text) : strlen(text);
        if (!n || n >= MT_IEC_NAME)
            return 33;
        memcpy(leaf, text, n);
        leaf[n] = 0;
        if (!end)
            return hidden(leaf) ? 26 : (!strcmp(leaf, ".") || !strcmp(leaf, "..") || component(leaf, true)) ? 0 : 33;
        if (!strcmp(leaf, ".."))
        {
            int rc = parent_up(d, parent);
            if (rc)
                return rc;
        }
        else if (strcmp(leaf, "."))
        {
            if (!component(leaf, false))
                return 33;
            mt_iec_entry_t e;
            int rc = lookup(d, parent, leaf, false, 'D', &e);
            if (rc)
                return rc;
            char tmp[MT_IEC_PATH];
            if (!join(tmp, parent, e.name))
                return 33;
            strcpy(parent, tmp);
        }
        text = end + 1;
        while (*text == '/')
            ++text;
    }
    leaf[0] = 0;
    return 0;
}
static int text_name(const uint8_t *src, size_t n, char *out)
{
    if (n >= MT_IEC_NAME)
        return 33;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned c = fold(src[i]);
        if (c < 32 || c > 126)
            return 33;
        out[i] = (char)c;
    }
    out[n] = 0;
    return 0;
}
/* Select a partition only for this operation. The selected partition retains
 * its own cwd; explicit OPEN paths do not change the current partition. */
static int select_name(mt_iec_dos_t *d, const uint8_t *src, size_t n, uint8_t out[MT_IEC_NAME],
                       size_t *length, char oldcwd[MT_IEC_PATH], unsigned *oldpart)
{
    strcpy(oldcwd, d->cwd);
    *oldpart = d->active_partition;
    if (n >= MT_IEC_NAME) return 33;
    if (n) memcpy(out, src, n);
    out[n] = 0; *length = n;
    size_t start = n && (out[0] == '$' || out[0] == '@') ? 1 : 0;
    if (start && out[0] == '$' && n > 2 && out[1] == '=') {
        if (out[2] == 'P') return 0;
        if (out[2] == 'T') start = 3;
    }
    size_t colon = start;
    while (colon < n && out[colon] != ':') ++colon;
    if (colon == n && (!start || out[0] != '$')) return 0;
    size_t p = start;
    unsigned number = 0;
    bool digits = false;
    while (p < colon && out[p] >= '0' && out[p] <= '9') {
        digits = true; number = number * 10U + out[p++] - '0';
        if (number > 255) return 77;
    }
    if (!number) number = d->current_partition;
    if (!number || !d->partitions[number].root[0]) return 77;
    if (digits || colon < n) {
        d->active_partition = number;
        strcpy(d->cwd, number == d->current_partition ? oldcwd : d->partitions[number].cwd);
    }
    if (p > start) { memmove(out + start, out + p, n - p + 1); n -= p - start; colon -= p - start; }
    if (colon < n) {
        /* A path before ':' is rooted in the selected partition. Join the
         * filename after it without letting another colon reach FatFs. */
        if (colon == start || (colon > start && out[colon - 1] == '/')) {
            memmove(out + colon, out + colon + 1, n - colon); --n;
        } else if (out[start] == '/') out[colon] = '/';
        else return 33;
    }
    *length = n;
    return 0;
}
static void restore_name(mt_iec_dos_t *d, const char *cwd, unsigned part)
{
    strcpy(d->cwd, cwd);
    d->active_partition = part;
}
int mt_iec_dos_init(mt_iec_dos_t *d, const mt_iec_fs_t *fs, const char *root)
{
    if (!d || !fs || !root || !fs->list || !fs->open || !fs->read || !fs->write || !fs->close || !fs->commit)
        return 74;
    size_t n = strlen(root);
    if (n < 3 || n >= MT_IEC_PATH || root[0] != '0' || root[1] != ':' || root[2] != '/' || strchr(root, '\\') ||
        strstr(root, "/../") || strstr(root, "/./") || suffix(root, "/..") || suffix(root, "/."))
        return 33;
    memset(d, 0, sizeof *d);
    d->fs = *fs;
    strcpy(d->root, root);
    while (n > 3 && d->root[n - 1] == '/')
        d->root[--n] = 0;
    strcpy(d->cwd, d->root);
    d->current_partition = d->active_partition = 1;
    d->current_device = d->configured_device = 9;
    strcpy(d->partitions[1].root, d->root);
    strcpy(d->partitions[1].cwd, d->root);
    strcpy(d->partitions[1].name, "MULTITINI");
    d->directory_owner = -1;
    mt_iec_dos_error(d, 73);
    return 0;
}
static int flush(mt_iec_dos_t *d, mt_iec_channel_t *c)
{
    if (c->error)
        return c->error;
    if (c->used)
    {
        size_t n = 0;
        int rc = d->fs.write(d->fs.context, c->handle, c->buffer, c->used, &n);
        if (rc || n != c->used)
        {
            c->error = rc ? rc : 25;
            return c->error;
        }
        c->used = 0;
    }
    return 0;
}
static int record_load(mt_iec_dos_t *d, mt_iec_channel_t *c, size_t offset)
{
    c->cursor = offset;
    c->record_dirty = false;
    memset(c->buffer, 0, c->record_size);
    c->record_missing = c->record_offset >= c->size;
    if (c->record_missing) {
        c->buffer[0] = 255;
        c->used = 1;
        c->cursor = 0;
        return 0;
    }
    int rc = d->fs.seek(d->fs.context, c->handle, c->record_offset);
    size_t got = 0, n = c->size - c->record_offset;
    if (n > c->record_size) n = c->record_size;
    if (!rc) rc = d->fs.read(d->fs.context, c->handle, c->buffer, n, &got);
    if (rc || got != n) return c->error = rc ? rc : 23;
    c->used = c->record_size;
    while (c->used > 1 && !c->buffer[c->used - 1]) --c->used;
    return 0;
}
static int record_flush(mt_iec_dos_t *d, mt_iec_channel_t *c)
{
    if (!c->record_dirty) return 0;
    if (!c->writable || protected_path(d, c->target)) return 26;
    if (c->record_offset > MT_IEC_MAX_FILE - c->record_size) return 72;
    uint8_t empty[256] = {0};
    int rc = 0;
    size_t wrote = 0;
    if (c->size < c->record_offset) {
        rc = d->fs.seek(d->fs.context, c->handle, c->size);
        size_t partial = (c->size - c->data_offset) % c->record_size;
        while (!rc && c->size < c->record_offset) {
            size_t count = c->record_size - partial;
            empty[0] = partial ? 0 : 255;
            rc = d->fs.write(d->fs.context, c->handle, empty, count, &wrote);
            if (!rc && wrote != count) rc = 25;
            if (!rc) c->size += count;
            partial = 0;
        }
    }
    if (!rc) rc = d->fs.seek(d->fs.context, c->handle, c->record_offset);
    if (c->cursor < c->record_size) memset(c->buffer + c->cursor, 0, c->record_size - c->cursor);
    if (!c->cursor) c->buffer[0] = 255;
    if (!rc) rc = d->fs.write(d->fs.context, c->handle, c->buffer, c->record_size, &wrote);
    if (!rc && wrote != c->record_size) rc = 25;
    if (rc) return c->error = rc;
    c->record_dirty = false;
    c->complete = true;
    if (c->size < c->record_offset + c->record_size) c->size = c->record_offset + c->record_size;
    return 0;
}
static int close_channel(mt_iec_dos_t *d, unsigned ch, bool abort)
{
    mt_iec_channel_t *c = &d->channels[ch];
    if (!c->kind)
        return 0;
    int rc = c->error;
    if (c->kind == 2 && !abort && !rc)
        rc = flush(d, c);
    if (c->kind == 5 && !abort && !rc) rc = record_flush(d, c);
    if (c->handle)
    {
        int r = d->fs.close(d->fs.context, &c->handle);
        if (!rc)
            rc = r;
    }
    if (c->auxiliary)
    {
        int r = d->fs.close(d->fs.context, &c->auxiliary);
        if (!rc)
            rc = r;
    }
    if (c->handle || c->auxiliary)
    {
        c->error = rc ? rc : 25;
        return c->error;
    }
    if (c->kind == 2 || (c->kind == 5 && c->writable))
    {
        if (!abort && !rc)
            rc = d->fs.commit(d->fs.context, c->temporary, c->target, c->replace);
        if (abort && !rc && d->fs.remove)
            rc = d->fs.remove(d->fs.context, c->temporary);
    }
    if (d->directory_owner == (int)ch)
        d->directory_owner = -1;
    memset(c, 0, sizeof *c);
    return rc;
}
int mt_iec_dos_close(mt_iec_dos_t *d, unsigned ch)
{
    if (ch > 15)
        return result(d, 70);
    if (ch == 15)
        return mt_iec_dos_quiesce(d);
    if (!d->channels[ch].kind)
        return 0;
    return result(d, close_channel(d, ch, false));
}
bool mt_iec_dos_has_channels(const mt_iec_dos_t *d)
{
    if (d->probe_handle) return true;
    for (unsigned i = 0; i < 15; ++i)
        if (d->channels[i].kind)
            return true;
    return false;
}
int mt_iec_dos_quiesce(mt_iec_dos_t *d)
{
    int rc = 0;
    if (d->probe_handle) rc = d->fs.close(d->fs.context, &d->probe_handle);
    for (unsigned i = 0; i < 15; ++i)
    {
        int r = close_channel(d, i, (d->channels[i].kind == 2 || d->channels[i].kind == 5) &&
                              !d->channels[i].complete);
        if (!rc)
            rc = r;
    }
    return rc ? result(d, rc) : 0;
}
int mt_iec_dos_reset(mt_iec_dos_t *d)
{
    int rc = mt_iec_dos_quiesce(d);
    if (!rc)
    {
        memset(d->channels, 0, sizeof d->channels);
        strcpy(d->cwd, partition_root(d));
        if (d->current_partition) strcpy(d->partitions[d->current_partition].cwd, d->cwd);
        mt_iec_dos_error(d, 73);
    }
    return rc;
}
static int dir_line(mt_iec_dos_t *d, unsigned number, const char *text)
{
    size_t n = strlen(text), p = d->directory_size;
    if (p + n + 5 > MT_IEC_DIRECTORY)
        return 71;
    unsigned next = 0x401U + (unsigned)(p - 2) + 4U + (unsigned)n + 1U;
    d->directory[p++] = (uint8_t)next;
    d->directory[p++] = (uint8_t)(next >> 8);
    d->directory[p++] = (uint8_t)number;
    d->directory[p++] = (uint8_t)(number >> 8);
    memcpy(d->directory + p, text, n);
    p += n;
    d->directory[p++] = 0;
    d->directory_size = p;
    return 0;
}
static int directory_target(mt_iec_dos_t *d, const char *spec, char path[MT_IEC_PATH],
                            char pattern[MT_IEC_NAME])
{
    while (*spec == ':') ++spec;
    if (!*spec) { strcpy(path, d->cwd); strcpy(pattern, "*"); return 0; }
    if (*spec != '/') { strcpy(path, d->cwd); strcpy(pattern, spec); return 0; }
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME];
    int rc = resolve(d, spec, parent, leaf);
    if (rc) return rc;
    strcpy(pattern, "*");
    if (!*leaf) { strcpy(path, parent); return 0; }
    mt_iec_entry_t e;
    rc = lookup(d, parent, leaf, false, 'D', &e);
    if (!rc) return join(path, parent, e.name) ? 0 : 33;
    if (rc != 62) return rc;
    strcpy(path, parent); strcpy(pattern, leaf);
    return 0;
}
static uint32_t fat_time(const mt_clock_time_t *t)
{
    return (uint32_t)(t->year - 1980U) << 25 | (uint32_t)t->month << 21 |
           (uint32_t)t->day << 16 | (uint32_t)t->hour << 11 | (uint32_t)t->minute << 5 | t->second / 2;
}
static int directory_date(const char *text, uint32_t *date)
{
    unsigned month, day, year, hour, minute; char marker[3]; int used = 0;
    if (sscanf(text, "%u/%u/%u %u:%u %2s%n", &month,&day,&year,&hour,&minute,marker,&used) != 6 ||
        text[used] || (strcmp(marker,"AM") && strcmp(marker,"PM")) || year > 99 ||
        month < 1 || month > 12 || day < 1 || day > 31 || hour < 1 || hour > 12 || minute > 59) return 30;
    year += year < 80 ? 2000 : 1900;
    mt_clock_time_t t = {(uint16_t)year,(uint8_t)month,(uint8_t)day,(uint8_t)(hour % 12 + (marker[0]=='P'?12:0)),(uint8_t)minute,0};
    if (!mt_clock_valid(&t)) return 30;
    *date = fat_time(&t); return 0;
}
static int directory_entry(mt_iec_dos_t *d, const mt_iec_entry_t *e, unsigned stamp,
                           const char *partition_type, unsigned partition)
{
    uint8_t line[64]; memset(line, ' ', sizeof line);
    size_t bytes = e->size >= e->data_offset ? e->size - e->data_offset : 0;
    unsigned blocks = partition_type ? partition : (unsigned)((bytes + 253) / 254);
    if (blocks > 9999) blocks = 9999;
    unsigned chars = blocks >= 1000 ? 4 : blocks >= 100 ? 3 : blocks >= 10 ? 2 : 1;
    line[0] = partition_type || e->metadata.sectors ? 1 : (uint8_t)(bytes % 254 + 2); line[1] = 1;
    line[2] = (uint8_t)blocks; line[3] = (uint8_t)(blocks >> 8);
    unsigned pos = 8 - chars; line[pos++] = '"';
    for (unsigned i=0;e->alias[i] && i<16;++i) line[pos++]=(uint8_t)e->alias[i];
    line[pos] = '"';
    const char *type = e->directory ? "DIR" : e->type=='S' ? "SEQ" : e->type=='U' ? "USR" : e->type=='L' ? "REL" : "PRG";
    memcpy(line + 27 - chars, partition_type ? partition_type : type, 3);
    if (!partition_type && (e->metadata.attributes & 1)) line[30-chars]='<';
    if (!partition_type && (e->metadata.attributes & 2)) line[31-chars]='H';
    unsigned length = 32;
    if (stamp) {
        uint32_t dt = (uint32_t)e->metadata.date << 16 | e->metadata.time;
        unsigned year=1980+(dt>>25),month=(dt>>21)&15,day=(dt>>16)&31,hour=(dt>>11)&31,minute=(dt>>5)&63;
        unsigned hour12=hour%12; if (!hour12) hour12=12;
        char rendered[32]; unsigned start;
        if (stamp == 2) {
            (void)snprintf(rendered,sizeof rendered,"%02u/%02u/%02u   %02u.%02u %cM",month,day,year%100,hour12,minute,hour>=12?'P':'A');
            start=33-chars; length=64;
        } else {
            line[28-chars]=' ';
            (void)snprintf(rendered,sizeof rendered,"%02u/%02u %02u.%02u %c",month,day,hour12,minute,hour>=12?'P':'A');
            start=29-chars; length=42;
        }
        size_t n=strlen(rendered); memcpy(line+start,rendered,n);
        memset(line+start+n,1,length-1-start-n);
    }
    line[length-1]=0;
    if (d->directory_size + length > sizeof d->directory) return 71;
    memcpy(d->directory+d->directory_size,line,length);d->directory_size+=length;return 0;
}
static int directory_open(mt_iec_dos_t *d, unsigned ch, const char *pattern)
{
    if (d->directory_owner >= 0)
        return 70;
    bool partitions = !strncmp(pattern, "=P", 2);
    unsigned stamp = !strncmp(pattern, "=T", 2) ? 1 : 0;
    if (stamp) pattern += 2;
    char path[MT_IEC_PATH], filter[MT_IEC_NAME];
    int rc = 0;
    if (partitions) {
        pattern += 2;
        if (*pattern == ':') ++pattern;
        (void)snprintf(filter, sizeof filter, "%s", *pattern ? pattern : "*");
        strcpy(path, d->root);
    } else {
        rc = directory_target(d, pattern, path, filter);
        if (!rc) rc = listing(d, path);
        if (rc) return rc;
    }
    char *option = strchr(filter, '=');
    unsigned types = 0;
    bool show_hidden = false;
    uint32_t minimum = 0, maximum = 0;
    if (option) {
        *option++ = 0;
        while (*option) {
            char *next = strchr(option, ','); if (next) *next++ = 0;
            if (partitions) {
                const char *at = strchr("N478C", *option);
                if (!at || option[1]) return 30;
                types |= 1U << (unsigned)(at - "N478C");
            } else if (*option == '<' || *option == '>') {
                rc = directory_date(option+1,*option=='<'?&maximum:&minimum); if (rc) return rc;
            } else {
                if (option[1]) return 30;
                const char *at = strchr("PSURDB", *option);
                if (at) types |= 1U << (unsigned)(at-"PSURDB" > 4 ? 4 : at-"PSURDB");
                else if (*option=='L') stamp=2;
                else if (*option=='N') stamp=0;
                else if (*option=='H') show_hidden=true;
                else return 30;
            }
            if (!next) break;
            option=next;
        }
    }
    if (!*filter) strcpy(filter, "*");
    d->directory[0] = 1;
    d->directory[1] = 4;
    d->directory_size = 2;
    const char *base = strrchr(path, '/');
    base = base && base[1] ? base + 1 : "MULTITINI";
    if (partitions) base = "PARTITIONS";
    else if (!strcmp(path, partition_root(d)) && d->active_partition)
        base = d->partitions[d->active_partition].name;
    mt_iec_metadata_t header = {0};
    if (!partitions && d->fs.metadata) {
        rc=d->fs.metadata(d->fs.context,path,&header);
        if(rc)return rc;
        if(header.label[0])base=header.label;
    }
    char line[80];
    (void)snprintf(line, sizeof line, "\022\"%-16.16s\" %s", base,header.id[0]?header.id:"09 2A");
    unsigned partition_count = 0;
    if (partitions) for (unsigned i=1;i<256;++i) if (d->partitions[i].root[0]) ++partition_count;
    rc = dir_line(d, partitions ? partition_count : d->active_partition, line);
    for (size_t i = 0; i < (partitions ? 256U : d->entry_count) && !rc; ++i)
    {
        if (partitions) {
            const mt_iec_partition_t *p = &d->partitions[i];
            unsigned type = suffix(p->root, ".d64") ? 1 : suffix(p->root, ".d71") ? 2 : suffix(p->root, ".d81") ? 3 : 0;
            if (!p->root[0] || !match(filter, p->name) || (types && !(types & (1U << type)))) continue;
            mt_iec_entry_t partition_entry = {0};
            (void)snprintf(partition_entry.alias,sizeof partition_entry.alias,"%.16s",p->name);
            rc = directory_entry(d,&partition_entry,0,type==1?"41 ":type==2?"71 ":type==3?"81 ":"NAT",(unsigned)i);
            continue;
        }
        mt_iec_entry_t *e = &d->entries[i];
        uint32_t dt = (uint32_t)e->metadata.date << 16 | e->metadata.time;
        if ((!show_hidden && (e->metadata.attributes & 2)) || (minimum && dt < minimum) || (maximum && dt > maximum)) continue;
        const char *type_at = strchr("PSULD", e->type);
        if (!match(filter, e->alias) || (types && (!type_at || !(types & (1U << (unsigned)(type_at - "PSULD"))))))
            continue;
        rc = directory_entry(d,e,stamp,NULL,0);
    }
    if (!rc && !partitions)
        rc = dir_line(d, 0, "BLOCKS FREE.");
    if (rc)
        return rc;
    if (d->directory_size + 2 > MT_IEC_DIRECTORY)
        return 71;
    d->directory[d->directory_size++] = 0;
    d->directory[d->directory_size++] = 0;
    d->directory_owner = (int)ch;
    d->channels[ch].kind = 3;
    d->channels[ch].size = d->directory_size;
    return 0;
}
static int relative_open(mt_iec_dos_t *d, unsigned ch, const char *parent, const char *leaf,
                         const mt_iec_entry_t *found, unsigned requested)
{
    mt_iec_channel_t *c = &d->channels[ch];
    if (!d->fs.seek) return 74;
    if (found && (found->directory || found->type != 'L')) return 64;
    if (!found && !requested) return 62;
    char actual[MT_IEC_NAME];
    (void)snprintf(actual, sizeof actual, "%s", found ? found->name : leaf);
    if (!found && !typed_extension(actual)) {
        if (strlen(actual) + 4 >= sizeof actual) return 33;
        strcat(actual, ".rel");
    }
    memset(c, 0, sizeof *c);
    if (!join(c->target, parent, actual)) return 33;
    if (in_use(d, c->target)) return 70;
    c->writable = !protected_path(d, c->target) && (!found || !(found->metadata.attributes & 1));
    if (!found && !c->writable) return 26;
    c->kind = 5;
    c->replace = found != NULL;
    int rc = 0;
    if (found) rc = d->fs.open(d->fs.context, c->target, "r", &c->auxiliary, &c->size);
    if (!rc && c->size > MT_IEC_MAX_FILE) rc = 72;
    if (!rc && c->writable) {
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            char temp[40];
            (void)snprintf(temp, sizeof temp, ".mtiec-%08X-%02u.tmp", (unsigned)++d->serial, ch);
            if (!join(c->temporary, parent, temp)) { rc = 33; break; }
            if (protected_path(d, c->temporary)) { rc = 26; break; }
            size_t unused = 0;
            rc = d->fs.open(d->fs.context, c->temporary, "x+", &c->handle, &unused);
            if (rc != 63 || c->handle) break;
        }
        size_t copied = 0;
        while (!rc && copied < c->size) {
            size_t n = c->size - copied, got = 0, wrote = 0;
            if (n > sizeof c->buffer) n = sizeof c->buffer;
            rc = d->fs.read(d->fs.context, c->auxiliary, c->buffer, n, &got);
            if (!rc && got != n) rc = 23;
            if (!rc) rc = d->fs.write(d->fs.context, c->handle, c->buffer, got, &wrote);
            if (!rc && wrote != got) rc = 25;
            copied += got;
        }
    } else if (!rc) { c->handle = c->auxiliary; c->auxiliary = NULL; }
    if (c->auxiliary) {
        int close = d->fs.close(d->fs.context, &c->auxiliary);
        if (!rc) rc = close;
    }
    if (!rc && c->size) {
        uint8_t h[2]; size_t got = 0;
        if (found && found->data_offset) {
            c->data_offset = found->data_offset; c->record_size = found->record_size;
        } else {
            rc = d->fs.seek(d->fs.context, c->handle, 0);
            if (!rc) rc = d->fs.read(d->fs.context, c->handle, h, 2, &got);
            if (!rc && !got) rc = 50;
            if (!rc) {
                c->record_size = h[0]; c->data_offset = 2;
                if ((got > 1 && h[1]) || (h[0] > 1 && c->size % h[0] != 2U % h[0] &&
                                         c->size % h[0] == 1U % h[0])) c->data_offset = 1;
            }
        }
        if (!rc && (!c->record_size || (requested && requested != c->record_size))) rc = 50;
    } else if (!rc) {
        uint8_t header[2] = {(uint8_t)requested, 0}; size_t wrote = 0;
        c->data_offset = 2; c->record_size = (uint8_t)requested;
        if (!requested) rc = 50;
        if (!rc) rc = d->fs.write(d->fs.context, c->handle, header, 2, &wrote);
        if (!rc && wrote != 2) rc = 25;
        c->size = 2;
    }
    c->record_offset = c->data_offset;
    if (!rc) rc = record_load(d, c, 0);
    if (rc) { c->error = rc; (void)close_channel(d, ch, true); }
    return rc;
}
static int open_channel(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size)
{
    if (ch > 15 || (!name && size))
        return result(d, 70);
    if (ch == 15)
        return size ? mt_iec_dos_command(d, name, size) : 0;
    if (d->channels[ch].kind)
        return result(d, 70);
    unsigned record_size = 0;
    if (size >= 4 && name[size - 4] == ',' && fold(name[size - 3]) == 'L' && name[size - 2] == ',') {
        record_size = name[size - 1]; size -= 2;
        if (!record_size) return result(d, 50);
    }
    char text[MT_IEC_NAME];
    int rc = text_name(name, size, text);
    if (rc)
        return result(d, rc);
    if (text[0] == '$')
        return result(d, directory_open(d, ch, text + 1));
    if (text[0] == '#') {
        if (text[1] == '#' && text[2] >= '2' && text[2] <= '9' && !text[3]) return result(d, 70);
        mt_iec_channel_t *c = &d->channels[ch];
        memset(c, 0, sizeof *c); c->kind = 4; c->size = 256;
        c->position = text[1] == '#' && text[2] == '1' && !text[3] ? 0 : 1;
        strcpy(c->target, d->cwd);
        return result(d, 0);
    }
    char *p = text;
    bool replace = false;
    if (*p == '@')
    {
        replace = true;
        ++p;
    }
    if (p[0] == '0' && p[1] == ':')
        p += 2;
    else if (*p == ':')
        ++p;
    char mode = ch == 1 ? 'W' : 'R', type = 0;
    char *opt = strchr(p, ',');
    if (opt)
        *opt++ = 0;
    while (opt && *opt)
    {
        char *next = strchr(opt, ',');
        if (next)
            *next++ = 0;
        if (strlen(opt) != 1)
            return result(d, 30);
        if (*opt == 'P' || *opt == 'S' || *opt == 'U' || *opt == 'L')
            type = *opt;
        else if (*opt == 'R' || *opt == 'W' || *opt == 'A' || *opt == 'M')
            mode = *opt == 'M' ? 'R' : *opt;
        else
            return result(d, 30);
        opt = next;
    }
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME];
    rc = resolve(d, p, parent, leaf);
    if (rc || !*leaf || !strcmp(leaf, ".") || !strcmp(leaf, ".."))
        return result(d, rc ? rc : 33);
    mt_iec_entry_t e;
    rc = lookup(d, parent, leaf, mode == 'R', 0, &e);
    bool exists = !rc;
    if (rc && rc != 62)
        return result(d, rc);
    if (type == 'L' || (exists && e.type == 'L' && !type))
        return result(d, relative_open(d, ch, parent, leaf, exists ? &e : NULL, record_size));
    if (!type && mode != 'R') type = ch < 2 ? 'P' : 'S';
    mt_iec_channel_t *c = &d->channels[ch];
    memset(c, 0, sizeof *c);
    if (mode == 'R')
    {
        if (!exists)
            return result(d, 62);
        if (e.directory || (type && e.type != type))
            return result(d, 64);
        if (!join(c->target, parent, e.name))
            return result(d, 33);
        rc = d->fs.open(d->fs.context, c->target, "r", &c->handle, &c->size);
        if (c->handle)
            c->kind = 1;
        if (!rc && c->size > MT_IEC_MAX_FILE)
        {
            (void)d->fs.close(d->fs.context, &c->handle);
            rc = 72;
        }
        if (rc)
        {
            if (c->handle)
                c->error = rc;
            return result(d, rc);
        }
        c->kind = 1;
        if (e.data_offset) {
            uint8_t header[26]; size_t got = 0;
            rc = d->fs.read(d->fs.context, c->handle, header, e.data_offset, &got);
            if (rc || got != e.data_offset) { c->error = rc ? rc : 23; return result(d, c->error); }
            c->size -= e.data_offset;
            c->data_offset = e.data_offset;
        }
        return result(d, 0);
    }
    if (!component(leaf, false) || strchr(leaf, '='))
        return result(d, 33);
    if (exists && e.directory)
        return result(d, 64);
    if (exists && (e.metadata.attributes & 1)) return result(d,26);
    if (exists && mode != 'A' && !replace)
        return result(d, 63);
    if (exists && type && type != e.type)
        return result(d, 64);
    char actual[MT_IEC_NAME];
    strcpy(actual, exists ? e.name : leaf);
    if (!exists && !typed_extension(actual) && !raw_extension(actual))
    {
        size_t n = strlen(actual);
        if (n + 4 >= sizeof actual)
            return result(d, 33);
        strcat(actual, type == 'S' ? ".seq" : type == 'U' ? ".usr" : ".prg");
    }
    if (!join(c->target, parent, actual))
        return result(d, 33);
    if (protected_path(d, c->target))
        return result(d, 26);
    if (in_use(d, c->target))
        return result(d, 70);
    c->replace = replace || mode == 'A';
    for (unsigned attempt = 0; attempt < 32; ++attempt)
    {
        char temp[40];
        (void)snprintf(temp, sizeof temp, ".mtiec-%08X-%02u.tmp", (unsigned)++d->serial, ch);
        if (!join(c->temporary, parent, temp))
            return result(d, 33);
        if (protected_path(d, c->temporary))
            return result(d, 26);
        size_t unused = 0;
        rc = d->fs.open(d->fs.context, c->temporary, "x", &c->handle, &unused);
        if (rc != 63 || c->handle)
            break;
    }
    if (c->handle) c->kind = 2;
    if (rc) { c->error = rc; return result(d, rc); }
    c->kind = 2;
    if (exists && e.data_offset && mode != 'A') {
        size_t total = 0, got = 0, wrote = 0;
        rc = d->fs.open(d->fs.context, c->target, "r", &c->auxiliary, &total);
        if (!rc) rc = d->fs.read(d->fs.context, c->auxiliary, c->buffer, e.data_offset, &got);
        if (!rc && got != e.data_offset) rc = 23;
        if (!rc) rc = d->fs.write(d->fs.context, c->handle, c->buffer, got, &wrote);
        if (!rc && wrote != got) rc = 25;
        if (c->auxiliary) {
            int close = d->fs.close(d->fs.context, &c->auxiliary);
            if (!rc) rc = close;
        }
        if (rc) { c->error = rc; (void)close_channel(d, ch, true); return result(d, rc); }
        c->size = got;
    }
    if (mode == 'A' && exists)
    {
        size_t total = 0;
        rc = d->fs.open(d->fs.context, c->target, "r", &c->auxiliary, &total);
        if (!rc && total > MT_IEC_MAX_FILE)
            rc = 72;
        while (!rc && c->size < total)
        {
            size_t got = 0, wrote = 0;
            rc = d->fs.read(d->fs.context, c->auxiliary, c->buffer, MT_IEC_BUFFER, &got);
            if (!rc && !got)
                rc = 23;
            if (!rc)
                rc = d->fs.write(d->fs.context, c->handle, c->buffer, got, &wrote);
            if (!rc && wrote != got)
                rc = 25;
            c->size += wrote;
        }
        if (c->auxiliary)
        {
            int r = d->fs.close(d->fs.context, &c->auxiliary);
            if (!rc)
                rc = r;
        }
        if (rc)
        {
            c->error = rc;
            (void)close_channel(d, ch, true);
            return result(d, rc);
        }
    }
    c->position = c->size;
    return result(d, 0);
}
int mt_iec_dos_open(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size)
{
    if (ch > 15 || (!name && size)) return result(d,70);
    if (ch == 15) return size ? mt_iec_dos_command(d, name, size) : 0;
    uint8_t translated[MT_IEC_NAME]; char cwd[MT_IEC_PATH]; unsigned part; size_t n;
    int rc = select_name(d, name, size, translated, &n, cwd, &part);
    if (!rc) rc = open_channel(d, ch, translated, n);
    else result(d, rc);
    restore_name(d, cwd, part);
    /* KERNAL LOAD still issues TALK after an OPEN error. Keep that channel's
     * failure so its empty response does not replace FILE NOT FOUND with
     * NO CHANNEL. The same rule preserves FILE EXISTS during failed SAVE. */
    if (rc && ch < 15 && !d->channels[ch].kind)
        d->channels[ch].error = rc;
    if (!rc && ch < 15)
        d->channels[ch].error = 0;
    return rc;
}
int mt_iec_dos_write(mt_iec_dos_t *d, unsigned ch, const uint8_t *data, size_t size, bool eoi)
{
    if (ch >= 15)
        return result(d, 70);
    mt_iec_channel_t *c = &d->channels[ch];
    if (c->kind == 4) {
        if (c->position > 256 || size > 256 - c->position) return result(d, 51);
        memcpy(c->buffer + c->position, data, size); c->position += size;
        return 0;
    }
    if (c->kind == 5) {
        if (c->error) return result(d, c->error);
        if (!c->writable || protected_path(d, c->target)) return result(d, 26);
        if (c->cursor > c->record_size || size > c->record_size - c->cursor) return result(d, 51);
        if (size) {
            memcpy(c->buffer + c->cursor, data, size); c->cursor += size;
            c->record_dirty = true; c->complete = false;
        }
        if (eoi && c->record_dirty) {
            int rc = record_flush(d, c);
            if (!rc) { c->record_offset += c->record_size; rc = record_load(d, c, 0); }
            if (rc) return result(d, rc);
        }
        return 0;
    }
    if (c->kind != 2)
        return result(d, c->error ? c->error : 70);
    if (c->error)
        return result(d, c->error);
    if (protected_path(d, c->target)) return result(d, 26);
    c->complete = false;
    if (size > MT_IEC_MAX_FILE - c->position)
    {
        c->error = 72;
        return result(d, 72);
    }
    for (size_t i = 0; i < size; ++i)
    {
        c->buffer[c->used++] = data[i];
        ++c->position;
        if (c->size < c->position) c->size = c->position;
        if (c->used == MT_IEC_BUFFER)
        {
            int rc = flush(d, c);
            if (rc)
                return result(d, rc);
        }
    }
    if (eoi)
    {
        int rc = flush(d, c);
        if (rc)
            return result(d, rc);
        c->complete = true;
        /* Secondary address 1 is KERNAL SAVE: EOI completes the file before
         * any following status request or transport shutdown is processed. */
        if (ch == 1)
            return mt_iec_dos_close(d, ch);
    }
    return 0;
}
void mt_iec_dos_begin_talk(mt_iec_dos_t *d, unsigned ch)
{
    /* GET# may UNTALK after every byte. Preserve each channel's cursor. */
    (void)d;
    (void)ch;
}
int mt_iec_dos_peek_many(mt_iec_dos_t *d, unsigned ch, uint8_t *out, size_t capacity, size_t *size,
                         bool *eof)
{
    *size = 0;
    *eof = false;
    if (capacity > MT_IEC_BUFFER || (!out && capacity))
        return 70;
    if (ch == 15) {
        size_t n = d->status_size - d->status_cursor;
        *size = n < capacity ? n : capacity;
        if (*size)
            memcpy(out, d->status + d->status_cursor, *size);
        *eof = *size == n;
        return 0;
    }
    if (ch >= 15)
        return result(d, 70);
    mt_iec_channel_t *c = &d->channels[ch];
    if (c->kind == 4 || c->kind == 5) {
        if (c->error) return result(d, c->error);
        size_t cursor = c->kind == 4 ? c->position : c->cursor;
        size_t end = c->kind == 4 ? c->size : c->used;
        size_t n = cursor < end ? end - cursor : 0;
        *size = n < capacity ? n : capacity;
        if (*size) memcpy(out, c->buffer + cursor, *size);
        *eof = *size == n;
        return 0;
    }
    if (c->kind != 1 && c->kind != 3)
        return result(d, c->error ? c->error : 70);
    if (c->error)
        return result(d, c->error);
    size_t remaining = c->size - c->position;
    size_t need = remaining < capacity ? remaining : capacity;
    if (c->kind == 3) {
        if (need)
            memcpy(out, d->directory + c->position, need);
    } else {
        size_t buffered = c->used - c->cursor;
        if (buffered < need) {
            memmove(c->buffer, c->buffer + c->cursor, buffered);
            c->used = buffered;
            c->cursor = 0;
            size_t fill = remaining < MT_IEC_BUFFER ? remaining : MT_IEC_BUFFER;
            size_t got = 0;
            int rc =
                d->fs.read(d->fs.context, c->handle, c->buffer + buffered, fill - buffered, &got);
            c->used += got;
            if (rc || got != fill - buffered) {
                c->error = rc ? rc : 23;
                return result(d, c->error);
            }
        }
        if (need)
            memcpy(out, c->buffer + c->cursor, need);
    }
    *size = need;
    *eof = need == remaining;
    return 0;
}
int mt_iec_dos_consume_many(mt_iec_dos_t *d, unsigned ch, size_t count)
{
    if (ch == 15) {
        size_t n = d->status_size - d->status_cursor;
        if (count > n)
            return 70;
        d->status_cursor += count;
        if (count && d->status_cursor == d->status_size)
            mt_iec_dos_error(d, 0);
        return 0;
    }
    if (ch >= 15)
        return 70;
    mt_iec_channel_t *c = &d->channels[ch];
    if (c->kind == 4) {
        if (c->position > c->size || count > c->size - c->position) return 70;
        c->position += count; return 0;
    }
    if (c->kind == 5) {
        if (c->cursor > c->used || count > c->used - c->cursor) return 70;
        c->cursor += count;
        if (count && c->cursor == c->used) {
            if (c->record_missing) { c->cursor = 0; mt_iec_dos_error(d, 50); return 0; }
            c->record_offset += c->record_size;
            return record_load(d, c, 0);
        }
        return 0;
    }
    if ((c->kind != 1 && c->kind != 3) || count > c->size - c->position ||
        (c->kind == 1 && count > c->used - c->cursor))
        return 70;
    c->position += count;
    if (c->kind == 1)
        c->cursor += count;
    return 0;
}
int mt_iec_dos_abort_channel(mt_iec_dos_t *d, unsigned ch)
{
    return ch < 15 ? close_channel(d, ch, true) : 0;
}
static int resolve_name(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size,
                        char path[MT_IEC_PATH])
{
    if (size >= 4 && name[size - 4] == ',' && fold(name[size - 3]) == 'L' && name[size - 2] == ',') size -= 2;
    char text[MT_IEC_NAME];
    int rc = text_name(name, size, text);
    if (rc || ch > 15)
        return rc ? rc : 70;
    if (text[0] == '$') {
        if (!strncmp(text, "$=P", 3)) { strcpy(path, "/partitions"); return 0; }
        char pattern[MT_IEC_NAME];
        return directory_target(d, text + 1, path, pattern);
    }
    if (text[0] == '#') {
        strcpy(path, "/buffer");
        return 0;
    }
    char *p = text;
    if (*p == '@')
        ++p;
    if (p[0] == '0' && p[1] == ':')
        p += 2;
    else if (*p == ':')
        ++p;
    char mode = ch == 1 ? 'W' : 'R', type = 0;
    char *opt = strchr(p, ',');
    if (opt)
        *opt++ = 0;
    while (opt && *opt) {
        char *next = strchr(opt, ',');
        if (next)
            *next++ = 0;
        if (strlen(opt) != 1)
            return 30;
        if (*opt == 'P' || *opt == 'S' || *opt == 'U' || *opt == 'L')
            type = *opt;
        else if (*opt == 'R' || *opt == 'W' || *opt == 'A' || *opt == 'M')
            mode = *opt == 'M' ? 'R' : *opt;
        else
            return 30;
        opt = next;
    }
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME];
    rc = resolve(d, p, parent, leaf);
    if (rc || !*leaf || !strcmp(leaf, ".") || !strcmp(leaf, ".."))
        return rc ? rc : 33;
    mt_iec_entry_t e;
    rc = lookup(d, parent, leaf, mode == 'R', 0, &e);
    if (!rc)
        return join(path, parent, e.name) ? 0 : 33;
    if (rc != 62 || !component(leaf, false))
        return rc;
    if (!type && mode != 'R') type = ch < 2 ? 'P' : 'S';
    if (!typed_extension(leaf)) {
        size_t n = strlen(leaf);
        if (n + 4 >= sizeof leaf)
            return 33;
        strcat(leaf, type == 'S' ? ".seq" : type == 'U' ? ".usr" : type == 'L' ? ".rel" : ".prg");
    }
    return join(path, parent, leaf) ? 0 : 33;
}
int mt_iec_dos_resolve_name(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size,
                            char path[MT_IEC_PATH])
{
    uint8_t translated[MT_IEC_NAME]; char cwd[MT_IEC_PATH]; unsigned part; size_t n;
    int rc = select_name(d, name, size, translated, &n, cwd, &part);
    if (!rc) rc = resolve_name(d, ch, translated, n, path);
    restore_name(d, cwd, part);
    return rc;
}
int mt_iec_dos_iec_name(mt_iec_dos_t *d, const char *path, char alias[17], uint8_t *type)
{
    char parent[MT_IEC_PATH];
    const char *name = strrchr(path, '/');
    if (name) {
        size_t n = (size_t)(name - path);
        if (n >= sizeof parent)
            return 33;
        memcpy(parent, path, n);
        parent[n] = 0;
        ++name;
        size_t root = strlen(d->root);
        if (strlen(parent) < root || strncmp(parent, d->root, root) ||
            (parent[root] && parent[root] != '/'))
            return 26;
    } else {
        name = path;
        strcpy(parent, d->cwd);
    }
    if (!component(name, false))
        return 33;
    int rc = listing(d, parent);
    if (rc)
        return rc;
    for (size_t i = 0; i < d->entry_count; ++i)
        if (!compare(name, d->entries[i].name)) {
            strcpy(alias, d->entries[i].alias);
            *type = d->entries[i].directory ? 5 : d->entries[i].type == 'S' ? 2 :
                    d->entries[i].type == 'U' ? 3 : d->entries[i].type == 'L' ? 4 : 1;
            return 0;
        }
    /* A hypothetical name uses the same deterministic alias function as a
     * real entry; compute it in the existing sorted directory context. */
    if (d->entry_count == MT_IEC_ENTRIES)
        return 71;
    size_t n = strlen(name);
    if (typed_extension(name))
        n -= 4;
    bool plain = n > 0 && n <= 16;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; name[i]; ++i)
        hash = (hash ^ (uint8_t)name[i]) * 16777619U;
    for (size_t i = 0; i < n; ++i)
        if (!alias_char(fold((uint8_t)name[i])))
            plain = false;
    if (plain) {
        for (size_t i = 0; i < n; ++i)
            alias[i] = (char)fold((uint8_t)name[i]);
        alias[n] = 0;
        for (size_t i = 0; i < d->entry_count; ++i)
            if (!compare(alias, d->entries[i].alias))
                plain = false;
    }
    if (!plain) {
        char prefix[10];
        for (size_t i = 0; i < 9; ++i)
            prefix[i] =
                i < n && alias_char(fold((uint8_t)name[i])) ? (char)fold((uint8_t)name[i]) : '_';
        prefix[9] = 0;
        for (unsigned salt = 0;; ++salt) {
            (void)snprintf(alias, 17, "%s~%06X", prefix, (unsigned)((hash + salt) & 0xffffffU));
            bool collision = false;
            for (size_t i = 0; i < d->entry_count; ++i)
                if (!compare(alias, d->entries[i].alias))
                    collision = true;
            if (!collision)
                break;
        }
    }
    *type = suffix(name, ".seq") ? 2 : suffix(name, ".prg") ? 1 : suffix(name, ".usr") ? 3 : suffix(name, ".rel") ? 4 : 0;
    return 0;
}
int mt_iec_dos_peek(mt_iec_dos_t *d, unsigned ch, uint8_t *byte, bool *eoi, bool *available)
{
    *available = false;
    *eoi = false;
    if (ch < 15 && (d->channels[ch].kind == 4 || d->channels[ch].kind == 5)) {
        size_t n = 0;
        int rc = mt_iec_dos_peek_many(d, ch, byte, 1, &n, eoi);
        *available = n != 0;
        return rc;
    }
    if (ch == 15)
    {
        size_t n = d->status_size;
        if (d->status_cursor < n)
        {
            *byte = (uint8_t)d->status[d->status_cursor];
            *eoi = d->status_cursor + 1 == n;
            *available = true;
        }
        return 0;
    }
    if (ch >= 15)
        return result(d, 70);
    mt_iec_channel_t *c = &d->channels[ch];
    if (c->kind != 1 && c->kind != 3)
        return result(d, c->error ? c->error : 70);
    if (c->error)
        return result(d, c->error);
    if (c->position == c->size)
        return 0;
    if (c->kind == 3)
        *byte = d->directory[c->position];
    else
    {
        if (c->cursor == c->used)
        {
            c->cursor = 0;
            c->used = 0;
            size_t need = c->size - c->position;
            if (need > MT_IEC_BUFFER)
                need = MT_IEC_BUFFER;
            int rc = d->fs.read(d->fs.context, c->handle, c->buffer, need, &c->used);
            if (rc || c->used != need)
            {
                c->error = rc ? rc : 23;
                return result(d, c->error);
            }
        }
        *byte = c->buffer[c->cursor];
    }
    *available = true;
    *eoi = c->position + 1 == c->size;
    return 0;
}
void mt_iec_dos_consume(mt_iec_dos_t *d, unsigned ch)
{
    if (ch < 15 && (d->channels[ch].kind == 4 || d->channels[ch].kind == 5)) {
        (void)mt_iec_dos_consume_many(d, ch, 1);
        return;
    }
    if (ch == 15)
    {
        if (d->status_cursor < d->status_size)
            ++d->status_cursor;
        if (d->status_cursor == d->status_size)
            mt_iec_dos_error(d, 0);
        return;
    }
    if (ch < 15)
    {
        mt_iec_channel_t *c = &d->channels[ch];
        if ((c->kind == 1 || c->kind == 3) && c->position < c->size)
        {
            ++c->position;
            if (c->kind == 1)
                ++c->cursor;
        }
    }
}
static int numbers(const uint8_t *p, size_t n, unsigned out[4])
{
    unsigned count = 0; size_t i = 0;
    while (count < 4) {
        while (i < n && (p[i] == ':' || p[i] == ',' || p[i] == ' ' || p[i] == 0x1d)) ++i;
        if (i == n || p[i] < '0' || p[i] > '9') break;
        unsigned value = 0;
        while (i < n && p[i] >= '0' && p[i] <= '9') {
            if (value <= 65535U) value = value * 10U + p[i] - '0';
            ++i;
        }
        out[count++] = value > 65535U ? 65535U : value;
    }
    return (int)count;
}
static int block_command(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    bool user = fold(data[0]) == 'U';
    unsigned op;
    size_t start = 2;
    if (user) op = data[1] == '1' || data[1] == 'A' ? 'R' : 'W';
    else {
        size_t dash = 1;
        while (dash < size && fold(data[dash]) >= 'A' && fold(data[dash]) <= 'Z') ++dash;
        if (dash + 1 >= size || data[dash] != '-') return result(d, 30);
        op = fold(data[dash + 1]); start = dash + 2;
        while (start < size && fold(data[start]) >= 'A' && fold(data[start]) <= 'Z') ++start;
    }
    unsigned p[4] = {0}; int n = numbers(data + start, size - start, p);
    if (op == 'P') {
        if (n < 2 || p[0] > 14 || d->channels[p[0]].kind != 4) return result(d, n < 2 ? 30 : 70);
        unsigned offset = n >= 3 ? p[1] + (p[2] << 8) : p[1] & 255U;
        if (offset > 255) return result(d, 30);
        d->channels[p[0]].position = offset;
        return result(d, 0);
    }
    bool allocate = op == 'A' || op == 'F';
    if ((!allocate && n != 4) || (allocate && n < 3) || !strchr("RWAF", (int)op)) return result(d, 30);
    mt_iec_channel_t *c = NULL; uint8_t unused[256] = {0};
    unsigned track = p[allocate ? 1 : 2], sector = p[allocate ? 2 : 3];
    const char *path = d->cwd;
    if (!allocate) {
        if (p[0] > 14 || d->channels[p[0]].kind != 4) return result(d, 70);
        c = &d->channels[p[0]]; path = c->target;
        if (op == 'W' && !user) c->buffer[0] = (uint8_t)(c->position - 1);
    }
    if (op != 'R' && protected_path(d, path)) return result(d, 26);
    unsigned action = op == 'R' ? 0 : op == 'W' ? 1 : op == 'A' ? 2 : 3;
    int rc = d->fs.sector ? d->fs.sector(d->fs.context, path, action, &track, &sector,
                                        c ? c->buffer : unused) : 74;
    if (!rc && op == 'R') { c->position = user ? 0 : 1; c->size = user ? 256 : (size_t)c->buffer[0] + 1; }
    result(d, rc);
    if (rc == 65 || rc == 66) {
        char *last = strrchr(d->status, ',');
        if (last) {
            char *prior = last;
            while (prior > d->status && prior[-1] != ',') --prior;
            (void)snprintf(prior, (size_t)(d->status + sizeof d->status - prior), "%02u,%02u\r", track, sector);
            d->status_size = strlen(d->status);
        }
    }
    return rc;
}
static int position_command(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    if (size < 3) return result(d, 30);
    unsigned ch = data[1] >= 19 ? data[1] & 15U : data[1];
    if (ch > 14) return result(d, 70);
    mt_iec_channel_t *c = &d->channels[ch];
    size_t position = 0;
    for (size_t i = 2; i < size && i < 6; ++i) position |= (size_t)data[i] << (8U * (i - 2));
    if (c->kind == 4) {
        if (position > 255) return result(d, 30);
        c->position = position; return result(d, 0);
    }
    if (c->kind == 1 && d->fs.seek) {
        if (position > c->size) position = c->size;
        int rc = d->fs.seek(d->fs.context, c->handle, position + c->data_offset);
        if (!rc) { c->position = position; c->cursor = c->used = 0; }
        return result(d, rc);
    }
    if (c->kind == 2 && d->fs.seek) {
        if (protected_path(d, c->target)) return result(d, 26);
        if (position > MT_IEC_MAX_FILE) return result(d, 72);
        int rc = flush(d, c);
        /* Grow through writes so a backend that clamps seeks cannot silently
         * land at the old EOF. Every new byte is initialized. */
        if (!rc && position > c->size) rc = d->fs.seek(d->fs.context, c->handle, c->size);
        memset(c->buffer, 0, sizeof c->buffer);
        while (!rc && position > c->size) {
            size_t n = position - c->size, written = 0;
            if (n > sizeof c->buffer) n = sizeof c->buffer;
            rc = d->fs.write(d->fs.context, c->handle, c->buffer, n, &written);
            if (!rc && written != n) rc = 72;
            c->size += written;
        }
        if (!rc) rc = d->fs.seek(d->fs.context, c->handle, position);
        if (!rc) c->position = position;
        else c->error = rc;
        return result(d, rc);
    }
    if (c->kind != 5) return result(d, 70);
    int rc = record_flush(d, c);
    if (rc) return result(d, rc);
    unsigned record = data[2] | (size > 3 ? (unsigned)data[3] << 8 : 0);
    size_t offset = size > 4 && data[4] ? data[4] - 1U : 0;
    int status = 0;
    if (offset >= c->record_size) { offset = c->record_size - 1U; status = 51; }
    c->record_offset = c->data_offset + (size_t)(record ? record - 1U : 0) * c->record_size;
    rc = record_load(d, c, offset);
    return result(d, rc ? rc : status ? status : c->record_missing ? 50 : 0);
}
static int command_path(mt_iec_dos_t *d, const char *name, char parent[MT_IEC_PATH],
                        char leaf[MT_IEC_NAME], unsigned *partition)
{
    uint8_t translated[MT_IEC_NAME]; char cwd[MT_IEC_PATH]; unsigned part; size_t n;
    int rc = select_name(d, (const uint8_t *)name, strlen(name), translated, &n, cwd, &part);
    if (!rc) rc = resolve(d, (const char *)translated, parent, leaf);
    if (partition) *partition = d->active_partition;
    restore_name(d, cwd, part);
    return rc;
}
typedef struct { char path[MT_IEC_PATH]; mt_iec_entry_t entry; } copy_source_t;
static int copy_command(mt_iec_dos_t *d, char *spec)
{
    if (d->write_protect) return 26;
    char *sources = strchr(spec, '=');
    if (!sources || !sources[1]) return 30;
    *sources++ = 0;
    unsigned count = 1;
    for (const char *p = sources; *p; ++p) if (*p == ',') ++count;
    copy_source_t *items = calloc(count, sizeof *items);
    if (!items) return 70;
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME];
    int rc = 0; unsigned record = 0;
    char *source = sources;
    for (unsigned i = 0; i < count && !rc; ++i) {
        char *next = strchr(source, ','); if (next) *next++ = 0;
        rc = command_path(d, source, parent, leaf, NULL);
        if (!rc) rc = lookup(d, parent, leaf, true, 0, &items[i].entry);
        mt_iec_entry_t *e = &items[i].entry;
        if (!rc && (e->directory || !join(items[i].path, parent, e->name))) rc = 64;
        if (!rc && i && ((e->type == 'L') != (items[0].entry.type == 'L'))) rc = 64;
        if (!rc && e->type == 'L') {
            if (!e->data_offset) {
                size_t size = 0, got = 0; uint8_t header[2];
                if (d->probe_handle) rc = 70;
                if (!rc) rc = d->fs.open(d->fs.context, items[i].path, "r", &d->probe_handle, &size);
                if (!rc) rc = d->fs.read(d->fs.context, d->probe_handle, header, 2, &got);
                if (d->probe_handle) { int close = d->fs.close(d->fs.context, &d->probe_handle); if (!rc) rc = close; }
                if (!rc && !got) rc = 50;
                if (!rc) {
                    e->record_size = header[0]; e->data_offset = 2;
                    if ((got > 1 && header[1]) || (header[0] > 1 && size % header[0] == 1U % header[0])) e->data_offset = 1;
                }
            }
            if (!rc && (!e->record_size || (i && record != e->record_size))) rc = 64;
            record = e->record_size;
        }
        source = next;
    }
    if (!rc) rc = command_path(d, spec, parent, leaf, NULL);
    if (!rc && !component(leaf, false)) rc = 33;
    mt_iec_entry_t existing;
    if (!rc) { int found = lookup(d, parent, leaf, false, 0, &existing); rc = found == 62 ? 0 : found ? found : 63; }
    unsigned ch = 0; while (ch < 15 && d->channels[ch].kind) ++ch;
    if (!rc && ch == 15) rc = 70;
    mt_iec_channel_t *c = ch < 15 ? &d->channels[ch] : NULL;
    if (!rc) {
        char type = items[0].entry.type;
        if (!typed_extension(leaf)) {
            if (strlen(leaf) + 4 >= sizeof leaf) rc = 33;
            else strcat(leaf, type == 'S' ? ".seq" : type == 'U' ? ".usr" : type == 'L' ? ".rel" : ".prg");
        }
        if (!rc && !join(c->target, parent, leaf)) rc = 33;
        if (!rc && protected_path(d, c->target)) rc = 26;
        if (!rc) {
            c->kind = 2;
            for (unsigned attempt = 0; attempt < 32; ++attempt) {
                char temp[40]; size_t size = 0;
                (void)snprintf(temp, sizeof temp, ".mtiec-%08X-%02u.tmp", (unsigned)++d->serial, ch);
                if (!join(c->temporary, parent, temp)) { rc = 33; break; }
                rc = d->fs.open(d->fs.context, c->temporary, "x", &c->handle, &size);
                if (rc != 63 || c->handle) break;
            }
        }
        if (!rc && record) {
            uint8_t header[2] = {(uint8_t)record, 0}; size_t written = 0;
            rc = d->fs.write(d->fs.context, c->handle, header, 2, &written);
            if (!rc && written != 2) rc = 25;
            c->size = 2;
        }
        for (unsigned i = 0; i < count && !rc; ++i) {
            size_t size = 0, offset = items[i].entry.data_offset;
            rc = d->fs.open(d->fs.context, items[i].path, "r", &c->auxiliary, &size);
            if (!rc && (offset > size || size - offset > MT_IEC_MAX_FILE - c->size)) rc = 72;
            if (!rc && offset) {
                size_t got = 0;
                rc = d->fs.read(d->fs.context, c->auxiliary, c->buffer, offset, &got);
                if (!rc && got != offset) rc = 23;
            }
            while (!rc && offset < size) {
                size_t n = size - offset, got = 0, written = 0;
                if (n > sizeof c->buffer) n = sizeof c->buffer;
                rc = d->fs.read(d->fs.context, c->auxiliary, c->buffer, n, &got);
                if (!rc && got != n) rc = 23;
                if (!rc) rc = d->fs.write(d->fs.context, c->handle, c->buffer, n, &written);
                if (!rc && written != n) rc = 25;
                offset += n; c->size += written;
            }
            if (c->auxiliary) { int close = d->fs.close(d->fs.context, &c->auxiliary); if (!rc) rc = close; }
        }
        c->complete = !rc;
        if (c->kind) { c->error = rc; int close = close_channel(d, ch, rc != 0); if (!rc) rc = close; }
    }
    free(items);
    return rc;
}
static int attributes_command(mt_iec_dos_t *d, char *names, unsigned attributes, unsigned mask, bool toggle)
{
    if (d->write_protect) return 26;
    if (!d->fs.attributes) return 30;
    unsigned changed = 0; int rc = 0;
    while (*names && !rc) {
        char *next = strchr(names, ','); if (next) *next++ = 0;
        char parent[MT_IEC_PATH], leaf[MT_IEC_NAME], path[MT_IEC_PATH];
        rc = command_path(d, names, parent, leaf, NULL);
        if (!rc && !strcmp(leaf, "$") && mask == 1) {
            rc = d->fs.attributes(d->fs.context, parent, (uint8_t)attributes, (uint8_t)(mask | 0x80U));
            if (!rc) ++changed;
        } else if (!rc) {
            rc = listing(d, parent);
            for (size_t i = 0; i < d->entry_count && !rc; ++i) {
                mt_iec_entry_t *e = &d->entries[i];
                if (!match(leaf, e->alias) && compare(leaf, e->name)) continue;
                if (!join(path, parent, e->name)) { rc = 33; break; }
                if (protected_path(d, path) || in_use(d, path)) { rc = 26; break; }
                unsigned value = toggle ? e->metadata.attributes ^ mask : attributes;
                rc = d->fs.attributes(d->fs.context, path, (uint8_t)value, (uint8_t)mask);
                if (!rc) ++changed;
            }
        }
        if (!next) break;
        names = next;
    }
    return rc ? rc : changed ? 0 : 62;
}
static int label_command(mt_iec_dos_t *d, char *spec, bool format)
{
    if (d->write_protect) return 26;
    char *id = strchr(spec, ','); if (id) *id++ = 0; else id = "";
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME]; unsigned partition;
    int rc = command_path(d, spec, parent, leaf, &partition);
    if (rc || !*leaf || !component(leaf, false)) return rc ? rc : 33;
    if (protected_path(d, parent)) return 26;
    if (format) return d->fs.format ? d->fs.format(d->fs.context, parent, leaf, id) : 30;
    if (d->fs.header) {
        rc = d->fs.header(d->fs.context, parent, leaf, id);
        if (rc != 30) return rc;
    }
    if (partition && !compare(parent, d->partitions[partition].root)) {
        (void)snprintf(d->partitions[partition].name, sizeof d->partitions[partition].name, "%.16s", leaf);
        return 0;
    }
    char destination[MT_IEC_PATH], ancestor[MT_IEC_PATH];
    strcpy(ancestor, parent); char *end = strrchr(ancestor, '/');
    if (!end || end < ancestor + strlen(d->root)) return 26;
    *end = 0;
    if (!join(destination, ancestor, leaf)) return 33;
    if (!compare(destination, parent)) return 0;
    return move_directory(d, parent, destination);
}
static unsigned clock_month_days(unsigned year, unsigned month)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (unsigned)days[month - 1] + (unsigned)(month == 2 && !(year % 4) && (year % 100 || !(year % 400)));
}
static int64_t clock_seconds(const mt_clock_time_t *t)
{
    int64_t days = 0;
    for (unsigned y = 1900; y < t->year; ++y) days += 365 + (!(y % 4) && (y % 100 || !(y % 400)));
    for (unsigned m = 1; m < t->month; ++m) days += clock_month_days(t->year, m);
    return ((days + t->day - 1) * 24 + t->hour) * 3600 + t->minute * 60 + t->second;
}
static void clock_from_seconds(int64_t seconds, mt_clock_time_t *t)
{
    if (seconds < 0) seconds = 0;
    int64_t days = seconds / 86400;
    t->second = (uint8_t)(seconds % 60); t->minute = (uint8_t)(seconds / 60 % 60);
    t->hour = (uint8_t)(seconds / 3600 % 24); t->year = 1900; t->month = 1;
    for (;;) {
        unsigned length = 365U + (unsigned)(!(t->year % 4) && (t->year % 100 || !(t->year % 400)));
        if (days < length || t->year == 9999) break;
        days -= length; ++t->year;
    }
    while (t->month < 12 && days >= clock_month_days(t->year, t->month)) {
        days -= clock_month_days(t->year, t->month); ++t->month;
    }
    t->day = (uint8_t)(days + 1);
}
static int digits(const uint8_t *p, unsigned n)
{
    int value = 0;
    for (unsigned i = 0; i < n; ++i) { if (p[i] < '0' || p[i] > '9') return -1; value = value * 10 + p[i] - '0'; }
    return value;
}
static int unbcd(unsigned n) { return (n & 15) < 10 && (n >> 4) < 10 ? (int)((n >> 4) * 10 + (n & 15)) : -1; }
static unsigned bcd(unsigned n) { return (n / 10 << 4) | (n % 10); }
static int clock_command(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    static const char *const weekdays[] = {"SUN.","MON.","TUES","WED.","THUR","FRI.","SAT."};
    static const char *const shortdays[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    if (size < 4 || data[1] != '-') return result(d, 30);
    mt_clock_time_t t = {2010,1,1,0,0,0};
    if (d->fs.clock) (void)d->fs.clock(d->fs.context, &t);
    if (!mt_clock_valid(&t)) t = (mt_clock_time_t){2010,1,1,0,0,0};
    int64_t now = clock_seconds(&t);
    if (data[2] == 'W') {
        const uint8_t *p = data + 4;
        int y = -1, m = -1, day = -1, h = -1, min = -1, sec = -1, wd = 0;
        if (data[3] == 'B' || data[3] == 'D') {
            if (size != 12 && !(size == 13 && data[12] == 13)) return result(d, 30);
            int fields[7];
            for (unsigned i = 0; i < 7; ++i) fields[i] = data[3] == 'B' ? unbcd(p[i]) : p[i];
            wd = fields[0]; y = fields[1]; m = fields[2]; day = fields[3]; h = fields[4]; min = fields[5]; sec = fields[6];
            if (y < 0 || h < 1 || h > 12 || p[7] > 1) return result(d, 30);
            y += y < 80 ? 2000 : 1900; h = h % 12 + (p[7] ? 12 : 0);
        } else {
            while (size > 4 && (data[size - 1] == 13 || data[size - 1] == 10)) --size;
            if (data[3] == 'I') {
                if ((size != 23 && size != 27) || p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':' || p[16] != ':') return result(d, 30);
                y = digits(p,4); m = digits(p+5,2); day = digits(p+8,2); h = digits(p+11,2); min = digits(p+14,2); sec = digits(p+17,2);
            } else if (data[3] == 'A') {
                if ((size != 26 && size != 29) || p[4] != ' ' || p[7] != '/' || p[10] != '/' || p[13] != ' ' || p[16] != ':' || p[19] != ':') return result(d, 30);
                for (wd = 0; wd < 7; ++wd) if (!memcmp(p,weekdays[wd],2)) break;
                m = digits(p+5,2); day = digits(p+8,2); y = digits(p+11,2); h = digits(p+14,2); min = digits(p+17,2); sec = digits(p+20,2);
                if (y >= 0) y += y < 80 ? 2000 : 1900;
                if (size == 29) {
                    if (p[22] != ' ' || p[24] != 'M' || (p[23] != 'A' && p[23] != 'P') || h < 1 || h > 12) return result(d, 30);
                    h = h % 12 + (p[23] == 'P' ? 12 : 0);
                }
            } else return result(d, 30);
        }
        if (y < 1980 || y > 2079 || m < 1 || m > 12 || day < 1 || h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59 || wd < 0 || wd > 6) return result(d, 30);
        t = (mt_clock_time_t){(uint16_t)y,(uint8_t)m,(uint8_t)day,(uint8_t)h,(uint8_t)min,(uint8_t)sec};
        if (!mt_clock_valid(&t)) return result(d, 30);
        d->clock_offset = clock_seconds(&t) - now;
        return result(d, 0);
    }
    while (size > 4 && (data[size - 1] == 13 || data[size - 1] == 10)) --size;
    if (data[2] != 'R' || size != 4) return result(d, 30);
    clock_from_seconds(now + d->clock_offset, &t);
    unsigned wd = (unsigned)(clock_seconds(&t) / 86400 + 1) % 7;
    unsigned hour = t.hour % 12; if (!hour) hour = 12;
    result(d, 0);
    if (data[3] == 'I')
        (void)snprintf(d->status,sizeof d->status,"%04u-%02u-%02uT%02u:%02u:%02u %s\r",t.year,t.month,t.day,t.hour,t.minute,t.second,shortdays[wd]);
    else if (data[3] == 'A')
        (void)snprintf(d->status,sizeof d->status,"%s %02u/%02u/%02u %02u:%02u:%02u %s\r",weekdays[wd],t.month,t.day,t.year%100,hour,t.minute,t.second,t.hour>=12?"PM":"AM");
    else if (data[3] == 'D' || data[3] == 'B') {
        unsigned values[] = {wd,data[3]=='B'?t.year%100:t.year-1900U,t.month,t.day,hour,t.minute,t.second};
        for (unsigned i=0;i<7;++i) d->status[i]=(char)(data[3]=='B'?bcd(values[i]):values[i]);
        d->status[7]=t.hour>=12; d->status[8]=13; d->status_size=9; return 0;
    } else return result(d,30);
    d->status_size=strlen(d->status); return 0;
}
static int set_device(mt_iec_dos_t *d, unsigned device)
{
    if (device < 8 || device > 30) return result(d, 30);
    int rc = d->fs.device_number ? d->fs.device_number(d->fs.context, device) : 30;
    if (!rc) d->current_device = device;
    return result(d, rc);
}
static int command_inner(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    if (size && fold(data[0]) == 'T') return clock_command(d, data, size);
    size_t terminated = size;
    if (terminated && data[terminated-1] == 13) --terminated;
    else if (terminated > 1 && data[terminated-2] == 13 && data[terminated-1] == 10) terminated -= 2;
    if (size >= 3 && !memcmp(data,"M-E",3)) return result(d,98);
    if (size >= 3 && !memcmp(data,"M-W",3)) {
        if (terminated >= 7 && data[3] == 0x77 && data[4] == 0 && data[5])
            return set_device(d,data[6] & 31U);
        return result(d,30);
    }
    if (size >= 2 && data[0] == 'U' && data[1] == '0') {
        /* Device 13 is data, not BASIC's optional line terminator. */
        if (size > 4 && data[size-1] == 13) --size;
        else if (size > 5 && data[size-2] == 13 && data[size-1] == 10) size -= 2;
        if (size == 4 && (data[2] & 31U) == 30) return set_device(d,data[3]);
        return result(d,30);
    }
    if (size >= 5 && !memcmp(data,"M-R",3)) {
        if (terminated < 5) return result(d,30);
        unsigned count = terminated > 5 && data[5] ? data[5] : terminated > 5 ? 256 : 1;
        if (count > 256U - data[3]) count = 256U - data[3];
        result(d,0); memset(d->status,0,count); d->status_size=count; return 0;
    }
    if (size && fold(data[0]) == 'P') return position_command(d, data, size);
    if (size && fold(data[0]) == 'B') return block_command(d, data, size);
    if (size >= 2 && fold(data[0]) == 'U' && strchr("12AB", data[1])) return block_command(d, data, size);
    if (size >= 2 && fold(data[0]) == 'C' && (data[1] == 'P' || data[1] == 0xd0)) {
        unsigned p[4] = {0};
        if (data[1] == 0xd0) { if (size < 3) return result(d, 30); p[0] = data[2]; }
        else (void)numbers(data + 2, size - 2, p);
        unsigned selected = p[0] ? p[0] : d->current_partition;
        if (selected > 255 || !selected || !d->partitions[selected].root[0]) return result(d, 77);
        if (d->current_partition) strcpy(d->partitions[d->current_partition].cwd, d->cwd);
        d->current_partition = d->active_partition = selected;
        strcpy(d->cwd, d->partitions[selected].cwd);
        result(d, 2);
        (void)snprintf(d->status, sizeof d->status, "02,PARTITION SELECTED,%02u,00\r", selected);
        d->status_size = strlen(d->status);
        return 0;
    }
    if (size >= 3 && fold(data[0]) == 'G' && data[1] == '-' && fold(data[2]) == 'P') {
        unsigned p[4] = {d->current_partition};
        if (size == 4) p[0] = data[3] == 255 ? d->current_partition : data[3];
        else if (size != 3) return result(d, 30);
        if (p[0] > 255) return result(d, 77);
        result(d, 0);
        memset(d->status, 0, 31); d->status[2] = (char)p[0]; d->status[30] = '\r';
        const mt_iec_partition_t *part = &d->partitions[p[0]];
        if (p[0] && part->root[0]) {
            d->status[0] = suffix(part->root, ".d64") ? 2 : suffix(part->root, ".d71") ? 3 : suffix(part->root, ".d81") ? 4 : 1;
            memset(d->status + 3, 0xa0, 16);
            memcpy(d->status + 3, part->name, strlen(part->name));
            uint64_t bytes = 0;
            if (d->fs.capacity && !d->fs.capacity(d->fs.context, part->root, &bytes)) {
                uint64_t blocks = bytes / 512U + (bytes % 512U != 0);
                if (blocks > 0xffffffU) blocks = 0xffffffU;
                d->status[27] = (char)(blocks >> 16);
                d->status[28] = (char)(blocks >> 8);
                d->status[29] = (char)blocks;
            }
        }
        d->status_size = 31;
        return 0;
    }
    char text[MT_IEC_NAME];
    while (size && (data[size - 1] == '\r' || data[size - 1] == '\n'))
        --size;
    int rc = text_name(data, size, text);
    if (rc)
        return result(d, rc);
    if (!strcmp(text,"S-8")) return set_device(d,8);
    if (!strcmp(text,"S-9")) return set_device(d,9);
    if (!strcmp(text,"S-D")) return set_device(d,d->configured_device);
    if (!strcmp(text, "W-0") || !strcmp(text, "W-1")) {
        d->write_protect = text[2] == '1'; return result(d, 0);
    }
    if (!strncmp(text, "A:", 2)) {
        char *names = strchr(text + 2, '=');
        if (!names) return result(d, 30);
        *names++ = 0; unsigned attributes = 0;
        for (char *letter = text + 2; *letter; ++letter) {
            if (*letter == 'R') attributes |= 1;
            else if (*letter == 'H') attributes |= 2;
            else if (*letter == 'A') attributes |= 32;
            else return result(d, 30);
        }
        return result(d, attributes_command(d, names, attributes, 0x23, false));
    }
    if (!strncmp(text, "EL", 2) || !strncmp(text, "EU", 2))
        return result(d, attributes_command(d, text + 2, text[1] == 'L', 1, false));
    if (text[0] == 'L') return result(d, attributes_command(d, text + 1, 0, 1, true));
    if (!strncmp(text, "EH", 2)) {
        char *part = text + 2;
        while (*part == ' ' || (*part >= '0' && *part <= '9')) ++part;
        if (*part == ':') return result(d, label_command(d, text + 2, false));
        return result(d, attributes_command(d, text + 2, 0, 2, true));
    }
    if (!strncmp(text, "R-H", 3)) return result(d, label_command(d, text + 3, false));
    if (!strncmp(text, "D:", 2)) return result(d, label_command(d, text + 1, false));
    if (!strncmp(text, "XH", 2)) return result(d, label_command(d, text + 2, false));
    if (text[0] == 'N') {
        if (!strchr(text, ':')) return result(d, 33);
        return result(d, label_command(d, text + 1, true));
    }
    if (text[0] == 'C' && text[1] != 'D' && text[1] != 'P') {
        char *spec = text + 1;
        while (*spec >= 'A' && *spec <= 'Z') ++spec;
        return result(d, copy_command(d, spec));
    }
    if (!strcmp(text,"UI+") || !strcmp(text,"UI-") || !strcmp(text,"U9+") || !strcmp(text,"U9-")) return result(d,0);
    if (!strcmp(text,"UI") || !strcmp(text,"U9")) { result(d,73); return 0; }
    if (!strcmp(text, "I") || !strcmp(text, "UJ") || !strcmp(text,"U:")) {
        int reset = 0;
        for (unsigned ch=0;ch<15;++ch) { int close=close_channel(d,ch,false); if (!reset) reset=close; }
        if (reset) return result(d,reset);
        if (text[0]=='U') d->clock_offset=0;
        if (size > 1 && data[1] == 0xca) {
            for (unsigned i=1;i<256;++i) if (d->partitions[i].root[0])
                strcpy(d->partitions[i].cwd,d->partitions[i].root);
            if (d->partitions[1].root[0]) d->current_partition=d->active_partition=1;
            if (d->current_partition) strcpy(d->cwd,d->partitions[d->current_partition].cwd);
        }
        result(d,text[0]=='U'?73:0); return 0;
    }
    if (!strcmp(text,"XPWD")) {
        result(d,0);
        const char *root=partition_root(d), *relative=d->cwd+strlen(root);
        (void)snprintf(d->status,sizeof d->status,"%u:%s",d->current_partition,*relative?relative:"/");
        for(char *p=d->status;*p;++p) *p=(char)fold((uint8_t)*p);
        d->status_size=strlen(d->status);return 0;
    }
    if (!strncmp(text, "R-P:", 4)) {
        char *old = strchr(text + 4, '=');
        if (!old || old == text + 4 || !old[1]) return result(d, 30);
        *old++ = 0;
        for (unsigned i = 1; i < 256; ++i) if (d->partitions[i].root[0] && !compare(old, d->partitions[i].name)) {
            (void)snprintf(d->partitions[i].name, sizeof d->partitions[i].name, "%.16s", text + 4);
            return result(d, 0);
        }
        return result(d, 77);
    }
    char *p = strchr(text, ':');
    if (!p)
        return result(d, 30);
    *p++ = 0;
    if (p[0] == '0' && p[1] == ':')
        p += 2;
    if (!strcmp(text, "CD"))
    {
        if (!strcmp(p, "_") || !strcmp(p, ".."))
        {
            return result(d, parent_up(d, d->cwd));
        }
        if (!strcmp(p, "/") || !*p)
        {
            strcpy(d->cwd, partition_root(d));
            return result(d, 0);
        }
        char parent[MT_IEC_PATH], leaf[MT_IEC_NAME];
        rc = resolve(d, p, parent, leaf);
        if (!rc && (!*leaf || !strcmp(leaf, ".")))
        {
            strcpy(d->cwd, parent);
            return result(d, 0);
        }
        if (!rc && !strcmp(leaf, ".."))
        {
            rc = parent_up(d, parent);
            if (!rc)
                strcpy(d->cwd, parent);
            return result(d, rc);
        }
        mt_iec_entry_t e;
        if (!rc)
            rc = lookup(d, parent, leaf, false, 'D', &e);
        if (!rc && !join(d->cwd, parent, e.name))
            rc = 33;
        return result(d, rc);
    }
    char *other = NULL;
    if (!strcmp(text, "R"))
    {
        other = strchr(p, '=');
        if (!other)
            return result(d, 30);
        *other++ = 0;
    }
    char parent[MT_IEC_PATH], leaf[MT_IEC_NAME], target[MT_IEC_PATH];
    rc = resolve(d, p, parent, leaf);
    if (rc || !*leaf || !strcmp(leaf, ".") || !strcmp(leaf, ".."))
        return result(d, rc ? rc : 33);
    if (!strcmp(text, "MD"))
    {
        if (!component(leaf, false) || !join(target, parent, leaf))
            return result(d, 33);
        if (protected_path(d, target))
            return result(d, 26);
        return result(d, d->fs.mkdir ? d->fs.mkdir(d->fs.context, target) : 30);
    }
    if (other)
    {
        if (!component(leaf, false))
            return result(d, 33);
        mt_iec_entry_t e;
        int found = lookup(d, parent, leaf, false, 0, &e);
        if (!found)
            return result(d, 63);
        if (found != 62)
            return result(d, found);
        char sourceparent[MT_IEC_PATH], sourceleaf[MT_IEC_NAME], source[MT_IEC_PATH];
        rc = resolve(d, other, sourceparent, sourceleaf);
        if (!rc)
            rc = lookup(d, sourceparent, sourceleaf, false, 0, &e);
        if (rc)
            return result(d, rc);
        char destname[MT_IEC_NAME];
        strcpy(destname, leaf);
        if (!e.directory && (suffix(e.name, ".prg") || suffix(e.name, ".seq")) && !suffix(destname, ".prg") &&
            !suffix(destname, ".seq"))
        {
            if (strlen(destname) + 4 >= sizeof destname)
                return result(d, 33);
            strcat(destname, e.type == 'S' ? ".seq" : ".prg");
        }
        if (!join(target, parent, destname) || !join(source, sourceparent, e.name))
            return result(d, 33);
        if (protected_path(d, target) || protected_path(d, source))
            return result(d, 26);
        if (e.directory) return result(d, move_directory(d, source, target));
        if (in_use(d, target) || in_use(d, source))
            return result(d, 70);
        return result(d, d->fs.rename ? d->fs.rename(d->fs.context, source, target) : 30);
    }
    if (strcmp(text, "S") && strcmp(text, "RD"))
        return result(d, 30);
    rc = listing(d, parent);
    if (rc)
        return result(d, rc);
    unsigned count = 0;
    /* Preflight the complete set: a protected match never allows a partial
     * wildcard scratch before reporting write protection. */
    for (size_t i = 0; i < d->entry_count; ++i)
    {
        mt_iec_entry_t *e = &d->entries[i];
        if (!match(leaf, e->alias) && compare(leaf, e->name))
            continue;
        if (e->directory != (!strcmp(text, "RD")))
            continue;
        if (!join(target, parent, e->name))
            return result(d, 33);
        if (protected_path(d, target))
            return result(d, 26);
        if (in_use(d, target))
            return result(d, 70);
        ++count;
    }
    if (!count)
        return result(d, 62);
    for (size_t i = 0; i < d->entry_count; ++i)
    {
        mt_iec_entry_t *e = &d->entries[i];
        if ((!match(leaf, e->alias) && compare(leaf, e->name)) || e->directory != (!strcmp(text, "RD")))
            continue;
        (void)join(target, parent, e->name);
        rc = d->fs.remove ? d->fs.remove(d->fs.context, target) : 30;
        if (rc)
            return result(d, rc);
    }
    if (!strcmp(text, "S"))
    {
        result(d, 1);
        (void)snprintf(d->status, sizeof d->status, "01,FILES SCRATCHED,%02u,00\r", count > 99 ? 99 : count);
        d->status_size = strlen(d->status);
        return 0;
    }
    return result(d, 0);
}
int mt_iec_dos_command(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    if (!data && size) return result(d,30);
    if (size >= 2 && fold(data[0]) == 'C' && fold(data[1]) == 'D') {
        if (size >= MT_IEC_NAME) return result(d, 33);
        uint8_t name[MT_IEC_NAME] = {'$'}, translated[MT_IEC_NAME], command[MT_IEC_NAME + 3];
        if (size > 2) memcpy(name + 1, data + 2, size - 2);
        size_t n = size - 1;
        while (n > 1 && (name[n - 1] == '\r' || name[n - 1] == '\n')) --n;
        char cwd[MT_IEC_PATH]; unsigned part; size_t length;
        int rc = select_name(d, name, n, translated, &length, cwd, &part);
        unsigned selected = d->active_partition;
        if (!rc) {
            memcpy(command, "CD:", 3);
            memcpy(command + 3, translated + 1, length - 1);
            rc = command_inner(d, command, length + 2);
            if (!rc && selected) strcpy(d->partitions[selected].cwd, d->cwd);
        } else result(d, rc);
        if (selected != d->current_partition || rc) restore_name(d, cwd, part);
        else d->active_partition = part;
        return rc;
    }
    int rc = command_inner(d, data, size);
    if (d->current_partition) strcpy(d->partitions[d->current_partition].cwd, d->cwd);
    return rc;
}
