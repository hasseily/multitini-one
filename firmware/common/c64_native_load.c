/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_native_load.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t scan;
    bool shift;
} native_key_t;

typedef struct {
    const mt_c64_native_load_io_t *io;
    void *context;
    uint8_t scan;
    bool key_pending;
    bool shift_pending;
} native_input_t;

#define NATIVE_SHIFT_SCAN UINT8_C(0x12)

/* Matrix mapping: third_party/c64/upstream/rtl/fpga64_keyboard.vhd.
 * This is the C64 mapping, not a PC text-keyboard layout. The upstream core
 * remaps physical Shift+6/7/9/0 to C64 Shift+7/6/8/9 respectively. */
static bool key_for_character(uint8_t character, native_key_t *key)
{
    static const uint8_t letters[] = {
        0x1cU, 0x32U, 0x21U, 0x23U, 0x24U, 0x2bU, 0x34U,
        0x33U, 0x43U, 0x3bU, 0x42U, 0x4bU, 0x3aU, 0x31U,
        0x44U, 0x4dU, 0x15U, 0x2dU, 0x1bU, 0x2cU, 0x3cU,
        0x2aU, 0x1dU, 0x22U, 0x35U, 0x1aU
    };
    static const uint8_t digits[] = {
        0x45U, 0x16U, 0x1eU, 0x26U, 0x25U,
        0x2eU, 0x36U, 0x3dU, 0x3eU, 0x46U
    };

    key->shift = false;
    if (character >= 'A' && character <= 'Z') {
        key->scan = letters[character - 'A'];
        return true;
    }
    if (character >= '0' && character <= '9') {
        key->scan = digits[character - '0'];
        return true;
    }
    switch (character) {
    case ' ': key->scan = 0x29U; break;
    case '+': key->scan = 0x55U; break;
    case '-': key->scan = 0x4eU; break;
    case '.': key->scan = 0x49U; break;
    case '/': key->scan = 0x4aU; break;
    case ';': key->scan = 0x52U; break;
    case '=': key->scan = 0x09U; break;
    case ',': key->scan = 0x41U; break; /* Command syntax only. */
    case '\r': key->scan = 0x5aU; break; /* Command syntax only. */
    case '!': key->scan = 0x16U; key->shift = true; break;
    case '"': key->scan = 0x1eU; key->shift = true; break;
    case '%': key->scan = 0x2eU; key->shift = true; break;
    case '&': key->scan = 0x3dU; key->shift = true; break;
    case '\'': key->scan = 0x36U; key->shift = true; break;
    case '(': key->scan = 0x46U; key->shift = true; break;
    case ')': key->scan = 0x45U; key->shift = true; break;
    case '[': key->scan = 0x4cU; key->shift = true; break;
    case ']': key->scan = 0x52U; key->shift = true; break;
    default: return false;
    }
    return true;
}

static size_t name_length(const uint8_t raw_name[16])
{
    size_t length = MT_C64_NATIVE_LOAD_NAME_BYTES;
    while (length != 0U && raw_name[length - 1U] == UINT8_C(0xa0)) {
        --length;
    }
    return length;
}

bool mt_c64_native_load_name_supported(const uint8_t raw_name[16])
{
    size_t length;
    native_key_t key;

    if (raw_name == NULL) {
        return false;
    }
    length = name_length(raw_name);
    if (length == 0U) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t character = raw_name[index];
        /* BASIC quotes do not escape the drive's filename grammar. Keep
         * command separators, directory/channel selectors and wildcards
         * out of the name rather than loading another directory entry. */
        if (character == '"' || character == ',' || character == '\r' ||
            !key_for_character(character, &key)) {
            return false;
        }
    }
    return true;
}

mt_c64_native_load_status_t mt_c64_native_load_build_basic_launcher(
    const uint8_t raw_name[16], uint8_t *output, size_t capacity,
    size_t *output_size)
{
    static const uint8_t tail[] = {'"', ',', '8', ',', '1', 0U, 0U, 0U};
    uint8_t launcher[MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES];
    size_t length;
    size_t size;
    uint16_t next_line;

    if (output_size != NULL) {
        *output_size = 0U;
    }
    if (raw_name == NULL || output == NULL || output_size == NULL) {
        return MT_C64_NATIVE_LOAD_INVALID_ARGUMENT;
    }
    if (!mt_c64_native_load_name_supported(raw_name)) {
        return MT_C64_NATIVE_LOAD_INVALID_NAME;
    }
    length = name_length(raw_name);
    size = 16U + length;
    if (capacity < size) {
        return MT_C64_NATIVE_LOAD_BUFFER_TOO_SMALL;
    }
    /* The two-byte file header is outside C64 memory. The last two bytes
     * are the zero link of the next line, at $0801 + size - 4. */
    next_line = (uint16_t)(UINT16_C(0x0801) + size - 4U);
    launcher[0] = 0x01U;
    launcher[1] = 0x08U;
    launcher[2] = (uint8_t)(next_line & UINT16_C(0x00ff));
    launcher[3] = (uint8_t)(next_line >> 8);
    launcher[4] = 10U;
    launcher[5] = 0U;
    /* Token $93 is LOAD in the pinned BASIC 901226-01 ROM. Its successful
     * in-program path in KERNAL 901227-03 ($E1B5-$E1BB) resets the BASIC
     * text pointer, relinks the loaded program, and restarts execution. */
    launcher[6] = 0x93U;
    launcher[7] = '"';
    memcpy(launcher + 8U, raw_name, length);
    memcpy(launcher + 8U + length, tail, sizeof(tail));
    /* Stage locally so raw_name may alias output and failures are atomic. */
    memcpy(output, launcher, size);
    *output_size = size;
    return MT_C64_NATIVE_LOAD_OK;
}

static int settle(native_input_t *input)
{
    return input->io->delay(input->context,
                            MT_C64_NATIVE_LOAD_KEY_DELAY_MS);
}

static void release_attempted_keys(native_input_t *input)
{
    if (input->key_pending) {
        (void)input->io->emit(input->context, input->scan, false);
        (void)settle(input);
    }
    if (input->shift_pending) {
        (void)input->io->emit(input->context, NATIVE_SHIFT_SCAN, false);
        (void)settle(input);
    }
}

static int type_key(native_input_t *input, const native_key_t *key)
{
    if (key->shift) {
        /* Mark a key before calling emit: a failed callback might still
         * have delivered the press, so cleanup must release it. */
        input->shift_pending = true;
        if (input->io->emit(input->context, NATIVE_SHIFT_SCAN, true) != 0 ||
            settle(input) != 0) {
            return -1;
        }
    }
    input->scan = key->scan;
    input->key_pending = true;
    if (input->io->emit(input->context, key->scan, true) != 0 ||
        settle(input) != 0 ||
        input->io->emit(input->context, key->scan, false) != 0) {
        return -1;
    }
    input->key_pending = false;
    if (settle(input) != 0) {
        return -1;
    }
    if (key->shift) {
        if (input->io->emit(input->context, NATIVE_SHIFT_SCAN, false) != 0) {
            return -1;
        }
        input->shift_pending = false;
        if (settle(input) != 0) {
            return -1;
        }
    }
    return 0;
}

mt_c64_native_load_status_t mt_c64_native_load(
    const uint8_t raw_name[16], const mt_c64_native_load_io_t *io,
    void *context)
{
    static const uint8_t prefix[] = "LOAD\"";
    static const uint8_t suffix[] = "\",8,1\r";
    native_key_t command[sizeof(prefix) - 1U +
                         MT_C64_NATIVE_LOAD_NAME_BYTES + sizeof(suffix) - 1U];
    size_t count = 0U;
    size_t length;
    native_input_t input = {io, context, 0U, false, false};

    if (raw_name == NULL || io == NULL || io->emit == NULL ||
        io->delay == NULL) {
        return MT_C64_NATIVE_LOAD_INVALID_ARGUMENT;
    }
    if (!mt_c64_native_load_name_supported(raw_name)) {
        return MT_C64_NATIVE_LOAD_INVALID_NAME;
    }
    length = name_length(raw_name);
    for (size_t index = 0U; index < sizeof(prefix) - 1U; ++index) {
        (void)key_for_character(prefix[index], &command[count++]);
    }
    for (size_t index = 0U; index < length; ++index) {
        (void)key_for_character(raw_name[index], &command[count++]);
    }
    for (size_t index = 0U; index < sizeof(suffix) - 1U; ++index) {
        (void)key_for_character(suffix[index], &command[count++]);
    }
    for (size_t index = 0U; index < count; ++index) {
        if (type_key(&input, &command[index]) != 0) {
            release_attempted_keys(&input);
            return MT_C64_NATIVE_LOAD_IO_ERROR;
        }
    }
    return MT_C64_NATIVE_LOAD_OK;
}
