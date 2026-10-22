#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise local C64 RAM, scheduler and the production NMOS 6510 bus."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def synthetic_memory() -> bytes:
    """Original test program; requires no Commodore ROM or external assets."""
    memory = bytearray([0xEA] * 65536)
    program = bytes.fromhex(
        "78 D8 "                  # SEI, CLD
        "A5 00 8D 0C 02 A5 01 8D 0D 02 "  # Read both 6510 registers before writes.
        "A9 2F 85 00 A9 15 85 01 "  # Set 6510 DDR and port latch.
        "A5 00 8D 00 02 A5 01 8D 01 02 "
        "A9 7F 8D 00 20 EE 00 20 0E 00 20 "  # NMOS INC and ASL.
        "A9 81 8D 00 20 A9 01 0F 00 20 8D 02 02 "  # Undocumented SLO.
        "08 68 8D 03 02 E6 01 6C FF 12"  # Port RMW, NMOS indirect JMP wrap.
    )
    memory[0x0800:0x0800 + len(program)] = program
    extended = bytes.fromhex(
        "F8 18 A9 45 69 55 8D 05 02 08 68 8D 06 02 "  # NMOS decimal flags.
        "38 A9 00 E9 01 8D 0A 02 08 68 8D 0B 02 D8 "
        "A2 01 BD FF 20 8D 07 02 A9 10 9D FF 20 "  # Indexed page crossing.
        "A2 FF A1 40 8D 08 02 A0 01 B1 FE 8D 09 02 "  # ZP wrap and indirect Y.
        "A9 81 85 42 06 42 46 42 26 42 66 42 C6 42 E6 42 "
        "1C FF 20 4C 10 0C"  # Undocumented absolute-X NOP, page-crossing branches.
    )
    memory[0x0934:0x0934 + len(extended)] = extended
    memory[0x003F:0x0041] = bytes.fromhex("00 30")
    memory[0x00FE:0x0100] = bytes.fromhex("FF 30")
    memory[0x2100], memory[0x3000], memory[0x3100] = 0x9A, 0xCD, 0x34
    memory[0x0C10:0x0C14] = bytes.fromhex("A9 01 D0 E8")  # Backward crossing to $0BFC.
    memory[0x0BFC:0x0BFF] = bytes.fromhex("4C FB 0A")
    memory[0x0AFB:0x0AFF] = bytes.fromhex("A9 01 D0 02")  # Forward crossing to $0B01.
    ending = bytes.fromhex("A9 00 D0 02 F0 02 EA EA A9 A5 8D 04 02 4C 0E 0B")
    memory[0x0B01:0x0B01 + len(ending)] = ending  # Not-taken then same-page taken.
    memory[0x12FF], memory[0x1200], memory[0x1300] = 0x34, 0x09, 0x08
    memory[0xFFFC:0xFFFE] = bytes.fromhex("00 08")
    return bytes(memory)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="append", choices=("local-ram", "cpu", "scheduler"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-acceleration-tests")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado")) / "bin")
    args = parser.parse_args()
    tests = args.test or ["local-ram", "cpu", "scheduler"]
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"
    receipt.unlink(missing_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = str(runtime) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    suffix = ".bat" if os.name == "nt" else ""
    sources: set[Path] = {Path(__file__).resolve()}
    initial_hashes: dict[str, str] = {}

    def capture(paths: list[Path]) -> None:
        for path in paths:
            sources.add(path)
            initial_hashes[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()

    capture([Path(__file__).resolve()])

    def run(name: str, command: list[object], directory: Path, timeout: int = 300) -> str:
        strings = [item.as_posix() if isinstance(item, Path) else str(item) for item in command]
        result = subprocess.run(strings, cwd=directory, env=environment, capture_output=True,
                                text=True, errors="replace", timeout=timeout)
        output = result.stdout + result.stderr
        (directory / (name + ".log")).write_text(output, encoding="utf-8")
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I):
            raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-16000:]}")
        return output

    def xsim(directory: Path, top: str, sv: list[Path], vhdl: list[Path], plusargs: list[str] | None = None) -> str:
        capture(sv + vhdl)
        if sv:
            run("xvlog", [args.vivado_bin / ("xvlog" + suffix), "--nolog", "--sv", *sv], directory)
        if vhdl:
            run("xvhdl", [args.vivado_bin / ("xvhdl" + suffix), "--nolog", "--2008", *vhdl], directory)
        run("xelab", [args.vivado_bin / ("xelab" + suffix), "--nolog", "--timescale", "1ns/1ps",
                      "--snapshot", top + "_sim", top], directory)
        command: list[object] = [args.vivado_bin / ("xsim" + suffix), "--nolog", top + "_sim", "--runall"]
        for item in plusargs or []:
            command.extend(["--testplusarg", item])
        return run("simulate", command, directory)

    results = []
    for test in tests:
        directory = build / test
        directory.mkdir(exist_ok=True)
        if test == "local-ram":
            selected = [ROOT / "hdl/core/c64/multitini_c64_local_ram.sv", ROOT / "hdl/sim/tb_c64_local_ram.sv"]
            capture(selected)
            executable = directory / "test.vvp"
            guard = [sys.executable, ROOT / "scripts/run_native_tool.py", "--"] if os.name == "nt" else []
            tool = runtime / "iverilog.exe" if os.name == "nt" else "iverilog"
            run("compile", [*guard, tool, "-g2012", "-s", "tb_c64_local_ram", "-o", executable, *selected], directory)
            tool = runtime / "vvp.exe" if os.name == "nt" else "vvp"
            output = run("simulate", [*guard, tool, executable], directory)
        elif test == "cpu":
            memory = synthetic_memory()
            memory_file = directory / "memory.hex"
            memory_file.write_text("\n".join(f"{value:02x}" for value in memory) + "\n")
            (directory / "memory.bin").write_bytes(memory)
            vhdl = [ROOT / f"third_party/c64/upstream/rtl/t65/T65{part}.vhd" for part in ("_Pack", "_ALU", "_MCode")]
            vhdl.append(ROOT / "hdl/core/c64/ports/T65.vhd")
            vhdl.append(ROOT / "hdl/core/c64/multitini_c64_6510_port.vhd")
            wrapper = ROOT / "hdl/core/c64/ports/cpu_6510.vhd"
            capture([wrapper])
            source = wrapper.read_text()
            declaration = "\t\tdoIO    : out unsigned(7 downto 0)"
            if source.count(declaration) != 1 or source.count("end architecture;") != 1:
                raise RuntimeError("6510 observation-port insertion boundary changed")
            # Expose the CPU's actual read bus. The complete production logic
            # remains unchanged; no behavioral proxy recreates port readback.
            observed_wrapper = directory / "cpu_6510_observed.vhd"
            observed_wrapper.write_text(source.replace(declaration, declaration + ";\n\t\ttest_data : out unsigned(7 downto 0)")
                .replace("end architecture;", "test_data <= unsigned(localDi);\nend architecture;"))
            vhdl.append(observed_wrapper)
            output = xsim(directory, "tb_c64_6510_bus", [ROOT / "hdl/sim/tb_c64_6510_bus.sv"], vhdl)
            traces: dict[int, list[tuple[str, str, str]]] = {0: [], 1: []}
            for match in re.finditer(r"^BUS,(\d+),\d+,([0-9a-f]+),([RW]),([0-9a-f]+)$", output, re.M | re.I):
                case, address, rw, data = match.groups()
                traces[int(case)].append((address.lower(), rw, data.lower()))
            if not traces[0] or traces[0] != traces[1]:
                raise RuntimeError("Native and interrupted accelerated 6510 accepted bus traces differ")
            (directory / "trace.json").write_text(json.dumps(traces[0], indent=2) + "\n")
            oracle = ROOT / "scripts/c64_mame_oracle.py"
            reference_root = ROOT / "third_party/mame_reference"
            reference_manifest = reference_root / "import_manifest.json"
            manifest = json.loads(reference_manifest.read_text())
            capture([oracle, reference_manifest, ROOT / "tests/c64_mame/oracle_harness.cpp",
                     ROOT / "tests/c64_mame/memory_harness.cpp",
                     *[reference_root / "upstream" / item["path"] for item in manifest["files"]]])
            reference_trace = directory / "mame_trace.csv"
            run("mame-oracle", [sys.executable, oracle, "--image", directory / "memory.bin",
                               "--build-dir", directory / "mame-oracle", "--trace", reference_trace],
                directory)
            with reference_trace.open(newline="") as stream:
                reference = [(row["address"].lower(), row["rw"], row["data"].lower())
                             for row in csv.DictReader(stream)]
            if traces[0] != reference:
                differences = [(index, dut, gold) for index, (dut, gold) in enumerate(zip(traces[0], reference)) if dut != gold]
                raise RuntimeError(f"Production 6510 differs from pinned MAME: HDL={len(traces[0])} "
                                   f"MAME={len(reference)} first differences={differences[:5]}")
            print(f"MAME 6510: {len(reference)} accepted bus accesses match in both clock schedules", flush=True)
        else:
            output = xsim(directory, "tb_c64_cpu_scheduler", [ROOT / "hdl/sim/tb_c64_cpu_scheduler.sv"],
                          [ROOT / "hdl/core/c64/multitini_c64_cpu_scheduler.vhd"])
        passes = [line for line in output.splitlines() if line.startswith("PASS: C64 ")]
        if len(passes) != 1:
            raise RuntimeError(f"{test} lacks its single explicit pass marker")
        print(passes[0], flush=True)
        results.append({"name": test, "status": "PASS", "summary": passes[0]})

    after = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    if initial_hashes != after:
        changed = [path for path in initial_hashes if initial_hashes[path] != after.get(path)]
        raise RuntimeError("C64 simulation inputs changed during validation: " + ", ".join(changed))
    receipt.write_text(json.dumps({"schema": 1, "status": "PASS", "checks": results,
        "source_sha256": initial_hashes,
        "limits": ["Functional simulation only; integrated timing and physical-board acceptance are separate.",
                   "Standalone CPU and RAM checks do not establish whole-machine MAME equivalence."]}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
