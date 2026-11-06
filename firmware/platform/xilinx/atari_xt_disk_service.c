/* SPDX-License-Identifier: GPL-3.0-only */
#include "atari_xt_disk_service.h"
#include "multitini_atari_disk.h"
#include "multitini_atari_sio.h"
#include "multitini_atari_xt_interface.h"
#include "multitini_content.h"
#include "multitini_media_browser.h"
#include "sd_storage.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    mt_content_type_t type;
    union {
        mt_atari_atr_t atr;
        mt_atari_xex_t xex;
    } image;
    char path[2U * MT_MEDIA_BROWSER_PATH_MAX + 2U];
    char relative[MT_MEDIA_BROWSER_PATH_MAX + 1U];
} mounted_disk_t;

static mounted_disk_t mounted;
static mt_atari_sio_t sio;
/* A close error can leave a managed handle alive. Retain it for recovery
 * before another read, core switch, or USB storage handoff. */
static mt_sd_file_t *close_pending;

bool atari_xt_disk_available(const mt_core_link_t *link)
{
    return link != NULL && link->read32 != NULL && link->write32 != NULL &&
        link->read32(link->context, MT_REG_CORE_ID) == MT_ATARI_XT_CORE_ID &&
        (uint16_t)link->read32(link->context, MT_REG_ABI_VERSION) >= MT_ATARI_XT_SIO_MIN_ABI_MINOR &&
        (link->read32(link->context, MT_REG_ATARI_SIO_STATUS) & MT_ATARI_SIO_STATUS_SIGNATURE_MASK) ==
            MT_ATARI_SIO_STATUS_SIGNATURE;
}

const char *atari_xt_disk_name(void)
{
    return mounted.relative[0] != '\0' ? mounted.relative : NULL;
}

static bool close_disk_file(mt_sd_file_t *file)
{
    const mt_sd_result_t result = mt_sd_file_close(file);
    close_pending = mt_sd_file_tell(file) >= 0 ? file : NULL;
    return result.status == MT_SD_OK;
}

bool atari_xt_disk_quiesce(void)
{
    if (close_pending != NULL && !close_disk_file(close_pending)) return false;
    mt_atari_sio_reset(&sio);
    return true;
}

bool atari_xt_disk_eject(void)
{
    if (!atari_xt_disk_quiesce()) return false;
    memset(&mounted, 0, sizeof(mounted));
    return true;
}

static bool read_file(void *context, uint32_t offset, uint8_t *data, size_t length)
{
    mt_sd_file_t *const file = context;
    if (mt_sd_file_tell(file) != offset &&
        mt_sd_file_seek(file, offset, SEEK_SET).status != MT_SD_OK)
        return false;
    const mt_sd_result_t result = mt_sd_file_read(file, data, length);
    return result.status == MT_SD_OK && result.size == length;
}

const char *atari_xt_disk_mount(const mt_core_link_t *link, const char *media_root,
                               const char *relative)
{
    if (!atari_xt_disk_available(link)) return "Update the Atari FPGA core to use disks and XEX";
    if (close_pending != NULL && !close_disk_file(close_pending)) return "Could not close Atari media";
    mounted_disk_t next = {0};
    next.type = mt_content_classify_for_core(MT_ATARI_XT_CORE_ID, relative);
    if ((next.type != MT_CONTENT_ATARI_XT_ATR && next.type != MT_CONTENT_ATARI_XT_XEX) ||
        strlen(relative) >= sizeof(next.relative) ||
        !mt_media_browser_join(media_root, relative, next.path, sizeof(next.path)))
        return "Choose an ATR disk or XEX program";
    mt_sd_info_t info;
    mt_sd_result_t result = mt_sd_file_stat(next.path, &info);
    if (result.status != MT_SD_OK) return mt_sd_status_name(result.status);
    if (info.size > UINT32_MAX) return "Atari media file is too large";
    mt_sd_file_t *file = NULL;
    result = mt_sd_file_open(next.path, "rb", &file);
    if (result.status != MT_SD_OK) return mt_sd_status_name(result.status);
    const mt_atari_disk_result_t validated = next.type == MT_CONTENT_ATARI_XT_ATR ?
        mt_atari_atr_open((uint32_t)info.size, read_file, file, &next.image.atr) :
        mt_atari_xex_open((uint32_t)info.size, read_file, file, &next.image.xex);
    if (!close_disk_file(file)) return "Could not close Atari media";
    switch (validated) {
    case MT_ATARI_DISK_OK: break;
    case MT_ATARI_DISK_IO: return "Could not read Atari media";
    case MT_ATARI_DISK_UNSUPPORTED:
        return next.type == MT_CONTENT_ATARI_XT_ATR ? "Unsupported ATR disk geometry" :
            "XEX needs RAM or a layout unavailable to the loader";
    default:
        return next.type == MT_CONTENT_ATARI_XT_ATR ? "Invalid ATR disk image" :
            "Invalid XEX executable";
    }
    memcpy(next.relative, relative, strlen(relative) + 1U);
    mt_atari_sio_reset(&sio);
    mounted = next;
    return NULL;
}

static bool disk_geometry(void *context, uint8_t unit, mt_atari_sio_geometry_t *geometry)
{
    (void)context;
    if (unit != 1U || atari_xt_disk_name() == NULL) return false;
    *geometry = mounted.type == MT_CONTENT_ATARI_XT_ATR ?
        (mt_atari_sio_geometry_t){mounted.image.atr.sector_count, mounted.image.atr.sector_size} :
        (mt_atari_sio_geometry_t){mounted.image.xex.sector_count, MT_ATARI_BOOT_SECTOR_BYTES};
    return true;
}

static bool disk_read(void *context, uint8_t unit, uint16_t sector, uint8_t *data, uint16_t bytes)
{
    (void)context;
    if (unit != 1U || atari_xt_disk_name() == NULL ||
        (close_pending != NULL && !close_disk_file(close_pending))) return false;
    mt_sd_file_t *file = NULL;
    if (mt_sd_file_open(mounted.path, "rb", &file).status != MT_SD_OK) return false;
    bool ok;
    if (mounted.type == MT_CONTENT_ATARI_XT_ATR) {
        uint32_t offset;
        uint16_t length;
        ok = mt_atari_atr_sector(&mounted.image.atr, sector, &offset, &length) &&
            bytes == length && read_file(file, offset, data, length);
    } else {
        ok = bytes == MT_ATARI_BOOT_SECTOR_BYTES &&
            mt_atari_xex_read_sector(&mounted.image.xex, read_file, file, sector, data) ==
                MT_ATARI_DISK_OK;
    }
    return close_disk_file(file) && ok;
}

void atari_xt_disk_poll(const mt_core_link_t *link)
{
    static const mt_atari_sio_disk_t disk = {NULL, disk_geometry, disk_read};
    void *const context = link->context;
    const uint32_t status = link->read32(context, MT_REG_ATARI_SIO_STATUS);
    if ((status & (MT_ATARI_SIO_STATUS_SIGNATURE_MASK | MT_ATARI_SIO_STATUS_RESET_ACTIVE)) !=
        MT_ATARI_SIO_STATUS_SIGNATURE) {
        mt_atari_sio_reset(&sio);
        return;
    }
    if ((status & MT_ATARI_SIO_STATUS_TX_FAULT) != 0U) {
        link->write32(context, MT_REG_ATARI_SIO_STATUS, MT_ATARI_SIO_STATUS_TX_FAULT);
        mt_atari_sio_reset(&sio);
    }
    _Static_assert(MT_ATARI_SIO_BYTE == MT_ATARI_SIO_RX_KIND_TX &&
                   MT_ATARI_SIO_COMMAND_BEGIN == MT_ATARI_SIO_RX_KIND_ASSERT &&
                   MT_ATARI_SIO_COMMAND_END == MT_ATARI_SIO_RX_KIND_RELEASE &&
                   MT_ATARI_SIO_RESYNC == MT_ATARI_SIO_RX_KIND_RESYNC, "Atari SIO event ABI");
    for (unsigned budget = 0U; budget < 32U; ++budget) {
        const uint32_t event = link->read32(context, MT_REG_ATARI_SIO_RX);
        if ((event & MT_ATARI_SIO_RX_VALID) == 0U) break;
        mt_atari_sio_receive(&sio, &disk, (uint8_t)(event >> MT_ATARI_SIO_RX_EPOCH_SHIFT),
            (mt_atari_sio_event_t)((event >> MT_ATARI_SIO_RX_KIND_SHIFT) & 3U), (uint8_t)event);
    }
    for (unsigned budget = 0U; budget < 32U; ++budget) {
        if ((link->read32(context, MT_REG_ATARI_SIO_STATUS) & MT_ATARI_SIO_STATUS_TX_READY) == 0U)
            break;
        mt_atari_sio_reply_t reply;
        if (!mt_atari_sio_next_reply(&sio, &disk, &reply)) break;
        link->write32(context, MT_REG_ATARI_SIO_TX, ((uint32_t)reply.epoch << MT_ATARI_SIO_TX_EPOCH_SHIFT) |
            (reply.gap ? MT_ATARI_SIO_TX_GAP : 0U) | reply.byte);
    }
}
