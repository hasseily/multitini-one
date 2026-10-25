/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_IEC_DOS_H
#define MULTITINI_C64_IEC_DOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "multitini_clock.h"

#define MT_IEC_PATH 512
#define MT_IEC_NAME 256
#define MT_IEC_ENTRIES 256
#define MT_IEC_BUFFER 512
#define MT_IEC_DIRECTORY 16384
#define MT_IEC_MAX_FILE (16U * 1024U * 1024U)

/* Filesystem callbacks return Commodore DOS codes (0 on success). All calls
 * run on one foreground owner. list must not call back into storage. close
 * clears *handle iff it actually closed, including on a failed sync. commit
 * must preserve a complete recovery copy until target data is durable. */
typedef int (*mt_iec_list_fn)(void *, const char *, size_t, bool);
typedef struct {
    uint16_t date, time; uint8_t attributes;
    bool sectors;
    char label[17], id[6]; /* Optional CBM directory header. */
} mt_iec_metadata_t;
typedef struct
{
    void *context;
    int (*list)(void *, const char *, mt_iec_list_fn, void *);
    int (*open)(void *, const char *, const char *, void **, size_t *);
    int (*read)(void *, void *, uint8_t *, size_t, size_t *);
    int (*write)(void *, void *, const uint8_t *, size_t, size_t *);
    int (*close)(void *, void **);
    int (*remove)(void *, const char *);
    int (*rename)(void *, const char *, const char *);
    int (*mkdir)(void *, const char *);
    int (*commit)(void *, const char *, const char *, bool);
    bool (*protected_path)(void *, const char *);
    /* Optional random access for REL files. Writable temporary handles must
     * also permit reads; a failed seek must leave ownership with the caller. */
    int (*seek)(void *, void *, size_t);
    /* 0 read, 1 write, 2 allocate, 3 free. A directory with no sector medium
     * returns a DOS error. Allocate can return the next free track/sector. */
    int (*sector)(void *, const char *, unsigned, unsigned *, unsigned *, uint8_t[256]);
    /* Total partition bytes (host volume or enclosing disk image). */
    int (*capacity)(void *, const char *, uint64_t *);
    int (*metadata)(void *, const char *, mt_iec_metadata_t *);
    int (*attributes)(void *, const char *, uint8_t, uint8_t);
    int (*format)(void *, const char *directory, const char *name, const char *id);
    int (*header)(void *, const char *directory, const char *name, const char *id);
    bool (*clock)(void *, mt_clock_time_t *);
    /* Change the serial address for subsequent transactions. Failure must
     * leave the previously active address in place. */
    int (*device_number)(void *, unsigned);
} mt_iec_fs_t;

typedef struct
{
    char name[MT_IEC_NAME], alias[17];
    size_t size;
    bool directory;
    char type;
    uint8_t data_offset, record_size;
    mt_iec_metadata_t metadata;
} mt_iec_entry_t;
typedef struct
{
    void *handle;
    void *auxiliary; /* append source retained if its close fails */
    char target[MT_IEC_PATH], temporary[MT_IEC_PATH];
    uint8_t buffer[MT_IEC_BUFFER];
    size_t size, position, used, cursor;
    unsigned kind; /* 0 closed, 1 read, 2 write, 3 directory, 4 buffer, 5 REL */
    int error;
    bool replace, complete;
    bool writable, record_dirty, record_missing;
    uint8_t record_size, data_offset;
    size_t record_offset;
} mt_iec_channel_t;
typedef struct
{
    char root[MT_IEC_PATH], cwd[MT_IEC_PATH], name[17];
} mt_iec_partition_t;
typedef struct
{
    mt_iec_fs_t fs;
    char root[MT_IEC_PATH], cwd[MT_IEC_PATH], status[257];
    size_t status_cursor, status_size;
    mt_iec_channel_t channels[15];
    mt_iec_entry_t entries[MT_IEC_ENTRIES];
    size_t entry_count;
    uint8_t directory[MT_IEC_DIRECTORY];
    size_t directory_size;
    int directory_owner;
    uint32_t serial;
    uint32_t status_generation;
    mt_iec_partition_t partitions[256];
    unsigned current_partition, active_partition;
    unsigned current_device, configured_device;
    void *probe_handle;
    int64_t clock_offset;
    bool write_protect;
} mt_iec_dos_t;

int mt_iec_dos_init(mt_iec_dos_t *, const mt_iec_fs_t *, const char *root);
int mt_iec_dos_open(mt_iec_dos_t *, unsigned channel, const uint8_t *, size_t);
int mt_iec_dos_write(mt_iec_dos_t *, unsigned channel, const uint8_t *, size_t, bool eoi);
int mt_iec_dos_close(mt_iec_dos_t *, unsigned channel);
/* Shutdown commits only EOI-complete writes, aborts incomplete writes, and
 * preserves failed temporary/recovery files. Returns the first real error. */
int mt_iec_dos_quiesce(mt_iec_dos_t *);
int mt_iec_dos_reset(mt_iec_dos_t *);
int mt_iec_dos_command(mt_iec_dos_t *, const uint8_t *, size_t);
void mt_iec_dos_begin_talk(mt_iec_dos_t *, unsigned channel);
/* Peek is idempotent until consume. available=false is EOF, never a made-up
 * byte. A canceled transport request can safely discard its peeked result. */
int mt_iec_dos_peek(mt_iec_dos_t *, unsigned channel, uint8_t *, bool *eoi, bool *available);
void mt_iec_dos_consume(mt_iec_dos_t *, unsigned channel);
/* Transactional UCI window. Reading can prefetch storage, but never advances
 * the guest cursor; commit only the number actually consumed by the guest.
 * Maximum window is MT_IEC_BUFFER. EOF is true when the window reaches EOF. */
int mt_iec_dos_peek_many(mt_iec_dos_t *, unsigned channel, uint8_t *, size_t capacity, size_t *size,
                         bool *eof);
int mt_iec_dos_consume_many(mt_iec_dos_t *, unsigned channel, size_t count);
int mt_iec_dos_abort_channel(mt_iec_dos_t *, unsigned channel);
/* Resolve without opening or creating a file, including a not-yet-existing
 * final name. The returned name is rooted; no raw guest path reaches FatFs. */
int mt_iec_dos_resolve_name(mt_iec_dos_t *, unsigned channel, const uint8_t *, size_t,
                            char path[MT_IEC_PATH]);
int mt_iec_dos_iec_name(mt_iec_dos_t *, const char *path, char alias[17], uint8_t *type);
int mt_iec_dos_partition_add(mt_iec_dos_t *, unsigned, const char *, const char *);
int mt_iec_dos_partition_remove(mt_iec_dos_t *, unsigned);
void mt_iec_dos_error(mt_iec_dos_t *, int code);
bool mt_iec_dos_has_channels(const mt_iec_dos_t *);
/* Shared reserved recovery namespace, case insensitive. */
bool mt_iec_dos_recovery_name(const char *basename);
#endif
