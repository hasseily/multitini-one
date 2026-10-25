/* SPDX-License-Identifier: GPL-3.0-only */
#define F_CAP 8
#define F_BYTES 822400
#define main service_regression_main
#include "test_c64_uci_service.c"
#undef main
#include <stdlib.h>

int main(void)
{
    setup();
    uint8_t *image = calloc(174848, 1);
    assert(image);
    uint8_t *bam = image + 357 * 256, *dir = image + 358 * 256;
    bam[0] = 18; bam[1] = 1; bam[2] = 0x41;
    memset(bam + 144, 0xa0, 16); memcpy(bam + 144, "WIRE TEST", 9);
    for (unsigned t = 1; t <= 35; t++) {
        unsigned n = t <= 17 ? 21 : t <= 24 ? 19 : t <= 30 ? 18 : 17;
        bam[t * 4] = (uint8_t)n;
        for (unsigned s = 0; s < n; s++) bam[t * 4 + 1 + s / 8] |= (uint8_t)(1U << (s % 8));
    }
    bam[4]--; bam[5] &= ~1U;
    bam[72] -= 2; bam[73] &= ~3U;
    dir[1] = 255; dir[2] = 0x82; dir[3] = 1;
    memset(dir + 5, 0xa0, 16); memcpy(dir + 5, "HELLO", 5); dir[30] = 1;
    image[1] = 6; memcpy(image + 2, "hello", 5);
    fixture_add("0:/media/disk.d64", false, image, 174848); free(image);
    mt_uci_target_t dos = c64_uci_dos_target(0);
    finish(&dos, start(&dos, 0x11, (const uint8_t *)"disk.d64", 8)); status("00,OK");
    finish(&dos, start(&dos, 0x12, NULL, 0));
    assert(reply.data_length == 10 && !memcmp(reply.data, "/disk.d64/", 10));
    finish(&dos, start(&dos, 0x13, NULL, 0)); status("00,OK");
    finish(&dos, start(&dos, 0x14, NULL, 0));
    assert(reply.data[0] == 8 && reply.more);
    finish(&dos, dos.next(dos.context, reply.data_length, &reply));
    assert(reply.data[0] == 0 && !reply.more && !memcmp(reply.data + 1, "HELLO.PRG", 9));
    open_file(&dos, "HEL*.PRG", 1);
    uint8_t bytes[] = {10, 0};
    finish(&dos, start(&dos, 4, bytes, sizeof bytes));
    assert(reply.data_length == 5 && !memcmp(reply.data, "hello", 5));
    finish(&dos, start(&dos, 7, NULL, 0)); status("00,OK");
    assert(reply.data[8] == 'P' && reply.data[9] == 'R' && reply.data[10] == 'G');
    finish(&dos, start(&dos, 3, NULL, 0)); status("00,OK");
    rename_pair(&dos, "HELLO.PRG", "WORLD.PRG"); status("00,OK");
    uint8_t copy[] = "WORLD.PRG\0COPIED.PRG";
    finish(&dos, start(&dos, 11, copy, sizeof copy)); status("00,OK");
    finish(&dos, start(&dos, 0x16, (const uint8_t *)"NESTED", 6)); status("00,OK");
    finish(&dos, start(&dos, 0x11, (const uint8_t *)"NESTED", 6)); status("00,OK");
    open_file(&dos, "NEW.PRG", 6);
    finish(&dos, start(&dos, 7, NULL, 0)); status("00,OK");
    uint8_t payload[] = {0, 0, 'n', 'e', 'w'};
    finish(&dos, start(&dos, 5, payload, sizeof payload)); status("00,OK");
    finish(&dos, start(&dos, 3, NULL, 0)); status("00,OK");
    open_file(&dos, "NEW.PRG", 1);
    finish(&dos, start(&dos, 4, bytes, sizeof bytes));
    assert(reply.data_length == 3 && !memcmp(reply.data, "new", 3));
    finish(&dos, start(&dos, 3, NULL, 0)); status("00,OK");
    finish(&dos, start(&dos, 9, (const uint8_t *)"NEW.PRG", 7)); status("00,OK");
    finish(&dos, start(&dos, 0x11, (const uint8_t *)"..", 2)); status("00,OK");
    finish(&dos, start(&dos, 9, (const uint8_t *)"NESTED", 6)); status("00,OK");
    dos.reset(dos.context);
    puts("C64 UCI virtual image command tests passed");
    return 0;
}
