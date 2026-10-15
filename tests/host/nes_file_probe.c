/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only private cartridge audit bridge, used by scripts/test_nes_files.py. */
#include "multitini_nes_content.h"
#include "multitini_nes_boot.h"
#include <string.h>

#ifndef MT_NES_PROBE_INSPECT_ONLY
typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / 4U], sequence;
    uint8_t *wire;
    size_t capacity, size, offset;
    unsigned streams;
    int reset, invalid;
} probe_shell_t;

static void receive(probe_shell_t *shell, uint8_t byte)
{
    if (shell->offset >= shell->size || shell->offset >= shell->capacity)
        shell->invalid = 1;
    else shell->wire[shell->offset++] = byte;
}

static uint32_t read32(void *context, uint32_t address)
{
    probe_shell_t *shell = context;
    if (address == MT_REG_STREAM_STATUS)
        return shell->offset == shell->size ? MT_STREAM_STATUS_DRAINED : MT_STREAM_STATUS_READY;
    return shell->registers[address / 4U];
}

static void write32(void *context, uint32_t address, uint32_t value)
{
    probe_shell_t *shell = context;
    shell->registers[address / 4U] = value;
    if (address == MT_REG_STREAM_DATA_BURST)
        for (unsigned i = 0U; i < 4U && shell->offset < shell->size; ++i)
            receive(shell, (uint8_t)(value >> (8U * i)));
}

static void progress(void *context)
{
    probe_shell_t *shell = context;
    uint32_t *registers = shell->registers;
    const uint32_t sequence = registers[MT_REG_COMMAND_SEQ / 4U];
    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    const uint32_t arg0 = registers[MT_REG_COMMAND_ARG0 / 4U];
    switch (registers[MT_REG_COMMAND_OPCODE / 4U]) {
    case MT_COMMAND_CORE_RESET: shell->reset = arg0 != 0U; break;
    case MT_COMMAND_STREAM_BEGIN:
        shell->size = registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->offset = 0U;
        if (!shell->reset || arg0 != MT_LOAD_NES_CART || shell->size > shell->capacity)
            shell->invalid = 1;
        break;
    case MT_COMMAND_STREAM_DATA: receive(shell, (uint8_t)arg0); break;
    case MT_COMMAND_STREAM_END:
        if (shell->offset != shell->size) shell->invalid = 1;
        ++shell->streams;
        break;
    default: shell->invalid = 1; break;
    }
    registers[MT_REG_RESPONSE_STATUS / 4U] = shell->invalid ? MT_RESPONSE_INVALID_ARGUMENT : MT_RESPONSE_OK;
    registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}
#endif

int mt_nes_file_probe(const uint8_t *input, size_t size, uint8_t *wire,
                      size_t capacity, uint32_t *metadata, int burst)
{
    mt_nes_content_info_t info;
    const mt_content_validation_t validation = mt_nes_content_inspect(input, size, &info);
    if (validation != MT_CONTENT_VALID) return (int)validation;
    metadata[0] = info.mapper;
    metadata[1] = (uint32_t)info.prg_size;
    metadata[2] = (uint32_t)info.chr_size;
    metadata[3] = info.battery ? 1U : 0U;
#ifndef MT_NES_PROBE_INSPECT_ONLY
    probe_shell_t shell = {0};
    mt_core_link_t link;
    const mt_nes_boot_images_t images = {{input, size}, {NULL, 0U}};
    shell.wire = wire;
    shell.capacity = capacity;
    shell.registers[MT_REG_ABI_VERSION / 4U] = burst ? MT_ABI_VERSION : 7U;
    mt_core_link_init(&link, &shell, read32, write32);
    const mt_link_result_t result = mt_nes_boot(&link, &images, 32U, progress, &shell);
    if (result != MT_LINK_OK || shell.invalid || shell.reset || shell.streams != 1U || shell.offset != size)
        return -100 + (int)result;
#else
    (void)wire; (void)capacity; (void)burst;
#endif
    return 0;
}
