/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_NATIVE_LOAD_H
#define MULTITINI_C64_NATIVE_LOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_C64_NATIVE_LOAD_NAME_BYTES 16U
#define MT_C64_NATIVE_LOAD_KEY_DELAY_MS 50U
#define MT_C64_NATIVE_LOAD_BASIC_LAUNCHER_MAX_BYTES 32U

typedef enum {
    MT_C64_NATIVE_LOAD_OK = 0,
    MT_C64_NATIVE_LOAD_INVALID_ARGUMENT,
    MT_C64_NATIVE_LOAD_INVALID_NAME,
    MT_C64_NATIVE_LOAD_IO_ERROR,
    MT_C64_NATIVE_LOAD_BUFFER_TOO_SMALL
} mt_c64_native_load_status_t;

typedef struct {
    /* Direct, nonextended PS/2 Set 2 events, after any USB layout mapping.
     * Callbacks return zero on success. A delay may service media requests,
     * but must keep USB/menu key events from interleaving this command. */
    int (*emit)(void *context, uint8_t scan, bool pressed);
    int (*delay)(void *context, uint32_t milliseconds);
} mt_c64_native_load_io_t;

/* Accept the raw directory name, not its display-text conversion. Only
 * trailing PETSCII $A0 padding is removed; spaces remain significant.
 * Quotes, wildcards, DOS metacharacters and unsupported PETSCII are rejected.
 * In particular, this deliberately supports uppercase letters, digits,
 * spaces and !%&'()+-./;=[]; it never substitutes or approximates a name. */
bool mt_c64_native_load_name_supported(
    const uint8_t raw_name[MT_C64_NATIVE_LOAD_NAME_BYTES]);

/* Build an original tokenized BASIC PRG at $0801: 10 LOAD"<name>",8,1.
 * Running this launcher lets BASIC's in-program LOAD path start the newly
 * loaded BASIC program itself, without timing or queuing a second RUN.
 * Use only for a target known to contain a BASIC program; native autostart
 * targets retain the direct keyboard LOAD below.
 * The caller stages this PRG, restores drive 8, and then starts it.
 * No output bytes change on failure; *output_size becomes zero whenever
 * output_size is nonnull. output_size must not overlap output or raw_name. */
mt_c64_native_load_status_t mt_c64_native_load_build_basic_launcher(
    const uint8_t raw_name[MT_C64_NATIVE_LOAD_NAME_BYTES],
    uint8_t *output, size_t capacity, size_t *output_size);

/* Validate first, then type LOAD"<exact name>",8,1 and Return. No RUN or SYS
 * follows: native autostart files must retain their own LOAD behavior.
 * The caller owns reset, BASIC readiness, a mounted drive 8, and input/menu
 * suppression. All keys and modifiers must be released before this call.
 * On callback failure, release attempted keys/modifiers on a best-effort
 * basis. A partial command may remain on screen; the caller must recover
 * before retrying. */
mt_c64_native_load_status_t mt_c64_native_load(
    const uint8_t raw_name[MT_C64_NATIVE_LOAD_NAME_BYTES],
    const mt_c64_native_load_io_t *io, void *context);

#endif
