/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_uci_internal.h"
static void observe_storage(void);
#define UCI_STORAGE_OBSERVE() observe_storage()
#include "c64_uci_storage_fixture.h"
#include "multitini_c64_interface.h"
#include "net_service.h"
#include <stdio.h>

static uint32_t regs[0x10000 / 4];
static uint8_t reu[0x1000000], ram[65536], ram_request_token;
static uint64_t usec;
static bool acknowledge_fence = true, acknowledge_ram = true;
static unsigned drains, writes, reads;
static unsigned native_pending, native_delay, native_ops, guest_resets;
static uint32_t native_op;
static bool native_stall, native_drop;
static bool watch_storage, iec_active = true;
static unsigned observed_storage;
static void observe_storage(void)
{
    if (watch_storage) {
        assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
        observed_storage++;
    }
}
static void native_step(void)
{
    if (!native_pending || native_stall || --native_pending)
        return;
    uint32_t *s = &regs[MT_REG_C64_UCI_STATUS / 4];
    uint32_t *two = &regs[MT_REG_C64_UCI_STATUS2 / 4];
    *s &= ~MT_C64_UCI_STATUS_HANDSHAKE_BUSY;
    *two = (*two & ~(3U << 19)) | ((((*two >> 19) + 1) & 3) << 19);
    if (native_drop) {
        native_drop = false;
        *two |= 1U << 21;
        return;
    }
    *two &= ~(1U << 21);
    switch (native_op & 15) {
    case MT_C64_UCI_OP_RESET:
        *s &= ~0x3ffefU;
        *two &= ~MT_C64_UCI_STATUS2_CMD_BUSY;
        break;
    case MT_C64_UCI_OP_IDLE_ABORT_DONE:
        *s &= ~MT_C64_UCI_STATUS_IDLE_ABORT;
        break;
    case MT_C64_UCI_OP_LAST:
    case MT_C64_UCI_OP_MORE:
        *s = (*s & ~0x3ffefU) | ((native_op & 15) == MT_C64_UCI_OP_LAST ? 3U : 2U);
        *two &= ~MT_C64_UCI_STATUS2_CMD_BUSY;
        break;
    default:
        assert(0);
    }
}
static uint32_t read32(void *context, uint32_t offset)
{
    (void)context;
    reads++;
    if (offset == MT_REG_C64_UCI_STATUS)
        native_step();
    return regs[offset / 4];
}
static void write32(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    writes++;
    regs[offset / 4] = value;
    if (offset == MT_REG_C64_UCI_HANDSHAKE) {
        assert(!native_pending);
        native_ops++;
        native_op = value;
        native_pending = native_delay ? native_delay : 1;
        regs[MT_REG_C64_UCI_STATUS / 4] |= MT_C64_UCI_STATUS_HANDSHAKE_BUSY;
    } else if (offset == MT_REG_C64_UCI_CONTROL) {
        regs[MT_REG_C64_CPU_STATUS / 4] =
            (value & MT_C64_UCI_CONTROL_HOLD) ? MT_C64_CPU_STATUS_PAUSE : 0;
    } else if (offset == MT_REG_C64_REU_HOST) {
        regs[offset / 4] |= MT_C64_REU_HOST_IDLE;
        if (acknowledge_fence)
            regs[offset / 4] |= (value & 15) << 8;
    } else if (offset == MT_REG_C64_UCI_RAM_READ) {
        ram_request_token = (uint8_t)((((regs[MT_REG_C64_UCI_STATUS2 / 4] >> 24) & 15) + 1) & 15);
        size_t n = ((value >> 16) & 0x7ff) + 1;
        for (size_t i = 0; i < n; i += 4) {
            uint32_t w = 0;
            for (size_t j = 0; j < 4 && i + j < n; j++)
                w |= (uint32_t)ram[(uint16_t)(value + i + j)] << (8 * j);
            regs[(MT_C64_UCI_BUFFER_OFFSET + MT_C64_UCI_REPLY_OFFSET + i) / 4] = w;
        }
        if (acknowledge_ram)
            regs[MT_REG_C64_UCI_STATUS2 / 4] = (uint32_t)ram_request_token << 24;
    }
}
static uint64_t now(void *context)
{
    (void)context;
    return usec;
}
void c64_uci_reu_read(uint32_t a, void *p, size_t n)
{
    assert(a + n <= sizeof reu);
    memcpy(p, reu + a, n);
}
void c64_uci_reu_write(uint32_t a, const void *p, size_t n)
{
    assert(a + n <= sizeof reu);
    memcpy(reu + a, p, n);
}
void c64_uci_reu_drain(void)
{
    drains++;
}
mt_uci_target_t mt_net_service_network_target(void)
{
    return (mt_uci_target_t){0};
}
mt_uci_target_t mt_net_service_http_target(void)
{
    return (mt_uci_target_t){0};
}
void mt_net_service_guest_reset(void) {guest_resets++;}
static mt_uci_result_t softiec_present(void *ctx, const uint8_t c[896], size_t n, mt_uci_reply_t *r)
{
    (void)ctx; (void)c; (void)n; (void)r;
    return MT_UCI_REPLY;
}
mt_uci_target_t c64_uci_softiec_target(void)
{
    return (mt_uci_target_t){.begin=softiec_present};
}
bool c64_iec_service_active(void)
{
    return iec_active;
}
static mt_core_link_t link;
static mt_uci_reply_t reply;
static uint8_t command[896];
static void setup(void)
{
    fixture_reset();
    memset(directories, 0, sizeof directories);
    memset(regs, 0, sizeof regs);
    file_close_failures = directory_close_failures = remove_failures = rename_failures = 0;
    file_close_released_failures = 0;
    memset(command, 0, sizeof command);
    usec = 0;
    watch_storage = false;
    iec_active = true;
    observed_storage = 0;
    drains = 0;
    native_pending = native_delay = native_ops = guest_resets = 0;
    ram_request_token = 0;
    native_stall = native_drop = false;
    acknowledge_fence = acknowledge_ram = true;
    regs[MT_REG_ABI_VERSION / 4] = (MT_ABI_MAJOR << 16) | 26;
    regs[MT_REG_CORE_ID / 4] = MT_C64_CORE_ID;
    regs[MT_REG_C64_EXPANSION_CAPS / 4] = MT_C64_EXPANSION_CAPS_UCI;
    regs[MT_REG_C64_EXPANSION_CONFIG / 4] = MT_C64_EXPANSION_REU_ENABLE | MT_C64_EXPANSION_UCI_ENABLE;
    regs[MT_REG_C64_REU_HOST / 4] = MT_C64_REU_HOST_IDLE;
    link = (mt_core_link_t){.read32 = read32, .write32 = write32};
    c64_uci_callbacks_t cb = {.microseconds = now};
    assert(c64_uci_service_init(&link, "0:/media", &cb) == 0);
}
static mt_uci_result_t start(mt_uci_target_t *t, uint8_t op, const void *data, size_t size)
{
    command[0] = 1;
    command[1] = op;
    if (size)
        memcpy(command + 2, data, size);
    mt_uci_reply_clear(&reply);
    return t->begin(t->context, command, size + 2, &reply);
}
static void finish(mt_uci_target_t *t, mt_uci_result_t result)
{
    for (unsigned i = 0; result == MT_UCI_WAIT && i < 20000; i++)
        result = t->poll(t->context, &reply);
    assert(result == MT_UCI_REPLY);
}
static void status(const char *text)
{
    if (reply.status_length != strlen(text) || memcmp(reply.status, text, strlen(text)))
        fprintf(stderr, "Expected status %s, received %.*s (REU path %s)\n", text,
                (int)reply.status_length, reply.status, c64_uci_environment.reu_image);
    assert(reply.status_length == strlen(text));
    assert(!memcmp(reply.status, text, strlen(text)));
}
static void open_file(mt_uci_target_t *t, const char *path, uint8_t mode)
{
    uint8_t data[514];
    data[0] = mode;
    strcpy((char *)data + 1, path);
    finish(t, start(t, 2, data, strlen(path) + 2));
    status("00,OK");
}
static bool protected_disk(void *context, const char *path)
{
    (void)context;
    return fixture_equal(path, "0:/media/Mounted Disk.d64");
}
static void path_alias_protection(void)
{
    setup();
    char path[512];
    assert(!fixture_find("0:/uci_temp"));
    assert(!c64_uci_path("0:/media", (const uint8_t *)"/Temp/bad.", 10, path));
    assert(!fixture_find("0:/uci_temp"));
    c64_uci_environment.callbacks.protected_path = protected_disk;
    fixture_file *disk = fixture_add("0:/media/Mounted Disk.d64", false, (const uint8_t *)"disk", 4);
    fixture_add("0:/media/.mtiec-save", false, (const uint8_t *)"journal", 7);
    fixture_add("0:/media/Long Directory", true, NULL, 0);
    mt_uci_target_t dos = c64_uci_dos_target(0);
    static const char *const invalid[] = {
        "Mounted Disk.d64.", "Mounted Disk.d64 ", "SUB./new.prg", "SUB /new.prg",
        ".mtiec-save.", ".mtiec-save ", "file.mtj.", "file.mtj "
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        assert(!c64_uci_path("0:/media", (const uint8_t *)invalid[i], strlen(invalid[i]), path));
        uint8_t request[128] = {10};
        strcpy((char *)request + 1, invalid[i]);
        finish(&dos, start(&dos, 2, request, strlen(invalid[i]) + 2));
        status("82,FILE NOT FOUND");
    }
    const char *blocked[] = {"MOUNTE~1.D64", "MTIECS~1.TMP"};
    for (unsigned i = 0; i < 2; i++) {
        uint8_t request[128] = {10};
        strcpy((char *)request + 1, blocked[i]);
        finish(&dos, start(&dos, 2, request, strlen(blocked[i]) + 2));
        status("98,FUNCTION PROHIBITED");
    }
    assert(disk->size == 4 && !memcmp(disk->bytes, "disk", 4));
    assert(fixture_find("0:/media/.mtiec-save")->size == 7);
    assert(c64_uci_path("0:/media", (const uint8_t *)"LONGDI~1/new.prg", 16, path));
    assert(!strcmp(path, "0:/media/Long Directory/new.prg"));
    open_file(&dos, "LONGDI~1/new.prg", 6);
    finish(&dos, start(&dos, 3, NULL, 0));
    assert(fixture_find("0:/media/Long Directory/new.prg"));
    /* Ordinary sidecars remain writable; this is an ownership fix. */
    open_file(&dos, "game.cfg", 6);
    finish(&dos, start(&dos, 3, NULL, 0));
    assert(fixture_find("0:/media/game.cfg"));
    puts("UCI FAT alias protection: 8 malformed names, 2 protected aliases, canonical create and cfg control passed");
}
static void temporary_paths(void)
{
    setup();
    fixture_add("0:/uci_temp", true, NULL, 0);
    fixture_add("0:/uci_temp/nested", true, NULL, 0);
    fixture_add("0:/uci_temp/nested/old.cfg", false, (const uint8_t *)"old", 3);
    fixture_add("0:/uci_temp/old.tmp", false, (const uint8_t *)"x", 1);
    fixture_add("0:/media/keep.prg", false, (const uint8_t *)"keep", 4);
    /* Launch quiesces immediately after initialization. It must not suppress
     * the pending startup cleanup, nor leave DIR handles across passes. */
    assert(c64_uci_service_quiesce() == 0);
    for (unsigned pass = 0; c64_uci_service_active() && pass < 16; pass++) {
        unsigned removed = fixture.removes;
        assert(c64_uci_service_poll(1) == 0);
        assert(fixture.removes <= removed + 1);
        for (unsigned i = 0; i < 4; i++)
            assert(!directories[i].used);
    }
    assert(!c64_uci_service_active());
    assert(fixture_find("0:/uci_temp") && !fixture_find("0:/uci_temp/nested"));
    assert(!fixture_find("0:/uci_temp/old.tmp") && fixture_find("0:/media/keep.prg"));
    char path[512];
    assert(c64_uci_path("0:/media", (const uint8_t *)"/usb*/SUB", 9, path));
    assert(!strcmp(path, "0:/media/SUB"));
    assert(c64_uci_path("0:/media", (const uint8_t *)"/*/SUB", 6, path));
    assert(!strcmp(path, "0:/media/SUB")); /* SD precedes Temp in alias order. */
    assert(c64_uci_path("0:/media", (const uint8_t *)"\xc7\xc1\xcd\xc5.PRG", 8, path));
    assert(!strcmp(path, "0:/media/GAME.PRG"));
    assert(c64_uci_path("0:/media", (const uint8_t *)"/Temp/new.cfg", 13, path));
    assert(!strcmp(path, "0:/uci_temp/new.cfg"));
    setup();
    assert(c64_uci_service_poll(1) == 0);
    assert(!fixture_find("0:/uci_temp") && !c64_uci_service_active());
    assert(c64_uci_path("0:/media", (const uint8_t *)"/Temp/new.cfg", 13, path));
    assert(fixture_find("0:/uci_temp"));
}
static void copy_home_validation(void)
{
    setup();
    mt_uci_target_t dos = c64_uci_dos_target(0);
    c64_uci_service_set_home("0:/media/SUB");
    finish(&dos, start(&dos, 0x17, NULL, 0));
    status("00,OK");
    finish(&dos, start(&dos, 0x12, NULL, 0));
    assert(reply.data_length == 5 && !memcmp(reply.data, "/SUB/", 5));
    fixture_add("0:/media/file", false, (const uint8_t *)"x", 1);
    fixture_add("0:/media/secret.mtj", true, NULL, 0);
    const char *invalid[] = {"0:/media/missing", "0:/media/file", "0:/media/secret.mtj"};
    for (unsigned i = 0; i < 3; i++) {
        c64_uci_service_set_home(invalid[i]);
        finish(&dos, start(&dos, 0x17, NULL, 0));
        status("83,NO SUCH DIRECTORY");
        finish(&dos, start(&dos, 0x12, NULL, 0));
        assert(reply.data_length == 5 && !memcmp(reply.data, "/SUB/", 5));
    }
    c64_uci_service_set_home("0:/media");
    finish(&dos, start(&dos, 0x17, NULL, 0));
    status("00,OK");
    finish(&dos, start(&dos, 0x12, NULL, 0));
    assert(reply.data_length == 1 && reply.data[0] == '/');
    /* A new scan may not discard an existing handle after a failed close. */
    assert(start(&dos, 0x13, NULL, 0) == MT_UCI_WAIT);
    directory_close_failures = 1;
    assert(start(&dos, 0x13, NULL, 0) == MT_UCI_FAILED);
    assert(directories[0].used && c64_uci_environment.fault);
    c64_uci_environment.fault = false;
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY);
    assert(!directories[0].used);
}
static void named_failure(const char *text, uint8_t low)
{
    status(text);
    assert(reply.data_length == 4 && reply.data[0] == low);
    assert(reply.data[1] == 255 && reply.data[2] == 255 && reply.data[3] == 255);
}
static void named_reu_validation(void)
{
    setup();
    c64_uci_service_set_reu_image("0:/media/image.reu");
    regs[MT_REG_C64_EXPANSION_CONFIG / 4] = 0;
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("84,REU NOT ENABLED", 254);
    regs[MT_REG_C64_EXPANSION_CONFIG / 4] = MT_C64_EXPANSION_REU_ENABLE;
    assert(c64_uci_named_reu_begin(false, &reply) == MT_UCI_REPLY);
    named_failure("85,REU FILE CANNOT BE OPENED", 255);
    fixture_add("0:/media/image.reu", false, (const uint8_t *)"original", 8);
    fixture_add("0:/media/image.bu", false, (const uint8_t *)"backup", 6);
    remove_failures = 1;
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("DISK ERROR", 255);
    assert(fixture_find("0:/media/image.reu") && fixture_find("0:/media/image.bu"));
    rename_failures = 1;
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("DISK ERROR", 255);
    assert(fixture_find("0:/media/image.reu") && !fixture_find("0:/media/image.bu"));
    mt_sd_file_t *busy = NULL;
    assert(mt_sd_file_open("0:/media/image.reu", "rb", &busy).status == MT_SD_OK);
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("85,REU FILE CANNOT BE OPENED", 255);
    assert(mt_sd_file_close(busy).status == MT_SD_OK);
    fixture_add("0:/media/image.bu", false, (const uint8_t *)"backup", 6);
    assert(mt_sd_file_open("0:/media/image.bu", "rb", &busy).status == MT_SD_OK);
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("98,FUNCTION PROHIBITED", 255);
    assert(mt_sd_file_close(busy).status == MT_SD_OK);
    c64_uci_service_set_reu_image("0:/media/image.bu");
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("98,FUNCTION PROHIBITED", 255);
    assert(fixture_find("0:/media/image.bu")->size == 6);
    c64_uci_service_set_reu_image("0:/media/SUB");
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("85,REU FILE CANNOT BE OPENED", 255);
    char long_name[512];
    memcpy(long_name, "0:/media/", 9);
    memset(long_name + 9, 'a', 501);
    long_name[209] = '/';
    long_name[410] = '/';
    long_name[510] = 0;
    c64_uci_service_set_reu_image(long_name);
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_REPLY);
    named_failure("98,FUNCTION PROHIBITED", 255);
    c64_uci_service_set_reu_image("0:/media/image.reu");
    assert(c64_uci_named_reu_begin(false, &reply) == MT_UCI_WAIT);
    mt_uci_result_t result = MT_UCI_WAIT;
    for (unsigned i = 0; result == MT_UCI_WAIT && i < 16; i++)
        result = c64_uci_named_reu_poll(&reply);
    assert(result == MT_UCI_REPLY);
    status("00,OK");
    assert(reply.data_length > 4 && reply.data[0] == 8);
    assert(!reply.data[1] && !reply.data[2] && !reply.data[3] && !memcmp(reu, "original", 8));
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    /* A failed SAVE close returns -1 and retains both HOLD and the handle. */
    assert(c64_uci_named_reu_begin(true, &reply) == MT_UCI_WAIT);
    assert(c64_uci_named_reu_poll(&reply) == MT_UCI_WAIT);
    fixture.fail_write = 25;
    file_close_failures = 1;
    mt_uci_result_t failed = MT_UCI_WAIT;
    for (unsigned i = 0; failed == MT_UCI_WAIT && i < 20000; i++)
        failed = c64_uci_named_reu_poll(&reply);
    assert(failed == MT_UCI_REPLY);
    status("DISK ERROR");
    assert(reply.data_length > 4 && !memcmp(reply.data, "\xff\xff\xff\xff", 4));
    assert(c64_uci_environment.fault && mt_sd_file_path_busy("0:/media/image.reu", true));
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    assert(fixture_find("0:/media/image.bu")->size == 8 &&
           !memcmp(fixture_find("0:/media/image.bu")->bytes, "original", 8));
    assert(c64_uci_service_quiesce() == -1);
    c64_uci_environment.fault = false;
    fixture.fail_write = 0;
    assert(c64_uci_named_reu_abort() == MT_UCI_REPLY);
    assert(!mt_sd_file_path_busy("0:/media/image.reu", true));
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    fixture_find("0:/media/image.reu")->size = 8;
    memcpy(fixture_find("0:/media/image.reu")->bytes, "original", 8);
    assert(c64_uci_named_reu_begin(false, &reply) == MT_UCI_WAIT);
    assert(c64_uci_named_reu_poll(&reply) == MT_UCI_WAIT);
    file_close_failures = 1;
    assert(c64_uci_named_reu_abort() == MT_UCI_FAILED && c64_uci_environment.fault);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    assert(mt_sd_file_path_busy("0:/media/image.reu", true));
    c64_uci_environment.fault = false;
    assert(c64_uci_named_reu_abort() == MT_UCI_REPLY);
    assert(!mt_sd_file_path_busy("0:/media/image.reu", true));
    puts("UCI COPY_HOME and named REU result/backup/close ownership cases passed");
}
#include "c64_uci_quiesce_cases.inc"
static bool mounted_tree(void *ctx, const char *path)
{
    (void)ctx;
    return fixture_equal(path, "0:/media") || fixture_equal(path, "0:/media/SUB") ||
           fixture_equal(path, "0:/media/SUB/disk.d64");
}
static void rename_pair(mt_uci_target_t *dos, const char *from, const char *to)
{
    uint8_t request[896];
    size_t a = strlen(from) + 1, b = strlen(to);
    assert(a + b <= sizeof request);
    memcpy(request, from, a);
    memcpy(request + a, to, b);
    finish(dos, start(dos, 10, request, a + b));
}
static void reviewer_regressions(void)
{
    setup();
    mt_uci_target_t dos = c64_uci_dos_target(0);
    fixture_add("0:/media/Long Directory", true, NULL, 0);
    const char *from[] = {".", "SUB", "SUB", "LONGDI~1", "/Temp"};
    const char *to[] = {"newroot", "SUB/child", "sub/child", "Long Directory/child", "/Temp/new"};
    for (unsigned i = 0; i < 5; i++) {
        rename_pair(&dos, from[i], to[i]);
        status("98,FUNCTION PROHIBITED");
        assert(fixture_find("0:/media") && fixture_find("0:/media/SUB"));
    }
    rename_pair(&dos, "SUB", "moved");
    status("00,OK");
    assert(fixture_find("0:/media/moved") && !fixture_find("0:/media/SUB"));

    setup();
    dos = c64_uci_dos_target(0);
    fixture_add("0:/media/SUB/disk.d64", false, (const uint8_t *)"disk", 4);
    fixture_add("0:/media/SUB/sibling.prg", false, (const uint8_t *)"PRG", 3);
    c64_uci_environment.callbacks.protected_path = mounted_tree;
    finish(&dos, start(&dos, 0x11, "SUB", 3)); status("00,OK");
    finish(&dos, start(&dos, 8, "disk.d64", 8)); status("00,OK");
    open_file(&dos, "sibling.prg", 1);
    finish(&dos, start(&dos, 4, (uint8_t[]){3, 0}, 2));
    assert(reply.data_length == 3 && !memcmp(reply.data, "PRG", 3));
    finish(&dos, start(&dos, 3, NULL, 0));
    finish(&dos, start(&dos, 2, (uint8_t[]){1,'d','i','s','k','.','d','6','4'}, 9));
    status("98,FUNCTION PROHIBITED");
    finish(&dos, start(&dos, 0x13, NULL, 0)); status("00,OK");
    rename_pair(&dos, ".", "../moved"); status("98,FUNCTION PROHIBITED");

    setup();
    regs[MT_REG_C64_EXPANSION_CAPS / 4] |= MT_C64_EXPANSION_CAPS_UCI_RAM_DMA | MT_C64_EXPANSION_CAPS_UCI_C64_IRQ;
    regs[MT_REG_C64_UCI_CONTROL / 4] |= MT_C64_UCI_CONTROL_HOLD;
    c64_uci_service_refresh_control();
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] == (9U | MT_C64_UCI_CONTROL_HOLD | MT_C64_UCI_CONTROL_FREEZE | MT_C64_UCI_CONTROL_IRQ));
    iec_active = false;
    c64_uci_service_refresh_control();
    assert((regs[MT_REG_C64_UCI_CONTROL / 4] & 31U) == 31U);
    regs[MT_REG_C64_EXPANSION_CONFIG / 4] &= ~MT_C64_EXPANSION_UCI_ENABLE;
    c64_uci_service_refresh_control();
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] == (31U | MT_C64_UCI_CONTROL_HOLD));
    regs[MT_REG_C64_UCI_CONTROL / 4] &= ~MT_C64_UCI_CONTROL_HOLD;

    fixture_add("0:/uci_temp", true, NULL, 0);
    fixture_add("0:/uci_temp/orphan", false, (const uint8_t *)"old", 3);
    remove_failures = 1;
    for (unsigned i = 0; i < 32; i++) assert(c64_uci_service_poll(32) == 0);
    assert(remove_failures == 1 && !c64_uci_environment.fault && !c64_uci_service_active());
    regs[MT_REG_C64_EXPANSION_CONFIG / 4] |= MT_C64_EXPANSION_UCI_ENABLE;
    assert(c64_uci_service_poll(1) == 0 && c64_uci_service_cleanup_warning());
    assert(!c64_uci_environment.fault && c64_uci_service_quiesce() == 0);
    assert(fixture_find("0:/uci_temp/orphan"));
    assert(c64_uci_service_retry() == 0);
    for (unsigned i = 0; c64_uci_service_active() && i < 8; i++) assert(c64_uci_service_poll(1) == 0);
    assert(!fixture_find("0:/uci_temp/orphan") && !c64_uci_service_cleanup_warning());

    /* Retry real retained target ownership through the public service API. */
    native_setup();
    fixture_add("0:/media/read", false, (const uint8_t *)"x", 1);
    const uint8_t open[] = {1, 2, 1, 'r','e','a','d'};
    native_push(open, sizeof open);
    for (unsigned i = 0; i < 4; i++) assert(c64_uci_service_poll(1) == 0);
    file_close_failures = 1;
    assert(c64_uci_service_quiesce() == -1 && mt_sd_file_path_busy("0:/media/read", true));
    int recovered = 1;
    for (unsigned i = 0; recovered == 1 && i < 32; i++) recovered = c64_uci_service_retry();
    assert(recovered == 0 && !c64_uci_environment.fault && !mt_sd_file_path_busy("0:/media/read", true));

    setup();
    uint8_t original[8193];
    for (unsigned i = 0; i < sizeof original; i++) original[i] = (uint8_t)(i ^ (i >> 8));
    fixture_add("0:/media/load", false, original, sizeof original);
    dos = c64_uci_dos_target(0);
    open_file(&dos, "load", 1);
    watch_storage = true;
    memset(reu, 0xcc, sizeof original);
    uint8_t request[] = {0,0,0,0,1,32,0,0};
    assert(start(&dos, 0x21, request, sizeof request) == MT_UCI_WAIT);
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT);
    assert(reu[0] == 0xcc && !(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY && reu[0] == 0xcc);
    finish(&dos, start(&dos, 6, (uint8_t[]){0,0,0,0}, 4));
    assert(start(&dos, 0x21, request, sizeof request) == MT_UCI_WAIT);
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT);
    fixture.fail_read = 25;
    finish(&dos, dos.poll(dos.context, &reply)); status("DISK ERROR");
    assert(reu[0] == 0xcc && !(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    fixture.fail_read = 0;
    finish(&dos, start(&dos, 6, (uint8_t[]){0,0,0,0}, 4));
    finish(&dos, start(&dos, 0x21, request, sizeof request)); status("00,OK");
    assert(!memcmp(reu, original, sizeof original));
    finish(&dos, start(&dos, 3, NULL, 0));
    open_file(&dos, "saved", 6);
    assert(start(&dos, 0x22, request, sizeof request) == MT_UCI_WAIT);
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT);
    while (regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD)
        assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT);
    memset(reu, 0x19, sizeof original); /* Guest may now change the live REU. */
    finish(&dos, dos.poll(dos.context, &reply)); status("00,OK");
    finish(&dos, start(&dos, 3, NULL, 0));
    fixture_file *saved = fixture_find("0:/media/saved");
    assert(saved && saved->size == sizeof original && !memcmp(saved->bytes, original, sizeof original));
    mt_uci_target_t control = c64_uci_control_target();
    for (unsigned i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)(i ^ 0x5a);
    finish(&control, start(&control, 15, "snapshot.bin", 12)); status("00,OK");
    saved = fixture_find("0:/media/snapshot.bin");
    assert(saved && saved->size == sizeof ram && !memcmp(saved->bytes, ram, sizeof ram));
    assert(observed_storage >= 25);
    watch_storage = false;
    assert(c64_uci_service_hold_begin() == 0);
    acknowledge_fence = false;
    assert(c64_uci_reu_fence() == 1);
    usec += 1000001;
    assert(c64_uci_reu_fence() == -1 && c64_uci_service_retry() == -1);
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    uint32_t fence = regs[MT_REG_C64_REU_HOST / 4];
    regs[MT_REG_C64_REU_HOST / 4] = fence | ((fence & 15U) << 8);
    assert(c64_uci_service_retry() == 0 && !c64_uci_service_reload_required());
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));

    native_setup();
    fixture_add("0:/media/read", false, (const uint8_t *)"x", 1);
    native_push(open, sizeof open);
    for (unsigned i = 0; i < 4; i++) assert(c64_uci_service_poll(1) == 0);
    c64_uci_service_hold_fault(); /* Unknown/partial stream has no provable late ACK. */
    assert(c64_uci_service_reload_required() && c64_uci_service_retry() == -1);
    assert(c64_uci_service_prepare_reload() == 1 && mt_sd_file_path_busy("0:/media/read", true));
    regs[MT_REG_CONTROL / 4] |= MT_CONTROL_CORE_RESET_REQUEST;
    assert(c64_uci_service_prepare_reload() == 1);
    regs[MT_REG_VIDEO_FRAME_STATUS / 4] |= MT_VIDEO_FRAME_STATUS_QUIESCENT;
    regs[MT_REG_C64_REU_HOST / 4] &= ~MT_C64_REU_HOST_IDLE;
    assert(c64_uci_service_prepare_reload() == 1);
    regs[MT_REG_C64_REU_HOST / 4] |= MT_C64_REU_HOST_IDLE;
    file_close_failures = 1;
    assert(c64_uci_service_prepare_reload() == -1 && mt_sd_file_path_busy("0:/media/read", true));
    assert(c64_uci_service_poll(32) == -1 && c64_uci_service_retry() == -1);
    assert(c64_uci_service_hold_begin() == -1);
    c64_uci_service_hold_end();
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    assert(c64_uci_service_prepare_reload() == 0 && !mt_sd_file_path_busy("0:/media/read", true));
    assert(c64_uci_service_reload_required() && c64_uci_service_quiesce() == -1);

    setup();
    char resolved[512];
    fixture_add("0:/media/match", false, (const uint8_t *)"x", 1);
    directory_close_failures = 1;
    assert(!c64_uci_path("0:/media", (const uint8_t *)"mat*", 4, resolved));
    assert(c64_uci_environment.fault && directories[0].used);
    assert(c64_uci_service_retry() == 0 && !directories[0].used);
    fixture_add("0:/uci_temp", true, NULL, 0);
    directory_close_failures = 1;
    assert(c64_uci_service_poll(1) == -1 && c64_uci_service_cleanup_warning());
    assert(c64_uci_service_retry() == 0);
    assert(c64_uci_service_poll(1) == 0 && !c64_uci_environment.fault);
    puts("UCI review regressions: rename roots/self/alias, mounted metadata/navigation, policy ID/Freeze/IRQ, off cleanup, retained-handle retry, staged REU/RAM snapshots and hardware-fault refusal passed");
}
static void retry_clean(void)
{
    int rc = 1;
    for (unsigned i = 0; rc == 1 && i < 64; i++)
        rc = c64_uci_service_retry();
    assert(rc == 0 && !c64_uci_environment.fault);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
}
static void encode_gcr(const uint8_t *in, unsigned bytes, uint8_t *out)
{
    static const uint8_t code[16] = {10,11,18,19,14,15,22,23,9,25,26,27,13,29,30,21};
    memset(out, 0, bytes * 10 / 8);
    unsigned bit = 0;
    for (unsigned i = 0; i < bytes; i++)
        for (unsigned half = 0; half < 2; half++) {
            unsigned value = code[(in[i] >> (half ? 0 : 4)) & 15];
            for (unsigned j = 0; j < 5; j++, bit++)
                out[bit / 8] |= (uint8_t)(((value >> (4 - j)) & 1) << (7 - bit % 8));
        }
}
static void wrapper_failure_recovery(void)
{
    /* Actual DOS wrapper, including the error-after-successful-f_close case. */
    for (unsigned released = 0; released < 2; released++) {
        native_setup();
        reu[0xbb0e] = 0x84; reu[0x7dc7] = 0x20;
        reu[0xbafe] = 0; reu[0xbb02] = 0x90; reu[0x9084] = 1;
        memset(reu + 0x300000, 0x5a, 65536);
        fixture_add("0:/media/image.dnp", false, reu + 0x300000, 65536);
        native_push((uint8_t[]){1,0x41,0,'i','m','a','g','e','.','d','n','p'}, 12);
        file_close_failures = !released;
        file_close_released_failures = released;
        int result = 0;
        for (unsigned i = 0; result == 0 && i < 200; i++)
            result = c64_uci_service_poll(1);
        assert(result == -1 && c64_uci_environment.fault);
        assert(mt_sd_file_path_busy("0:/media/image.dnp", true) == !released);
        retry_clean();
        assert(!mt_sd_file_path_busy("0:/media/image.dnp", true));
        native_push((uint8_t[]){4,0x40}, 2);
        for (unsigned i = 0; i < 8; i++) assert(c64_uci_service_poll(1) == 0);
        assert((regs[MT_REG_C64_UCI_STATUS / 4] & 3U) == 3U);
    }
    /* Control's private named-REU context must remain reachable after its
     * failure reply. A public retry, not a helper-only abort, releases it. */
    native_setup();
    fixture_add("0:/media/image.reu", false, (const uint8_t *)"old", 3);
    c64_uci_service_set_reu_image("0:/media/image.reu");
    native_push((uint8_t[]){4,9,0,0,0}, 5);
    fixture.fail_write = 25;
    file_close_failures = 1;
    int result = 0;
    for (unsigned i = 0; result == 0 && i < 20000; i++)
        result = c64_uci_service_poll(1);
    assert(result == -1 && c64_uci_environment.fault);
    assert(mt_sd_file_path_busy("0:/media/image.reu", true));
    assert(c64_uci_named_reu_begin(false, &reply) == MT_UCI_FAILED);
    assert(mt_sd_file_path_busy("0:/media/image.reu", true));
    fixture.fail_write = 0;
    retry_clean();
    assert(!mt_sd_file_path_busy("0:/media/image.reu", true));
    /* Actual Control GCR decoder writes a sector, then times out on its
     * cache fence. The exact late ACK permits cleanup through the wrapper. */
    native_setup();
    uint8_t raw[400], header[8] = {8,0,0,1,0x12,0x34,15,15}, data[260] = {7};
    memset(raw, 0x55, sizeof raw); memset(raw, 255, 5);
    encode_gcr(header, 8, raw + 5); memset(raw + 24, 255, 5);
    for (unsigned i = 1; i <= 256; i++) data[i] = (uint8_t)(i - 1);
    encode_gcr(data, 260, raw + 29);
    memcpy(reu + 65536, raw, sizeof raw);
    memset(reu + 131072, 0xff, 256);
    uint8_t request[14] = {4,0x11,1,1,0,0,1,0,0,0,2,0,144,1};
    acknowledge_fence = false;
    native_push(request, sizeof request);
    result = 0;
    for (unsigned i = 0; result == 0 && i < 1000; i++) {
        result = c64_uci_service_poll(1);
        if (drains) usec += 1000001;
    }
    assert(result == -1 && c64_uci_service_reload_required());
    for (unsigned i = 0; i < 256; i++) assert(reu[131072 + i] == i);
    assert(c64_uci_service_retry() == -1);
    uint32_t token = regs[MT_REG_C64_REU_HOST / 4] & 15U;
    regs[MT_REG_C64_REU_HOST / 4] |= token << 8;
    acknowledge_fence = true;
    retry_clean();
    native_push((uint8_t[]){4,0x40}, 2);
    for (unsigned i = 0; i < 8; i++) assert(c64_uci_service_poll(1) == 0);
    assert((regs[MT_REG_C64_UCI_STATUS / 4] & 3U) == 3U);
    puts("UCI wrapper recovery: DOS GEOS live/released close failures, Control named REU owner, Control GCR late fence ACK passed");
}
int main(void)
{
    wrapper_failure_recovery();
    reviewer_regressions();
    quiesce_cases();
    path_alias_protection();
    copy_home_validation();
    named_reu_validation();
    temporary_paths();
    setup();
    assert(c64_uci_service_available());
    char path[512];
    assert(c64_uci_path("0:/media/SUB", (const uint8_t *)"../../root", 10, path));
    assert(!strcmp(path, "0:/media/root"));
    assert(c64_uci_path("0:/media", (const uint8_t *)"/Usb2/SUB", 9, path));
    assert(!strcmp(path, "0:/media/SUB"));
    assert(!c64_uci_path("0:/media", (const uint8_t *)"0:/secret", 9, path));
    assert(!c64_uci_path("0:/media", (const uint8_t *)".MTIEC-save", 11, path));
    assert(c64_uci_protected("0:/media/test.mtj", true));
    uint8_t data[1200];
    for (unsigned i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)i;
    fixture_add("0:/media/one.prg", false, data, sizeof data);
    fixture_add("0:/media/secret.mtj", false, data, 8);
    assert(c64_uci_path("0:/media", (const uint8_t *)"o*.prg", 6, path));
    assert(!strcmp(path, "0:/media/one.prg"));
    mt_uci_target_t dos = c64_uci_dos_target(0), second = c64_uci_dos_target(1);
    open_file(&dos, "one.prg", 1);
    uint8_t count[] = {0xb0, 4};
    finish(&dos, start(&dos, 4, count, 2));
    assert(reply.more && reply.data_length == 512 && reply.status_length == 0);
    assert(!memcmp(reply.data, data, 512));
    finish(&dos, dos.next(dos.context, 512, &reply));
    assert(reply.more && reply.data_length == 512);
    finish(&dos, dos.next(dos.context, 512, &reply));
    assert(!reply.more && reply.data_length == 176 && !memcmp(reply.data, data + 1024, 176));
    finish(&dos, start(&dos, 6, (uint8_t[]){0, 0, 0, 0}, 4));
    status("00,OK");
    finish(&dos, start(&dos, 7, NULL, 0));
    assert(reply.data_length == 19 && reply.data[0] == 0xb0 && reply.data[1] == 4 &&
           reply.data[4] == 0x18);
    finish(&dos, start(&dos, 3, NULL, 0));
    status("00,OK");
    finish(&second, start(&second, 3, NULL, 0));
    status("84,NO FILE TO CLOSE");
    finish(&dos, start(&dos, 0x13, NULL, 0));
    status("00,OK");
    finish(&dos, start(&dos, 0x14, NULL, 0));
    assert(reply.more && reply.data[0] == 16);
    finish(&dos, dos.next(dos.context, reply.data_length, &reply));
    assert(!reply.more && !memcmp(reply.data + 1, "one.prg", 7));
    status("00,OK");
    finish(&second, start(&second, 0x11, "SUB", 3));
    status("00,OK");
    finish(&dos, start(&dos, 0x12, NULL, 0));
    assert(reply.data_length == 1 && reply.data[0] == '/');
    finish(&second, start(&second, 0x12, NULL, 0));
    assert(reply.data_length == 5 && !memcmp(reply.data, "/SUB/", 5));
    finish(&dos, start(&dos, 11, "one.prg\0SUB/copy.prg", 20));
    status("00,OK");
    fixture_file *copy = fixture_find("0:/media/SUB/copy.prg");
    assert(copy && copy->size == 1200 && !memcmp(copy->bytes, data, 1200));
    finish(&dos, start(&dos, 11, "one.prg\0SUB/copy.prg", 20));
    status("FILE EXISTS");
    open_file(&dos, "one.prg", 1);
    uint8_t transfer[] = {0xfe, 0xff, 0xff, 0, 8, 0, 0, 0};
    finish(&dos, start(&dos, 0x21, transfer, 8));
    status("02,REQUEST TRUNCATED");
    assert(reu[0xfffffe] == 0 && reu[0xffffff] == 1 && drains == 1);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    finish(&dos, start(&dos, 6, (uint8_t[]){0, 0, 0, 0}, 4));
    uint8_t transfer2[] = {0, 0, 0, 0, 0xb0, 4, 0, 0};
    assert(start(&dos, 0x21, transfer2, 8) == MT_UCI_WAIT);
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT);
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT); /* HOLD accepted */
    assert(dos.poll(dos.context, &reply) == MT_UCI_WAIT); /* RAM changed, fence pending */
    assert(dos.abort(dos.context, 0) == MT_UCI_WAIT);
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY);
    assert(!memcmp(reu, data, sizeof data));
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    mt_uci_target_t control = c64_uci_control_target();
    assert(start(&control, 5, NULL, 0) == MT_UCI_WAIT);
    c64_uci_action_t action;
    unsigned device;
    assert(c64_uci_service_action(&action, path, sizeof path, &device) &&
           action == C64_UCI_ACTION_FREEZE);
    c64_uci_service_action_done(0);
    finish(&control, control.poll(control.context, &reply));
    assert(!reply.status_length);
    assert(start(&control, 0x0f, "dump.bin", 8) == MT_UCI_WAIT);
    assert(control.poll(control.context, &reply) == MT_UCI_WAIT);
    acknowledge_ram = false;
    assert(control.poll(control.context, &reply) == MT_UCI_WAIT);
    assert(control.abort(control.context, 0) == MT_UCI_WAIT);
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    regs[MT_REG_C64_UCI_STATUS2 / 4] = (uint32_t)ram_request_token << 24;
    assert(control.abort(control.context, 0) == MT_UCI_REPLY);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    /* Actual DOS and Control dispatch into the GEOS helper, preserving the
     * second DOS channel cwd and completing the same physical cache fence. */
    reu[0xbb0e] = 0x84;
    reu[0x7dc7] = 0x20;
    reu[0xbafe] = 0;
    reu[0xbb02] = 0x90;
    reu[0x9084] = 1;
    for (unsigned i = 0; i < 65536; i++)
        reu[0x300000 + i] = (uint8_t)(i ^ (i >> 8));
    fixture_add("0:/media/SUB/ram.dnp", false, reu + 0x300000, 65536);
    finish(&control, start(&control, 0x40, NULL, 0));
    status("00,OK");
    assert(reply.data_length == 8 && reply.data[0] == 0xdd && reply.data[1] == 1);
    uint8_t geosload[] = {0, 'r', 'a', 'm', '.', 'd', 'n', 'p'};
    finish(&second, start(&second, 0x41, geosload, sizeof geosload));
    status("00,OK");
    assert(!memcmp(reu + 0x200000, reu + 0x300000, 65536));
    uint8_t geossave[] = {0, 's', 'a', 'v', 'e', 'd', '.', 'd', 'n', 'p'};
    finish(&second, start(&second, 0x42, geossave, sizeof geossave));
    status("00,OK");
    fixture_file *saved = fixture_find("0:/media/SUB/saved.dnp");
    assert(saved && saved->size == 65536 && !memcmp(saved->bytes, reu + 0x200000, 65536));
    uint8_t no_sync[] = {1, 1, 0, 0, 0x40, 0, 0, 0, 0x50, 0, 10, 0};
    memset(reu + 0x400000, 0x55, 10);
    finish(&control, start(&control, 0x11, no_sync, sizeof no_sync));
    status("82,ERRORS ON TRACK");
    assert(reply.data_length == 1 && reply.data[0] == 0);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    /* Directory and RAM-dump close failures retain the still-live handle.
     * Test-only fault clearing below permits a retry; production never clears
     * the sticky lifecycle fault automatically. */
    directory_close_failures = 1;
    unsigned close_before = directory_close_calls;
    mt_uci_result_t scan = start(&dos, 0x13, NULL, 0);
    for (unsigned i = 0; scan == MT_UCI_WAIT && i < 32; i++)
        scan = dos.poll(dos.context, &reply);
    assert(scan == MT_UCI_FAILED && c64_uci_environment.fault &&
           directory_close_calls == close_before + 1);
    assert(directories[0].used && c64_uci_service_quiesce() == -1);
    c64_uci_environment.fault = false;
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY);
    assert(!directories[0].used && directory_close_calls == close_before + 2);
    acknowledge_ram = true;
    assert(start(&control, 0x0f, "close-fail.bin", 14) == MT_UCI_WAIT);
    file_close_failures = 1;
    close_before = file_close_calls;
    mt_uci_result_t dumped = MT_UCI_WAIT;
    for (unsigned i = 0; dumped == MT_UCI_WAIT && i < 2048; i++)
        dumped = control.poll(control.context, &reply);
    assert(dumped == MT_UCI_FAILED && c64_uci_environment.fault &&
           file_close_calls == close_before + 1);
    assert(mt_sd_file_path_busy("0:/media/close-fail.bin", true));
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    assert(c64_uci_service_quiesce() == -1);
    c64_uci_environment.fault = false;
    assert(control.abort(control.context, 0) == MT_UCI_REPLY);
    assert(!mt_sd_file_path_busy("0:/media/close-fail.bin", true) &&
           file_close_calls == close_before + 2);
    assert(!(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD));
    assert(start(&control, 0x0f, "abort-fail.bin", 14) == MT_UCI_WAIT);
    file_close_failures = 1;
    close_before = file_close_calls;
    assert(control.abort(control.context, 0) == MT_UCI_FAILED && c64_uci_environment.fault);
    assert(mt_sd_file_path_busy("0:/media/abort-fail.bin", true));
    c64_uci_environment.fault = false;
    assert(control.abort(control.context, 0) == MT_UCI_REPLY);
    assert(file_close_calls == close_before + 2 &&
           !mt_sd_file_path_busy("0:/media/abort-fail.bin", true));
    /* Failed coherence acknowledgement retains HOLD; no runnable stale cache. */
    assert(c64_uci_service_hold_begin() == 0);
    acknowledge_fence = false;
    assert(c64_uci_reu_fence() == 1);
    usec += 1000001;
    assert(c64_uci_reu_fence() == -1);
    c64_uci_service_hold_end();
    assert(regs[MT_REG_C64_UCI_CONTROL / 4] & MT_C64_UCI_CONTROL_HOLD);
    assert(c64_uci_service_poll(1) == -1);
    printf("C64 UCI platform: paths, DOS, independent channels, copy, metadata, REU fence/abort, "
           "posted actions, RAM cancellation passed (%u reads, %u writes)\n",
           reads, writes);
    return 0;
}
