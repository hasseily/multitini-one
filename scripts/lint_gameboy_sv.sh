#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sim_build="$repo_root/build/hdl-sim"
mkdir -p "$sim_build"

local_sources=(
    "$repo_root/hdl/core/gameboy/multitini_gameboy_defs_pkg.sv"
    "$repo_root/hdl/core/gameboy/multitini_gameboy_loader.sv"
    "$repo_root/hdl/core/gameboy/multitini_gameboy_clock_enable.sv"
    "$repo_root/hdl/core/gameboy/multitini_gameboy_input.sv"
    "$repo_root/hdl/core/gameboy/multitini_gameboy_memory.sv"
    "$repo_root/hdl/core/gameboy/ports/dpram.sv"
    "$repo_root/hdl/core/gameboy/ports/gb.v"
    "$repo_root/hdl/core/gameboy/ports/cart.v"
)

# Parse and elaborate both mixed-language top-level ports. VHDL dependencies
# are intentionally black boxes here; Vivado is the authoritative mixed-
# language elaborator on the PC handoff.
iverilog -g2012 -Wall -Wimplicit -i -s multitini_gameboy_adapter \
    -o "$sim_build/multitini_gameboy_adapter.vvp" \
    "${local_sources[@]}" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_adapter.sv"

# Elaborate the complete board shell with the GBC build-time branch selected.
# The generated Zynq wrapper and VHDL machine entities remain black boxes.
iverilog -g2012 -Wall -Wimplicit -i -s multitini_one_top \
    -P multitini_one_top.ACTIVE_MACHINE=1 \
    -o "$sim_build/multitini_gameboy_top.vvp" \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/core/multitini_core_interface.sv" \
    "${local_sources[@]}" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_adapter.sv" \
    "$repo_root/third_party/appletini/hdl/reset_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bus_sampled.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_handshake_reg.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_pcm_tx.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_tone.sv" \
    "$repo_root/third_party/appletini/hdl/audio_spdif_pcm_tx.sv" \
    "$repo_root/hdl/board/multitini_audio_spdif_pcm_tx.sv" \
    "$repo_root/hdl/common/multitini_axi3_regs.sv" \
    "$repo_root/hdl/common/multitini_control_regs.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_cdc_pulse_toggle.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/board/multitini_audio_test.sv" \
    "$repo_root/hdl/board/multitini_one_top.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv"

# Parse the opt-in physical PSRAM branch with Xilinx primitives black-boxed.
iverilog -g2012 -Wall -Wimplicit -i -s multitini_gameboy_psram_path \
    -o "$sim_build/multitini_gameboy_psram_path.vvp" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_memory_cdc.sv" \
    "$repo_root/hdl/common/multitini_psram_line_service.sv" \
    "$repo_root/third_party/appletini/hdl/psram_driver.sv" \
    "$repo_root/hdl/board/multitini_gameboy_psram_path.sv"

upstream_sources=(
    "${(@f)$(cd "$repo_root" && rg --files third_party/gameboy/upstream/rtl | \
        rg '\.(v|sv)$' | rg -v '/(gb|cart|video)\.v$')}"
)
upstream_sources=("${upstream_sources[@]/#/$repo_root/}")

# The upstream video module contains an unpacked tri-state array construct
# unsupported by Verilator; Icarus parses it in the mixed-language compile
# gate and Vivado must elaborate it later. All other selected Verilog logic is
# elaborated here. Missing VHDL entities are treated as black boxes.
verilator --lint-only --timing --bbox-unsup \
    --top-module multitini_gameboy_adapter -Wall -Wno-fatal \
    -Wno-MODMISSING -Wno-PROCASSWIRE -Wno-DECLFILENAME \
    -Wno-TIMESCALEMOD -Wno-PINCONNECTEMPTY -Wno-PINMISSING \
    -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-UNDRIVEN -Wno-WIDTH \
    -Wno-MULTIDRIVEN -Wno-LATCH -Wno-EOFNEWLINE -Wno-VARHIDDEN \
    -Wno-ASCRANGE -Wno-PROCASSINIT -Wno-CASEINCOMPLETE \
    -I"$repo_root/hdl/core/gameboy/boot" \
    "${local_sources[@]}" "${upstream_sources[@]}" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_adapter.sv"

echo "PASS: Game Boy Verilog adapter lint (VHDL and video deferred to Vivado)"
