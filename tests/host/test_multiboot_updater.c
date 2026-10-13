/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise production FatFs-to-multiboot installation against live NOR bytes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "multiboot_platform.h"
#include "updater.h"
#include "crc32.h"

#define PAYLOAD 8192U
#define IMAGE_SIZE (PAYLOAD + MB_MANIFEST_SIZE)
#define FILE_COUNT 3U
static const char *const bin_paths[FILE_COUNT] = {
    "0:/FIRMWARE.BIN", "0:/FIRMWARE-APPLETINI.BIN", "0:/FIRMWARE-MULTITINI.BIN"
};
static const char *const ok_paths[FILE_COUNT] = {
    "0:/FIRMWARE.OK", "0:/FIRMWARE-APPLETINI.OK", "0:/FIRMWARE-MULTITINI.OK"
};
typedef struct {
    uint8_t data[IMAGE_SIZE];
    FSIZE_t size;
    int exists, ok_exists, short_read;
    uint32_t ok_crc, seeks, reads, writes, fail_mutation;
    FRESULT missing_result, open_result, close_result, unlink_result, rename_result;
    FRESULT seek_result, read_result;
} fake_file_t;
static fake_file_t files[FILE_COUNT];
static uint8_t nor[MB_FLASH_SIZE];
static uint32_t mutations, mutation_attempts, mount_count;
static FRESULT mount_result;
static int flash_available, active_file;
enum { EV_OPEN, EV_CLOSE, EV_UNLINK, EV_RENAME, EV_FLASH_FAILURE };
typedef struct { unsigned kind, file; } event_t;
static event_t events[64];
static unsigned event_count;
static void event(unsigned kind, unsigned file)
{
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (event_t){kind, file};
}
static unsigned event_total(unsigned kind, unsigned file)
{
    unsigned n = 0;
    for (unsigned i = 0; i < event_count; ++i)
        if (events[i].kind == kind && events[i].file == file) ++n;
    return n;
}
static unsigned path_index(const char *path, const char *const *paths)
{
    for (unsigned i = 0; i < FILE_COUNT; ++i) if (!strcmp(path, paths[i])) return i;
    assert(!"updater used an unexpected filename");
    return 0;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t crc(const void *p, unsigned n)
{
    return crc32_finish(crc32_update(crc32_init(), p, n));
}
static void checksum(uint8_t *p, unsigned words)
{
    uint32_t sum = 0;
    for (unsigned i = 0; i < words; ++i) sum += get32(p + 4 * i);
    put32(p + 4 * words, ~sum);
}
static void make_image(unsigned index, uint32_t product, int legacy, uint8_t revision)
{
    fake_file_t *f = &files[index];
    uint8_t *p = f->data, *m = p + PAYLOAD;
    memset(p, revision, IMAGE_SIZE); memset(p, 0, 512);
    put32(p + 0x20, 0xaa995566); put32(p + 0x24, 0x584c4e58);
    put32(p + 0x30, 512); put32(p + 0x34, PAYLOAD - 512);
    put32(p + 0x38, 0x100000); put32(p + 0x3c, 0x100000);
    put32(p + 0x40, PAYLOAD - 512); checksum(p + 0x20, 10);
    put32(p + 0x98, 0xa0); put32(p + 0x9c, 0x100);
    put32(p + 0xa0, 0x01020000); put32(p + 0xa4, 1);
    put32(p + 0xa8, 0x100 / 4); put32(p + 0xac, 0xe0 / 4);
    put32(p + 0x104, (PAYLOAD - 512) / 4); put32(p + 0x108, (PAYLOAD - 512) / 4);
    put32(p + 0x10c, 0x100000); put32(p + 0x110, 0x100000);
    put32(p + 0x114, 512 / 4); put32(p + 0x118, 0x10);
    checksum(p + 0x100, 15); put32(p + 0x17c, 0xffffffff);
    memcpy(p + 4090, MB_RECOVERY_MARKER, sizeof(MB_RECOVERY_MARKER) - 1);
    if (!legacy) {
        const char *marker = product == MB_PRODUCT_APPLETINI ? MB_APPLETINI_MARKER : MB_MULTITINI_MARKER;
        memcpy(p + 4200, marker, strlen(marker));
    }
    memcpy(m, "ATNIMG1\0", 8); put32(m + 8, legacy ? 1 : 2); put32(m + 12, 2);
    put32(m + 16, legacy ? MB_FLAG_RECOVERY : product | MB_FLAG_RECOVERY | MB_FLAG_MULTIBOOT);
    put32(m + 20, PAYLOAD); put32(m + 24, crc(p, PAYLOAD)); put32(m + 28, crc(m, 28));
    f->size = IMAGE_SIZE; f->exists = 1;
}
static int flash_read(void *context, uint32_t off, void *dst, uint32_t len)
{
    (void)context; assert(off <= MB_FLASH_SIZE && len <= MB_FLASH_SIZE - off);
    memcpy(dst, nor + off, len); return 0;
}
static int mutation_fails(void)
{
    ++mutation_attempts;
    if (active_file >= 0) {
        fake_file_t *f = &files[active_file];
        if (f->fail_mutation != 0 && --f->fail_mutation == 0) {
            event(EV_FLASH_FAILURE, (unsigned)active_file); return 1;
        }
        ++f->writes;
    }
    return 0;
}
static int flash_erase(void *context, uint32_t off, uint32_t len)
{
    (void)context; assert(off <= MB_FLASH_SIZE && len <= MB_FLASH_SIZE - off);
    if (mutation_fails()) return -1;
    memset(nor + off, 0xff, len); ++mutations; return 0;
}
static int flash_program(void *context, uint32_t off, const void *src, uint32_t len)
{
    const uint8_t *p = src; (void)context;
    assert(off <= MB_FLASH_SIZE && len <= MB_FLASH_SIZE - off);
    if (mutation_fails()) return -1;
    for (uint32_t i = 0; i < len; ++i) { assert((nor[off + i] & p[i]) == p[i]); nor[off + i] &= p[i]; }
    ++mutations; return 0;
}
static const mb_flash_t flash = {NULL, flash_read, flash_erase, flash_program, MB_FLASH_SIZE};
const mb_flash_t *mb_platform_flash(void) { return flash_available ? &flash : NULL; }
void uart_puts(uint32_t base, const char *s) { (void)base; (void)s; }
void golden_led_on(void) {}
void golden_led_toggle(void) {}
FRESULT f_mount(FATFS *fs, const char *path, BYTE opt)
{
    (void)fs; assert(!strcmp(path, "0:/") && opt == 1); ++mount_count; return mount_result;
}
FRESULT f_open(FIL *f, const char *path, BYTE mode)
{
    unsigned index = path_index(path, bin_paths);
    fake_file_t *source = &files[index];
    assert(mode == FA_READ && active_file == -1); event(EV_OPEN, index);
    if (!source->exists) return source->missing_result;
    if (source->open_result != FR_OK) return source->open_result;
    f->kind = index + 1; f->placeholder = 0; f->mode = mode; active_file = (int)index;
    return FR_OK;
}
static fake_file_t *opened_file(const FIL *f)
{
    assert(f->kind >= 1 && f->kind <= FILE_COUNT && f->mode == FA_READ);
    assert(active_file == (int)f->kind - 1); return &files[f->kind - 1];
}
FSIZE_t f_size(const FIL *f) { return opened_file(f)->size; }
FRESULT f_lseek(FIL *f, FSIZE_t pos)
{
    fake_file_t *source = opened_file(f);
    assert(pos <= source->size && pos <= IMAGE_SIZE); ++source->seeks;
    if (source->seek_result != FR_OK) return source->seek_result;
    f->placeholder = (unsigned)pos; return FR_OK;
}
FRESULT f_read(FIL *f, void *dst, UINT len, UINT *got)
{
    fake_file_t *source = opened_file(f);
    assert(f->placeholder <= IMAGE_SIZE && len <= IMAGE_SIZE - f->placeholder);
    assert((FSIZE_t)f->placeholder + len <= source->size); ++source->reads; *got = 0;
    if (source->read_result != FR_OK) return source->read_result;
    *got = source->short_read && len != 0 ? len - 1 : len;
    memcpy(dst, source->data + f->placeholder, *got); f->placeholder += *got; return FR_OK;
}
FRESULT f_close(FIL *f)
{
    fake_file_t *source = opened_file(f);
    FRESULT result = source->close_result; event(EV_CLOSE, f->kind - 1);
    /* A reported close failure must prevent acknowledging this BIN. */
    f->mode = 0; active_file = -1; return result;
}
FRESULT f_unlink(const char *path)
{
    unsigned index = path_index(path, ok_paths);
    fake_file_t *source = &files[index];
    assert(active_file == -1); event(EV_UNLINK, index);
    if (source->unlink_result != FR_OK) return source->unlink_result;
    if (!source->ok_exists) return FR_NO_FILE;
    source->ok_exists = 0; return FR_OK;
}
FRESULT f_rename(const char *from, const char *to)
{
    unsigned index = path_index(from, bin_paths);
    fake_file_t *source = &files[index];
    assert(path_index(to, ok_paths) == index && active_file == -1);
    assert(source->exists && event_total(EV_CLOSE, index) > 0); event(EV_RENAME, index);
    if (source->rename_result != FR_OK) return source->rename_result;
    if (source->ok_exists) return FR_EXIST;
    /* Only a fully verified live image may be acknowledged. */
    uint32_t product = get32(source->data + PAYLOAD + 16) & MB_PRODUCT_MASK;
    assert(mb_slot_verify(&flash, product, NULL) == MB_OK);
    assert(!memcmp(nor + mb_product_offset(product), source->data, IMAGE_SIZE));
    source->exists = 0; source->ok_exists = 1; source->ok_crc = crc(source->data, IMAGE_SIZE); return FR_OK;
}
static void reset_fixture(void)
{
    memset(nor, 0xff, sizeof(nor)); memset(files, 0, sizeof(files));
    mutations = mutation_attempts = mount_count = event_count = 0;
    mount_result = FR_OK; flash_available = 1; active_file = -1;
    for (unsigned i = 0; i < FILE_COUNT; ++i) files[i].missing_result = FR_NO_FILE;
}
static void assert_batch(unsigned found, unsigned installed, unsigned failed, unsigned renamed,
                         const updater_result_t *r)
{
    assert(r->files_found == found && r->files_updated == installed);
    assert(r->files_failed == failed && r->files_renamed == renamed);
    assert(r->found_file == (found != 0));
    assert(r->updated == (installed != 0) && r->verified == (installed != 0));
    assert(active_file == -1 && mount_count == 1);
    unsigned opened = 0;
    for (unsigned i = 0; i < event_count; ++i) {
        if (events[i].kind == EV_OPEN) {
            assert(events[i].file == opened);
            ++opened;
        } else {
            /* Finish close/acknowledgement before opening the next candidate. */
            assert(opened != 0 && events[i].file == opened - 1);
        }
    }
    assert(opened == FILE_COUNT);
    for (unsigned i = 0; i < FILE_COUNT; ++i) assert(event_total(EV_OPEN, i) == 1);
}
static void assert_slot(unsigned index, uint32_t product)
{
    assert(mb_slot_verify(&flash, product, NULL) == MB_OK);
    assert(!memcmp(nor + mb_product_offset(product), files[index].data, IMAGE_SIZE));
}
static void assert_done(unsigned index)
{
    assert(!files[index].exists && files[index].ok_exists);
    assert(files[index].ok_crc == crc(files[index].data, IMAGE_SIZE));
    assert(files[index].seeks == files[index].reads && files[index].reads > 10);
    assert(event_total(EV_CLOSE, index) == 1 && event_total(EV_RENAME, index) == 1);
}
static void test_presence_subsets(void)
{
    updater_result_t r;
    for (unsigned mask = 1; mask < 8; ++mask) {
        unsigned found = 0; reset_fixture();
        for (unsigned i = 0; i < FILE_COUNT; ++i) if (mask & (1U << i)) {
            make_image(i, i == 1 ? MB_PRODUCT_MULTITINI : MB_PRODUCT_APPLETINI, 0, (uint8_t)(0x31 + i)); ++found;
        }
        assert(updater_run(&r) == 0); assert_batch(found, found, 0, found, &r);
        if (mask & 5U) assert_slot(mask & 4U ? 2 : 0, MB_PRODUCT_APPLETINI);
        else assert(mb_slot_verify(&flash, MB_PRODUCT_APPLETINI, NULL) != MB_OK);
        if (mask & 2U) assert_slot(1, MB_PRODUCT_MULTITINI);
        else assert(mb_slot_verify(&flash, MB_PRODUCT_MULTITINI, NULL) != MB_OK);
        for (unsigned i = 0; i < FILE_COUNT; ++i) if (mask & (1U << i)) assert_done(i);
    }
    /* Deliberately swapped names must still route by the intrinsic product. */
    reset_fixture(); make_image(1, MB_PRODUCT_MULTITINI, 0, 0x45); make_image(2, MB_PRODUCT_APPLETINI, 0, 0x56);
    assert(updater_run(&r) == 0); assert_batch(2, 2, 0, 2, &r);
    assert_slot(1, MB_PRODUCT_MULTITINI); assert_slot(2, MB_PRODUCT_APPLETINI);
    /* Different payloads distinguish deterministic last-file-wins behavior. */
    for (uint32_t product = MB_PRODUCT_APPLETINI; product <= MB_PRODUCT_MULTITINI; product += MB_PRODUCT_APPLETINI) {
        reset_fixture();
        for (unsigned i = 0; i < FILE_COUNT; ++i) make_image(i, product, 0, (uint8_t)(0x70 + i));
        assert(crc(files[0].data, IMAGE_SIZE) != crc(files[2].data, IMAGE_SIZE));
        assert(updater_run(&r) == 0); assert_batch(3, 3, 0, 3, &r); assert_slot(2, product);
        assert(r.file_crc32 == crc(files[2].data, IMAGE_SIZE) && r.flash_crc32 == r.file_crc32);
        assert(mb_slot_verify(&flash, product == MB_PRODUCT_APPLETINI ? MB_PRODUCT_MULTITINI : MB_PRODUCT_APPLETINI, NULL) != MB_OK);
    }
}
static void test_missing_and_bad_files(void)
{
    updater_result_t r; uint32_t boot = 0xdeadbeef; reset_fixture();
    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        files[i].ok_exists = 1; files[i].ok_crc = 0x1234U + i;
        files[i].missing_result = i == 1 ? FR_NO_PATH : FR_NO_FILE;
    }
    assert(!updater_has_verified_firmware());
    assert(updater_select_firmware(&boot) != 0 && boot == 0xdeadbeef);
    assert(updater_run(&r) == 0); assert_batch(0, 0, 0, 0, &r);
    assert(mutations == 0 && mutation_attempts == 0);
    for (unsigned i = 0; i < FILE_COUNT; ++i) assert(files[i].ok_exists && files[i].ok_crc == 0x1234U + i);
    reset_fixture(); make_image(0, MB_PRODUCT_APPLETINI, 0, 0x33); files[0].data[800] ^= 1;
    make_image(1, MB_PRODUCT_APPLETINI, 1, 0x34); make_image(2, MB_PRODUCT_MULTITINI, 0, 0x35);
    assert(updater_run(&r) != 0); assert_batch(3, 1, 2, 1, &r);
    assert(files[0].exists && !files[0].ok_exists && files[0].writes == 0);
    assert(files[1].exists && !files[1].ok_exists && files[1].writes == 0);
    assert_slot(2, MB_PRODUCT_MULTITINI); assert_done(2);
    assert(mb_slot_verify(&flash, MB_PRODUCT_APPLETINI, NULL) != MB_OK);
    assert(r.file_crc32 == crc(files[2].data, IMAGE_SIZE));
    assert(r.error_code == 20 + MB_E_CRC && strstr(r.error_msg, bin_paths[0]) != NULL);
    /* Later failure must retain the preceding verified success/result. */
    reset_fixture(); make_image(0, MB_PRODUCT_MULTITINI, 0, 0x41); make_image(2, MB_PRODUCT_APPLETINI, 1, 0x42);
    assert(updater_run(&r) != 0); assert_batch(2, 1, 1, 1, &r);
    assert_slot(0, MB_PRODUCT_MULTITINI); assert_done(0);
    assert(r.file_size == IMAGE_SIZE && r.file_crc32 == crc(files[0].data, IMAGE_SIZE));
    /* Retagging the footer cannot override the linked product marker. */
    reset_fixture(); make_image(1, MB_PRODUCT_APPLETINI, 0, 0x48);
    put32(files[1].data + PAYLOAD + 16, MB_PRODUCT_MULTITINI | MB_FLAG_RECOVERY | MB_FLAG_MULTIBOOT);
    put32(files[1].data + PAYLOAD + 28, crc(files[1].data + PAYLOAD, 28));
    assert(updater_run(&r) != 0); assert_batch(1, 0, 1, 0, &r); assert(mutation_attempts == 0);
    assert(files[1].exists && !files[1].ok_exists);
}
static void test_source_errors(void)
{
    updater_result_t r;
    for (unsigned fault = 0; fault < 6; ++fault) {
        reset_fixture(); make_image(0, MB_PRODUCT_APPLETINI, 0, 0x50);
        if (fault == 0) files[0].short_read = 1;
        if (fault == 1) files[0].size = (FSIZE_t)UINT32_MAX + 1;
        if (fault == 2) files[0].size = MB_MANIFEST_SIZE;
        if (fault == 3) files[0].seek_result = FR_DISK_ERR;
        if (fault == 4) files[0].read_result = FR_DISK_ERR;
        if (fault == 5) files[0].open_result = FR_DISK_ERR;
        make_image(2, MB_PRODUCT_MULTITINI, 0, 0x51);
        assert(updater_run(&r) != 0); assert_batch(2, 1, 1, 1, &r);
        assert(files[0].writes == 0 && files[0].exists && !files[0].ok_exists);
        assert_slot(2, MB_PRODUCT_MULTITINI); assert_done(2);
        assert(mb_slot_verify(&flash, MB_PRODUCT_APPLETINI, NULL) != MB_OK);
    }
    reset_fixture(); mount_result = FR_DISK_ERR; make_image(0, MB_PRODUCT_APPLETINI, 0, 0x52);
    assert(updater_run(&r) != 0 && !r.found_file && !r.verified && event_count == 0 && mutation_attempts == 0);
    reset_fixture(); make_image(0, MB_PRODUCT_APPLETINI, 0, 0x53); flash_available = 0;
    assert(updater_run(&r) != 0); assert_batch(1, 0, 1, 0, &r); assert(!updater_has_verified_firmware());
    assert(files[0].exists && mutation_attempts == 0);
}
static void test_acknowledgement_errors(void)
{
    updater_result_t r;
    for (unsigned fault = 0; fault < 3; ++fault) {
        reset_fixture(); make_image(0, MB_PRODUCT_APPLETINI, 0, 0x60); make_image(1, MB_PRODUCT_MULTITINI, 0, 0x61);
        files[0].ok_exists = 1; files[0].ok_crc = 0xbad0c0de;
        if (fault == 0) files[0].close_result = FR_DISK_ERR;
        if (fault == 1) files[0].unlink_result = FR_DISK_ERR;
        if (fault == 2) files[0].rename_result = FR_DISK_ERR;
        assert(updater_run(&r) != 0); assert_batch(2, 2, 1, 1, &r);
        assert(r.error_code != 0 && r.rename_failures == (fault != 0));
        assert(files[0].exists && event_total(EV_RENAME, 0) == (fault == 2));
        if (fault == 0) assert(event_total(EV_UNLINK, 0) == 0);
        if (fault != 2) assert(files[0].ok_exists && files[0].ok_crc == 0xbad0c0de);
        assert_slot(0, MB_PRODUCT_APPLETINI); assert_slot(1, MB_PRODUCT_MULTITINI); assert_done(1);
        /* Only an unacknowledged BIN is retried on the next invocation. */
        files[0].close_result = files[0].unlink_result = files[0].rename_result = FR_OK;
        event_count = mount_count = 0; files[0].reads = files[0].seeks = files[1].reads = files[1].seeks = 0;
        assert(updater_run(&r) == 0); assert_batch(1, 1, 0, 1, &r); assert_done(0);
        assert(event_total(EV_CLOSE, 1) == 0 && event_total(EV_RENAME, 1) == 0);
    }
    reset_fixture(); make_image(2, MB_PRODUCT_APPLETINI, 0, 0x62); files[2].ok_exists = 1;
    assert(updater_run(&r) == 0); assert_batch(1, 1, 0, 1, &r); assert_done(2);
    unsigned close = 99, unlink = 99, rename = 99;
    for (unsigned i = 0; i < event_count; ++i) if (events[i].file == 2) {
        if (events[i].kind == EV_CLOSE) close = i;
        if (events[i].kind == EV_UNLINK) unlink = i;
        if (events[i].kind == EV_RENAME) rename = i;
    }
    assert(close < unlink && unlink < rename);
}
static void test_flash_failure_continues(void)
{
    updater_result_t r;
    for (unsigned failed = 0; failed < 2; ++failed) {
        reset_fixture(); make_image(0, MB_PRODUCT_APPLETINI, 0, 0x71);
        make_image(1, MB_PRODUCT_MULTITINI, 0, 0x72); make_image(2, MB_PRODUCT_APPLETINI, 0, 0x73);
        files[failed].fail_mutation = 5;
        assert(updater_run(&r) != 0); assert_batch(3, 2, 1, 2, &r);
        assert(files[failed].exists && !files[failed].ok_exists && event_total(EV_FLASH_FAILURE, failed) == 1);
        assert_slot(2, MB_PRODUCT_APPLETINI); assert_done(2);
        if (failed == 0) { assert_slot(1, MB_PRODUCT_MULTITINI); assert_done(1); }
        else { assert_done(0); assert(mb_slot_verify(&flash, MB_PRODUCT_MULTITINI, NULL) != MB_OK); }
        uint32_t boot = 0; assert(updater_select_firmware(&boot) == 0);
        assert(boot == MB_APPLETINI_OFFSET || (failed == 0 && boot == MB_MULTITINI_OFFSET));
    }
}
static void test_verified_fallback(void)
{
    updater_result_t r; uint32_t boot = 0;
    reset_fixture(); make_image(0, MB_PRODUCT_MULTITINI, 0, 0x79); make_image(1, MB_PRODUCT_APPLETINI, 0, 0x7a);
    assert(updater_run(&r) == 0); assert_batch(2, 2, 0, 2, &r);
    assert(mb_set_request(&flash, MB_PRODUCT_APPLETINI) == MB_E_UNAVAILABLE);
    memcpy(nor, files[1].data, IMAGE_SIZE);
    memcpy(nor + 4300, MB_GOLDEN_MARKER, sizeof(MB_GOLDEN_MARKER) - 1);
    put32(nor + PAYLOAD + 8, 1); put32(nor + PAYLOAD + 12, 1); put32(nor + PAYLOAD + 16, 1);
    put32(nor + PAYLOAD + 24, crc(nor, PAYLOAD)); put32(nor + PAYLOAD + 28, crc(nor + PAYLOAD, 28));
    assert(mb_set_request(&flash, MB_PRODUCT_APPLETINI) == MB_OK);
    nor[MB_APPLETINI_OFFSET + 800] ^= 1;
    assert(updater_select_firmware(&boot) == 0 && boot == MB_MULTITINI_OFFSET);
    mb_metadata_t metadata;
    assert(mb_metadata_load(&flash, &metadata) == MB_OK && metadata.request_product == MB_PRODUCT_NONE);
    assert(metadata.default_product == MB_PRODUCT_MULTITINI);
}
int main(void)
{
    test_presence_subsets(); test_missing_and_bad_files(); test_source_errors();
    test_acknowledgement_errors(); test_flash_failure_continues(); test_verified_fallback();
    puts("PASS: all filename subsets, intrinsic product routing, sequential duplicate installs, isolated errors/retries, closed .OK renames, verified fallback");
    return 0;
}
