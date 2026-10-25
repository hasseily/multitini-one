/* SPDX-License-Identifier: GPL-3.0-only */
/* Original implementation of CBM image access. Format reference: Ultimate
 * d43546fe25b26cb332f43f00219d33153dfe44f8, filesystem_d64.cc. */
#include "c64_uci_image.h"
#include "c64_uci_internal.h"
#include "multitini_c64_uci_cvt.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* A DNP BAM has a 32-byte header and 255 maps of 256 sectors. Track zero
 * terminates chains, so a 256th track is not representable on disk. */
#define IMAGE_MAX_SECTORS (255U * 256U)
#define IMAGE_VISITED_BYTES (IMAGE_MAX_SECTORS / 8U)
#define IMAGE_MAX_BYTES (IMAGE_MAX_SECTORS * 256U)
#define IMAGE_DNP 100U

struct c64_uci_image_file {
    struct c64_uci_image_file *next;
    mt_sd_file_t *owner;
    uint8_t *image, *data;
    size_t bytes, size, capacity, cursor, entry, directory, first_directory, header, slot;
    unsigned type, tracks, sectors, directory_track, extended_bam, label_offset, allocation_hint;
    uint8_t flags;
    bool changed, prepared, listing, volume, orphan, committing, cvt;
    uint8_t visited[IMAGE_VISITED_BYTES];
    char path[512];
};
static c64_uci_image_file_t *owners;
static mt_sd_result_t result(unsigned code, size_t n)
{
    mt_sd_status_t status = code == 0 ? MT_SD_OK : code == 4 || code == 5 ? MT_SD_OPEN_FAILED
                         : code == 8 ? MT_SD_FILE_EXISTS : code == 16 ? MT_SD_FILE_BUSY
                         : code == 17 ? MT_SD_FILE_LIMIT : MT_SD_READ_FAILED;
    return (mt_sd_result_t){status, code, n};
}
static bool same(const char *a, const char *b)
{
    while (*a && *b)
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return false;
    return !*a && !*b;
}
static bool match(const char *pattern, const char *name)
{
    const char *star = NULL, *retry = NULL;
    while (*name) {
        if (*pattern == '?' || toupper((unsigned char)*pattern) == toupper((unsigned char)*name)) {
            pattern++; name++;
        } else if (*pattern == '*') { star = pattern++; retry = name; }
        else if (star) { pattern = star + 1; name = ++retry; }
        else return false;
    }
    while (*pattern == '*') pattern++;
    return !*pattern;
}
bool c64_uci_image_path(const char *path, char image[512], const char **member)
{
    if (!path || strlen(path) >= 512) return false;
    for (const char *p = path; *p; p++) {
        if (*p != '.') continue;
        size_t n = strcspn(p, "/");
        if (n != 4) continue;
        char ext[5]; memcpy(ext, p, 4); ext[4] = 0;
        if (!same(ext, ".D64") && !same(ext, ".D71") && !same(ext, ".D81") && !same(ext, ".DNP")) continue;
        size_t count = (size_t)(p - path) + 4;
        memcpy(image, path, count); image[count] = 0;
        mt_sd_info_t info;
        mt_sd_result_t found = mt_sd_file_stat(image, &info);
        if (found.status != MT_SD_OK) {
            char journal[512];
            if (count + 5 > sizeof journal) continue;
            memcpy(journal, image, count); memcpy(journal + count, ".mtj", 5);
            if (mt_sd_file_stat(journal, &info).status != MT_SD_OK) continue;
        } else if (info.attributes & 16) continue;
        *member = path + count + (path[count] == '/');
        return true;
    }
    return false;
}
static unsigned spt(const c64_uci_image_file_t *f, unsigned track)
{
    if (f->type == IMAGE_DNP) return 256;
    if (f->type == 81) return 40;
    if (f->type == 71 && track > 35) track -= 35;
    return track <= 17 ? 21 : track <= 24 ? 19 : track <= 30 ? 18 : 17;
}
static size_t lba(const c64_uci_image_file_t *f, unsigned track, unsigned sector)
{
    if (!track || track > f->tracks || sector >= spt(f, track)) return SIZE_MAX;
    if (f->type == IMAGE_DNP) {
        size_t n = (track - 1U) * 256U + sector;
        return n < f->sectors ? n : SIZE_MAX;
    }
    size_t n = sector;
    for (unsigned t = 1; t < track; t++) n += spt(f, t);
    return n < f->sectors ? n : SIZE_MAX;
}
static void ts(const c64_uci_image_file_t *f, size_t block, unsigned *track, unsigned *sector)
{
    if (f->type == IMAGE_DNP) { *track = (unsigned)(block / 256) + 1; *sector = (unsigned)(block % 256); return; }
    *track = 1;
    while (block >= spt(f, *track)) block -= spt(f, (*track)++);
    *sector = (unsigned)block;
}
static bool visit(uint8_t visited[IMAGE_VISITED_BYTES], size_t block)
{
    if (block >= IMAGE_MAX_SECTORS || (visited[block / 8] & (1U << (block % 8)))) return false;
    visited[block / 8] |= (uint8_t)(1U << (block % 8));
    return true;
}
static size_t root_header(const c64_uci_image_file_t *f)
{
    return lba(f, f->directory_track, f->type == IMAGE_DNP ? 1 : 0);
}
static size_t root_directory(const c64_uci_image_file_t *f)
{
    if (f->type == IMAGE_DNP) {
        const uint8_t *h = f->image + root_header(f) * 256;
        return lba(f, h[0], h[1]);
    }
    return lba(f, f->directory_track, f->type == 81 ? 3 : 1);
}
static void dispose(c64_uci_image_file_t *f)
{
    c64_uci_image_file_t **p = &owners;
    while (*p && *p != f) p = &(*p)->next;
    if (*p) *p = f->next;
    free(f->image); free(f->data); free(f);
}
/* Dolphin, Speed and Prologic extensions have distinct on-disk BAM layouts.
 * Only an unambiguous map is used for new allocations; all other extra tracks
 * remain readable and are preserved while the standard 35-track BAM is used. */
static bool extended_map(const uint8_t *root, unsigned offset)
{
    bool present = false;
    for (unsigned t = 0; t < 5; t++) {
        const uint8_t *p = root + offset + t * 4;
        unsigned free = 0;
        if ((p[3] & 0xfe) || p[0] > 17) return false;
        for (unsigned b = 0; b < 17; b++) free += (p[1 + b / 8] >> (b % 8)) & 1U;
        if (p[0] != free) return false;
        present |= free != 0;
    }
    return present;
}
static bool validate_allocations(c64_uci_image_file_t *f);
static mt_sd_result_t load_image(const char *path, bool write, bool validate, c64_uci_image_file_t **out)
{
    *out = NULL;
    if (c64_uci_protected(path, write)) return result(16, 0);
    for (c64_uci_image_file_t *f = owners; f; f = f->next)
        if (same(path, f->path) && (write || (f->flags & 2))) return result(16, 0);
    char journal[512];
    if (strlen(path) + 5 > sizeof journal) return result(6, 0);
    strcpy(journal, path); strcat(journal, ".mtj");
    mt_sd_info_t j;
    mt_sd_result_t recovered = mt_sd_file_stat(journal, &j);
    if (recovered.status == MT_SD_OK) {
        if (c64_uci_protected(path, true)) return result(16, 0);
        /* A reset can interrupt replacement while the target is truncated or
         * not yet created. Recover before deriving geometry from that target. */
        size_t capacity = j.size < IMAGE_MAX_BYTES ? (size_t)j.size : IMAGE_MAX_BYTES;
        uint8_t *bytes = malloc(capacity);
        if (!bytes) return result(17, 0);
        recovered = mt_sd_storage_journal_recover(path, journal, bytes, capacity);
        free(bytes);
        if (recovered.status != MT_SD_OK) return recovered;
    } else if (recovered.backend_status != 4 && recovered.backend_status != 5) return recovered;
    mt_sd_info_t info;
    mt_sd_result_t r = mt_sd_file_stat(path, &info);
    if (r.status != MT_SD_OK) return r;
    const char *ext = strrchr(path, '.');
    unsigned type = ext && same(ext, ".D64") ? 64 : ext && same(ext, ".D71") ? 71 :
                    ext && same(ext, ".D81") ? 81 : ext && same(ext, ".DNP") ? IMAGE_DNP : 0;
    unsigned tracks = type == 71 ? 70 : type == 81 ? 80 : 35;
    unsigned sectors = type == 71 ? 1366 : type == 81 ? 3200 : 683;
    if (type == IMAGE_DNP) {
        if (info.size < 35U * 256U || info.size > IMAGE_MAX_BYTES || (info.size % 256U)) return result(13, 0);
        sectors = (unsigned)(info.size / 256U); tracks = (sectors + 255U) / 256U;
    }
    if (type == 64 && (info.size == 196608 || info.size == 197376)) { tracks = 40; sectors = 768; }
    if (type == 64 && (info.size == 205312 || info.size == 206114)) { tracks = 42; sectors = 802; }
    if (!type || (info.size != sectors * 256U && info.size != sectors * 257U)) return result(13, 0);
    c64_uci_image_file_t *f = calloc(1, sizeof *f);
    if (!f) return result(17, 0);
    f->bytes = (size_t)info.size;
    f->image = malloc(f->bytes);
    if (!f->image) { free(f); return result(17, 0); }
    f->type = type; f->tracks = tracks; f->sectors = sectors;
    f->directory_track = type == IMAGE_DNP ? 1 : type == 81 ? 40 : 18;
    f->flags = write ? 3 : 1;
    strcpy(f->path, path);
    r = mt_sd_file_open(path, write ? "rb+" : "rb", &f->owner);
    if (r.status != MT_SD_OK) { dispose(f); return r; }
    r = mt_sd_file_read(f->owner, f->image, f->bytes);
    if (r.status == MT_SD_OK && r.size != f->bytes) r = result(1, 0);
    f->next = owners; owners = f;
    if (r.status != MT_SD_OK) {
        mt_sd_result_t closed = c64_uci_image_close(&f);
        if (closed.status != MT_SD_OK) { *out = f; return closed; }
        return r;
    }
    const uint8_t *root = f->image + root_header(f) * 256;
    uint8_t version = root[2];
    f->label_offset = f->type == 81 || f->type == IMAGE_DNP ? 4 : 144;
    if (f->type == 64 && tracks > 35) {
        bool dolphin = extended_map(root, 0xac), speed = extended_map(root, 0xc0);
        if (version == 0x50 && extended_map(root, 0x90)) {
            f->extended_bam = 0x90; f->label_offset = 0xa4;
        } else if (dolphin != speed) f->extended_bam = dolphin ? 0xac : 0xc0;
    }
    if (write && version && version < 0x40) {
        r = c64_uci_image_close(&f);
        if (r.status != MT_SD_OK) { *out = f; return r; }
        return result(10, 0);
    }
    f->directory = f->first_directory = root_directory(f);
    f->header = root_header(f);
    if (write && validate && !validate_allocations(f)) {
        r = c64_uci_image_close(&f);
        if (r.status != MT_SD_OK) { *out = f; return r; }
        return result(1, 0);
    }
    *out = f;
    return result(0, 0);
}
static mt_sd_result_t load(const char *path, bool write, c64_uci_image_file_t **out)
{
    return load_image(path, write, true, out);
}
static bool reserved_name(const uint8_t *name, size_t n)
{
    static const char *const words[] = {"CON", "PRN", "AUX", "NUL"};
    if (n >= 3 && (n == 3 || name[3] == '.'))
        for (unsigned i = 0; i < 4; i++) if (!memcmp(name, words[i], 3)) return true;
    return n >= 4 && (n == 4 || name[4] == '.') && name[3] >= '1' && name[3] <= '9' &&
           (!memcmp(name, "COM", 3) || !memcmp(name, "LPT", 3));
}
static void encode_name(const uint8_t name[16], char out[256])
{
    size_t length = 0;
    while (length < 16 && name[length]) length++;
    while (length && name[length - 1] == 0xa0) length--;
    size_t at = 0;
    if (reserved_name(name, length)) { out[at++] = '{'; out[at++] = '}'; }
    bool escaped = false;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; i++) {
        uint8_t c = name[i];
        bool special = c < 32 || c >= 96 || strchr(":/\\*\"<>?", c) || (!i && c == '.');
        if (special) {
            if (!escaped) out[at++] = '{';
            out[at++] = hex[c >> 4]; out[at++] = hex[c & 15];
        } else {
            if (escaped) out[at++] = '}';
            out[at++] = (char)c;
        }
        escaped = special;
    }
    if (escaped) out[at++] = '}';
    out[at] = 0;
    if (at >= 4 && (same(out + at - 4, ".PRG") || same(out + at - 4, ".SEQ") ||
                    same(out + at - 4, ".USR") || same(out + at - 4, ".REL"))) {
        out[at++] = '{'; out[at++] = '}'; out[at] = 0;
    }
}
static int hex_value(char c)
{
    c = (char)toupper((unsigned char)c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static bool decode_name(const char *text, size_t length, uint8_t name[16])
{
    size_t at = 0, count = 0;
    memset(name, 0xa0, 16);
    while (at < length) {
        if (text[at] == '{') {
            at++;
            while (at < length && text[at] != '}') {
                if (at + 1 >= length || count == 16) return false;
                int hi = hex_value(text[at++]), lo = hex_value(text[at++]);
                if (hi < 0 || lo < 0 || (!hi && !lo)) return false;
                name[count++] = (uint8_t)((hi << 4) | lo);
            }
            if (at == length) return false;
            at++;
        } else {
            if (count == 16 || text[at] == '}') return false;
            name[count++] = (uint8_t)toupper((unsigned char)text[at++]);
        }
    }
    return count != 0;
}
static bool geos_entry(const uint8_t *e)
{
    unsigned type = e[0] & 7;
    return type >= 1 && type <= 3 && e[19] && e[21] <= 1;
}
static mt_sd_result_t cvt_result(mt_c64_uci_cvt_result_t r)
{
    return result(r == MT_C64_UCI_CVT_OK ? 0 : r == MT_C64_UCI_CVT_NOMEM ? 17 :
                  r == MT_C64_UCI_CVT_NOSPACE ? 7 : 1, 0);
}
static mt_c64_uci_cvt_ops_t cvt_ops(c64_uci_image_file_t *f);
static const char *extensions[] = {"", "SEQ", "PRG", "USR", "REL", "CBM", ""};
static void entry_info(c64_uci_image_file_t *f, size_t at, mt_sd_info_t *info)
{
    const uint8_t *e = f->image + at;
    memset(info, 0, sizeof *info);
    encode_name(e + 3, info->name);
    size_t n = strlen(info->name);
    unsigned type = e[0] & 7;
    if (type != 6) { info->name[n++] = '.'; memcpy(info->name + n, type == 7 || geos_entry(e) ? "CVT" : extensions[type], 4); }
    info->attributes = (e[0] & 0x40) ? 1 : 0;
    if (type == 6) info->attributes |= 16;
    info->size = (e[28] | ((unsigned)e[29] << 8)) * 254U;
}
static mt_sd_result_t next_entry(c64_uci_image_file_t *f, size_t *at)
{
    while (f->directory != SIZE_MAX) {
        if (!f->slot && !visit(f->visited, f->directory)) return result(1, 0);
        while (f->slot < 8) {
            size_t entry = f->directory * 256 + (f->slot++) * 32 + 2;
            unsigned type = f->image[entry] & 7;
            if (type && type <= 6) { *at = entry; return result(0, 1); }
        }
        const uint8_t *p = f->image + f->directory * 256;
        if (!p[0]) f->directory = SIZE_MAX;
        else {
            f->directory = lba(f, p[0], p[1]);
            if (f->directory == SIZE_MAX) return result(1, 0);
        }
        f->slot = 0;
    }
    return result(0, 0);
}
static void rewind_dir(c64_uci_image_file_t *f)
{
    f->directory = f->first_directory;
    f->slot = 0; memset(f->visited, 0, sizeof f->visited);
}
static mt_sd_result_t find_leaf(c64_uci_image_file_t *f, const char *name, size_t *entry)
{
    if (!name[0] || strchr(name, '/')) return result(5, 0);
    rewind_dir(f);
    mt_sd_result_t r;
    while ((r = next_entry(f, entry)).status == MT_SD_OK && r.size) {
        mt_sd_info_t info; entry_info(f, *entry, &info);
        if (match(name, info.name)) return result(0, 0);
        char *ext = strrchr(info.name, '.');
        if (ext) *ext = 0;
        if (!strchr(name, '.') && match(name, info.name)) return result(0, 0);
    }
    return r.status == MT_SD_OK ? result(4, 0) : r;
}
static mt_sd_result_t select_directory_checked(c64_uci_image_file_t *f, const char *path,
                                                 size_t length, size_t forbidden)
{
    f->header = root_header(f);
    f->first_directory = root_directory(f);
    if (f->first_directory == SIZE_MAX) return result(13, 0);
    uint8_t visited[IMAGE_VISITED_BYTES] = {0};
    visit(visited, f->header);
    const char *p = path;
    while ((size_t)(p - path) < length) {
        size_t n = strcspn(p, "/");
        if (n > length - (size_t)(p - path)) n = length - (size_t)(p - path);
        char name[256];
        if (!n || n >= sizeof name) return result(6, 0);
        memcpy(name, p, n); name[n] = 0;
        size_t entry;
        mt_sd_result_t r = find_leaf(f, name, &entry);
        if (r.status != MT_SD_OK || (f->image[entry] & 7) != 6) return result(5, 0);
        f->header = lba(f, f->image[entry + 1], f->image[entry + 2]);
        if (f->header == forbidden) return result(7, 0);
        if (f->header == SIZE_MAX || !visit(visited, f->header)) return result(1, 0);
        const uint8_t *h = f->image + f->header * 256;
        f->first_directory = lba(f, h[0], h[1]);
        if (f->first_directory == SIZE_MAX) return result(1, 0);
        p += n;
        if ((size_t)(p - path) < length) p++;
    }
    rewind_dir(f);
    return result(0, 0);
}
static mt_sd_result_t select_directory(c64_uci_image_file_t *f, const char *path, size_t length)
{
    return select_directory_checked(f, path, length, SIZE_MAX);
}
static mt_sd_result_t select_parent(c64_uci_image_file_t *f, const char *path, const char **leaf)
{
    const char *slash = strrchr(path, '/');
    *leaf = slash ? slash + 1 : path;
    return select_directory(f, path, slash ? (size_t)(slash - path) : 0);
}
static mt_sd_result_t find_entry(c64_uci_image_file_t *f, const char *name, size_t *entry)
{
    const char *leaf;
    mt_sd_result_t r = select_parent(f, name, &leaf);
    return r.status == MT_SD_OK ? find_leaf(f, leaf, entry) : r;
}
static mt_sd_result_t chain(c64_uci_image_file_t *f, size_t entry, uint8_t *data,
                             size_t *bytes, uint16_t blocks[IMAGE_MAX_SECTORS], size_t *count)
{
    const uint8_t *e = f->image + entry;
    unsigned track = e[1], sector = e[2];
    uint8_t visited[IMAGE_VISITED_BYTES] = {0};
    *bytes = *count = 0;
    while (track) {
        size_t block = lba(f, track, sector);
        if (block == SIZE_MAX || !visit(visited, block)) return result(1, 0);
        const uint8_t *p = f->image + block * 256;
        if (!p[0] && !p[1]) return result(1, 0);
        size_t n = p[0] ? 254 : p[1] - 1U;
        if (data) memcpy(data + *bytes, p + 2, n);
        if (blocks) blocks[*count] = (uint16_t)block;
        (*count)++; *bytes += n;
        track = p[0]; sector = p[1];
    }
    return result(0, *bytes);
}
static bool bam(c64_uci_image_file_t *f, unsigned track, uint8_t **count, uint8_t **bits)
{
    if (!track || track > f->tracks) return false;
    uint8_t *root = f->image + root_header(f) * 256;
    if (f->type == IMAGE_DNP) {
        *count = NULL; *bits = f->image + 2U * 256U + track * 32U;
    } else if (f->type == 81) {
        *count = f->image + lba(f, 40, track > 40 ? 2 : 1) * 256 +
                 16 + 6 * ((track - 1) % 40);
        *bits = *count + 1;
    } else if (f->type == 71 && track > 35) {
        *count = root + 0xdd + track - 36;
        *bits = f->image + lba(f, 53, 0) * 256 + 3 * (track - 36);
    } else {
        if (track > 35) {
            if (!f->extended_bam || track > 40) return false;
            *count = root + f->extended_bam + 4 * (track - 36);
        } else *count = root + track * 4;
        *bits = *count + 1;
    }
    return true;
}
static bool allocated(c64_uci_image_file_t *f, unsigned track, unsigned sector)
{
    uint8_t *count, *bits;
    if (lba(f, track, sector) == SIZE_MAX) return true;
    if (f->type == IMAGE_DNP) sector ^= 7;
    return !bam(f, track, &count, &bits) || !(bits[sector / 8] & (1U << (sector % 8)));
}
/* Before ordinary filesystem mutation, reject cross-linked files/directories
 * and allocated metadata advertised as free. A corrupt BAM must never make a
 * new member overwrite another file. Raw DOS block commands intentionally
 * bypass this check so utilities can inspect and repair malformed images. */
static bool own_block(c64_uci_image_file_t *f, uint8_t used[IMAGE_VISITED_BYTES], unsigned track,
                       unsigned sector)
{
    size_t block = lba(f, track, sector);
    return block != SIZE_MAX && allocated(f, track, sector) && visit(used, block);
}
typedef struct { c64_uci_image_file_t *file; uint8_t *used; } cvt_owner_t;
static bool cvt_own(void *context, uint8_t t, uint8_t s)
{
    cvt_owner_t *owner = context;
    return own_block(owner->file, owner->used, t, s);
}
static bool own_chain(c64_uci_image_file_t *f, uint8_t used[IMAGE_VISITED_BYTES], unsigned t, unsigned s)
{
    while (t) {
        if (!own_block(f, used, t, s)) return false;
        const uint8_t *p = f->image + lba(f, t, s) * 256;
        if (!p[0] && !p[1]) return false;
        t = p[0]; s = p[1];
    }
    return true;
}
static bool own_directory(c64_uci_image_file_t *f, uint8_t used[IMAGE_VISITED_BYTES], unsigned t,
                           unsigned s, unsigned depth)
{
    if (depth > 255) return false;
    while (t) {
        if (!own_block(f, used, t, s)) return false;
        const uint8_t *p = f->image + lba(f, t, s) * 256;
        for (unsigned slot = 0; slot < 8; slot++) {
            const uint8_t *e = p + slot * 32 + 2;
            unsigned type = e[0] & 7;
            if (!type) continue;
            if (geos_entry(e)) {
                mt_c64_uci_cvt_ops_t ops = cvt_ops(f);
                cvt_owner_t owner = {f, used};
                if (mt_c64_uci_cvt_walk(e, &ops, cvt_own, &owner) != MT_C64_UCI_CVT_OK) return false;
            } else if (type <= 4) {
                if (!own_chain(f, used, e[1], e[2])) return false;
                /* REL side sectors and GEOS auxiliary information are owned
                 * independently of the ordinary data chain. */
                if (e[19] && !own_chain(f, used, e[19], e[20])) return false;
            } else if (type == 6) {
                if (!own_block(f, used, e[1], e[2])) return false;
                const uint8_t *h = f->image + lba(f, e[1], e[2]) * 256;
                if (!h[0] || !own_directory(f, used, h[0], h[1], depth + 1)) return false;
            } else return false;
        }
        t = p[0]; s = p[1];
    }
    return true;
}
static bool validate_allocations(c64_uci_image_file_t *f)
{
    uint8_t used[IMAGE_VISITED_BYTES] = {0};
    if (!own_block(f, used, f->directory_track, f->type == IMAGE_DNP ? 1 : 0)) return false;
    if (f->type == IMAGE_DNP) {
        if (!own_block(f, used, 1, 0)) return false;
        for (unsigned s = 2; s <= 33; s++) if (!own_block(f, used, 1, s)) return false;
        const uint8_t *h = f->image + root_header(f) * 256;
        return h[0] && own_directory(f, used, h[0], h[1], 0);
    }
    if (f->type == 81 && (!own_block(f, used, 40, 1) || !own_block(f, used, 40, 2))) return false;
    if (f->type == 71 && !own_block(f, used, 53, 0)) return false;
    return own_directory(f, used, f->directory_track, f->type == 81 ? 3 : 1, 0);
}
static void allocation(c64_uci_image_file_t *f, unsigned track, unsigned sector, bool allocate)
{
    uint8_t *count, *bits;
    size_t block = lba(f, track, sector);
    if (block == SIZE_MAX || !bam(f, track, &count, &bits)) return;
    if (f->type == IMAGE_DNP) {
        sector ^= 7;
        if (allocate) f->allocation_hint = (unsigned)block + 1;
        else if (block < f->allocation_hint) f->allocation_hint = (unsigned)block;
    }
    bool free = (bits[sector / 8] & (1U << (sector % 8))) != 0;
    if (allocate && free) { bits[sector / 8] &= (uint8_t)~(1U << (sector % 8)); if (count && *count) --*count; }
    if (!allocate && !free) { bits[sector / 8] |= (uint8_t)(1U << (sector % 8)); if (count) ++*count; }
}
static bool next_free(c64_uci_image_file_t *f, unsigned *track, unsigned *sector, bool directory)
{
    /* DNP directories share the data allocator, including track 1. Retain a
     * per-transaction hint so a full-size file is linear, not quadratic. */
    if (f->type == IMAGE_DNP) {
        for (unsigned i = 0; i < f->sectors; i++) {
            unsigned block = (f->allocation_hint + i) % f->sectors;
            unsigned t = block / 256 + 1, s = block % 256;
            if (!allocated(f, t, s)) { *track = t; *sector = s; return true; }
        }
        return false;
    }
    for (unsigned t = 1; t <= f->tracks; t++) {
        if (directory ? t != f->directory_track : t == f->directory_track || (f->type == 71 && t == 53)) continue;
        for (unsigned s = 0; s < spt(f, t); s++)
            if (!allocated(f, t, s)) { *track = t; *sector = s; return true; }
    }
    return false;
}
static bool cvt_read(void *context, uint8_t t, uint8_t s, uint8_t data[256])
{
    c64_uci_image_file_t *f = context; size_t block = lba(f, t, s);
    if (block == SIZE_MAX) return false;
    memcpy(data, f->image + block * 256, 256); return true;
}
static bool cvt_write(void *context, uint8_t t, uint8_t s, const uint8_t data[256])
{
    c64_uci_image_file_t *f = context; size_t block = lba(f, t, s);
    if (block == SIZE_MAX) return false;
    memcpy(f->image + block * 256, data, 256); return true;
}
static bool cvt_allocate(void *context, uint8_t *track, uint8_t *sector)
{
    c64_uci_image_file_t *f = context; unsigned t, s;
    if (!next_free(f, &t, &s, false)) return false;
    allocation(f, t, s, true); *track = (uint8_t)t; *sector = (uint8_t)s; return true;
}
static mt_c64_uci_cvt_ops_t cvt_ops(c64_uci_image_file_t *f)
{
    return (mt_c64_uci_cvt_ops_t){f, cvt_read, cvt_allocate, cvt_write};
}
static mt_sd_result_t create_entry(c64_uci_image_file_t *f, const char *path, bool directory, size_t *entry)
{
    const char *name;
    mt_sd_result_t parent = select_parent(f, path, &name);
    if (parent.status != MT_SD_OK) return parent;
    if (!*name || strchr(name, '/') || strpbrk(name, "*?")) return result(6, 0);
    const char *ext = strrchr(name, '.');
    unsigned type = directory ? 6 : 2;
    if (directory) ext = NULL;
    if (ext) {
        if (same(ext + 1, "CVT")) type = 7;
        else {
            for (type = 1; type <= 4 && !same(ext + 1, extensions[type]); type++) {}
            if (type > 4) return result(6, 0);
        }
    }
    size_t n = ext ? (size_t)(ext - name) : strlen(name);
    uint8_t raw_name[16];
    if (!decode_name(name, n, raw_name)) return result(6, 0);
    size_t block = f->first_directory;
    uint8_t visited[IMAGE_VISITED_BYTES] = {0};
    while (1) {
        if (block == SIZE_MAX || !visit(visited, block)) return result(1, 0);
        uint8_t *p = f->image + block * 256;
        for (unsigned i = 0; i < 8; i++) {
            uint8_t *e = p + 2 + 32 * i;
            if (!(e[0] & 7)) {
                memset(e, 0, 30); e[0] = (uint8_t)(0x80 | type);
                memcpy(e + 3, raw_name, 16);
                *entry = (size_t)(e - f->image); return result(0, 0);
            }
        }
        if (p[0]) block = lba(f, p[0], p[1]);
        else {
            unsigned t, s;
            if (!next_free(f, &t, &s, f->header == root_header(f))) return result(7, 0);
            allocation(f, t, s, true); p[0] = (uint8_t)t; p[1] = (uint8_t)s;
            block = lba(f, t, s); memset(f->image + block * 256, 0, 256);
            f->image[block * 256 + 1] = 255;
        }
    }
}
mt_sd_result_t c64_uci_image_open(const char *path, uint8_t flags, c64_uci_image_file_t **out)
{
    *out = NULL;
    char image[512]; const char *member;
    if (!(flags & 3) || (flags & ~31U) || ((flags & 28U) && !(flags & 2)) ||
        !c64_uci_image_path(path, image, &member)) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, (flags & 2) != 0, &f);
    if (r.status != MT_SD_OK) { *out = f; return r; }
    r = find_entry(f, member, &f->entry);
    if (r.status == MT_SD_OK && (flags & 4)) r = result(8, 0);
    else if (r.backend_status == 4 && (flags & 2) && (flags & (4|8|16))) {
        r = create_entry(f, member, false, &f->entry);
        if (r.status == MT_SD_OK) f->changed = true;
    }
    if (r.status == MT_SD_OK) {
        unsigned type = f->image[f->entry] & 7;
        f->cvt = type == 7 || geos_entry(f->image + f->entry);
        if ((!f->cvt && type > 4) || ((flags & 2) && (f->image[f->entry] & 0x40))) r = result(10, 0);
    }
    size_t count = 0;
    if (r.status == MT_SD_OK && f->cvt) {
        if (!(flags & 8) && geos_entry(f->image + f->entry)) {
            mt_c64_uci_cvt_ops_t ops = cvt_ops(f);
            r = cvt_result(mt_c64_uci_cvt_export(f->image + f->entry, &ops, &f->data, &f->size));
            f->capacity = f->size;
        }
    } else if (r.status == MT_SD_OK) r = chain(f, f->entry, NULL, &f->size, NULL, &count);
    if (r.status == MT_SD_OK && (flags & 8) && (flags & 2)) { f->size = 0; f->changed = true; }
    if (r.status == MT_SD_OK && !f->cvt) {
        size_t header = (f->image[f->entry] & 7) == 4 ? 2 : 0;
        size_t payload = f->size;
        f->capacity = f->size + header;
        if (f->capacity) {
            f->data = malloc(f->capacity);
            if (!f->data) r = result(17, 0);
            else {
                if (header) { f->data[0] = f->image[f->entry + 21]; f->data[1] = 0; }
                if (!(flags & 8)) r = chain(f, f->entry, f->data + header, &payload, NULL, &count);
                f->size += header;
            }
        }
    }
    if (r.status != MT_SD_OK) {
        f->changed = false;
        mt_sd_result_t closed = c64_uci_image_close(&f);
        if (closed.status != MT_SD_OK) { *out = f; return closed; }
        return r;
    }
    f->flags = flags; *out = f; return result(0, f->size);
}
mt_sd_result_t c64_uci_image_read(c64_uci_image_file_t *f, void *data, size_t n)
{
    if (!f || !(f->flags & 1) || f->listing) return result(9, 0);
    if (n > f->size - f->cursor) n = f->size - f->cursor;
    if (n) memcpy(data, f->data + f->cursor, n);
    f->cursor += n; return result(0, n);
}
mt_sd_result_t c64_uci_image_write(c64_uci_image_file_t *f, const void *data, size_t n)
{
    if (!f || !(f->flags & 2) || f->listing) return result(9, 0);
    if (f->prepared || n > f->sectors * 254U + 2U - f->cursor) return result(7, 0);
    size_t end = f->cursor + n;
    if (end > f->capacity) {
        uint8_t *p = realloc(f->data, end);
        if (!p) return result(17, 0);
        f->data = p; f->capacity = end;
    }
    if (n) memcpy(f->data + f->cursor, data, n);
    if (end > f->size) f->size = end;
    f->cursor = end; f->changed |= n != 0; return result(0, n);
}
mt_sd_result_t c64_uci_image_seek(c64_uci_image_file_t *f, uint32_t offset)
{
    if (!f || f->listing) return result(9, 0);
    f->cursor = offset > f->size ? f->size : offset; return result(0, f->cursor);
}
typedef struct { c64_uci_image_file_t *file; uint16_t *blocks; size_t count; } cvt_blocks_t;
static bool collect_cvt_block(void *context, uint8_t t, uint8_t s)
{
    cvt_blocks_t *list = context; size_t block = lba(list->file, t, s);
    if (block == SIZE_MAX || list->count == IMAGE_MAX_SECTORS) return false;
    list->blocks[list->count++] = (uint16_t)block; return true;
}
static mt_sd_result_t prepare_cvt(c64_uci_image_file_t *f)
{
    uint16_t old[IMAGE_MAX_SECTORS]; cvt_blocks_t list = {f, old, 0};
    mt_c64_uci_cvt_ops_t ops = cvt_ops(f);
    mt_sd_result_t r = result(0, 0);
    if (geos_entry(f->image + f->entry))
        r = cvt_result(mt_c64_uci_cvt_walk(f->image + f->entry, &ops, collect_cvt_block, &list));
    else {
        size_t bytes;
        r = chain(f, f->entry, NULL, &bytes, old, &list.count);
    }
    if (r.status != MT_SD_OK) return r;
    uint8_t *saved = f->image;
    f->image = malloc(f->bytes);
    if (!f->image) { f->image = saved; return result(17, 0); }
    memcpy(f->image, saved, f->bytes);
    for (size_t i = 0; i < list.count; i++) { unsigned t, s; ts(f, old[i], &t, &s); allocation(f, t, s, false); }
    r = cvt_result(mt_c64_uci_cvt_import(f->data, f->size, &ops, f->image + f->entry));
    if (r.status == MT_SD_OK) {
        mt_sd_info_t imported; entry_info(f, f->entry, &imported);
        size_t other; rewind_dir(f);
        mt_sd_result_t scan;
        while ((scan = next_entry(f, &other)).status == MT_SD_OK && scan.size) {
            mt_sd_info_t candidate; entry_info(f, other, &candidate);
            if (other != f->entry && same(imported.name, candidate.name)) { r = result(8, 0); break; }
        }
        if (scan.status != MT_SD_OK) r = scan;
    }
    if (r.status != MT_SD_OK) { free(f->image); f->image = saved; return r; }
    free(saved); f->prepared = true; return result(0, 0);
}
static mt_sd_result_t prepare(c64_uci_image_file_t *f)
{
    if (f->cvt) return prepare_cvt(f);
    uint16_t old[IMAGE_MAX_SECTORS]; size_t bytes, count;
    bool relative = (f->image[f->entry] & 7) == 4;
    size_t header = relative ? 2 : 0;
    if (relative && (f->size < 2 || !f->data[0] || f->data[0] > 254 || f->data[1])) return result(6, 0);
    mt_sd_result_t r = chain(f, f->entry, NULL, &bytes, old, &count);
    if (r.status != MT_SD_OK) return r;
    if (relative && f->image[f->entry + 19]) {
        uint16_t side[IMAGE_MAX_SECTORS]; size_t side_count;
        r = chain(f, f->entry + 18, NULL, &bytes, side, &side_count);
        if (r.status != MT_SD_OK || side_count > IMAGE_MAX_SECTORS - count) return result(1, 0);
        uint8_t visited[IMAGE_VISITED_BYTES] = {0};
        for (size_t i = 0; i < count; i++) visit(visited, old[i]);
        for (size_t i = 0; i < side_count; i++) {
            if (!visit(visited, side[i])) return result(1, 0);
            old[count++] = side[i];
        }
    }
    uint8_t *saved = f->image;
    f->image = malloc(f->bytes);
    if (!f->image) { f->image = saved; return result(17, 0); }
    memcpy(f->image, saved, f->bytes);
    for (size_t i = 0; i < count; i++) { unsigned t, s; ts(f, old[i], &t, &s); allocation(f, t, s, false); }
    uint8_t *e = f->image + f->entry;
    e[1] = e[2] = 0;
    size_t payload = f->size - header;
    size_t nblocks = (payload + 253) / 254, previous = SIZE_MAX;
    if (!nblocks) nblocks = 1;
    for (size_t i = 0; i < nblocks; i++) {
        unsigned t, s;
        if (!next_free(f, &t, &s, false)) { free(f->image); f->image = saved; return result(7, 0); }
        allocation(f, t, s, true);
        size_t block = lba(f, t, s); uint8_t *p = f->image + block * 256;
        memset(p, 0, 256);
        if (previous == SIZE_MAX) { e[1] = (uint8_t)t; e[2] = (uint8_t)s; }
        else { f->image[previous * 256] = (uint8_t)t; f->image[previous * 256 + 1] = (uint8_t)s; }
        size_t n = payload - i * 254; if (n > 254) n = 254;
        if (n) memcpy(p + 2, f->data + header + i * 254, n);
        p[1] = (uint8_t)(n + 1); previous = block; old[i] = (uint16_t)block;
    }
    size_t total = nblocks;
    if (relative) {
        size_t sides = (nblocks + 119) / 120, extra = f->sectors > 720 ? 1 : 0;
        uint16_t side[(IMAGE_MAX_SECTORS + 119) / 120 + 1];
        if (sides + extra > sizeof side / sizeof side[0]) { free(f->image); f->image = saved; return result(1, 0); }
        for (size_t i = 0; i < sides + extra; i++) {
            unsigned t, s;
            if (!next_free(f, &t, &s, false)) { free(f->image); f->image = saved; return result(7, 0); }
            allocation(f, t, s, true); side[i] = (uint16_t)lba(f, t, s);
            memset(f->image + side[i] * 256, 0, 256);
        }
        unsigned t, s; ts(f, side[0], &t, &s); e[19] = (uint8_t)t; e[20] = (uint8_t)s; e[21] = f->data[0];
        for (size_t i = 0; i < sides; i++) {
            uint8_t *p = f->image + side[i + extra] * 256;
            size_t group = (i / 6) * 6;
            p[2] = (uint8_t)(i % 6); p[3] = f->data[0];
            for (size_t j = group; j < group + 6 && j < sides; j++) {
                ts(f, side[j + extra], &t, &s);
                p[4 + (j - group) * 2] = (uint8_t)t; p[5 + (j - group) * 2] = (uint8_t)s;
            }
            size_t n = nblocks - i * 120; if (n > 120) n = 120;
            for (size_t j = 0; j < n; j++) {
                ts(f, old[i * 120 + j], &t, &s);
                p[16 + j * 2] = (uint8_t)t; p[17 + j * 2] = (uint8_t)s;
            }
            if (i + 1 < sides) { ts(f, side[i + 1 + extra], &t, &s); p[0] = (uint8_t)t; p[1] = (uint8_t)s; }
            else p[1] = (uint8_t)(n * 2 + 15);
        }
        if (extra) {
            uint8_t *p = f->image + side[0] * 256;
            ts(f, side[1], &t, &s); p[0] = (uint8_t)t; p[1] = (uint8_t)s; p[2] = 254;
            for (size_t i = 0; i < sides; i += 6) {
                ts(f, side[i + 1], &t, &s); p[3 + (i / 6) * 2] = (uint8_t)t; p[4 + (i / 6) * 2] = (uint8_t)s;
            }
        }
        total += sides + extra;
    }
    e[28] = (uint8_t)total; e[29] = (uint8_t)(total >> 8); e[0] |= 0x80;
    free(saved); f->prepared = true; return result(0, 0);
}
mt_sd_result_t c64_uci_image_close(c64_uci_image_file_t **fp)
{
    c64_uci_image_file_t *f = *fp;
    if (!f) return result(0, 0);
    if (f->changed && !f->prepared) {
        mt_sd_result_t r = prepare(f);
        if (r.status != MT_SD_OK) {
            /* Preparation is only in RAM. A full/malformed image must not
             * become an unrecoverable owner that can never be closed. */
            f->changed = false;
            mt_sd_result_t closed = c64_uci_image_close(fp);
            return closed.status == MT_SD_OK ? r : closed;
        }
    }
    if (f->owner) {
        mt_sd_result_t r = mt_sd_file_close(f->owner);
        if (r.status == MT_SD_OK || mt_sd_file_tell(f->owner) < 0) f->owner = NULL;
        if (r.status != MT_SD_OK) return r;
    }
    if (f->changed) {
        char journal[512]; strcpy(journal, f->path); strcat(journal, ".mtj");
        f->committing = true;
        mt_sd_result_t r = mt_sd_storage_journal_replace(f->path, journal, f->image, f->bytes);
        if (r.status != MT_SD_OK) return r;
    }
    dispose(f); *fp = NULL; return result(0, 0);
}
mt_sd_result_t c64_uci_image_abort(c64_uci_image_file_t **fp)
{
    if (!*fp) return result(0, 0);
    if ((*fp)->committing) return result(16, 0);
    (*fp)->changed = (*fp)->prepared = false;
    return c64_uci_image_close(fp);
}
mt_sd_result_t c64_uci_image_directory(const char *path, c64_uci_image_file_t **out)
{
    *out = NULL;
    char image[512]; const char *member;
    if (!c64_uci_image_path(path, image, &member)) return result(5, 0);
    mt_sd_result_t r = load(image, false, out);
    if (r.status == MT_SD_OK) r = select_directory(*out, member, strlen(member));
    if (r.status == MT_SD_OK) { (*out)->listing = true; (*out)->volume = true; }
    else if (*out) {
        mt_sd_result_t closed = c64_uci_image_close(out);
        if (closed.status != MT_SD_OK) return closed;
    }
    return r;
}
mt_sd_result_t c64_uci_image_next(c64_uci_image_file_t *f, mt_sd_info_t *info)
{
    memset(info, 0, sizeof *info);
    if (!f || !f->listing) return result(9, 0);
    if (f->volume) {
        const uint8_t *root = f->image + f->header * 256;
        unsigned offset = f->header != root_header(f) ? 4 : f->label_offset;
        encode_name(root + offset, info->name);
        if (!info->name[0]) strcpy(info->name, "UNTITLED");
        info->attributes = 8; f->volume = false; return result(0, 1);
    }
    size_t entry;
    mt_sd_result_t r = next_entry(f, &entry);
    if (r.status == MT_SD_OK && r.size) entry_info(f, entry, info);
    return r;
}
mt_sd_result_t c64_uci_image_header(c64_uci_image_file_t *f, char name[17], char id[6])
{
    if (!f || !f->listing || !name || !id) return result(9, 0);
    const uint8_t *root = f->image + f->header * 256;
    unsigned offset = f->header != root_header(f) ? 4 : f->label_offset;
    memcpy(name, root + offset, 16); name[16] = 0;
    size_t n = 16; while (n && (uint8_t)name[n - 1] == 0xa0) name[--n] = 0;
    memcpy(id, root + offset + 18, 2); id[2] = ' ';
    memcpy(id + 3, root + offset + 21, 2); id[5] = 0;
    for (unsigned i = 0; i < 5; i++) if (!id[i] || (uint8_t)id[i] == 0xa0) id[i] = ' ';
    return result(0, 0);
}
mt_sd_result_t c64_uci_image_stat(const char *path, mt_sd_info_t *info)
{
    char image[512]; const char *member;
    if (!c64_uci_image_path(path, image, &member)) return result(4, 0);
    if (!*member) {
        mt_sd_result_t r = mt_sd_file_stat(image, info);
        if (r.status == MT_SD_OK) info->attributes |= 16;
        return r;
    }
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, false, &f);
    size_t entry;
    if (r.status == MT_SD_OK) r = find_entry(f, member, &entry);
    if (r.status == MT_SD_OK) entry_info(f, entry, info);
    mt_sd_result_t closed = c64_uci_image_close(&f);
    if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return closed; }
    return r;
}
mt_sd_result_t c64_uci_image_info(c64_uci_image_file_t *f, mt_sd_info_t *info)
{
    if (!f || f->listing) return result(9, 0);
    entry_info(f, f->entry, info);
    return result(0, 0);
}
mt_sd_result_t c64_uci_image_remove(const char *path)
{
    char image[512]; const char *member;
    if (!c64_uci_image_path(path, image, &member) || !*member) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, true, &f);
    size_t entry;
    if (r.status == MT_SD_OK) r = find_entry(f, member, &entry);
    unsigned type = r.status == MT_SD_OK ? f->image[entry] & 7 : 0;
    if (r.status == MT_SD_OK && ((f->image[entry] & 0x40) || type == 5)) r = result(10, 0);
    if (r.status == MT_SD_OK && type == 6) {
        r = select_directory(f, member, strlen(member));
        size_t child;
        if (r.status == MT_SD_OK) {
            r = next_entry(f, &child);
            if (r.status == MT_SD_OK && r.size) r = result(7, 0);
        }
    }
    uint16_t blocks[IMAGE_MAX_SECTORS]; size_t bytes, count;
    if (r.status == MT_SD_OK && geos_entry(f->image + entry)) {
        cvt_blocks_t list = {f, blocks, 0}; mt_c64_uci_cvt_ops_t ops = cvt_ops(f);
        r = cvt_result(mt_c64_uci_cvt_walk(f->image + entry, &ops, collect_cvt_block, &list));
        count = list.count;
    } else if (r.status == MT_SD_OK) r = chain(f, entry, NULL, &bytes, blocks, &count);
    if (r.status == MT_SD_OK && type == 4 && f->image[entry + 19]) {
        uint16_t side[IMAGE_MAX_SECTORS]; size_t side_count;
        r = chain(f, entry + 18, NULL, &bytes, side, &side_count);
        if (r.status == MT_SD_OK && side_count > IMAGE_MAX_SECTORS - count) r = result(1, 0);
        uint8_t visited[IMAGE_VISITED_BYTES] = {0};
        for (size_t i = 0; i < count; i++) visit(visited, blocks[i]);
        if (r.status == MT_SD_OK) for (size_t i = 0; i < side_count; i++) {
            if (!visit(visited, side[i])) { r = result(1, 0); break; }
            blocks[count++] = side[i];
        }
    }
    if (r.status == MT_SD_OK) {
        for (size_t i = 0; i < count; i++) { unsigned t, s; ts(f, blocks[i], &t, &s); allocation(f, t, s, false); }
        f->image[entry] = 0; f->changed = f->prepared = true;
    }
    mt_sd_result_t closed = c64_uci_image_close(&f);
    if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return closed; }
    return r;
}
mt_sd_result_t c64_uci_image_mkdir(const char *path)
{
    char image[512]; const char *member;
    if (!c64_uci_image_path(path, image, &member) || !*member) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, true, &f);
    size_t entry = 0;
    if (r.status == MT_SD_OK) {
        r = find_entry(f, member, &entry);
        if (r.status == MT_SD_OK) r = result(8, 0);
        else if (r.backend_status == 4) r = create_entry(f, member, true, &entry);
    }
    if (r.status == MT_SD_OK) {
        unsigned ht, hs, dt, ds;
        if (!next_free(f, &ht, &hs, false)) r = result(7, 0);
        else {
            allocation(f, ht, hs, true);
            if (!next_free(f, &dt, &ds, false)) r = result(7, 0);
            else {
                allocation(f, dt, ds, true);
                uint8_t *h = f->image + lba(f, ht, hs) * 256;
                uint8_t *d = f->image + lba(f, dt, ds) * 256;
                uint8_t *e = f->image + entry;
                memset(h, 0, 256); memset(d, 0, 256);
                h[0] = (uint8_t)dt; h[1] = (uint8_t)ds; h[2] = 0x48;
                memcpy(h + 4, f->image + root_header(f) * 256 + f->label_offset, 27);
                memcpy(h + 4, e + 3, 16);
                unsigned parent_t, parent_s, dir_t, dir_s;
                ts(f, f->header, &parent_t, &parent_s);
                ts(f, entry / 256, &dir_t, &dir_s);
                h[32] = (uint8_t)ht; h[33] = (uint8_t)hs;
                h[34] = (uint8_t)parent_t; h[35] = (uint8_t)parent_s;
                h[36] = (uint8_t)dir_t; h[37] = (uint8_t)dir_s; h[38] = (uint8_t)((entry % 256) / 32);
                d[1] = 255; e[1] = (uint8_t)ht; e[2] = (uint8_t)hs; e[28] = 2;
                f->changed = f->prepared = true;
            }
        }
    }
    mt_sd_result_t closed = c64_uci_image_close(&f);
    if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return closed; }
    return r;
}
mt_sd_result_t c64_uci_image_rename(const char *from, const char *to)
{
    char a[512], b[512]; const char *source, *dest;
    if (!c64_uci_image_path(from, a, &source) || !c64_uci_image_path(to, b, &dest) || !same(a, b)) return result(6, 0);
    size_t n = strlen(source);
    if (strlen(dest) > n && dest[n] == '/') {
        bool child = true;
        for (size_t i = 0; i < n; i++) if (toupper((unsigned char)source[i]) != toupper((unsigned char)dest[i])) child = false;
        if (child) return result(7, 0);
    }
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(a, true, &f);
    size_t entry = 0, other;
    if (r.status == MT_SD_OK) r = find_entry(f, source, &entry);
    if (r.status == MT_SD_OK) {
        mt_sd_result_t exists = find_entry(f, dest, &other);
        if (exists.status == MT_SD_OK) r = result(8, 0);
        else if (exists.backend_status != 4) r = exists;
    }
    if (r.status == MT_SD_OK && (f->image[entry] & 0x40)) r = result(10, 0);
    if (r.status == MT_SD_OK && (f->image[entry] & 7) == 6) {
        const char *slash = strrchr(dest, '/');
        size_t header = lba(f, f->image[entry + 1], f->image[entry + 2]);
        r = select_directory_checked(f, dest, slash ? (size_t)(slash - dest) : 0, header);
    }
    if (r.status == MT_SD_OK) {
        uint8_t original[30]; memcpy(original, f->image + entry, 30);
        f->image[entry] = 0;
        r = create_entry(f, dest, (original[0] & 7) == 6, &other);
        if (r.status == MT_SD_OK) {
            uint8_t new_name[16]; memcpy(new_name, f->image + other + 3, 16);
            unsigned new_type = f->image[other] & 7;
            unsigned old_type = original[0] & 7;
            if (new_type != 7 && new_type != old_type &&
                (new_type == 4 || old_type == 4 || geos_entry(original))) r = result(12, 0);
            memcpy(f->image + other, original, 30); memcpy(f->image + other + 3, new_name, 16);
            if (new_type == 7) new_type = original[0] & 7;
            f->image[other] = (uint8_t)((original[0] & 0xf8) | new_type);
            if (new_type == 6) {
                size_t header = lba(f, original[1], original[2]);
                if (header == SIZE_MAX) r = result(1, 0);
                else {
                    uint8_t *h = f->image + header * 256;
                    memcpy(h + 4, new_name, 16);
                    unsigned t, s; ts(f, f->header, &t, &s); h[34] = (uint8_t)t; h[35] = (uint8_t)s;
                    ts(f, other / 256, &t, &s); h[36] = (uint8_t)t; h[37] = (uint8_t)s; h[38] = (uint8_t)((other % 256) / 32);
                }
            }
            if (r.status == MT_SD_OK) f->changed = f->prepared = true;
        }
    }
    mt_sd_result_t closed = c64_uci_image_close(&f);
    if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return closed; }
    return r;
}
mt_sd_result_t c64_uci_image_copy(const char *from, const char *to)
{
    char a[512], b[512]; const char *source, *dest;
    if (!c64_uci_image_path(from, a, &source) || !c64_uci_image_path(to, b, &dest) || !same(a, b)) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(a, true, &f);
    size_t source_entry = 0, destination = 0, bytes = 0, count = 0;
    if (r.status == MT_SD_OK) r = find_entry(f, source, &source_entry);
    unsigned type = r.status == MT_SD_OK ? f->image[source_entry] & 7 : 0;
    if (r.status == MT_SD_OK && type > 4) r = result(7, 0);
    if (r.status == MT_SD_OK) {
        mt_sd_result_t exists = find_entry(f, dest, &destination);
        if (exists.status == MT_SD_OK) r = result(8, 0);
        else if (exists.backend_status != 4) r = exists;
    }
    if (r.status == MT_SD_OK && geos_entry(f->image + source_entry)) {
        mt_c64_uci_cvt_ops_t ops = cvt_ops(f);
        r = cvt_result(mt_c64_uci_cvt_export(f->image + source_entry, &ops, &f->data, &f->size));
        f->capacity = f->size;
    } else if (r.status == MT_SD_OK) {
        r = chain(f, source_entry, NULL, &bytes, NULL, &count);
        if (r.status == MT_SD_OK) {
            size_t header = type == 4 ? 2 : 0;
            f->size = f->capacity = bytes + header;
            f->data = malloc(f->capacity ? f->capacity : 1);
            if (!f->data) r = result(17, 0);
            else {
                r = chain(f, source_entry, f->data + header, &bytes, NULL, &count);
                if (header) { f->data[0] = f->image[source_entry + 21]; f->data[1] = 0; }
            }
        }
    }
    if (r.status == MT_SD_OK) r = create_entry(f, dest, false, &f->entry);
    if (r.status == MT_SD_OK) {
        f->changed = true; f->cvt = (f->image[f->entry] & 7) == 7;
        if (f->cvt && geos_entry(f->image + source_entry) && f->size >= 30)
            memcpy(f->data + 3, f->image + f->entry + 3, 16);
    }
    mt_sd_result_t closed = c64_uci_image_close(&f);
    if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return closed; }
    return r;
}
static mt_sd_result_t finish_operation(c64_uci_image_file_t **f, mt_sd_result_t r)
{
    mt_sd_result_t closed = c64_uci_image_close(f);
    if (closed.status != MT_SD_OK) {
        if (*f) (*f)->orphan = true;
        c64_uci_environment.fault = true; return closed;
    }
    return r;
}
static bool valid_label(const char *label, const char *id)
{
    return (!label || strlen(label) <= 16) && (!id || !*id || strlen(id) == 2 || strlen(id) == 3);
}
static void set_label(c64_uci_image_file_t *f, const char *label, const char *id)
{
    uint8_t *root = f->image + root_header(f) * 256;
    uint8_t *p = root + f->label_offset;
    if (label) {
        memset(p, 0xa0, 16);
        for (size_t i = 0; label[i]; i++) p[i] = (uint8_t)toupper((unsigned char)label[i]);
    }
    if (id && *id) {
        p[16] = p[17] = p[20] = 0xa0;
        for (unsigned i = 0; i < 2; i++) p[18 + i] = (uint8_t)toupper((unsigned char)id[i]);
        if (f->type == 81) {
            for (unsigned sector = 1; sector <= 2; sector++)
                memcpy(f->image + lba(f, 40, sector) * 256 + 4, p + 18, 2);
        } else if (f->type == IMAGE_DNP) memcpy(f->image + 2U * 256U + 4, p + 18, 2);
    }
}
mt_sd_result_t c64_uci_image_label(const char *path, const char *label, const char *id)
{
    char image[512]; const char *member;
    if (!valid_label(label, id) || !c64_uci_image_path(path, image, &member)) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, true, &f);
    if (r.status == MT_SD_OK) {
        set_label(f, label, id); f->changed = f->prepared = true;
    }
    return finish_operation(&f, r);
}
mt_sd_result_t c64_uci_image_attributes(const char *path, uint8_t attributes, uint8_t mask)
{
    if (mask & ~1U) return result(12, 0);
    char image[512]; const char *member;
    if (!c64_uci_image_path(path, image, &member) || !*member) return result(6, 0);
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load(image, true, &f);
    size_t entry;
    if (r.status == MT_SD_OK) r = find_entry(f, member, &entry);
    if (r.status == MT_SD_OK && (mask & 1)) {
        f->image[entry] = (uint8_t)((f->image[entry] & ~0x40U) | ((attributes & 1) ? 0x40U : 0));
        f->changed = f->prepared = true;
    }
    return finish_operation(&f, r);
}
mt_sd_result_t c64_uci_image_format(const char *path, const char *label, const char *id)
{
    char image[512]; const char *member;
    if (!label || !valid_label(label, id)) return result(6, 0);
    if (!c64_uci_image_path(path, image, &member)) {
        if (!path || strlen(path) + 5 > sizeof image) return result(6, 0);
        strcpy(image, path);
    }
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load_image(image, true, false, &f);
    if (r.backend_status == 4 && id && *id) {
        const char *ext = strrchr(image, '.');
        unsigned type = ext && same(ext, ".D64") ? 64 : ext && same(ext, ".D71") ? 71 :
                        ext && same(ext, ".D81") ? 81 : ext && same(ext, ".DNP") ? IMAGE_DNP : 0;
        if (!type) return result(6, 0);
        unsigned dnp_tracks = 0;
        if (type == IMAGE_DNP) {
            if (strlen(id) != 3 || strspn(id, "0123456789") != 3) return result(6, 0);
            dnp_tracks = (unsigned)strtoul(id, NULL, 10);
            if (!dnp_tracks || dnp_tracks > 255) return result(19, 0);
        }
        f = calloc(1, sizeof *f);
        if (!f) return result(17, 0);
        f->type = type; f->tracks = type == IMAGE_DNP ? dnp_tracks : type == 64 ? 35 : type == 71 ? 70 : 80;
        f->sectors = type == IMAGE_DNP ? dnp_tracks * 256U : type == 64 ? 683 : type == 71 ? 1366 : 3200;
        f->bytes = f->sectors * 256U; f->image = calloc(1, f->bytes);
        if (!f->image) { free(f); return result(17, 0); }
        f->directory_track = type == IMAGE_DNP ? 1 : type == 81 ? 40 : 18;
        f->label_offset = type == IMAGE_DNP || type == 81 ? 4 : 144;
        f->flags = 3; strcpy(f->path, image); f->next = owners; owners = f;
        r = result(0, 0);
        /* Ultimate N:NAME.DNP,ddd allocates an unformatted native image.
         * Formatting after CD into it is a separate DOS N: operation. */
        if (type == IMAGE_DNP) {
            f->changed = f->prepared = true;
            return finish_operation(&f, r);
        }
    }
    if (r.status == MT_SD_OK) {
        uint8_t *root = f->image + root_header(f) * 256;
        uint8_t old_id[3]; memcpy(old_id, root + f->label_offset + 18, 3);
        memset(root, 0, 256);
        if (f->type == 71) memset(f->image + lba(f, 53, 0) * 256, 0, 256);
        if (f->type == 81) memset(f->image + lba(f, 40, 1) * 256, 0, 512);
        if (f->type == IMAGE_DNP) memset(f->image + 2U * 256U, 0, 8192);
        /* Formatting produces the standard format; undocumented extended BAM
         * bytes are not guessed, and extra-track contents remain intact. */
        f->extended_bam = 0; f->label_offset = f->type == 81 || f->type == IMAGE_DNP ? 4 : 144;
        root[0] = (uint8_t)f->directory_track; root[1] = f->type == IMAGE_DNP ? 34 : f->type == 81 ? 3 : 1;
        root[2] = f->type == IMAGE_DNP ? 0x48 : f->type == 81 ? 0x44 : 0x41;
        if (f->type == 64 || f->type == 71) root[3] = f->type == 71 ? 0x80 : 0x20;
        memset(root + f->label_offset, 0xa0, 27);
        memcpy(root + f->label_offset + 18, old_id, 3);
        memcpy(root + f->label_offset + 21, f->type == IMAGE_DNP ? "1H" : f->type == 81 ? "3D" : "2A", 2);
        unsigned tracks = f->type == 64 ? 35 : f->tracks;
        for (unsigned t = 1; t <= tracks; t++)
            for (unsigned s = 0; s < spt(f, t); s++) allocation(f, t, s, false);
        allocation(f, f->directory_track, 0, true);
        allocation(f, f->directory_track, root[1], true);
        if (f->type == 71)
            for (unsigned s = 0; s < 19; s++) allocation(f, 53, s, true);
        if (f->type == 81) {
            allocation(f, 40, 1, true); allocation(f, 40, 2, true);
            uint8_t *b = f->image + lba(f, 40, 1) * 256;
            b[0] = 40; b[1] = 2; b[2] = 0x44; b[3] = 0xbb;
            memcpy(b + 4, old_id, 2); b[6] = 0xc0;
            memcpy(b + 256, b, 6); b[256] = 0; b[257] = 255;
        }
        if (f->type == IMAGE_DNP) {
            for (unsigned s = 0; s <= 34; s++) allocation(f, 1, s, true);
            root[32] = 1; root[33] = 1;
            uint8_t *b = f->image + 2U * 256U;
            b[2] = 0x48; b[3] = 0xb7; b[6] = 0xc0; b[8] = (uint8_t)f->tracks;
            memcpy(b + 4, old_id, 2);
        }
        uint8_t *d = f->image + lba(f, f->directory_track, root[1]) * 256;
        memset(d, 0, 256); d[1] = 255;
        set_label(f, label, id); f->changed = f->prepared = true;
    }
    return finish_operation(&f, r);
}
int c64_uci_image_sector(const char *path, unsigned operation, unsigned *track,
                         unsigned *sector, uint8_t data[256])
{
    char image[512]; const char *member;
    if (operation > 3 || !track || !sector || !c64_uci_image_path(path, image, &member) || *member) return 74;
    c64_uci_image_file_t *f = NULL;
    mt_sd_result_t r = load_image(image, operation != 0, false, &f);
    if (r.status != MT_SD_OK) {
        mt_sd_result_t closed = c64_uci_image_close(&f);
        if (closed.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; }
        return r.backend_status == 10 ? 26 : r.backend_status == 16 ? 70 : 74;
    }
    size_t block = lba(f, *track, *sector); int code = 0;
    if (block == SIZE_MAX) code = 66;
    else if (!operation) memcpy(data, f->image + block * 256, 256);
    else if (operation == 1) { memcpy(f->image + block * 256, data, 256); f->changed = f->prepared = true; }
    else if (operation >= 2 && f->type == 64 && *track > 35 &&
             (!f->extended_bam || *track > 40)) code = 74;
    else if (operation == 2 && allocated(f, *track, *sector)) {
        code = 65;
        if (!next_free(f, track, sector, false)) *track = *sector = 0;
    } else { allocation(f, *track, *sector, operation == 2); f->changed = f->prepared = true; }
    r = c64_uci_image_close(&f);
    if (r.status != MT_SD_OK) { if (f) f->orphan = true; c64_uci_environment.fault = true; return 25; }
    return code;
}

int c64_uci_image_retry(void)
{
    c64_uci_image_file_t *f = owners;
    while (f) {
        c64_uci_image_file_t *next = f->next;
        if (f->orphan) {
            mt_sd_result_t r = c64_uci_image_close(&f);
            if (r.status != MT_SD_OK) return -1;
        }
        f = next;
    }
    return 0;
}
