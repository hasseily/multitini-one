/* SPDX-License-Identifier: GPL-3.0-only */
#include "atari_xt_content_service.h"
#include "multitini_atari_xt_interface.h"
#include "sd_storage.h"
#include "sleep.h"

#include <assert.h>
#include <string.h>

#define CORE_DIRECTORY "0:/multitini/cores/atari-800xl"
#define MEDIA_ROOT CORE_DIRECTORY "/media"
#define CART_NAME "nested/game.CAR"

typedef struct {
    uint32_t regs[MT_REGISTER_SPAN / 4U];
    uint8_t data[MT_ATARI_XT_CART_16_BYTES];
    uint8_t completed[8];
    size_t size;
    size_t received;
    unsigned reset_commands;
    unsigned completed_count;
    uint8_t kind;
    uint8_t reject_kind;
    bool reset_held;
    bool stream_open;
    bool reject_pending;
    bool hold_reset_on_release;
} shell_t;

static shell_t shell;
static bool missing_basic;
static bool corrupt_cart;

mt_sd_result_t mt_sd_storage_read_file_exact(const char *path, uint8_t *dst, size_t size)
{
    const bool basic = strcmp(path, CORE_DIRECTORY "/roms/ataribas.rom") == 0;
    assert(basic || strcmp(path, CORE_DIRECTORY "/roms/atarixl.rom") == 0);
    assert(size == (basic ? MT_ATARI_XT_BASIC_BYTES : MT_ATARI_XT_OS_BYTES));
    if (basic && missing_basic) return (mt_sd_result_t){.status = MT_SD_OPEN_FAILED};
    memset(dst, 0, size);
    dst[0] = basic ? 0x42U : 0x81U;
    return (mt_sd_result_t){.status = MT_SD_OK, .size = size};
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst, size_t capacity)
{
    static const uint8_t header[MT_ATARI_XT_CART_HEADER_BYTES] = {
        'C', 'A', 'R', 'T', 0U, 0U, 0U, 1U, 0U, 0x15U, 0x60U, 0U, 0U, 0U, 0U, 0U
    };
    assert(strcmp(path, MEDIA_ROOT "/" CART_NAME) == 0);
    assert(capacity >= sizeof(header) + MT_ATARI_XT_CART_8_BYTES);
    memcpy(dst, header, sizeof(header));
    memset(dst + sizeof(header), 0xAB, MT_ATARI_XT_CART_8_BYTES);
    if (corrupt_cart) dst[sizeof(header)] = 0U;
    return (mt_sd_result_t){.status = MT_SD_OK, .size = sizeof(header) + MT_ATARI_XT_CART_8_BYTES};
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    (void)status;
    return "mock SD error";
}

void usleep(uint32_t microseconds)
{
    assert(microseconds == 1U);
    if (shell.reject_pending) {
        shell.regs[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_LOAD_ERROR;
        shell.reject_pending = false;
    }
}

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *mock = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_STATUS && mock->stream_open)
        return mock->received < mock->size ? MT_STREAM_STATUS_READY : MT_STREAM_STATUS_DRAINED;
    return mock->regs[offset / 4U];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *mock = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_DATA_BURST) {
        assert(mock->reset_held && mock->stream_open && mock->received < mock->size);
        for (unsigned lane = 0U; lane < MT_STREAM_BURST_BYTES && mock->received < mock->size; ++lane)
            mock->data[mock->received++] = (uint8_t)(value >> (lane * 8U));
    } else if (offset == MT_REG_ERROR) {
        assert(value == MT_ERROR_CORE_LOAD);
        mock->regs[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_LOAD_ERROR;
    } else if (offset == MT_REG_COMMAND_DOORBELL) {
        const uint32_t arg = mock->regs[MT_REG_COMMAND_ARG0 / 4U];
        assert(value == mock->regs[MT_REG_COMMAND_SEQ / 4U]);
        switch (mock->regs[MT_REG_COMMAND_OPCODE / 4U]) {
        case MT_COMMAND_CORE_RESET:
            assert(!mock->stream_open);
            ++mock->reset_commands;
            if (arg != 0U) {
                mock->reset_held = true;
                mock->regs[MT_REG_CONTROL / 4U] |= MT_CONTROL_CORE_RESET_REQUEST;
                mock->regs[MT_REG_STATUS / 4U] &= ~MT_STATUS_CORE_READY;
            } else {
                if (!mock->hold_reset_on_release) {
                    mock->reset_held = false;
                    mock->regs[MT_REG_CONTROL / 4U] &= ~MT_CONTROL_CORE_RESET_REQUEST;
                }
                mock->regs[MT_REG_STATUS / 4U] |= MT_STATUS_CORE_READY;
            }
            break;
        case MT_COMMAND_STREAM_BEGIN:
            assert(mock->reset_held && !mock->stream_open);
            mock->kind = (uint8_t)arg;
            mock->size = mock->regs[MT_REG_COMMAND_ARG1 / 4U];
            assert(mock->size != 0U && mock->size <= sizeof(mock->data));
            mock->received = 0U;
            mock->stream_open = true;
            break;
        case MT_COMMAND_STREAM_END:
            assert(mock->reset_held && mock->stream_open && mock->received == mock->size);
            assert(mock->completed_count < sizeof(mock->completed));
            mock->completed[mock->completed_count++] = mock->kind;
            mock->stream_open = false;
            mock->reject_pending = mock->kind == mock->reject_kind;
            break;
        default:
            assert(false);
        }
        mock->regs[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
        mock->regs[MT_REG_RESPONSE_SEQ / 4U] = value;
    } else {
        mock->regs[offset / 4U] = value;
    }
}

int main(void)
{
    mt_core_link_t link;
    shell.regs[MT_REG_CORE_ID / 4U] = MT_ATARI_XT_CORE_ID;
    shell.regs[MT_REG_ABI_VERSION / 4U] = MT_ABI_VERSION;
    mt_core_link_init(&link, &shell, read32, write32);
    atari_xt_content_reset_selection();
    assert(atari_xt_content_insert(&link, MEDIA_ROOT, CART_NAME, NULL) != NULL);
    assert(atari_xt_content_restart(&link, NULL) != NULL);

    missing_basic = true;
    assert(atari_xt_content_boot(&link, CORE_DIRECTORY, NULL) != NULL);
    assert(!atari_xt_content_roms_loaded() && shell.reset_commands == 0U);
    missing_basic = false;
    assert(atari_xt_content_boot(&link, CORE_DIRECTORY, NULL) == NULL);
    assert(atari_xt_content_roms_loaded() && !shell.reset_held && shell.reset_commands == 2U);
    assert(shell.completed_count == 2U && shell.completed[0] == MT_LOAD_ATARI_XT_OS &&
           shell.completed[1] == MT_LOAD_ATARI_XT_BASIC && shell.size == MT_ATARI_XT_BASIC_BYTES);
    assert(shell.data[0] == 0x42U);

    corrupt_cart = true;
    assert(atari_xt_content_insert(&link, MEDIA_ROOT, CART_NAME, NULL) != NULL);
    assert(shell.reset_commands == 2U && atari_xt_content_cart_name() == NULL);
    corrupt_cart = false;
    assert(atari_xt_content_insert(&link, MEDIA_ROOT, CART_NAME, NULL) == NULL);
    assert(!shell.reset_held && shell.reset_commands == 4U && !atari_xt_content_cart_uncertain());
    assert(shell.completed[2] == MT_LOAD_ATARI_XT_CART && shell.size == MT_ATARI_XT_CART_8_BYTES);
    for (size_t i = 0U; i < shell.size; ++i) assert(shell.data[i] == 0xABU);
    assert(strcmp(atari_xt_content_cart_name(), CART_NAME) == 0);

    shell.reject_kind = MT_LOAD_ATARI_XT_CART;
    assert(atari_xt_content_insert(&link, MEDIA_ROOT, CART_NAME, NULL) != NULL);
    assert(shell.reset_held && shell.reset_commands == 5U && atari_xt_content_cart_uncertain());
    assert(atari_xt_content_cart_name() == NULL);
    assert(atari_xt_content_restart(&link, NULL) != NULL && shell.reset_commands == 5U);
    assert(atari_xt_content_eject(&link, NULL) == NULL);
    assert(!shell.reset_held && shell.reset_commands == 7U && !atari_xt_content_cart_uncertain());
    assert(shell.completed[4] == MT_LOAD_ATARI_XT_CART_EJECT && shell.size == 1U && shell.data[0] == 0U);
    assert(atari_xt_content_restart(&link, NULL) == NULL);
    assert(!shell.reset_held && shell.reset_commands == 9U && shell.completed_count == 5U);

    /* A stale ready bit cannot override a live reset hold. */
    shell.hold_reset_on_release = true;
    assert(atari_xt_content_eject(&link, NULL) != NULL);
    assert(shell.reset_held && shell.reset_commands == 12U);
    return 0;
}
