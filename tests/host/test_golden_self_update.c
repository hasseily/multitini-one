/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the real updater. The NOR fixture permits only 1->0 programming, splits
 * writes at physical page boundaries and cuts power after each mutation. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "golden_self_update.h"
#include "qspi_nor.h"
#include "crc32.h"
#include "ff.h"

#define SLOT 0x100000U
#define FLASH_SIZE 0x1000000U
#define PAYLOAD 8192U
#define IMAGE_SIZE (PAYLOAD + 32U)
static uint8_t flash[FLASH_SIZE], target[IMAGE_SIZE], old_image[IMAGE_SIZE];
static uint8_t fallback[IMAGE_SIZE];
static uint32_t allowed_slot, mutations, cut_at, fail_at, reads, fail_read;
static uint32_t file_offset;
static int close_error, read_error, corrupt_program;
static jmp_buf power_cut;
static golden_self_update_result_t result;

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t crc(const uint8_t *p, size_t n) {
    return crc32_finish(crc32_update(crc32_init(), p, n));
}
static void checksum(uint8_t *p, unsigned words) {
    uint32_t sum = 0;
    for (unsigned i = 0; i < words; ++i) sum += get32(p + 4 * i);
    put32(p + 4 * words, ~sum);
}
static void manifest(uint8_t *p, unsigned role) {
    uint8_t *m = p + PAYLOAD;
    memcpy(m, "ATNIMG1\0", 8);
    put32(m+8, 1); put32(m+12, role); put32(m+16, 1);
    put32(m+20, PAYLOAD); put32(m+24, crc(p, PAYLOAD));
    put32(m+28, crc(m, 28));
}
static void make_image(uint8_t *p, unsigned role, uint8_t pattern) {
    memset(p, pattern, IMAGE_SIZE);
    memset(p, 0, 512);
    put32(p+0x20, 0xaa995566); put32(p+0x24, 0x584c4e58);
    put32(p+0x30, 512); put32(p+0x34, PAYLOAD-512);
    put32(p+0x38, 0x100000); put32(p+0x3c, 0x100000);
    put32(p+0x40, PAYLOAD-512); checksum(p+0x20, 10);
    put32(p+0x98, 0xa0); put32(p+0x9c, 0x100);
    put32(p+0xa0, 0x01020000); put32(p+0xa4, 1);
    put32(p+0xa8, 0x100/4); put32(p+0xac, 0xe0/4);
    put32(p+0x104, (PAYLOAD-512)/4); put32(p+0x108, (PAYLOAD-512)/4);
    put32(p+0x10c, 0x100000); put32(p+0x110, 0x100000);
    put32(p+0x114, 512/4); put32(p+0x118, 0x10);
    checksum(p+0x100, 15); put32(p+0x17c, 0xffffffff);
    /* Marker straddles the engine's 4096-byte read boundary. */
    memcpy(p+4090, GOLDEN_SELF_UPDATE_RECOVERY_MARKER,
           sizeof(GOLDEN_SELF_UPDATE_RECOVERY_MARKER)-1);
    manifest(p, role);
}
static void clear_flash(int repairing) {
    memset(flash, 0xff, sizeof flash);
    memcpy(flash, old_image, IMAGE_SIZE);
    memcpy(flash+2*SLOT, fallback, IMAGE_SIZE);
    if (repairing) memcpy(flash+SLOT, target, IMAGE_SIZE);
    allowed_slot = repairing ? 0 : SLOT;
    mutations = reads = cut_at = fail_at = fail_read = 0;
    file_offset = 0; close_error = read_error = corrupt_program = 0;
}
static void mutation_done(void) {
    ++mutations;
    if (cut_at == mutations) longjmp(power_cut, 1);
}
int qspi_nor_init(qspi_nor_t *n, uint8_t addr, uint32_t sector) {
    memset(n, 0, sizeof *n); assert(addr == 3 && sector == 0x10000);
    n->capacity_bytes = FLASH_SIZE; return 0;
}
uint32_t qspi_nor_capacity_bytes(const qspi_nor_t *n) { return n->capacity_bytes; }
int qspi_nor_read(qspi_nor_t *n, uint32_t a, void *p, uint32_t len) {
    (void)n; assert(a <= FLASH_SIZE && len <= FLASH_SIZE-a);
    if (++reads == fail_read) return 1;
    memcpy(p, flash+a, len); return 0;
}
int qspi_nor_erase_region(qspi_nor_t *n, uint32_t a, uint32_t len) {
    (void)n; assert(a == allowed_slot && len == SLOT);
    for (uint32_t off=0; off<len; off+=0x10000) {
        if (fail_at == mutations+1) return 1;
        memset(flash+a+off, 0xff, 0x10000); mutation_done();
    }
    return 0;
}
int qspi_nor_program(qspi_nor_t *n, uint32_t a, const void *src, uint32_t len) {
    const uint8_t *p = src; (void)n;
    assert(a >= allowed_slot && a+len <= allowed_slot+SLOT);
    while (len) {
        uint32_t chunk = 256-(a & 255U);
        if (chunk > len) chunk = len;
        if (fail_at == mutations+1) return 1;
        for (uint32_t i=0; i<chunk; ++i) {
            assert((flash[a+i] & p[i]) == p[i]);
            flash[a+i] &= p[i];
        }
        if (corrupt_program && a > allowed_slot+512) flash[a] &= 0;
        mutation_done(); a+=chunk; p+=chunk; len-=chunk;
    }
    return 0;
}
void uart_puts(uint32_t base, const char *s) { (void)base; (void)s; }
FRESULT f_open(FIL *f, const char *path, BYTE mode) {
    (void)f; assert(!strcmp(path,"0:/BOOT.BIN") && mode == FA_READ);
    file_offset=0; return FR_OK;
}
FSIZE_t f_size(const FIL *f) { (void)f; return IMAGE_SIZE; }
FRESULT f_lseek(FIL *f, FSIZE_t pos) {
    (void)f; assert(pos<=IMAGE_SIZE); file_offset=(uint32_t)pos; return FR_OK;
}
FRESULT f_read(FIL *f, void *p, UINT n, UINT *got) {
    (void)f; if (read_error) return FR_DISK_ERR;
    assert(n <= IMAGE_SIZE-file_offset);
    memcpy(p,target+file_offset,n); file_offset+=n; *got=n; return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; return close_error ? FR_DISK_ERR : FR_OK; }
static int run(int repairing) {
    return repairing ? golden_self_update_repair_primary(SLOT,0,0,&result) :
        golden_self_update_run_memory(target,IMAGE_SIZE,0,0,&result);
}
static void invariants(int repairing) {
    /* Neither the firmware fallback nor its metadata may ever be touched. */
    assert(!memcmp(flash+2*SLOT,fallback,IMAGE_SIZE));
    for (uint32_t i=2*SLOT+IMAGE_SIZE; i<FLASH_SIZE; ++i) assert(flash[i]==0xff);
    assert(!memcmp(flash+(repairing ? SLOT : 0),
                   repairing ? target : old_image, IMAGE_SIZE));
    /* Once XLNX is bootable, all bytes must be the complete old/new image. */
    if (get32(flash+allowed_slot+0x24)==0x584c4e58) {
        assert(!memcmp(flash+allowed_slot,target,IMAGE_SIZE) ||
               (repairing && !memcmp(flash,old_image,IMAGE_SIZE)));
    }
}
static void faults(int repairing) {
    uint32_t count, read_count;
    clear_flash(repairing); assert(run(repairing)==0);
    assert(result.updated && result.verified && result.reset_required);
    assert(result.boot_offset == allowed_slot);
    assert(!memcmp(flash+allowed_slot,target,IMAGE_SIZE));
    count=mutations; read_count=reads;
    for (uint32_t stop=1; stop<=count; ++stop) {
        clear_flash(repairing); cut_at=stop;
        if (!setjmp(power_cut)) { (void)run(repairing); assert(!"power cut missing"); }
        invariants(repairing);
        /* Re-enter after power restoration: the untouched copy permits retry. */
        cut_at=0; assert(run(repairing)==0); invariants(repairing);
    }
    for (uint32_t stop=1; stop<=count; ++stop) {
        clear_flash(repairing); fail_at=stop;
        assert(run(repairing)!=0 && !result.verified && !result.reset_required);
        invariants(repairing);
    }
    for (uint32_t stop=1; stop<=read_count; ++stop) {
        clear_flash(repairing); fail_read=stop;
        int rc=run(repairing);
        if (rc==0) assert(!memcmp(flash+allowed_slot,target,IMAGE_SIZE));
        else assert(!result.verified && !result.reset_required);
        invariants(repairing);
    }
    clear_flash(repairing); corrupt_program=1;
    assert(run(repairing)!=0 && !result.verified); invariants(repairing);
    printf("golden %s: %u power cuts, %u mutation failures, %u read failures\n",
           repairing ? "repair" : "stage",count,count,read_count);
}
int main(void) {
    make_image(target,1,0xa5); make_image(old_image,1,0x55);
    make_image(fallback,2,0x5a);
    faults(0); faults(1);
    for (unsigned kind=0; kind<7; ++kind) {
        clear_flash(0);
        if (kind==0) target[PAYLOAD+8]^=1; /* manifest version/CRC */
        if (kind==1) target[512]^=1; /* payload CRC */
        if (kind==2) { target[4090]=0; manifest(target,1); }
        if (kind==3) flash[0x24]=0; /* primary not bootable */
        if (kind==4) flash[2*SLOT+PAYLOAD+12]=1; /* fallback role */
        if (kind==5) flash[2*SLOT+512]^=1; /* fallback CRC */
        if (kind==6) { target[0x17c]=0; manifest(target,1); } /* PHT sentinel */
        assert(run(0)!=0 && mutations==0 && !result.reset_required);
        make_image(target,1,0xa5);
    }
    clear_flash(0); close_error=1;
    assert(golden_self_update_run("0:/BOOT.BIN",0,0,&result)==0 && result.verified);
    clear_flash(0); read_error=1;
    assert(golden_self_update_run("0:/BOOT.BIN",0,0,&result)!=0 && mutations==0);
    clear_flash(0);
    assert(golden_self_update_repair_primary(0,0,0,&result)==0 && mutations==0);
    puts("golden self-update production engine: PASS"); return 0;
}
