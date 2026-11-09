/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_spectrum_tzx.h"

#include <stdbool.h>
#include <string.h>

/* TZX revision 1.20, https://worldofspectrum.net/TZXformat.html. */
enum {
    TZX_TONE = 0x01, TZX_PULSES = 0x02, TZX_PWM = 0x03, TZX_PAUSE = 0x04,
    TZX_DIRECT = 0x05, TZX_LEVEL = 0x06, TZX_STOP_48K = 0x07,
};

typedef struct {
    const uint8_t *next;
    size_t remaining;
    uint8_t *output;
    size_t capacity, written;
    size_t loop_start;
    uint16_t repetitions;
    bool group_open;
} tzx_compiler_t;

static uint16_t read_word(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8U);
}

static uint32_t read_size(const uint8_t *bytes)
{
    return read_word(bytes) | (uint32_t)bytes[2] << 16U;
}

static const uint8_t *take(tzx_compiler_t *tape, size_t count)
{
    if (count > tape->remaining) return NULL;
    const uint8_t *const bytes = tape->next;
    tape->next += count;
    tape->remaining -= count;
    return bytes;
}

static bool emit(tzx_compiler_t *tape, const uint8_t *bytes, size_t count)
{
    if (count > tape->capacity - tape->written) return false;
    memcpy(tape->output + tape->written, bytes, count);
    tape->written += count;
    return true;
}

static bool emit_tone(tzx_compiler_t *tape, uint16_t duration, uint16_t count)
{
    const uint8_t command[] = {
        TZX_TONE, (uint8_t)duration, (uint8_t)(duration >> 8U),
        (uint8_t)count, (uint8_t)(count >> 8U),
    };
    return emit(tape, command, sizeof(command));
}

static bool emit_pause(tzx_compiler_t *tape, uint16_t milliseconds)
{
    const uint8_t command[] = {
        TZX_PAUSE, (uint8_t)milliseconds, (uint8_t)(milliseconds >> 8U),
    };
    return emit(tape, command, sizeof(command));
}

static bool emit_data(tzx_compiler_t *tape, uint8_t type,
                      uint16_t first, uint16_t second, uint8_t last_bits,
                      const uint8_t *payload, uint32_t length, uint16_t pause)
{
    uint8_t command[9U] = {type, (uint8_t)first, (uint8_t)(first >> 8U)};
    size_t header = 3U;
    if (type == TZX_PWM) {
        command[header++] = (uint8_t)second;
        command[header++] = (uint8_t)(second >> 8U);
    }
    command[header++] = last_bits;
    command[header++] = (uint8_t)length;
    command[header++] = (uint8_t)(length >> 8U);
    command[header++] = (uint8_t)(length >> 16U);
    return emit(tape, command, header) && emit(tape, payload, length) &&
           (pause == 0U || emit_pause(tape, pause));
}

static mt_spectrum_tzx_status_t compile_standard(tzx_compiler_t *tape)
{
    const uint8_t *const header = take(tape, 4U);
    if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    const uint16_t length = read_word(header + 2U);
    const uint8_t *const payload = take(tape, length);
    if (payload == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    if (length == 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;

    /* 667 and 735 T-states for the two sync pulses. */
    const uint8_t sync[] = {TZX_PULSES, 2U, 0x9bU, 0x02U, 0xdfU, 0x02U};
    return emit_tone(tape, 2168U, payload[0] < 128U ? 8063U : 3223U) &&
           emit(tape, sync, sizeof(sync)) &&
           emit_data(tape, TZX_PWM, 855U, 1710U, 8U, payload, length, read_word(header))
        ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
}

static mt_spectrum_tzx_status_t compile_turbo(tzx_compiler_t *tape)
{
    const uint8_t *const header = take(tape, 18U);
    if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    const uint32_t length = read_size(header + 15U);
    const uint8_t *const payload = take(tape, length);
    if (payload == NULL) return MT_SPECTRUM_TZX_TRUNCATED;

    const uint16_t pilot = read_word(header);
    const uint16_t pilot_count = read_word(header + 10U);
    const uint16_t zero = read_word(header + 6U), one = read_word(header + 8U);
    const uint8_t last_bits = header[12];
    if (length == 0U || last_bits == 0U || last_bits > 8U ||
        zero == 0U || one == 0U || (pilot_count != 0U && pilot == 0U))
        return MT_SPECTRUM_TZX_INVALID_BLOCK;

    uint8_t sync[] = {TZX_PULSES, 0U, 0U, 0U, 0U, 0U};
    size_t sync_size = 2U;
    for (size_t offset = 2U; offset <= 4U; offset += 2U) {
        if (read_word(header + offset) == 0U) continue;
        sync[sync_size++] = header[offset];
        sync[sync_size++] = header[offset + 1U];
        ++sync[1];
    }
    return (pilot_count == 0U || emit_tone(tape, pilot, pilot_count)) &&
           (sync[1] == 0U || emit(tape, sync, sync_size)) &&
           emit_data(tape, TZX_PWM, zero, one, last_bits, payload, length, read_word(header + 13U))
        ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
}

static mt_spectrum_tzx_status_t compile_data(tzx_compiler_t *tape, bool direct)
{
    const uint8_t *const header = take(tape, direct ? 8U : 10U);
    if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    const uint32_t length = read_size(header + (direct ? 5U : 7U));
    const uint8_t *const payload = take(tape, length);
    if (payload == NULL) return MT_SPECTRUM_TZX_TRUNCATED;

    const uint16_t first = read_word(header);
    const uint16_t second = direct ? 0U : read_word(header + 2U);
    const uint8_t last_bits = header[4];
    const uint16_t pause = read_word(header + (direct ? 2U : 5U));
    if (length == 0U || last_bits == 0U || last_bits > 8U ||
        first == 0U || (!direct && second == 0U))
        return MT_SPECTRUM_TZX_INVALID_BLOCK;
    return emit_data(tape, direct ? TZX_DIRECT : TZX_PWM,
                     first, second, last_bits, payload, length, pause)
        ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
}

static mt_spectrum_tzx_status_t compile_pulses(tzx_compiler_t *tape)
{
    const uint8_t *const count = take(tape, 1U);
    if (count == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    const size_t length = (size_t)*count * 2U;
    const uint8_t *const pulses = take(tape, length);
    if (pulses == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
    if (*count == 0U) return MT_SPECTRUM_TZX_OK;
    for (size_t offset = 0U; offset < length; offset += 2U)
        if (read_word(pulses + offset) == 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
    const uint8_t command[] = {TZX_PULSES, *count};
    return emit(tape, command, sizeof(command)) && emit(tape, pulses, length)
        ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
}

static mt_spectrum_tzx_status_t finish_loop(tzx_compiler_t *tape)
{
    if (tape->repetitions == 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
    const size_t length = tape->written - tape->loop_start;
    const size_t extra = (size_t)tape->repetitions - 1U;
    tape->repetitions = 0U;
    if (length == 0U) return MT_SPECTRUM_TZX_OK;
    if (extra > (tape->capacity - tape->written) / length)
        return MT_SPECTRUM_TZX_TOO_LARGE;
    for (size_t copy = 0U; copy < extra; ++copy) {
        memcpy(tape->output + tape->written, tape->output + tape->loop_start, length);
        tape->written += length;
    }
    return MT_SPECTRUM_TZX_OK;
}

static mt_spectrum_tzx_status_t skip_bytes(tzx_compiler_t *tape, size_t length)
{
    return take(tape, length) == NULL ? MT_SPECTRUM_TZX_TRUNCATED : MT_SPECTRUM_TZX_OK;
}

static mt_spectrum_tzx_status_t compile_block(tzx_compiler_t *tape, uint8_t type)
{
    const uint8_t *header;
    switch (type) {
    case 0x10: return compile_standard(tape);
    case 0x11: return compile_turbo(tape);
    case 0x12:
        header = take(tape, 4U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (read_word(header + 2U) == 0U) return MT_SPECTRUM_TZX_OK;
        if (read_word(header) == 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        return emit_tone(tape, read_word(header), read_word(header + 2U))
            ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
    case 0x13: return compile_pulses(tape);
    case 0x14: return compile_data(tape, false);
    case 0x15: return compile_data(tape, true);
    case 0x18: return MT_SPECTRUM_TZX_UNSUPPORTED_CSW;
    case 0x19: return MT_SPECTRUM_TZX_UNSUPPORTED_GENERALIZED;
    case 0x20:
        header = take(tape, 2U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        return emit_pause(tape, read_word(header))
            ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
    case 0x21: /* Group start */
        if (tape->group_open) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        tape->group_open = true;
        /* Fall through to the length-prefixed description. */
        /* fall through */
    case 0x30:
        header = take(tape, 1U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        return skip_bytes(tape, header[0]);
    case 0x22:
        if (!tape->group_open) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        tape->group_open = false;
        return MT_SPECTRUM_TZX_OK;
    case 0x23:
    case 0x26:
    case 0x27:
    case 0x28:
        return MT_SPECTRUM_TZX_UNSUPPORTED_NAVIGATION;
    case 0x24:
        header = take(tape, 2U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (tape->repetitions != 0U || read_word(header) < 2U)
            return MT_SPECTRUM_TZX_INVALID_BLOCK;
        tape->repetitions = read_word(header);
        tape->loop_start = tape->written;
        return MT_SPECTRUM_TZX_OK;
    case 0x25: return finish_loop(tape);
    case 0x2a: {
        header = take(tape, 4U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (read_size(header) != 0U || header[3] != 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        const uint8_t command = TZX_STOP_48K;
        return emit(tape, &command, 1U) ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
    }
    case 0x2b: {
        header = take(tape, 4U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (read_size(header) != 1U || header[3] != 0U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        const uint8_t *const level = take(tape, 1U);
        if (level == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (*level > 1U) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        const uint8_t command[] = {TZX_LEVEL, *level};
        return emit(tape, command, sizeof(command)) ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_TOO_LARGE;
    }
    case 0x31: /* Message */
    case 0x32: /* Archive information */
        header = take(tape, 2U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        return skip_bytes(tape, type == 0x31U ? header[1] : read_word(header));
    case 0x33:
        header = take(tape, 1U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        return skip_bytes(tape, (size_t)header[0] * 3U);
    case 0x35:
        header = take(tape, 20U); /* 16-byte ID and 32-bit payload length. */
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        return skip_bytes(tape, read_size(header + 16U) | (uint32_t)header[19] << 24U);
    case 0x5a:
        header = take(tape, 9U);
        if (header == NULL) return MT_SPECTRUM_TZX_TRUNCATED;
        if (memcmp(header, "XTape!\x1a", 7U) != 0) return MT_SPECTRUM_TZX_INVALID_BLOCK;
        return header[7] == 1U ? MT_SPECTRUM_TZX_OK : MT_SPECTRUM_TZX_UNSUPPORTED_VERSION;
    default:
        return MT_SPECTRUM_TZX_UNSUPPORTED_BLOCK;
    }
}

mt_spectrum_tzx_status_t mt_spectrum_tzx_compile(
    const uint8_t *input, size_t size, uint8_t *output, size_t capacity,
    size_t *output_size)
{
    if (output_size == NULL) return MT_SPECTRUM_TZX_BAD_ARGUMENT;
    *output_size = 0U;
    if (input == NULL || output == NULL || input == output)
        return MT_SPECTRUM_TZX_BAD_ARGUMENT;
    if (size < 10U || memcmp(input, "ZXTape!\x1a", 8U) != 0)
        return MT_SPECTRUM_TZX_BAD_HEADER;
    if (input[8] != 1U) return MT_SPECTRUM_TZX_UNSUPPORTED_VERSION;

    tzx_compiler_t tape = {
        .next = input + 10U, .remaining = size - 10U, .output = output,
        .capacity = capacity < MT_SPECTRUM_TZX_MAX_BYTES ? capacity : MT_SPECTRUM_TZX_MAX_BYTES,
    };
    while (tape.remaining != 0U) {
        const uint8_t type = *take(&tape, 1U);
        const mt_spectrum_tzx_status_t status = compile_block(&tape, type);
        if (status != MT_SPECTRUM_TZX_OK) return status;
    }
    if (tape.repetitions != 0U || tape.group_open) return MT_SPECTRUM_TZX_INVALID_BLOCK;
    if (tape.written == 0U) return MT_SPECTRUM_TZX_EMPTY;
    *output_size = tape.written;
    return MT_SPECTRUM_TZX_OK;
}

const char *mt_spectrum_tzx_status_name(mt_spectrum_tzx_status_t status)
{
    switch (status) {
    case MT_SPECTRUM_TZX_OK: return "TZX ready";
    case MT_SPECTRUM_TZX_BAD_ARGUMENT: return "Invalid TZX buffer";
    case MT_SPECTRUM_TZX_BAD_HEADER: return "Invalid TZX file header";
    case MT_SPECTRUM_TZX_UNSUPPORTED_VERSION: return "Unsupported TZX major version";
    case MT_SPECTRUM_TZX_TRUNCATED: return "Truncated TZX block";
    case MT_SPECTRUM_TZX_INVALID_BLOCK: return "Invalid TZX block";
    case MT_SPECTRUM_TZX_UNSUPPORTED_BLOCK: return "Unsupported TZX block type";
    case MT_SPECTRUM_TZX_UNSUPPORTED_CSW: return "TZX CSW recordings are unsupported";
    case MT_SPECTRUM_TZX_UNSUPPORTED_GENERALIZED: return "TZX generalized data is unsupported";
    case MT_SPECTRUM_TZX_UNSUPPORTED_NAVIGATION: return "TZX jumps, calls, and selections are unsupported";
    case MT_SPECTRUM_TZX_TOO_LARGE: return "TZX playback data exceeds the tape buffer";
    case MT_SPECTRUM_TZX_EMPTY: return "TZX contains no playable blocks";
    default: return "TZX compile failed";
    }
}
