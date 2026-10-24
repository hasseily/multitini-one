/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_iec_fixture.h"
#include "c64_iec_service.h"
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
static uint32_t control, abi = 25, status_token;
static bool tx_ready = true, transport_fault;
static unsigned disable_with_rx, journal_replays, journal_writes;
static bool fail_journal;
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
static uint32_t read32(void *ctx, uint32_t reg)
{
    (void)ctx;
    if (reg == MT_REG_CORE_ID)
        return MT_C64_CORE_ID;
    if (reg == MT_REG_ABI_VERSION)
        return abi;
    if (reg == MT_REG_C64_IEC_CAPS)
        return MT_C64_IEC_CAPS_DEVICE9;
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
    printf("PASS C64 IEC service: %u checks\n", checks);
    return 0;
}
