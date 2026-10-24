/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_IEC_DOS_H
#define MULTITINI_C64_IEC_DOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
} mt_iec_fs_t;

typedef struct
{
    char name[MT_IEC_NAME], alias[17];
    size_t size;
    bool directory;
    char type;
} mt_iec_entry_t;
typedef struct
{
    void *handle;
    void *auxiliary; /* append source retained if its close fails */
    char target[MT_IEC_PATH], temporary[MT_IEC_PATH];
    uint8_t buffer[MT_IEC_BUFFER];
    size_t size, position, used, cursor;
    unsigned kind; /* 0 closed, 1 read, 2 write, 3 directory */
    int error;
    bool replace, complete;
} mt_iec_channel_t;
typedef struct
{
    mt_iec_fs_t fs;
    char root[MT_IEC_PATH], cwd[MT_IEC_PATH], status[80];
    size_t status_cursor;
    mt_iec_channel_t channels[15];
    mt_iec_entry_t entries[MT_IEC_ENTRIES];
    size_t entry_count;
    uint8_t directory[MT_IEC_DIRECTORY];
    size_t directory_size;
    int directory_owner;
    uint32_t serial;
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
void mt_iec_dos_error(mt_iec_dos_t *, int code);
bool mt_iec_dos_has_channels(const mt_iec_dos_t *);
#endif
