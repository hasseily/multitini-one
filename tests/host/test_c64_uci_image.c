/* SPDX-License-Identifier: GPL-3.0-only */
#define F_CAP 8
#define F_BYTES (255U * 65536U)
#define UCI_STORAGE_CUSTOM_JOURNAL
#include "c64_uci_storage_fixture.h"
#include "c64_uci_image.h"
#include "c64_uci_internal.h"
#include <stdlib.h>

c64_uci_environment_t c64_uci_environment;
static bool deny_image, journal_failure, valid_recovery;
static unsigned journal_writes, journal_recovers;
bool c64_uci_protected(const char *path, bool writing)
{
    (void)path; (void)writing; return deny_image;
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal,
                                             const uint8_t *bytes, size_t size)
{
    journal_writes++;
    assert(strstr(journal, ".mtj"));
    if (journal_failure) return (mt_sd_result_t){MT_SD_SYNC_FAILED, 1, 0};
    fixture_file *f = fixture_find(path);
    if (!f) f = fixture_add(path, false, bytes, size);
    assert(f && size <= F_BYTES);
    memcpy(f->bytes, bytes, size); f->size = size;
    return (mt_sd_result_t){MT_SD_OK, 0, size};
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal,
                                             uint8_t *bytes, size_t size)
{
    journal_recovers++;
    if (valid_recovery) {
        fixture_file *saved = fixture_find(journal);
        assert(saved && saved->size <= size);
        memcpy(bytes, saved->bytes, saved->size);
        mt_sd_result_t r = mt_sd_storage_journal_replace(path, journal, bytes, saved->size);
        saved->exists = false;
        return r;
    }
    return (mt_sd_result_t){MT_SD_JOURNAL_INVALID, 1, 0};
}
static unsigned sectors(unsigned type, unsigned track)
{
    if (type == 100) return 256;
    if (type == 81) return 40;
    if (type == 71 && track > 35) track -= 35;
    return track <= 17 ? 21 : track <= 24 ? 19 : track <= 30 ? 18 : 17;
}
static unsigned offset(unsigned type, unsigned track, unsigned sector)
{
    unsigned n = sector;
    for (unsigned t = 1; t < track; t++) n += sectors(type, t);
    return n * 256;
}
static void allocated(uint8_t *image, unsigned type, unsigned track, unsigned sector)
{
    if (type == 100) {
        uint8_t *byte = image + 512 + track * 32 + sector / 8;
        uint8_t bit = (uint8_t)(128U >> (sector % 8));
        assert(*byte & bit); *byte &= (uint8_t)~bit; return;
    }
    uint8_t *count, *bits, *root = image + offset(type, type == 81 ? 40 : 18, 0);
    if (type == 81) {
        count = image + offset(type, 40, track > 40 ? 2 : 1) + 16 + 6 * ((track - 1) % 40);
        bits = count + 1;
    } else if (type == 71 && track > 35) {
        count = root + 0xdd + track - 36;
        bits = image + offset(type, 53, 0) + 3 * (track - 36);
    } else { count = root + track * 4; bits = count + 1; }
    assert(bits[sector / 8] & (1U << (sector % 8)));
    bits[sector / 8] &= (uint8_t)~(1U << (sector % 8)); --*count;
}
static fixture_file *format(unsigned type)
{
    fixture_reset();
    memset(directories, 0, sizeof directories);
    deny_image = journal_failure = valid_recovery = false;
    journal_writes = journal_recovers = 0;
    file_close_failures = file_close_released_failures = 0;
    memset(&c64_uci_environment, 0, sizeof c64_uci_environment);
    size_t size = type == 100 ? 16U * 65536U : type == 64 ? 174848 : type == 71 ? 349696 : 819200;
    uint8_t *bytes = calloc(size, 1); assert(bytes);
    if (type == 100) {
        /* Independent fixture from pinned FileSystemDNP::format. */
        bytes[256] = 1; bytes[257] = 34; bytes[258] = 0x48;
        memset(bytes + 260, 0xa0, 27); memcpy(bytes + 260, "SYNTHETIC", 9);
        memcpy(bytes + 278, "ID", 2); memcpy(bytes + 281, "1H", 2);
        bytes[288] = bytes[289] = 1;
        bytes[514] = 0x48; bytes[515] = 0xb7; bytes[518] = 0xc0; bytes[520] = 16;
        memcpy(bytes + 516, "ID", 2);
        memset(bytes + 544, 255, (size / 256) / 8);
        for (unsigned s = 0; s <= 34; s++) allocated(bytes, type, 1, s);
        bytes[34 * 256 + 1] = 255;
        fixture_file *f = fixture_add("0:/media/disk.dnp", false, bytes, size);
        free(bytes); return f;
    }
    unsigned tracks = type == 64 ? 35 : type == 71 ? 70 : 80;
    unsigned roottrack = type == 81 ? 40 : 18, firstdir = type == 81 ? 3 : 1;
    uint8_t *root = bytes + offset(type, roottrack, 0);
    root[0] = (uint8_t)roottrack; root[1] = (uint8_t)firstdir; root[2] = type == 81 ? 0x44 : 0x41;
    memset(root + (type == 81 ? 4 : 144), 0xa0, 16);
    memcpy(root + (type == 81 ? 4 : 144), "SYNTHETIC", 9);
    for (unsigned t = 1; t <= tracks; t++) {
        uint8_t *count, *bits;
        if (type == 81) { count = bytes + offset(type, 40, t > 40 ? 2 : 1) + 16 + 6 * ((t - 1) % 40); bits = count + 1; }
        else if (type == 71 && t > 35) { count = root + 0xdd + t - 36; bits = bytes + offset(type, 53, 0) + 3 * (t - 36); }
        else { count = root + t * 4; bits = count + 1; }
        *count = (uint8_t)sectors(type, t);
        for (unsigned s = 0; s < sectors(type, t); s++) bits[s / 8] |= (uint8_t)(1U << (s % 8));
    }
    allocated(bytes, type, roottrack, 0); allocated(bytes, type, roottrack, firstdir);
    if (type == 81) { allocated(bytes, type, 40, 1); allocated(bytes, type, 40, 2); }
    if (type == 71) for (unsigned s = 0; s < 19; s++) allocated(bytes, type, 53, s);
    bytes[offset(type, roottrack, firstdir) + 1] = 255;
    char path[64]; snprintf(path, sizeof path, "0:/media/disk.d%u", type);
    fixture_file *f = fixture_add(path, false, bytes, size); free(bytes); return f;
}
static void happy(unsigned type)
{
    fixture_file *disk = format(type);
    char member[524]; snprintf(member, sizeof member, "%s/HELLO.PRG", disk->path);
    uint8_t data[777], readback[900];
    for (unsigned i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 43U);
    c64_uci_image_file_t *f = NULL, *other = NULL;
    assert(c64_uci_image_open(member, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, data, sizeof data).size == sizeof data);
    assert(c64_uci_image_open(member, 1, &other).status == MT_SD_FILE_BUSY && !other);
    assert(c64_uci_image_close(&f).status == MT_SD_OK && !f && journal_writes == 1);
    assert(c64_uci_image_open(member, 9, &f).backend_status == 6 && !f);
    assert(c64_uci_image_open(member, 17, &f).backend_status == 6 && !f);
    assert(c64_uci_image_open(member, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, sizeof readback).size == sizeof data);
    assert(!memcmp(data, readback, sizeof data));
    assert(c64_uci_image_seek(f, 251).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, 16).size == 16 && !memcmp(readback, data + 251, 16));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    char copied[524]; snprintf(copied, sizeof copied, "%s/COPY.PRG", disk->path);
    assert(c64_uci_image_copy(member, copied).status == MT_SD_OK);
    assert(c64_uci_image_open(copied, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, sizeof readback).size == sizeof data);
    assert(!memcmp(data, readback, sizeof data));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_remove(copied).status == MT_SD_OK);
    assert(c64_uci_image_open(member, 6, &f).status == MT_SD_FILE_EXISTS && !f);
    mt_sd_info_t info;
    assert(c64_uci_image_stat(member, &info).status == MT_SD_OK && !strcmp(info.name, "HELLO.PRG"));
    assert(c64_uci_image_directory(disk->path, &f).status == MT_SD_OK);
    assert(c64_uci_image_next(f, &info).size == 1 && info.attributes == 8 && !strcmp(info.name, "SYNTHETIC"));
    assert(c64_uci_image_next(f, &info).size == 1 && !strcmp(info.name, "HELLO.PRG"));
    assert(c64_uci_image_next(f, &info).size == 0 && !info.name[0]);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    char renamed[524]; snprintf(renamed, sizeof renamed, "%s/RENAMED.PRG", disk->path);
    assert(c64_uci_image_rename(member, renamed).status == MT_SD_OK);
    assert(c64_uci_image_stat(member, &info).backend_status == 4);
    assert(c64_uci_image_open(renamed, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, sizeof readback).size == sizeof data && !memcmp(data, readback, sizeof data));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    /* CREATE_ALWAYS truncates the existing member without retaining its old payload. */
    assert(c64_uci_image_open(renamed, 10, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "new", 3).size == 3);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_open(renamed, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, sizeof readback).size == 3 && !memcmp(readback, "new", 3));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_remove(renamed).status == MT_SD_OK);
    assert(c64_uci_image_stat(renamed, &info).backend_status == 4);
    /* Native image folders and relative-record export/import use Ultimate's
     * two-byte record-length header; side sectors remain ordinary CBM metadata. */
    char folder[524], relpath[544];
    snprintf(folder, sizeof folder, "%s/SUBDIR", disk->path);
    snprintf(relpath, sizeof relpath, "%s/RECORDS.REL", folder);
    assert(c64_uci_image_mkdir(folder).status == MT_SD_OK);
    char invalid[560]; snprintf(invalid, sizeof invalid, "%s/*/LOOP", disk->path);
    assert(c64_uci_image_rename(folder, invalid).status != MT_SD_OK);
    assert(c64_uci_image_open(relpath, 6, &f).status == MT_SD_OK);
    uint8_t records[513] = {17, 0};
    for (unsigned i = 2; i < sizeof records; i++) records[i] = (uint8_t)i;
    assert(c64_uci_image_write(f, records, sizeof records).size == sizeof records);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    snprintf(invalid, sizeof invalid, "%s/RECORDS.PRG", folder);
    assert(c64_uci_image_rename(relpath, invalid).backend_status == 12);
    assert(c64_uci_image_open(relpath, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_read(f, readback, sizeof readback).size == sizeof records);
    assert(!memcmp(readback, records, sizeof records));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_directory(folder, &f).status == MT_SD_OK);
    assert(c64_uci_image_next(f, &info).size == 1 && !strcmp(info.name, "SUBDIR"));
    assert(c64_uci_image_next(f, &info).size == 1 && !strcmp(info.name, "RECORDS.REL"));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_remove(folder).status != MT_SD_OK);
    assert(c64_uci_image_remove(relpath).status == MT_SD_OK);
    assert(c64_uci_image_remove(folder).status == MT_SD_OK);
    unsigned block_track = type == 100 ? 2 : 1, t = block_track, s = 0;
    assert(c64_uci_image_sector(disk->path, 2, &t, &s, readback) == 0);
    t = block_track; s = 0;
    assert(c64_uci_image_sector(disk->path, 2, &t, &s, readback) == 65 && (t != block_track || s != 0));
    t = block_track; s = 0;
    assert(c64_uci_image_sector(disk->path, 1, &t, &s, data) == 0);
    assert(c64_uci_image_sector(disk->path, 0, &t, &s, readback) == 0 && !memcmp(data, readback, 256));
    assert(c64_uci_image_sector(disk->path, 3, &t, &s, readback) == 0);
    t = 0; assert(c64_uci_image_sector(disk->path, 0, &t, &s, readback) == 66);
    deny_image = true;
    assert(c64_uci_image_open(member, 6, &f).status == MT_SD_FILE_BUSY && !f);
    deny_image = false;
}
static void failures(void)
{
    fixture_file *disk = format(64);
    c64_uci_image_file_t *f = NULL;
    mt_sd_info_t info;
    const char *path = "0:/media/disk.d64/FILE.PRG";
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "abc", 3).size == 3);
    journal_failure = true;
    assert(c64_uci_image_close(&f).status == MT_SD_SYNC_FAILED && f);
    c64_uci_image_file_t *other = NULL;
    assert(c64_uci_image_open(path, 1, &other).status == MT_SD_FILE_BUSY && !other);
    assert(c64_uci_image_abort(&f).status == MT_SD_FILE_BUSY && f);
    journal_failure = false;
    assert(c64_uci_image_close(&f).status == MT_SD_OK && !f);
    const char *aborted = "0:/media/disk.d64/ABORTED.PRG";
    assert(c64_uci_image_open(aborted, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "partial", 7).size == 7);
    file_close_failures = 1;
    assert(c64_uci_image_abort(&f).status == MT_SD_CLOSE_FAILED && f);
    assert(c64_uci_image_abort(&f).status == MT_SD_OK && !f);
    assert(c64_uci_image_stat(aborted, &info).backend_status == 4);
    file_close_failures = 1;
    assert(c64_uci_image_stat(path, &info).status == MT_SD_CLOSE_FAILED);
    assert(c64_uci_image_retry() == 0);
    c64_uci_environment.fault = false;
    /* Loop in a file chain is rejected before copying any bytes. */
    uint8_t *entry = disk->bytes + offset(64, 18, 1) + 2;
    uint8_t *block = disk->bytes + offset(64, entry[1], entry[2]);
    block[0] = entry[1]; block[1] = entry[2];
    assert(c64_uci_image_open(path, 1, &f).status != MT_SD_OK && !f);
    /* Directory links are checked independently, including repeated sectors. */
    uint8_t *dir = disk->bytes + offset(64, 18, 1);
    dir[0] = 18; dir[1] = 1;
    assert(c64_uci_image_directory(disk->path, &f).status == MT_SD_OK);
    assert(c64_uci_image_next(f, &info).status == MT_SD_OK);
    assert(c64_uci_image_next(f, &info).status == MT_SD_OK);
    assert(c64_uci_image_next(f, &info).status != MT_SD_OK);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    format(64);
    disk = fixture_find("0:/media/disk.d64");
    disk->bytes[offset(64, 18, 0) + 2] = 0x3c;
    assert(c64_uci_image_open(path, 6, &f).backend_status == 10 && !f);
    disk->size--;
    assert(c64_uci_image_directory(disk->path, &f).backend_status == 13 && !f);
    disk = format(64);
    memset(disk->bytes + offset(64, 18, 0) + 4, 0, 140);
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "full", 4).size == 4);
    assert(c64_uci_image_close(&f).status != MT_SD_OK && !f);
    assert(c64_uci_image_stat(path, &info).backend_status == 4 && journal_writes == 0);
}
static void extended_images(void)
{
    for (unsigned tracks = 40; tracks <= 42; tracks += 2) {
        fixture_file *disk = format(64);
        disk->size = tracks == 40 ? 196608 : 205312;
        memset(disk->bytes + 174848, 0x5a, disk->size - 174848);
        c64_uci_image_file_t *f = NULL;
        assert(c64_uci_image_open("0:/media/disk.d64/EXT.PRG", 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "data", 4).size == 4);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        for (size_t i = 174848; i < disk->size; i++) assert(disk->bytes[i] == 0x5a);
    }
    /* The standard BAM is full, but an unambiguous Dolphin map permits
     * allocation on track36 without changing the disk label. */
    fixture_file *disk = format(64);
    disk->size = 196608;
    uint8_t *bam = disk->bytes + offset(64, 18, 0);
    memset(bam + 4, 0, 140);
    for (unsigned i = 0; i < 5; i++) {
        bam[0xac + i * 4] = 17;
        bam[0xad + i * 4] = bam[0xae + i * 4] = 255;
        bam[0xaf + i * 4] = 1;
    }
    c64_uci_image_file_t *f = NULL;
    assert(c64_uci_image_open("0:/media/disk.d64/EXT.PRG", 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "data", 4).size == 4);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(disk->bytes[offset(64, 18, 1) + 3] == 36);
    assert(bam[0xac] == 16 && !memcmp(bam + 144, "SYNTHETIC", 9));
}
static void escaped_names(void)
{
    format(64);
    static const char *const names[] = {"A{2F}B.PRG", "{}CON.PRG", "NAME.PRG{}.PRG", "{2E}HIDDEN.SEQ", "A{A0}B.USR", "{C1C2}.PRG"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[256]; snprintf(path, sizeof path, "0:/media/disk.d64/%s", names[i]);
        c64_uci_image_file_t *f = NULL;
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "x", 1).size == 1);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        mt_sd_info_t info;
        assert(c64_uci_image_stat(path, &info).status == MT_SD_OK && !strcmp(info.name, names[i]));
    }
}
static void geos_conversion(void)
{
    for (unsigned type = 0; type < 4; type++) {
        unsigned format_type = type == 0 ? 64 : type == 1 ? 71 : type == 2 ? 81 : 100;
        fixture_file *disk = format(format_type);
        uint8_t cvt[513] = {0};
        cvt[0] = 0x82; memset(cvt + 3, 0xa0, 16); memcpy(cvt + 3, "ORIGINAL", 8);
        cvt[21] = 0; cvt[22] = 6;
        strcpy((char *)cvt + 30, "PRG formatted GEOS file V1.0");
        memcpy(cvt + 508, "hello", 5);
        char path[540], original[540];
        snprintf(path, sizeof path, "%s/IMPORT.CVT", disk->path);
        snprintf(original, sizeof original, "%s/ORIGINAL.CVT", disk->path);
        c64_uci_image_file_t *f = NULL;
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, cvt, sizeof cvt).size == sizeof cvt);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        assert(c64_uci_image_open(original, 1, &f).status == MT_SD_OK);
        uint8_t out[1024];
        assert(c64_uci_image_read(f, out, sizeof out).size == sizeof cvt);
        assert(!memcmp(out + 30, cvt + 30, 27) && !memcmp(out + 508, "hello", 5));
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        /* Imported header names cannot overwrite or alias an existing member. */
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, cvt, sizeof cvt).size == sizeof cvt);
        assert(c64_uci_image_close(&f).status == MT_SD_FILE_EXISTS && !f);
        assert(c64_uci_image_copy(original, path).status == MT_SD_OK);
        assert(c64_uci_image_open(path, 1, &f).status == MT_SD_OK);
        assert(c64_uci_image_read(f, out, sizeof out).size == sizeof cvt);
        assert(!memcmp(out + 3, "IMPORT", 6) && !memcmp(out + 508, "hello", 5));
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        assert(c64_uci_image_remove(path).status == MT_SD_OK);
        assert(c64_uci_image_remove(original).status == MT_SD_OK);
        cvt[33] = '!';
        unsigned before = journal_writes;
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, cvt, sizeof cvt).size == sizeof cvt);
        assert(c64_uci_image_close(&f).status != MT_SD_OK && !f && journal_writes == before);
    }
}
static void image_metadata(void)
{
    for (unsigned type = 0; type < 3; type++) {
        unsigned kind = type == 0 ? 64 : type == 1 ? 71 : 81;
        fixture_file *disk = format(kind);
        char path[540]; snprintf(path, sizeof path, "%s/LOCK.PRG", disk->path);
        c64_uci_image_file_t *f = NULL; mt_sd_info_t info;
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "payload", 7).size == 7);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        assert(c64_uci_image_attributes(path, 1, 1).status == MT_SD_OK);
        assert(c64_uci_image_stat(path, &info).status == MT_SD_OK && (info.attributes & 1));
        assert(c64_uci_image_open(path, 2, &f).status != MT_SD_OK && !f);
        assert(c64_uci_image_remove(path).status != MT_SD_OK);
        assert(c64_uci_image_attributes(path, 0, 3).backend_status == 12);
        assert(c64_uci_image_attributes(path, 0, 1).status == MT_SD_OK);
        assert(c64_uci_image_label(disk->path, "NEW LABEL", "ID").status == MT_SD_OK);
        assert(c64_uci_image_open(path, 1, &f).status == MT_SD_OK);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        unsigned root = offset(kind, kind == 81 ? 40 : 18, 0), label = kind == 81 ? 4 : 144;
        assert(!memcmp(disk->bytes + root + label, "NEW LABEL", 9));
        assert(c64_uci_image_format(path, "FRESH", NULL).status == MT_SD_OK);
        assert(!memcmp(disk->bytes + root + label + 18, "ID", 2));
        assert(c64_uci_image_stat(path, &info).backend_status == 4);
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "again", 5).size == 5);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        assert(c64_uci_image_format(disk->path, "LONGER THAN SIXTEEN BYTES", "ID").backend_status == 6);
        char fresh[64]; snprintf(fresh, sizeof fresh, "0:/media/new.d%u", kind);
        assert(c64_uci_image_format(fresh, "CREATED", "42").status == MT_SD_OK);
        assert(c64_uci_image_directory(fresh, &f).status == MT_SD_OK);
        char label_text[17], id[6];
        assert(c64_uci_image_header(f, label_text, id).status == MT_SD_OK);
        assert(!strcmp(label_text, "CREATED") && !strcmp(id, kind == 81 ? "42 3D" : "42 2A"));
        assert(c64_uci_image_next(f, &info).size == 1 && !strcmp(info.name, "CREATED"));
        assert(c64_uci_image_next(f, &info).size == 0);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        snprintf(path, sizeof path, "%s/SUB", fresh);
        assert(c64_uci_image_mkdir(path).status == MT_SD_OK);
        assert(c64_uci_image_directory(path, &f).status == MT_SD_OK);
        assert(c64_uci_image_header(f, label_text, id).status == MT_SD_OK);
        assert(!strcmp(label_text, "SUB") && !strcmp(id, kind == 81 ? "42 3D" : "42 2A"));
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
        snprintf(path, sizeof path, "%s/NEW.PRG", fresh);
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "new", 3).size == 3);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
    }
}
static void corruption_and_recovery(void)
{
    fixture_file *disk = format(64);
    const char *path = "0:/media/disk.d64/A.PRG";
    c64_uci_image_file_t *f = NULL; mt_sd_info_t info;
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "saved", 5).size == 5);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    uint8_t *a = disk->bytes + offset(64, 18, 1) + 2;
    memcpy(a + 32, a, 30); a[35] = 'B';
    unsigned before = journal_writes;
    assert(c64_uci_image_open("0:/media/disk.d64/C.PRG", 6, &f).status != MT_SD_OK && !f);
    assert(journal_writes == before);
    a[32] = 0;
    uint8_t *bam = disk->bytes + offset(64, 18, 0);
    bam[4 * a[1] + 1 + a[2] / 8] |= (uint8_t)(1U << (a[2] % 8));
    assert(c64_uci_image_remove(path).status != MT_SD_OK && journal_writes == before);
    bam[4 * a[1] + 1 + a[2] / 8] &= (uint8_t)~(1U << (a[2] % 8));
    fixture_add("0:/media/disk.d64.mtj", false, disk->bytes, disk->size);
    valid_recovery = true; disk->size = 11;
    assert(c64_uci_image_stat(path, &info).status == MT_SD_OK && journal_recovers == 1);
    fixture_add("0:/media/disk.d64.mtj", false, disk->bytes, disk->size);
    disk->exists = false;
    assert(c64_uci_image_open(path, 1, &f).status == MT_SD_OK && journal_recovers == 2);
    uint8_t out[8]; assert(c64_uci_image_read(f, out, sizeof out).size == 5 && !memcmp(out, "saved", 5));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_format("0:/media/missing.d64", "NO ID", NULL).status != MT_SD_OK);
    assert(!fixture_find("0:/media/missing.d64"));
}
static void geos_vlir(unsigned type)
{
    fixture_file *disk = format(type);
    char path[540]; snprintf(path, sizeof path, "%s/VLIR.CVT", disk->path);
    uint8_t cvt[1021] = {0};
    cvt[0] = 0x82; memset(cvt + 3, 0xa0, 16); memcpy(cvt + 3, "VLIR", 4);
    cvt[21] = 1; cvt[22] = 6;
    strcpy((char *)cvt + 30, "PRG formatted GEOS file V1.0");
    cvt[508] = 1; cvt[509] = 255;
    cvt[510] = 0; cvt[511] = 255;
    cvt[512] = 1; cvt[513] = 6;
    memset(cvt + 762, 0x35, 254); memcpy(cvt + 1016, "vlir!", 5);
    c64_uci_image_file_t *f = NULL;
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, cvt, sizeof cvt).size == sizeof cvt);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_open(path, 1, &f).status == MT_SD_OK);
    uint8_t out[1100]; assert(c64_uci_image_read(f, out, sizeof out).size == sizeof cvt);
    assert(!memcmp(out + 508, cvt + 508, sizeof cvt - 508));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_remove(path).status == MT_SD_OK);
    /* Removal must release info, index and every record, not only record0. */
    if (type == 81) {
        uint8_t *bam = disk->bytes + offset(81, 40, 1);
        assert(bam[16] == 40);
    } else assert(disk->bytes[548] == 0x1f && disk->bytes[549] == 0xff);
}
static void dnp_geometry_and_format(void)
{
    fixture_file *disk = format(100);
    c64_uci_image_file_t *f = NULL;
    const char *fresh = "0:/media/new.dnp";
    assert(c64_uci_image_format(fresh, "RAW", NULL).status != MT_SD_OK);
    assert(c64_uci_image_format(fresh, "RAW", "AB").backend_status == 6);
    assert(c64_uci_image_format(fresh, "RAW", "000").backend_status == 19);
    assert(c64_uci_image_format(fresh, "RAW", "256").backend_status == 19);
    assert(!fixture_find(fresh));
    assert(c64_uci_image_format(fresh, "RAW", "001").status == MT_SD_OK);
    fixture_file *created = fixture_find(fresh);
    assert(created && created->size == 65536);
    for (size_t i = 0; i < created->size; i++) assert(!created->bytes[i]);
    assert(c64_uci_image_directory(fresh, &f).backend_status == 13 && !f);
    assert(c64_uci_image_format(fresh, "formatted", "ab").status == MT_SD_OK);
    assert(created->bytes[256] == 1 && created->bytes[257] == 34 && created->bytes[258] == 0x48);
    assert(!memcmp(created->bytes + 260, "FORMATTED", 9));
    assert(!memcmp(created->bytes + 278, "AB", 2) && !memcmp(created->bytes + 281, "1H", 2));
    assert(created->bytes[288] == 1 && created->bytes[289] == 1);
    assert(created->bytes[514] == 0x48 && created->bytes[515] == 0xb7 && created->bytes[518] == 0xc0);
    assert(created->bytes[520] == 1 && !memcmp(created->bytes + 516, "AB", 2));
    for (size_t i = 544; i < 548; i++) assert(!created->bytes[i]);
    assert(created->bytes[548] == 0x1f);
    for (size_t i = 549; i < 576; i++) assert(created->bytes[i] == 0xff);
    for (size_t i = 576; i < 8704; i++) assert(!created->bytes[i]);
    assert(c64_uci_image_label(fresh, "RENAMED", "YZ").status == MT_SD_OK);
    assert(!memcmp(created->bytes + 516, "YZ", 2));
    assert(c64_uci_image_directory(fresh, &f).status == MT_SD_OK);
    char label[17], id[6];
    assert(c64_uci_image_header(f, label, id).status == MT_SD_OK && !strcmp(label, "RENAMED") && !strcmp(id, "YZ 1H"));
    assert(c64_uci_image_close(&f).status == MT_SD_OK);

    /* Unlike D64, the DNP root follows the header's actual directory link. */
    memcpy(disk->bytes + 255 * 256, disk->bytes + 34 * 256, 256);
    disk->bytes[257] = 255;
    allocated(disk->bytes, 100, 1, 255);
    disk->bytes[548] |= 0x20; /* Release the previous directory at 1/34. */
    for (unsigned i = 0; i < 12; i++) {
        char path[80]; snprintf(path, sizeof path, "%s/FILE%u.PRG", disk->path, i);
        assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
        assert(c64_uci_image_write(f, "test", 4).size == 4);
        assert(c64_uci_image_close(&f).status == MT_SD_OK);
    }
    assert(disk->bytes[255 * 256] != 0); /* Directory grew with ordinary allocator. */
    assert(c64_uci_image_mkdir("0:/media/disk.dnp/SUB").status == MT_SD_OK);
    assert(c64_uci_image_rename("0:/media/disk.dnp/SUB", "0:/media/disk.dnp/{53}UB/LOOP").status != MT_SD_OK);
    assert(c64_uci_image_format("0:/media/disk.dnp/SUB", "FRESH", NULL).status == MT_SD_OK);
    assert(disk->bytes[257] == 34);
    mt_sd_info_t info;
    assert(c64_uci_image_stat("0:/media/disk.dnp/FILE0.PRG", &info).backend_status == 4);
    unsigned t = 17, s = 0; uint8_t block[256];
    assert(c64_uci_image_sector(disk->path, 0, &t, &s, block) == 66);
    t = 16; s = 256; assert(c64_uci_image_sector(disk->path, 0, &t, &s, block) == 66);
    /* Truncated/non-sector and >255-track images are rejected before reads. */
    disk->size--;
    assert(c64_uci_image_directory(disk->path, &f).backend_status == 13 && !f);
    disk->size = 65536U * 256U;
    assert(c64_uci_image_directory(disk->path, &f).backend_status == 13 && !f);
}
static void dnp_large_relative(void)
{
    fixture_file *disk = format(100);
    const char *path = "0:/media/disk.dnp/BIG.REL";
    size_t length = 3300U * 254U + 2;
    uint8_t *data = malloc(length); assert(data);
    for (size_t i = 2; i < length; i++) data[i] = (uint8_t)(i * 17U);
    data[0] = 17; data[1] = 0;
    c64_uci_image_file_t *f = NULL;
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_write(f, data, length).size == length);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    const uint8_t *entry = disk->bytes + 34 * 256 + 2;
    unsigned side = offset(100, entry[19], entry[20]);
    assert(disk->bytes[side + 2] == 254); /* Super side-sector, 28 regular sectors. */
    assert((entry[28] | ((unsigned)entry[29] << 8)) == 3329);
    assert(c64_uci_image_open(path, 3, &f).status == MT_SD_OK);
    uint8_t got[8192];
    for (size_t i = 0; i < length;) {
        size_t n = length - i; if (n > sizeof got) n = sizeof got;
        assert(c64_uci_image_read(f, got, n).size == n && !memcmp(got, data + i, n)); i += n;
    }
    assert(c64_uci_image_seek(f, 2).status == MT_SD_OK);
    assert(c64_uci_image_write(f, "changed", 7).size == 7);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    assert(c64_uci_image_remove(path).status == MT_SD_OK);
    assert(disk->bytes[548] == 0x1f);
    for (unsigned i = 549; i < 544 + 512; i++) assert(disk->bytes[i] == 0xff);
    free(data);
}
static void dnp_maximum_and_recovery(void)
{
    format(100);
    const char *image = "0:/media/max.dnp", *path = "0:/media/max.dnp/FULL.PRG";
    assert(c64_uci_image_format(image, "RAW", "255").status == MT_SD_OK);
    fixture_file *disk = fixture_find(image); assert(disk && disk->size == F_BYTES);
    assert(c64_uci_image_format(image, "MAX", "42").status == MT_SD_OK);
    assert(disk->bytes[520] == 255);
    uint8_t block[8192]; memset(block, 0x5a, sizeof block);
    c64_uci_image_file_t *f = NULL;
    assert(c64_uci_image_open(path, 6, &f).status == MT_SD_OK);
    size_t length = (255U * 256U - 35U) * 254U;
    for (size_t n = 0; n < length;) {
        size_t count = length - n; if (count > sizeof block) count = sizeof block;
        assert(c64_uci_image_write(f, block, count).size == count); n += count;
    }
    journal_failure = true;
    assert(c64_uci_image_close(&f).status == MT_SD_SYNC_FAILED && f);
    assert(c64_uci_image_abort(&f).status == MT_SD_FILE_BUSY && f);
    journal_failure = false;
    assert(c64_uci_image_close(&f).status == MT_SD_OK && !f);
    assert(disk->bytes[F_BYTES - 256] == 0 && disk->bytes[F_BYTES - 255] == 255);
    assert(c64_uci_image_open(path, 1, &f).status == MT_SD_OK);
    assert(c64_uci_image_seek(f, (uint32_t)(length - 8192)).status == MT_SD_OK);
    memset(block, 0, sizeof block);
    assert(c64_uci_image_read(f, block, sizeof block).size == sizeof block);
    for (unsigned i = 0; i < sizeof block; i++) assert(block[i] == 0x5a);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
    unsigned before = journal_writes;
    assert(c64_uci_image_open("0:/media/max.dnp/FAIL.PRG", 6, &f).status == MT_SD_OK);
    assert(c64_uci_image_close(&f).status != MT_SD_OK && !f && journal_writes == before);
    /* A live file's last sector cannot be advertised free by a corrupt BAM. */
    disk->bytes[8703] |= 1;
    assert(c64_uci_image_remove(path).status != MT_SD_OK && journal_writes == before);
    disk->bytes[8703] &= 0xfe;
    assert(c64_uci_image_remove(path).status == MT_SD_OK);
    assert(disk->bytes[8703] == 255);
    fixture_add("0:/media/max.dnp.mtj", false, disk->bytes, disk->size);
    disk->size = 0; valid_recovery = true;
    assert(c64_uci_image_directory(image, &f).status == MT_SD_OK && journal_recovers == 1);
    assert(c64_uci_image_close(&f).status == MT_SD_OK);
}
int main(void)
{
    happy(64); happy(71); happy(81); happy(100); failures(); extended_images(); escaped_names(); geos_conversion(); image_metadata();
    corruption_and_recovery(); geos_vlir(81); geos_vlir(100);
    dnp_geometry_and_format(); dnp_large_relative(); dnp_maximum_and_recovery();
    puts("C64 UCI D64/D71/D81/DNP image tests passed");
    return 0;
}
