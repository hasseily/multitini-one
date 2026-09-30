/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_boot.h"
#include <assert.h>
#include <string.h>

typedef struct {
    uint32_t regs[MT_REGISTER_SPAN / 4U];
    uint32_t sequence;
    uint8_t kind;
    uint32_t size, offset;
    uint8_t order[3];
    unsigned completed, resets;
    bool reset;
    uint8_t config[MT_SIZE_GENESIS_CONFIG];
    uint8_t reject_kind;
} shell_t;

static uint32_t read32(void *context, uint32_t offset)
{ return ((shell_t *)context)->regs[offset / 4U]; }
static void write32(void *context, uint32_t offset, uint32_t value)
{ ((shell_t *)context)->regs[offset / 4U] = value; }
static void progress(void *context)
{
    shell_t *s = context;
    const uint32_t sequence = s->regs[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = s->regs[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = s->regs[MT_REG_COMMAND_ARG0 / 4U];
    uint32_t status = MT_RESPONSE_OK;
    if (s->sequence == sequence) return;
    s->sequence = sequence;
    switch (opcode) {
    case MT_COMMAND_CORE_RESET:
        s->reset = arg0 != 0U; ++s->resets; break;
    case MT_COMMAND_STREAM_BEGIN:
        assert(s->reset);
        s->kind = (uint8_t)arg0;
        s->size = s->regs[MT_REG_COMMAND_ARG1 / 4U];
        s->offset = 0U;
        if (s->kind == s->reject_kind) status = MT_RESPONSE_IO_ERROR;
        break;
    case MT_COMMAND_STREAM_DATA:
        assert(s->offset < s->size);
        if (s->kind == MT_LOAD_GENESIS_CONFIG) s->config[s->offset] = (uint8_t)arg0;
        ++s->offset; break;
    case MT_COMMAND_STREAM_END:
        assert(s->offset == s->size && s->completed < 3U);
        s->order[s->completed++] = s->kind; break;
    case MT_COMMAND_STREAM_ABORT: break;
    default: assert(0);
    }
    s->regs[MT_REG_RESPONSE_STATUS / 4U] = status;
    s->regs[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

int main(void)
{
    uint8_t rom[512] = {0};
    static uint8_t save[MT_SIZE_GENESIS_SAVE];
    mt_genesis_boot_images_t images = {
        {rom, sizeof(rom)}, {save, sizeof(save)},
        {MT_GENESIS_REGION_US, 3U, 0U, 1U}
    };
    mt_core_link_t link;
    shell_t shell = {0};
    memcpy(rom + 0x100U, "SEGA MEGA DRIVE", 15U);
    mt_core_link_init(&link, &shell, read32, write32);
    assert(mt_genesis_stage(&link, &images, 2U, progress, &shell) == MT_LINK_OK);
    assert(shell.reset && shell.resets == 1U && shell.completed == 3U);
    assert(shell.order[0] == MT_LOAD_GENESIS_CONFIG && shell.order[1] == MT_LOAD_GENESIS_CART &&
           shell.order[2] == MT_LOAD_GENESIS_SAVE);
    assert(memcmp(shell.config, "MD01", 4U) == 0);
    assert(shell.config[4] == 1U && shell.config[5] == 3U &&
           shell.config[6] == 0U && shell.config[7] == 1U);
    for (size_t i = 8U; i < sizeof(shell.config); ++i) assert(shell.config[i] == 0U);
    assert(mt_genesis_release(&link, 2U, progress, &shell) == MT_LINK_OK);
    assert(!shell.reset && shell.resets == 2U);
    images.config.region = MT_GENESIS_REGION_AUTO;
    assert(mt_genesis_boot(&link, &images, 2U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    images.config.region = MT_GENESIS_REGION_US;
    images.save.size = MT_SIZE_GENESIS_SAVE - 1U;
    assert(mt_genesis_boot(&link, &images, 2U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    images.save.size = MT_SIZE_GENESIS_SAVE;
    shell.completed = 0U;
    shell.reject_kind = MT_LOAD_GENESIS_CART;
    assert(mt_genesis_boot(&link, &images, 2U, progress, &shell) == MT_LINK_REMOTE_ERROR);
    assert(shell.reset && shell.completed == 1U);
    shell.reject_kind = 0U;
    shell.completed = 0U;
    images.save.data = NULL; images.save.size = 0U;
    assert(mt_genesis_boot(&link, &images, 2U, progress, &shell) == MT_LINK_OK);
    assert(!shell.reset && shell.completed == 2U);
    return 0;
}
