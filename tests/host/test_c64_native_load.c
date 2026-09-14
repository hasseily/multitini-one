/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_native_load.h"
#include "multitini_c64_content.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t scan;
    bool pressed;
    uint32_t at_ms;
} event_t;

typedef struct {
    event_t events[256];
    size_t event_count;
    size_t delay_count;
    size_t fail_event;
    size_t fail_delay;
    bool deliver_failed_event;
    bool held[256];
    uint32_t now_ms;
    char text[128];
    size_t text_length;
} keyboard_t;

/* Decode the receiving C64 matrix, including the upstream number-key
 * remaps. Do not decode as a US PC keyboard: quotes, ampersand, apostrophe,
 * brackets, semicolon and equals have different matrix positions. */
static char c64_character(uint8_t scan, bool shifted)
{
    switch (scan) {
    case 0x1cU: assert(!shifted); return 'A';
    case 0x32U: assert(!shifted); return 'B';
    case 0x21U: assert(!shifted); return 'C';
    case 0x23U: assert(!shifted); return 'D';
    case 0x24U: assert(!shifted); return 'E';
    case 0x2bU: assert(!shifted); return 'F';
    case 0x34U: assert(!shifted); return 'G';
    case 0x33U: assert(!shifted); return 'H';
    case 0x43U: assert(!shifted); return 'I';
    case 0x3bU: assert(!shifted); return 'J';
    case 0x42U: assert(!shifted); return 'K';
    case 0x4bU: assert(!shifted); return 'L';
    case 0x3aU: assert(!shifted); return 'M';
    case 0x31U: assert(!shifted); return 'N';
    case 0x44U: assert(!shifted); return 'O';
    case 0x4dU: assert(!shifted); return 'P';
    case 0x15U: assert(!shifted); return 'Q';
    case 0x2dU: assert(!shifted); return 'R';
    case 0x1bU: assert(!shifted); return 'S';
    case 0x2cU: assert(!shifted); return 'T';
    case 0x3cU: assert(!shifted); return 'U';
    case 0x2aU: assert(!shifted); return 'V';
    case 0x1dU: assert(!shifted); return 'W';
    case 0x22U: assert(!shifted); return 'X';
    case 0x35U: assert(!shifted); return 'Y';
    case 0x1aU: assert(!shifted); return 'Z';
    case 0x16U: return shifted ? '!' : '1';
    case 0x1eU: return shifted ? '"' : '2';
    case 0x26U: return shifted ? '#' : '3';
    case 0x25U: return shifted ? '$' : '4';
    case 0x2eU: return shifted ? '%' : '5';
    case 0x36U: return shifted ? '\'' : '6';
    case 0x3dU: return shifted ? '&' : '7';
    case 0x3eU: return shifted ? '*' : '8';
    case 0x46U: return shifted ? '(' : '9';
    case 0x45U: return shifted ? ')' : '0';
    case 0x29U: assert(!shifted); return ' ';
    case 0x55U: assert(!shifted); return '+';
    case 0x4eU: assert(!shifted); return '-';
    case 0x49U: assert(!shifted); return '.';
    case 0x4aU: assert(!shifted); return '/';
    case 0x09U: assert(!shifted); return '=';
    case 0x41U: assert(!shifted); return ',';
    case 0x4cU: return shifted ? '[' : ':';
    case 0x52U: return shifted ? ']' : ';';
    case 0x5aU: assert(!shifted); return '\r';
    default: assert(false); return '\0';
    }
}

static int emit(void *context, uint8_t scan, bool pressed)
{
    keyboard_t *const keyboard = context;
    bool fail;

    assert(keyboard->event_count < 256U);
    keyboard->events[keyboard->event_count++] =
        (event_t){scan, pressed, keyboard->now_ms};
    fail = keyboard->event_count == keyboard->fail_event;
    if (!fail || keyboard->deliver_failed_event) {
        if (pressed) {
            assert(!keyboard->held[scan]);
        }
        keyboard->held[scan] = pressed;
        if (pressed && scan != 0x12U) {
            assert(keyboard->text_length < sizeof(keyboard->text) - 1U);
            keyboard->text[keyboard->text_length++] =
                c64_character(scan, keyboard->held[0x12U]);
            keyboard->text[keyboard->text_length] = '\0';
        }
    }
    return fail ? -1 : 0;
}

static int delay(void *context, uint32_t milliseconds)
{
    keyboard_t *const keyboard = context;
    assert(milliseconds >= 40U);
    keyboard->now_ms += milliseconds;
    ++keyboard->delay_count;
    return keyboard->delay_count == keyboard->fail_delay ? -1 : 0;
}

static const mt_c64_native_load_io_t io = {emit, delay};

static void raw_name(uint8_t raw[16], const char *name)
{
    const size_t length = strlen(name);
    assert(length <= 16U);
    memset(raw, 0xa0, 16U);
    memcpy(raw, name, length);
}

static void assert_released(const keyboard_t *keyboard)
{
    for (size_t index = 0U; index < 256U; ++index) {
        assert(!keyboard->held[index]);
    }
}

static void expect_command(const char *name, const char *expected)
{
    uint8_t raw[16];
    keyboard_t keyboard = {0};
    raw_name(raw, name);
    assert(mt_c64_native_load_name_supported(raw));
    assert(mt_c64_native_load(raw, &io, &keyboard) == MT_C64_NATIVE_LOAD_OK);
    assert(strcmp(keyboard.text, expected) == 0);
    assert(keyboard.text[keyboard.text_length - 1U] == '\r');
    assert_released(&keyboard);
    for (size_t index = 1U; index < keyboard.event_count; ++index) {
        assert(keyboard.events[index].at_ms -
               keyboard.events[index - 1U].at_ms >= 40U);
    }
    assert(keyboard.now_ms -
           keyboard.events[keyboard.event_count - 1U].at_ms >= 40U);
}

static void expect_rejected(const uint8_t raw[16])
{
    keyboard_t keyboard = {0};
    uint8_t output[MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES];
    uint8_t before[sizeof(output)];
    size_t output_size = 99U;

    memset(output, 0xcd, sizeof(output));
    memcpy(before, output, sizeof(before));
    assert(!mt_c64_native_load_name_supported(raw));
    assert(mt_c64_native_load(raw, &io, &keyboard) ==
           MT_C64_NATIVE_LOAD_INVALID_NAME);
    assert(keyboard.event_count == 0U && keyboard.delay_count == 0U);
    assert(mt_c64_native_load_build_basic_launcher(
               raw, output, sizeof(output), &output_size) ==
           MT_C64_NATIVE_LOAD_INVALID_NAME);
    assert(output_size == 0U);
    assert(memcmp(output, before, sizeof(output)) == 0);
}

static void test_validation(void)
{
    static const uint8_t rejected[] = {
        '"', '*', '?', ',', ':', '$', '#', '@',
        0x00U, 0x0dU, 0x13U, 0x5cU, 0x5eU, 0x5fU, 0x61U,
        0x7fU, 0x80U, 0xa0U, 0xc1U, 0xffU
    };
    uint8_t raw[16];
    keyboard_t keyboard = {0};
    mt_c64_native_load_io_t bad_io = io;

    memset(raw, 0xa0, sizeof(raw));
    expect_rejected(raw);
    for (size_t index = 0U; index < sizeof(rejected); ++index) {
        raw_name(raw, "ABCDEF");
        raw[2] = rejected[index];
        expect_rejected(raw);
        raw_name(raw, "ABCDEFGHIJKLMNOP");
        raw[15] = rejected[index];
        if (rejected[index] != 0xa0U) {
            expect_rejected(raw);
        }
    }
    raw_name(raw, "ULTIMA I");
    assert(!mt_c64_native_load_name_supported(NULL));
    assert(mt_c64_native_load(NULL, &io, &keyboard) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    assert(mt_c64_native_load(raw, NULL, &keyboard) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    bad_io.emit = NULL;
    assert(mt_c64_native_load(raw, &bad_io, &keyboard) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    bad_io = io;
    bad_io.delay = NULL;
    assert(mt_c64_native_load(raw, &bad_io, &keyboard) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    assert(keyboard.event_count == 0U && keyboard.delay_count == 0U);
}

static void test_callback_failures(void)
{
    uint8_t raw[16];
    keyboard_t successful = {0};

    raw_name(raw, "ULTIMA I (1)");
    assert(mt_c64_native_load(raw, &io, &successful) == MT_C64_NATIVE_LOAD_OK);
    /* Fail every make/break event, both before and after delivery. This
     * covers command keys, both quotes, shifted punctuation and Return. */
    for (size_t index = 1U; index <= successful.event_count; ++index) {
        for (unsigned int delivered = 0U; delivered < 2U; ++delivered) {
            keyboard_t failed = {0};
            failed.fail_event = index;
            failed.deliver_failed_event = delivered != 0U;
            assert(mt_c64_native_load(raw, &io, &failed) ==
                   MT_C64_NATIVE_LOAD_IO_ERROR);
            assert_released(&failed);
        }
    }
    for (size_t index = 1U; index <= successful.delay_count; ++index) {
        keyboard_t failed = {0};
        failed.fail_delay = index;
        assert(mt_c64_native_load(raw, &io, &failed) ==
               MT_C64_NATIVE_LOAD_IO_ERROR);
        assert_released(&failed);
    }
}

static void expect_basic_launcher(const char *name)
{
    uint8_t raw[16];
    uint8_t output[MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES + 3U];
    size_t output_size = 0U;
    const size_t length = strlen(name);
    size_t next_offset;
    uint16_t next_line;
    mt_c64_content_image_t image;

    raw_name(raw, name);
    memset(output, 0xcd, sizeof(output));
    /* Exact capacity must succeed; the bytes after the PRG stay untouched. */
    assert(mt_c64_native_load_build_basic_launcher(
               raw, output, 16U + length, &output_size) ==
           MT_C64_NATIVE_LOAD_OK);
    assert(output_size == 16U + length);
    assert(output[0] == 0x01U && output[1] == 0x08U);
    next_line = (uint16_t)((uint16_t)output[2] |
                          ((uint16_t)output[3] << 8));
    next_offset = (size_t)next_line - 0x0801U + 2U;
    assert(next_offset == output_size - 2U);
    assert(output[4] == 10U && output[5] == 0U);
    assert(output[6] == 0x93U); /* LOAD is one token, not ASCII letters. */
    assert(output[7] == '"');
    assert(memcmp(output + 8U, name, length) == 0);
    assert(memcmp(output + 8U + length, "\",8,1", 5U) == 0);
    assert(output[next_offset - 1U] == 0U); /* Line terminator. */
    assert(output[next_offset] == 0U && output[next_offset + 1U] == 0U);
    for (size_t index = output_size; index < sizeof(output); ++index) {
        assert(output[index] == 0xcdU);
    }
    image.data = output;
    image.size = output_size;
    assert(mt_c64_validate_prg(&image).status == MT_C64_CONTENT_OK);
    assert(mt_c64_prg_has_basic_program(&image));
}

static void test_basic_launcher_failures(void)
{
    uint8_t raw[16];
    uint8_t output[MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES];
    uint8_t before[sizeof(output)];
    size_t output_size;

    raw_name(raw, "ULTIMA II");
    memset(output, 0xcd, sizeof(output));
    memcpy(before, output, sizeof(before));
    for (size_t capacity = 0U; capacity < 25U; ++capacity) {
        output_size = 99U;
        assert(mt_c64_native_load_build_basic_launcher(
                   raw, output, capacity, &output_size) ==
               MT_C64_NATIVE_LOAD_BUFFER_TOO_SMALL);
        assert(output_size == 0U);
        assert(memcmp(output, before, sizeof(output)) == 0);
    }
    output_size = 99U;
    assert(mt_c64_native_load_build_basic_launcher(
               NULL, output, sizeof(output), &output_size) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    assert(output_size == 0U);
    assert(memcmp(output, before, sizeof(output)) == 0);
    output_size = 99U;
    assert(mt_c64_native_load_build_basic_launcher(
               raw, NULL, sizeof(output), &output_size) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    assert(output_size == 0U);
    assert(mt_c64_native_load_build_basic_launcher(
               raw, output, sizeof(output), NULL) ==
           MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
    assert(memcmp(output, before, sizeof(output)) == 0);
    assert(mt_c64_native_load_build_basic_launcher(
               NULL, NULL, 0U, NULL) == MT_C64_NATIVE_LOAD_INVALID_ARGUMENT);
}

static void test_basic_launcher(void)
{
    /* Exact original fixture: $0801, line 10 LOAD"ULTIMA II",8,1, next
     * link $0816, followed immediately by the final empty link. */
    static const uint8_t expected[] = {
        0x01U, 0x08U, 0x16U, 0x08U, 10U, 0U, 0x93U, '"',
        'U', 'L', 'T', 'I', 'M', 'A', ' ', 'I', 'I',
        '"', ',', '8', ',', '1', 0U, 0U, 0U
    };
    uint8_t output[MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES];
    size_t size = 0U;

    /* Raw-name/output aliasing is supported by the builder's local staging. */
    raw_name(output, "ULTIMA II");
    assert(mt_c64_native_load_build_basic_launcher(
               output, output, sizeof(output), &size) == MT_C64_NATIVE_LOAD_OK);
    assert(size == sizeof(expected));
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    expect_basic_launcher("ULTIMA I");
    expect_basic_launcher("ULTIMA II");
    expect_basic_launcher("0123456789ABCDEF");
    expect_basic_launcher("!%&'()+-./;=[]");
    expect_basic_launcher(" A ");
    test_basic_launcher_failures();
}

int main(void)
{
    expect_command("ULTIMA I", "LOAD\"ULTIMA I\",8,1\r");
    expect_command("0123456789", "LOAD\"0123456789\",8,1\r");
    expect_command("!%&'()+-./;=[]", "LOAD\"!%&'()+-./;=[]\",8,1\r");
    expect_command("ABCDEFGHIJKLMNOP", "LOAD\"ABCDEFGHIJKLMNOP\",8,1\r");
    expect_command("QRSTUVWXYZ", "LOAD\"QRSTUVWXYZ\",8,1\r");
    expect_command(" A ", "LOAD\" A \",8,1\r");
    test_validation();
    test_callback_failures();
    test_basic_launcher();
    return 0;
}
