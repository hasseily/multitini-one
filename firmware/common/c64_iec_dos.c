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
static bool hidden(const char *s)
{
    const char *prefix = ".MTIEC-";
    bool same = strlen(s) >= 7;
    for (unsigned i = 0; i < 7 && same; ++i)
        if (fold((uint8_t)s[i]) != (unsigned)prefix[i])
            same = false;
    return same || suffix(s, ".mtiec");
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
    default:
        code = 74;
        break;
    }
    (void)snprintf(d->status, sizeof d->status, "%02d,%s,00,00\r", code, s);
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
    return d->fs.protected_path && d->fs.protected_path(d->fs.context, p);
}
static bool in_use(mt_iec_dos_t *d, const char *p)
{
    for (unsigned i = 0; i < 15; ++i)
        if (d->channels[i].kind && compare(d->channels[i].target, p) == 0)
            return true;
    return false;
}
static int parent_up(mt_iec_dos_t *d, char *path)
{
    if (!compare(path, d->root))
        return 26;
    char *last = strrchr(path, '/');
    if (!last)
        return 26;
    if ((size_t)(last - path) < strlen(d->root))
        strcpy(path, d->root);
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
    e->type = directory ? 'D' : suffix(name, ".seq") ? 'S' : 'P';
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
        size_t n = strlen(e->name);
        if (!e->directory && (suffix(e->name, ".prg") || suffix(e->name, ".seq")))
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
        if ((!type || e->type == type) &&
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
    strcpy(parent, *text == '/' ? d->root : d->cwd);
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
static int close_channel(mt_iec_dos_t *d, unsigned ch, bool abort)
{
    mt_iec_channel_t *c = &d->channels[ch];
    if (!c->kind)
        return 0;
    int rc = c->error;
    if (c->kind == 2 && !abort && !rc)
        rc = flush(d, c);
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
    if (c->kind == 2)
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
    for (unsigned i = 0; i < 15; ++i)
        if (d->channels[i].kind)
            return true;
    return false;
}
int mt_iec_dos_quiesce(mt_iec_dos_t *d)
{
    int rc = 0;
    for (unsigned i = 0; i < 15; ++i)
    {
        int r = close_channel(d, i, d->channels[i].kind == 2 && !d->channels[i].complete);
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
        strcpy(d->cwd, d->root);
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
static int directory_open(mt_iec_dos_t *d, unsigned ch, const char *pattern)
{
    if (d->directory_owner >= 0)
        return 70;
    if (*pattern == '0')
        ++pattern;
    if (*pattern == ':')
        ++pattern;
    if (!*pattern)
        pattern = "*";
    int rc = listing(d, d->cwd);
    if (rc)
        return rc;
    d->directory[0] = 1;
    d->directory[1] = 4;
    d->directory_size = 2;
    const char *base = strrchr(d->cwd, '/');
    base = base && base[1] ? base + 1 : "MULTITINI";
    char line[80];
    (void)snprintf(line, sizeof line, "\022\"%-16.16s\" 09 2A", base);
    rc = dir_line(d, 0, line);
    for (size_t i = 0; i < d->entry_count && !rc; ++i)
    {
        mt_iec_entry_t *e = &d->entries[i];
        if (!match(pattern, e->alias))
            continue;
        size_t blocks = e->size / 254 + (e->size % 254 != 0);
        if (blocks > 65535)
            blocks = 65535;
        (void)snprintf(line, sizeof line, "   \"%-16s\" %s", e->alias,
                       e->directory     ? "DIR"
                       : e->type == 'S' ? "SEQ"
                                        : "PRG");
        rc = dir_line(d, (unsigned)blocks, line);
    }
    if (!rc)
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
static int open_channel(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size)
{
    if (ch > 15 || (!name && size))
        return result(d, 70);
    if (ch == 15)
        return size ? mt_iec_dos_command(d, name, size) : 0;
    if (d->channels[ch].kind)
        return result(d, 70);
    char text[MT_IEC_NAME];
    int rc = text_name(name, size, text);
    if (rc)
        return result(d, rc);
    if (text[0] == '$')
        return result(d, directory_open(d, ch, text + 1));
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
        if (*opt == 'P' || *opt == 'S')
            type = *opt;
        else if (*opt == 'R' || *opt == 'W' || *opt == 'A')
            mode = *opt;
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
        return result(d, 0);
    }
    if (!component(leaf, false) || strchr(leaf, '='))
        return result(d, 33);
    if (exists && e.directory)
        return result(d, 64);
    if (exists && mode != 'A' && !replace)
        return result(d, 63);
    if (exists && type && type != e.type)
        return result(d, 64);
    char actual[MT_IEC_NAME];
    strcpy(actual, exists ? e.name : leaf);
    if (!exists && !suffix(actual, ".prg") && !suffix(actual, ".seq"))
    {
        size_t n = strlen(actual);
        if (n + 4 >= sizeof actual)
            return result(d, 33);
        strcat(actual, type == 'S' ? ".seq" : ".prg");
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
        (void)snprintf(temp, sizeof temp, ".mtiec-%08X-%02u.tmp", ++d->serial, ch);
        if (!join(c->temporary, parent, temp))
            return result(d, 33);
        if (protected_path(d, c->temporary))
            return result(d, 26);
        size_t unused = 0;
        rc = d->fs.open(d->fs.context, c->temporary, "x", &c->handle, &unused);
        if (rc != 63)
            break;
    }
    if (rc)
        return result(d, rc);
    c->kind = 2;
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
    return result(d, 0);
}
int mt_iec_dos_open(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t size)
{
    int rc = open_channel(d, ch, name, size);
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
    if (c->kind != 2)
        return result(d, c->error ? c->error : 70);
    if (c->error)
        return result(d, c->error);
    c->complete = false;
    if (size > MT_IEC_MAX_FILE - c->size)
    {
        c->error = 72;
        return result(d, 72);
    }
    for (size_t i = 0; i < size; ++i)
    {
        c->buffer[c->used++] = data[i];
        ++c->size;
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
int mt_iec_dos_peek(mt_iec_dos_t *d, unsigned ch, uint8_t *byte, bool *eoi, bool *available)
{
    *available = false;
    *eoi = false;
    if (ch == 15)
    {
        size_t n = strlen(d->status);
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
    if (ch == 15)
    {
        if (d->status_cursor < strlen(d->status))
            ++d->status_cursor;
        if (d->status_cursor == strlen(d->status))
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
int mt_iec_dos_command(mt_iec_dos_t *d, const uint8_t *data, size_t size)
{
    char text[MT_IEC_NAME];
    while (size && (data[size - 1] == '\r' || data[size - 1] == '\n'))
        --size;
    int rc = text_name(data, size, text);
    if (rc)
        return result(d, rc);
    if (!strcmp(text, "I") || !strcmp(text, "UJ"))
        return mt_iec_dos_reset(d);
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
            strcpy(d->cwd, d->root);
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
        return 0;
    }
    return result(d, 0);
}
