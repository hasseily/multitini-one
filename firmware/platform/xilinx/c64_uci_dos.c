/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_uci_geos.h"
#include "c64_uci_image.h"
#include "c64_uci_internal.h"
#include "multitini_c64_iec_dos.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    DOS_IDLE,
    DOS_READ,
    DOS_DIRECTORY,
    DOS_SCAN,
    DOS_COPY,
    DOS_REU_HOLD,
    DOS_REU_MEMORY,
    DOS_REU_COPY,
    DOS_REU_FENCE,
    DOS_ACTION,
    DOS_GEOS
} dos_state_t;
typedef struct { mt_sd_file_t *sd; c64_uci_image_file_t *image; } dos_file_t;
typedef struct
{
    char cwd[C64_UCI_PATH], filename[C64_UCI_PATH], copy_path[C64_UCI_PATH];
    dos_file_t file, copy, copy_destination;
    c64_uci_image_file_t *image_directory;
    mt_sd_directory_t *directory;
    uint8_t *entries;
    size_t *offsets;
    size_t entries_capacity, offsets_capacity;
    size_t entries_used, count, index, remaining, transferred, staged, memory_offset;
    uint32_t reu_address;
    dos_state_t state;
    bool truncated, save, changed, named, canceling, virtual_root;
    const char *failure;
} dos_t;
static dos_t instances[3];
static uint8_t transfer[4096];
/* One foreground UCI command owns this snapshot. SD latency never extends
 * the acknowledged native HOLD; the complete REU view is still atomic. */
static uint8_t reu_staging[0x1000000];
uint8_t *c64_uci_reu_staging(void) { return reu_staging; }
static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static mt_uci_result_t answer(mt_uci_reply_t *r, const char *text)
{
    mt_uci_reply_clear(r);
    mt_uci_reply_status(r, text);
    return MT_UCI_REPLY;
}
static mt_uci_result_t named_error(mt_uci_reply_t *r, const char *text, int32_t result)
{
    answer(r, text);
    put32(r->data, (uint32_t)result);
    r->data_length = 4;
    return MT_UCI_REPLY;
}
static bool equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++))
            return false;
    }
    return *a == *b;
}
static bool same_or_child(const char *parent, const char *path)
{
    while (*parent && *path && toupper((unsigned char)*parent) == toupper((unsigned char)*path)) {
        parent++;
        path++;
    }
    return !*parent && (!*path || *path == '/');
}
static bool guest_path(const dos_t *d, char text[C64_UCI_PATH])
{
    if (d->virtual_root) {
        strcpy(text, "/");
        return true;
    }
    const bool temporary = same_or_child(c64_uci_environment.temporary, d->cwd);
    const char *root = temporary ? c64_uci_environment.temporary : c64_uci_environment.root;
    int n = snprintf(text, C64_UCI_PATH, "%s%s", temporary ? "/Temp" : "/SD",
                     d->cwd + strlen(root));
    return n >= 0 && n < C64_UCI_PATH;
}
static bool resolve_path(dos_t *d, const uint8_t *bytes, size_t length,
                         char out[C64_UCI_PATH], bool *virtual_root)
{
    char name[C64_UCI_PATH], logical[C64_UCI_PATH];
    if (!c64_uci_path_text(bytes, length, name))
        return false;
    *virtual_root = false;
    /* Retain confined native paths used by older clients and service hooks. */
    if (strchr(name, ':'))
        return c64_uci_path(d->cwd, (const uint8_t *)name, strlen(name), out);
    if (name[0] == '/')
        strcpy(logical, "/");
    else if (!guest_path(d, logical))
        return false;
    /* Navigate the guest namespace before mapping a volume to its confined
     * storage root. In particular, /SD/.. is /, not the SD directory again. */
    for (const char *p = name; *p;) {
        if (*p == '/') { p++; continue; }
        size_t n = strcspn(p, "/"), used = strlen(logical);
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            char *slash = strrchr(logical, '/');
            if (slash == logical) logical[1] = 0;
            else *slash = 0;
        } else if (!(n == 1 && p[0] == '.')) {
            size_t separator = used > 1 ? 1 : 0;
            if (used + separator + n >= sizeof logical)
                return false;
            if (separator) logical[used++] = '/';
            memcpy(logical + used, p, n);
            logical[used + n] = 0;
        }
        p += n;
    }
    *virtual_root = !strcmp(logical, "/");
    return c64_uci_path(c64_uci_environment.root, (const uint8_t *)logical,
                        strlen(logical), out);
}
static bool path(dos_t *d, const uint8_t *command, size_t length, size_t start, char *out)
{
    bool virtual_root;
    return start <= length && resolve_path(d, command + start, length - start, out, &virtual_root);
}
static bool opened(const dos_file_t *file) { return file->sd || file->image; }
static mt_sd_result_t open_flags(const char *name, uint8_t flags, dos_file_t *file)
{
    char image[C64_UCI_PATH]; const char *member;
    if (c64_uci_image_path(name, image, &member) && *member)
        return c64_uci_image_open(name, flags, &file->image);
    return mt_sd_file_open_flags(name, flags, &file->sd);
}
static mt_sd_result_t file_read(dos_file_t *f, void *p, size_t n)
{
    return f->image ? c64_uci_image_read(f->image, p, n) : mt_sd_file_read(f->sd, p, n);
}
static mt_sd_result_t file_write(dos_file_t *f, const void *p, size_t n)
{
    return f->image ? c64_uci_image_write(f->image, p, n) : mt_sd_file_write(f->sd, p, n);
}
static mt_sd_result_t file_stat(const char *name, mt_sd_info_t *info)
{
    char image[C64_UCI_PATH]; const char *member;
    if (c64_uci_image_path(name, image, &member) && *member)
        return c64_uci_image_stat(name, info);
    return mt_sd_file_stat(name, info);
}
static void close_file(dos_file_t *file, const char **failure)
{
    if (file->image) {
        mt_sd_result_t s = c64_uci_image_close(&file->image);
        if (s.status != MT_SD_OK) {
            if (file->image) c64_uci_environment.fault = true;
            if (!*failure) *failure = c64_uci_sd_status(s);
        }
    }
    if (file->sd) {
        mt_sd_result_t s = mt_sd_file_close(file->sd);
        if (s.status == MT_SD_OK || mt_sd_file_tell(file->sd) < 0)
            file->sd = NULL;
        if (s.status != MT_SD_OK) {
            c64_uci_environment.fault = true;
            if (!*failure) *failure = c64_uci_sd_status(s);
        }
    }
}
static mt_sd_result_t directory_close(dos_t *d)
{
    if (d->image_directory) return c64_uci_image_close(&d->image_directory);
    mt_sd_result_t s = mt_sd_directory_close(d->directory);
    if (s.status == MT_SD_OK) d->directory = NULL;
    return s;
}
static mt_sd_result_t directory_open(dos_t *d)
{
    char image[C64_UCI_PATH]; const char *member;
    if (c64_uci_image_path(d->cwd, image, &member))
        return c64_uci_image_directory(d->cwd, &d->image_directory);
    return mt_sd_directory_open(d->cwd, &d->directory);
}
static mt_uci_result_t read_block(dos_t *d, mt_uci_reply_t *r)
{
    mt_uci_reply_clear(r);
    size_t n = d->remaining > 512 ? 512 : d->remaining;
    mt_sd_result_t s = file_read(&d->file, r->data, n);
    d->remaining -= s.size;
    r->data_length = s.size;
    r->more = s.status == MT_SD_OK && s.size == n && d->remaining != 0;
    if (!r->more)
        d->state = DOS_IDLE;
    if (s.status != MT_SD_OK)
        mt_uci_reply_status(r, c64_uci_sd_status(s));
    return MT_UCI_REPLY;
}
static void clear_directory(dos_t *d)
{
    free(d->entries);
    free(d->offsets);
    d->entries = NULL;
    d->offsets = NULL;
    d->entries_used = d->count = d->index = 0;
    d->entries_capacity = d->offsets_capacity = 0;
}
static bool append_directory(dos_t *d, const mt_sd_info_t *info)
{
    size_t bytes = strlen(info->name) + 2;
    if (bytes > SIZE_MAX - d->entries_used || d->count == SIZE_MAX)
        return false;
    if (d->count == d->offsets_capacity) {
        size_t capacity = d->offsets_capacity ? d->offsets_capacity * 2 : 64;
        if (capacity < d->offsets_capacity || capacity > SIZE_MAX / sizeof(size_t))
            return false;
        size_t *offsets = realloc(d->offsets, capacity * sizeof(size_t));
        if (!offsets)
            return false;
        d->offsets = offsets;
        d->offsets_capacity = capacity;
    }
    if (bytes > d->entries_capacity - d->entries_used) {
        size_t capacity = d->entries_capacity ? d->entries_capacity * 2 : 4096;
        if (capacity < d->entries_capacity)
            return false;
        if (capacity < d->entries_used + bytes)
            capacity = d->entries_used + bytes;
        uint8_t *entries = realloc(d->entries, capacity);
        if (!entries)
            return false;
        d->entries = entries;
        d->entries_capacity = capacity;
    }
    d->offsets[d->count++] = d->entries_used;
    d->entries[d->entries_used++] = info->attributes;
    memcpy(d->entries + d->entries_used, info->name, bytes - 1);
    d->entries_used += bytes - 1;
    return true;
}
static mt_uci_result_t current_path(dos_t *d, mt_uci_reply_t *r)
{
    char text[C64_UCI_PATH];
    if (!guest_path(d, text))
        return answer(r, "INVALID NAME");
    size_t n = strlen(text);
    if (n > 1) {
        if (n + 1 >= sizeof text)
            return answer(r, "INVALID NAME");
        text[n++] = '/';
        text[n] = 0;
    }
    mt_uci_reply_text(r, text, "00,OK");
    return MT_UCI_REPLY;
}
static mt_uci_result_t directory_block(dos_t *d, mt_uci_reply_t *r)
{
    mt_uci_reply_clear(r);
    if (d->index >= d->count) {
        d->state = DOS_IDLE;
        return answer(r, d->truncated ? "02,REQUEST TRUNCATED" : "01,DIRECTORY EMPTY");
    }
    uint8_t *entry = d->entries + d->offsets[d->index++];
    size_t n = strlen((char *)entry + 1) + 1;
    memcpy(r->data, entry, n);
    r->data_length = n;
    r->more = d->index < d->count;
    if (!r->more) {
        d->state = DOS_IDLE;
        mt_uci_reply_status(r, d->truncated ? "02,REQUEST TRUNCATED" : "00,OK");
    }
    return MT_UCI_REPLY;
}
static mt_uci_result_t reu_finish(dos_t *d, mt_uci_reply_t *r)
{
    c64_uci_service_hold_end();
    if (d->named)
        close_file(&d->file, &d->failure);
    d->state = DOS_IDLE;
    mt_uci_reply_clear(r);
    if (d->named) {
        int32_t result = d->failure ? -1 : (int32_t)d->transferred;
        put32(r->data, (uint32_t)result);
        int n = snprintf((char *)r->data + 4, sizeof(r->data) - 4,
                         d->save ? "REU Save: Saved %u bytes from $%6x to %s"
                                 : "REU Load: Loaded %u bytes to $%6x from %s",
                         (unsigned)d->transferred, (unsigned)d->reu_address,
                         c64_uci_environment.reu_image);
        if (d->failure)
            n = snprintf((char *)r->data + 4, sizeof(r->data) - 4, "REU %s: %s",
                         d->save ? "Save" : "Load", d->failure);
        size_t used = n > 0 ? (size_t)n : 0;
        if (used >= sizeof(r->data) - 4)
            used = sizeof(r->data) - 5;
        r->data_length = 4 + used;
        for (size_t i = 4; i < r->data_length; i++)
            r->data[i] = (uint8_t)toupper(r->data[i]);
    } else {
        int n =
            snprintf((char *)r->data, sizeof(r->data),
                     d->save ? "$%6x BYTES SAVED FROM REU $%6x" : "$%6x BYTES LOADED TO REU $%6x",
                     (unsigned)d->transferred, (unsigned)d->reu_address);
        if (n > 0)
            r->data_length = (size_t)n;
    }
    mt_uci_reply_status(r, d->failure     ? d->failure
                           : d->truncated ? "02,REQUEST TRUNCATED"
                                          : "00,OK");
    d->changed = false;
    return MT_UCI_REPLY;
}
static mt_uci_result_t poll(void *context, mt_uci_reply_t *r)
{
    dos_t *d = context;
    if (d->state == DOS_GEOS) {
        mt_uci_result_t result = c64_uci_geos_poll(r);
        if (result == MT_UCI_REPLY && !c64_uci_environment.fault)
            d->state = DOS_IDLE;
        return result;
    }
    if (d->state == DOS_SCAN) {
        for (unsigned step = 0; step < 8; step++) {
            mt_sd_info_t info;
            mt_sd_result_t s = (d->image_directory ? c64_uci_image_next(d->image_directory, &info) : mt_sd_directory_next(d->directory, &info));
            if (s.status != MT_SD_OK || !info.name[0]) {
                mt_sd_result_t closed = directory_close(d);
                if (closed.status != MT_SD_OK) {
                    c64_uci_environment.fault = true;
                    return MT_UCI_FAILED;
                }
                d->directory = NULL;
                d->state = DOS_IDLE;
                if (s.status != MT_SD_OK || closed.status != MT_SD_OK) {
                    clear_directory(d);
                    return answer(r, "86,CAN'T READ DIRECTORY");
                }
                return answer(r, d->count ? "00,OK" : "01,DIRECTORY EMPTY");
            }
            if (info.name[0] == '.' && (!info.name[1] || (info.name[1] == '.' && !info.name[2])))
                continue;
            if (mt_iec_dos_recovery_name(info.name))
                continue;
            if (!append_directory(d, &info)) {
                mt_sd_result_t closed = directory_close(d);
                if (closed.status != MT_SD_OK) {
                    c64_uci_environment.fault = true;
                    return MT_UCI_FAILED;
                }
                d->directory = NULL;
                d->state = DOS_IDLE;
                clear_directory(d);
                return answer(r, "OUT OF MEMORY");
            }
        }
        return MT_UCI_WAIT;
    }
    if (d->state == DOS_COPY) {
        mt_sd_result_t s = file_read(&d->copy, transfer, sizeof transfer);
        if (s.status != MT_SD_OK)
            d->failure = c64_uci_sd_status(s);
        if (!d->failure && s.size) {
            mt_sd_result_t w = file_write(&d->copy_destination, transfer, s.size);
            if (w.status != MT_SD_OK || w.size != s.size)
                d->failure = w.status == MT_SD_OK ? "DISK IS FULL" : c64_uci_sd_status(w);
        }
        if (d->failure || s.size < sizeof transfer) {
            close_file(&d->copy, &d->failure);
            close_file(&d->copy_destination, &d->failure);
            d->state = DOS_IDLE;
            return answer(r, d->failure ? d->failure : "00,OK");
        }
        return MT_UCI_WAIT;
    }
    if (d->state == DOS_ACTION) {
        int a = c64_uci_action_result();
        if (a > 0)
            return MT_UCI_WAIT;
        d->state = DOS_IDLE;
        return answer(r, a ? "87,INTERNAL ERROR" : "00,OK");
    }
    if (d->state == DOS_REU_HOLD) {
        int held = c64_uci_service_hold_begin();
        if (held > 0)
            return MT_UCI_WAIT;
        if (held < 0) {
            d->failure = "87,INTERNAL ERROR";
            return reu_finish(d, r);
        }
        d->memory_offset = 0;
        d->state = DOS_REU_MEMORY;
        return MT_UCI_WAIT;
    }
    if (d->state == DOS_REU_MEMORY) {
        size_t n = d->staged - d->memory_offset;
        if (n > sizeof transfer)
            n = sizeof transfer;
        if (d->save)
            c64_uci_reu_read(d->reu_address + (uint32_t)d->memory_offset,
                             reu_staging + d->memory_offset, n);
        else if (n) {
            c64_uci_reu_write(d->reu_address + (uint32_t)d->memory_offset,
                              reu_staging + d->memory_offset, n);
            d->changed = true;
        }
        d->memory_offset += n;
        if (d->memory_offset == d->staged) {
            if (d->save) {
                c64_uci_service_hold_end();
                d->state = DOS_REU_COPY;
            } else if (d->changed)
                d->state = DOS_REU_FENCE;
            else
                return reu_finish(d, r);
        }
        return MT_UCI_WAIT;
    }
    if (d->state == DOS_REU_COPY) {
        size_t n = d->remaining > sizeof transfer ? sizeof transfer : d->remaining;
        mt_sd_result_t s = {MT_SD_OK, 0, 0};
        if (d->save) {
            if (n)
                s = file_write(&d->file, reu_staging + d->transferred, n);
        } else {
            if (n)
                s = file_read(&d->file, reu_staging + d->transferred, n);
        }
        d->transferred += s.size;
        d->remaining -= s.size;
        if (s.status != MT_SD_OK)
            d->failure = c64_uci_sd_status(s);
        if (s.size < n && d->save && !d->failure)
            d->failure = "DISK IS FULL";
        if (d->failure || !d->remaining || s.size < n) {
            if (d->failure)
                return reu_finish(d, r);
            if (!d->save && d->transferred) {
                d->staged = d->transferred;
                d->state = DOS_REU_HOLD;
                return MT_UCI_WAIT;
            }
            return reu_finish(d, r);
        }
        return MT_UCI_WAIT;
    }
    if (d->state == DOS_REU_FENCE) {
        int done = c64_uci_reu_fence();
        if (done > 0)
            return MT_UCI_WAIT;
        if (done < 0)
            d->failure = "87,INTERNAL ERROR";
        else
            d->changed = false;
        return reu_finish(d, r);
    }
    return answer(r, "81,NOT IN DATA MODE");
}
static mt_uci_result_t next(void *context, size_t consumed, mt_uci_reply_t *r)
{
    dos_t *d = context;
    (void)consumed;
    if (d->state == DOS_READ)
        return read_block(d, r);
    if (d->state == DOS_DIRECTORY)
        return directory_block(d, r);
    return answer(r, "81,NOT IN DATA MODE");
}
static mt_uci_result_t abort_target(void *context, size_t consumed)
{
    dos_t *d = context;
    (void)consumed;
    if (c64_uci_image_retry() != 0) return MT_UCI_FAILED;
    if (d->state == DOS_GEOS) {
        mt_uci_result_t result = c64_uci_geos_abort();
        if (result != MT_UCI_REPLY)
            return result;
    }
    if (d->state == DOS_ACTION && !c64_uci_cancel_action())
        return MT_UCI_WAIT;
    if (d->changed) {
        int result = c64_uci_reu_fence();
        if (result > 0)
            return MT_UCI_WAIT;
        if (result < 0)
            return MT_UCI_FAILED;
        d->changed = false;
    }
    if (d->directory || d->image_directory) {
        mt_sd_result_t s = directory_close(d);
        if (s.status != MT_SD_OK) {
            c64_uci_environment.fault = true;
            return MT_UCI_FAILED;
        }
        d->directory = NULL;
    }
    if (d->state == DOS_COPY) {
        close_file(&d->copy, &d->failure);
        close_file(&d->copy_destination, &d->failure);
    }
    if (d->named)
        c64_uci_service_hold_end();
    if (d->named)
        close_file(&d->file, &d->failure);
    c64_uci_service_hold_end();
    d->state = DOS_IDLE;
    return c64_uci_environment.fault ? MT_UCI_FAILED : MT_UCI_REPLY;
}
static void reset_target(void *context)
{
    dos_t *d = context;
    const char *failure = NULL;
    if (abort_target(d, 0) != MT_UCI_REPLY)
        return;
    close_file(&d->file, &failure);
    close_file(&d->copy, &failure);
    close_file(&d->copy_destination, &failure);
    if (failure)
        c64_uci_environment.fault = true;
    strcpy(d->cwd, c64_uci_environment.root);
    d->virtual_root = false;
    clear_directory(d);
}
static mt_uci_result_t information(const char *filename, c64_uci_image_file_t *image, mt_uci_reply_t *r)
{
    mt_sd_info_t info;
    if ((image ? c64_uci_image_info(image, &info) : file_stat(filename, &info)).status != MT_SD_OK)
        return answer(r, "82,FILE NOT FOUND");
    mt_uci_reply_clear(r);
    put32(r->data, (uint32_t)info.size);
    r->data[4] = (uint8_t)info.date;
    r->data[5] = (uint8_t)(info.date >> 8);
    r->data[6] = (uint8_t)info.time;
    r->data[7] = (uint8_t)(info.time >> 8);
    memset(r->data + 8, ' ', 3);
    const char *ext = strrchr(info.name, '.');
    if (ext)
        for (unsigned i = 0; i < 3 && ext[i + 1]; i++)
            r->data[8 + i] = (uint8_t)toupper((unsigned char)ext[i + 1]);
    r->data[11] = info.attributes;
    size_t n = strlen(info.name);
    if (n > 63)
        n = 63;
    memcpy(r->data + 12, info.name, n);
    r->data_length = 12 + n;
    mt_uci_reply_status(r, "00,OK");
    return MT_UCI_REPLY;
}
static mt_uci_result_t get_time(const uint8_t *command, size_t length, mt_uci_reply_t *r, bool set)
{
    mt_clock_time_t t = {2010, 1, 1, 0, 0, 0};
    bool valid = false;
    c64_uci_callbacks_t *c = &c64_uci_environment.callbacks;
    if (set) {
        if (length != 8)
            return answer(r, "98,FUNCTION PROHIBITED");
        t = (mt_clock_time_t){(uint16_t)(1900 + command[2]),
                              command[3],
                              command[4],
                              command[5],
                              command[6],
                              command[7]};
        uint8_t encoded[7];
        if (!mt_clock_encode(&t, encoded) || !c->set_time || !c->set_time(c->context, &t))
            return answer(r, "98,FUNCTION PROHIBITED");
    } else if (length > 2 && command[2] > 1)
        return answer(r, "21,UNKNOWN COMMAND");
    if (c->get_time)
        valid = c->get_time(c->context, &t);
    if (!valid)
        t = (mt_clock_time_t){2010, 1, 1, 0, 0, 0};
    char data[32];
    if (set || (length > 2 && command[2] == 1)) {
        static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        uint8_t encoded[7];
        (void)mt_clock_encode(&t, encoded);
        snprintf(data, sizeof data, "%s %04u/%02u/%02u %02u:%02u:%02u", days[encoded[4] % 7],
                 t.year, t.month, t.day, t.hour, t.minute, t.second);
    } else
        snprintf(data, sizeof data, "%04u/%02u/%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour,
                 t.minute, t.second);
    mt_uci_reply_text(r, data, valid ? "00,OK" : "88,NO INFORMATION AVAILABLE");
    return MT_UCI_REPLY;
}
static mt_uci_result_t begin(void *context, const uint8_t command[896], size_t length,
                             mt_uci_reply_t *r)
{
    dos_t *d = context;
    char name[C64_UCI_PATH], other[C64_UCI_PATH];
    mt_sd_result_t s;
    d->failure = NULL;
    d->named = false;
    d->state = DOS_IDLE;
    switch (command[1]) {
    case 1:
        mt_uci_reply_text(r, "ULTIMATE-II DOS V1.2", "00,OK");
        return MT_UCI_REPLY;
    case 2:
        close_file(&d->file, &d->failure);
        if (d->failure)
            return answer(r, d->failure);
        if (length < 4 || !path(d, command, length, 3, name))
            return answer(r, "82,FILE NOT FOUND");
        if (c64_uci_protected(name, (command[2] & 2) != 0))
            return answer(r, "98,FUNCTION PROHIBITED");
        s = open_flags(name, command[2], &d->file);
        if (s.status == MT_SD_OK)
            strcpy(d->filename, name);
        return answer(r, c64_uci_sd_status(s));
    case 3:
        if (!opened(&d->file))
            return answer(r, "84,NO FILE TO CLOSE");
        close_file(&d->file, &d->failure);
        return answer(r, d->failure ? d->failure : "00,OK");
    case 4:
        if (!opened(&d->file))
            return answer(r, "85,NO FILE OPEN");
        d->remaining = command[2] | ((size_t)command[3] << 8);
        d->state = DOS_READ;
        return read_block(d, r);
    case 5:
        if (!opened(&d->file))
            return answer(r, "85,NO FILE OPEN");
        if (length < 4)
            return answer(r, "21,UNKNOWN COMMAND");
        s = file_write(&d->file, command + 4, length - 4);
        return answer(r, c64_uci_sd_status(s));
    case 6:
        if (!opened(&d->file))
            return answer(r, "85,NO FILE OPEN");
        return answer(r, c64_uci_sd_status((d->file.image ? c64_uci_image_seek(d->file.image, le32(command + 2)) : mt_sd_file_seek(d->file.sd, le32(command + 2), SEEK_SET))));
    case 7:
        if (!opened(&d->file))
            return answer(r, "85,NO FILE OPEN");
        return information(d->filename, d->file.image, r);
    case 8:
        if (!path(d, command, length, 2, name) || c64_uci_metadata_protected(name))
            return answer(r, "82,FILE NOT FOUND");
        return information(name, NULL, r);
    case 9:
        if (!path(d, command, length, 2, name))
            return answer(r, "82,FILE NOT FOUND");
        if (c64_uci_protected(name, true))
            return answer(r, "98,FUNCTION PROHIBITED");
        {
            char image[C64_UCI_PATH]; const char *member;
            s = c64_uci_image_path(name, image, &member) && *member ?
                c64_uci_image_remove(name) : mt_sd_file_remove(name);
            return answer(r, c64_uci_sd_status(s));
        }
    case 10:
    case 11: {
        size_t split = 2;
        while (split < length && command[split])
            split++;
        if (split == length || !path(d, command, split, 2, name) ||
            !path(d, command, length, split + 1, other))
            return answer(r, "21,UNKNOWN COMMAND");
        if (c64_uci_protected(name, command[1] == 10) || c64_uci_protected(other, true))
            return answer(r, "98,FUNCTION PROHIBITED");
        mt_sd_info_t info;
        /* FatFs does not reject moving a directory into itself. Validate
         * canonical identities before it creates the new directory entry. */
        if (command[1] == 10 && same_or_child(name, other))
            return answer(r, "98,FUNCTION PROHIBITED");
        if (command[1] == 11 && file_stat(other, &info).status == MT_SD_OK &&
            (info.attributes & 16)) {
            const char *base = strrchr(name, '/');
            base = base ? base + 1 : name;
            size_t a = strlen(other), b = strlen(base);
            if (a + b + 2 > sizeof other)
                return answer(r, "INVALID NAME");
            other[a] = '/';
            memcpy(other + a + 1, base, b + 1);
            if (c64_uci_protected(other, true))
                return answer(r, "98,FUNCTION PROHIBITED");
        }
        if (command[1] == 10) {
            char image[C64_UCI_PATH]; const char *member;
            s = c64_uci_image_path(name, image, &member) && *member ?
                c64_uci_image_rename(name, other) : mt_sd_file_rename(name, other);
            return answer(r, c64_uci_sd_status(s));
        }
        if (file_stat(other, &info).status == MT_SD_OK)
            return answer(r, "FILE EXISTS");
        char source_image[C64_UCI_PATH], destination_image[C64_UCI_PATH];
        const char *source_member, *destination_member;
        if (c64_uci_image_path(name, source_image, &source_member) && *source_member &&
            c64_uci_image_path(other, destination_image, &destination_member) && *destination_member &&
            equal(source_image, destination_image))
            return answer(r, c64_uci_sd_status(c64_uci_image_copy(name, other)));
        s = open_flags(name, 1, &d->copy);
        if (s.status != MT_SD_OK)
            return answer(r, c64_uci_sd_status(s));
        s = open_flags(other, 6, &d->copy_destination);
        if (s.status != MT_SD_OK) {
            close_file(&d->copy, &d->failure);
            return answer(r, c64_uci_sd_status(s));
        }
        strcpy(d->copy_path, other);
        d->state = DOS_COPY;
        return MT_UCI_WAIT;
    }
    case 0x11: {
        mt_sd_info_t info;
        bool virtual_root;
        if (length < 2 || !resolve_path(d, command + 2, length - 2, name, &virtual_root) ||
            c64_uci_protected(name, false))
            return answer(r, "83,NO SUCH DIRECTORY");
        char image[C64_UCI_PATH]; const char *member;
        if (c64_uci_image_path(name, image, &member)) {
            c64_uci_image_file_t *probe = NULL;
            s = c64_uci_image_directory(name, &probe);
            mt_sd_result_t closed = c64_uci_image_close(&probe);
            if (closed.status != MT_SD_OK) { c64_uci_environment.fault = true; return MT_UCI_FAILED; }
            if (s.status != MT_SD_OK) return answer(r, "83,NO SUCH DIRECTORY");
        } else if (mt_sd_file_stat(name, &info).status != MT_SD_OK || !(info.attributes & 16))
            return answer(r, "83,NO SUCH DIRECTORY");
        strcpy(d->cwd, name);
        d->virtual_root = virtual_root;
        return answer(r, "00,OK");
    }
    case 0x12:
        return current_path(d, r);
    case 0x13:
        if (d->directory || d->image_directory) {
            s = directory_close(d);
            if (s.status != MT_SD_OK) {
                c64_uci_environment.fault = true;
                return MT_UCI_FAILED;
            }
            d->directory = NULL;
        }
        clear_directory(d);
        d->truncated = false;
        if (d->virtual_root) {
            /* Device discovery is separate from listing the contents of SD.
             * USB names remain path aliases; they are not extra devices. */
            const char *devices[] = {"SD", "Temp"};
            for (unsigned i = 0; i < sizeof devices / sizeof devices[0]; i++) {
                mt_sd_info_t info = {0};
                info.attributes = 16;
                strcpy(info.name, devices[i]);
                if (!append_directory(d, &info)) {
                    clear_directory(d);
                    return answer(r, "OUT OF MEMORY");
                }
            }
            d->state = DOS_IDLE;
            return answer(r, "00,OK");
        }
        s = directory_open(d);
        if (s.status != MT_SD_OK)
            return answer(r, "86,CAN'T READ DIRECTORY");
        d->truncated = false;
        d->state = DOS_SCAN;
        return MT_UCI_WAIT;
    case 0x14:
        d->index = 0;
        d->state = DOS_DIRECTORY;
        return directory_block(d, r);
    case 0x16:
        if (!path(d, command, length, 2, name))
            return answer(r, "INVALID NAME");
        if (c64_uci_protected(name, true))
            return answer(r, "98,FUNCTION PROHIBITED");
        {
            char image[C64_UCI_PATH]; const char *member;
            s = c64_uci_image_path(name, image, &member) && *member ?
                c64_uci_image_mkdir(name) : mt_sd_storage_ensure_directory(name);
            return answer(r, c64_uci_sd_status(s));
        }
    case 0x17: {
        mt_sd_info_t info;
        if (c64_uci_protected(c64_uci_environment.home, false) ||
            mt_sd_file_stat(c64_uci_environment.home, &info).status != MT_SD_OK ||
            !(info.attributes & 16))
            return answer(r, "83,NO SUCH DIRECTORY");
        strcpy(d->cwd, c64_uci_environment.home);
        d->virtual_root = false;
        return current_path(d, r);
    }
    case 0x21:
    case 0x22:
        if (!opened(&d->file))
            return answer(r, "85,NO FILE OPEN");
        if (!c64_uci_reu_enabled())
            return answer(r, "98,FUNCTION PROHIBITED");
        d->reu_address = le32(command + 2) & 0xffffff;
        d->remaining = le32(command + 6) & 0x1ffffff;
        d->truncated = d->remaining > 0x1000000U - d->reu_address;
        if (d->truncated)
            d->remaining = 0x1000000U - d->reu_address;
        d->transferred = 0;
        d->save = command[1] == 0x22;
        d->changed = false;
        d->staged = d->remaining;
        d->state = d->save ? DOS_REU_HOLD : DOS_REU_COPY;
        return MT_UCI_WAIT;
    case 0x23: {
        if (command[2] != 0 && command[2] != c64_uci_service_drive_address())
            return answer(r, "90,DRIVE NOT PRESENT");
        if (!path(d, command, length, 3, name))
            return answer(r, "82,FILE NOT FOUND");
        const char *ext = strrchr(name, '.');
        if (!ext)
            return answer(r, "89,NOT A DISK IMAGE");
        if (!equal(ext, ".d64") && !equal(ext, ".g64"))
            return answer(r, equal(ext, ".d71") || equal(ext, ".d81") || equal(ext, ".g71")
                                 ? "91,INCOMPATIBLE IMAGE"
                                 : "89,NOT A DISK IMAGE");
        mt_sd_info_t info;
        if (c64_uci_protected(name, false) || mt_sd_file_stat(name, &info).status != MT_SD_OK)
            return answer(r, "82,FILE NOT FOUND");
        if (!c64_uci_post(C64_UCI_ACTION_MOUNT, name, 8))
            return answer(r, "87,INTERNAL ERROR");
        d->state = DOS_ACTION;
        return MT_UCI_WAIT;
    }
    case 0x24:
        if (command[2] != 0 && command[2] != c64_uci_service_drive_address())
            return answer(r, "90,DRIVE NOT PRESENT");
        if (!c64_uci_post(C64_UCI_ACTION_UNMOUNT, "", 8))
            return answer(r, "87,INTERNAL ERROR");
        d->state = DOS_ACTION;
        return MT_UCI_WAIT;
    case 0x25:
        if (command[2] != 0 && command[2] != c64_uci_service_drive_address())
            return answer(r, "90,DRIVE NOT PRESENT");
        if (!c64_uci_post(C64_UCI_ACTION_SWAP, "", 8))
            return answer(r, "87,INTERNAL ERROR");
        d->state = DOS_ACTION;
        return MT_UCI_WAIT;
    case 0x26:
        return get_time(command, length, r, false);
    case 0x27:
        return get_time(command, length, r, true);
    case 0xf0:
        mt_uci_reply_clear(r);
        memcpy(r->data, command, length);
        r->data_length = length;
        mt_uci_reply_status(r, "00,OK");
        return MT_UCI_REPLY;
    case 0x41:
    case 0x42: {
        mt_uci_result_t result = c64_uci_geos_begin(command, length, d->cwd, r);
        if (result == MT_UCI_WAIT)
            d->state = DOS_GEOS;
        return result;
    }
    case 0x15:
        return answer(r, "99,FUNCTION NOT IMPLEMENTED");
    default:
        return answer(r, "21,UNKNOWN COMMAND");
    }
}
mt_uci_target_t c64_uci_dos_target(unsigned instance)
{
    if (instance > 1)
        instance = 1;
    dos_t *d = &instances[instance];
    clear_directory(d);
    memset(d, 0, sizeof *d);
    strcpy(d->cwd, c64_uci_environment.root);
    return (mt_uci_target_t){d, begin, poll, next, abort_target, reset_target};
}
mt_uci_result_t c64_uci_named_reu_begin(bool save, mt_uci_reply_t *r)
{
    dos_t *d = &instances[2];
    if (opened(&d->file) || d->state != DOS_IDLE) {
        c64_uci_environment.fault = true;
        return MT_UCI_FAILED;
    }
    memset(d, 0, sizeof *d);
    d->named = true;
    d->save = save;
    const char *name = c64_uci_environment.reu_image;
    if (!c64_uci_reu_enabled()) {
        return named_error(r, "84,REU NOT ENABLED", -2);
    }
    if (!name[0] || c64_uci_protected(name, save)) {
        return named_error(r, "85,REU FILE CANNOT BE OPENED", -1);
    }
    mt_sd_info_t info;
    if (save) {
        char backup[C64_UCI_PATH];
        strcpy(backup, name);
        char *base = strrchr(backup, '/'), *extension = strrchr(backup, '.');
        if (extension && (!base || extension > base))
            *extension = 0;
        size_t length = strlen(backup);
        if (length + 4 > sizeof backup)
            return named_error(r, "98,FUNCTION PROHIBITED", -1);
        memcpy(backup + length, ".bu", 4);
        if (equal(name, backup) || c64_uci_protected(backup, true))
            return named_error(r, "98,FUNCTION PROHIBITED", -1);
        mt_sd_result_t existing = mt_sd_file_stat(name, &info);
        if (existing.status == MT_SD_OK) {
            if (info.attributes & 16)
                return named_error(r, "85,REU FILE CANNOT BE OPENED", -1);
            mt_sd_result_t s = mt_sd_file_remove(backup);
            if (s.status != MT_SD_OK && s.backend_status != 4)
                return named_error(r, c64_uci_sd_status(s), -1);
            s = mt_sd_file_rename(name, backup);
            if (s.status != MT_SD_OK)
                return named_error(r, c64_uci_sd_status(s), -1);
        } else if (existing.backend_status != 4 && existing.backend_status != 5)
            return named_error(r, c64_uci_sd_status(existing), -1);
        d->remaining = 0x1000000;
    } else {
        if (mt_sd_file_stat(name, &info).status != MT_SD_OK) {
            return named_error(r, "85,REU FILE CANNOT BE OPENED", -1);
        }
        d->remaining = info.size > 0x1000000 ? 0x1000000 : (size_t)info.size;
        d->truncated = info.size > 0x1000000;
    }
    mt_sd_result_t s = open_flags(name, save ? 6 : 1, &d->file);
    if (s.status != MT_SD_OK) {
        return named_error(r, "85,REU FILE CANNOT BE OPENED", -1);
    }
    d->staged = d->remaining;
    d->state = save ? DOS_REU_HOLD : DOS_REU_COPY;
    return MT_UCI_WAIT;
}
mt_uci_result_t c64_uci_named_reu_poll(mt_uci_reply_t *r)
{
    return poll(&instances[2], r);
}
mt_uci_result_t c64_uci_named_reu_abort(void)
{
    return abort_target(&instances[2], 0);
}
