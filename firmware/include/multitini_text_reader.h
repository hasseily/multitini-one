/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_TEXT_READER_H
#define MULTITINI_TEXT_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_TEXT_READER_INPUT_LIMIT (256U * 1024U)
#define MT_TEXT_READER_MAX_COLUMNS 95U

typedef enum {
    MT_TEXT_READER_OK = 0,
    MT_TEXT_READER_INVALID_ARGUMENT,
    MT_TEXT_READER_NO_MEMORY,
    MT_TEXT_READER_UNSUPPORTED_ENCODING
} mt_text_reader_result_t;

typedef enum {
    MT_TEXT_READER_UP = 0,
    MT_TEXT_READER_DOWN,
    MT_TEXT_READER_PAGE_UP,
    MT_TEXT_READER_PAGE_DOWN,
    MT_TEXT_READER_HOME,
    MT_TEXT_READER_END
} mt_text_reader_navigation_t;

/* Initialize before use. The reader owns its buffers; do not copy an open
 * reader or modify its fields. The fields are public for rendering/status. */
typedef struct {
    char *text;
    uint32_t *line_starts;
    size_t length;
    size_t line_count;
    size_t top;
    size_t columns;
    size_t page_rows;
    bool active;
    bool truncated;
} mt_text_reader_t;

void mt_text_reader_init(mt_text_reader_t *reader);
void mt_text_reader_close(mt_text_reader_t *reader);

/* Copy at most INPUT_LIMIT bytes, normalize UTF-8/ASCII and index visual lines.
 * Pass truncated when the supplied data is only the beginning of a file.
 * columns must be 1..MAX_COLUMNS and page_rows must be positive. UTF-16 BOMs
 * are rejected. Any failure closes the old reader; success opens even an
 * empty file (line_count == 0). A trailing newline ends the preceding line
 * without adding an extra empty line. The caller can free data after return. */
mt_text_reader_result_t mt_text_reader_open(mt_text_reader_t *reader,
                                           const void *data, size_t size,
                                           bool truncated, size_t columns,
                                           size_t page_rows);

/* Read an absolute visual line. capacity must be at least columns + 1.
 * Invalid requests return false and leave output untouched. */
bool mt_text_reader_line(const mt_text_reader_t *reader, size_t line,
                          char *output, size_t capacity);

/* Keep top within the last full page; return true only if top changed. */
bool mt_text_reader_navigate(mt_text_reader_t *reader,
                              mt_text_reader_navigation_t navigation);

#endif
