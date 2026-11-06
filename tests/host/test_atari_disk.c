/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_atari_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    unsigned reads;
    uint32_t last_offset;
    size_t last_length;
    bool fail;
} source_t;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static bool read_at(void *context, uint32_t offset, uint8_t *data, size_t length)
{
    source_t *source = context;
    ++source->reads;
    source->last_offset = offset;
    source->last_length = length;
    if (source->fail || offset > source->size || length > source->size - offset) return false;
    memcpy(data, source->data + offset, length);
    return true;
}

static void put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void atr_header(uint8_t header[16], uint32_t payload, uint16_t sector_size)
{
    const uint32_t paragraphs = payload / 16U;
    memset(header, 0, 16U);
    put_u16(header, 0x0296U);
    put_u16(header + 2U, (uint16_t)paragraphs);
    put_u16(header + 4U, sector_size);
    put_u16(header + 6U, (uint16_t)(paragraphs >> 16U));
}

static void check_sector(const mt_atari_atr_t *atr, uint16_t sector, uint32_t start, uint16_t size)
{
    uint32_t offset;
    uint16_t length;
    check(mt_atari_atr_sector(atr, sector, &offset, &length) && offset == start && length == size,
          "ATR sector offset and length");
}

static void test_atr(void)
{
    uint8_t data[16U + 4U * 256U] = {0};
    source_t source = {.data = data, .size = sizeof(data)};
    mt_atari_atr_t atr;
    atr_header(data, 720U * 128U, 128U);
    data[15] = 1U;
    check(mt_atari_atr_open(16U + 720U * 128U, read_at, &source, &atr) == MT_ATARI_DISK_OK &&
          atr.sector_count == 720U && atr.sector_size == 128U && atr.write_protected && source.reads == 1U,
          "a single-density image needs only the header read");
    check_sector(&atr, 1U, 16U, 128U);
    check_sector(&atr, 720U, 16U + 719U * 128U, 128U);
    uint32_t offset;
    uint16_t length;
    check(!mt_atari_atr_sector(&atr, 0U, &offset, &length) &&
          !mt_atari_atr_sector(&atr, 721U, &offset, &length), "sectors are one-based and bounded");

    const uint32_t large_payload = 384U + (65535U - 3U) * 256U;
    atr_header(data, large_payload, 256U);
    check(mt_atari_atr_open(16U + large_payload, read_at, &source, &atr) == MT_ATARI_DISK_OK &&
          atr.sector_count == 65535U && atr.sector_size == 256U && !atr.write_protected,
          "compact double-density image accepts the largest SIO sector number");
    check_sector(&atr, 3U, 16U + 256U, 128U);
    check_sector(&atr, 4U, 16U + 384U, 256U);
    check_sector(&atr, 65535U, 16U + large_payload - 256U, 256U);
    atr_header(data, large_payload + 256U, 256U);
    check(mt_atari_atr_open(16U + large_payload + 256U, read_at, &source, &atr) == MT_ATARI_DISK_UNSUPPORTED,
          "images larger than the SIO sector space are rejected");

    atr_header(data, sizeof(data) - 16U, 256U);
    data[16U + 256U] = 0x44U;
    check(mt_atari_atr_open(sizeof(data), read_at, &source, &atr) == MT_ATARI_DISK_OK && atr.sector_count == 4U,
          "legacy packed boot sectors with trailing zero padding are accepted");
    check_sector(&atr, 2U, 16U + 128U, 128U);
    check_sector(&atr, 3U, 16U + 256U, 128U);
    check_sector(&atr, 4U, 16U + 768U, 256U);
    data[16U + 512U] = 0x45U;
    check(mt_atari_atr_open(sizeof(data), read_at, &source, &atr) == MT_ATARI_DISK_OK,
          "legacy interleaved boot padding is accepted");
    check_sector(&atr, 2U, 16U + 256U, 128U);
    check_sector(&atr, 3U, 16U + 512U, 128U);
    check_sector(&atr, 4U, 16U + 768U, 256U);

    check(mt_atari_atr_open(sizeof(data) - 16U, read_at, &source, &atr) == MT_ATARI_DISK_INVALID,
          "ATR declared payload must match the file size");
    atr_header(data, 400U, 256U);
    check(mt_atari_atr_open(416U, read_at, &source, &atr) == MT_ATARI_DISK_INVALID,
          "incomplete physical sectors are invalid");
    atr_header(data, 128U, 512U);
    check(mt_atari_atr_open(144U, read_at, &source, &atr) == MT_ATARI_DISK_UNSUPPORTED,
          "unknown sector sizes are unsupported");
    data[0] = 0U;
    check(mt_atari_atr_open(144U, read_at, &source, &atr) == MT_ATARI_DISK_INVALID,
          "ATR magic is required");
    source.fail = true;
    check(mt_atari_atr_open(144U, read_at, &source, &atr) == MT_ATARI_DISK_IO,
          "ATR header I/O failures are distinguished from invalid images");
}

static void segment(uint8_t *file, size_t *size, uint16_t address, const uint8_t *data, uint16_t length)
{
    put_u16(file + *size, address);
    put_u16(file + *size + 2U, (uint16_t)(address + length - 1U));
    memcpy(file + *size + 4U, data, length);
    *size += 4U + length;
}

static size_t fixture(uint8_t data[512])
{
    const uint8_t first_init[] = {0xa9, 0x11, 0x8d, 0x00, 0x60, 0x60};
    const uint8_t second_init[] = {0xee, 0x00, 0x60, 0x60};
    const uint8_t run[] = {0xad, 0x00, 0x60, 0x8d, 0x01, 0x60, 0x4c, 0x16, 0xa0};
    const uint8_t first_address[] = {0x00, 0x07};
    const uint8_t second_address[] = {0x00, 0xa0};
    const uint8_t run_address[] = {0x10, 0xa0};
    uint8_t payload[257];
    for (size_t i = 0U; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i ^ 0x5aU);
    data[0] = data[1] = 0xffU;
    size_t size = 2U;
    segment(data, &size, 0x0700U, first_init, sizeof(first_init));
    segment(data, &size, 0x02e2U, first_address, sizeof(first_address));
    data[size++] = 0xffU;
    data[size++] = 0xffU;
    segment(data, &size, 0x1fffU, payload, sizeof(payload));
    segment(data, &size, 0xa000U, second_init, sizeof(second_init));
    segment(data, &size, 0x02e2U, second_address, sizeof(second_address));
    segment(data, &size, 0x02e0U, first_address, sizeof(first_address));
    segment(data, &size, 0xa010U, run, sizeof(run));
    segment(data, &size, 0x02e0U, run_address, sizeof(run_address));
    return size;
}

static void test_xex(const char *output)
{
    uint8_t data[512];
    const size_t size = fixture(data);
    source_t source = {.data = data, .size = size};
    mt_atari_xex_t xex;
    check(mt_atari_xex_open((uint32_t)size, read_at, &source, &xex) == MT_ATARI_DISK_OK &&
          xex.load_address == 0x0800U && xex.run_address == 0x0700U,
          "segmented XEX relocates its bootstrap around a low-memory segment");
    uint8_t sector[128];
    const unsigned previous_reads = source.reads;
    check(mt_atari_xex_read_sector(&xex, read_at, &source, 1U, sector) == MT_ATARI_DISK_OK &&
          sector[0] == 0U && sector[2] == 0U && sector[3] == 0x08U && source.reads == previous_reads,
          "the boot header contains the relocated address and needs no file I/O");
    const uint16_t boot_sectors = sector[1];
    check(boot_sectors != 0U && xex.sector_count == boot_sectors + 3U, "virtual disk sector count");
    check(mt_atari_xex_read_sector(&xex, read_at, &source, (uint16_t)(boot_sectors + 2U), sector) == MT_ATARI_DISK_OK &&
          source.last_offset == 128U && source.last_length == 128U && memcmp(sector, data + 128U, 128U) == 0,
          "virtual data sectors support direct random reads");
    check(mt_atari_xex_read_sector(&xex, read_at, &source, xex.sector_count, sector) == MT_ATARI_DISK_OK &&
          source.last_offset == 256U && source.last_length == size - 256U &&
          memcmp(sector, data + 256U, size - 256U) == 0 && sector[127] == 0U,
          "final sector reads only remaining data and zero pads");
    check(mt_atari_xex_read_sector(&xex, read_at, &source, 0U, sector) == MT_ATARI_DISK_INVALID &&
          mt_atari_xex_read_sector(&xex, read_at, &source, (uint16_t)(xex.sector_count + 1U), sector) == MT_ATARI_DISK_INVALID,
          "virtual sector bounds are enforced");

    if (output != NULL) {
        FILE *file = fopen(output, "wb");
        check(file != NULL, "open generated ATR test fixture");
        uint8_t header[16];
        atr_header(header, (uint32_t)xex.sector_count * sizeof(sector), sizeof(sector));
        header[15] = 1U;
        check(fwrite(header, 1U, sizeof(header), file) == sizeof(header), "write test fixture header");
        for (uint16_t i = 1U; i <= xex.sector_count; ++i)
            check(mt_atari_xex_read_sector(&xex, read_at, &source, i, sector) == MT_ATARI_DISK_OK &&
                  fwrite(sector, 1U, sizeof(sector), file) == sizeof(sector), "write test fixture sector");
        check(fclose(file) == 0, "close test fixture");
    }

    source.fail = true;
    check(mt_atari_xex_read_sector(&xex, read_at, &source, xex.sector_count, sector) == MT_ATARI_DISK_IO &&
          mt_atari_xex_open((uint32_t)size, read_at, &source, &xex) == MT_ATARI_DISK_IO,
          "virtual data and validation propagate I/O failures");
    source.fail = false;
    check(mt_atari_xex_open((uint32_t)size - 1U, read_at, &source, &xex) == MT_ATARI_DISK_INVALID,
          "truncated segment payload is rejected");
    data[size] = data[size + 1U] = 0xffU;
    source.size += 2U;
    check(mt_atari_xex_open((uint32_t)size + 2U, read_at, &source, &xex) == MT_ATARI_DISK_INVALID,
          "a trailing marker without a segment is rejected");
    data[0] = 0U;
    check(mt_atari_xex_open((uint32_t)size, read_at, &source, &xex) == MT_ATARI_DISK_INVALID,
          "the leading XEX marker is required");
    data[0] = 0xffU;
    put_u16(data + 4U, 0x06ffU);
    check(mt_atari_xex_open((uint32_t)size, read_at, &source, &xex) == MT_ATARI_DISK_INVALID,
          "a backwards segment is rejected");
    put_u16(data + 2U, 0xc000U);
    put_u16(data + 4U, 0xc005U);
    check(mt_atari_xex_open((uint32_t)size, read_at, &source, &xex) == MT_ATARI_DISK_UNSUPPORTED,
          "segments under the OS ROM are unsupported");
    check(mt_atari_xex_open(UINT32_MAX, read_at, &source, &xex) == MT_ATARI_DISK_UNSUPPORTED,
          "the virtual disk cannot exceed the SIO address space");
}

int main(int argc, char *argv[])
{
    check(argc <= 2, "optional argument is an output ATR fixture path");
    test_atr();
    test_xex(argc == 2 ? argv[1] : NULL);
    puts("PASS: ATR geometry and bounded streaming XEX boot sectors");
    return EXIT_SUCCESS;
}
