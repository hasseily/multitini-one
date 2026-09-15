/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_vectrex_boot.h"
#include "multitini_vectrex_hid_input.h"
#include "multitini_vectrex_interface.h"
#include "multitini_core_frontend.h"
#include "multitini_media_browser.h"
#include "vectrex_content_service.h"
#include "vectrex32_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t regs[MT_REGISTER_SPAN / 4U];
    unsigned reset_count, completed[256];
    uint32_t kind, size, offset;
    uint8_t packet[16], mode;
    uint8_t line[9];
    int reset, fail_kind, block_kind;
    uint64_t now;
    unsigned services, stream_services, aborts;
} shell_t;
static shell_t shell;
static int bios_missing, file_fail;
static size_t cart_size = 32769U;
static unsigned basic_runs, asset_reads;
static unsigned content_reads, bios_reads;
static const char *expected_program_path = "0:/games/vectrex/test.bas";
static const char *expected_basic_root = "0:/games/vectrex";
static const char *expected_basic_name = "test.bas";
static const char *expected_cart_path = "0:/games/vectrex/cart.BIN";
static const char *expected_asset_path = "0:/games/vectrex/asset.txt";
static const char *program_override;
static char last_content_path[512], last_asset_path[512];
static unsigned stop_after_services = 1000U;
static unsigned stop_after_stream_services = 1000U;
static uint16_t input_values[5];
static unsigned input_events;
static int input_fail;
static int console_input[32];
static size_t console_input_size, console_input_position, console_output_size;
static uint8_t console_output[128];
static int console_empty = -2, console_write_status;
struct mt_sd_file { bool open, writable, append, eof; size_t position; unsigned disk; };
static struct mt_sd_file fake_handles[16];
static uint8_t saved_data[256];
static size_t saved_size;
static char saved_name[96];
static char printed[4096];
static unsigned file_opens, file_closes, file_removes, file_renames;
static bool fail_file_write, fail_file_sync, fail_file_seek, fail_file_rename, fail_file_close;
static const char program[] =
    "call ScaleSprite(10)\ncall MoveSprite(2,-3)\ncall IntensitySprite(60)\n"
    "x=LinesSprite({{MoveTo,0,0},{DrawTo,3,4}})\n"
    "c=WaitForFrame(JoystickAnalog,Controller1,3)\n"
    "call Sound({{7,255}})\nc=WaitForFrame(JoystickAnalog,Controller1,3)\n"
    "f=FOpen(\"asset.txt\",\"r\")\ns=FGets(f)\ncall FClose(f)\n";

static uint32_t read32(void *ctx, uint32_t offset)
{ return ((shell_t *)ctx)->regs[offset / 4U]; }

static void write32(void *ctx, uint32_t offset, uint32_t value)
{
    shell_t *s = ctx;
    s->regs[offset / 4U] = value;
    if (offset == MT_REG_COMMAND_DOORBELL) {
        uint32_t op = s->regs[MT_REG_COMMAND_OPCODE / 4U];
        uint32_t a = s->regs[MT_REG_COMMAND_ARG0 / 4U];
        uint32_t b = s->regs[MT_REG_COMMAND_ARG1 / 4U];
        unsigned response = MT_RESPONSE_OK;
        if (op == MT_COMMAND_CORE_RESET) {
            s->reset = a != 0U; ++s->reset_count;
            s->regs[MT_REG_STATUS / 4U] = s->reset ? 0U : MT_STATUS_CORE_READY;
        } else if (op == MT_COMMAND_STREAM_BEGIN) {
            s->kind = a; s->size = b; s->offset = 0U;
            memset(s->packet, 0, sizeof(s->packet));
            if ((int)a == s->fail_kind) response = MT_RESPONSE_BUSY;
        } else if (op == MT_COMMAND_STREAM_DATA) {
            if ((int)s->kind == s->block_kind) response = MT_RESPONSE_BUSY;
            else {
                if (s->offset < sizeof(s->packet)) s->packet[s->offset] = (uint8_t)a;
                ++s->offset;
            }
        } else if (op == MT_COMMAND_STREAM_ABORT) {
            ++s->aborts;
        } else if (op == MT_COMMAND_STREAM_END) {
            assert(s->offset == s->size);
            ++s->completed[s->kind];
            if (s->kind == MT_LOAD_VECTREX_MODE) s->mode = s->packet[0];
            if (s->kind == MT_LOAD_VECTREX_LINE) memcpy(s->line, s->packet, sizeof(s->line));
        }
        s->regs[MT_REG_RESPONSE_STATUS / 4U] = response;
        s->regs[MT_REG_RESPONSE_SEQ / 4U] = value;
    }
}

mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *data, size_t size)
{
    mt_sd_result_t r = {MT_SD_OK, 0U, size};
    ++bios_reads;
    assert(strcmp(path, MT_VECTREX_BIOS_PATH) == 0);
    assert(size == MT_SIZE_VECTREX_BIOS);
    if (bios_missing) { r.status = MT_SD_OPEN_FAILED; return r; }
    memset(data, 0x42, size); return r;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *data, size_t cap)
{
    mt_sd_result_t r = {MT_SD_OK, 0U, cart_size};
    ++content_reads; assert(strlen(path) < sizeof(last_content_path)); strcpy(last_content_path, path);
    if (file_fail) { r.status = MT_SD_READ_FAILED; return r; }
    if (cap == MT_SIZE_VECTREX_BASIC_MAX) {
        const char *source = program_override != NULL ? program_override : program;
        assert(strcmp(path, expected_program_path) == 0);
        r.size = strlen(source); assert(cap >= r.size);
        memcpy(data, source, r.size);
    } else if (strstr(path, "asset.txt") != NULL) {
        assert(strcmp(path, "0:/games/vectrex/asset.txt") == 0);
        ++asset_reads; r.size = 3U; memcpy(data, "hi\n", 3U);
    } else {
        assert(strcmp(path, expected_cart_path) == 0);
        if (r.size > cap) { r.status = MT_SD_TOO_LARGE; return r; }
        memset(data, 0xA5, r.size);
    }
    return r;
}

static mt_sd_result_t file_result(mt_sd_status_t status, size_t size)
{ mt_sd_result_t value = {status, 0U, size}; return value; }
mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **out)
{
    static const char root[] = "0:/games/vectrex/";
    unsigned index;
    bool asset;
    asset = strcmp(path, expected_asset_path) == 0;
    if (asset) {
        assert(strlen(path) < sizeof(last_asset_path)); strcpy(last_asset_path, path);
    } else {
        assert(strncmp(path, root, sizeof(root) - 1U) == 0);
        path += sizeof(root) - 1U;
    }
    *out = NULL;
    if (!asset && mode[0] == 'r' && strcmp(path, saved_name) != 0)
        return file_result(MT_SD_OPEN_FAILED, 0U);
    if (!asset && (mode[0] == 'w' || mode[0] == 'a')) {
        if (mode[0] == 'w' || strcmp(path, saved_name) != 0) saved_size = 0U;
        assert(strlen(path) < sizeof(saved_name)); strcpy(saved_name, path);
    }
    for (index = 0U; index < 16U && fake_handles[index].open; ++index) { }
    if (index == 16U) return file_result(MT_SD_FILE_LIMIT, 0U);
    memset(&fake_handles[index], 0, sizeof(fake_handles[index]));
    fake_handles[index].open = true; fake_handles[index].disk = asset ? 0U : 1U;
    fake_handles[index].writable = mode[0] != 'r' || strchr(mode, '+') != NULL;
    fake_handles[index].append = mode[0] == 'a';
    if (asset) ++asset_reads;
    *out = &fake_handles[index]; ++file_opens;
    return file_result(MT_SD_OK, 0U);
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *file, void *data, size_t size)
{
    const size_t length = file->disk == 0U ? 3U : saved_size;
    size_t received = file->position >= length ? 0U : length - file->position;
    assert(file->open);
    if (received > size) received = size;
    if (received != 0U) memcpy(data, (file->disk == 0U ? (const uint8_t *)"hi\n" : saved_data) + file->position, received);
    file->position += received; if (received < size) file->eof = true;
    return file_result(MT_SD_OK, received);
}
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *file, const void *data, size_t size)
{
    assert(file->open);
    if (!file->writable || fail_file_write) return file_result(MT_SD_WRITE_FAILED, 0U);
    if (file->append) file->position = saved_size;
    assert(file->position + size <= sizeof(saved_data));
    if (file->position > saved_size) memset(saved_data + saved_size, 0, file->position - saved_size);
    memcpy(saved_data + file->position, data, size); file->position += size;
    if (file->position > saved_size) saved_size = file->position;
    return file_result(fail_file_sync ? MT_SD_SYNC_FAILED : MT_SD_OK, size);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *file, int64_t offset, int origin)
{
    int64_t base = origin == 0 ? 0 : (origin == 1 ? (int64_t)file->position :
        (file->disk == 0U ? 3 : (int64_t)saved_size));
    assert(file->open);
    if (fail_file_seek || base + offset < 0) return file_result(MT_SD_SEEK_FAILED, 0U);
    file->position = (size_t)(base + offset); file->eof = false;
    return file_result(MT_SD_OK, 0U);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *file)
{ return file->open ? (int64_t)file->position : -1; }
int mt_sd_file_eof(const mt_sd_file_t *file)
{ return file->open ? (file->eof ? 1 : 0) : -1; }
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file)
{
    assert(file->open);
    file->open = false; ++file_closes;
    return file_result(fail_file_close ? MT_SD_SYNC_FAILED : MT_SD_OK, 0U);
}
mt_sd_result_t mt_sd_file_remove(const char *path)
{
    assert(strncmp(path, "0:/games/vectrex/", 17U) == 0);
    assert(strcmp(path + 17U, saved_name) == 0);
    saved_name[0] = '\0'; ++file_removes; return file_result(MT_SD_OK, 0U);
}
mt_sd_result_t mt_sd_file_rename(const char *old_path, const char *new_path)
{
    assert(strncmp(old_path, "0:/games/vectrex/", 17U) == 0);
    assert(strncmp(new_path, "0:/games/vectrex/", 17U) == 0);
    assert(strcmp(old_path + 17U, saved_name) == 0);
    if (fail_file_rename) return file_result(MT_SD_RENAME_FAILED, 0U);
    strcpy(saved_name, new_path + 17U); ++file_renames; return file_result(MT_SD_OK, 0U);
}

static int emit(void *ctx, uint16_t code, uint16_t value)
{
    (void)ctx;
    assert((code >> 8U) == MT_INPUT_VECTREX && (code & 255U) < 5U);
    if (input_fail) return -1;
    input_values[code & 255U] = value; ++input_events; return 0;
}

static int service(void *ctx)
{ shell_t *s = ctx; ++s->services; return s->services < stop_after_services; }
static int stream_service(void *ctx)
{ shell_t *s = ctx; ++s->stream_services; return s->stream_services < stop_after_stream_services; }
static uint64_t milliseconds(void *ctx)
{ return ++((shell_t *)ctx)->now; }
static void controls(void *ctx, int out[2][6])
{ (void)ctx; memset(out, 0, sizeof(int) * 12U); out[0][0] = -32768; out[0][2] = 1; }
static void output_text(void *ctx, const char *message)
{
    (void)ctx;
    assert(strlen(printed) + strlen(message) < sizeof(printed)); strcat(printed, message);
}
static int console_read(void *ctx)
{
    (void)ctx;
    return console_input_position < console_input_size ?
        console_input[console_input_position++] : console_empty;
}
static int console_write(void *ctx, const void *data, size_t size)
{
    (void)ctx;
    if (console_write_status != 0) return console_write_status;
    assert(console_output_size + size <= sizeof(console_output));
    memcpy(console_output + console_output_size, data, size); console_output_size += size;
    return 0;
}
static int basic(void *ctx, const char *root, const char *name, const char *source)
{
    assert(strcmp(root, expected_basic_root) == 0 && strcmp(name, expected_basic_name) == 0);
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_BASIC_FAILED);
    assert(vectrex_content_service_load("test.bas").status == MT_VECTREX_CONTENT_BASIC_FAILED);
    ++basic_runs; return vectrex32_service_run(ctx, root, name, source);
}

static void reject_content_path(const char *path)
{
    const unsigned reads = content_reads, rom_reads = bios_reads;
    const unsigned resets = shell.reset_count, runs = basic_runs;
    const bool was_attached = vectrex_content_service_cartridge_attached();
    const bool was_basic = vectrex_content_service_basic_selected();
    char attached_before[MT_VECTREX_CONTENT_PATH_CAPACITY];
    strcpy(attached_before, vectrex_content_service_cartridge_name());
    assert(vectrex_content_service_load(path).status == MT_VECTREX_CONTENT_INVALID_ARGUMENT);
    assert(content_reads == reads && bios_reads == rom_reads && shell.reset_count == resets && basic_runs == runs);
    assert(vectrex_content_service_cartridge_attached() == was_attached &&
        vectrex_content_service_basic_selected() == was_basic &&
        strcmp(vectrex_content_service_cartridge_name(), attached_before) == 0);
}

int main(void)
{
    mt_core_link_t link;
    mt_vectrex_hid_input_t input;
    mt_content_info_t info;
    const uint8_t keys[] = {0x1dU, 0x1bU, 0x06U, 0x19U, 0x52U};
    uint8_t report[20] = {0U, 20U};
    uint8_t buttons;
    int16_t x, y;
    unsigned commands;
    int run_status;
    uint64_t start_ms;
    const mt_vectrex32_platform_ops_t ops = {&shell, service, milliseconds, controls, output_text, stream_service,
        console_read, console_write};
    assert(mt_content_classify("cart.bin") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify_for_core(MT_VECTREX_CORE_ID, "cart.BIN") == MT_CONTENT_VECTREX_CART);
    assert(mt_content_classify_for_core(0U, "cart.BIN") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify("test.BAS") == MT_CONTENT_VECTREX_BASIC);
    assert(mt_core_frontend_accepts_content(MT_VECTREX_CORE_ID, MT_MENU_BROWSER_PROGRAM,
        MT_CONTENT_VECTREX_BASIC));
    assert(!mt_core_frontend_accepts_content(MT_VECTREX_CORE_ID, MT_MENU_BROWSER_DRIVE8,
        MT_CONTENT_D64));
    assert(mt_content_inspect(MT_CONTENT_VECTREX_CART, report, 0U, &info) == MT_CONTENT_TOO_SMALL);
    assert(mt_content_inspect(MT_CONTENT_VECTREX_CART, report, 65537U, &info) == MT_CONTENT_WRONG_SIZE);
    assert(mt_content_inspect(MT_CONTENT_VECTREX_BASIC, report, sizeof(report), &info) == MT_CONTENT_BAD_STRUCTURE);

    mt_vectrex_hid_input_init(&input, emit, NULL);
    assert(mt_vectrex_hid_input_poll(&input) == 0 && input_events == 5U);
    assert(mt_vectrex_hid_keyboard_report(&input, 0U, 0U, keys, sizeof(keys)) == 0);
    assert(input_values[0] == 15U && input_values[2] == 32767U);
    assert(mt_vectrex_hid_gamepad_report(&input, 2U, 1U, -12345, 23456) == 0);
    assert(mt_vectrex_hid_gamepad_report(&input, 3U, 8U, 1111, -2222) == 0);
    assert(input_values[0] == 0x8fU && input_values[3] == 1111U);
    assert(mt_vectrex_hid_disconnect(&input, 0U) == 0);
    assert(input_values[0] == 0x81U && input_values[2] == 23456U);
    {
        const unsigned before = input_events;
        /* Reset may discard an already acknowledged hardware mailbox entry.
         * Ready false->true invalidates the cache and resends every field. */
        mt_vectrex_hid_reassert(&input);
        input_fail = 1;
        assert(mt_vectrex_hid_input_poll(&input) != 0 && input_events == before);
        input_fail = 0;
        assert(mt_vectrex_hid_input_poll(&input) == 0 && input_events == before + 5U);
        assert(input_values[0] == 0x81U && input_values[1] == (uint16_t)-12345 &&
            input_values[2] == 23456U && input_values[3] == 1111U && input_values[4] == (uint16_t)-2222);
        assert(mt_vectrex_hid_input_poll(&input) == 0 && input_events == before + 5U);
    }
    input_fail = 1;
    assert(mt_vectrex_hid_release_all(&input) != 0);
    input_fail = 0;
    assert(mt_vectrex_hid_input_poll(&input) == 0 && input_values[0] == 0U && input_values[4] == 0U);
    assert(mt_vectrex_axis_normalize(0, 0, 255) == INT16_MIN);
    assert(mt_vectrex_axis_normalize(128, 0, 255) == 0);
    assert(mt_vectrex_axis_normalize(INT32_MAX, INT32_MIN, INT32_MAX) == INT16_MAX);
    report[3] = 0xf0U; report[7] = 0x80U; report[9] = 0x7fU; report[8] = 0xffU;
    assert(mt_vectrex_xinput_decode(report, 20U, &buttons, &x, &y) == 0);
    assert(buttons == 15U && x == INT16_MIN && y == INT16_MAX);
    assert(mt_vectrex_xinput_decode(report, 19U, &buttons, &x, &y) != 0);

    mt_core_link_init(&link, &shell, read32, write32);
    vectrex32_service_init(&link, &ops);
    vectrex_content_service_init(&link, "0:/games/vectrex", NULL, NULL, basic, NULL);
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_NO_BASIC_PROGRAM);
    bios_missing = 1;
    assert(!vectrex_content_service_read_bios(0xe000U, report, 1U));
    assert(vectrex_content_service_boot_bios().status == MT_VECTREX_CONTENT_READ_FAILED);
    assert(shell.reset_count == 0U);
    assert(vectrex_content_service_load("../cart.bin").status == MT_VECTREX_CONTENT_INVALID_ARGUMENT);
    assert(vectrex_content_service_load("cart.BIN").status == MT_VECTREX_CONTENT_READ_FAILED);
    bios_missing = 0;
    assert(vectrex_content_service_load("cart.BIN").status == MT_VECTREX_CONTENT_OK);
    assert(vectrex_content_service_cartridge_attached() && shell.reset == 0);
    assert(vectrex_content_service_read_bios(0xfff8U, report, 8U) && report[0] == 0x42U);
    assert(!vectrex_content_service_read_bios(0xfff8U, report, 9U));
    assert(!vectrex_content_service_read_bios(0xdfffU, report, 1U));
    assert(shell.completed[MT_LOAD_VECTREX_BIOS] == 1U && shell.completed[MT_LOAD_VECTREX_CART] == 1U);
    commands = shell.reset_count; cart_size = 65537U;
    assert(vectrex_content_service_load("cart.BIN").status == MT_VECTREX_CONTENT_READ_FAILED);
    assert(shell.reset_count == commands && vectrex_content_service_cartridge_attached());
    cart_size = 8192U; shell.fail_kind = MT_LOAD_VECTREX_CART;
    assert(vectrex_content_service_load("cart.BIN").status == MT_VECTREX_CONTENT_TRANSFER_FAILED);
    assert(shell.reset && !vectrex_content_service_cartridge_attached());
    shell.fail_kind = 0;
    assert(vectrex_content_service_detach().status == MT_VECTREX_CONTENT_OK && shell.mode == 2U);
    bios_missing = 1;
    assert(vectrex_content_service_load("test.bas").status == MT_VECTREX_CONTENT_OK);
    assert(basic_runs == 1U && asset_reads == 1U && shell.mode == 1U);
    assert(shell.completed[MT_LOAD_VECTREX_LINE] == 2U && shell.line[0] == 24U && shell.line[8] == 120U);
    assert(shell.completed[MT_LOAD_VECTREX_PSG] >= 4U);
    assert(shell.now >= 17U); /* 60 Hz frame callback waited on platform time. */
    assert(vectrex_content_service_basic_selected());
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_OK);
    assert(basic_runs == 2U && asset_reads == 2U && shell.mode == 1U);
    commands = shell.reset_count; file_fail = 1;
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_READ_FAILED);
    assert(shell.reset_count == commands && basic_runs == 2U);
    file_fail = 0;
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_OK);
    assert(basic_runs == 3U);
    shell.services = 0U; stop_after_services = 3U;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "loop.bas",
        "while 1\na=1\nendwhile\n") == 0);
    assert(shell.services == 3U); /* Same cooperative stop used by F12. */
    stop_after_services = 1000U; shell.services = 0U;
    shell.fail_kind = MT_LOAD_VECTREX_LINE;
    assert(vectrex_content_service_load("test.bas").status == MT_VECTREX_CONTENT_BASIC_FAILED);
    assert(strcmp(vectrex32_service_error(), "Core stream transfer failed") == 0);
    assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_NO_BASIC_PROGRAM);
    shell.fail_kind = MT_LOAD_VECTREX_PSG;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "end.bas", "a=1\n") != 0);
    assert(strcmp(vectrex32_service_error(), "Core stream transfer failed") == 0);
    shell.fail_kind = 0;
    shell.block_kind = MT_LOAD_VECTREX_LINE; shell.stream_services = 0U;
    stop_after_stream_services = 2U; commands = shell.aborts;
    assert(vectrex_content_service_load("test.bas").status == MT_VECTREX_CONTENT_OK);
    assert(shell.stream_services == 2U && shell.aborts == commands + 1U);
    assert(vectrex32_service_error()[0] == '\0'); /* F12 while waiting is intentional. */
    shell.block_kind = 0; stop_after_stream_services = 1000U;
    start_ms = shell.now; commands = shell.completed[MT_LOAD_VECTREX_PSG];
    run_status = vectrex32_service_run(NULL, "0:/games/vectrex", "rate.bas",
        "call SetFrameRate(150)\ncall Sound({{7,255}})\nc=WaitForFrame(JoystickAnalog,Controller1,3)\nc=WaitForFrame(JoystickAnalog,Controller1,3)\n");
    if (run_status != 0) fprintf(stderr, "Frame-rate test: %s\n", vectrex32_service_error());
    assert(run_status == 0);
    assert(shell.now - start_ms >= 13U && shell.completed[MT_LOAD_VECTREX_PSG] == commands + 4U);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "ratebad.bas",
        "call SetFrameRate(0)\nc=WaitForFrame(JoystickAnalog,Controller1,3)\n") != 0);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "bad.bas",
        "call CodeSprite({256})\n") != 0);
    printed[0] = '\0'; shell.services = 0U;
    run_status = vectrex32_service_run(NULL, "0:/games/vectrex", "files.bas",
        "f=FOpen(\"save.dat\",\"wb+\")\ncall FPuts(\"abc\",f)\nprint FTell(f)\n"
        "call FSeek(f,0,0)\nprint FRead(3,f)\ncall FSeek(f,0,2)\ncall FWrite(\"!\",1,f)\ncall FClose(f)\n"
        "call Rename(\"save.dat\",\"renamed.dat\")\nf=FOpen(\"renamed.dat\",\"ab\")\n"
        "call FPuts(\"?\",f)\ncall FClose(f)\nf=FOpen(\"renamed.dat\",\"rb\")\n"
        "print FRead(8,f)\nprint FEof(f)\ncall FClose(f)\ncall Remove(\"renamed.dat\")\n");
    if (run_status != 0) fprintf(stderr, "File API test: %s\n", vectrex32_service_error());
    assert(run_status == 0 && file_removes == 1U && file_renames == 1U);
    assert(saved_size == 5U && memcmp(saved_data, "abc!?", 5U) == 0);
    assert(strstr(printed, "abc!?") != NULL && file_opens == file_closes);
    commands = file_opens;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "escape.bas",
        "f=FOpen(\"../escape\",\"wb\")\n") != 0);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "escape.bas",
        "f=FOpen(\".. /escape\",\"wb\")\n") != 0);
    assert(file_opens == commands);
    printed[0] = '\0'; fail_file_write = true; fail_file_seek = true;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "failure.bas",
        "f=FOpen(\"save.dat\",\"w+\")\nprint FPuts(\"x\",f)\nprint FSeek(f,0,0)\ncall FClose(f)\n") == 0);
    assert(strstr(printed, "-1") != NULL);
    fail_file_write = false; fail_file_seek = false; fail_file_close = true;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "closefail.bas",
        "f=FOpen(\"save.dat\",\"rb\")\n") != 0);
    fail_file_close = false;
    assert(file_opens == file_closes);
    printed[0] = '\0'; fail_file_rename = true;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "renamefail.bas",
        "print Rename(\"save.dat\",\"other.dat\")\n") == 0);
    assert(strcmp(saved_name, "save.dat") == 0 && strstr(printed, "0") != NULL);
    fail_file_rename = false;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "invalid.bas", NULL) != 0);
    printed[0] = '\0'; shell.now = 2499U;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "clock.bas", "print GetTickCount()\n") == 0);
    assert(strstr(printed, "2400") != NULL); /* 2501 ms converts to 960 Hz ticks. */
    printed[0] = '\0'; shell.now = UINT64_C(4473924265);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "clockwrap.bas", "print GetTickCount()\n") == 0);
    assert(printed[0] == '0'); /* Tick counter wraps at 2^32. */
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "endpoints.bas",
        "call ScaleSprite(255)\ncall IntensitySprite(127)\n"
        "s=LinesSprite({{MoveTo,-1000,-1000},{DrawTo,1000,1000}})\nc=WaitForFrame(0,0,0)\n") == 0);
    assert(shell.line[0] == 0U && shell.line[1] == 0x80U && shell.line[2] == 0U && shell.line[3] == 0x80U);
    assert(shell.line[4] == 0xffU && shell.line[5] == 0x7fU && shell.line[6] == 0xffU && shell.line[7] == 0x7fU);
    assert(shell.line[8] == 255U);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "dark.bas",
        "call IntensitySprite(0)\ns=LinesSprite({{MoveTo,0,0},{DrawTo,1,1}})\nc=WaitForFrame(0,0,0)\n") == 0);
    assert(shell.line[8] == 0U);
    saved_name[0] = '\0'; strcpy(saved_name, "binary.dat");
    saved_data[0] = 0U; saved_data[1] = 'A'; saved_size = 2U; printed[0] = '\0';
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "binary.bas",
        "f=FOpen(\"binary.dat\",\"rb\")\ns=FRead(2,f)\nprint Len(s)\ncall FClose(f)\n"
        "f=FOpen(\"copy.dat\",\"wb\")\ncall FWrite(s,2,f)\ncall FClose(f)\n") == 0);
    assert(saved_size == 2U && saved_data[0] == 0U && saved_data[1] == 'A' && strstr(printed, "2") != NULL);
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "oversizeread.bas",
        "f=FOpen(\"copy.dat\",\"rb\")\ns=FRead(65536,f)\n") != 0);
    fail_file_sync = true;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "syncfail.bas",
        "f=FOpen(\"copy.dat\",\"wb\")\nn=FWrite(\"allbytes\",8,f)\n") != 0);
    fail_file_sync = false;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "lifetime.bas",
        "a={0,0,0}\ncall CameraTranslate(a)\nf=FOpen(\"asset.txt\",\"r\")\na[1]=f\nf=nil\n") == 0);
    assert(file_opens == file_closes);
    shell.services = 0U; printed[0] = '\0';
    console_input[0] = -1; console_input[1] = -1; console_input[2] = 0;
    console_input[3] = 'A'; console_input[4] = 3;
    console_input_size = 5U; console_input_position = 0U;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "terminal.bas",
        "call BreakOnControlC(0)\ns=FRead(3,Stdin)\nprint Len(s)\n"
        "call FWrite(s,3,Stdout)\ncall FPuts(\"!\",Stdout)\nprint FEof(Stdin)\n"
        "print FGetc(Stdin)\nprint FEof(Stdin)\n") == 0);
    assert(shell.services >= 2U && console_output_size == 4U &&
        console_output[0] == 0U && console_output[1] == 'A' && console_output[2] == 3U && console_output[3] == '!');
    assert(strstr(printed, "3") != NULL && strstr(printed, "-1") != NULL);
    console_input_position = 4U; shell.services = 0U; printed[0] = '\0';
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "controlc.bas",
        "c=FGetc(Stdin)\nprint \"should-not-run\"\n") == 0);
    assert(strstr(printed, "should-not-run") == NULL);
    console_input_position = console_input_size; console_empty = -1;
    shell.services = 0U; stop_after_services = 3U;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "terminalcancel.bas", "c=FGetc(Stdin)\n") == 0);
    assert(shell.services == 3U); stop_after_services = 1000U;
    console_empty = -3; shell.services = 0U;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "terminalerror.bas", "c=FGetc(Stdin)\n") != 0);
    console_empty = -2; console_write_status = -1;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "terminalwriteerror.bas", "call FPuts(\"x\",Stdout)\n") != 0);
    console_write_status = -2;
    assert(vectrex32_service_run(NULL, "0:/games/vectrex", "terminalwritecancel.bas", "call FPuts(\"x\",Stdout)\n") == 0);
    console_write_status = 0;
    {
        static const char *invalid[] = {
            NULL, "", "/rom.vec", "\\rom.vec", "0:/rom.vec", "C:\\rom.vec",
            "../rom.vec", "./rom.vec", "games/../rom.vec", "games/./rom.vec",
            "games//rom.vec", "games\\\\rom.vec", "games/\\rom.vec", "games/", "games\\",
            "games./rom.vec", "games /rom.vec", "games/rom.vec.", "games/rom.vec ",
            "games/*.vec", "games/ro?.vec", "games/ro|m.vec", "games/ro\"m.vec",
            "games/<rom>.vec", "games/rom:vec", "games/ro\x01m.vec", "games/ro\x7fm.vec"
        };
        unsigned i, before;
        char long_relative[257], long_root[257], absolute[512], asset_path[512];
        char basic_directory[512];
        mt_media_browser_t browser;
        mt_menu_t menu;
        mt_menu_view_t view = {0};
        mt_menu_file_t menu_file = {"Vecalabeth", 0U, MT_MENU_FILE_DIRECTORY};
        mt_menu_action_t action;
        char resolved[MT_VECTREX_CONTENT_PATH_CAPACITY];
        bios_missing = 0; shell.services = 0U;
        vectrex_content_service_init(&link, "0:/media", NULL, NULL, basic, NULL);
        expected_program_path = "0:/media/Vecalabeth/vec.bas";
        expected_basic_root = "0:/media/Vecalabeth"; expected_basic_name = "vec.bas";
        expected_asset_path = "0:/media/Vecalabeth/asset.txt";
        before = asset_reads;
        view.active_core_id = "vectrex"; view.files = &menu_file; view.file_count = 1U;
        mt_menu_init(&menu, &view);
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_F12, true);
        mt_menu_open_browser(&menu, MT_MENU_BROWSER_PROGRAM);
        action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
        assert(action.kind == MT_MENU_ACTION_OPEN_DIRECTORY && action.file_index == 0U);
        assert(mt_media_browser_init(&browser, "0:/media") && mt_media_browser_enter(&browser, menu_file.name));
        strcpy(menu_file.name, "vec.bas"); menu_file.cookie = 0U;
        action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
        assert(action.kind == MT_MENU_ACTION_LOAD_CONTENT);
        assert(mt_media_browser_resolve(&browser, menu_file.name, resolved, sizeof(resolved)));
        assert(strcmp(resolved, "Vecalabeth/vec.bas") == 0);
        assert(vectrex_content_service_load(resolved).status == MT_VECTREX_CONTENT_OK);
        assert(strcmp(last_content_path, expected_program_path) == 0 &&
            strcmp(last_asset_path, expected_asset_path) == 0 && asset_reads == before + 1U);
        assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_OK);
        assert(strcmp(last_content_path, expected_program_path) == 0 && asset_reads == before + 2U);
        assert(vectrex_content_service_load("Vecalabeth\\vec.bas").status == MT_VECTREX_CONTENT_OK);
        assert(strcmp(last_content_path, expected_program_path) == 0 && asset_reads == before + 3U);
        for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) reject_content_path(invalid[i]);
        /* Rejected candidates must not replace the saved BASIC restart path. */
        assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_OK);
        assert(strcmp(last_content_path, expected_program_path) == 0 && asset_reads == before + 4U);

        expected_cart_path = "0:/media/Arcade/berzerk.vec";
        assert(vectrex_content_service_load("Arcade\\berzerk.vec").status == MT_VECTREX_CONTENT_OK);
        assert(strcmp(vectrex_content_service_cartridge_name(), "Arcade/berzerk.vec") == 0);
        assert(!vectrex_content_service_basic_selected());
        assert(vectrex_content_service_load(vectrex_content_service_cartridge_name()).status == MT_VECTREX_CONTENT_OK);
        for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) reject_content_path(invalid[i]);

        memset(long_relative, 'x', 256U); long_relative[256] = '\0';
        reject_content_path(long_relative);
        long_relative[256] = 'x'; /* The bounded validator also handles a full buffer without NUL. */
        reject_content_path(long_relative);
        memset(long_root, 'r', 255U); memcpy(long_root, "0:/", 3U); long_root[255] = '\0';
        memset(long_relative, 'g', 247U); memcpy(long_relative + 247U, "/rom.vec", 9U);
        assert(strlen(long_relative) == 255U);
        memcpy(absolute, long_root, 255U); absolute[255] = '/'; memcpy(absolute + 256U, long_relative, 256U);
        assert(strlen(absolute) == 511U);
        expected_cart_path = absolute;
        vectrex_content_service_init(&link, long_root, NULL, NULL, basic, NULL);
        assert(vectrex_content_service_load(long_relative).status == MT_VECTREX_CONTENT_OK);
        assert(strlen(vectrex_content_service_cartridge_name()) == 255U && strlen(last_content_path) == 511U);
        assert(vectrex_content_service_load(vectrex_content_service_cartridge_name()).status == MT_VECTREX_CONTENT_OK);

        /* A 505-byte containing directory leaves five bytes for an asset name
         * in the board's 512-byte absolute-path buffer, including slash/NUL. */
        memset(long_relative, 'g', 249U); memcpy(long_relative + 249U, "/v.bas", 7U);
        memcpy(absolute, long_root, 255U); absolute[255] = '/'; memcpy(absolute + 256U, long_relative, 256U);
        memcpy(basic_directory, absolute, 505U); basic_directory[505] = '\0';
        memcpy(asset_path, basic_directory, 505U); memcpy(asset_path + 505U, "/a.dat", 7U);
        assert(strlen(absolute) == 511U && strlen(asset_path) == 511U);
        expected_program_path = absolute; expected_basic_root = basic_directory; expected_basic_name = "v.bas";
        expected_asset_path = asset_path;
        program_override = "f=FOpen(\"a.dat\",\"r\")\ns=FGets(f)\ncall FClose(f)\nf=FOpen(\"aa.dat\",\"r\")\n";
        before = file_opens;
        {
            const mt_vectrex_content_result_t loaded = vectrex_content_service_load(long_relative);
            if (loaded.status != MT_VECTREX_CONTENT_OK)
                fprintf(stderr, "Long BASIC path: %s / %s\n", vectrex_content_status_name(loaded.status), vectrex32_service_error());
            assert(loaded.status == MT_VECTREX_CONTENT_OK);
        }
        assert(file_opens == before + 1U && strlen(last_asset_path) == 511U);
        assert(vectrex_content_service_restart_basic().status == MT_VECTREX_CONTENT_OK);
        assert(file_opens == before + 2U);
        long_root[255] = 'r'; long_root[256] = '\0';
        vectrex_content_service_init(&link, long_root, NULL, NULL, basic, NULL);
        reject_content_path("rom.vec");
    }
    puts("Vectrex firmware content, input, boot failure and BASIC streaming tests passed");
    return 0;
}
