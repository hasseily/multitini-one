/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX32_H
#define MULTITINI_VECTREX32_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The interpreter is a single instance. Callbacks execute synchronously on
 * its thread; they may service hardware but must not re-enter the interpreter.
 * A false callback result stops execution, including while inside a BASIC SUB.
 * Coordinates are signed PiTrex units: each Vectrex delta is multiplied by
 * (scale + 2). The normal field is approximately +/-17688 X, +/-21384 Y.
 * The frame callback waits for the requested cadence, flushes submitted lines
 * and returns two controllers: X,Y,button1,button2,button3,button4.
 */
typedef struct mt_v32_callbacks {
    void *context;
    int (*service)(void *context);
    int (*begin_frame)(void *context);
    int (*line)(void *context, int32_t x0, int32_t y0, int32_t x1,
                int32_t y1, uint8_t intensity, uint8_t pattern);
    int (*frame)(void *context, unsigned int hz, int controls[2][6]);
    int (*psg)(void *context, unsigned int reg, uint8_t value);
    void (*text)(void *context, const char *text);
    /* Names are validated relative paths without '..', drive or root prefix.
     * Backends must confine reads to the selected game directory. Open returns
     * an opaque handle. These legacy callbacks remain a read-only fallback. */
    void *(*open_read)(void *context, const char *name);
    int (*read_byte)(void *context, void *file); /* 0..255, -1 EOF, -2 error */
    void (*close)(void *context, void *file);
    /* Optional caller-supplied BIOS, addressed in the Vectrex CPU map. */
    int (*read_bios)(void *context, uint16_t address, uint8_t *data, size_t count);
    /* Modern file callbacks: 0 success, -1 failure; EOF returns 0/1/-1.
     * All names remain relative to the selected game directory. */
    void *(*file_open)(void *context, const char *name, const char *mode);
    int (*file_read)(void *context, void *file, void *buffer, size_t capacity, size_t *received);
    int (*file_write)(void *context, void *file, const void *buffer, size_t size, size_t *written);
    int (*file_seek)(void *context, void *file, int64_t offset, int origin);
    int64_t (*file_tell)(void *context, void *file);
    int (*file_eof)(void *context, void *file);
    int (*file_close)(void *context, void *file);
    int (*file_remove)(void *context, const char *name);
    int (*file_rename)(void *context, const char *old_name, const char *new_name);
    uint64_t (*monotonic_milliseconds)(void *context);
    int (*console_read_byte)(void *context); /* 0..255, -1 not ready, -2 EOF, -3 error */
    int (*console_write)(void *context, const void *data, size_t size); /* 0 success, -1 error, -2 cancelled */
    uint32_t (*random_seed)(void *context); /* Optional; otherwise use monotonic time, then 1. */
} mt_v32_callbacks;

typedef struct mt_v32_result {
    uint64_t statements;
    uint64_t frames;
    uint64_t lines;
    unsigned int error_line;
    int stopped;
    char error[256];
    int raster_font_fallback; /* BIOS unavailable; vector glyphs used for TextSprite/TextListSprite. */
} mt_v32_result;

/* Compile and run a caller-owned, NUL-terminated source buffer. Zero means
 * success or intentional callback stop. Nonzero means compile/runtime failure.
 * Service is called at least every 256 executed BASIC statements. */
int mt_v32_run(const char *source, const char *name,
               const mt_v32_callbacks *callbacks, mt_v32_result *result);
int mt_v32_valid_relative_path(const char *name);

#ifdef __cplusplus
}
#endif
#endif
