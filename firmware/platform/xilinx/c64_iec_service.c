/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_iec_service.h"
#include "c64_uci_image.h"
#include "c64_uci_internal.h"
#include "ff.h"
#include "multitini_c64_iec_dos.h"
#include "multitini_c64_interface.h"
#include "sd_storage.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ABI25 mailbox. These offsets are machine registers, never physical IEC
 * pins. All filesystem work is on the foreground CPU0 storage owner. */
enum
{
    IEC_CAPS = MT_REG_C64_IEC_CAPS,
    IEC_CONTROL = MT_REG_C64_IEC_CONTROL,
    IEC_STATUS = MT_REG_C64_IEC_STATUS,
    IEC_RX = MT_REG_C64_IEC_RX,
    IEC_TX = MT_REG_C64_IEC_TX
};
enum
{
    EV_RESET = MT_C64_IEC_EVENT_RESET,
    EV_OPEN = MT_C64_IEC_EVENT_OPEN,
    EV_CLOSE = MT_C64_IEC_EVENT_CLOSE,
    EV_DATA = MT_C64_IEC_EVENT_DATA,
    EV_DATA_EOI = MT_C64_IEC_EVENT_DATA_EOI,
    EV_UNLISTEN = MT_C64_IEC_EVENT_UNLISTEN,
    EV_TALK = MT_C64_IEC_EVENT_TALK,
    EV_UNTALK = MT_C64_IEC_EVENT_UNTALK,
    EV_LISTEN = MT_C64_IEC_EVENT_LISTEN,
    EV_READ = MT_C64_IEC_EVENT_READ,
    EV_TX_ACK = MT_C64_IEC_EVENT_TX_ACK
};
static mt_iec_dos_t dos;
static uint16_t uci_channels;

static mt_core_link_t *link;
static c64_iec_protected_fn protected_callback;
static void *protected_context;
static bool initialized, enabled, quiescing;
static uint8_t command[MT_IEC_NAME];
static size_t command_size;
static unsigned input_channel;
static bool opening, input_error;
static bool tx_pending, offered;
static unsigned offered_channel;
static uint16_t offered_token;
static uint32_t tx_word;
static int storage_failure;
static char listing_path[MT_IEC_PATH], listing_label[17], listing_id[6];
static char failed_targets[15][MT_IEC_PATH];
static unsigned failed_target_count;
static mt_sd_file_t *exclusive_handle;
/* Shared synchronous recovery/commit workspace. Never allocated on the ARM
 * stack, and never used concurrently with a storage callback or USB export. */
static uint8_t commit_buffer[MT_IEC_MAX_FILE];
static char journals[MT_IEC_ENTRIES][MT_IEC_NAME];
static size_t journal_count;
static int list_error;
typedef struct {
    bool used;
    c64_uci_image_file_t *image;
    mt_sd_file_t *temporary, *guard;
} image_handle_t;
static image_handle_t image_handles[20];
static c64_uci_image_file_t *image_listing, *image_commit;
static char image_commit_target[MT_IEC_PATH], image_commit_temporary[MT_IEC_PATH];
static unsigned image_commit_phase;
static bool image_commit_replace;

mt_iec_dos_t *c64_iec_service_dos(void)
{
    return initialized && !storage_failure ? &dos : NULL;
}
mt_iec_dos_t *c64_iec_service_cleanup_dos(void)
{
    /* A reported storage failure must not hide handles that still need to
     * close. This accessor is only for cleanup of an existing UCI lease. */
    return initialized ? &dos : NULL;
}
void c64_iec_service_channel_release(unsigned channel)
{
    if (channel < 16)
        uci_channels &= (uint16_t)~(1U << channel);
}
void c64_iec_service_write_failure(int code)
{
    if (code)
        storage_failure = code;
}
bool c64_iec_service_channel_acquire(unsigned channel)
{
    if (!initialized || storage_failure || channel >= 16 || (uci_channels & (1U << channel)) ||
        ((offered || tx_pending) && offered_channel == channel) ||
        ((opening || command_size) && input_channel == channel))
        return false;
    uci_channels |= (uint16_t)(1U << channel);
    return true;
}

static int sd_code(mt_sd_result_t r, bool writing)
{
    if (r.status == MT_SD_OK)
        return 0;
    if (r.backend_status == FR_NO_FILE || r.backend_status == FR_NO_PATH)
        return 62;
    if (r.backend_status == FR_WRITE_PROTECTED)
        return 26;
    if (writing && r.backend_status == FR_DENIED)
        return 72;
    if (r.backend_status == 12 || r.backend_status == 19) return 30; /* Unsupported operation or invalid parameter. */
    switch (r.status)
    {
    case MT_SD_FILE_EXISTS:
        return 63;
    case MT_SD_FILE_BUSY:
    case MT_SD_FILE_LIMIT:
        return 70;
    case MT_SD_TOO_LARGE:
        return 72;
    case MT_SD_DIR_OPEN_FAILED:
    case MT_SD_DIR_READ_FAILED:
    case MT_SD_DIR_CLOSE_FAILED:
        return 71;
    case MT_SD_MOUNT_FAILED:
    case MT_SD_EXTERNAL_ACCESS:
        return 74;
    default:
        return writing ? 25 : 23;
    }
}
static bool protected_path(void *unused, const char *path)
{
    (void)unused;
    return protected_callback && protected_callback(protected_context, path);
}
static bool journal_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 6)
        return false;
    const char *s = name + n - 6;
    const char *t = ".mtiec";
    for (unsigned i = 0; i < 6; ++i)
    {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != t[i])
            return false;
    }
    return true;
}
static bool path_join(char *dst, const char *parent, const char *name)
{
    int n = snprintf(dst, MT_IEC_PATH, "%s%s%s", parent, parent[strlen(parent) - 1] == '/' ? "" : "/", name);
    return n > 0 && n < MT_IEC_PATH;
}
static image_handle_t *image_handle(void *handle)
{
    for (unsigned i = 0; i < sizeof image_handles / sizeof image_handles[0]; ++i)
        if (handle == &image_handles[i] && image_handles[i].used) return &image_handles[i];
    return NULL;
}
static image_handle_t *new_image_handle(void)
{
    for (unsigned i = 0; i < sizeof image_handles / sizeof image_handles[0]; ++i)
        if (!image_handles[i].used) {
            memset(&image_handles[i], 0, sizeof image_handles[i]);
            image_handles[i].used = true;
            return &image_handles[i];
        }
    return NULL;
}
/* CBM names are sixteen bytes. Staging files stay on the host filesystem,
 * beside the image, and retain the same globally unique reserved basename. */
static bool image_temporary(const char *path, char physical[MT_IEC_PATH], char image[MT_IEC_PATH])
{
    const char *member;
    if (!c64_uci_image_path(path, image, &member) || !*member) return false;
    const char *leaf = strrchr(member, '/'); leaf = leaf ? leaf + 1 : member;
    if (strncmp(leaf, ".mtiec-", 7)) return false;
    char parent[MT_IEC_PATH]; strcpy(parent, image);
    char *slash = strrchr(parent, '/');
    if (!slash) return false;
    *slash = 0;
    return path_join(physical, parent, leaf);
}
static int exclusive_target(const char *path, bool *exists)
{
    if (exclusive_handle)
        return 70;
    mt_sd_result_t r = mt_sd_file_open(path, "r+", &exclusive_handle);
    if (r.status == MT_SD_OPEN_FAILED && r.backend_status == FR_NO_FILE)
    {
        *exists = false;
        return 0;
    }
    if (r.status != MT_SD_OK)
        return sd_code(r, true);
    *exists = true;
    r = mt_sd_file_close(exclusive_handle);
    if (mt_sd_file_tell(exclusive_handle) < 0)
        exclusive_handle = NULL;
    return sd_code(r, true);
}
static int recover(const char *target, const char *journal)
{
    if (protected_path(NULL, target) || protected_path(NULL, journal))
        return 26;
    bool exists = false;
    int rc = exclusive_target(target, &exists);
    if (rc)
        return rc;
    rc = exclusive_target(journal, &exists);
    if (rc)
        return rc;
    return sd_code(mt_sd_storage_journal_recover(target, journal, commit_buffer, sizeof commit_buffer), true);
}
static int gather_journal(void *unused, const char *name, size_t size, bool directory)
{
    (void)unused;
    (void)size;
    if (directory || !journal_name(name))
        return 0;
    if (journal_count == MT_IEC_ENTRIES || strlen(name) >= MT_IEC_NAME)
    {
        list_error = 71;
        return 1;
    }
    strcpy(journals[journal_count++], name);
    return 0;
}
typedef struct
{
    mt_iec_list_fn callback;
    void *context;
    int error;
} list_context_t;
static int list_entry(void *ctx, const char *name, size_t size, bool directory)
{
    list_context_t *c = ctx;
    c->error = c->callback(c->context, name, size, directory);
    return c->error;
}
static int fs_list(void *unused, const char *path, mt_iec_list_fn callback, void *context)
{
    (void)unused;
    listing_path[0]=listing_label[0]=listing_id[0]=0;
    char image[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(path, image, &member)) {
        if (image_listing) return 70;
        mt_sd_result_t r = c64_uci_image_directory(path, &image_listing);
        int rc = sd_code(r, false);
        if (!rc) {
            rc=sd_code(c64_uci_image_header(image_listing,listing_label,listing_id),false);
            if(!rc)strcpy(listing_path,path);
        }
        while (!rc) {
            mt_sd_info_t info;
            r = c64_uci_image_next(image_listing, &info);
            if (r.status != MT_SD_OK || !r.size) { rc = sd_code(r, false); break; }
            if (info.attributes & 8) continue; /* Volume label is the listing header. */
            rc = callback(context, info.name, (size_t)info.size, (info.attributes & 16) != 0);
        }
        mt_sd_result_t closed = c64_uci_image_close(&image_listing);
        if (closed.status != MT_SD_OK) { storage_failure = sd_code(closed, false); return storage_failure; }
        return rc;
    }
    journal_count = 0;
    list_error = 0;
    int rc = sd_code(mt_sd_storage_list(path, gather_journal, NULL), false);
    if (rc || list_error)
        return rc ? rc : list_error;
    for (size_t i = 0; i < journal_count; ++i)
    {
        char journal[MT_IEC_PATH], target[MT_IEC_PATH];
        if (!path_join(journal, path, journals[i]))
            return 33;
        strcpy(target, journal);
        target[strlen(target) - 6] = 0;
        rc = recover(target, journal);
        if (rc)
            return rc;
    }
    list_context_t c = {callback, context, 0};
    rc = sd_code(mt_sd_storage_list(path, list_entry, &c), false);
    return rc ? rc : c.error;
}
static int fs_close(void *unused, void **handle);
static int fs_open(void *unused, const char *path, const char *mode, void **handle, size_t *size)
{
    (void)unused;
    *handle = NULL;
    *size = 0;
    if (protected_path(NULL, path))
        return mode[0] == 'r' ? 70 : 26;
    char image[MT_IEC_PATH], temporary[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(path, image, &member)) {
        image_handle_t *h = new_image_handle();
        if (!h) return 70;
        *handle = h;
        mt_sd_result_t r;
        if (image_temporary(path, temporary, image)) {
            r = mt_sd_file_open(image, "r", &h->guard);
            if (r.status == MT_SD_OK) r = mt_sd_file_open(temporary, mode, &h->temporary);
            if (r.status == MT_SD_OK && mode[0] == 'r') {
                r = mt_sd_file_seek(h->temporary, 0, 2);
                int64_t length = mt_sd_file_tell(h->temporary);
                if (r.status == MT_SD_OK && length >= 0 && length <= MT_IEC_MAX_FILE) {
                    *size = (size_t)length; r = mt_sd_file_seek(h->temporary, 0, 0);
                } else if (r.status == MT_SD_OK) r.status = MT_SD_TOO_LARGE;
            }
        } else if (mode[0] == 'r' && !strchr(mode, '+')) {
            r = c64_uci_image_open(path, 1, &h->image);
            if (r.status == MT_SD_OK) *size = r.size;
        } else r = (mt_sd_result_t){MT_SD_INVALID_ARGUMENT, FR_INVALID_PARAMETER, 0};
        if (r.status != MT_SD_OK) {
            int close = fs_close(NULL, handle);
            if (close) return close;
        }
        return sd_code(r, mode[0] != 'r');
    }
    mt_sd_file_t *file = NULL;
    mt_sd_result_t r = mt_sd_file_open(path, mode, &file);
    *handle = file;
    *size = 0;
    if (r.status != MT_SD_OK)
        return sd_code(r, mode[0] != 'r');
    if (mode[0] == 'r')
    {
        r = mt_sd_file_seek(file, 0, 2);
        int64_t n = mt_sd_file_tell(file);
        if (r.status == MT_SD_OK && n >= 0 && (uint64_t)n <= MT_IEC_MAX_FILE)
        {
            *size = (size_t)n;
            r = mt_sd_file_seek(file, 0, 0);
        }
        else if (r.status == MT_SD_OK)
        {
            r.status = MT_SD_TOO_LARGE;
        }
        if (r.status != MT_SD_OK)
        {
            mt_sd_result_t close = mt_sd_file_close(file);
            if (mt_sd_file_tell(file) < 0)
                *handle = NULL;
            return sd_code(close.status == MT_SD_OK ? r : close, false);
        }
    }
    return 0;
}
static int fs_read(void *unused, void *handle, uint8_t *data, size_t size, size_t *got)
{
    (void)unused;
    image_handle_t *h = image_handle(handle);
    mt_sd_result_t r = h && h->image ? c64_uci_image_read(h->image, data, size) :
                       mt_sd_file_read(h ? h->temporary : handle, data, size);
    *got = r.size;
    return sd_code(r, false);
}
static int fs_write(void *unused, void *handle, const uint8_t *data, size_t size, size_t *got)
{
    (void)unused;
    image_handle_t *h = image_handle(handle);
    mt_sd_result_t r = h && h->image ? c64_uci_image_write(h->image, data, size) :
                       mt_sd_file_write_buffered(h ? h->temporary : handle, data, size);
    *got = r.size;
    return sd_code(r, true);
}
static int fs_seek(void *unused, void *handle, size_t offset)
{
    (void)unused;
    image_handle_t *h = image_handle(handle);
    return sd_code(h && h->image ? c64_uci_image_seek(h->image, (uint32_t)offset) :
                   mt_sd_file_seek(h ? h->temporary : handle, (int64_t)offset, 0), false);
}
static int fs_close(void *unused, void **handle)
{
    (void)unused;
    image_handle_t *h = image_handle(*handle);
    if (h) {
        int rc = 0;
        if (h->image) rc = sd_code(c64_uci_image_close(&h->image), true);
        if (h->temporary) {
            mt_sd_result_t r = mt_sd_file_close(h->temporary);
            if (mt_sd_file_tell(h->temporary) < 0) h->temporary = NULL;
            if (!rc) rc = sd_code(r, true);
        }
        if (h->guard) {
            mt_sd_result_t r = mt_sd_file_close(h->guard);
            if (mt_sd_file_tell(h->guard) < 0) h->guard = NULL;
            if (!rc) rc = sd_code(r, true);
        }
        if (!h->image && !h->temporary && !h->guard) { h->used = false; *handle = NULL; }
        return rc;
    }
    mt_sd_file_t *file = *handle;
    mt_sd_result_t r = mt_sd_file_close(file);
    if (mt_sd_file_tell(file) < 0)
        *handle = NULL;
    return sd_code(r, true);
}
static int fs_remove(void *unused, const char *path)
{
    (void)unused;
    if (protected_path(NULL, path))
        return 26;
    char image[MT_IEC_PATH], temporary[MT_IEC_PATH]; const char *member;
    if (image_temporary(path, temporary, image)) return sd_code(mt_sd_file_remove(temporary), true);
    if (c64_uci_image_path(path, image, &member) && *member) return sd_code(c64_uci_image_remove(path), true);
    return sd_code(mt_sd_file_remove(path), true);
}
static int fs_rename(void *unused, const char *from, const char *to)
{
    (void)unused;
    if (protected_path(NULL, from) || protected_path(NULL, to))
        return 26;
    char image[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(from, image, &member) && *member) return sd_code(c64_uci_image_rename(from, to), true);
    return sd_code(mt_sd_file_rename(from, to), true);
}
static int fs_mkdir(void *unused, const char *path)
{
    (void)unused;
    if (protected_path(NULL, path))
        return 26;
    char image[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(path, image, &member) && *member) return sd_code(c64_uci_image_mkdir(path), true);
    return sd_code(mt_sd_storage_ensure_directory(path), true);
}
static void clear_image_commit(void)
{
    image_commit_phase = 0;
    image_commit_target[0] = image_commit_temporary[0] = 0;
}
static int finish_image_commit(void)
{
    if (!image_commit_phase) return 0;
    if (image_commit_phase == 4) {
        int rc = sd_code(c64_uci_image_abort(&image_commit), true);
        if (rc) return rc;
        /* Preparation never reached the disk. Preserve the complete host
         * temporary for recovery, but do not wedge reset on a full image. */
        clear_image_commit();
        return 0;
    }
    if (image_commit_phase == 1) {
        mt_sd_result_t r = mt_sd_storage_read_file_max(image_commit_temporary, commit_buffer, sizeof commit_buffer);
        if (r.status != MT_SD_OK) {
            clear_image_commit();
            return sd_code(r, false);
        }
        size_t size = r.size;
        r = c64_uci_image_open(image_commit_target, image_commit_replace ? 10 : 6, &image_commit);
        if (r.status == MT_SD_OK) {
            r = c64_uci_image_write(image_commit, commit_buffer, size);
            if (r.status == MT_SD_OK && r.size != size) r.status = MT_SD_WRITE_FAILED;
        }
        if (r.status != MT_SD_OK) {
            image_commit_phase = 4;
            mt_sd_result_t closed = c64_uci_image_abort(&image_commit);
            if (!image_commit) clear_image_commit();
            return sd_code(closed.status == MT_SD_OK ? r : closed, true);
        }
        image_commit_phase = 2;
    }
    if (image_commit_phase == 2) {
        int rc = sd_code(c64_uci_image_close(&image_commit), true);
        if (rc) {
            /* Preparation failure leaves the disk unchanged and the complete
             * source on SD. A started journal retains its owner and retries. */
            if (!image_commit) clear_image_commit();
            return rc;
        }
        image_commit_phase = 3;
    }
    mt_sd_result_t removed = mt_sd_file_remove(image_commit_temporary);
    if (removed.status != MT_SD_OK && removed.backend_status != FR_NO_FILE) return sd_code(removed, true);
    clear_image_commit();
    return 0;
}
static int commit_impl(void *unused, const char *temporary, const char *target, bool replace)
{
    (void)unused;
    char image[MT_IEC_PATH], physical[MT_IEC_PATH];
    if (image_temporary(temporary, physical, image)) {
        if (image_commit_phase) return 70;
        if (protected_path(NULL, target) || protected_path(NULL, physical) || protected_path(NULL, image)) return 26;
        strcpy(image_commit_target, target); strcpy(image_commit_temporary, physical);
        image_commit_replace = replace; image_commit_phase = 1;
        return finish_image_commit();
    }
    char journal[MT_IEC_PATH];
    int n = snprintf(journal, sizeof journal, "%s.mtiec", target);
    if (n < 0 || n >= (int)sizeof journal)
        return 33;
    if (protected_path(NULL, target) || protected_path(NULL, temporary) || protected_path(NULL, journal))
        return 26;
    bool exists = false;
    int rc = exclusive_target(target, &exists);
    if (rc)
        return rc;
    if (exists && !replace)
        return 63;
    rc = exclusive_target(journal, &exists);
    if (rc)
        return rc;
    /* Recover a prior complete transaction before creating a new journal. */
    rc = sd_code(mt_sd_storage_journal_recover(target, journal, commit_buffer, sizeof commit_buffer), true);
    if (rc)
        return rc;
    mt_sd_result_t r = mt_sd_storage_read_file_max(temporary, commit_buffer, sizeof commit_buffer);
    if (r.status != MT_SD_OK)
        return sd_code(r, false);
    rc = sd_code(mt_sd_storage_journal_replace(target, journal, commit_buffer, r.size), true);
    if (rc)
        return rc;
    return sd_code(mt_sd_file_remove(temporary), true);
}
static int fs_commit(void *unused, const char *temporary, const char *target, bool replace)
{
    int rc = commit_impl(unused, temporary, target, replace);
    if (rc)
    {
        char image[MT_IEC_PATH]; const char *member;
        if (c64_uci_image_path(target, image, &member) && *member) return rc;
        bool found = false;
        for (unsigned i = 0; i < failed_target_count; ++i)
            if (!strcmp(failed_targets[i], target))
                found = true;
        if (!found && failed_target_count < 15)
            strcpy(failed_targets[failed_target_count++], target);
    }
    return rc;
}
static int fs_sector(void *unused, const char *path, unsigned operation, unsigned *track,
                      unsigned *sector, uint8_t data[256])
{
    (void)unused;
    if (operation && protected_path(NULL, path)) return 26;
    int rc = c64_uci_image_sector(path, operation, track, sector, data);
    if (rc == 25) storage_failure = rc;
    return rc;
}
static int fs_capacity(void *unused, const char *path, uint64_t *bytes)
{
    (void)unused;
    char image[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(path, image, &member)) {
        mt_sd_info_t info;
        mt_sd_result_t r = mt_sd_file_stat(image, &info);
        if (r.status == MT_SD_OK) *bytes = info.size;
        return sd_code(r, false);
    }
    return sd_code(mt_sd_storage_capacity(path, bytes), false);
}
static int fs_metadata(void *unused, const char *path, mt_iec_metadata_t *metadata)
{
    (void)unused;
    memset(metadata,0,sizeof *metadata);
    if(!strcmp(path,listing_path) && listing_label[0]) {
        strcpy(metadata->label,listing_label);strcpy(metadata->id,listing_id);metadata->sectors=true;return 0;
    }
    char image[MT_IEC_PATH]; const char *member; mt_sd_info_t info;
    bool inside=c64_uci_image_path(path,image,&member);
    mt_sd_result_t r = inside && *member ?
                       c64_uci_image_stat(path,&info) : mt_sd_file_stat(path,&info);
    if (r.status == MT_SD_OK) {
        metadata->date=info.date;metadata->time=info.time;metadata->attributes=info.attributes;metadata->sectors=inside;
    }
    return sd_code(r,false);
}
static int fs_attributes(void *unused, const char *path, uint8_t attributes, uint8_t mask)
{
    (void)unused;
    if (protected_path(NULL,path)) return 26;
    char image[MT_IEC_PATH]; const char *member;
    bool inside=c64_uci_image_path(path,image,&member);
    if (mask & 0x80) {
        if (!inside) return 30;
        return sd_code(mt_sd_file_attributes(image,attributes,mask & 0x7f),true);
    }
    return sd_code(inside && *member ? c64_uci_image_attributes(path,attributes,mask) :
                   mt_sd_file_attributes(path,attributes,mask),true);
}
static int fs_format(void *unused,const char *directory,const char *name,const char *id)
{
    (void)unused;
    char image[MT_IEC_PATH], path[MT_IEC_PATH]; const char *member;
    if (c64_uci_image_path(directory,image,&member)) {
        if (protected_path(NULL,image)) return 26;
        int rc=sd_code(c64_uci_image_format(directory,name,id),true);
        if (!rc) {
            for (unsigned i=1;i<256;++i) {
                char *cwd=dos.partitions[i].cwd; size_t n=strlen(image);
                if (!strncmp(cwd,image,n) && cwd[n]=='/') strcpy(cwd,image);
            }
            if (dos.current_partition) strcpy(dos.cwd,dos.partitions[dos.current_partition].cwd);
        }
        return rc;
    }
    char file[32],label[17];
    (void)snprintf(file,sizeof file,"%.20s",name);
    char *extension=strrchr(file,'.'); bool explicit_type=extension &&
        (!strcmp(extension,".D64") || !strcmp(extension,".D41") || !strcmp(extension,".D71") ||
         !strcmp(extension,".D81") || !strcmp(extension,".DNP"));
    bool native=extension && !strcmp(extension,".DNP");
    size_t id_length=strlen(id);
    if (native) {
        if (id_length && (id_length!=3 || id[0]<'0' || id[0]>'9' ||
                          id[1]<'0' || id[1]>'9' || id[2]<'0' || id[2]>'9')) return 30;
    } else if (id_length && (id_length<2 || id_length>3)) return 30;
    if (!explicit_type) { (void)snprintf(file,sizeof file,"%.16s.D64",name); extension=strrchr(file,'.'); }
    if (!strcmp(extension,".D41")) memcpy(extension,".D64",5);
    size_t base=(size_t)(extension-file); if(base>16)base=16;
    memcpy(label,file,base);label[base]=0;
    int n=snprintf(path,sizeof path,"%s/%s",directory,file);
    if(n<0 || n>=(int)sizeof path)return 33;
    if(protected_path(NULL,path))return 26;
    mt_sd_info_t info;mt_sd_result_t exists=mt_sd_file_stat(path,&info);
    if(exists.status==MT_SD_OK && (!explicit_type || native))return 63;
    if(exists.status!=MT_SD_OK && exists.backend_status!=FR_NO_FILE && exists.backend_status!=FR_NO_PATH)return sd_code(exists,false);
    if(exists.status!=MT_SD_OK && !*id)return 30;
    if(native && !strcmp(id,"000"))return 62;
    return sd_code(c64_uci_image_format(path,label,id),true);
}
static int fs_header(void *unused,const char *directory,const char *name,const char *id)
{
    (void)unused;char image[MT_IEC_PATH];const char *member;
    if(!c64_uci_image_path(directory,image,&member))return 30;
    if(protected_path(NULL,image))return 26;
    return sd_code(c64_uci_image_label(directory,name,id),true);
}
static bool fs_clock(void *unused,mt_clock_time_t *time)
{
    (void)unused;c64_uci_callbacks_t *callbacks=&c64_uci_environment.callbacks;
    return callbacks->get_time && callbacks->get_time(callbacks->context,time);
}
static int fs_device_number(void *unused,unsigned device)
{
    (void)unused;return c64_iec_service_set_device_number(device);
}
static mt_iec_fs_t filesystem(void)
{
    mt_iec_fs_t fs = {NULL,      fs_list,   fs_open,  fs_read,   fs_write,      fs_close,
                      fs_remove, fs_rename, fs_mkdir, fs_commit, protected_path, fs_seek, fs_sector, fs_capacity,
                      fs_metadata, fs_attributes, fs_format, fs_header, fs_clock, fs_device_number};
    return fs;
}
bool c64_iec_service_available(void)
{
    if (!initialized || !link || !link->read32 || !link->write32)
        return false;
    if (link->read32(link->context, MT_REG_CORE_ID) != MT_C64_CORE_ID)
        return false;
    uint32_t abi = link->read32(link->context, MT_REG_ABI_VERSION);
    return (abi >> 16) == MT_ABI_MAJOR && (uint16_t)abi >= MT_C64_IEC_MIN_ABI &&
           (link->read32(link->context, IEC_CAPS) & ~MT_C64_IEC_CAPS_ADDRESS) == MT_C64_IEC_CAPS_DEVICE9;
}
unsigned c64_iec_service_device_number(void)
{
    return initialized ? dos.current_device : 9;
}
int c64_iec_service_set_device_number(unsigned device)
{
    if (device < 8 || device > 30) return 30;
    if (!c64_iec_service_available()) return 74;
    if (!(link->read32(link->context,IEC_CAPS) & MT_C64_IEC_CAPS_ADDRESS)) {
        if (device != 9) return 30;
    } else {
        uint32_t value=link->read32(link->context,MT_REG_C64_PERIPHERAL_CONTROL);
        value=(value & ~MT_C64_PERIPHERAL_IEC_ADDRESS_MASK) |
              ((uint32_t)device << MT_C64_PERIPHERAL_IEC_ADDRESS_SHIFT);
        link->write32(link->context,MT_REG_C64_PERIPHERAL_CONTROL,value);
    }
    dos.current_device=device;
    c64_uci_service_refresh_control();
    return 0;
}
static void transport_reset(void)
{
    command_size = 0;
    input_channel = 0;
    opening = false;
    input_error = false;
    tx_pending = false;
    offered = false;
}
int c64_iec_service_init(mt_core_link_t *new_link, const char *root, c64_iec_protected_fn fn, void *context)
{
    if (initialized && (enabled || mt_iec_dos_has_channels(&dos) || exclusive_handle || image_listing || image_commit_phase || storage_failure))
        return 70;
    mt_iec_fs_t fs = filesystem();
    int rc = mt_iec_dos_init(&dos, &fs, root);
    if (rc)
        return rc;
    link = new_link;
    protected_callback = fn;
    protected_context = context;
    initialized = true;
    enabled = false;
    quiescing = false;
    storage_failure = 0;
    uci_channels = 0;
    failed_target_count = 0;
    transport_reset();
    if (c64_iec_service_available()) {
        link->write32(link->context, IEC_CONTROL, 0);
        (void)c64_iec_service_set_device_number(dos.configured_device);
    }
    return 0;
}
int c64_iec_service_configure_root(const char *root)
{
    if (!initialized)
        return 74;
    if (enabled || mt_iec_dos_has_channels(&dos) || image_listing || image_commit_phase || storage_failure)
        return 70;
    mt_iec_fs_t fs = filesystem();
    int rc=mt_iec_dos_init(&dos, &fs, root);
    if (!rc && c64_iec_service_available()) rc=c64_iec_service_set_device_number(dos.configured_device);
    return rc;
}
int c64_iec_service_enable(bool on)
{
    if (!on)
        return c64_iec_service_quiesce();
    if (!c64_iec_service_available() || storage_failure)
        return storage_failure ? storage_failure : 74;
    int rc = sd_code(mt_sd_storage_mount(), false);
    if (rc)
        return rc;
    quiescing = false;
    link->write32(link->context, IEC_CONTROL, 1);
    enabled = true;
    return 0;
}
static int finish_input(void)
{
    if ((uci_channels & (1U << input_channel)) || (input_channel == 15 && uci_channels)) {
        command_size = 0;
        opening = false;
        input_error = false;
        mt_iec_dos_error(&dos, 70);
        return 70;
    }
    int rc = 0;
    if (input_error)
    {
        rc = 33;
        mt_iec_dos_error(&dos, rc);
    }
    else if (opening)
    {
        rc = storage_failure && input_channel != 15 ? storage_failure
                                                    : mt_iec_dos_open(&dos, input_channel, command, command_size);
        if (rc)
            mt_iec_dos_error(&dos, rc);
    }
    else if (input_channel == 15 && command_size)
    {
        rc = mt_iec_dos_command(&dos, command, command_size);
    }
    command_size = 0;
    opening = false;
    input_error = false;
    return rc;
}
static void request_byte(unsigned channel, uint16_t token, bool next)
{
    if (!next)
        mt_iec_dos_begin_talk(&dos, channel);
    uint8_t byte = 0;
    bool eoi = false, available = false;
    int rc = (uci_channels & (1U << channel))
                 ? 70
                 : mt_iec_dos_peek(&dos, channel, &byte, &eoi, &available);
    tx_word = (uint32_t)token << 16;
    if (rc || !available)
        tx_word |= 1U << 9;
    else
    {
        tx_word |= byte;
        if (eoi)
            tx_word |= 1U << 8;
    }
    tx_pending = true;
    offered_channel = channel;
}
static uint32_t activity_reads, activity_writes;
void c64_iec_service_activity(uint32_t *reads, uint32_t *writes)
{
    if (reads != NULL)
        *reads = activity_reads;
    if (writes != NULL)
        *writes = activity_writes;
}
static int event(uint32_t value)
{
    unsigned type = (value >> 12) & 15, channel = (value >> 8) & 15;
    uint8_t byte = (uint8_t)value;
    int rc = 0;
    bool uci_blocked =
        ((type == EV_DATA || type == EV_DATA_EOI) && (uci_channels & (1U << input_channel))) ||
        (type == EV_CLOSE && ((uci_channels & (1U << channel)) || (channel == 15 && uci_channels)));
    bool mutating = ((type == EV_DATA || type == EV_DATA_EOI) && !opening && input_channel < 15 &&
                     (dos.channels[input_channel].kind == 2 || (dos.channels[input_channel].kind == 5 && dos.channels[input_channel].writable))) ||
                    (type == EV_CLOSE && channel < 15 && (dos.channels[channel].kind == 2 ||
                     (dos.channels[channel].kind == 5 && dos.channels[channel].writable)));
    switch (type)
    {
    case EV_RESET: /* Enable also emits RESET: preserve cwd across menu resume. */
        transport_reset();
        break;
    case EV_OPEN:
    case EV_LISTEN:
        (void)finish_input();
        input_channel = channel;
        opening = type == EV_OPEN;
        command_size = 0;
        input_error = false;
        break;
    case EV_DATA:
    case EV_DATA_EOI:
        ++activity_writes;
        if (opening || input_channel == 15)
        {
            if (command_size < sizeof command - 1)
                command[command_size++] = byte;
            else
                input_error = true;
            if (type == EV_DATA_EOI)
                rc = finish_input();
        }
        else
        {
            rc = (uci_channels & (1U << input_channel))
                     ? 70
                     : mt_iec_dos_write(&dos, input_channel, &byte, 1, type == EV_DATA_EOI);
        }
        break;
    case EV_UNLISTEN:
        rc = finish_input();
        break;
    case EV_CLOSE:
        rc = ((uci_channels & (1U << channel)) || (channel == 15 && uci_channels))
                 ? 70
                 : mt_iec_dos_close(&dos, channel);
        if (offered_channel == channel)
            offered = false;
        break;
    case EV_TALK:
        request_byte(channel, (uint16_t)(value >> 16), false);
        break;
    case EV_READ:
        request_byte(channel, (uint16_t)(value >> 16), true);
        break;
    case EV_TX_ACK:
        ++activity_reads;
        if (offered && offered_channel == channel && offered_token == (uint16_t)(value >> 16))
        {
            mt_iec_dos_consume(&dos, channel);
            offered = false;
        }
        break;
    case EV_UNTALK:
        tx_pending = false;
        offered = false;
        break;
    default:
        rc = 30;
        mt_iec_dos_error(&dos, rc);
        break;
    }
    if ((rc == 23 || rc == 25 || rc == 72 || rc == 74) && mutating && !uci_blocked)
        storage_failure = rc;
    return rc;
}
int c64_iec_service_poll(unsigned budget)
{
    if (!enabled || !c64_iec_service_available())
        return -1;
    unsigned processed = 0;
    while (processed < budget)
    {
        uint32_t status = link->read32(link->context, IEC_STATUS);
        if (status & (1U << 4))
            return -1;
        if (tx_pending)
        {
            if (status & 2)
            {
                link->write32(link->context, IEC_TX, tx_word);
                offered = (tx_word & (1U << 9)) == 0;
                offered_token = (uint16_t)(tx_word >> 16);
                tx_pending = false;
            }
            else if (!(status & 1))
                break;
        }
        if (!(status & 1))
            break;
        (void)event(link->read32(link->context, IEC_RX));
        ++processed;
    }
    /* A read request processed in the final budget slot need not wait for
     * the next outer service poll to make its response visible. */
    if (tx_pending)
    {
        uint32_t s = link->read32(link->context, IEC_STATUS);
        if (s & 2)
        {
            link->write32(link->context, IEC_TX, tx_word);
            offered = (tx_word & (1U << 9)) == 0;
            offered_token = (uint16_t)(tx_word >> 16);
            tx_pending = false;
        }
    }
    return (int)processed;
}
int c64_iec_service_quiesce(void)
{
    if (uci_channels)
        return 70; /* Caller must quiesce UCI before device9. */
    if (!initialized)
        return 0;
    if (enabled && c64_iec_service_available())
    {
        quiescing = true;
        link->write32(link->context, IEC_CONTROL, 3);
        bool drained = false;
        for (unsigned i = 0; i < 100000; ++i)
        {
            if (c64_iec_service_poll(32) < 0)
                return 74;
            if (link->read32(link->context, IEC_STATUS) & (1U << 5))
            {
                drained = true;
                break;
            }
        }
        if (!drained)
            return 74;
        link->write32(link->context, IEC_CONTROL, 0);
    }
    enabled = false;
    quiescing = false;
    tx_pending = false;
    offered = false;
    int rc = mt_iec_dos_quiesce(&dos);
    if (image_listing) {
        int close = sd_code(c64_uci_image_close(&image_listing), false);
        if (!rc) rc = close;
    }
    if (image_commit_phase) {
        int retry = finish_image_commit();
        if (!rc) rc = retry;
    }
    if (c64_uci_image_retry() && !rc) rc = 25;
    if (exclusive_handle)
    {
        mt_sd_result_t close = mt_sd_file_close(exclusive_handle);
        if (mt_sd_file_tell(exclusive_handle) < 0)
            exclusive_handle = NULL;
        if (!rc)
            rc = sd_code(close, true);
    }
    if (rc)
        storage_failure = rc;
    return storage_failure ? storage_failure : rc;
}
int c64_iec_service_reset(void)
{
    int rc = c64_iec_service_quiesce();
    if (rc)
    {
        if (enabled || mt_iec_dos_has_channels(&dos) || exclusive_handle || image_listing || image_commit_phase || !storage_failure)
            return rc;
        /* Explicit recovery after a reported failure. Partial temporary files
         * remain hidden; only checksum-validated complete journals replay. */
        for (unsigned i = 0; i < failed_target_count; ++i)
        {
            char journal[MT_IEC_PATH];
            int n = snprintf(journal, sizeof journal, "%s.mtiec", failed_targets[i]);
            if (n < 0 || n >= (int)sizeof journal)
                return 33;
            int recovered = recover(failed_targets[i], journal);
            if (recovered)
                return recovered;
        }
        storage_failure = 0;
        failed_target_count = 0;
    }
    rc = mt_iec_dos_reset(&dos);
    if (!rc)
        transport_reset();
    return rc;
}
bool c64_iec_service_active(void)
{
    return enabled;
}
const char *c64_iec_service_cwd(void)
{
    return initialized ? dos.cwd : "";
}
const char *c64_iec_service_status(void)
{
    return initialized ? dos.status : "74,DRIVE NOT READY,00,00\r";
}
