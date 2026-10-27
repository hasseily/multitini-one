/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the service's register/storage fixture and command driver. This
 * executable provisions thousands of tiny files instead of REU payloads. */
#define F_CAP 5008
#define F_BYTES 16
#define main service_regression_main
#define mt_sd_directory_next fixture_directory_next
#include "test_c64_uci_service.c"
#undef mt_sd_directory_next
#undef main

mt_sd_result_t mt_sd_directory_next(mt_sd_directory_t *d, mt_sd_info_t *info)
{
    mt_sd_result_t r = fixture_directory_next(d, info);
    if (r.status == MT_SD_OK && info->name[0] && !(info->attributes & 16))
        info->attributes |= 7; /* read-only, hidden and system all survive UCI. */
    return r;
}
static void check_path(mt_uci_target_t *dos, const char *expected)
{
    finish(dos, start(dos, 0x12, NULL, 0));
    status("00,OK");
    assert(reply.data_length == strlen(expected));
    assert(!memcmp(reply.data, expected, reply.data_length));
}
static void change_path(mt_uci_target_t *dos, const char *name, const char *expected)
{
    finish(dos, start(dos, 0x11, name, strlen(name)));
    status("00,OK");
    check_path(dos, expected);
}
static void virtual_devices(void)
{
    setup();
    fixture_find("0:/media/SUB")->exists = false;
    mt_uci_target_t dos = c64_uci_dos_target(0);
    mt_uci_target_t second = c64_uci_dos_target(1);
    check_path(&dos, "/SD/");
    change_path(&dos, "/", "/");
    finish(&dos, start(&dos, 0x13, NULL, 0)); status("00,OK");
    finish(&dos, start(&dos, 0x14, NULL, 0));
    assert(reply.data_length == 3 && reply.data[0] == 16 &&
           !memcmp(reply.data + 1, "SD", 2) && reply.more);
    finish(&dos, dos.next(dos.context, reply.data_length, &reply));
    assert(reply.data_length == 5 && reply.data[0] == 16 &&
           !memcmp(reply.data + 1, "Temp", 4) && !reply.more);
    status("00,OK");
    /* Replay and abort retain the snapshot and hold no storage handle. */
    finish(&dos, start(&dos, 0x14, NULL, 0));
    assert(reply.more && !memcmp(reply.data + 1, "SD", 2));
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY);
    for (unsigned i = 0; i < 4; i++) assert(!directories[i].used);
    check_path(&second, "/SD/");
    change_path(&dos, "SD", "/SD/");
    finish(&dos, start(&dos, 0x13, NULL, 0)); status("01,DIRECTORY EMPTY");
    finish(&dos, start(&dos, 0x14, NULL, 0)); status("01,DIRECTORY EMPTY");
    fixture_add("0:/media/GAMES", true, NULL, 0);
    change_path(&dos, "GAMES", "/SD/GAMES/");
    change_path(&dos, "..", "/SD/");
    change_path(&dos, "..", "/");
    change_path(&dos, ".././", "/");
    change_path(&dos, "/SD/../Temp", "/Temp/");
    open_file(&dos, "new.cfg", 6); status("00,OK");
    finish(&dos, start(&dos, 3, NULL, 0)); status("00,OK");
    char temporary_file[512];
    snprintf(temporary_file, sizeof temporary_file, "%s/new.cfg", c64_uci_environment.temporary);
    assert(fixture_find(temporary_file));
    change_path(&dos, "../SD/GAMES", "/SD/GAMES/");
    /* Existing absolute paths and USB aliases still use the confined SD. */
    change_path(&dos, "/GAMES", "/SD/GAMES/");
    change_path(&dos, "/Usb0", "/SD/");
    change_path(&dos, "0:/media/GAMES", "/SD/GAMES/");
    change_path(&dos, "../../", "/");
    const char *invalid[] = {"/SD/bad./../GAMES", "/SD/secret.mtj/../GAMES", "0:/outside"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        finish(&dos, start(&dos, 0x11, invalid[i], strlen(invalid[i])));
        status("83,NO SUCH DIRECTORY");
        check_path(&dos, "/");
    }
    c64_uci_service_set_home("0:/media/GAMES");
    finish(&dos, start(&dos, 0x17, NULL, 0)); status("00,OK");
    check_path(&dos, "/SD/GAMES/");
    change_path(&dos, "/", "/");
    dos.reset(dos.context);
    check_path(&dos, "/SD/");
    finish(&dos, start(&dos, 0x14, NULL, 0)); status("01,DIRECTORY EMPTY");
    second.reset(second.context);
}
int main(void)
{
    virtual_devices();
    setup();
    mt_uci_target_t dos = c64_uci_dos_target(0);
    c64_uci_service_set_home("0:/media/SUB");
    finish(&dos, start(&dos, 0x17, NULL, 0));
    status("00,OK");
    assert(reply.data_length == 8 && !memcmp(reply.data, "/SD/SUB/", 8));
    finish(&dos, start(&dos, 0x13, NULL, 0));
    status("01,DIRECTORY EMPTY");
    finish(&dos, start(&dos, 0x14, NULL, 0));
    status("01,DIRECTORY EMPTY");
    finish(&dos, start(&dos, 0x11, (const uint8_t *)"/SD", 3));
    status("00,OK");
    for (unsigned i = 0; i < 5000; i++) {
        char path[96];
        snprintf(path, sizeof path, "0:/media/entry-%05u-long-filename.prg", i);
        fixture_add(path, false, (const uint8_t *)"ok", 2);
    }
    finish(&dos, start(&dos, 0x13, NULL, 0));
    status("00,OK");
    finish(&dos, start(&dos, 0x14, NULL, 0));
    size_t count = 0, files = 0;
    while (1) {
        assert(reply.data_length > 1);
        if (reply.data[0] & 16) assert(reply.data[0] == 16);
        else { assert(reply.data[0] == 39); files++; }
        count++;
        if (!reply.more) break;
        finish(&dos, dos.next(dos.context, reply.data_length, &reply));
    }
    assert(count == 5001 && files == 5000);
    status("00,OK");
    /* READ_DIR replays the captured snapshot, matching Ultimate, without a
     * hard 4,096-entry or 64 KiB-name truncation. */
    finish(&dos, start(&dos, 0x14, NULL, 0));
    assert(reply.more && reply.data[0] == 16);
    assert(dos.abort(dos.context, 0) == MT_UCI_REPLY);
    uint8_t device = 8;
    assert(start(&dos, 0x25, &device, 1) == MT_UCI_WAIT);
    c64_uci_action_t action; char path[512]; unsigned drive;
    assert(c64_uci_service_action(&action, path, sizeof path, &drive));
    assert(action == C64_UCI_ACTION_SWAP && drive == 8);
    c64_uci_service_action_done(0);
    finish(&dos, dos.poll(dos.context, &reply));
    status("00,OK");
    regs[MT_REG_ABI_VERSION / 4] = 27;
    regs[MT_REG_C64_EXPANSION_CAPS / 4] |= MT_C64_EXPANSION_CAPS_DRIVE_POWER;
    regs[MT_REG_C64_DRIVE_STATUS / 4] = 0x8000000a;
    device = 8;
    finish(&dos, start(&dos, 0x25, &device, 1));
    status("90,DRIVE NOT PRESENT");
    device = 10;
    assert(start(&dos, 0x25, &device, 1) == MT_UCI_WAIT);
    assert(c64_uci_service_action(&action, path, sizeof path, &drive));
    assert(action == C64_UCI_ACTION_SWAP && drive == 8);
    c64_uci_service_action_done(0);
    finish(&dos, dos.poll(dos.context, &reply));
    status("00,OK");
    device = 1;
    finish(&dos, start(&dos, 0x25, &device, 1));
    status("90,DRIVE NOT PRESENT");
    dos.reset(dos.context);
    puts("C64 UCI DOS directory and swap tests passed");
    return 0;
}
