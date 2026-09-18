/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_vectrex_overlay.h"
#include "multitini_vectrex_interface.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / 4U];
    uint8_t received[MT_VECTREX_OVERLAY_BYTES];
    size_t size;
    size_t count;
    unsigned commands;
    unsigned aborted;
    unsigned progress;
    unsigned stalls;
    uint32_t fail_opcode;
    bool blocked;
    bool open;
} shell_t;

static shell_t shell;
static uint8_t buffer[MT_VECTREX_OVERLAY_BYTES + 1U];

static uint32_t read_register(void *context, uint32_t offset)
{
    shell_t *const state = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_STATUS) {
        if (state->blocked || state->stalls != 0U) return 0U;
        return state->count == state->size ?
            MT_STREAM_STATUS_DRAINED : MT_STREAM_STATUS_READY;
    }
    return state->registers[offset / 4U];
}

static void write_register(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const state = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    state->registers[offset / 4U] = value;
    if (offset == MT_REG_STREAM_DATA_BURST) {
        unsigned lane;
        assert(state->open);
        for (lane = 0U; lane < MT_STREAM_BURST_BYTES && state->count < state->size; ++lane) {
            assert(state->count < sizeof(state->received));
            state->received[state->count++] = (uint8_t)(value >> (8U * lane));
        }
    } else if (offset == MT_REG_COMMAND_DOORBELL) {
        const uint32_t opcode = state->registers[MT_REG_COMMAND_OPCODE / 4U];
        ++state->commands;
        if (opcode == MT_COMMAND_STREAM_BEGIN) {
            assert(state->registers[MT_REG_COMMAND_ARG0 / 4U] == MT_LOAD_VECTREX_OVERLAY);
            state->size = state->registers[MT_REG_COMMAND_ARG1 / 4U];
            state->count = 0U;
            state->open = true;
        } else if (opcode == MT_COMMAND_STREAM_END) {
            assert(state->count == state->size);
            state->open = false;
        } else if (opcode == MT_COMMAND_STREAM_ABORT) {
            ++state->aborted;
            state->open = false;
        } else assert(false);
        state->registers[MT_REG_RESPONSE_SEQ / 4U] = value;
        state->registers[MT_REG_RESPONSE_STATUS / 4U] =
            opcode == state->fail_opcode ? MT_RESPONSE_BUSY : MT_RESPONSE_OK;
    }
}

static void progress(void *context)
{
    shell_t *const state = context;
    ++state->progress;
    if (state->stalls != 0U) --state->stalls;
}

static void reset_shell(mt_core_link_t *link)
{
    memset(&shell, 0, sizeof(shell));
    shell.registers[MT_REG_ABI_VERSION / 4U] = (MT_ABI_MAJOR << 16U) | 13U;
    mt_core_link_init(link, &shell, read_register, write_register);
}

static void test_pixels(void)
{
    uint32_t word;
    uint8_t data[2];
    mt_vectrex_overlay_rgba_t pixel = {1U, 2U, 3U, 4U};
    assert(!mt_vectrex_overlay_valid_size(0U));
    assert(!mt_vectrex_overlay_valid_size(1U));
    assert(mt_vectrex_overlay_valid_size(2U));
    assert(mt_vectrex_overlay_valid_size(MT_VECTREX_OVERLAY_BYTES));
    assert(!mt_vectrex_overlay_valid_size(MT_VECTREX_OVERLAY_BYTES + 2U));
    assert(!mt_vectrex_overlay_valid_size(SIZE_MAX));
    for (word = 0U; word <= UINT16_MAX; ++word) {
        data[0] = (uint8_t)word;
        data[1] = (uint8_t)(word >> 8U);
        assert(mt_vectrex_overlay_pixel(data, sizeof(data), 0U, 0U, &pixel));
        assert(pixel.red == (word & 15U) * 17U);
        assert(pixel.green == ((word >> 4U) & 15U) * 17U);
        assert(pixel.blue == ((word >> 8U) & 15U) * 17U);
        assert(pixel.alpha == ((word >> 12U) & 15U) * 17U);
    }
    assert(!mt_vectrex_overlay_pixel(data, 1U, 0U, 0U, &pixel));
    assert(pixel.red == 255U && pixel.alpha == 255U);
    assert(!mt_vectrex_overlay_pixel(data, 2U, 540U, 0U, &pixel));
    assert(!mt_vectrex_overlay_pixel(data, 2U, 0U, 720U, &pixel));
    assert(!mt_vectrex_overlay_pixel(NULL, 2U, 0U, 0U, &pixel));
    assert(!mt_vectrex_overlay_pixel(data, 2U, 0U, 0U, NULL));
    assert(mt_vectrex_overlay_pixel(data, 2U, 539U, 719U, &pixel));
    assert(pixel.red == 0U && pixel.green == 0U && pixel.blue == 0U && pixel.alpha == 0U);
    memset(buffer, 0, sizeof(buffer));
    buffer[1080] = 0x21U; buffer[1081] = 0x43U;
    assert(mt_vectrex_overlay_pixel(buffer, MT_VECTREX_OVERLAY_BYTES, 0U, 1U, &pixel));
    assert(pixel.red == 17U && pixel.green == 34U && pixel.blue == 51U && pixel.alpha == 68U);
}

static void test_paths(void)
{
    static const char *const invalid[] = {
        "", "/game.vec", "a//game.vec", "a/", "./game.vec", "../game.vec",
        "a/../game.vec", "a/.. /game.vec", "a./game.vec", "a /game.vec",
        "C:/game.vec", "0:/game.vec", "a\\game.vec", "a/*.vec", "a/g?.vec",
        "a/g<.vec", "a/g>.vec", "a/g|.vec", "a/g\".vec", "a/g:.vec",
        "a/\001.vec", "a/\177.vec", "game.vec.", "game.vec "
    };
    char output[MT_VECTREX_OVERLAY_PATH_MAX + 8U];
    char large[MT_VECTREX_OVERLAY_PATH_MAX + 2U];
    size_t i;
    assert(mt_vectrex_overlay_sidecar_path("folder.VEC/deeper/My.Game.VEC", output, sizeof(output)));
    assert(strcmp(output, "folder.VEC/deeper/My.Game.ovr") == 0);
    assert(mt_vectrex_overlay_sidecar_path("Vecalabeth/vec.bas", output, sizeof(output)));
    assert(strcmp(output, "Vecalabeth/vec.ovr") == 0);
    assert(mt_vectrex_overlay_sidecar_path("a/game", output, sizeof(output)));
    assert(strcmp(output, "a/game.ovr") == 0);
    assert(mt_vectrex_overlay_sidecar_path("a/.hidden", output, sizeof(output)));
    assert(strcmp(output, "a/.hidden.ovr") == 0);
    assert(mt_vectrex_overlay_sidecar_path("a/GAME.OVR", output, sizeof(output)));
    assert(strcmp(output, "a/GAME.ovr") == 0);
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        strcpy(output, "unchanged");
        assert(!mt_vectrex_overlay_sidecar_path(invalid[i], output, sizeof(output)));
        assert(strcmp(output, "unchanged") == 0);
    }
    assert(!mt_vectrex_overlay_sidecar_path(NULL, output, sizeof(output)));
    assert(!mt_vectrex_overlay_sidecar_path("game.vec", NULL, sizeof(output)));
    assert(!mt_vectrex_overlay_sidecar_path("game.vec", output, 8U));
    assert(strcmp(output, "unchanged") == 0);
    assert(mt_vectrex_overlay_sidecar_path("game.vec", output, 9U));
    assert(strcmp(output, "game.ovr") == 0);
    strcpy(output, "dir/game.bin");
    assert(mt_vectrex_overlay_sidecar_path(output, output, sizeof(output)));
    assert(strcmp(output, "dir/game.ovr") == 0);
    strcpy(output, "dir/game.bin");
    assert(mt_vectrex_overlay_sidecar_path(output, output + 1U, sizeof(output) - 1U));
    assert(strcmp(output + 1U, "dir/game.ovr") == 0);
    memset(large, 'a', sizeof(large));
    large[251] = '\0';
    assert(mt_vectrex_overlay_sidecar_path(large, output, 256U));
    assert(strlen(output) == 255U);
    large[251] = 'a'; large[252] = '\0';
    assert(!mt_vectrex_overlay_sidecar_path(large, output, sizeof(output)));
    memcpy(large + 251U, ".vec", 5U);
    assert(mt_vectrex_overlay_sidecar_path(large, output, 256U));
    assert(strlen(output) == 255U);
    large[255] = 'a'; large[256] = '\0';
    assert(!mt_vectrex_overlay_sidecar_path(large, output, sizeof(output)));
}

static void test_transfer(void)
{
    mt_core_link_t link;
    size_t i;
    reset_shell(&link);
    for (i = 0U; i < sizeof(buffer); ++i) buffer[i] = (uint8_t)(i * 73U + 41U);
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), MT_VECTREX_OVERLAY_BYTES,
        8U, progress, &shell) == MT_LINK_OK);
    assert(shell.size == MT_VECTREX_OVERLAY_BYTES && shell.count == shell.size);
    assert(memcmp(buffer, shell.received, shell.size) == 0);
    assert(shell.commands == 2U && shell.aborted == 0U && !shell.open);
    reset_shell(&link);
    memset(buffer, 0xa5, sizeof(buffer));
    buffer[0] = 0x21U; buffer[1] = 0x03U; /* Alpha-zero RGB must survive. */
    shell.stalls = 3U;
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), 2U,
        8U, progress, &shell) == MT_LINK_OK);
    assert(shell.received[0] == 0x21U && shell.received[1] == 0x03U);
    for (i = 2U; i < MT_VECTREX_OVERLAY_BYTES; ++i)
        assert(buffer[i] == 0U && shell.received[i] == 0U);
    assert(buffer[MT_VECTREX_OVERLAY_BYTES] == 0xa5U);
    assert(shell.progress >= 4U);
    reset_shell(&link);
    memset(buffer, 0xa5, sizeof(buffer));
    assert(mt_vectrex_overlay_load(&link, buffer, MT_VECTREX_OVERLAY_BYTES - 1U, 2U,
        8U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), 1U,
        8U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), 2U,
        0U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    assert(mt_vectrex_overlay_load(NULL, buffer, sizeof(buffer), 2U,
        8U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    assert(mt_vectrex_overlay_load(&link, NULL, sizeof(buffer), 2U,
        8U, progress, &shell) == MT_LINK_BAD_ARGUMENT);
    assert(shell.commands == 0U);
    for (i = 0U; i < sizeof(buffer); ++i) assert(buffer[i] == 0xa5U);
    assert(mt_vectrex_overlay_clear(&link, 8U, progress, &shell) == MT_LINK_OK);
    assert(shell.size == 1U && shell.count == 1U && shell.received[0] == 0U);
    reset_shell(&link);
    shell.blocked = true;
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), 2U,
        3U, progress, &shell) == MT_LINK_TIMEOUT);
    assert(shell.aborted == 1U && !shell.open && shell.count == 0U);
    reset_shell(&link);
    shell.fail_opcode = MT_COMMAND_STREAM_END;
    assert(mt_vectrex_overlay_load(&link, buffer, sizeof(buffer), 2U,
        8U, progress, &shell) == MT_LINK_REMOTE_ERROR);
    assert(shell.aborted == 1U && !shell.open);
    reset_shell(&link);
    shell.fail_opcode = MT_COMMAND_STREAM_BEGIN;
    assert(mt_vectrex_overlay_clear(&link, 8U, progress, &shell) == MT_LINK_REMOTE_ERROR);
    assert(shell.aborted == 1U && shell.count == 0U);
}

int main(void)
{
    test_pixels();
    test_paths();
    test_transfer();
    puts("PASS: Vectrex OVR pixels, confined sidecars, full/short load, clear, transfer failures");
    return 0;
}
