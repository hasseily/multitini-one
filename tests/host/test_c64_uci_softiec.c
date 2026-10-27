/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_iec_fixture.h"
#include "c64_iec_service.h"
#include "c64_uci_internal.h"
#include <stdlib.h>

static mt_iec_dos_t dos;
static uint8_t ram[65536];
static uint16_t leases;
static bool active = true, held, ram_pending, storage_fault;
static unsigned checks, waits, failures;
static mt_uci_target_t target;
static mt_uci_reply_t reply;
static int unheld_read(void *c, void *h, uint8_t *p, size_t n, size_t *got)
{ assert(!held); return fixture_read(c,h,p,n,got); }
static int unheld_write(void *c, void *h, const uint8_t *p, size_t n, size_t *got)
{ assert(!held); return fixture_write(c,h,p,n,got); }
static int unheld_close(void *c, void **h)
{ assert(!held); return fixture_close(c,h); }
static int unheld_commit(void *c, const char *a, const char *b, bool replace)
{ assert(!held); return fixture_commit(c,a,b,replace); }
c64_uci_environment_t c64_uci_environment;
mt_iec_dos_t *c64_iec_service_dos(void)
{
    return storage_fault ? NULL : &dos;
}
mt_iec_dos_t *c64_iec_service_cleanup_dos(void)
{
    return &dos;
}
bool c64_iec_service_active(void)
{
    return active;
}
bool c64_iec_service_channel_acquire(unsigned ch)
{
    if (ch > 15 || (leases & (1U << ch)))
        return false;
    leases |= (uint16_t)(1U << ch);
    return true;
}
void c64_iec_service_channel_release(unsigned ch)
{
    leases &= (uint16_t)~(1U << ch);
}
void c64_iec_service_write_failure(int code)
{
    if (code)
        ++failures;
    storage_fault = code != 0;
}
int c64_uci_service_hold_begin(void)
{
    held = true;
    return 0;
}
void c64_uci_service_hold_end(void)
{
    assert(!ram_pending);
    held = false;
}
int c64_uci_ram_read(uint16_t addr, uint8_t *out, size_t size)
{
    assert(held && size <= 256 && (unsigned)addr + size <= 65536);
    if (!ram_pending) {
        ram_pending = true;
        ++waits;
        return 1;
    }
    memcpy(out, ram + addr, size);
    ram_pending = false;
    return 0;
}
int c64_uci_ram_write(uint16_t addr, const uint8_t *in, size_t size)
{
    assert(held && size <= 256 && (unsigned)addr + size <= 65536);
    memcpy(ram + addr, in, size);
    return 0;
}
int c64_uci_ram_cancel(void)
{
    if (ram_pending) {
        ram_pending = false;
        return 1;
    }
    return 0;
}
bool c64_uci_path(const char *cwd, const uint8_t *name, size_t size, char out[512])
{
    if (size > 255 || memchr(name, 0, size) || memchr(name, '\\', size))
        return false;
    char text[256];
    memcpy(text, name, size);
    text[size] = 0;
    if (strstr(text, "..") || strchr(text, ':'))
        return false;
    int n =
        snprintf(out, 512, "%s/%s", *text == '/' ? dos.root : cwd, *text == '/' ? text + 1 : text);
    return n > 0 && n < 512;
}
mt_sd_result_t mt_sd_file_stat(const char *path, mt_sd_info_t *info)
{
    fixture_file *f = fixture_find(path);
    memset(info, 0, sizeof *info);
    if (!f)
        return (mt_sd_result_t){MT_SD_OPEN_FAILED, 4, 0};
    info->attributes = f->directory ? 0x10 : 0;
    info->size = f->size;
    return (mt_sd_result_t){MT_SD_OK, 0, 0};
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static mt_uci_result_t start(unsigned op, unsigned ch, const uint8_t *data, size_t bytes,
                             size_t offset)
{
    uint8_t command[896] = {5, (uint8_t)op, (uint8_t)ch};
    assert(offset + bytes <= sizeof command);
    if (bytes)
        memcpy(command + offset, data, bytes);
    return target.begin(NULL, command, offset + bytes, &reply);
}
static void complete(void)
{
    for (unsigned i = 0; i < 1024; ++i)
        if (target.poll(NULL, &reply) != MT_UCI_WAIT) {
            CHECK(!held && !ram_pending);
            return;
        }
    CHECK(false);
}
static void ack(size_t n)
{
    CHECK(target.abort(NULL, n) == MT_UCI_REPLY);
}
static void open_name(unsigned ch, const char *name)
{
    CHECK(start(0x13, ch, (const uint8_t *)name, strlen(name), 4) == MT_UCI_REPLY);
    CHECK(reply.status_length == 0);
}
int main(void)
{
    fixture_reset();
    mt_iec_fs_t fs = fixture_api();
    fs.read=unheld_read;fs.write=unheld_write;fs.close=unheld_close;fs.commit=unheld_commit;
    CHECK(mt_iec_dos_init(&dos, &fs, "0:/media") == 0);
    target = c64_uci_softiec_target();
    target.reset(NULL);
    static uint8_t data[1600];
    for (unsigned i = 0; i < sizeof data; ++i)
        data[i] = (uint8_t)(i * 19U + 7U);
    data[0] = 0;
    data[1] = 0x20;
    fixture_add("0:/media/data.prg", false, data, sizeof data);
    fixture_add("0:/media/sub", true, NULL, 0);
    fixture_add("0:/media/sub/nested.seq", false, data, 90);
    CHECK(start(1, 0, NULL, 0, 2) == MT_UCI_REPLY &&
          reply.data_length == strlen("SOFTWARE IEC TARGET V1.0"));
    active = false;
    CHECK(start(1, 0, NULL, 0, 2) == MT_UCI_REPLY &&
          reply.data_length == strlen("SOFTWARE IEC TARGET V1.0"));
    /* Exercise the complete shared-DOS command sequence with the physical
     * serial listener off, including channel leases and RAM LOAD/SAVE. */
    const uint8_t move_address[]={'M','-','W',0x77,0,1,12};
    CHECK(start(0x13,15,move_address,sizeof move_address,4)==MT_UCI_REPLY && dos.current_device==12 && fixture.device==12);
    CHECK(start(0x13,15,(const uint8_t *)"M-E",3,4)==MT_UCI_REPLY);
    CHECK(start(0x15,15,NULL,0,3)==MT_UCI_REPLY && reply.data_length==27 && !memcmp(reply.data,"98,UNKNOWN DRIVECODE,00,00\r",27));
    ack(reply.data_length);
    CHECK(start(0x13,15,(const uint8_t *)"S-D",3,4)==MT_UCI_REPLY && dos.current_device==9);
    open_name(2, "DATA");
    CHECK(start(0x15, 2, NULL, 0, 3) == MT_UCI_REPLY && reply.data_length == 32 && reply.more &&
          leases == (1U << 2));
    CHECK(!memcmp(reply.data, data, 32) && dos.channels[2].position == 0);
    ack(7);
    CHECK(dos.channels[2].position == 7 && !leases);
    CHECK(start(0x15, 2, NULL, 0, 3) == MT_UCI_REPLY && !memcmp(reply.data, data + 7, 32));
    CHECK(target.next(NULL, 32, &reply) == MT_UCI_REPLY && reply.data_length == 256);
    CHECK(dos.channels[2].position == 39 && !memcmp(reply.data, data + 39, 256));
    /* Cross the old 512-byte prefetch boundary with an unconsumed tail. */
    CHECK(target.next(NULL, 256, &reply) == MT_UCI_REPLY && !memcmp(reply.data, data + 295, 256));
    ack(17);
    CHECK(dos.channels[2].position == 312);
    uint8_t native;
    bool eoi, have;
    CHECK(mt_iec_dos_peek(&dos, 2, &native, &eoi, &have) == 0 && have && native == data[312]);
    mt_iec_dos_consume(&dos, 2);
    CHECK(start(0x15, 2, NULL, 0, 3) == MT_UCI_REPLY && reply.data[0] == data[313]);
    ack(0);
    CHECK(start(0x14, 2, NULL, 0, 3) == MT_UCI_REPLY && !dos.channels[2].kind);
    mt_iec_dos_error(&dos, 62);
    CHECK(start(0x15, 15, NULL, 0, 3) == MT_UCI_REPLY && reply.data[0] == '6');
    mt_iec_dos_error(&dos, 25);
    ack(5);
    CHECK(dos.status_cursor == 0 && !strncmp(dos.status, "25,", 3));
    CHECK(start(0x15, 15, NULL, 0, 3) == MT_UCI_REPLY);
    size_t status_bytes = reply.data_length;
    ack(status_bytes);
    CHECK(!strncmp(dos.status, "00,", 3));
    CHECK(start(0x10, 1, (const uint8_t *)"DATA", 4, 8) == MT_UCI_REPLY && reply.status[0] == 0 &&
          reply.data[1] == 0x20);
    ack(2);
    CHECK(leases == 1); /* Setup survives completion until LOAD_EX. */
    CHECK(start(0x11, 0, NULL, 0, 4) == MT_UCI_WAIT);
    complete();
    CHECK(reply.status_length == 3 && reply.status[0] == 0 &&
          !memcmp(ram + 0x2000, data + 2, sizeof data - 2));
    CHECK(!leases && !dos.channels[0].kind);
    CHECK(start(0x10, 1, (const uint8_t *)"DATA", 4, 8) == MT_UCI_REPLY);
    ack(2);
    uint8_t verify_command[896] = {5, 0x11, 0, 1};
    CHECK(target.begin(NULL, verify_command, 4, &reply) == MT_UCI_WAIT);
    complete();
    CHECK(reply.status[0] == 0 && waits >= 7);
    CHECK(start(0x10, 1, (const uint8_t *)"DATA", 4, 8) == MT_UCI_REPLY);
    ack(2);
    ram[0x2100] ^= 1;
    CHECK(target.begin(NULL, verify_command, 4, &reply) == MT_UCI_WAIT);
    complete();
    CHECK(reply.status[0] == 0x80);
    /* SAVE stores load address + exact RAM range and commits before status00. */
    uint8_t save_command[896] = {5, 0x12, 0, 1, 0, 0x20, 0x11, 0x22, 'S', 'A', 'V', 'E', 'D'};
    CHECK(target.begin(NULL, save_command, 13, &reply) == MT_UCI_WAIT);
    complete();
    fixture_file *saved = fixture_find("0:/media/SAVED.prg");
    CHECK(reply.status[0] == 0 && saved && saved->size == 0x213 && saved->bytes[0] == 0 &&
          saved->bytes[1] == 0x20);
    CHECK(!memcmp(saved->bytes + 2, ram + 0x2000, 0x211));
    /* Abort an asynchronous read before reusing HOLD or committing a SAVE. */
    save_command[8] = 'A';
    CHECK(target.begin(NULL, save_command, 13, &reply) == MT_UCI_WAIT);
    CHECK(target.poll(NULL, &reply) == MT_UCI_WAIT);
    CHECK(target.poll(NULL, &reply) == MT_UCI_WAIT && ram_pending);
    CHECK(target.abort(NULL, 0) == MT_UCI_WAIT);
    CHECK(target.abort(NULL, 0) == MT_UCI_REPLY && !held && !leases);
    CHECK(!fixture_find("0:/media/AAVED.prg"));
    /* Guest protocol mistakes are DOS errors, not failed storage ownership. */
    CHECK(start(0x16, 8, (const uint8_t *)"x", 1, 4) == MT_UCI_REPLY && !failures);
    CHECK(!strncmp(dos.status, "70,", 3));
    CHECK(start(0x16, 0xf3, (const uint8_t *)"TEXT,S,W", 8, 4) == MT_UCI_REPLY);
    CHECK(start(0x16, 3, (const uint8_t *)"hello", 5, 4) == MT_UCI_REPLY);
    CHECK(start(0x16, 0xe3, NULL, 0, 4) == MT_UCI_REPLY);
    CHECK(fixture_find("0:/media/TEXT.seq") && fixture_find("0:/media/TEXT.seq")->size == 5);
    CHECK(start(0x22, 1, (const uint8_t *)"NEW,S,W", 7, 3) == MT_UCI_REPLY && reply.status[0] == 0);
    CHECK(reply.data_length == strlen("/NEW.seq") &&
          !memcmp(reply.data, "/NEW.seq", reply.data_length));
    CHECK(!fixture_find("0:/media/NEW.seq"));
    CHECK(start(0x22, 3, (const uint8_t *)"NEWDEFAULT,W", 12, 3) == MT_UCI_REPLY);
    CHECK(reply.data_length == strlen("/NEWDEFAULT.seq") &&
          !memcmp(reply.data, "/NEWDEFAULT.seq", reply.data_length));
    open_name(3, "NEWDEFAULT,W");
    CHECK(start(0x16, 3, (const uint8_t *)"x", 1, 4) == MT_UCI_REPLY);
    CHECK(start(0x14, 3, NULL, 0, 3) == MT_UCI_REPLY && fixture_find("0:/media/NEWDEFAULT.seq"));
    CHECK(start(0x23, 0, (const uint8_t *)"data.prg", 8, 2) == MT_UCI_REPLY &&
          reply.status[0] == 0 && reply.data[0] == 1 && !memcmp(reply.data + 1, "DATA", 4));
    CHECK(start(0x20, 7, (const uint8_t *)"TITLE:/sub", 10, 3) == MT_UCI_REPLY &&
          reply.status[0] == 0);
    CHECK(!strcmp(dos.partitions[7].name, "TITLE"));
    CHECK(start(0x22, 4, (const uint8_t *)"$7/", 3, 3) == MT_UCI_REPLY && reply.status[0] == 0);
    CHECK(reply.data_length == 4 && !memcmp(reply.data, "/sub", 4));
    CHECK(start(0x22, 4, (const uint8_t *)"$=P", 3, 3) == MT_UCI_REPLY && reply.status[0] == 0);
    CHECK(reply.data_length == 11 && !memcmp(reply.data, "/partitions", 11));
    CHECK(start(0x16, 15, (const uint8_t *)"CP:7", 4, 4) == MT_UCI_REPLY);
    CHECK(dos.current_partition == 7 && !strcmp(dos.cwd, "0:/media/sub"));
    open_name(4, "NESTED");
    CHECK(dos.channels[4].kind == 1);
    CHECK(start(0x14, 4, NULL, 0, 3) == MT_UCI_REPLY);
    CHECK(start(0x16, 15, (const uint8_t *)"CD:..", 5, 4) == MT_UCI_REPLY);
    CHECK(!strncmp(dos.status, "26,", 3) && !strcmp(dos.cwd, "0:/media/sub"));
    CHECK(start(0x16, 15, (const uint8_t *)"CP:1", 4, 4) == MT_UCI_REPLY);
    CHECK(dos.current_partition == 1 && !strcmp(dos.cwd, "0:/media"));
    open_name(4, "7:NESTED");
    CHECK(dos.channels[4].kind == 1 && !strcmp(dos.cwd, "0:/media"));
    CHECK(start(0x14, 4, NULL, 0, 3) == MT_UCI_REPLY);
    CHECK(start(0x21, 7, NULL, 0, 3) == MT_UCI_REPLY && reply.status[0] == 0);
    CHECK(start(0x22, 0, (const uint8_t *)"7:NESTED", 8, 3) == MT_UCI_REPLY &&
          reply.status[0] == 8);
    CHECK(start(0x20, 1, (const uint8_t *)"BAD:/../escape", 14, 3) == MT_UCI_REPLY &&
          reply.status[0] == 9);
    CHECK(start(0xff, 0, NULL, 0, 2) == MT_UCI_REPLY && reply.status[0] == 4);
    open_name(6, "DATA");
    target.reset(NULL);
    CHECK(dos.channels[6].kind == 1); /* Ordinary OPEN survives target reset. */
    CHECK(mt_iec_dos_close(&dos, 6) == 0);
    CHECK(!failures && !leases);
    fixture.fail_commit = 25;
    save_command[8] = 'F';
    CHECK(target.begin(NULL, save_command, 13, &reply) == MT_UCI_WAIT);
    complete();
    CHECK(reply.status[0] == 2 && failures && !fixture_find("0:/media/FAVED.prg"));
    CHECK(!strncmp(dos.status, "25,", 3));
    CHECK(!active && storage_fault);
    CHECK(start(1, 0, NULL, 0, 2) == MT_UCI_REPLY && reply.status[0] == 5);
    CHECK(start(0x10, 1, (const uint8_t *)"DATA", 4, 8) == MT_UCI_REPLY &&
          reply.status[0] == 5 && !leases && !dos.channels[0].kind);
    active = true;
    CHECK(start(1, 0, NULL, 0, 2) == MT_UCI_REPLY && reply.status[0] == 5);
    active = false;
    fixture.fail_commit = 0;
    storage_fault = false; /* Explicit device9 error recovery. */
    CHECK(start(0x10, 1, (const uint8_t *)"DATA", 4, 8) == MT_UCI_REPLY);
    fixture.keep_handle = true;
    fixture.fail_close = 25;
    target.reset(NULL);
    CHECK(c64_uci_environment.fault && leases && dos.channels[0].handle);
    fixture.fail_close = 0;
    c64_uci_environment.fault = false;
    target.reset(NULL);
    CHECK(!leases && !dos.channels[0].handle && !held);
    CHECK(storage_fault && c64_uci_environment.fault);
    c64_uci_environment.fault = false;
    target.reset(NULL);
    CHECK(!leases && !c64_uci_environment.fault);
    printf("SoftIEC target PASS: %u checks, actual shared DOS, partial ACK/abort, async RAM, all "
           "command "
           "families\n",
           checks);
    return 0;
}
