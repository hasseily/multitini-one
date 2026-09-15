#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sim_build="$repo_root/build/hdl-sim"
mkdir -p "$sim_build"
cp "$repo_root/hdl/common/multitini_font7x8.mem" "$sim_build/"

run_test() {
    name=$1
    shift
    iverilog -g2012 -Wall -Wimplicit -s "$name" \
        -o "$sim_build/$name.vvp" "$@"
    (cd "$sim_build" && vvp "$name.vvp")
}

run_test tb_control_regs \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/common/multitini_control_regs.sv" \
    "$repo_root/hdl/sim/tb_control_regs.sv"

run_test tb_stream_burst_mailbox \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/common/multitini_control_regs.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/sim/tb_stream_burst_mailbox.sv"

run_test tb_axi3_regs \
    "$repo_root/hdl/common/multitini_axi3_regs.sv" \
    "$repo_root/hdl/sim/tb_axi3_regs.sv"

run_test tb_video_test \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/sim/tb_video_test.sv"

run_test tb_border_config \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_border_config.sv" \
    "$repo_root/hdl/sim/tb_border_config.sv"

run_test tb_border_overlay \
    "$repo_root/hdl/board/multitini_border_overlay.sv" \
    "$repo_root/hdl/sim/tb_border_overlay.sv"

run_test tb_c64_video_bridge \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/hdl/board/multitini_c64_video_bridge.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/sim/tb_c64_video_bridge.sv"

run_test tb_c64_ntsc_video_bridge \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/hdl/board/multitini_c64_video_bridge.sv" \
    "$repo_root/hdl/sim/tb_c64_ntsc_video_bridge.sv"

run_test tb_gameboy_video_bridge \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/hdl/board/multitini_gameboy_video_bridge.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/sim/tb_gameboy_video_bridge.sv"

run_test tb_audio_test \
    "$repo_root/third_party/appletini/hdl/reset_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_bit_sync.sv" \
    "$repo_root/third_party/appletini/hdl/cdc_handshake_reg.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_tone.sv" \
    "$repo_root/third_party/appletini/hdl/audio_i2s_pcm_tx.sv" \
    "$repo_root/hdl/board/multitini_audio_spdif_pcm_tx.sv" \
    "$repo_root/hdl/board/multitini_audio_test.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/sim/tb_audio_test.sv"

run_test tb_sid_output_schedule \
    "$repo_root/hdl/sim/tb_sid_output_schedule.sv" \
    "$repo_root/hdl/core/c64/ports/sid_top.sv"

run_test tb_psram_bist \
    "$repo_root/hdl/common/multitini_psram_bist.sv" \
    "$repo_root/hdl/sim/tb_psram_bist.sv"

run_test tb_async_mailbox \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/sim/tb_async_mailbox.sv"

run_test tb_text_overlay \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_text_overlay.sv" \
    "$repo_root/hdl/sim/tb_text_overlay.sv"

run_test tb_cdc_pulse_toggle \
    "$repo_root/hdl/common/multitini_cdc_pulse_toggle.sv" \
    "$repo_root/hdl/sim/tb_cdc_pulse_toggle.sv"

run_test tb_c64_loader \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_loader.sv" \
    "$repo_root/hdl/sim/tb_c64_loader.sv"

run_test tb_c64_content_loader \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_content_loader.sv" \
    "$repo_root/hdl/sim/tb_c64_content_loader.sv"

# Only the head tracker is under test here. The imported drive and media leaves
# are intentionally black-boxed, matching the portable whole-shell gate below.
iverilog -g2012 -Wall -Wimplicit -i -s tb_c64_1541_head_reset \
    -o "$sim_build/tb_c64_1541_head_reset.vvp" \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_1541.sv" \
    "$repo_root/hdl/sim/tb_c64_1541_head_reset.sv"
(cd "$sim_build" && vvp tb_c64_1541_head_reset.vvp)

# The parallel-cable regression drives the imported 1541 CPU and VIAs, whose
# T65 and 6522 models are VHDL. Icarus cannot elaborate them, so that test
# runs through the XSim script when a working Vivado is on PATH and is
# otherwise skipped here. WSL exposes the Windows wrapper without a Linux
# binary, so probe the tool instead of only its name.
if xelab --version >/dev/null 2>&1; then
    sh "$repo_root/scripts/run_xsim_tests.sh"
else
    echo "SKIP: tb_c64_parallel_cable needs XSim; see scripts/run_xsim_tests.sh"
fi

run_test tb_c64_cpu_turbo_guard \
    "$repo_root/hdl/core/c64/multitini_c64_cpu_turbo_guard.sv" \
    "$repo_root/hdl/sim/tb_c64_cpu_turbo_guard.sv"

run_test tb_c64_run_injector \
    "$repo_root/hdl/core/c64/multitini_c64_run_injector.sv" \
    "$repo_root/hdl/sim/tb_c64_run_injector.sv"

run_test tb_c64_cart_memory \
    "$repo_root/hdl/core/c64/multitini_c64_cart_memory.sv" \
    "$repo_root/hdl/sim/tb_c64_cart_memory.sv"

run_test tb_c64_d64_track \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_d64_track.sv" \
    "$repo_root/hdl/sim/tb_c64_d64_track.sv"

run_test tb_c64_d64_buffer_alignment \
    "$repo_root/hdl/core/c64/ports/iecdrv_misc.sv" \
    "$repo_root/hdl/core/c64/ports/c1541_gcr.sv" \
    "$repo_root/hdl/sim/tb_c64_d64_buffer_alignment.sv"

run_test tb_c64_g64_track \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_g64_track.sv" \
    "$repo_root/hdl/sim/tb_c64_g64_track.sv"

run_test tb_c64_input \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_input.sv" \
    "$repo_root/hdl/sim/tb_c64_input.sv"

run_test tb_gameboy_input \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_defs_pkg.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_input.sv" \
    "$repo_root/hdl/sim/tb_gameboy_input.sv"

run_test tb_gameboy_clock_enable \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_clock_enable.sv" \
    "$repo_root/hdl/sim/tb_gameboy_clock_enable.sv"

run_test tb_gameboy_loader \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_defs_pkg.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_loader.sv" \
    "$repo_root/hdl/sim/tb_gameboy_loader.sv"

run_test tb_gameboy_memory \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_memory.sv" \
    "$repo_root/hdl/sim/tb_gameboy_memory.sv"

run_test tb_gameboy_save \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_defs_pkg.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_save.sv" \
    "$repo_root/hdl/sim/tb_gameboy_save.sv"

run_test tb_psram_line_service \
    "$repo_root/hdl/common/multitini_psram_line_service.sv" \
    "$repo_root/hdl/sim/tb_psram_line_service.sv"

run_test tb_memory_cdc \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_text_overlay.sv" \
    "$repo_root/hdl/common/multitini_memory_cdc.sv" \
    "$repo_root/hdl/sim/tb_memory_cdc.sv"

# Compile the complete tracked shell. The generated block-design wrapper is a
# Vivado artifact, so -i treats that one module as a black box here.
iverilog -g2012 -Wall -Wimplicit -i -s multitini_one_top \
    -o "$sim_build/multitini_one_top.vvp" \
    "$repo_root/hdl/generated/multitini_regs_pkg.sv" \
    "$repo_root/hdl/core/multitini_core_interface.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_loader.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_content_loader.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_run_injector.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_cart_memory.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_input.sv" \
    "$repo_root/hdl/core/c64/ports/cartridge.sv" \
    "$repo_root/hdl/core/c64/ports/iecdrv_misc.sv" \
    "$repo_root/hdl/core/c64/ports/c1541_logic.sv" \
    "$repo_root/hdl/core/c64/ports/c1541_gcr.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_d64_track.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_g64_track.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_1541.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_cpu_turbo_guard.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_adapter.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_defs_pkg.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_loader.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_clock_enable.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_input.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_memory.sv" \
    "$repo_root/hdl/core/gameboy/multitini_gameboy_save.sv" \
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
    "$repo_root/hdl/common/multitini_border_config.sv" \
    "$repo_root/hdl/board/multitini_border_overlay.sv" \
    "$repo_root/hdl/common/multitini_psram_bist.sv" \
    "$repo_root/hdl/common/multitini_psram_line_service.sv" \
    "$repo_root/hdl/common/multitini_async_mailbox.sv" \
    "$repo_root/hdl/common/multitini_text_overlay.sv" \
    "$repo_root/hdl/common/multitini_memory_cdc.sv" \
    "$repo_root/hdl/common/multitini_cdc_pulse_toggle.sv" \
    "$repo_root/hdl/board/multitini_c64_video_bridge.sv" \
    "$repo_root/hdl/board/multitini_gameboy_video_bridge.sv" \
    "$repo_root/hdl/board/multitini_video_test.sv" \
    "$repo_root/hdl/board/multitini_audio_test.sv" \
    "$repo_root/hdl/board/multitini_one_top.sv" \
    "$repo_root/hdl/sim/xilinx_primitives.sv"
# Two exact pinned cartridge leaves use SystemVerilog constructs unsupported by
# Icarus. Vivado/XSim compile those sources; this portable whole-shell gate
# black-boxes them along with the generated block design.
echo "PASS: complete shell compiles with block design and pinned cartridge leaves black-boxed"
