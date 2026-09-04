/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_d64.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    mt_c64_d64_prg_entry_t entries[8];
    size_t stored;
    size_t stop_after;
} entry_capture_t;

static uint8_t test_sectors_per_track(uint8_t track)
{
    if (track <= 17U) return 21U;
    if (track <= 24U) return 19U;
    if (track <= 30U) return 18U;
    return 17U;
}

static size_t test_lba(uint8_t track, uint8_t sector)
{
    size_t lba = 0U;
    uint8_t current;
    assert(track >= 1U && track <= 42U);
    assert(sector < test_sectors_per_track(track));
    for (current = 1U; current < track; ++current)
        lba += test_sectors_per_track(current);
    return lba + sector;
}

static void test_lba_to_track_sector(size_t lba, uint8_t *track,
                                     uint8_t *sector)
{
    uint8_t current;
    for (current = 1U; current <= 42U; ++current) {
        const uint8_t sectors = test_sectors_per_track(current);
        if (lba < sectors) {
            *track = current;
            *sector = (uint8_t)lba;
            return;
        }
        lba -= sectors;
    }
    assert(0);
}

static uint8_t *test_sector(uint8_t *image, uint8_t track, uint8_t sector)
{
    return image + test_lba(track, sector) * MT_C64_D64_SECTOR_SIZE;
}

static void set_directory_entry(uint8_t *image, uint8_t directory_sector,
                                uint8_t slot, uint8_t file_type,
                                uint8_t start_track, uint8_t start_sector,
                                const uint8_t *name, size_t name_size,
                                uint16_t blocks)
{
    uint8_t *const entry = test_sector(image, 18U, directory_sector) + 2U +
        (size_t)slot * 32U;
    assert(directory_sector >= 1U && directory_sector <= 18U);
    assert(slot < 8U && name_size <= MT_C64_D64_NAME_SIZE);
    memset(entry, 0U, 32U);
    entry[0] = file_type;
    entry[1] = start_track;
    entry[2] = start_sector;
    memset(entry + 3U, 0xa0U, MT_C64_D64_NAME_SIZE);
    memcpy(entry + 3U, name, name_size);
    entry[28U] = (uint8_t)blocks;
    entry[29U] = (uint8_t)(blocks >> 8U);
}

static bool capture_entry(void *context,
                          const mt_c64_d64_prg_entry_t *entry)
{
    entry_capture_t *const capture = context;
    if (capture->stored < sizeof(capture->entries) /
                              sizeof(capture->entries[0])) {
        capture->entries[capture->stored] = *entry;
    }
    ++capture->stored;
    return capture->stop_after == 0U ||
           capture->stored < capture->stop_after;
}

static void make_test_directory(uint8_t *image)
{
    static const uint8_t hello[] = "HELLO";
    static const uint8_t seq[] = "SEQUENCE";
    static const uint8_t duplicate[] = "HELLO";
    static const uint8_t shifted[] = {0xc1U, 0xc2U};
    uint8_t *const first = test_sector(image, 18U, 1U);
    uint8_t *const second = test_sector(image, 18U, 2U);
    uint8_t *block;
    size_t index;

    memset(image, 0U, 174848U);
    first[0] = 18U;
    first[1] = 2U;
    second[0] = 0U;
    second[1] = 0xffU;
    set_directory_entry(image, 1U, 0U, 0x82U, 1U, 0U,
                        hello, sizeof(hello) - 1U, 2U);
    set_directory_entry(image, 1U, 1U, 0x81U, 1U, 4U,
                        seq, sizeof(seq) - 1U, 1U);
    set_directory_entry(image, 1U, 2U, 0x02U, 1U, 3U,
                        hello, sizeof(hello) - 1U, 1U);
    set_directory_entry(image, 2U, 0U, 0xc2U, 1U, 2U,
                        duplicate, sizeof(duplicate) - 1U, 1U);
    set_directory_entry(image, 2U, 1U, 0x82U, 1U, 5U,
                        shifted, sizeof(shifted), 1U);

    block = test_sector(image, 1U, 0U);
    block[0] = 1U;
    block[1] = 1U;
    for (index = 0U; index < 254U; ++index)
        block[index + 2U] = (uint8_t)index;
    block = test_sector(image, 1U, 1U);
    block[0] = 0U;
    block[1] = 4U;
    block[2] = 0x11U;
    block[3] = 0x22U;
    block[4] = 0x33U;

    block = test_sector(image, 1U, 2U);
    block[0] = 0U;
    block[1] = 4U;
    block[2] = 0x01U;
    block[3] = 0x08U;
    block[4] = 0x60U;
    memcpy(test_sector(image, 1U, 3U), block,
           MT_C64_D64_SECTOR_SIZE);
    memcpy(test_sector(image, 1U, 5U), block,
           MT_C64_D64_SECTOR_SIZE);
}

static void assert_unchanged(const uint8_t *data, size_t size, uint8_t value)
{
    size_t index;
    for (index = 0U; index < size; ++index) assert(data[index] == value);
}

static void test_prg_listing_and_extraction(void)
{
    static uint8_t image[MT_C64_D64_MAX_SIZE];
    static uint8_t output[MT_C64_D64_MAX_PRG_SIZE];
    entry_capture_t capture = {0};
    size_t count = 99U;
    size_t output_size = 99U;
    size_t index;

    make_test_directory(image);
    assert(mt_c64_d64_list_prgs(image, 174848U, capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    assert(count == 4U && capture.stored == 4U);
    assert(capture.entries[0].directory_sector == 1U);
    assert(capture.entries[0].directory_slot == 0U);
    assert(capture.entries[0].file_type == 0x82U);
    assert(capture.entries[0].start_track == 1U);
    assert(capture.entries[0].start_sector == 0U);
    assert(capture.entries[0].blocks == 2U);
    assert(strcmp(capture.entries[0].display_name, "HELLO") == 0);
    assert(capture.entries[1].file_type == 0x02U);
    assert(capture.entries[2].directory_sector == 2U);
    assert(capture.entries[2].file_type == 0xc2U);
    assert(strcmp(capture.entries[2].display_name, "HELLO") == 0);
    assert(strcmp(capture.entries[3].display_name, "AB") == 0);

    assert(mt_c64_d64_extract_prg(image, 174848U, &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_OK);
    assert(output_size == 257U);
    for (index = 0U; index < 254U; ++index)
        assert(output[index] == (uint8_t)index);
    assert(output[254U] == 0x11U && output[255U] == 0x22U &&
           output[256U] == 0x33U);

    /* An error-info tail is never interpreted as file data. */
    assert(mt_c64_d64_extract_prg(image, 175531U, &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_OK);
    assert(output_size == 257U);

    capture.stored = 0U;
    capture.stop_after = 2U;
    assert(mt_c64_d64_list_prgs(image, 174848U, capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    assert(capture.stored == 2U && count == 4U);
    assert(mt_c64_d64_list_prgs(image, 174848U, NULL, NULL, &count) ==
           MT_C64_D64_OK);
    assert(count == 4U);
}

static void test_directory_errors_and_identity(void)
{
    static uint8_t image[174848U];
    uint8_t output[16U];
    entry_capture_t capture = {0};
    mt_c64_d64_prg_entry_t entry;
    size_t count = 0U;
    size_t output_size = 9U;
    uint8_t *directory;

    make_test_directory(image);
    assert(mt_c64_d64_list_prgs(image, sizeof(image), capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    entry = capture.entries[0];
    directory = test_sector(image, 18U, 1U);

    directory[5U] ^= 1U;
    memset(output, 0xa5U, sizeof(output));
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_ENTRY_CHANGED);
    assert(output_size == 0U);
    assert_unchanged(output, sizeof(output), 0xa5U);
    directory[5U] ^= 1U;

    directory[2U] = 0x81U;
    entry.file_type = 0x81U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_NOT_PRG);
    directory[2U] = 0x82U;

    entry = capture.entries[1];
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_BAD_STRUCTURE);

    entry = capture.entries[0];
    entry.directory_slot = 8U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_INVALID_ARGUMENT);

    make_test_directory(image);
    directory = test_sector(image, 18U, 2U);
    directory[0] = 18U;
    directory[1] = 1U;
    assert(mt_c64_d64_list_prgs(image, sizeof(image), NULL, NULL, &count) ==
           MT_C64_D64_CHAIN_CYCLE);
    capture.stored = 0U;
    capture.stop_after = 1U;
    assert(mt_c64_d64_list_prgs(image, sizeof(image), capture_entry, &capture,
                                &count) == MT_C64_D64_CHAIN_CYCLE);
    assert(capture.stored == 1U);

    make_test_directory(image);
    test_sector(image, 18U, 2U)[0] = 18U;
    test_sector(image, 18U, 2U)[1] = 19U;
    assert(mt_c64_d64_list_prgs(image, sizeof(image), NULL, NULL, &count) ==
           MT_C64_D64_BAD_STRUCTURE);
    make_test_directory(image);
    test_sector(image, 18U, 2U)[0] = 17U;
    assert(mt_c64_d64_list_prgs(image, sizeof(image), NULL, NULL, &count) ==
           MT_C64_D64_BAD_STRUCTURE);
}

static void test_file_chain_errors_do_not_write(void)
{
    static uint8_t image[174848U];
    uint8_t output[300U];
    entry_capture_t capture = {0};
    mt_c64_d64_prg_entry_t entry;
    size_t count;
    size_t output_size;
    uint8_t *first;
    uint8_t *second;

    make_test_directory(image);
    assert(mt_c64_d64_list_prgs(image, sizeof(image), capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    entry = capture.entries[0];
    first = test_sector(image, 1U, 0U);
    second = test_sector(image, 1U, 1U);

    memset(output, 0xa5U, sizeof(output));
    output_size = 99U;
    second[0] = 1U;
    second[1] = 0U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_CHAIN_CYCLE);
    assert(output_size == 0U);
    assert_unchanged(output, sizeof(output), 0xa5U);

    make_test_directory(image);
    first = test_sector(image, 1U, 0U);
    first[0] = 43U;
    first[1] = 0U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_BAD_STRUCTURE);
    assert_unchanged(output, sizeof(output), 0xa5U);

    make_test_directory(image);
    first = test_sector(image, 1U, 0U);
    first[0] = 1U;
    first[1] = 21U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_BAD_STRUCTURE);
    assert_unchanged(output, sizeof(output), 0xa5U);

    make_test_directory(image);
    test_sector(image, 1U, 1U)[1] = 0U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_BAD_STRUCTURE);
    assert_unchanged(output, sizeof(output), 0xa5U);

    make_test_directory(image);
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  256U, &output_size) ==
           MT_C64_D64_BUFFER_TOO_SMALL);
    assert_unchanged(output, sizeof(output), 0xa5U);
}

static void test_oversized_prg_is_rejected_before_copy(void)
{
    static uint8_t image[174848U];
    static uint8_t maximum_output[MT_C64_D64_MAX_PRG_SIZE];
    uint8_t output[16U];
    entry_capture_t capture = {0};
    size_t count;
    size_t output_size = 99U;
    size_t index;

    memset(image, 0U, sizeof(image));
    set_directory_entry(image, 1U, 0U, 0x82U, 1U, 0U,
                        (const uint8_t *)"TOO LARGE", 9U, 259U);
    test_sector(image, 18U, 1U)[1] = 0xffU;
    for (index = 0U; index < 259U; ++index) {
        uint8_t track;
        uint8_t sector;
        uint8_t *block;
        test_lba_to_track_sector(index, &track, &sector);
        block = test_sector(image, track, sector);
        if (index + 1U == 259U) {
            block[0] = 0U;
            block[1] = 8U;
        } else {
            test_lba_to_track_sector(index + 1U, &block[0], &block[1]);
        }
    }
    assert(mt_c64_d64_list_prgs(image, sizeof(image), capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    assert(count == 1U);
    memset(output, 0xa5U, sizeof(output));
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_PRG_TOO_LARGE);
    assert(output_size == 0U);
    assert_unchanged(output, sizeof(output), 0xa5U);

    /* The exact transport maximum is accepted, but one byte less caller
     * capacity is rejected without touching the caller's storage. */
    test_sector(image, 13U, 6U)[1] = 7U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &capture.entries[0],
                                  maximum_output, sizeof(maximum_output),
                                  &output_size) == MT_C64_D64_OK);
    assert(output_size == MT_C64_D64_MAX_PRG_SIZE);
    memset(maximum_output, 0xa5U, sizeof(maximum_output));
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &capture.entries[0],
                                  maximum_output,
                                  sizeof(maximum_output) - 1U,
                                  &output_size) ==
           MT_C64_D64_BUFFER_TOO_SMALL);
    assert(output_size == 0U);
    assert_unchanged(maximum_output, sizeof(maximum_output), 0xa5U);
}

static void test_extended_track_geometry(void)
{
    static uint8_t image[MT_C64_D64_MAX_SIZE];
    uint8_t output[3U];
    entry_capture_t capture = {0};
    size_t count;
    size_t output_size;

    memset(image, 0U, sizeof(image));
    set_directory_entry(image, 1U, 0U, 0x82U, 40U, 16U,
                        (const uint8_t *)"TRACK 40", 8U, 1U);
    test_sector(image, 18U, 1U)[1] = 0xffU;
    test_sector(image, 40U, 16U)[0] = 0U;
    test_sector(image, 40U, 16U)[1] = 4U;
    test_sector(image, 40U, 16U)[2] = 0x01U;
    test_sector(image, 40U, 16U)[3] = 0x08U;
    test_sector(image, 40U, 16U)[4] = 0x60U;
    assert(mt_c64_d64_list_prgs(image, 196608U, capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    assert(mt_c64_d64_extract_prg(image, 196608U, &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_OK);
    assert(output_size == sizeof(output) && output[2] == 0x60U);
    assert(mt_c64_d64_extract_prg(image, 174848U, &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_BAD_STRUCTURE);

    capture.stored = 0U;
    set_directory_entry(image, 1U, 0U, 0x82U, 42U, 16U,
                        (const uint8_t *)"TRACK 42", 8U, 1U);
    test_sector(image, 42U, 16U)[0] = 0U;
    test_sector(image, 42U, 16U)[1] = 4U;
    test_sector(image, 42U, 16U)[2] = 0x01U;
    test_sector(image, 42U, 16U)[3] = 0x08U;
    test_sector(image, 42U, 16U)[4] = 0x60U;
    assert(mt_c64_d64_list_prgs(image, 205312U, capture_entry, &capture,
                                &count) == MT_C64_D64_OK);
    assert(mt_c64_d64_extract_prg(image, 205312U, &capture.entries[0],
                                  output, sizeof(output), &output_size) ==
           MT_C64_D64_OK);
}

static void test_prg_api_arguments(void)
{
    static uint8_t image[174848U];
    uint8_t output[3U];
    mt_c64_d64_prg_entry_t entry = {0};
    size_t count = 9U;
    size_t output_size = 9U;

    assert(mt_c64_d64_list_prgs(NULL, sizeof(image), NULL, NULL, &count) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(count == 9U);
    assert(mt_c64_d64_list_prgs(image, 1U, NULL, NULL, &count) ==
           MT_C64_D64_WRONG_SIZE);
    assert(count == 0U);
    assert(mt_c64_d64_list_prgs(image, sizeof(image), NULL, NULL, NULL) ==
           MT_C64_D64_INVALID_ARGUMENT);

    assert(mt_c64_d64_extract_prg(NULL, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(output_size == 0U);
    entry.directory_sector = 19U;
    output_size = 9U;
    assert(mt_c64_d64_extract_prg(image, sizeof(image), &entry, output,
                                  sizeof(output), &output_size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(output_size == 0U);
}

int main(void)
{
    static uint8_t image[MT_C64_D64_MAX_SIZE];
    static uint8_t write_data[MT_C64_D64_SECTOR_SIZE * 2U];
    const uint8_t *data = NULL;
    size_t size = 0U;

    memset(image, 0xA5, sizeof(image));
    assert(mt_c64_d64_sector_count(174848U) == 683U);
    assert(mt_c64_d64_sector_count(175531U) == 683U);
    assert(mt_c64_d64_sector_count(196608U) == 768U);
    assert(mt_c64_d64_sector_count(197376U) == 768U);
    assert(mt_c64_d64_sector_count(205312U) == 802U);
    assert(mt_c64_d64_sector_count(206114U) == 802U);
    assert(mt_c64_d64_sector_count(1U) == 0U);

    assert(mt_c64_d64_resolve_request(image, 175531U, 357U, 19U,
                                      &data, &size) == MT_C64_D64_OK);
    assert(data == image + 357U * MT_C64_D64_SECTOR_SIZE);
    assert(size == 19U * MT_C64_D64_SECTOR_SIZE);

    assert(mt_c64_d64_resolve_request(image, 175531U, 682U, 1U,
                                      &data, &size) == MT_C64_D64_OK);
    assert(mt_c64_d64_resolve_request(image, 175531U, 683U, 1U,
                                      &data, &size) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(mt_c64_d64_resolve_request(image, 175531U, 682U, 2U,
                                      &data, &size) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(mt_c64_d64_resolve_request(image, 123U, 0U, 1U,
                                      &data, &size) == MT_C64_D64_WRONG_SIZE);
    assert(mt_c64_d64_resolve_request(NULL, 174848U, 0U, 1U,
                                      &data, &size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(mt_c64_d64_resolve_request(image, 174848U, 0U, 0U,
                                      &data, &size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(strcmp(mt_c64_d64_result_name(MT_C64_D64_OK), "ok") == 0);

    memset(write_data, 0x3C, sizeof(write_data));
    image[174848U] = 0x5AU;
    assert(mt_c64_d64_write_blocks(image, 175531U, 681U, 2U,
                                   write_data, sizeof(write_data)) ==
           MT_C64_D64_OK);
    assert(memcmp(image + 681U * MT_C64_D64_SECTOR_SIZE,
                  write_data, sizeof(write_data)) == 0);
    assert(image[174848U] == 0x5AU);

    image[0] = 0xA5U;
    assert(mt_c64_d64_write_blocks(image, 174848U, 0U, 1U,
                                   write_data,
                                   MT_C64_D64_SECTOR_SIZE - 1U) ==
           MT_C64_D64_DATA_SIZE_MISMATCH);
    assert(image[0] == 0xA5U);
    assert(mt_c64_d64_write_blocks(image, 174848U, 682U, 2U,
                                   write_data, sizeof(write_data)) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(image[0] == 0xA5U);
    assert(mt_c64_d64_write_blocks(NULL, 174848U, 0U, 1U,
                                   write_data, MT_C64_D64_SECTOR_SIZE) ==
           MT_C64_D64_INVALID_ARGUMENT);

    test_prg_listing_and_extraction();
    test_directory_errors_and_identity();
    test_file_chain_errors_do_not_write();
    test_oversized_prg_is_rejected_before_copy();
    test_extended_track_geometry();
    test_prg_api_arguments();
    assert(strcmp(mt_c64_d64_result_name(MT_C64_D64_CHAIN_CYCLE),
                  "chain cycle") == 0);
    return 0;
}
