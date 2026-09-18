/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_text_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void open_text(mt_text_reader_t *reader, const char *text,
                       size_t columns, size_t page_rows)
{
    check(mt_text_reader_open(reader, text, strlen(text), false, columns, page_rows) ==
          MT_TEXT_READER_OK, "open text");
}

static void line_is(const mt_text_reader_t *reader, size_t index,
                      const char *expected)
{
    char output[MT_TEXT_READER_MAX_COLUMNS + 2U];
    memset(output, '#', sizeof(output));
    check(mt_text_reader_line(reader, index, output, reader->columns + 1U), "read indexed line");
    check(strcmp(output, expected) == 0, "expected line contents");
    check(output[reader->columns + 1U] == '#', "line respects output capacity");
}

static void test_lifecycle_and_errors(void)
{
    mt_text_reader_t reader;
    char output[96] = "unchanged";
    const unsigned char utf16le[] = {0xFFU, 0xFEU, 'A', 0U};
    const unsigned char utf16be[] = {0xFEU, 0xFFU, 0U, 'A'};
    size_t iteration;
    mt_text_reader_init(&reader);
    check(!reader.active && reader.text == NULL && reader.line_starts == NULL, "initialized closed state");
    check(mt_text_reader_open(&reader, NULL, 0U, false, 95U, 20U) == MT_TEXT_READER_OK &&
          reader.active && reader.line_count == 0U && reader.length == 0U, "empty file is valid");
    check(!mt_text_reader_line(&reader, 0U, output, sizeof(output)) &&
          strcmp(output, "unchanged") == 0, "empty file has no readable lines");
    check(!mt_text_reader_navigate(&reader, MT_TEXT_READER_END), "empty navigation is stable");
    open_text(&reader, "\357\273\277", 95U, 20U);
    check(reader.line_count == 0U && reader.length == 0U, "BOM-only file is empty");
    for (iteration = 0U; iteration < 32U; ++iteration) {
        open_text(&reader, "Replacing\nan existing\nreader", 10U, 2U);
        check(reader.line_count == 4U && reader.top == 0U, "reopen replaces indexed content");
        check(mt_text_reader_open(&reader, utf16le, sizeof(utf16le), false, 10U, 2U) ==
              MT_TEXT_READER_UNSUPPORTED_ENCODING && !reader.active &&
              reader.text == NULL && reader.line_starts == NULL, "UTF-16 LE closes old content");
    }
    check(mt_text_reader_open(&reader, utf16be, sizeof(utf16be), false, 10U, 2U) ==
          MT_TEXT_READER_UNSUPPORTED_ENCODING, "UTF-16 BE is rejected");
    check(mt_text_reader_open(&reader, NULL, 1U, false, 10U, 2U) ==
          MT_TEXT_READER_INVALID_ARGUMENT, "nonempty data needs input");
    check(mt_text_reader_open(&reader, "x", 1U, false, 0U, 2U) ==
          MT_TEXT_READER_INVALID_ARGUMENT, "zero width rejected");
    check(mt_text_reader_open(&reader, "x", 1U, false, 96U, 2U) ==
          MT_TEXT_READER_INVALID_ARGUMENT, "oversize width rejected");
    check(mt_text_reader_open(&reader, "x", 1U, false, 10U, 0U) ==
          MT_TEXT_READER_INVALID_ARGUMENT, "zero page rows rejected");
    check(mt_text_reader_open(NULL, "x", 1U, false, 10U, 2U) ==
          MT_TEXT_READER_INVALID_ARGUMENT, "null reader rejected");
    check(!mt_text_reader_navigate(NULL, MT_TEXT_READER_END) &&
          !mt_text_reader_line(NULL, 0U, output, sizeof(output)), "null access is safe");
    mt_text_reader_close(&reader);
    mt_text_reader_close(&reader);
    mt_text_reader_close(NULL);
    mt_text_reader_init(NULL);
}

static void test_normalization(void)
{
    mt_text_reader_t reader = {0};
    char copied[] = "Copy me";
    const unsigned char controls[] = {'A', 0U, 1U, 0x7FU, 'Z'};
    const unsigned char malformed[] = {'A', 0xE2U, 'B', 0x80U, 'C', 0xE2U, 0x80U};
    const unsigned char invalid_codes[] = {0xEDU, 0xA0U, 0x80U, 0xF4U, 0x90U, 0x80U, 0x80U};
    open_text(&reader, "\357\273\277one\r\ntwo\rthree\nfour\n\n", 95U, 5U);
    check(reader.line_count == 5U, "CR LF CRLF and blank lines");
    line_is(&reader, 0U, "one");
    line_is(&reader, 1U, "two");
    line_is(&reader, 2U, "three");
    line_is(&reader, 3U, "four");
    line_is(&reader, 4U, "");
    open_text(&reader, "\342\200\234Caf\303\251\342\200\235\302\240\342\200\230x\342\200\231\342\200\223\342\200\224\360\237\230\200", 95U, 2U);
    line_is(&reader, 0U, "\"Caf?\" 'x'--?");
    check(mt_text_reader_open(&reader, controls, sizeof(controls), false, 95U, 2U) ==
          MT_TEXT_READER_OK, "embedded controls accepted safely");
    line_is(&reader, 0U, "A???Z");
    check(mt_text_reader_open(&reader, malformed, sizeof(malformed), true, 95U, 2U) ==
          MT_TEXT_READER_OK && reader.truncated, "partial final UTF-8 sequence");
    line_is(&reader, 0U, "A?B?C?");
    check(mt_text_reader_open(&reader, invalid_codes, sizeof(invalid_codes), false, 95U, 2U) ==
          MT_TEXT_READER_OK, "invalid Unicode scalar values");
    line_is(&reader, 0U, "??");
    open_text(&reader, copied, 95U, 2U);
    memset(copied, 'X', sizeof(copied) - 1U);
    line_is(&reader, 0U, "Copy me");
    mt_text_reader_close(&reader);
}

static void test_wrapping(void)
{
    mt_text_reader_t reader = {0};
    char output[96] = "unchanged";
    open_text(&reader, "one two three", 7U, 2U);
    check(reader.line_count == 2U, "word ending exactly at edge stays put");
    line_is(&reader, 0U, "one two");
    line_is(&reader, 1U, "three");
    open_text(&reader, "one longer last", 8U, 2U);
    check(reader.line_count == 3U, "prefer word boundaries");
    line_is(&reader, 0U, "one");
    line_is(&reader, 1U, "longer");
    line_is(&reader, 2U, "last");
    open_text(&reader, "abcdefghijk", 4U, 2U);
    check(reader.line_count == 3U, "long unbroken word is split");
    line_is(&reader, 0U, "abcd");
    line_is(&reader, 1U, "efgh");
    line_is(&reader, 2U, "ijk");
    open_text(&reader, "one     two\n  indent\nA\tB", 10U, 2U);
    check(reader.line_count == 4U, "whitespace wrapping and indentation");
    line_is(&reader, 0U, "one");
    line_is(&reader, 1U, "two");
    line_is(&reader, 2U, "  indent");
    line_is(&reader, 3U, "A       B");
    open_text(&reader, "\tX\nABC \t \nnext", 3U, 2U);
    check(reader.line_count == 5U, "narrow tab and trailing separators consume input");
    line_is(&reader, 0U, "   ");
    line_is(&reader, 1U, "X");
    line_is(&reader, 2U, "ABC");
    line_is(&reader, 3U, "nex");
    line_is(&reader, 4U, "t");
    open_text(&reader, "A\tB\n\nC", 1U, 3U);
    check(reader.line_count == 4U, "single-column wrapping makes progress");
    line_is(&reader, 0U, "A");
    line_is(&reader, 1U, "B");
    line_is(&reader, 2U, "");
    line_is(&reader, 3U, "C");
    check(!mt_text_reader_line(&reader, 0U, output, 1U) && strcmp(output, "unchanged") == 0,
          "short output remains untouched");
    check(!mt_text_reader_line(&reader, reader.line_count, output, sizeof(output)) &&
          !mt_text_reader_line(&reader, 0U, NULL, sizeof(output)), "invalid line requests rejected");
    mt_text_reader_close(&reader);
}

static void test_navigation(void)
{
    mt_text_reader_t reader = {0};
    open_text(&reader, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 8U, 3U);
    check(!mt_text_reader_navigate(&reader, MT_TEXT_READER_UP) &&
          !mt_text_reader_navigate(&reader, MT_TEXT_READER_PAGE_UP), "top clamps upward movement");
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_DOWN) && reader.top == 1U,
          "line down");
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_PAGE_DOWN) && reader.top == 4U,
          "page down");
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_PAGE_DOWN) && reader.top == 7U &&
          !mt_text_reader_navigate(&reader, MT_TEXT_READER_DOWN), "last full page clamps movement");
    line_is(&reader, reader.top + 2U, "9");
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_UP) && reader.top == 6U &&
          mt_text_reader_navigate(&reader, MT_TEXT_READER_PAGE_UP) && reader.top == 3U,
          "line and page up");
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_HOME) && reader.top == 0U &&
          mt_text_reader_navigate(&reader, MT_TEXT_READER_END) && reader.top == 7U,
          "home and end");
    check(!mt_text_reader_navigate(&reader, (mt_text_reader_navigation_t)99) && reader.top == 7U,
          "unknown navigation leaves position");
    open_text(&reader, "small", 8U, SIZE_MAX);
    check(!mt_text_reader_navigate(&reader, MT_TEXT_READER_PAGE_DOWN) && reader.top == 0U,
          "huge page size does not overflow");
    mt_text_reader_close(&reader);
}

static void test_large_input(void)
{
    mt_text_reader_t reader = {0};
    char *input = malloc(MT_TEXT_READER_INPUT_LIMIT + 1U);
    char expected[MT_TEXT_READER_MAX_COLUMNS + 1U];
    check(input != NULL, "allocate large test input");
    memset(input, 'X', MT_TEXT_READER_INPUT_LIMIT + 1U);
    check(mt_text_reader_open(&reader, input, MT_TEXT_READER_INPUT_LIMIT + 1U, false, 95U, 40U) ==
          MT_TEXT_READER_OK && reader.truncated && reader.length == MT_TEXT_READER_INPUT_LIMIT,
          "overlength file is capped and marked");
    check(reader.line_count == (MT_TEXT_READER_INPUT_LIMIT + 94U) / 95U,
          "maximum unbroken file indexes all visual lines");
    memset(expected, 'X', 95U); expected[95] = '\0';
    line_is(&reader, 0U, expected);
    check(mt_text_reader_navigate(&reader, MT_TEXT_READER_END) &&
          reader.top == reader.line_count - 40U, "large indexed file jumps to end");
    memset(input, '\n', MT_TEXT_READER_INPUT_LIMIT);
    check(mt_text_reader_open(&reader, input, MT_TEXT_READER_INPUT_LIMIT, false, 95U, 40U) ==
          MT_TEXT_READER_OK && !reader.truncated && reader.line_count == MT_TEXT_READER_INPUT_LIMIT,
          "maximum newline count fits index allocation");
    line_is(&reader, reader.line_count - 1U, "");
    memset(input, '\t', MT_TEXT_READER_INPUT_LIMIT);
    check(mt_text_reader_open(&reader, input, MT_TEXT_READER_INPUT_LIMIT, true, 95U, 40U) ==
          MT_TEXT_READER_OK && reader.truncated && reader.length == MT_TEXT_READER_INPUT_LIMIT,
          "tabs never inflate normalized allocation");
    free(input);
    mt_text_reader_close(&reader);
}

static void test_byte_sequences(void)
{
    mt_text_reader_t reader = {0};
    unsigned char input[4096];
    char output[MT_TEXT_READER_MAX_COLUMNS + 1U];
    uint32_t random = UINT32_C(0x12345678);
    size_t index, width, line;
    for (index = 0U; index < sizeof(input); ++index) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        input[index] = (unsigned char)(random >> 24U);
    }
    for (width = 1U; width <= MT_TEXT_READER_MAX_COLUMNS; width += 7U) {
        check(mt_text_reader_open(&reader, input, sizeof(input), false, width, 10U) ==
              MT_TEXT_READER_OK, "mixed byte sequence opens");
        check(reader.line_count <= reader.length, "every indexed line consumes input");
        for (line = 0U; line < reader.line_count; ++line) {
            check(reader.line_starts[line] < reader.length &&
                  (line == 0U || reader.line_starts[line] > reader.line_starts[line - 1U]),
                  "visual line offsets are bounded and strictly increasing");
            check(mt_text_reader_line(&reader, line, output, sizeof(output)) &&
                  strlen(output) <= width, "random line stays within width");
            for (index = 0U; output[index] != '\0'; ++index)
                check(output[index] >= ' ' && output[index] <= '~', "rendered characters are printable ASCII");
        }
    }
    mt_text_reader_close(&reader);
}

int main(void)
{
    test_lifecycle_and_errors();
    test_normalization();
    test_wrapping();
    test_navigation();
    test_large_input();
    test_byte_sequences();
    puts("PASS: bounded text reader, Unicode normalization, wrapping, indexed navigation and lifecycle");
    return 0;
}
