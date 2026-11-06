/* SPDX-License-Identifier: GPL-3.0-only */
#include "atari_xt_disk_service.h"
#include "multitini_atari_xt_interface.h"
#include "sd_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_ROOT "0:/multitini/cores/atari-800xl/media"
#define ATR_NAME "nested/boot.ATR"
#define XEX_NAME "run.XEX"
#define BAD_NAME "bad.atr"
#define ATR_DATA_OFFSET 400U

typedef struct {
    uint32_t core_id, abi, status;
    uint32_t rx[7], tx[260];
    size_t rx_index, rx_count, tx_count, tx_credit;
    unsigned sio_accesses;
} shell_fixture_t;

typedef struct {
    const uint8_t *data;
    size_t size;
} image_t;

struct mt_sd_file {
    image_t image;
    size_t position;
    bool active;
};

static uint8_t atr[ATR_DATA_OFFSET + 256U];
static const uint8_t xex[] = {0xffU, 0xffU, 0U, 0x20U, 0U, 0x20U, 0x60U};
static const uint8_t invalid_atr[16] = {0};
static shell_fixture_t shell;
static mt_sd_file_t handle;
static unsigned file_opens, file_stats;
static bool fail_close, check_ack_on_open;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static image_t find_image(const char *path)
{
    if (strcmp(path, MEDIA_ROOT "/" ATR_NAME) == 0) return (image_t){atr, sizeof(atr)};
    if (strcmp(path, MEDIA_ROOT "/" XEX_NAME) == 0) return (image_t){xex, sizeof(xex)};
    if (strcmp(path, MEDIA_ROOT "/" BAD_NAME) == 0) return (image_t){invalid_atr, sizeof(invalid_atr)};
    return (image_t){0};
}

static bool mounted_is(const char *expected)
{
    const char *const name = atari_xt_disk_name();
    return name != NULL && strcmp(name, expected) == 0;
}

mt_sd_result_t mt_sd_file_stat(const char *path, mt_sd_info_t *info)
{
    ++file_stats;
    const image_t image = find_image(path);
    info->size = image.size;
    return (mt_sd_result_t){.status = image.data != NULL ? MT_SD_OK : MT_SD_OPEN_FAILED};
}

mt_sd_result_t mt_sd_file_open(const char *path, const char *mode, mt_sd_file_t **file)
{
    check(strcmp(mode, "rb") == 0 && !handle.active, "only one read-only FAT handle is active");
    if (check_ack_on_open) {
        check(shell.tx_count == 1U && (uint8_t)shell.tx[0] == 'A', "FPGA receives ACK before FAT opens");
        check_ack_on_open = false;
    }
    const image_t image = find_image(path);
    ++file_opens;
    if (image.data == NULL) return (mt_sd_result_t){.status = MT_SD_OPEN_FAILED};
    handle = (mt_sd_file_t){.image = image, .active = true};
    *file = &handle;
    return (mt_sd_result_t){.status = MT_SD_OK};
}

int64_t mt_sd_file_tell(const mt_sd_file_t *file)
{
    return file == &handle && file->active ? (int64_t)file->position : -1;
}

mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *file, int64_t offset, int origin)
{
    check(file == &handle && file->active && origin == SEEK_SET && offset >= 0,
          "disk reads use absolute offsets on an active file");
    if ((uint64_t)offset > file->image.size) return (mt_sd_result_t){.status = MT_SD_SEEK_FAILED};
    file->position = (size_t)offset;
    return (mt_sd_result_t){.status = MT_SD_OK};
}

mt_sd_result_t mt_sd_file_read(mt_sd_file_t *file, void *data, size_t capacity)
{
    check(file == &handle && file->active, "FAT reads require an active handle");
    const size_t remaining = file->image.size - file->position;
    const size_t size = capacity < remaining ? capacity : remaining;
    memcpy(data, file->image.data + file->position, size);
    file->position += size;
    return (mt_sd_result_t){.status = MT_SD_OK, .size = size};
}

mt_sd_result_t mt_sd_file_close(mt_sd_file_t *file)
{
    check(file == &handle && file->active, "FAT close uses the active handle");
    if (fail_close) return (mt_sd_result_t){.status = MT_SD_CLOSE_FAILED};
    file->active = false;
    return (mt_sd_result_t){.status = MT_SD_OK};
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    (void)status;
    return "Mock SD failure";
}

static uint32_t read32(void *context, uint32_t offset)
{
    shell_fixture_t *mock = context;
    switch (offset) {
    case MT_REG_CORE_ID: return mock->core_id;
    case MT_REG_ABI_VERSION: return mock->abi;
    case MT_REG_ATARI_SIO_STATUS:
        ++mock->sio_accesses;
        return mock->status | (mock->tx_credit != 0U ? MT_ATARI_SIO_STATUS_TX_READY : 0U);
    case MT_REG_ATARI_SIO_RX:
        ++mock->sio_accesses;
        return mock->rx_index < mock->rx_count ? mock->rx[mock->rx_index++] : 0U;
    default:
        check(false, "unexpected FPGA register read");
        return 0U;
    }
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_fixture_t *mock = context;
    check(offset == MT_REG_ATARI_SIO_TX && mock->tx_credit != 0U &&
          mock->tx_count < sizeof(mock->tx) / sizeof(mock->tx[0]), "TX obeys FIFO credit");
    ++mock->sio_accesses;
    mock->tx[mock->tx_count++] = value;
    --mock->tx_credit;
}

static uint32_t event(uint8_t epoch, uint32_t kind, uint8_t data)
{
    return MT_ATARI_SIO_RX_VALID | (uint32_t)epoch << MT_ATARI_SIO_RX_EPOCH_SHIFT |
        kind << MT_ATARI_SIO_RX_KIND_SHIFT | data;
}

static void prepare_read(uint8_t epoch, uint8_t sector)
{
    shell.rx_index = shell.tx_count = 0U;
    shell.rx_count = 7U;
    shell.rx[0] = event(epoch, MT_ATARI_SIO_RX_KIND_ASSERT, 0U);
    shell.rx[1] = event(epoch, MT_ATARI_SIO_RX_KIND_TX, 0x31U);
    shell.rx[2] = event(epoch, MT_ATARI_SIO_RX_KIND_TX, 'R');
    shell.rx[3] = event(epoch, MT_ATARI_SIO_RX_KIND_TX, sector);
    shell.rx[4] = event(epoch, MT_ATARI_SIO_RX_KIND_TX, 0U);
    shell.rx[5] = event(epoch, MT_ATARI_SIO_RX_KIND_TX, (uint8_t)(0x31U + 'R' + sector));
    shell.rx[6] = event(epoch, MT_ATARI_SIO_RX_KIND_RELEASE, 0U);
    shell.tx_credit = 1U;
}

static void poll_read(const mt_core_link_t *link, uint8_t epoch, uint8_t sector, size_t length)
{
    prepare_read(epoch, sector);
    const unsigned before = file_opens;
    check_ack_on_open = true;
    atari_xt_disk_poll(link);
    check(shell.tx_count == 1U && file_opens == before, "full TX FIFO defers FAT after ACK");
    shell.tx_credit = sizeof(shell.tx) / sizeof(shell.tx[0]);
    for (unsigned poll = 0U; poll < 10U && shell.tx_count < length + 3U; ++poll)
        atari_xt_disk_poll(link);
    check(shell.tx_count == length + 3U && file_opens == before + 1U && !handle.active,
          "sector reply drains across polls and releases the FAT handle");
    check(shell.tx[0] == ((uint32_t)epoch << MT_ATARI_SIO_TX_EPOCH_SHIFT | 'A') &&
          shell.tx[1] == ((uint32_t)epoch << MT_ATARI_SIO_TX_EPOCH_SHIFT | MT_ATARI_SIO_TX_GAP | 'C'),
          "ACK and gapped completion preserve the FPGA epoch");
}

int main(void)
{
    mt_core_link_t link;
    mt_core_link_init(&link, &shell, read32, write32);
    shell.core_id = MT_ATARI_XT_CORE_ID;
    shell.abi = (MT_ABI_MAJOR << 16U) | (MT_ATARI_XT_SIO_MIN_ABI_MINOR - 1U);
    check(!atari_xt_disk_available(&link) && atari_xt_disk_mount(&link, MEDIA_ROOT, ATR_NAME) != NULL &&
          atari_xt_disk_quiesce() && atari_xt_disk_eject() && shell.sio_accesses == 0U &&
          file_opens == 0U && file_stats == 0U,
          "legacy FPGA rejects disk mounting without SIO MMIO or FAT, and permits handoff");

    shell.abi = (MT_ABI_MAJOR << 16U) | MT_ATARI_XT_SIO_MIN_ABI_MINOR;
    check(!atari_xt_disk_available(&link), "new ABI alone is insufficient without the SIO signature");
    shell.status = MT_ATARI_SIO_STATUS_SIGNATURE;
    check(atari_xt_disk_available(&link), "matching SIO signature enables Atari disks");
    atr[0] = 0x96U;
    atr[1] = 0x02U;
    atr[2] = 40U;
    atr[5] = 1U;
    for (size_t i = 0U; i < 256U; ++i) atr[ATR_DATA_OFFSET + i] = (uint8_t)(i ^ 0x5aU);
    check(atari_xt_disk_mount(&link, MEDIA_ROOT, ATR_NAME) == NULL && !handle.active &&
          mounted_is(ATR_NAME), "nested ATR mounts read-only and closes after validation");
    check(atari_xt_disk_mount(&link, MEDIA_ROOT, BAD_NAME) != NULL && !handle.active &&
          mounted_is(ATR_NAME), "invalid replacement preserves the mounted ATR");
    poll_read(&link, 0x98U, 4U, 256U);
    for (size_t i = 0U; i < 256U; ++i)
        check((uint8_t)shell.tx[2U + i] == atr[ATR_DATA_OFFSET + i], "ATR sector reads correct FAT offset and data");

    prepare_read(0x99U, 4U);
    const unsigned before_reset = file_opens;
    atari_xt_disk_poll(&link);
    check(shell.tx_count == 1U, "pending read acknowledged before reset");
    shell.status |= MT_ATARI_SIO_STATUS_RESET_ACTIVE;
    atari_xt_disk_poll(&link);
    shell.status &= ~MT_ATARI_SIO_STATUS_RESET_ACTIVE;
    shell.tx_credit = 1U;
    atari_xt_disk_poll(&link);
    check(shell.tx_count == 1U && file_opens == before_reset &&
          mounted_is(ATR_NAME), "reset discards the queued read and preserves the disk");

    fail_close = true;
    check(atari_xt_disk_mount(&link, MEDIA_ROOT, XEX_NAME) != NULL && handle.active &&
          mounted_is(ATR_NAME) && !atari_xt_disk_quiesce() &&
          !atari_xt_disk_eject(), "a failed validation close blocks handoff and retains the previous disk");
    fail_close = false;
    check(atari_xt_disk_quiesce() && !handle.active && mounted_is(ATR_NAME),
          "handoff can retry the retained FAT handle without discarding the disk");

    check(atari_xt_disk_mount(&link, MEDIA_ROOT, XEX_NAME) == NULL && !handle.active &&
          mounted_is(XEX_NAME), "XEX mounts as a read-only virtual disk");
    poll_read(&link, 0xa0U, 1U, 128U);
    const uint8_t boot_sectors = (uint8_t)shell.tx[3];
    check((uint8_t)shell.tx[2] == 0U && boot_sectors != 0U && (uint8_t)shell.tx[5] == 7U,
          "XEX sector one contains the generated 6502 boot header");
    poll_read(&link, 0xa1U, (uint8_t)(boot_sectors + 1U), 128U);
    for (size_t i = 0U; i < 128U; ++i)
        check((uint8_t)shell.tx[2U + i] == (i < sizeof(xex) ? xex[i] : 0U), "virtual disk streams and pads the XEX file");
    check(atari_xt_disk_eject() && atari_xt_disk_name() == NULL, "eject clears the virtual disk");
    prepare_read(0xa2U, 1U);
    atari_xt_disk_poll(&link);
    check(shell.tx_count == 0U, "ejected drive ignores SIO requests");
    puts("PASS: Atari disk service capability, FAT mounts, SIO ordering, and handoff");
    return EXIT_SUCCESS;
}
