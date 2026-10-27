/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_title_service.h"
#include "sd_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t file[MT_C64_CFG_BYTES];
static size_t file_size;
static bool exists, busy, fail_apply, fail_save, cartridge;
static unsigned applied_count, recoveries, writes;
static mt_c64_title_values_t hardware;
static char expected_path[512] = "0:/package/media/Game.cfg";
bool c64_content_service_crt_attached(void)
{
    return cartridge;
}
bool mt_sd_file_path_busy(const char *p, bool exclusive)
{
    (void)p;
    assert(exclusive);
    return busy;
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *p, const char *j, uint8_t *b, size_t n)
{
    (void)p;
    (void)j;
    (void)b;
    (void)n;
    ++recoveries;
    return (mt_sd_result_t){MT_SD_OK, 0, 0};
}
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *p, uint8_t *b, size_t n,
                                                    bool *found)
{
    *found = exists && strcmp(p, expected_path) == 0;
    if (!*found)
        return (mt_sd_result_t){MT_SD_OK, 0, 0};
    assert(file_size <= n);
    memcpy(b, file, file_size);
    return (mt_sd_result_t){MT_SD_OK, 0, file_size};
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *p, const char *j, const uint8_t *b,
                                             size_t n)
{
    (void)j;
    assert(strcmp(p, expected_path) == 0);
    ++writes;
    if (fail_save)
        return (mt_sd_result_t){MT_SD_SYNC_FAILED, 1, 0};
    memcpy(file, b, n);
    file_size = n;
    exists = true;
    return (mt_sd_result_t){MT_SD_OK, 0, n};
}
static int apply(void *p, const mt_c64_title_values_t *v, bool live, bool event)
{
    (void)p;
    (void)live;
    (void)event;
    ++applied_count;
    if (fail_apply)
        return -1;
    hardware = *v;
    return 0;
}
static void set_file(const char *s)
{
    memcpy(file, s, strlen(s));
    file_size = strlen(s);
    exists = true;
}
int main(void)
{
    mt_c64_title_values_t stored;
    mt_c64_title_defaults(&stored);
    mt_c64_capabilities_t caps = {UINT32_MAX, UINT32_MAX, UINT32_MAX, true, true, true, true, true};
    caps.expansion &= ~MT_C64_EXPANSION_CAPS_CART_COEXIST;
    const c64_title_callbacks_t cb = {NULL, apply};
    assert(c64_title_service_init("0:/package/media", &cb) == 0);
    c64_title_service_refresh(&stored, &caps, false, true);
    set_file("[U64 Specific Settings]\nCPU Speed=16\nTurbo Control=Manual\n");
    const mt_c64_launch_hook_t *hook = c64_title_service_prepare("Game.prg", MT_TITLE_PRG, true);
    assert(hook && hook->hold_reset && c64_title_service_launch_allowed());
    assert(c64_title_service_overlay()->owned == 0 && applied_count == 0);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_CONFIG_FAILED);
    assert(c64_title_service_overlay()->owned == 0 && applied_count == 0);
    hook = c64_title_service_prepare("Game.prg", MT_TITLE_PRG, true);
    assert(hook->commit(hook->context) == 0);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(hardware.turbo && mt_c64_speed_mhz(hardware.detail & 15) == 16);
    assert(strcmp(c64_title_service_session(), "Game.prg") == 0);
    mt_c64_title_values_t changed = hardware;
    changed.detail = 0x4f;
    assert(c64_title_service_edit(MT_TITLE_SPEED, &changed));
    mt_c64_title_values_t requested, actual;
    c64_title_service_effective(&requested, &actual, false);
    assert((requested.detail & 15) == 15 && stored.detail == 0x41);
    fail_save = true;
    assert(c64_title_service_save() < 0);
    assert(writes == 1);
    fail_save = false;
    assert(c64_title_service_save() == 0);
    assert(writes == 2);
    assert(hardware.detail == 0x4f);
    fail_apply = true;
    assert(c64_title_service_revert() < 0);
    assert(c64_title_service_overlay()->owned != 0);
    fail_apply = false;
    assert(c64_title_service_revert() == 0);
    assert(c64_title_service_overlay()->owned == 0);
    assert(hardware.detail == stored.detail && !hardware.turbo);
    c64_title_service_refresh(&stored, &caps, false, false);
    assert(c64_title_service_prepare("Game.prg", MT_TITLE_PRG, true) == NULL);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(c64_title_service_apply("Game.cfg", false, NULL, 0) ==
           0); /* Off only controls automatic lookup. */
    unsigned before = applied_count;
    fail_apply = true;
    set_file("[U64 Specific Settings]\nCPU Speed=2\n");
    assert(c64_title_service_apply("Game.cfg", false, NULL, 0) == 87);
    assert(applied_count == before + 1);
    c64_title_service_effective(&requested, &actual, false);
    assert((requested.detail & 15) == 15);
    fail_apply = false;
    busy = true;
    before = recoveries;
    assert(c64_title_service_apply("Game.cfg", false, NULL, 0) == 88);
    assert(recoveries == before);
    busy = false;
    assert(c64_title_service_apply("0:/elsewhere/Game.cfg", true, NULL, 0) == 88);
    assert(c64_title_service_apply("../Game.cfg", false, NULL, 0) == 88);
    assert(c64_title_service_apply("0:/package/media/Game.cfg", true, NULL, 0) == 0);
    {
        char log[1024];
        set_file("[U64 Specific Settings]\nCPU Speed=16\n");
        memset(log, 0xcc, sizeof(log));
        assert(c64_title_service_apply("0:/package/media/Game.cfg", true, log, sizeof(log)) == 0);
        assert(!log[0]); /* Ultimate returns no parse log on full success. */
        assert(strstr(c64_title_service_status(), "Title settings:"));
        set_file("[Other Hardware]\nFuture Option=Enabled\n"
                 "[C64 and Cartridge Settings]\nCustom Kernal ROM=other.rom\n");
        assert(c64_title_service_apply("0:/package/media/Game.cfg", true, log, sizeof(log)) == 0);
        assert(strstr(log, "unknown section") && strstr(log, "Custom Kernal ROM"));
        assert(!strstr(log, "Title settings:"));
        set_file("[U64 Specific Settings]\nCPU Speed=16\nSystem Mode=NTSC\n");
        before = applied_count;
        assert(c64_title_service_apply("0:/package/media/Game.cfg", true, log, sizeof(log)) == 89);
        assert(strstr(log, "different video image required") && applied_count == before);
        set_file("[U64 Specific Settings]\n");
        for (unsigned n = 0; n < 16; ++n) {
            const char unknown[] = "Unknown setting with a long name=foo\n";
            memcpy(file + file_size, unknown, sizeof(unknown) - 1);
            file_size += sizeof(unknown) - 1;
        }
        memset(log, 0xcc, sizeof(log));
        assert(c64_title_service_apply("0:/package/media/Game.cfg", true, log, sizeof(log)) == 0);
        assert(strlen(log) == 511 && (unsigned char)log[512] == 0xcc);
    }
    set_file("[U64 Specific Settings]\nCPU Speed=oops\n");
    before = applied_count;
    assert(c64_title_service_apply("Game.cfg", false, NULL, 0) == 89);
    assert(applied_count == before);
    assert(c64_title_service_revert() == 0);
    c64_title_service_refresh(&stored, &caps, false, true);
    set_file("[C64 and Cartridge Settings]\nRAM Expansion Unit=Disabled\n");
    hook = c64_title_service_prepare("Game.crt", MT_TITLE_CRT, true);
    assert(hook && c64_title_service_launch_allowed());
    assert(hook->commit(hook->context) == 0);
    cartridge = true;
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(!hardware.reu);
    assert(c64_title_service_revert() == 0);
    assert(!hardware.reu); /* requested REU remains saved-on while CRT attached. */
    hook = c64_title_service_prepare_detach();
    cartridge = false;
    assert(hook->commit(hook->context) == 0);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(hardware.reu && !*c64_title_service_session());
    set_file("[U64 Specific Settings]\nCPU Speed=32\n");
    before = applied_count;
    unsigned before_recovery = recoveries, before_writes = writes;
    char report[2048];
    assert(c64_title_service_test("Game.cfg", report, sizeof report) == 0);
    assert(strstr(report, "Dry run:") && strstr(report, "0 errors"));
    assert(applied_count == before && recoveries == before_recovery && writes == before_writes);
    assert(!*c64_title_service_session() && !c64_title_service_overlay()->owned);
    set_file("[U64 Specific Settings]\nCPU Speed=invalid\n");
    assert(c64_title_service_test("Game.cfg", report, sizeof report) < 0);
    assert(applied_count == before && recoveries == before_recovery && writes == before_writes);
    /* U64 exports include mixer channels that this board does not implement.
     * Preserve those lines without silently discarding edits to real SIDs. */
    assert(c64_title_service_init("0:/package/media", &cb) == 0);
    c64_title_service_refresh(&stored, &caps, false, true);
    set_file("[SID Addressing]\r\nUltiSID 1 Address=$D400\r\n"
             "UltiSID 2 Address=$D500\r\n[Audio Mixer]\r\n"
             "Vol Drive 1=-27 dB\r\nVol UltiSID 1=0 dB\r\n"
             "Pan Drive 1=Left\r\n;keep this comment\r\n");
    hook = c64_title_service_prepare("Game.prg", MT_TITLE_PRG, true);
    assert(hook && hook->commit(hook->context) == 0);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    mt_c64_sid_model_t edited_sid;
    assert(mt_c64_sid_preset(&edited_sid, MT_SID_EIGHT));
    edited_sid.sid_volume[0] = 13;
    edited_sid.sid_pan[7] = 9;
    c64_title_service_edit_sid(&edited_sid, UINT64_MAX, 7);
    c64_title_service_effective(&requested, &actual, false);
    const mt_c64_sid_model_t expected_sid = actual.sid;
    assert(c64_title_service_save() == 0);
    assert(memcmp(&hardware.sid, &expected_sid, sizeof(expected_sid)) == 0);
    assert(file_size < sizeof(file));
    file[file_size] = 0;
    assert(strstr((const char *)file, "Vol Drive 1=-27 dB\r\n"));
    assert(strstr((const char *)file, "Pan Drive 1=Left\r\n;keep this comment\r\n"));
    assert(c64_title_service_revert() == 0);
    hook = c64_title_service_prepare("Game.prg", MT_TITLE_PRG, true);
    assert(hook && hook->commit(hook->context) == 0);
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(memcmp(&hardware.sid, &expected_sid, sizeof(expected_sid)) == 0);
    fail_apply = true;
    assert(c64_title_service_reboot(false) < 0);
    assert(*c64_title_service_session() && c64_title_service_overlay()->owned);
    fail_apply = false;
    assert(c64_title_service_reboot(false) == 0);
    assert(!*c64_title_service_session() && !c64_title_service_overlay()->owned);
    assert(memcmp(&hardware.sid, &stored.sid, sizeof stored.sid) == 0);
    assert(hardware.reu == stored.reu);
    assert(c64_title_service_reboot(true) == 0);
    assert(!hardware.reu && !hardware.ua);
    caps.expansion |= MT_C64_EXPANSION_CAPS_CART_COEXIST;
    stored.reu = stored.ua = true;
    c64_title_service_refresh(&stored, &caps, false, true);
    exists = false;
    cartridge = false;
    hook = c64_title_service_prepare("Game.crt", MT_TITLE_CRT, true);
    assert(c64_title_service_launch_allowed());
    /* No title edits require no apply; the already configured requested
     * registers are sufficient for the hardware's cartridge policy. */
    if (hook) assert(hook->commit(hook->context) == 0);
    cartridge = true;
    c64_title_service_finish(MT_C64_CONTENT_SERVICE_OK);
    assert(c64_title_service_revert() == 0 && hardware.reu && hardware.ua);
    assert(c64_title_service_reboot(true) == 0 && hardware.reu && hardware.ua);
    set_file("[C64 and Cartridge Settings]\nRAM Expansion Unit=Enabled\nMap Ultimate Audio $DF20-DFFF=Enabled\n");
    assert(c64_title_service_apply("Game.cfg", false, NULL, 0) == 0);
    assert(hardware.reu && hardware.ua && stored.reu && stored.ua);
    puts("C64 title launch/apply/failed-hold/save/revert/reboot/busy-file/root isolation service PASS");
    return 0;
}
