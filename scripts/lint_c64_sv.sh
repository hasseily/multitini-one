#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if ! command -v verilator >/dev/null 2>&1; then
    echo "ERROR: Verilator is required for C64 SystemVerilog lint" >&2
    exit 1
fi

# These warnings describe deliberate upstream style or explicit narrowing.
# Structural issues such as out-of-range selects and procedural net writes are
# not disabled and remain fatal.
lint_common="--lint-only --language 1800-2023 -Wall \
    -Wno-BLKSEQ -Wno-CASEINCOMPLETE -Wno-GENUNNAMED \
    -Wno-IMPLICITSTATIC -Wno-PINCONNECTEMPTY -Wno-PINMISSING \
    -Wno-PROCASSINIT \
    -Wno-TIMESCALEMOD -Wno-UNUSEDPARAM -Wno-UNUSEDSIGNAL \
    -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC"

# shellcheck disable=SC2086
verilator $lint_common --top-module sid_top \
    "$repo_root/hdl/core/c64/ports/sid_dac.sv" \
    "$repo_root/hdl/core/c64/ports/sid_filter.sv" \
    "$repo_root/third_party/c64/upstream/rtl/sid/sid_envelope.sv" \
    "$repo_root/hdl/core/c64/ports/sid_tables.sv" \
    "$repo_root/third_party/c64/upstream/rtl/sid/sid_voice.sv" \
    "$repo_root/hdl/core/c64/ports/sid_top.sv"

# shellcheck disable=SC2086
verilator $lint_common --top-module mos6526 \
    "$repo_root/third_party/c64/upstream/rtl/mos6526.v"

# shellcheck disable=SC2086
verilator $lint_common --top-module multitini_c64_loader \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_loader.sv"

# shellcheck disable=SC2086
verilator $lint_common --top-module multitini_c64_input \
    "$repo_root/hdl/core/c64/multitini_c64_defs_pkg.sv" \
    "$repo_root/hdl/core/c64/multitini_c64_input.sv"

echo "PASS: C64 SystemVerilog lint"
