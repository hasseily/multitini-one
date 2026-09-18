/* SPDX-License-Identifier: GPL-3.0-only */
#include "text_document.h"
#include "sd_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mt_text_reader_t *observed_reader;
static const unsigned char *file_data;
static size_t file_size, prefix_calls, progress_calls;
static mt_sd_status_t prefix_status;
static bool prefix_active, oversized_result;
static unsigned progress_context;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

mt_sd_result_t mt_sd_storage_read_file_prefix(const char *path, uint8_t *dst,
                                              size_t capacity, bool *truncated,
                                              void (*progress)(void *), void *context)
{
    mt_sd_result_t result = {prefix_status, prefix_status == MT_SD_OK ? 0U : 9U, 0U};
    size_t position;
    const size_t loaded = file_size > capacity ? capacity : file_size;
    check(path != NULL && strcmp(path, "0:/multitini/cores/c64/media/readme.txt") == 0,
          "loader uses resolved path without rewriting");
    check(observed_reader != NULL && !observed_reader->active, "old reader closes before file work");
    check(capacity == MT_TEXT_READER_INPUT_LIMIT && dst != NULL && truncated != NULL,
          "loader supplies bounded prefix storage");
    ++prefix_calls;
    prefix_active = true;
    *truncated = file_size > capacity;
    if (prefix_status == MT_SD_OK) {
        for (position = 0U; position < loaded;) {
            size_t chunk = loaded - position;
            if (chunk > 4096U) chunk = 4096U;
            memcpy(dst + position, file_data + position, chunk);
            position += chunk;
            if (progress != NULL) progress(context);
        }
        result.size = oversized_result ? capacity + 1U : loaded;
    }
    prefix_active = false;
    return result;
}

static void progress(void *context)
{
    check(context == &progress_context && progress_context == 123U, "progress context preserved");
    check(prefix_active && !observed_reader->active, "callback forwards before decoding");
    ++progress_calls;
}

static void reset_fake(mt_text_reader_t *reader, const void *data, size_t size)
{
    file_data = data;
    file_size = size;
    prefix_calls = progress_calls = 0U;
    prefix_status = MT_SD_OK;
    prefix_active = oversized_result = false;
    observed_reader = reader;
    progress_context = 123U;
}

static mt_text_document_result_t load(mt_text_reader_t *reader)
{
    return mt_text_document_load(reader, "0:/multitini/cores/c64/media/readme.txt",
                                 95U, 40U, progress, &progress_context);
}

static void test_success_and_bounds(void)
{
    mt_text_reader_t reader = {0};
    char line[96];
    unsigned char *data = malloc(MT_TEXT_READER_INPUT_LIMIT + 100U);
    check(data != NULL, "allocate large fixture");
    memset(data, 'A', MT_TEXT_READER_INPUT_LIMIT + 100U);
    reset_fake(&reader, "one\r\ntwo", 8U);
    check(load(&reader) == MT_TEXT_DOCUMENT_OK && reader.active && reader.line_count == 2U,
          "load and normalize text");
    check(!prefix_active && prefix_calls == 1U && progress_calls == 1U,
          "small document finishes file work and reports one chunk");
    check(mt_text_reader_line(&reader, 1U, line, sizeof(line)) && strcmp(line, "two") == 0,
          "decoded bytes survive source buffer cleanup");
    reset_fake(&reader, NULL, 0U);
    check(load(&reader) == MT_TEXT_DOCUMENT_OK && reader.active && reader.line_count == 0U &&
          !reader.truncated && progress_calls == 0U, "empty file opens reader without progress");
    reset_fake(&reader, data, MT_TEXT_READER_INPUT_LIMIT);
    check(load(&reader) == MT_TEXT_DOCUMENT_OK && !reader.truncated &&
          reader.length == MT_TEXT_READER_INPUT_LIMIT && progress_calls == 64U,
          "exact input limit is not truncated");
    reset_fake(&reader, data, MT_TEXT_READER_INPUT_LIMIT + 100U);
    check(load(&reader) == MT_TEXT_DOCUMENT_OK && reader.truncated &&
          reader.length == MT_TEXT_READER_INPUT_LIMIT && progress_calls == 64U,
          "larger files retain bounded content and truncation marker");
    reset_fake(&reader, data, 4096U);
    check(mt_text_document_load(&reader, "0:/multitini/cores/c64/media/readme.txt",
                                20U, 5U, NULL, NULL) == MT_TEXT_DOCUMENT_OK &&
          reader.columns == 20U && reader.page_rows == 5U && progress_calls == 0U,
          "optional callback and caller layout supported");
    free(data);
    mt_text_reader_close(&reader);
}

static void test_errors(void)
{
    static const mt_sd_status_t io_errors[] = {
        MT_SD_MOUNT_FAILED, MT_SD_OPEN_FAILED, MT_SD_READ_FAILED, MT_SD_CLOSE_FAILED
    };
    static const mt_text_document_result_t expected[] = {
        MT_TEXT_DOCUMENT_OPEN_FAILED, MT_TEXT_DOCUMENT_OPEN_FAILED,
        MT_TEXT_DOCUMENT_READ_FAILED, MT_TEXT_DOCUMENT_CLOSE_FAILED
    };
    mt_text_reader_t reader = {0};
    const unsigned char utf16[] = {0xFFU, 0xFEU, 'A', 0U};
    size_t index;
    for (index = 0U; index < sizeof(io_errors) / sizeof(io_errors[0]); ++index) {
        reset_fake(&reader, "data", 4U);
        check(mt_text_reader_open(&reader, "old", 3U, false, 20U, 5U) == MT_TEXT_READER_OK,
              "open old content before IO failure");
        prefix_status = io_errors[index];
        check(load(&reader) == expected[index] && !reader.active && !prefix_active &&
              prefix_calls == 1U && progress_calls == 0U, "IO failure leaves reader closed");
    }
    reset_fake(&reader, "data", 4U);
    oversized_result = true;
    check(load(&reader) == MT_TEXT_DOCUMENT_READ_FAILED && !reader.active,
          "invalid backend byte count is rejected");
    reset_fake(&reader, utf16, sizeof(utf16));
    check(load(&reader) == MT_TEXT_DOCUMENT_DECODE_FAILED && !reader.active && !prefix_active,
          "decode failure follows completed file operation");
    reset_fake(&reader, "data", 4U);
    check(mt_text_reader_open(&reader, "old", 3U, false, 20U, 5U) == MT_TEXT_READER_OK,
          "open previous content before invalid load");
    check(mt_text_document_load(&reader, NULL, 20U, 5U, progress, &progress_context) ==
          MT_TEXT_DOCUMENT_INVALID_PATH && !reader.active && prefix_calls == 0U,
          "invalid path closes old reader without IO");
    check(mt_text_document_load(NULL, "anything", 20U, 5U, NULL, NULL) ==
          MT_TEXT_DOCUMENT_INVALID_PATH, "null reader rejected");
    check(mt_text_document_load(&reader, "anything", 0U, 5U, NULL, NULL) ==
          MT_TEXT_DOCUMENT_DECODE_FAILED && !reader.active && prefix_calls == 0U,
          "invalid layout fails before IO");
    mt_text_reader_close(&reader);
}

int main(void)
{
    test_success_and_bounds();
    test_errors();
    puts("PASS: bounded document loading, progress, truncation and reader cleanup");
    return 0;
}
