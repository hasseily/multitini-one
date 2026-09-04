#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Mixed-language regressions that need XSim. The imported 1541 CPU and VIA
# models are VHDL, which the portable Icarus gate in run_hdl_tests.sh cannot
# elaborate. Requires Vivado 2025.2 xvlog, xvhdl, xelab, and xsim on PATH.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sim_build="$repo_root/build/hdl-xsim"
mkdir -p "$sim_build"

# The XSim console does not turn $fatal into a failing exit status, so each
# run must print exactly one PASS line and no FAIL, Fatal, or Error line.
check_log() {
    log=$1
    grep -q "^PASS: " "$log"
    if grep -q "^FAIL: \|Fatal\|ERROR\|Error" "$log"; then
        echo "FAIL: $log reports an error" >&2
        exit 1
    fi
}

cd "$sim_build"

# The exact pinned direct-GCR leaf needs the same relaxed analysis as in the
# vendor flow; every Multitini source keeps strict checking.
xvlog --nolog --sv --relax \
    "$repo_root/third_party/c64/upstream/rtl/iec_drive/lfsr.v" \
    "$repo_root/third_party/c64/upstream/rtl/iec_drive/c1541_direct_gcr.sv"
xvlog --nolog --sv \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/ports/iecdrv_misc.sv" \
    "$repo_root/hdl/core/c64/ports/c1541_logic.sv" \
    "$repo_root/hdl/core/c64/ports/c1541_gcr.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_d64_track.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_g64_track.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_1541.sv" \
    "$repo_root/hdl/sim/tb_c64_parallel_cable.sv"
xvhdl --nolog --2008 \
    "$repo_root/third_party/c64/upstream/rtl/t65/T65_Pack.vhd" \
    "$repo_root/third_party/c64/upstream/rtl/t65/T65_ALU.vhd" \
    "$repo_root/third_party/c64/upstream/rtl/t65/T65_MCode.vhd" \
    "$repo_root/third_party/c64/upstream/rtl/t65/T65.vhd" \
    "$repo_root/third_party/c64/upstream/rtl/iec_drive/iecdrv_via6522.vhd"
# Pinned leaves carry no timescale; give them the shell default.
xelab --nolog --timescale 1ns/1ps -s tb_c64_parallel_cable_sim \
    tb_c64_parallel_cable
xsim --nolog -R tb_c64_parallel_cable_sim | tee tb_c64_parallel_cable.log
check_log tb_c64_parallel_cable.log
