/* SPDX-License-Identifier: GPL-3.0-only */
/* Normalization and wrapping adapted from Appletini ONE's
 * ps_sources/frontend/config_menu_text_reader.c at
 * 1fd4f371e8f4ef08bc63875550564590609fcfeb. Storage and UI stay with callers. */
#include "multitini_text_reader.h"

#include <stdlib.h>
#include <string.h>

void mt_text_reader_init(mt_text_reader_t *reader)
{
    if (reader != NULL) memset(reader, 0, sizeof(*reader));
}

void mt_text_reader_close(mt_text_reader_t *reader)
{
    if (reader == NULL) return;
    free(reader->line_starts);
    free(reader->text);
    mt_text_reader_init(reader);
}

/* Consume a valid UTF-8 character as one unit, including unsupported glyphs.
 * A malformed byte becomes '?'; a capped final sequence becomes one marker. */
static uint32_t character(const unsigned char *input, size_t size,
                           size_t *position)
{
    const size_t start = *position;
    const uint32_t first = input[start];
    uint32_t code, minimum;
    size_t length, index;

    *position = start + 1U;
    if (first < 0x80U) return first;
    if (first >= 0xC2U && first <= 0xDFU) {
        code = first & 0x1FU;
        length = 2U;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        code = first & 0x0FU;
        length = 3U;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        code = first & 0x07U;
        length = 4U;
        minimum = 0x10000U;
    } else return '?';

    if (length > size - start) {
        while (*position < size && (input[*position] & 0xC0U) == 0x80U)
            ++*position;
        return '?';
    }
    for (index = 1U; index < length; ++index) {
        if ((input[start + index] & 0xC0U) != 0x80U) return '?';
        code = (code << 6U) | (input[start + index] & 0x3FU);
    }
    *position = start + length;
    if (code < minimum || code > 0x10FFFFU ||
        (code >= 0xD800U && code <= 0xDFFFU)) return '?';
    return code;
}

static size_t normalize(char *text, size_t size)
{
    size_t read_pos = 0U, write_pos = 0U;
    const unsigned char *const input = (const unsigned char *)text;
    if (size >= 3U && input[0] == 0xEFU && input[1] == 0xBBU && input[2] == 0xBFU)
        read_pos = 3U;
    while (read_pos < size) {
        uint32_t code = character(input, size, &read_pos);
        if (code == '\r') {
            if (read_pos < size && input[read_pos] == '\n') ++read_pos;
            code = '\n';
        } else if (code == 0xA0U) code = ' ';
        else if (code == 0x2018U || code == 0x2019U) code = '\'';
        else if (code == 0x201CU || code == 0x201DU) code = '"';
        else if (code == 0x2013U || code == 0x2014U) code = '-';
        if (code != '\n' && code != '\t' && (code < 0x20U || code > 0x7EU))
            code = '?';
        text[write_pos++] = (char)code;
    }
    text[write_pos] = '\0';
    return write_pos;
}

/* Tabs expand while rendering, so a file full of tabs cannot inflate the
 * cached text. Wrapping prefers a word boundary and always consumes input. */
static size_t visual_line(const mt_text_reader_t *reader, size_t start,
                            char *output)
{
    size_t position = start, column = 0U;
    size_t break_position = 0U, break_column = 0U;
    bool in_space = false, seen_text = false, wrapped = false;

    while (position < reader->length) {
        const char ch = reader->text[position];
        size_t width;
        if (ch == '\n') {
            ++position;
            break;
        }
        if (column == reader->columns) {
            if (ch == ' ' || ch == '\t') {
                while (position < reader->length &&
                       (reader->text[position] == ' ' || reader->text[position] == '\t'))
                    ++position;
                if (position < reader->length && reader->text[position] == '\n')
                    ++position;
                break;
            }
            wrapped = true;
            break;
        }
        width = ch == '\t' ? 8U - column % 8U : 1U;
        if (width > reader->columns - column) width = reader->columns - column;
        if (ch == ' ' || ch == '\t') {
            if (seen_text) {
                if (!in_space) break_column = column;
                break_position = position + 1U;
            }
            in_space = true;
        } else {
            seen_text = true;
            in_space = false;
        }
        if (output != NULL) memset(output + column, ch == '\t' ? ' ' : ch, width);
        column += width;
        ++position;
    }
    if (wrapped && break_position != 0U) {
        position = break_position;
        column = break_column;
        while (position < reader->length &&
               (reader->text[position] == ' ' || reader->text[position] == '\t'))
            ++position;
        if (position < reader->length && reader->text[position] == '\n') ++position;
    }
    if (output != NULL) output[column] = '\0';
    return position;
}

mt_text_reader_result_t mt_text_reader_open(mt_text_reader_t *reader,
                                           const void *data, size_t size,
                                           bool truncated, size_t columns,
                                           size_t page_rows)
{
    mt_text_reader_t candidate = {0};
    const unsigned char *const input = data;
    const size_t loaded = size > MT_TEXT_READER_INPUT_LIMIT ?
                          MT_TEXT_READER_INPUT_LIMIT : size;
    mt_text_reader_result_t result = MT_TEXT_READER_OK;
    size_t position, index;

    if (reader == NULL) return MT_TEXT_READER_INVALID_ARGUMENT;
    if ((data == NULL && size != 0U) || columns == 0U ||
        columns > MT_TEXT_READER_MAX_COLUMNS || page_rows == 0U) {
        result = MT_TEXT_READER_INVALID_ARGUMENT;
        goto fail;
    }
    if (loaded >= 2U &&
        ((input[0] == 0xFFU && input[1] == 0xFEU) ||
         (input[0] == 0xFEU && input[1] == 0xFFU))) {
        result = MT_TEXT_READER_UNSUPPORTED_ENCODING;
        goto fail;
    }
    candidate.columns = columns;
    candidate.page_rows = page_rows;
    candidate.truncated = truncated || loaded < size;
    if (loaded != 0U) {
        candidate.text = malloc(loaded + 1U);
        if (candidate.text == NULL) {
            result = MT_TEXT_READER_NO_MEMORY;
            goto fail;
        }
        memcpy(candidate.text, input, loaded);
        candidate.length = normalize(candidate.text, loaded);
        for (position = 0U; position < candidate.length;
             position = visual_line(&candidate, position, NULL))
            ++candidate.line_count;
        if (candidate.line_count != 0U) {
            candidate.line_starts = malloc(candidate.line_count * sizeof(*candidate.line_starts));
            if (candidate.line_starts == NULL) {
                result = MT_TEXT_READER_NO_MEMORY;
                goto fail;
            }
            position = 0U;
            for (index = 0U; index < candidate.line_count; ++index) {
                candidate.line_starts[index] = (uint32_t)position;
                position = visual_line(&candidate, position, NULL);
            }
        }
    }
    candidate.active = true;
    mt_text_reader_close(reader);
    *reader = candidate;
    return MT_TEXT_READER_OK;

fail:
    mt_text_reader_close(&candidate);
    mt_text_reader_close(reader);
    return result;
}

bool mt_text_reader_line(const mt_text_reader_t *reader, size_t line,
                          char *output, size_t capacity)
{
    if (reader == NULL || !reader->active || output == NULL ||
        capacity <= reader->columns || line >= reader->line_count) return false;
    (void)visual_line(reader, reader->line_starts[line], output);
    return true;
}

bool mt_text_reader_navigate(mt_text_reader_t *reader,
                              mt_text_reader_navigation_t navigation)
{
    size_t before, last, step;
    if (reader == NULL || !reader->active) return false;
    before = reader->top;
    last = reader->line_count > reader->page_rows ?
           reader->line_count - reader->page_rows : 0U;
    step = navigation == MT_TEXT_READER_PAGE_UP || navigation == MT_TEXT_READER_PAGE_DOWN ?
           reader->page_rows : 1U;
    switch (navigation) {
    case MT_TEXT_READER_UP:
    case MT_TEXT_READER_PAGE_UP:
        reader->top = reader->top > step ? reader->top - step : 0U;
        break;
    case MT_TEXT_READER_DOWN:
    case MT_TEXT_READER_PAGE_DOWN:
        reader->top = step < last - reader->top ? reader->top + step : last;
        break;
    case MT_TEXT_READER_HOME:
        reader->top = 0U;
        break;
    case MT_TEXT_READER_END:
        reader->top = last;
        break;
    default:
        return false;
    }
    return before != reader->top;
}
