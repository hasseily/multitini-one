/* SPDX-License-Identifier: GPL-3.0-only */
#define F_CAP 48
#define F_BYTES 220000
#include "c64_iec_fixture.h"
#include "c64_iec_service.h"
#include "c64_uci_internal.h"
#include "ff.h"
#include "multitini_c64_interface.h"
#include "sd_storage.h"
static unsigned checks;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static uint32_t rx[8192], tx[8192];
static unsigned head, tail, tx_count;
static uint32_t control, abi = 25, status_token, peripheral_control;
static uint32_t iec_caps = MT_C64_IEC_CAPS_DEVICE9;
static bool tx_ready = true, transport_fault;
static unsigned disable_with_rx, journal_replays, journal_writes;
static bool fail_journal;
static unsigned refresh_count, refreshed_device;
c64_uci_environment_t c64_uci_environment;
void c64_uci_service_refresh_control(void)
{
    ++refresh_count;refreshed_device=c64_iec_service_device_number();
}
bool c64_uci_protected(const char *path, bool mutate)
{
    (void)mutate;
    return fixture_protected(NULL, path);
}
static mt_sd_result_t sd_result(int code, size_t size)
{
    mt_sd_result_t r = {MT_SD_OK, FR_OK, size};
    if (code)
    {
        r.status = code == 63   ? MT_SD_FILE_EXISTS
                   : code == 70 ? MT_SD_FILE_BUSY
                   : code == 72 ? MT_SD_TOO_LARGE
                   : code == 62 ? MT_SD_OPEN_FAILED
                                : MT_SD_WRITE_FAILED;
        r.backend_status = code == 62 ? FR_NO_FILE : FR_DISK_ERR;
    }
    return r;
}
mt_sd_result_t mt_sd_storage_mount(void)
{
    return sd_result(0, 0);
}
mt_sd_result_t mt_sd_storage_capacity(const char *path, uint64_t *bytes)
{
    fixture_file *f = fixture_find(path);
    if (!f || !f->directory) return sd_result(62, 0);
    *bytes = UINT64_C(32) * 1024 * 1024 * 1024;
    return sd_result(0, 0);
}
bool mt_sd_storage_external_active(void)
{
    return false;
}
mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **handle)
{
    size_t size = 0;
    void *h = NULL;
    int rc = fixture_open(NULL, path, mode, &h, &size);
    *handle = (mt_sd_file_t *)h;
    return sd_result(rc, size);
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *h, void *data, size_t n)
{
    size_t got;
    int rc = fixture_read(NULL, h, data, n, &got);
    return sd_result(rc, got);
}
mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *h, const void *data, size_t n)
{
    size_t wrote;
    int rc = fixture_write(NULL, h, data, n, &wrote);
    return sd_result(rc, wrote);
}
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *h, const void *data, size_t n)
{
    return mt_sd_file_write_buffered(h, data, n);
}
mt_sd_result_t mt_sd_file_stat(const char *path, mt_sd_info_t *info)
{
    fixture_file *f = fixture_find(path);
    if (!f) return sd_result(62, 0);
    memset(info, 0, sizeof *info);
    info->size = f->size; info->attributes = f->directory ? 16 : 0;
    info->attributes |= f->metadata.attributes; info->date=f->metadata.date; info->time=f->metadata.time;
    const char *leaf = strrchr(path, '/');
    (void)snprintf(info->name, sizeof info->name, "%s", leaf ? leaf + 1 : path);
    return sd_result(0, 0);
}
mt_sd_result_t mt_sd_file_attributes(const char *path,uint8_t attributes,uint8_t mask)
{
    return sd_result(fixture_attributes(NULL,path,attributes,mask),0);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *file, int64_t offset, int origin)
{
    fixture_handle *h = (fixture_handle *)file;
    assert(h && h->used);
    int64_t target = offset + (origin == 2 ? (int64_t)h->file->size : origin == 1 ? (int64_t)h->cursor : 0);
    if (target < 0 || (uint64_t)target > h->file->size)
        return sd_result(23, 0);
    h->cursor = (size_t)target;
    return sd_result(0, 0);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *file)
{
    const fixture_handle *h = (const fixture_handle *)file;
    return h && h->used ? (int64_t)h->cursor : -1;
}
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file)
{
    void *h = file;
    return sd_result(fixture_close(NULL, &h), 0);
}
mt_sd_result_t mt_sd_file_remove(const char *path)
{
    return sd_result(fixture_remove(NULL, path), 0);
}
mt_sd_result_t mt_sd_file_rename(const char *a, const char *b)
{
    return sd_result(fixture_rename(NULL, a, b), 0);
}
mt_sd_result_t mt_sd_storage_ensure_directory(const char *path)
{
    return sd_result(fixture_mkdir(NULL, path), 0);
}
typedef struct
{
    mt_sd_list_fn fn;
    void *context;
} list_adapter;
static int sd_entry(void *context, const char *name, size_t size, bool directory)
{
    list_adapter *a = context;
    return a->fn(a->context, name, size, directory);
}
mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn fn, void *context)
{
    list_adapter a = {fn, context};
    int rc = fixture_list(NULL, path, sd_entry, &a); /* Actual sd_storage returns OK when callback requests stop. */
    return sd_result(rc == 71 ? 0 : rc, 0);
}
mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst, size_t capacity)
{
    fixture_file *f = fixture_find(path);
    if (!f)
        return sd_result(62, 0);
    if (f->size > capacity)
        return sd_result(72, 0);
    memcpy(dst, f->bytes, f->size);
    return sd_result(0, f->size);
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal, uint8_t *buffer, size_t capacity)
{
    (void)buffer;
    fixture_file *j = fixture_find(journal);
    if (!j)
        return sd_result(0, 0);
    if (j->size > capacity)
        return sd_result(72, 0);
    if (fail_journal)
        return sd_result(25, 0);
    fixture_add(path, false, j->bytes, j->size);
    j->exists = false;
    ++journal_replays;
    return sd_result(0, 0);
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal, const uint8_t *src, size_t size)
{
    ++journal_writes;
    fixture_add(journal, false, src, size);
    if (fail_journal)
        return sd_result(25, 0);
    fixture_add(path, false, src, size);
    fixture_find(journal)->exists = false;
    return sd_result(0, size);
}
static void push(unsigned type, unsigned channel, uint8_t byte, uint16_t token)
{
    assert(tail < 8192);
    rx[tail++] = (uint32_t)token << 16 | type << 12 | channel << 8 | byte;
}
static void add_test_disk(void)
{
    static uint8_t image[174848];
    memset(image, 0, sizeof image);
    const unsigned bam = 357U * 256U;
    image[bam] = 18; image[bam + 1] = 1; image[bam + 2] = 0x41;
    for (unsigned t = 1; t <= 35; ++t) {
        unsigned sectors = t <= 17 ? 21 : t <= 24 ? 19 : t <= 30 ? 18 : 17;
        image[bam + t * 4] = (uint8_t)sectors;
        for (unsigned s = 0; s < sectors; ++s) image[bam + t * 4 + 1 + s / 8] |= (uint8_t)(1U << (s % 8));
    }
    image[bam + 18 * 4] -= 2; image[bam + 18 * 4 + 1] &= (uint8_t)~3U;
    image[bam + 256 + 1] = 255;
    memset(image + bam + 144, 0xa0, 16); memcpy(image + bam + 144, "SERVICE TEST", 12);
    fixture_add("0:/media/test.d64", false, image, sizeof image);
}
static int image_bridge_tests(void)
{
    CHECK(c64_iec_service_configure_root("0:/media") == 0);
    /* Earlier fault cases intentionally retain incomplete host temporaries.
     * Start this independent image case with a fresh, fully closed medium. */
    for (unsigned i = 0; i < 16; ++i) CHECK(!fixture.handles[i].used);
    fixture_reset();
    add_test_disk();
    mt_iec_dos_t *d = c64_iec_service_dos();
    CHECK(d && mt_iec_dos_partition_add(d, 2, "0:/media/test.d64", "IMAGE") == 0);
    const uint8_t image_info[] = {'G','-','P',2};
    CHECK(mt_iec_dos_command(d, image_info, sizeof image_info) == 0);
    CHECK(d->status_size == 31 && d->status[0] == 2 &&
          !d->status[27] && d->status[28] == 1 && (uint8_t)d->status[29] == 86);
    const uint8_t host_info[] = {'G','-','P',1};
    CHECK(mt_iec_dos_command(d, host_info, sizeof host_info) == 0);
    CHECK(d->status[0] == 1 && (uint8_t)d->status[27] == 255 &&
          (uint8_t)d->status[28] == 255 && (uint8_t)d->status[29] == 255);
    CHECK(mt_iec_dos_command(d, (const uint8_t *)"CP:2", 4) == 0);
    const uint8_t program[] = {1,8,'H','E','L','L','O'};
    CHECK(mt_iec_dos_open(d, 1, (const uint8_t *)"PROGRAM", 7) == 0);
    mt_sd_file_t *conflict = NULL;
    CHECK(mt_sd_file_open("0:/media/test.d64", "r+", &conflict).status == MT_SD_FILE_BUSY && !conflict);
    CHECK(mt_iec_dos_write(d, 1, program, sizeof program, true) == 0);
    CHECK(mt_iec_dos_open(d, 0, (const uint8_t *)"PROGRAM", 7) == 0);
    uint8_t bytes[256]; size_t got; bool eof;
    CHECK(mt_iec_dos_peek_many(d, 0, bytes, sizeof bytes, &got, &eof) == 0 && got == sizeof program && eof && !memcmp(bytes, program, got));
    CHECK(mt_iec_dos_close(d, 0) == 0);
    /* REL uses host staging but roundtrips through actual CBM side sectors. */
    const uint8_t rel[] = {'R','E','C',',','L',',',10};
    CHECK(mt_iec_dos_open(d, 3, rel, sizeof rel) == 0);
    CHECK(mt_iec_dos_write(d, 3, (const uint8_t *)"RECORD", 6, true) == 0);
    CHECK(mt_iec_dos_close(d, 3) == 0);
    CHECK(mt_iec_dos_open(d, 3, (const uint8_t *)"REC", 3) == 0);
    CHECK(mt_iec_dos_peek_many(d, 3, bytes, sizeof bytes, &got, &eof) == 0 && got == 6 && !memcmp(bytes, "RECORD", 6));
    CHECK(mt_iec_dos_close(d, 3) == 0);
    CHECK(mt_iec_dos_open(d, 4, (const uint8_t *)"#", 1) == 0);
    CHECK(mt_iec_dos_command(d, (const uint8_t *)"U1:4,0,18,0", 11) == 0);
    CHECK(mt_iec_dos_peek_many(d, 4, bytes, sizeof bytes, &got, &eof) == 0 && got == 256 && bytes[2] == 0x41);
    CHECK(mt_iec_dos_close(d, 4) == 0);
    /* Failure in durable image replacement retains its owner and the full
     * host temporary. Explicit recovery completes it before reopening. */
    CHECK(mt_iec_dos_open(d, 2, (const uint8_t *)"RETRY,S,W", 9) == 0);
    CHECK(mt_iec_dos_write(d, 2, (const uint8_t *)"complete", 8, true) == 0);
    fail_journal = true;
    CHECK(mt_iec_dos_close(d, 2) == 25);
    c64_iec_service_write_failure(25);
    CHECK(c64_iec_service_dos() == NULL && c64_iec_service_quiesce() == 25);
    fail_journal = false;
    CHECK(c64_iec_service_reset() == 0 && c64_iec_service_dos() == d);
    CHECK(mt_iec_dos_open(d, 2, (const uint8_t *)"RETRY", 5) == 0);
    CHECK(mt_iec_dos_peek_many(d, 2, bytes, sizeof bytes, &got, &eof) == 0 && got == 8 && !memcmp(bytes, "complete", 8));
    CHECK(mt_iec_dos_close(d, 2) == 0);
    CHECK(mt_iec_dos_command(d, (const uint8_t *)"CP:1", 4) == 0);
    CHECK(mt_iec_dos_command(d, (const uint8_t *)"CD:TEST.D64", 11) == 0);
    CHECK(!strcmp(d->cwd, "0:/media/test.d64"));
    CHECK(c64_iec_service_quiesce() == 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(!fixture.handles[i].used);
    for (unsigned i = 0; i < F_CAP; ++i)
        CHECK(!fixture.files[i].exists || !strstr(fixture.files[i].path, ".mtiec-"));
    /* No free CBM blocks is a guest-visible error, not a permanent service
     * lock. The unchanged image and the complete host source both survive. */
    fixture_file *disk = fixture_find("0:/media/test.d64");
    CHECK(disk != NULL);
    memset(disk->bytes + 357U * 256U + 4, 0, 35U * 4U);
    static uint8_t full_image[174848];
    memcpy(full_image, disk->bytes, sizeof full_image);
    CHECK(mt_iec_dos_open(d, 2, (const uint8_t *)"FULL,S,W", 8) == 0);
    char staged[MT_IEC_PATH];
    (void)snprintf(staged, sizeof staged, "0:/media/%s", strrchr(d->channels[2].temporary, '/') + 1);
    CHECK(mt_iec_dos_write(d, 2, (const uint8_t *)"retained", 8, true) == 0);
    CHECK(mt_iec_dos_close(d, 2) == 72);
    c64_iec_service_write_failure(72);
    CHECK(c64_iec_service_reset() == 0);
    CHECK(!memcmp(full_image, disk->bytes, sizeof full_image));
    fixture_file *source = fixture_find(staged);
    CHECK(source && source->size == 8 && !memcmp(source->bytes, "retained", 8));
    CHECK(mt_sd_file_open("0:/media/test.d64", "r+", &conflict).status == MT_SD_OK && conflict);
    CHECK(mt_sd_file_close(conflict).status == MT_SD_OK);
    for (unsigned i = 0; i < 16; ++i) CHECK(!fixture.handles[i].used);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"CD:/",4)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NEW,ID",8)==0);
    CHECK(fixture_find("0:/media/NEW.D64") && fixture_find("0:/media/NEW.D64")->size==174848);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NEW,ID",8)==63);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"CD:NEW.D64",10)==0);
    CHECK(mt_iec_dos_open(d,1,(const uint8_t *)"ONE",3)==0);
    CHECK(mt_iec_dos_write(d,1,program,sizeof program,true)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"C:TWO=ONE",9)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"EL:TWO",6)==0);
    CHECK(mt_iec_dos_open(d,1,(const uint8_t *)"@TWO",4)==26);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"EU:TWO",6)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"A:H=TWO",7)==30); /* CBM image has no FAT hidden bit. */
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"R-H:LABEL,AB",12)==0);
    CHECK(mt_iec_dos_open(d,0,(const uint8_t *)"$",1)==0);
    CHECK(mt_iec_dos_peek_many(d,0,bytes,sizeof bytes,&got,&eof)==0);
    bool label_seen=false,id_seen=false;
    for(size_t i=0;i+5<=got;++i) {
        if(!memcmp(bytes+i,"LABEL",5))label_seen=true;
        if(!memcmp(bytes+i,"AB 2A",5))id_seen=true;
    }
    CHECK(label_seen && id_seen);
    CHECK(mt_iec_dos_close(d,0)==0);
    CHECK(mt_iec_dos_open(d,0,(const uint8_t *)"TWO",3)==0);
    CHECK(mt_iec_dos_close(d,0)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:EMPTY,AB",10)==0);
    CHECK(mt_iec_dos_open(d,0,(const uint8_t *)"TWO",3)==62);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"CD:/",4)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NATIVE.DNP,001",16)==0);
    fixture_file *native=fixture_find("0:/media/NATIVE.DNP");
    CHECK(native && native->size==65536 && native->bytes[258]==0 && native->bytes[256+34]==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NATIVE.DNP,001",16)==63);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:BAD.DNP,00X",13)==30);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:ZERO.DNP,000",14)==62);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NONE.DNP",10)==30);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"CD:NATIVE.DNP",13)==0);
    CHECK(mt_iec_dos_command(d,(const uint8_t *)"N:NATIVE,AB",11)==0 && native->bytes[258]==0x48);
    CHECK(mt_iec_dos_open(d,1,(const uint8_t *)"ONE",3)==0 && mt_iec_dos_write(d,1,program,sizeof program,true)==0);
    CHECK(mt_iec_dos_open(d,0,(const uint8_t *)"ONE",3)==0);
    CHECK(mt_iec_dos_peek_many(d,0,bytes,sizeof bytes,&got,&eof)==0 && got==sizeof program && !memcmp(bytes,program,got));
    CHECK(mt_iec_dos_close(d,0)==0);
    CHECK(c64_iec_service_quiesce()==0);
    return 0;
}
static uint32_t read32(void *ctx, uint32_t reg)
{
    (void)ctx;
    if (reg == MT_REG_CORE_ID)
        return MT_C64_CORE_ID;
    if (reg == MT_REG_ABI_VERSION)
        return abi;
    if (reg == MT_REG_C64_IEC_CAPS)
        return iec_caps;
    if (reg == MT_REG_C64_PERIPHERAL_CONTROL) return peripheral_control;
    if (reg == MT_REG_C64_IEC_STATUS)
        return status_token << 16 | (head < tail ? 1 : 0) | (tx_ready ? 2 : 0) | (control & 1 ? 4 : 0) |
               (transport_fault ? 16 : 0) | ((control & 2) && head == tail ? 32 : 0);
    if (reg == MT_REG_C64_IEC_RX)
    {
        assert(head < tail);
        return rx[head++];
    }
    return 0;
}
static void write32(void *ctx, uint32_t reg, uint32_t value)
{
    (void)ctx;
    if (reg == MT_REG_C64_IEC_CONTROL)
    {
        if (!value && head < tail)
            ++disable_with_rx;
        bool first = !(control & 1) && (value & 1);
        control = value;
        if (first)
            push(MT_C64_IEC_EVENT_RESET, 0, 0, 0);
    }
    else if (reg == MT_REG_C64_IEC_TX)
    {
        assert(tx_ready && tx_count < 8192);
        tx[tx_count++] = value;
    }
    else if (reg == MT_REG_C64_PERIPHERAL_CONTROL) peripheral_control=value;
}
static mt_core_link_t link = {NULL, read32, write32, 0};
static void poll_all(void)
{
    while (head < tail)
        assert(c64_iec_service_poll(32) >= 0);
}
static void send_name(unsigned ch, const char *name)
{
    push(MT_C64_IEC_EVENT_OPEN, ch, 0, 0);
    for (size_t i = 0; i < strlen(name); ++i)
        push(i + 1 == strlen(name) ? MT_C64_IEC_EVENT_DATA_EOI : MT_C64_IEC_EVENT_DATA, ch, (uint8_t)name[i], 0);
    push(MT_C64_IEC_EVENT_UNLISTEN, ch, 0, 0);
    poll_all();
}
static void listen(unsigned ch)
{
    push(MT_C64_IEC_EVENT_LISTEN, ch, 0, 0);
    poll_all();
}
static uint32_t request(unsigned event, unsigned ch, uint16_t token)
{
    unsigned before = tx_count;
    push(event, ch, 0, token);
    poll_all();
    assert(tx_count == before + 1);
    return tx[tx_count - 1];
}
static void acknowledge(unsigned ch, uint16_t token, uint8_t byte)
{
    push(MT_C64_IEC_EVENT_TX_ACK, ch, byte, token);
    poll_all();
}
int main(void)
{
    fixture_reset();
    CHECK(c64_iec_service_init(&link, "0:/media", fixture_protected, NULL) == 0);
    abi = 24;
    CHECK(!c64_iec_service_available());
    CHECK(c64_iec_service_enable(true) == 74);
    abi = 25;
    CHECK(c64_iec_service_available());
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    CHECK(c64_iec_service_device_number()==9 && c64_iec_service_set_device_number(10)==30);
    iec_caps|=MT_C64_IEC_CAPS_ADDRESS;abi=27;
    peripheral_control=0x8000807f;
    CHECK(c64_iec_service_available());
    const uint8_t new_address[]={'U','0','>',13};
    mt_iec_dos_t *address_dos=c64_iec_service_dos();
    CHECK(address_dos && mt_iec_dos_command(address_dos,new_address,sizeof new_address)==0);
    CHECK(c64_iec_service_device_number()==13 && peripheral_control==0x80008d7f && (control&1));
    CHECK(refresh_count && refreshed_device==13);
    CHECK(mt_iec_dos_command(address_dos,(const uint8_t *)"S-D",3)==0 && c64_iec_service_device_number()==9);
    CHECK(peripheral_control==0x8000897f && c64_iec_service_set_device_number(31)==30);
    const uint8_t prg[] = {1, 8, 0, 255, 13, 128};
    fixture_add("0:/media/HELLO.prg", false, prg, sizeof prg);
    send_name(0, "HELLO");
    status_token = 0; /* STATUS token deliberately lags RX token. */
    uint32_t word = request(MT_C64_IEC_EVENT_TALK, 0, 10);
    CHECK((word >> 16) == 10 && (word & 255) == 1 && !(word & 512));
    push(MT_C64_IEC_EVENT_UNTALK, 0, 0, 11);
    poll_all();
    word = request(MT_C64_IEC_EVENT_TALK, 0, 12);
    CHECK((word & 255) == 1); /* Canceled, unACKed byte repeats. */
    acknowledge(0, 11, 1);    /* Wrong token is ignored. */
    acknowledge(0, 12, 1);
    acknowledge(0, 12, 1); /* Duplicate proof cannot consume twice. */
    push(MT_C64_IEC_EVENT_UNTALK, 0, 0, 13);
    poll_all();
    word = request(MT_C64_IEC_EVENT_TALK, 0, 14);
    CHECK((word & 255) == 8);
    for (unsigned i = 1; i < sizeof prg; ++i)
    {
        CHECK((word & 255) == prg[i]);
        CHECK(((word >> 8) & 1) == (i + 1 == sizeof prg));
        acknowledge(0, (uint16_t)(13 + i), prg[i]);
        if (i + 1 < sizeof prg)
            word = request(MT_C64_IEC_EVENT_READ, 0, (uint16_t)(14 + i));
    }
    push(MT_C64_IEC_EVENT_UNTALK, 0, 0, 30);
    poll_all();
    CHECK(request(MT_C64_IEC_EVENT_TALK, 0, 31) & MT_C64_IEC_TX_ERROR);
    push(MT_C64_IEC_EVENT_CLOSE, 0, 0, 0);
    poll_all();
    send_name(0, "MISSING");
    CHECK(request(MT_C64_IEC_EVENT_TALK, 0, 32) & MT_C64_IEC_TX_ERROR);
    CHECK(!strncmp(c64_iec_service_status(), "62,", 3));
    push(MT_C64_IEC_EVENT_CLOSE, 0, 0, 0);
    poll_all();
    CHECK(!strncmp(c64_iec_service_status(), "62,", 3));
    char prior[80];
    strcpy(prior, c64_iec_service_status());
    for (unsigned i = 0; i < strlen(prior); ++i)
    {
        word = request(MT_C64_IEC_EVENT_TALK, 15, (uint16_t)(100 + i));
        CHECK((word & 255) == (uint8_t)prior[i]);
        acknowledge(15, (uint16_t)(100 + i), (uint8_t)prior[i]);
        push(MT_C64_IEC_EVENT_UNTALK, 15, 0, 0);
        poll_all();
    }
    CHECK(!strncmp(c64_iec_service_status(), "00,", 3));
    send_name(1, "SAVED");
    listen(1);
    for (unsigned i = 0; i < sizeof prg; ++i)
        push(i + 1 == sizeof prg ? MT_C64_IEC_EVENT_DATA_EOI : MT_C64_IEC_EVENT_DATA, 1, prg[i], 0);
    push(MT_C64_IEC_EVENT_CLOSE, 1, 0, 0);
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(!disable_with_rx);
    CHECK(journal_writes == 1);
    fixture_file *saved = fixture_find("0:/media/SAVED.prg");
    CHECK(saved && saved->size == sizeof prg && !memcmp(saved->bytes, prg, sizeof prg));
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    send_name(15, "CD:SUB");
    CHECK(!strcmp(c64_iec_service_cwd(), "0:/media/SUB"));
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    CHECK(!strcmp(c64_iec_service_cwd(), "0:/media/SUB"));
    CHECK(c64_iec_service_reset() == 0);
    CHECK(!strcmp(c64_iec_service_cwd(), "0:/media"));
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    send_name(1, "@SAVED");
    listen(1);
    push(MT_C64_IEC_EVENT_DATA, 1, 99, 0);
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(saved->size == sizeof prg && !memcmp(saved->bytes, prg, sizeof prg));
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    strcpy(fixture.protected_name, "0:/media/ACTIVE.D64");
    fixture_add(fixture.protected_name, false, prg, sizeof prg);
    send_name(0, "ACTIVE.D64");
    CHECK(!strncmp(c64_iec_service_status(), "70,", 3));
    send_name(1, "@ACTIVE.D64.");
    CHECK(!strncmp(c64_iec_service_status(), "33,", 3));
    CHECK(c64_iec_service_quiesce() == 0);
    fixture.protected_name[0] = 0;
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    /* Failed durable replacement retains journal, old target, sticky handoff
     * failure. Only explicit reset retries validated recovery. */
    send_name(1, "@SAVED");
    listen(1);
    fail_journal = true;
    push(MT_C64_IEC_EVENT_DATA_EOI, 1, 42, 0);
    poll_all();
    CHECK(!strncmp(c64_iec_service_status(), "25,", 3));
    CHECK(saved->size == sizeof prg);
    CHECK(fixture_find("0:/media/SAVED.prg.mtiec"));
    CHECK(c64_iec_service_quiesce() == 25);
    CHECK(c64_iec_service_quiesce() == 25);
    CHECK(c64_iec_service_reset() == 25);
    fail_journal = false;
    CHECK(c64_iec_service_reset() == 0);
    CHECK(journal_replays == 1 && saved->size == 1 && saved->bytes[0] == 42);
    CHECK(!fixture_find("0:/media/SAVED.prg.mtiec"));
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    fixture_file *huge = fixture_add("0:/media/HUGE.prg", false, NULL, 0);
    huge->size = MT_IEC_MAX_FILE + 1U;
    send_name(0, "HUGE");
    CHECK(!strncmp(c64_iec_service_status(), "72,", 3));
    CHECK(c64_iec_service_quiesce() == 0);
    huge->size = 0;
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    /* A temporary exclusivity probe whose close fails remains owned and
     * blocks storage handoff; retry releases it without leaking a FIL. */
    fixture_add("0:/media/LOCK.prg", false, prg, sizeof prg);
    fixture_add("0:/media/LOCK.prg.mtiec", false, prg, sizeof prg);
    fixture.fail_close = 25;
    fixture.keep_handle = true;
    send_name(0, "LOCK");
    CHECK(!strncmp(c64_iec_service_status(), "25,", 3));
    CHECK(c64_iec_service_quiesce() == 25);
    fixture.fail_close = 0;
    CHECK(c64_iec_service_reset() == 0);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(!fixture.handles[i].used);
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    /* A recovered completed new-file journal is discovered before lookup. */
    fixture_add("0:/media/RECOVER.prg.mtiec", false, prg, sizeof prg);
    send_name(0, "RECOVER");
    CHECK(fixture_find("0:/media/RECOVER.prg") && journal_replays == 3);
    word = request(MT_C64_IEC_EVENT_TALK, 0, 300);
    CHECK((word & 255) == 1);
    push(MT_C64_IEC_EVENT_CLOSE, 0, 0, 0);
    poll_all();
    tx_ready = false;
    send_name(0, "HELLO");
    unsigned before = tx_count;
    push(MT_C64_IEC_EVENT_TALK, 0, 0, 400);
    poll_all();
    CHECK(tx_count == before);
    push(MT_C64_IEC_EVENT_UNTALK, 0, 0, 401);
    poll_all();
    tx_ready = true;
    CHECK(c64_iec_service_poll(32) == 0 && tx_count == before);
    word = request(MT_C64_IEC_EVENT_TALK, 0, 402);
    CHECK((word & 255) == 1);
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    send_name(2, "HELLO");
    mt_iec_dos_t *shared = c64_iec_service_dos();
    CHECK(shared && c64_iec_service_channel_acquire(2));
    CHECK(!c64_iec_service_channel_acquire(2));
    uint8_t window[16]; size_t bytes; bool end;
    CHECK(mt_iec_dos_peek_many(shared, 2, window, sizeof window, &bytes, &end) == 0);
    CHECK(bytes == sizeof prg && end && !memcmp(window, prg, bytes));
    word = request(MT_C64_IEC_EVENT_TALK, 2, 500);
    CHECK(word & MT_C64_IEC_TX_ERROR);
    push(MT_C64_IEC_EVENT_CLOSE, 2, 0, 0); poll_all();
    CHECK(shared->channels[2].kind == 1 && shared->channels[2].position == 0);
    CHECK(c64_iec_service_quiesce() == 70);
    CHECK(mt_iec_dos_consume_many(shared, 2, 2) == 0);
    c64_iec_service_channel_release(2);
    word = request(MT_C64_IEC_EVENT_TALK, 2, 501);
    CHECK((word & 255) == prg[2]);
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(c64_iec_service_channel_acquire(0));
    CHECK(!c64_iec_service_active() && control == 0 &&
          c64_iec_service_dos() == shared);
    CHECK(mt_iec_dos_open(shared, 0, (const uint8_t *)"HELLO", 5) == 0);
    c64_iec_service_write_failure(25);
    CHECK(c64_iec_service_dos() == NULL);
    CHECK(c64_iec_service_cleanup_dos() == shared && shared->channels[0].handle);
    CHECK(c64_iec_service_quiesce() == 70); /* Retained UCI lease. */
    CHECK(mt_iec_dos_abort_channel(c64_iec_service_cleanup_dos(), 0) == 0);
    c64_iec_service_channel_release(0);
    CHECK(c64_iec_service_reset() == 0);
    CHECK(c64_iec_service_dos() == shared);
    CHECK(c64_iec_service_configure_root("0:/media/SUB") == 0);
    CHECK(!strcmp(c64_iec_service_cwd(), "0:/media/SUB"));
    CHECK(c64_iec_service_enable(true) == 0);
    poll_all();
    CHECK(c64_iec_service_configure_root("0:/media") == 70);
    transport_fault = true;
    CHECK(c64_iec_service_poll(32) == -1);
    CHECK(c64_iec_service_quiesce() == 74);
    transport_fault = false;
    CHECK(c64_iec_service_quiesce() == 0);
    CHECK(image_bridge_tests() == 0);
    printf("PASS C64 IEC service: %u checks\n", checks);
    return 0;
}
