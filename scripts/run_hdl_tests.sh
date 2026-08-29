#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sim_build="$repo_root/build/hdl-sim"
mkdir -p "$sim_build"

run_test() {
    name=$1
    shift
    iverilog -g2012 -Wall -Wimplicit -s "$name" \
        -o "$sim_build/$name.vvp" "$@"
    vvp "$sim_build/$name.vvp"
}

run_test tb_control_regs \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/common/multitini_control_regs.sv" \
    "$repo_root/hdl/sim/tb_control_regs.sv"

run_test tb_axi3_regs \
    "$repo_root/hdl/common/multitini_axi3_regs.sv" \
    "$repo_root/hdl/sim/tb_axi3_regs.sv"

run_test tb_video_test \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/sim/tb_video_test.sv"

run_test tb_audio_test \
    "$repo_root/third_party/appletini/hdl/reset_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_tone.sv" \
    "$repo_root/third_party/appletini/hdl/audio_spdif_pcm_tx.sv" \
    "$repo_root/hdl/board/multitini_audio_test.sv" \
    "$repo_root/hdl/sim/tb_audio_test.sv"

run_test tb_psram_bist \
    "$repo_root/hdl/common/multitini_psram_bist.sv" \
    "$repo_root/hdl/sim/tb_psram_bist.sv"

# Compile the complete tracked shell. The generated block-design wrapper is a
# Vivado artifact, so -i treats that one module as a black box here.
iverilog -g2012 -Wall -Wimplicit -i -s multitini_one_top \
    -o "$sim_build/multitini_one_top.vvp" \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/core/multitini_core_interface.sv" \
    "$repo_root/third_party/appletini/hdl/reset_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bus_sampled.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_handshake_reg.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_pulse_toggle.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_pcm_tx.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_tone.sv" \
    "$repo_root/third_party/appletini/hdl/audio_spdif_pcm_tx.sv" \
    "$repo_root/hdl/common/multitini_axi3_regs.sv" \
    "$repo_root/hdl/common/multitini_control_regs.sv" \
    "$repo_root/hdl/common/multitini_psram_bist.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/board/multitini_audio_test.sv" \
    "$repo_root/hdl/board/multitini_one_top.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv"
echo "PASS: complete shell compiles with block design black-boxed"
