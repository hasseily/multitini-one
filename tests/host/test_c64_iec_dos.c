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
static unsigned sector_op, sector_track, sector_number;
static char sector_path[512];
static uint8_t sector_bytes[256];
static int fake_sector(void *context, const char *path, unsigned op, unsigned *track,
                       unsigned *sector, uint8_t bytes[256])
{
    (void)context;
    strcpy(sector_path, path); sector_op = op; sector_track = *track; sector_number = *sector;
    if (*track == 99) return 66;
    if (op == 0) memcpy(bytes, sector_bytes, 256);
    if (op == 1) memcpy(sector_bytes, bytes, 256);
    if (op == 2) { *track = 18; *sector = 3; return 65; }
    return 0;
}
static bool directory_contains(const char *text)
{
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= dos.directory_size; ++i)
        if (!memcmp(dos.directory + i, text, n)) return true;
    return false;
}
static mt_clock_time_t rtc_time = {2026,10,25,14,5,6};
static bool fake_clock(void *context,mt_clock_time_t *time)
{
    (void)context;*time=rtc_time;return true;
}
static int command_extensions(void)
{
    initialize(); dos.fs.clock=fake_clock;
    CHECK(command("T-RA")==0 && !strcmp(dos.status,"SUN. 10/25/26 02:05:06 PM\r"));
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2026-10-25T14:05:06 SUN\r"));
    CHECK(command("T-RD")==0 && dos.status_size==9 && (uint8_t)dos.status[1]==126 && dos.status[4]==2 && dos.status[7]==1);
    CHECK(command("T-RB")==0 && (uint8_t)dos.status[1]==0x26 && (uint8_t)dos.status[2]==0x10);
    CHECK(command("T-WI2024-02-29T23:59:59")==0);
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2024-02-29T23:59:59 THU\r"));
    rtc_time.second=7;
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2024-03-01T00:00:00 FRI\r"));
    CHECK(command("T-WI2023-02-29T00:00:00")==30);
    const uint8_t twd[]={'T','-','W','D',0,126,10,25,12,34,56,0};
    CHECK(mt_iec_dos_command(&dos,twd,sizeof twd)==0);
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2026-10-25T00:34:56 SUN\r"));
    const uint8_t twb[]={'T','-','W','B',0,0x26,0x10,0x25,0x12,0x34,0x56,1};
    CHECK(mt_iec_dos_command(&dos,twb,sizeof twb)==0);
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2026-10-25T12:34:56 SUN\r"));
    CHECK(command("T-WASUN. 10/25/26 11:22:33 PM")==0);
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2026-10-25T23:22:33 SUN\r"));
    CHECK(command("CD:SUB")==0 && command("UJ")==0 && !strcmp(dos.cwd,"0:/media/SUB"));
    CHECK(command("T-RI")==0 && !strcmp(dos.status,"2026-10-25T14:05:07 SUN\r"));
    CHECK(command("XPWD")==0 && !strcmp(dos.status,"1:/SUB"));
    CHECK(command("CD:/")==0);
    const uint8_t memory_read[]={'M','-','R',254,0,0};
    CHECK(mt_iec_dos_command(&dos,memory_read,sizeof memory_read)==0 && dos.status_size==2 && !dos.status[0] && !dos.status[1]);
    CHECK(command("M-E")==98 && !strcmp(dos.status,"98,UNKNOWN DRIVECODE,00,00\r"));
    const uint8_t address_write[]={'M','-','W',0x77,0,1,0x2a};
    CHECK(mt_iec_dos_command(&dos,address_write,sizeof address_write)==0 && dos.current_device==10 && fixture.device==10);
    CHECK(command("S-8")==0 && dos.current_device==8 && fixture.device==8);
    CHECK(command("S-9")==0 && dos.current_device==9);
    const uint8_t address13[]={'U','0','>',13},address30[]={'U','0',0x9e,30,13,10};
    CHECK(mt_iec_dos_command(&dos,address13,sizeof address13)==0 && dos.current_device==13);
    CHECK(mt_iec_dos_command(&dos,address30,sizeof address30)==0 && dos.current_device==30);
    CHECK(command("S-D")==0 && dos.current_device==9 && fixture.device==9);
    uint8_t invalid_address[]={'U','0','>',31};
    CHECK(mt_iec_dos_command(&dos,invalid_address,sizeof invalid_address)==30 && dos.current_device==9);
    invalid_address[3]=7;
    CHECK(mt_iec_dos_command(&dos,invalid_address,sizeof invalid_address)==30);
    fixture.fail_device=74;
    CHECK(mt_iec_dos_command(&dos,address13,sizeof address13)==74 && dos.current_device==9 && fixture.device==9);
    fixture.fail_device=0;
    const uint8_t write_bad[]={'M','-','W',0x78,0,1,10},write13[]={'M','-','W',0x77,0,1,13,13};
    CHECK(mt_iec_dos_command(&dos,write_bad,sizeof write_bad)==30);
    CHECK(mt_iec_dos_command(&dos,write13,sizeof write13-1)==30);
    CHECK(mt_iec_dos_command(&dos,write13,sizeof write13)==0 && dos.current_device==13);
    CHECK(command("S-D")==0 && dos.current_device==9);
    const uint8_t a[]={1,2,3},b[]={4,5};
    fixture_file *alpha=fixture_add("0:/media/ALPHA.prg",false,a,sizeof a);
    fixture_add("0:/media/BETA.prg",false,b,sizeof b);
    CHECK(open_name(2,"RAW.DNP,W")==0 && mt_iec_dos_write(&dos,2,a,sizeof a,true)==0);
    CHECK(mt_iec_dos_close(&dos,2)==0 && fixture_find("0:/media/RAW.DNP") && !fixture_find("0:/media/RAW.DNP.seq"));
    alpha->metadata.date=(uint16_t)((26+20)<<9 | 10<<5 | 25);
    alpha->metadata.time=(uint16_t)(14<<11 | 5<<5 | 3);
    CHECK(command("C:JOINED=ALPHA,BETA")==0);
    fixture_file *joined=fixture_find("0:/media/JOINED.prg");
    const uint8_t combined[]={1,2,3,4,5};
    CHECK(joined && joined->size==5 && !memcmp(joined->bytes,combined,5));
    CHECK(command("C:JOINED=ALPHA")==63);
    CHECK(command("C:ABSENT=DOESNOTEXIST")==62 && !fixture_find("0:/media/ABSENT.prg"));
    CHECK(command("EL:ALPHA")==0 && alpha->metadata.attributes==1);
    CHECK(open_name(1,"@ALPHA")==26);
    CHECK(command("EU:ALPHA")==0 && !alpha->metadata.attributes);
    CHECK(command("L:ALPHA")==0 && alpha->metadata.attributes==1);
    CHECK(command("L:ALPHA")==0 && !alpha->metadata.attributes);
    CHECK(command("A:RHA=ALPHA")==0 && alpha->metadata.attributes==35);
    CHECK(open_name(0,"$*")==0 && !directory_contains("ALPHA"));
    CHECK(mt_iec_dos_close(&dos,0)==0);
    CHECK(open_name(0,"$*=H,L")==0 && directory_contains("ALPHA") && directory_contains("10/25/26   02.05 PM"));
    CHECK(mt_iec_dos_close(&dos,0)==0);
    CHECK(open_name(0,"$=T:ALPHA=H")==0 && directory_contains("10/25 02.05 P"));
    CHECK(mt_iec_dos_close(&dos,0)==0);
    CHECK(command("EHALPHA")==0 && alpha->metadata.attributes==33);
    CHECK(command("A:=ALPHA")==0 && !alpha->metadata.attributes);
    CHECK(open_name(0,"$:*=L,>10/25/26 02:04 PM,<10/25/26 02:06 PM")==0 && directory_contains("ALPHA") && !directory_contains("BETA"));
    CHECK(mt_iec_dos_close(&dos,0)==0);
    CHECK(open_name(0,"$:*=L,>02/29/23 01:00 AM")==30);
    CHECK(command("W-1")==0 && command("C:LOCKED=ALPHA")==26);
    CHECK(command("W-0")==0);
    CHECK(open_name(2,"SEEK,S,W")==0);
    CHECK(mt_iec_dos_write(&dos,2,(const uint8_t *)"ABCD",4,false)==0);
    const uint8_t seek_back[]={'P',2,1},seek_past[]={'P',2,6},seek_invalid[]={'P',18,0};
    CHECK(mt_iec_dos_command(&dos,seek_invalid,sizeof seek_invalid)==70);
    CHECK(mt_iec_dos_command(&dos,seek_back,sizeof seek_back)==0);
    CHECK(mt_iec_dos_write(&dos,2,(const uint8_t *)"Z",1,false)==0);
    CHECK(mt_iec_dos_command(&dos,seek_past,sizeof seek_past)==0);
    CHECK(command("W-1")==0 && mt_iec_dos_write(&dos,2,a,1,false)==26);
    CHECK(mt_iec_dos_command(&dos,seek_back,sizeof seek_back)==26);
    CHECK(command("W-0")==0 && mt_iec_dos_write(&dos,2,(const uint8_t *)"X",1,true)==0);
    CHECK(mt_iec_dos_close(&dos,2)==0);
    const uint8_t seek_result[]={'A','Z','C','D',0,0,'X'};
    fixture_file *seek_file=fixture_find("0:/media/SEEK.seq");
    CHECK(seek_file && seek_file->size==sizeof seek_result && !memcmp(seek_file->bytes,seek_result,sizeof seek_result));
    const uint8_t rel[]={4,0,'A',0,0,0};
    fixture_add("0:/media/R1.rel",false,rel,sizeof rel);
    fixture_add("0:/media/R2.rel",false,rel,sizeof rel);
    CHECK(command("C:RELJOIN=R1,R2")==0);
    joined=fixture_find("0:/media/RELJOIN.rel");
    CHECK(joined && joined->size==10 && joined->bytes[0]==4 && joined->bytes[2]=='A' && joined->bytes[6]=='A');
    CHECK(command("C:MIXED=R1,ALPHA")==64 && !fixture_find("0:/media/MIXED.rel"));
    CHECK(command("R-H:HOME")==0 && !strcmp(dos.partitions[1].name,"HOME"));
    fixture_add("0:/media/SUB/INNER",true,NULL,0);
    fixture_add("0:/media/SUB/INNER/OPEN.prg",false,a,sizeof a);
    CHECK(mt_iec_dos_partition_add(&dos,7,"0:/media/SUB/INNER","NESTED")==0);
    CHECK(open_name(0,"SUB/INNER/OPEN")==0);
    CHECK(command("CD:SUB")==0 && command("D:RENAMED")==70);
    CHECK(mt_iec_dos_close(&dos,0)==0);
    CHECK(command("D:RENAMED")==0 && !strcmp(dos.cwd,"0:/media/RENAMED"));
    CHECK(fixture_find("0:/media/RENAMED") && !fixture_find("0:/media/SUB"));
    CHECK(!strcmp(dos.partitions[7].root,"0:/media/RENAMED/INNER"));
    CHECK(command("CP:7")==0 && open_name(0,"OPEN")==0 && mt_iec_dos_close(&dos,0)==0);
    CHECK(command("CP:1")==0 && command("CD:/")==0);
    CHECK(command("R:RENAMED/INNER/NESTED=RENAMED")==26);
    CHECK(command("R:FINAL=RENAMED")==0 && !strcmp(dos.partitions[7].cwd,"0:/media/FINAL/INNER"));
    CHECK(command("CP:7")==0);
    const uint8_t cold_reset[]={'U',0xca};
    CHECK(mt_iec_dos_command(&dos,cold_reset,sizeof cold_reset)==0 && dos.current_partition==1 && !strcmp(dos.cwd,"0:/media"));
    CHECK(mt_iec_dos_quiesce(&dos)==0);
    for(unsigned i=0;i<16;++i) CHECK(!fixture.handles[i].used);
    return 0;
}
static int extensions(void)
{
    initialize();
    const uint8_t contents[] = {1, 8, 'A', 'B', 'C'};
    fixture_add("0:/media/SUB/INNER", true, NULL, 0);
    fixture_add("0:/media/SUB/USER.usr", false, contents, sizeof contents);
    CHECK(mt_iec_dos_partition_add(&dos, 7, "0:/media/SUB", "TOOLS") == 0);
    CHECK(mt_iec_dos_partition_add(&dos, 8, "0:/elsewhere", "BAD") == 77);
    CHECK(command("CP:7") == 0 && dos.current_partition == 7);
    CHECK(command("CD:INNER") == 0 && !strcmp(dos.cwd, "0:/media/SUB/INNER"));
    CHECK(command("CP:1") == 0 && !strcmp(dos.cwd, "0:/media"));
    CHECK(command("CD7:/") == 0 && !strcmp(dos.partitions[7].cwd, "0:/media/SUB"));
    CHECK(!strcmp(dos.cwd, "0:/media"));
    CHECK(command("R-P:RENAMED=TOOLS") == 0);
    CHECK(open_name(0, "$=P") == 0 && directory_contains("RENAMED") && directory_contains("PARTITIONS"));
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    char path[MT_IEC_PATH], alias[17]; uint8_t type;
    CHECK(mt_iec_dos_resolve_name(&dos, 2, (const uint8_t *)"$7/INNER/", 9, path) == 0);
    CHECK(!strcmp(path, "0:/media/SUB/INNER"));
    CHECK(open_name(2, "7:USER,U,R") == 0);
    CHECK(mt_iec_dos_close(&dos, 2) == 0);
    CHECK(mt_iec_dos_iec_name(&dos, "0:/media/SUB/USER.usr", alias, &type) == 0 && type == 3 && !strcmp(alias, "USER"));
    CHECK(open_name(2, "DEFAULT,W") == 0);
    CHECK(mt_iec_dos_write(&dos, 2, contents, sizeof contents, true) == 0);
    CHECK(mt_iec_dos_close(&dos, 2) == 0 && fixture_find("0:/media/DEFAULT.seq"));
    const uint8_t partition_info[] = {'G','-','P',7};
    CHECK(mt_iec_dos_command(&dos, partition_info, sizeof partition_info) == 0);
    uint8_t reply[64]; size_t got; bool eof;
    CHECK(mt_iec_dos_peek_many(&dos, 15, reply, sizeof reply, &got, &eof) == 0 && got == 31 && eof);
    CHECK(reply[0] == 1 && reply[2] == 7 && !memcmp(reply + 3, "RENAMED", 7) && reply[30] == 13);
    CHECK(mt_iec_dos_consume_many(&dos, 15, got) == 0 && !strncmp(dos.status, "00,", 3));
    const uint8_t current_info[] = {'G','-','P',255};
    CHECK(mt_iec_dos_command(&dos, current_info, sizeof current_info) == 0 && dos.status[2] == 1);
    const uint8_t digit_info[] = {'G','-','P','7'};
    CHECK(mt_iec_dos_command(&dos, digit_info, sizeof digit_info) == 0 && dos.status[2] == '7' && !dos.status[0]);
    CHECK(command("G-P:7") == 30);
    CHECK(command("I") == 0 && !strncmp(dos.status, "00,", 3));
    CHECK(command("UI") == 0 && !strncmp(dos.status, "73,", 3));
    /* X00 header names and payload offsets agree across listing, mapping,
     * LOAD and replacement; unrelated files keep their bytes untouched. */
    uint8_t wrapped[31] = {0};
    memcpy(wrapped, "C64File\0", 8); memcpy(wrapped + 8, "WRAPPED", 7);
    memcpy(wrapped + 26, contents, sizeof contents);
    fixture_add("0:/media/host.p00", false, wrapped, sizeof wrapped);
    CHECK(mt_iec_dos_iec_name(&dos, "0:/media/host.p00", alias, &type) == 0 && type == 1 && !strcmp(alias, "WRAPPED"));
    CHECK(open_name(0, "WRAPPED") == 0);
    CHECK(mt_iec_dos_peek_many(&dos, 0, reply, sizeof reply, &got, &eof) == 0 && got == sizeof contents && eof);
    CHECK(!memcmp(reply, contents, got));
    CHECK(mt_iec_dos_close(&dos, 0) == 0);
    CHECK(open_name(1, "@WRAPPED") == 0);
    CHECK(mt_iec_dos_write(&dos, 1, (const uint8_t *)"new", 3, true) == 0);
    fixture_file *file = fixture_find("0:/media/host.p00");
    CHECK(file && file->size == 29 && !memcmp(file->bytes, wrapped, 26) && !memcmp(file->bytes + 26, "new", 3));
    /* A REL transaction uses a temporary file. Positioning beyond EOF does
     * not extend it; writing fills missing records with FF followed by zeros. */
    const uint8_t relname[] = {'R','E','C',',','L',',',8};
    CHECK(mt_iec_dos_open(&dos, 6, relname, sizeof relname) == 0 && dos.channels[6].kind == 5);
    CHECK(!fixture_find("0:/media/REC.rel"));
    CHECK(mt_iec_dos_write(&dos, 6, (const uint8_t *)"AB", 2, true) == 0);
    uint8_t pos[] = {'P',6,3,0,1};
    CHECK(mt_iec_dos_command(&dos, pos, sizeof pos) == 50 && dos.channels[6].size == 10);
    CHECK(mt_iec_dos_write(&dos, 6, (const uint8_t *)"XYZ", 3, true) == 0);
    CHECK(mt_iec_dos_close(&dos, 6) == 0);
    file = fixture_find("0:/media/REC.rel");
    CHECK(file && file->size == 26 && file->bytes[0] == 8 && file->bytes[1] == 0);
    CHECK(!memcmp(file->bytes + 2, "AB\0\0\0\0\0\0", 8) && file->bytes[10] == 255 && !memcmp(file->bytes + 18, "XYZ", 3));
    CHECK(open_name(6, "REC") == 0 && dos.channels[6].record_size == 8);
    CHECK(mt_iec_dos_command(&dos, pos, sizeof pos) == 0);
    CHECK(mt_iec_dos_peek_many(&dos, 6, reply, sizeof reply, &got, &eof) == 0 && got == 3 && eof && !memcmp(reply, "XYZ", 3));
    CHECK(mt_iec_dos_consume_many(&dos, 6, got) == 0);
    CHECK(mt_iec_dos_peek_many(&dos, 6, reply, sizeof reply, &got, &eof) == 0 && got == 1 && reply[0] == 255);
    CHECK(mt_iec_dos_consume_many(&dos, 6, 1) == 0 && !strncmp(dos.status, "50,", 3));
    pos[2] = 1; pos[4] = 9;
    CHECK(mt_iec_dos_command(&dos, pos, sizeof pos) == 51);
    CHECK(mt_iec_dos_write(&dos, 6, (const uint8_t *)"AB", 2, true) == 51);
    CHECK(mt_iec_dos_close(&dos, 6) == 0 && file->size == 26);
    uint8_t wrong[] = {'R','E','C',',','L',',',7};
    CHECK(mt_iec_dos_open(&dos, 6, wrong, sizeof wrong) == 50 && !dos.channels[6].kind);
    /* Failure leaves the previously committed REL intact and a retryable
     * owned handle when closing the temporary file itself fails. */
    CHECK(open_name(6, "REC") == 0);
    fixture.fail_write = 25;
    CHECK(mt_iec_dos_write(&dos, 6, (const uint8_t *)"FAIL", 4, true) == 25);
    fixture.fail_write = 0; fixture.fail_close = 25; fixture.keep_handle = true;
    CHECK(mt_iec_dos_close(&dos, 6) == 25 && dos.channels[6].handle);
    fixture.fail_close = 0;
    CHECK(mt_iec_dos_close(&dos, 6) == 25 && !dos.channels[6].kind && !memcmp(file->bytes + 2, "AB", 2));
    /* Buffer channels capture their partition. Block I/O must use that
     * medium even after CP changes, and report actual backend errors. */
    dos.fs.sector = fake_sector;
    CHECK(command("CP:7") == 0 && open_name(4, "#") == 0 && dos.channels[4].position == 1);
    CHECK(command("CP:1") == 0);
    for (unsigned i = 0; i < 256; ++i) sector_bytes[i] = (uint8_t)i;
    CHECK(command("U1: 4 0 18 0") == 0 && sector_op == 0 && sector_track == 18 && sector_number == 0 && !strcmp(sector_path, "0:/media/SUB"));
    CHECK(mt_iec_dos_peek_many(&dos, 4, reply, sizeof reply, &got, &eof) == 0 && got == sizeof reply && !eof && reply[1] == 1);
    CHECK(command("B-P:4,254") == 0);
    CHECK(mt_iec_dos_write(&dos, 4, (const uint8_t *)"XY", 2, true) == 0);
    CHECK(command("U2:4,0,18,1") == 0 && sector_bytes[254] == 'X' && sector_bytes[255] == 'Y');
    sector_bytes[0] = 3;
    CHECK(command("B-R:4,0,18,0") == 0 && dos.channels[4].position == 1);
    CHECK(command("BLOCK-READ:4,0,18,0") == 0 && dos.channels[4].position == 1);
    CHECK(mt_iec_dos_peek_many(&dos, 4, reply, sizeof reply, &got, &eof) == 0 && got == 3 && eof && reply[0] == 1);
    CHECK(command("B-A:0,18,0") == 65 && sector_op == 2 && strstr(dos.status, ",18,03\r"));
    CHECK(command("B-F:0,18,3") == 0 && sector_op == 3);
    CHECK(command("U1:4,0,99,0") == 66 && strstr(dos.status, ",99,00\r"));
    CHECK(command("B-P:4,0,1") == 30);
    CHECK(mt_iec_dos_close(&dos, 4) == 0);
    CHECK(open_name(4, "##2") == 70);
    CHECK(open_name(4, "##1") == 0 && dos.channels[4].position == 0);
    CHECK(mt_iec_dos_close(&dos, 4) == 0);
    CHECK(mt_iec_dos_partition_remove(&dos, 7) == 0 && open_name(4, "7:USER") == 77);
    CHECK(mt_iec_dos_quiesce(&dos) == 0);
    return 0;
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
    CHECK(open_name(1, "256:/ESCAPE") == 77);
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
        /* Ultimate stores exact-size metadata in ordinary entry link bytes;
         * BASIC relinks the loaded listing. Header/footer remain BASIC links. */
        CHECK(end < dos.directory_size && (next == 0x401 + end + 1 - 2 || (next >> 8) == 1));
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
    dos.channels[1].position = dos.channels[1].size;
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
    CHECK(extensions() == 0);
    CHECK(command_extensions() == 0);
    printf("PASS C64 IEC DOS: %u checks\n", checks);
    return 0;
}
