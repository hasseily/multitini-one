/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the real updater. The NOR fixture permits only 1->0 programming, splits
 * writes at physical page boundaries and cuts power after each mutation. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "golden_self_update.h"
#include "multiboot.h"
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
static int close_error, read_error, short_read, corrupt_program;
static const uint8_t *file_data;
static uint8_t done_data[IMAGE_SIZE];
static FSIZE_t file_size;
static int bin_exists, done_exists, file_opened, volume_read_only;
static BYTE bin_attributes, done_attributes;
static FRESULT mount_error, stat_error, open_error, seek_error, unlink_error, rename_error;
static unsigned opens, closes, unlinks, renames, mounts;
static char sd_order[128];
static unsigned sd_events;
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
    if (role == 1) {
        memcpy(p + 4300, MB_GOLDEN_MARKER, sizeof(MB_GOLDEN_MARKER)-1);
    }
    manifest(p, role);
    if (role == 2) {
        uint8_t *m = p + PAYLOAD;
        memcpy(p + 4200, MB_APPLETINI_MARKER, sizeof(MB_APPLETINI_MARKER)-1);
        put32(m+8, 2);
        put32(m+16, MB_PRODUCT_APPLETINI | MB_FLAG_MULTIBOOT | MB_FLAG_RECOVERY);
        put32(m+24, crc(p, PAYLOAD));
        put32(m+28, crc(m, 28));
    }
}
static void clear_flash(int repairing) {
    memset(flash, 0xff, sizeof flash);
    memcpy(flash, old_image, IMAGE_SIZE);
    memcpy(flash+2*SLOT, fallback, IMAGE_SIZE);
    if (repairing) memcpy(flash+SLOT, target, IMAGE_SIZE);
    allowed_slot = repairing ? 0 : SLOT;
    mutations = reads = cut_at = fail_at = fail_read = 0;
    file_offset = 0; close_error = read_error = short_read = corrupt_program = 0;
    file_data=target; file_size=IMAGE_SIZE;
    bin_exists=1; done_exists=file_opened=volume_read_only=0;
    bin_attributes=done_attributes=0;
    mount_error=stat_error=open_error=seek_error=unlink_error=rename_error=FR_OK;
    opens=closes=unlinks=renames=mounts=sd_events=0; sd_order[0]=0;
    memset(done_data,0,sizeof(done_data));
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
static void sd_event(char c) {
    assert(sd_events<sizeof(sd_order)-1); sd_order[sd_events++]=c; sd_order[sd_events]=0;
}
FRESULT f_mount(FATFS *fs, const char *path, BYTE option) {
    assert(fs!=NULL && !strcmp(path,"0:/") && option==1);
    ++mounts; return mount_error;
}
FRESULT f_stat(const char *path, FILINFO *info) {
    assert(!strcmp(path,"0:/BOOT.BIN") || !strcmp(path,"0:/BOOT.OK"));
    sd_event('S');
    if(stat_error!=FR_OK) return stat_error;
    int source=!strcmp(path,"0:/BOOT.BIN");
    if(!(source?bin_exists:done_exists)) return FR_NO_FILE;
    if(info) {
        memset(info,0,sizeof(*info));
        info->fsize=source?file_size:IMAGE_SIZE;
        info->fattrib=source?bin_attributes:done_attributes;
    }
    return FR_OK;
}
FRESULT f_open(FIL *f, const char *path, BYTE mode) {
    assert(!strcmp(path,"0:/BOOT.BIN") && mode == FA_READ && !file_opened);
    ++opens; sd_event('O');
    if(!bin_exists) return FR_NO_FILE;
    if(open_error!=FR_OK) return open_error;
    if(bin_attributes&AM_DIR) return FR_NO_FILE;
    f->kind=1; f->mode=mode; file_offset=0; file_opened=1; return FR_OK;
}
FSIZE_t f_size(const FIL *f) { assert(file_opened && f->kind==1); return file_size; }
FRESULT f_lseek(FIL *f, FSIZE_t pos) {
    assert(file_opened && f->kind==1 && pos<=file_size);
    if(seek_error!=FR_OK) return seek_error;
    file_offset=(uint32_t)pos; return FR_OK;
}
FRESULT f_read(FIL *f, void *p, UINT n, UINT *got) {
    assert(file_opened && f->kind==1); *got=0;
    if (read_error) return FR_DISK_ERR;
    assert(n <= file_size-file_offset && n<=IMAGE_SIZE-file_offset);
    *got=short_read && n>0 ? n-1:n;
    memcpy(p,file_data+file_offset,*got); file_offset+=*got; return FR_OK;
}
FRESULT f_close(FIL *f) {
    assert(file_opened && f->kind==1); ++closes; sd_event('C');
    if(close_error) return FR_DISK_ERR;
    file_opened=0; f->mode=0; return FR_OK;
}
FRESULT f_unlink(const char *path) {
    assert(!strcmp(path,"0:/BOOT.OK") && !file_opened); ++unlinks; sd_event('U');
    if(unlink_error!=FR_OK) return unlink_error;
    if(volume_read_only) return (FRESULT)10; /* FR_WRITE_PROTECTED */
    if(!done_exists) return FR_NO_FILE;
    if(done_attributes&1U) return (FRESULT)7; /* FR_DENIED for AM_RDO */
    if(done_attributes&AM_DIR) return (FRESULT)7;
    done_exists=0; return FR_OK;
}
FRESULT f_rename(const char *from, const char *to) {
    assert(!strcmp(from,"0:/BOOT.BIN") && !strcmp(to,"0:/BOOT.OK"));
    assert(bin_exists && !file_opened && closes>0); ++renames; sd_event('N');
    /* BOOT.OK may only acknowledge the fully repaired primary bytes. */
    assert(file_size==IMAGE_SIZE && !memcmp(flash,file_data,IMAGE_SIZE));
    if(rename_error!=FR_OK) return rename_error;
    if(volume_read_only) return (FRESULT)10;
    if(done_exists) return FR_EXIST;
    memcpy(done_data,file_data,IMAGE_SIZE); done_attributes=bin_attributes;
    bin_exists=0; done_exists=1; return FR_OK;
}
static int run(int repairing) {
    int rc=repairing ? golden_self_update_repair_primary(SLOT,0,0,&result) :
        golden_self_update_run_memory(target,IMAGE_SIZE,0,0,&result);
    assert(!unlinks&&!renames); /* Neither staging nor repair acknowledges SD. */
    return rc;
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
static void prepare_completion(void) {
    clear_flash(1);
    assert(run(1)==0 && result.updated && result.verified && result.reset_required);
    assert(result.boot_offset==GOLDEN_SELF_UPDATE_PRIMARY_OFFSET);
    reads=0;
}
static int complete(uint32_t running, const golden_self_update_result_t *repair) {
    golden_self_update_result_t preserved={0};
    uint32_t before=mutations;
    unsigned previous_renames=renames, previous_unlinks=unlinks;
    if(repair) preserved=*repair;
    int rc=golden_self_update_complete_sd(running,repair,0,0);
    assert(mutations==before); /* SD acknowledgement never writes either slot. */
    if(repair) assert(!memcmp(repair,&preserved,sizeof(preserved)));
    assert(!memcmp(flash+SLOT,target,IMAGE_SIZE));
    assert(!memcmp(flash+2*SLOT,fallback,IMAGE_SIZE));
    if(rc==GOLDEN_SELF_UPDATE_SD_RENAMED) {
        assert(!bin_exists && done_exists && !file_opened && renames==previous_renames+1);
        assert(!memcmp(done_data,target,IMAGE_SIZE));
        assert(strrchr(sd_order,'C')<strrchr(sd_order,'N'));
        if(unlinks>previous_unlinks) assert(strrchr(sd_order,'C')<strrchr(sd_order,'U') &&
                                           strrchr(sd_order,'U')<strrchr(sd_order,'N'));
    }
    return rc;
}
static void completion_tests(void) {
    golden_self_update_result_t repair;
    uint8_t different[IMAGE_SIZE];
    /* A verified trial stage cannot rename the source before primary repair. */
    clear_flash(0);
    assert(golden_self_update_run("0:/BOOT.BIN",0,0,&result)==0);
    assert(bin_exists && !done_exists && !unlinks && !renames);
    assert(golden_self_update_complete_sd(SLOT,&result,0,0)==GOLDEN_SELF_UPDATE_SD_FAILED);
    assert(bin_exists && !done_exists && !unlinks && !renames);

    prepare_completion();
    assert(complete(SLOT,&result)==GOLDEN_SELF_UPDATE_SD_RENAMED);
    assert(!unlinks); /* No old BOOT.OK: rename needs no removal. */
    assert(golden_self_update_complete_sd(0,NULL,0,0)==GOLDEN_SELF_UPDATE_SD_UNCHANGED);
    assert(done_exists && !bin_exists && renames==1); /* Retry is idempotent. */

    /* Retry from primary after power loss between primary commit and rename. */
    prepare_completion();
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_RENAMED);
    prepare_completion(); done_exists=1; memcpy(done_data,old_image,IMAGE_SIZE);
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_RENAMED);
    assert(unlinks==1 && !memcmp(done_data,target,IMAGE_SIZE));
    prepare_completion(); bin_attributes=1; /* FatFs permits AM_RDO source rename. */
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_RENAMED && done_attributes==1);

    /* Missing or unrelated BOOT.BIN must leave an older BOOT.OK untouched. */
    for(unsigned kind=0;kind<7;++kind) {
        prepare_completion(); done_exists=1; memcpy(done_data,old_image,IMAGE_SIZE);
        if(kind==0) bin_exists=0;
        if(kind==1) file_data=old_image;
        if(kind==2) { memcpy(different,target,IMAGE_SIZE); different[PAYLOAD+16]^=1; file_data=different; }
        if(kind==3) file_size=IMAGE_SIZE-1;
        if(kind==4) file_data=fallback; /* Same size, firmware role, unrelated bytes. */
        if(kind==5) open_error=FR_NO_PATH;
        if(kind==6) bin_attributes=AM_DIR;
        assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_UNCHANGED);
        assert(bin_exists==(kind!=0) && done_exists && !unlinks && !renames);
        assert(!memcmp(done_data,old_image,IMAGE_SIZE));
    }
    /* A file replaced while trial was booting must not acknowledge a different
     * image merely because the repair result itself was successful. */
    prepare_completion(); file_data=old_image;
    assert(complete(SLOT,&result)==GOLDEN_SELF_UPDATE_SD_UNCHANGED && bin_exists);

    for(unsigned kind=0;kind<6;++kind) {
        prepare_completion(); repair=result;
        if(kind==0) repair.updated=0;
        if(kind==1) repair.verified=0;
        if(kind==2) repair.reset_required=0;
        if(kind==3) repair.boot_offset=SLOT;
        if(kind==4) repair.error_code=1;
        assert(complete(kind==5 ? 2*SLOT:SLOT,&repair)==GOLDEN_SELF_UPDATE_SD_FAILED);
        assert(bin_exists && !mounts && !opens && !unlinks && !renames);
    }
    prepare_completion();
    assert(complete(SLOT,NULL)==GOLDEN_SELF_UPDATE_SD_FAILED && !opens && !renames);
    for(unsigned kind=0;kind<3;++kind) {
        prepare_completion(); repair=result;
        if(kind==0) ++repair.payload_size;
        if(kind==1) repair.payload_crc32^=1;
        if(kind==2) repair.flash_crc32^=1;
        assert(complete(SLOT,&repair)==GOLDEN_SELF_UPDATE_SD_UNCHANGED);
        assert(bin_exists && !unlinks && !renames);
    }

    /* Every source/flash check must finish before the first SD mutation. */
    for(unsigned kind=0;kind<8;++kind) {
        prepare_completion();
        if(kind==0) mount_error=FR_DISK_ERR;
        if(kind==1) open_error=FR_DISK_ERR;
        if(kind==2) read_error=1;
        if(kind==3) short_read=1;
        if(kind==4) close_error=1;
        if(kind==5) fail_read=1;
        if(kind==6) flash[0x24]=0;
        if(kind==7) flash[5000]^=1;
        assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_FAILED);
        assert(bin_exists && !done_exists && !unlinks && !renames);
    }
    for(unsigned kind=0;kind<6;++kind) {
        prepare_completion(); done_exists=1; memcpy(done_data,old_image,IMAGE_SIZE);
        if(kind==0) stat_error=FR_DISK_ERR;
        if(kind==1) done_attributes=AM_DIR;
        if(kind==2) done_attributes=1;
        if(kind==3) unlink_error=FR_DISK_ERR;
        if(kind==4) rename_error=FR_DISK_ERR;
        if(kind==5) volume_read_only=1;
        assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_FAILED && bin_exists);
        if(kind!=4) assert(done_exists && !memcmp(done_data,old_image,IMAGE_SIZE));
        else assert(!done_exists && unlinks==1 && renames==1);
        if(kind<4 || kind==5) assert(!renames);
        assert(!file_opened);
        if(kind==4) {
            /* A failed rename after removing old OK is retryable from A;
             * no flash staging/repair or reboot-result change is needed. */
            rename_error=FR_OK;
            assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_RENAMED);
            assert(unlinks==1 && renames==2);
        }
    }
    prepare_completion(); volume_read_only=1;
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_FAILED && bin_exists && !done_exists);
    prepare_completion(); rename_error=FR_DISK_ERR;
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_FAILED && bin_exists && !done_exists);
    prepare_completion(); file_size=SLOT+1;
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_UNCHANGED && bin_exists && !renames);
    prepare_completion(); file_size=UINT64_MAX;
    assert(complete(0,NULL)==GOLDEN_SELF_UPDATE_SD_UNCHANGED && bin_exists && !renames);
    puts("golden completion: exact installed-primary identity, no trial-stage rename, retry, old OK replacement, readonly media, and all SD/flash failure boundaries PASS");
}
int main(void) {
    make_image(target,1,0xa5); make_image(old_image,1,0x55);
    make_image(fallback,2,0x5a);
    clear_flash(0);
    memset(target+4300,0,sizeof(MB_GOLDEN_MARKER)-1); manifest(target,1);
    assert(run(0)!=0 && mutations==0);
    make_image(target,1,0x73);
    memset(old_image+4300,0,sizeof(MB_GOLDEN_MARKER)-1); manifest(old_image,1);
    clear_flash(0); assert(run(0)==0 && result.verified);
    make_image(old_image,1,0x15);
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
    clear_flash(0); seek_error=FR_DISK_ERR;
    assert(golden_self_update_run("0:/BOOT.BIN",0,0,&result)!=0 && mutations==0 && bin_exists && !renames && !unlinks);
    clear_flash(0);
    assert(golden_self_update_repair_primary(0,0,0,&result)==0 && mutations==0);
    completion_tests();
    puts("golden self-update production engine: PASS"); return 0;
}
