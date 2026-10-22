#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production C64 machine with local RAM and original synthetic ROMs."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def synthetic_memory() -> bytes:
    memory = bytearray([0xEA] * 65536)
    program = bytes.fromhex(
        "78 D8 A9 2F 85 00 A9 37 85 01 "
        "A9 00 8D 11 D0 A9 0B 8D 20 D0 "
        "A9 4E 8D 00 D4 A9 07 8D 00 D8 A9 80 8D 0D DC "
        "A9 11 8D 00 20 EE 00 20 0F 00 20 8D 00 21 "
        "AD 00 A0 8D 01 21 A9 30 85 01 A9 A6 8D 00 A0 AD 00 A0 8D 02 21 "
        "A9 33 85 01 AD 00 D0 8D 03 21 A9 37 85 01 "
        "AD 00 D8 29 0F 8D 04 21 A5 00 8D 06 21 A5 01 8D 07 21 "
        "A9 A5 8D 05 21"
    )
    stop = 0x0800 + len(program)
    memory[0x0800:stop + 3] = program + bytes([0x4C, stop & 255, stop >> 8])
    return bytes(memory)


def instrument(source: str) -> str:
    """Add output declarations and assignments only; preserve the RTL body."""
    ports = """
    dbg_phase : out std_logic_vector(4 downto 0);
    dbg_cpu_addr : out std_logic_vector(15 downto 0);
    dbg_cpu_di, dbg_cpu_do, dbg_cpu_port, dbg_vic_bus : out std_logic_vector(7 downto 0);
    dbg_cpu_we, dbg_request, dbg_retire, dbg_reset, dbg_hold : out std_logic;
    dbg_rdy, dbg_cpu_owned, dbg_dma : out std_logic;
    dbg_native_ce : out std_logic_vector(5 downto 0);
    dbg_io_cs : out std_logic_vector(4 downto 0);
    dbg_io_write : out std_logic;
"""
    assignments = """
    dbg_phase <= std_logic_vector(to_unsigned(sysCycleDef'pos(sysCycle), 5));
    dbg_cpu_addr <= std_logic_vector(cpuAddr_pre);
    dbg_cpu_di <= std_logic_vector(observedCpuRead);
    dbg_cpu_do <= std_logic_vector(cpuDo_pre);
    dbg_cpu_port <= std_logic_vector(cpuIO);
    dbg_vic_bus <= std_logic_vector(lastVicDi);
    dbg_cpu_we <= cpuWe_pre;
    dbg_request <= cpu_cyc;
    dbg_retire <= enableCpu and not dma_active and not memory_wait_q;
    dbg_reset <= reset;
    dbg_hold <= memory_wait_q;
    dbg_rdy <= baLoc;
    dbg_cpu_owned <= cpuHasBus;
    dbg_dma <= dma_active;
    dbg_native_ce <= '0' & (enablePixel and not memory_wait_q) &
        (enableVic and not memory_wait_q) & (enableCia_n and not memory_wait_q) &
        (enableCia_p and not memory_wait_q) & (enableSid and not memory_wait_q);
    dbg_io_cs <= cs_cia2 & cs_cia1 & cs_color & cs_sid & cs_vic;
    dbg_io_write <= pulseWr_io;
"""
    marker = "port(\n"
    if source.count(marker) != 1:
        raise ValueError("FPGA64 entity port marker changed")
    generated = source.replace(marker, marker + ports, 1)
    declaration = "signal observedCpuRead : unsigned(7 downto 0);\n"
    connection = "\tdebug_read => observedCpuRead,\n"
    generated = generated.replace("\nbegin\n", "\n" + declaration + "begin\n" + assignments, 1)
    generated = generated.replace("\twe => cpuWe_pre,\n", "\twe => cpuWe_pre,\n" + connection, 1)
    if generated.replace(ports, "", 1).replace(assignments, "", 1).replace(declaration, "", 1).replace(connection, "", 1) != source:
        raise ValueError("Observation instrumentation changed production RTL")
    return generated


def instrument_wrapper(source: str) -> str:
    port = "\t\tdebug_read : out unsigned(7 downto 0);\n"
    assignment = "\tdebug_read <= unsigned(localDi);\n"
    if source.count("\tport (\n") != 1:
        raise ValueError("6510 wrapper port marker changed")
    generated = source.replace("\tport (\n", "\tport (\n" + port, 1)
    generated = generated.replace("\nbegin\n", "\nbegin\n" + assignment, 1)
    if generated.replace(port, "", 1).replace(assignment, "", 1) != source:
        raise ValueError("Observation instrumentation changed wrapper RTL")
    return generated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-machine-schedule")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado")) / "bin")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"
    receipt.unlink(missing_ok=True)
    production = ROOT / "hdl/core/c64/ports/fpga64_sid_iec.vhd"
    observed = build / "fpga64_sid_iec_observed.vhd"
    observed.write_text(instrument(production.read_text(encoding="utf-8")), encoding="utf-8")
    wrapper = ROOT / "hdl/core/c64/ports/cpu_6510.vhd"
    observed_wrapper = build / "cpu_6510_observed.vhd"
    observed_wrapper.write_text(instrument_wrapper(wrapper.read_text(encoding="utf-8")), encoding="utf-8")
    (build / "machine_memory.hex").write_text("\n".join(f"{b:02x}" for b in synthetic_memory()) + "\n")

    vhdl = []
    section = ""
    for raw in (ROOT / "hdl/hdl_sources.txt").read_text().splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line
        elif section == "[c64]" and line and not line.startswith("#"):
            path = (ROOT / "hdl" / line.split("|")[0].strip()).resolve()
            if path.suffix == ".vhd":
                vhdl.append(path)
    sv = [ROOT / path for path in (
        "third_party/c64/upstream/rtl/mos6526.v",
        "hdl/core/c64/ports/sid_dac.sv", "hdl/core/c64/ports/sid_filter.sv",
        "third_party/c64/upstream/rtl/sid/sid_envelope.sv",
        "hdl/core/c64/ports/sid_tables.sv", "third_party/c64/upstream/rtl/sid/sid_voice.sv",
        "hdl/core/c64/ports/sid_top.sv", "hdl/core/c64/multitini_c64_local_ram.sv",
        "hdl/sim/tb_c64_machine_schedule.sv")]
    tracked = [Path(__file__).resolve(), ROOT / "hdl/hdl_sources.txt", *vhdl, *sv]
    before = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in tracked}
    vhdl = [observed if path == production else observed_wrapper if path == wrapper else path for path in vhdl]
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = "C:/msys64/ucrt64/bin" + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    suffix = ".bat" if os.name == "nt" else ""

    def run(name: str, command: list[object]) -> str:
        argv = [arg.as_posix() if isinstance(arg, Path) else str(arg) for arg in command]
        result = subprocess.run(argv, cwd=build, env=environment, capture_output=True,
                                text=True, errors="replace", timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I):
            raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-14000:]}")
        return output

    # Pinned CIA/SID leaves have forward declarations accepted by synthesis.
    # Relax only imported leaves; keep all Multitini ports/tests strict.
    imported = [path for path in sv if "third_party" in path.parts]
    local = [path for path in sv if "third_party" not in path.parts]
    run("xvlog_imported", [args.vivado_bin / ("xvlog" + suffix), "--nolog", "--sv", "--relax", *imported])
    run("xvlog", [args.vivado_bin / ("xvlog" + suffix), "--nolog", "--sv", *local])
    run("xvhdl", [args.vivado_bin / ("xvhdl" + suffix), "--nolog", "--2008", *vhdl])
    top = "tb_c64_machine_schedule"
    run("xelab", [args.vivado_bin / ("xelab" + suffix), "--nolog", "--relax", "--timescale", "1ns/1ps",
                  "--snapshot", top + "_sim", top])
    output = run("simulate", [args.vivado_bin / ("xsim" + suffix), "--nolog", top + "_sim", "--runall"])
    passes = [line for line in output.splitlines() if line.startswith("PASS: C64 production machine")]
    if len(passes) != 1:
        raise RuntimeError("Production machine regression lacks explicit pass marker")
    after = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in tracked}
    if before != after:
        raise RuntimeError("Production machine simulation sources changed during validation")
    summaries = [line for line in output.splitlines() if line.startswith("MACHINE_SUMMARY ")]
    receipt.write_text(json.dumps({"schema": 1, "status": "PASS", "source_sha256": before,
        "observation_only_copy_sha256": hashlib.sha256(observed.read_bytes()).hexdigest(),
        "observation_only_wrapper_sha256": hashlib.sha256(observed_wrapper.read_bytes()).hexdigest(),
        "checks": summaries, "summary": passes[0],
        "limits": "Real machine RTL simulation with synthetic program; bounded MAME behavior checks are separate, and full MAME/game compatibility and physical timing are not established."}, indent=2) + "\n")
    print("\n".join(summaries + passes))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
