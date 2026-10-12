#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test Genesis packed download through the real shell/reset/abort crossings."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "hdl/generated/multitini_regs_pkg.sv",
    "hdl/core/genesis/multitini_genesis_defs_pkg.sv",
    "hdl/common/multitini_control_regs.sv",
    "hdl/common/multitini_async_mailbox.sv",
    "hdl/common/multitini_cdc_pulse_toggle.sv",
    "third_party/appletini/hdl/cdc_bit_sync.sv",
    "hdl/core/genesis/multitini_genesis_loader.sv",
    "hdl/sim/xilinx_primitives.sv",
    "hdl/sim/tb_genesis_stream_loader.sv",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / "build/genesis-stream-loader-tests")
    parser.add_argument("--simulator", choices=("verilator", "iverilog"),
                        default="verilator")
    parser.add_argument("--actual-adapter", action="store_true",
                        help="include real chipset/cache/memory CDC and PSRAM line service")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    validation = output / "validation.json"
    validation.unlink(missing_ok=True)
    sources = [ROOT / item for item in SOURCES]
    tracked_helpers = [Path(__file__).resolve()]
    if args.actual_adapter:
        from lint_genesis import manifest_sources
        sources += manifest_sources() + [ROOT / "hdl/sim/genesis_primitives.sv"]
        sources = list(dict.fromkeys(sources))
        tracked_helpers += [ROOT / "scripts/lint_genesis.py", ROOT / "hdl/hdl_sources.txt"]
        for source in sources:
            if source.suffix in (".txt", ".mem"):
                shutil.copyfile(source, output / source.name)
    tracked = sources + tracked_helpers
    hdl = [str(source) for source in sources if source.suffix in (".v", ".sv")]

    def hashes() -> dict[str, str]:
        return {item.relative_to(ROOT).as_posix(): hashlib.sha256(item.read_bytes()).hexdigest()
                for item in tracked}

    before = hashes()
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))

    def tool(name: str) -> str:
        path = str(runtime / f"{name}.exe") if os.name == "nt" else shutil.which(name)
        if path is None:
            raise FileNotFoundError(f"Test requires {name}")
        return path

    environment = os.environ.copy()
    if args.simulator == "verilator":
        compiler = tool("verilator_bin") if os.name == "nt" else tool("verilator")
        objects = output / "obj"
        binary = objects / ("Vtb_genesis_stream_loader" + (".exe" if os.name == "nt" else ""))
        commands = [[compiler, "--binary", "--timing", "--top-module", "tb_genesis_stream_loader",
                     "-Wno-fatal", "--build-jobs", "2", "--Mdir", str(objects),
                     *(["-DMT_GENESIS_ACTUAL_TEST"] if args.actual_adapter else []),
                     f"-GACTUAL_ADAPTER={int(args.actual_adapter)}", *hdl], [str(binary)]]
        if os.name == "nt":
            environment["PATH"] = str(runtime) + os.pathsep + str(runtime.parent.parent / "usr/bin") + os.pathsep + environment.get("PATH", "")
            environment.setdefault("VERILATOR_ROOT", str(runtime.parent / "share/verilator"))
    else:
        binary = output / "tb_genesis_stream_loader.vvp"
        commands = [[tool("iverilog"), "-g2012", "-Wall", "-s", "tb_genesis_stream_loader",
                     *(["-DMT_GENESIS_ACTUAL_TEST"] if args.actual_adapter else []),
                     f"-Ptb_genesis_stream_loader.ACTUAL_ADAPTER={int(args.actual_adapter)}", "-o", str(binary), *hdl],
                    [tool("vvp"), str(binary)]]
    for stage, command in zip(("compile", "simulate"), commands):
        log = output / f"{stage}.log"
        with log.open("w") as stream:
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", *command],
                cwd=output, env=environment, stdout=stream, stderr=subprocess.STDOUT)
        text = log.read_text(errors="replace")
        if result.returncode or (stage == "simulate" and "PASS Genesis shell download" not in text):
            print(text[-12000:])
            raise RuntimeError(f"Genesis shell download {stage} failed: {log}")
        if stage == "simulate":
            print(text.strip())
    if hashes() != before:
        raise RuntimeError("Test sources changed during execution")
    validation.write_text(json.dumps({
        "schema": 1, "passed": True, "simulator": args.simulator,
        "actual_adapter": args.actual_adapter, "source_sha256": before,
        "commands": commands,
        "limits": ("Actual chipset/cache/memory CDC/PSRAM line service; device command completion remains modeled. No PSRAM PHY, vendor timing or board validation."
                   if args.actual_adapter else "Portable mailbox model and delayed physical-write model. No chipset, vendor timing, PSRAM PHY, or board validation."),
    }, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
