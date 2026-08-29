/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gameboy_boot.h"
#include "multitini_gameboy_interface.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_kind;
    uint32_t active_size;
    uint32_t active_offset;
    uint32_t checksum;
    uint32_t reset_commands;
    int core_reset;
    int cart_complete;
    int boot_complete;
} boot_shell_t;

static void finish_gameboy_header(uint8_t *cart)
{
    uint8_t checksum = 0U;
    size_t index;
    cart[0x143U] = 0x80U;
    cart[0x147U] = 0x00U;
    cart[0x148U] = 0x00U;
    cart[0x149U] = 0x00U;
    for (index = 0x134U; index <= 0x14CU; ++index) {
        checksum = (uint8_t)(checksum - cart[index] - 1U);
    }
    cart[0x14DU] = checksum;
}

static uint32_t read32(void *context, uint32_t offset)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    boot_shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
    const uint32_t arg1 = shell->registers[MT_REG_COMMAND_ARG1 / 4U];

    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    switch (opcode) {
    case MT_COMMAND_CORE_RESET:
        shell->core_reset = arg0 != 0U;
        ++shell->reset_commands;
        break;
    case MT_COMMAND_STREAM_BEGIN:
        assert(shell->core_reset != 0 && shell->active_size == 0U);
        shell->active_kind = arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        assert(shell->active_size != 0U);
        shell->checksum += arg0;
        ++shell->active_offset;
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        if (shell->active_kind == MT_LOAD_GBC_BOOT) shell->boot_complete = 1;
        if (shell->active_kind == MT_LOAD_GB_CART) shell->cart_complete = 1;
        shell->active_kind = 0U;
        shell->active_size = 0U;
        shell->active_offset = 0U;
        break;
    default:
        assert(0);
    }
    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
    shell->registers[MT_REG_RESPONSE_ARG0 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_ARG1 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

int main(void)
{
    static uint8_t cartridge[MT_SIZE_GB_CART_MIN];
    uint8_t bootstrap[MT_SIZE_GBC_BOOT];
    mt_gameboy_boot_images_t images;
    mt_core_link_t link;
    boot_shell_t shell = {0};
    uint32_t expected_checksum = 0U;
    size_t index;

    for (index = 0U; index < sizeof(cartridge); ++index) {
        cartridge[index] = (uint8_t)(index ^ 0xA5U);
    }
    finish_gameboy_header(cartridge);
    for (index = 0U; index < sizeof(cartridge); ++index)
        expected_checksum += cartridge[index];
    for (index = 0U; index < sizeof(bootstrap); ++index) {
        bootstrap[index] = (uint8_t)(index ^ 0x5AU);
        expected_checksum += bootstrap[index];
    }
    images.cartridge.data = cartridge;
    images.cartridge.size = sizeof(cartridge);
    images.bootstrap_override.data = bootstrap;
    images.bootstrap_override.size = sizeof(bootstrap);

    mt_core_link_init(&link, &shell, read32, write32);
    assert(mt_gameboy_boot(&link, &images, 2U, respond, &shell) == MT_LINK_OK);
    assert(shell.core_reset == 0 && shell.reset_commands == 2U);
    assert(shell.boot_complete != 0 && shell.cart_complete != 0);
    assert(shell.checksum == expected_checksum);

    images.bootstrap_override.data = NULL;
    images.bootstrap_override.size = 0U;
    cartridge[0x14DU] ^= 1U;
    assert(mt_gameboy_boot(&link, &images, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    cartridge[0x14DU] ^= 1U;
    images.cartridge.size = MT_SIZE_GB_CART_MIN - 1U;
    assert(mt_gameboy_boot(&link, &images, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(mt_gameboy_boot(&link, NULL, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    return 0;
}
