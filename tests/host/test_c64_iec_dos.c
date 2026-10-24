/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_iec_fixture.h"
static mt_iec_dos_t dos;
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
static int open_name(unsigned ch, const char *name)
{
    return mt_iec_dos_open(&dos, ch, (const uint8_t *)name, strlen(name));
}
static int command(const char *s)
{
    return mt_iec_dos_command(&dos, (const uint8_t *)s, strlen(s));
}
static void initialize(void)
{
    fixture_reset();
    mt_iec_fs_t fs = fixture_api();
    assert(mt_iec_dos_init(&dos, &fs, "0:/media") == 0);
}
int main(void)
{
    initialize();
    CHECK(!strncmp(dos.status, "73,", 3));
    const uint8_t prg[] = {1, 8, 0, 255, 128, 13, 0};
    fixture_add("0:/media/hello.prg", false, prg, sizeof prg);
    CHECK(open_name(0, "HELLO") == 0);
    for (size_t i = 0; i < sizeof prg; ++i)
    {
        uint8_t b = 0, b2 = 0;
        bool eoi = false, have = false, e2 = false, h2 = false;
        CHECK(mt_iec_dos_peek(&dos, 0, &b, &eoi, &have) == 0);
        CHECK(have && b == prg[i] && eoi == (i + 1 == sizeof prg));
        CHECK(mt_iec_dos_peek(&dos, 0, &b2, &e2, &h2) == 0 && b2 == b && h2 && e2 == eoi);
        mt_iec_dos_consume(&dos, 0);
    }
    {
        uint8_t b;
        bool e, h;
        CHECK(mt_iec_dos_peek(&dos, 0, &b, &e, &h) == 0 && !h);
    }
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(0, "HE?L*") == 0);
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(0, "MISSING") == 62);
    {
        uint8_t b;
        bool e, h;
        CHECK(mt_iec_dos_peek(&dos, 0, &b, &e, &h) == 62 && !h && !strncmp(dos.status, "62,", 3));
    }
    CHECK(mt_iec_dos_close(&dos, 0) == 0 && !strncmp(dos.status, "62,", 3));
    /* GET# status retalk keeps its cursor; final acknowledged CR clears it. */
    char status[80];
    strcpy(status, dos.status);
    for (size_t i = 0; i < strlen(status); ++i)
    {
        uint8_t b;
        bool e, h;
        mt_iec_dos_begin_talk(&dos, 15);
        CHECK(mt_iec_dos_peek(&dos, 15, &b, &e, &h) == 0 && h && b == (uint8_t)status[i]);
        mt_iec_dos_consume(&dos, 15);
    }
    CHECK(!strncmp(dos.status, "00,", 3));
    CHECK(open_name(1, "SAVED") == 0);
    CHECK(mt_iec_dos_write(&dos, 1, prg, sizeof prg, true) == 0);
    fixture_file *saved = fixture_find("0:/media/SAVED.prg");
    CHECK(saved && saved->size == sizeof prg && !memcmp(saved->bytes, prg, sizeof prg));
    CHECK(fixture.commits == 1 && !dos.channels[1].kind);
    CHECK(open_name(1, "SAVED") == 63);
    CHECK(mt_iec_dos_write(&dos, 1, prg, sizeof prg, true) == 63 && !strncmp(dos.status, "63,", 3));
    CHECK(open_name(1, "@0:SAVED") == 0);
    CHECK(mt_iec_dos_write(&dos, 1, (const uint8_t *)"partial", 7, false) == 0);
    CHECK(mt_iec_dos_quiesce(&dos) == 0);
    CHECK(saved->size == sizeof prg && !memcmp(saved->bytes, prg, sizeof prg));
    CHECK(open_name(2, "LOG,S,W") == 0);
    CHECK(mt_iec_dos_write(&dos, 2, (const uint8_t *)"A", 1, true) == 0);
    CHECK(mt_iec_dos_write(&dos, 2, (const uint8_t *)"BC", 2, true) == 0);
    CHECK(mt_iec_dos_close(&dos, 2) == 0);
    CHECK(fixture_find("0:/media/LOG.seq")->size == 3);
    CHECK(open_name(2, "LOG,S,A") == 0);
    CHECK(mt_iec_dos_write(&dos, 2, (const uint8_t *)"D", 1, true) == 0);
    CHECK(mt_iec_dos_close(&dos, 2) == 0);
    CHECK(!memcmp(fixture_find("0:/media/LOG.seq")->bytes, "ABCD", 4));
    CHECK(open_name(0, "LOG,P,R") == 64);
    CHECK(command("CD:SUB") == 0 && !strcmp(dos.cwd, "0:/media/SUB"));
    CHECK(open_name(1, "INNER") == 0);
    CHECK(mt_iec_dos_write(&dos, 1, prg, sizeof prg, true) == 0);
    CHECK(command("CD:..") == 0);
    CHECK(command("CD:..") == 26);
    CHECK(command("CD:/") == 0);
    CHECK(open_name(0, "SUB/INNER") == 0);
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(0, "../HELLO") == 26);
    CHECK(open_name(1, "0:/../ESCAPE") == 26);
    CHECK(open_name(1, "1:/ESCAPE") == 33);
    CHECK(open_name(1, "A\\B") == 33);
    const char *bad[] = {"NAME.", "NAME ", "NAME<", "NAME>", "NAME|", "NAME?", "NAME*", ".mtiec-hidden", "X.MTIEC"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
    {
        CHECK(open_name(1, bad[i]) != 0);
        char s[300];
        snprintf(s, sizeof s, "MD:%s", bad[i]);
        CHECK(command(s) != 0);
        snprintf(s, sizeof s, "R:%s=HELLO", bad[i]);
        CHECK(command(s) != 0);
    }
    strcpy(fixture.protected_name, "0:/media/ACTIVE.D64");
    fixture_add(fixture.protected_name, false, prg, sizeof prg);
    CHECK(open_name(1, "@ACTIVE.D64") == 26);
    CHECK(open_name(1, "@ACTIVE.D64.") == 33);
    CHECK(open_name(1, "@ACTIVE.D64 ") == 33);
    CHECK(command("S:ACTIVE.D64") == 26);
    CHECK(command("R:RENAMED=ACTIVE.D64") == 26);
    CHECK(command("S:*") == 26);
    CHECK(fixture_find("0:/media/hello.prg"));
    fixture.protected_name[0] = 0;
    CHECK(command("MD:NEW") == 0);
    CHECK(command("RD:NEW") == 0);
    CHECK(command("R:RENAMED=HELLO") == 0);
    CHECK(fixture_find("0:/media/RENAMED.prg"));
    CHECK(command("S:REN*") == 0);
    CHECK(!fixture_find("0:/media/RENAMED.prg"));
    fixture_add("0:/media/a very long Unicode \xc3\xa9 filename.prg", false, prg, sizeof prg);
    fixture_add("0:/media/a very long Unicode \xc3\xa9 filename.seq", false, prg, sizeof prg);
    CHECK(open_name(0, "$") == 0);
    char aliases[2][17];
    unsigned aliasn = 0;
    for (size_t i = 0; i < dos.entry_count; ++i)
        if (strchr(dos.entries[i].alias, '~'))
        {
            CHECK(aliasn < 2);
            strcpy(aliases[aliasn++], dos.entries[i].alias);
        }
    CHECK(aliasn == 2 && strcmp(aliases[0], aliases[1]));
    /* BASIC directory links target exactly the following record. */
    size_t at = 2;
    unsigned lines = 0;
    while (at + 2 < dos.directory_size)
    {
        unsigned next = dos.directory[at] | dos.directory[at + 1] << 8;
        if (!next)
            break;
        size_t end = at + 4;
        while (end < dos.directory_size && dos.directory[end])
            ++end;
        CHECK(end < dos.directory_size && next == 0x401 + end + 1 - 2);
        at = end + 1;
        ++lines;
    }
    CHECK(lines == dos.entry_count + 2);
    CHECK(open_name(2, "$") == 70);
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(0, aliases[0]) == 0);
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(0, "$") == 0);
    for (unsigned k = 0; k < 2; ++k)
    {
        bool found = false;
        for (size_t i = 0; i < dos.entry_count; ++i)
            if (!strcmp(aliases[k], dos.entries[i].alias))
                found = true;
        CHECK(found);
    }
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    const uint8_t petscii[] = {0xd3, 0xc1, 0xd6, 0xc5, 0xc4};
    const char *oddnames[] = {" leading.prg", "FOO..prg", "VISIBLE.MTIEC.prg", " a deliberately long filename.prg"};
    char oddaliases[4][17];
    for (unsigned i = 0; i < 4; ++i)
    {
        char path[512];
        snprintf(path, sizeof path, "0:/media/%s", oddnames[i]);
        fixture_add(path, false, prg, sizeof prg);
    }
    CHECK(open_name(0, "$") == 0);
    for (unsigned k = 0; k < 4; ++k)
    {
        bool found = false;
        for (size_t i = 0; i < dos.entry_count; ++i)
            if (!strcmp(oddnames[k], dos.entries[i].name))
            {
                strcpy(oddaliases[k], dos.entries[i].alias);
                found = true;
            }
        CHECK(found && strchr(oddaliases[k], '~') && oddaliases[k][0] != ' ');
    }
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    for (unsigned k = 0; k < 4; ++k)
    {
        CHECK(open_name(0, oddaliases[k]) == 0);
        CHECK(mt_iec_dos_close(&dos, 0) == 0);
    }
    CHECK(mt_iec_dos_open(&dos, 0, petscii, sizeof petscii) == 0);
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    for (unsigned ch = 0; ch < 15; ++ch)
        CHECK(open_name(ch, "SAVED,R") == 0);
    CHECK(open_name(0, "SAVED,R") == 70);
    CHECK(mt_iec_dos_quiesce(&dos) == 0);
    /* Buffered writes, fault retention, no replacement on partial failure. */
    uint8_t large[1300];
    for (size_t i = 0; i < sizeof large; ++i)
        large[i] = (uint8_t)i;
    unsigned writes = fixture.writes;
    CHECK(open_name(1, "LARGE") == 0);
    CHECK(mt_iec_dos_write(&dos, 1, large, sizeof large, true) == 0);
    CHECK(fixture.writes - writes == 3);
    CHECK(fixture_find("0:/media/LARGE.prg")->size == sizeof large);
    CHECK(open_name(1, "@SAVED") == 0);
    fixture.short_write = true;
    CHECK(mt_iec_dos_write(&dos, 1, large, sizeof large, true) == 25);
    CHECK(mt_iec_dos_close(&dos, 1) == 25);
    fixture.short_write = false;
    CHECK(saved->size == sizeof prg);
    CHECK(open_name(1, "@SAVED") == 0);
    fixture.fail_commit = 25;
    CHECK(mt_iec_dos_write(&dos, 1, prg, sizeof prg, true) == 25);
    fixture.fail_commit = 0;
    CHECK(saved->size == sizeof prg);
    CHECK(open_name(0, "SAVED") == 0);
    fixture.short_read = true;
    {
        uint8_t b;
        bool e, h;
        CHECK(mt_iec_dos_peek(&dos, 0, &b, &e, &h) == 23);
    }
    fixture.short_read = false;
    CHECK(mt_iec_dos_close(&dos, 0) == 23);
    CHECK(open_name(1, "LIMIT") == 0);
    dos.channels[1].size = MT_IEC_MAX_FILE - 1;
    CHECK(mt_iec_dos_write(&dos, 1, prg, 2, true) == 72);
    CHECK(mt_iec_dos_quiesce(&dos) == 72);
    CHECK(!fixture_find("0:/media/LIMIT.prg"));
    CHECK(open_name(0, "SAVED") == 0);
    fixture.fail_close = 25;
    fixture.keep_handle = true;
    CHECK(mt_iec_dos_close(&dos, 0) == 25 && dos.channels[0].handle);
    fixture.fail_close = 0;
    CHECK(mt_iec_dos_close(&dos, 0) == 25 && !dos.channels[0].handle);
    fixture_file *huge = fixture_add("0:/media/HUGE.prg", false, NULL, 0);
    huge->size = MT_IEC_MAX_FILE + 1U;
    fixture.fail_close = 25;
    fixture.keep_handle = true;
    CHECK(open_name(0, "HUGE") == 72 && dos.channels[0].kind == 1 && dos.channels[0].handle);
    fixture.fail_close = 0;
    CHECK(mt_iec_dos_close(&dos, 0) == 72 && !dos.channels[0].handle);
    huge->size = 0;
    fixture.fail_close = 25;
    fixture.keep_handle = true;
    CHECK(open_name(2, "LOG,S,A") == 25 && dos.channels[2].auxiliary && dos.channels[2].handle);
    fixture.fail_close = 0;
    CHECK(mt_iec_dos_close(&dos, 2) == 25 && !dos.channels[2].auxiliary && !dos.channels[2].handle);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(!fixture.handles[i].used);
    initialize();
    for (unsigned i = 0; i < 256; ++i)
    {
        char path[64];
        snprintf(path, sizeof path, "0:/media/F%03u.prg", i);
        fixture_add(path, false, prg, sizeof prg);
    }
    CHECK(open_name(0, "$") == 71); /* SUB +256 exceeds bound, no silent truncation. */
    initialize();
    fixture_add("0:/", true, NULL, 0);
    mt_iec_fs_t fs = fixture_api();
    CHECK(mt_iec_dos_init(&dos, &fs, "0:/") == 0);
    CHECK(command("CD:MEDIA") == 0);
    CHECK(command("CD:..") == 0 && !strcmp(dos.cwd, "0:/"));
    CHECK(command("CD:..") == 26);
    CHECK(command("CD:MEDIA/SUB/../../") == 0 && !strcmp(dos.cwd, "0:/"));
    printf("PASS C64 IEC DOS: %u checks\n", checks);
    return 0;
}
