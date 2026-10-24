/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_DEBUG_H
#define MULTITINI_C64_DEBUG_H
#include "multitini_c64_features.h"
#include "multitini_c64_reu_host.h"

/* All file and aperture callbacks use ordinary bytes. Aperture callbacks
 * must use the startup-established Normal non-cacheable mapping, never a
 * cacheable alias or cache-range maintenance. apply_turbo recomputes the
 * stored configuration and calls effective_overlay before writing it. */
typedef struct {
    void *context;
    uint32_t (*read32)(void *, uint32_t);
    void (*write32)(void *, uint32_t, uint32_t);
    uint64_t (*milliseconds)(void *);
    void (*progress)(void *);
    void (*pause)(void *, bool);
    bool (*paused)(void *);
    void (*output)(void *, const char *);
    void (*apply_turbo)(void *, bool apply_change);
    bool (*cartridge)(void *);
    bool (*content_busy)(void *);
    bool (*reu_enabled)(void *);
    bool (*file_open)(void *, const char *, size_t *);
    bool (*file_read)(void *, size_t, void *, size_t);
    void (*file_close)(void *);
    bool (*file_write)(void *, const char *, const void *, size_t);
    bool (*reu_read)(void *, uint32_t, void *, size_t);
    bool (*reu_write)(void *, uint32_t, const void *, size_t);
    void (*drain_writes)(void *);
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
} mt_c64_debug_ops_t;

typedef struct {
    mt_c64_debug_ops_t ops;
    mt_c64_capabilities_t caps;
    mt_c64_reu_host_t reu_host;
    bool active, session, armed, override_active, c64u_default, trace;
    uint8_t override_index, variant;
    uint32_t debug_bits, last_config, last_status;
    uint64_t trace_time;
    uint8_t *capture;
    size_t capture_frames, capture_target;
    uint64_t capture_started;
    bool capture_overflow;
    unsigned capture_peak, tone_count, tones[8];
    char capture_path[128];
} mt_c64_debug_t;

void mt_c64_debug_init(mt_c64_debug_t *, const mt_c64_debug_ops_t *, bool c64u_default);
/* Call deactivate before replacing PL, activate only after the new image's
 * ABI/capabilities have been qualified. A core reload clears reload_required. */
void mt_c64_debug_activate(mt_c64_debug_t *, const mt_c64_capabilities_t *, unsigned variant);
void mt_c64_debug_deactivate(mt_c64_debug_t *);
/* Return false only for an unrelated existing console command. Recovery
 * console must never dispatch here. A leading ':' is optional. */
bool mt_c64_debug_command(mt_c64_debug_t *, const char *line);
mt_c64_reu_load_result_t mt_c64_debug_load_reu(mt_c64_debug_t *, const char *path, uint32_t offset);
void mt_c64_debug_tick(mt_c64_debug_t *);
void mt_c64_debug_clear_override(mt_c64_debug_t *);
uint32_t mt_c64_debug_effective_overlay(const mt_c64_debug_t *, uint32_t stored_config,
    uint32_t previous_config, bool apply_change);
void mt_c64_debug_wav_header(uint8_t header[44], uint32_t frames);
double mt_c64_debug_goertzel(const uint8_t *stereo_le, size_t frames, double frequency);
#endif
