/* SPDX-License-Identifier: GPL-3.0-only */
#include "text_document.h"

#include "sd_storage.h"

#include <stdlib.h>

mt_text_document_result_t mt_text_document_load(mt_text_reader_t *reader,
                                                const char *path,
                                                size_t columns,
                                                size_t page_rows,
                                                void (*progress)(void *),
                                                void *context)
{
    const size_t capacity = MT_TEXT_READER_INPUT_LIMIT;
    mt_text_document_result_t result = MT_TEXT_DOCUMENT_OK;
    mt_text_reader_result_t decoded;
    mt_sd_result_t io;
    unsigned char *data;
    bool truncated = false;

    if (reader == NULL) return MT_TEXT_DOCUMENT_INVALID_PATH;
    mt_text_reader_close(reader);
    if (path == NULL) return MT_TEXT_DOCUMENT_INVALID_PATH;
    if (columns == 0U || columns > MT_TEXT_READER_MAX_COLUMNS || page_rows == 0U)
        return MT_TEXT_DOCUMENT_DECODE_FAILED;
    data = malloc(capacity);
    if (data == NULL) return MT_TEXT_DOCUMENT_NO_MEMORY;
    io = mt_sd_storage_read_file_prefix(path, data, capacity, &truncated, progress, context);
    if (io.status == MT_SD_MOUNT_FAILED || io.status == MT_SD_OPEN_FAILED)
        result = MT_TEXT_DOCUMENT_OPEN_FAILED;
    else if (io.status == MT_SD_CLOSE_FAILED) result = MT_TEXT_DOCUMENT_CLOSE_FAILED;
    else if (io.status != MT_SD_OK || io.size > capacity) result = MT_TEXT_DOCUMENT_READ_FAILED;
    if (result == MT_TEXT_DOCUMENT_OK) {
        decoded = mt_text_reader_open(reader, data, io.size, truncated, columns, page_rows);
        if (decoded == MT_TEXT_READER_NO_MEMORY) result = MT_TEXT_DOCUMENT_NO_MEMORY;
        else if (decoded != MT_TEXT_READER_OK) result = MT_TEXT_DOCUMENT_DECODE_FAILED;
    }
    free(data);
    return result;
}
