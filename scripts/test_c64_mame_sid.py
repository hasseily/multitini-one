#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare production SID socket routing with pinned MAME socket methods.

The VHDL wrapper copies the complete SID subsystem verbatim from FPGA64.
Real SID synthesis leaves run in XSim. MAME SID chip doubles record only bus
calls and route gains; this is not a waveform or analog-filter differential.
"""
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
COMMIT = "122719e8d13e2fdb7dd33886455484207de59fc8"
UPSTREAM = ROOT / "third_party/mame_reference/upstream/src/devices/bus/c64"
PINNED = {
    "sid.cpp": "5c04a90a8ffd28196c12c06388062302f6a7bf80625381559ab4f324d63c9ef5",
    "dualsid.cpp": "9f6898a97bcb3090bb3778d940202c0a1da78c3ddf88390044e94d72797693d7",
    "dualsid.h": "1c02853f4e3932a73a9014c2866ecfc475cae63e749d37701c282a0497f95622",
}

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def method(source: str, name: str) -> str:
    match = re.search(r"^(?:void|uint8_t) " + re.escape(name) + r"\([^\n]*\)\n\{", source, re.M)
    if not match:
        raise ValueError("Pinned MAME method missing: " + name)
    depth, cursor = 1, match.end()
    while depth:
        if source[cursor] == "{": depth += 1
        elif source[cursor] == "}": depth -= 1
        cursor += 1
    return source[match.start():cursor]

def subsystem(source: str) -> tuple[str, str]:
    start = source.index("component sid_top\n")
    component = source[start:source.index("end component;", start) + len("end component;")]
    start = source.index("--\tRight SID Port:")
    instance = source.index("sid : sid_top\n", start)
    body = source[start:source.index("\n);", instance) + 3]
    wrapper = """library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.ALL;
use IEEE.numeric_std.all;
entity c64_sid_socket_observed is port(
    clk32, reset, enableSid, memory_wait_q, pulseWr_io, cs_sid, ioe_i, iof_i : in std_logic;
    sid_mode : in unsigned(2 downto 0);
    cpuAddr : in unsigned(15 downto 0);
    cpuDo : in unsigned(7 downto 0);
    sid_ver : in std_logic_vector(1 downto 0);
    sid_do : out unsigned(7 downto 0);
    sid_sel_l, sid_sel_r : out std_logic;
    audio_l, audio_r : out std_logic_vector(17 downto 0));
end entity;
architecture observed of c64_sid_socket_observed is
signal pot_x1,pot_y1,pot_x2,pot_y2 : std_logic_vector(7 downto 0);
signal pot1 : std_logic_vector(7 downto 0) := x"ED";
signal pot2 : std_logic_vector(7 downto 0) := x"CB";
signal pot3,pot4 : std_logic_vector(7 downto 0) := x"00";
signal cia1_pao : std_logic_vector(7 downto 0) := x"40";
signal sid_filter : std_logic_vector(1 downto 0) := "11";
signal sid_cfg : std_logic_vector(3 downto 0) := "0000";
signal sid_fc_off_l,sid_fc_off_r : std_logic_vector(12 downto 0) := (others=>'0');
signal sid_ld_addr : std_logic_vector(11 downto 0) := (others=>'0');
signal sid_ld_data : std_logic_vector(15 downto 0) := (others=>'0');
signal sid_ld_clk : std_logic;
signal sid_ld_wr,sid_digifix,io_ext : std_logic := '0';
signal io_data,io_data_i : unsigned(7 downto 0) := x"FF";
""" + component + "\nbegin\nsid_ld_clk <= clk32;\n" + body + "\nend architecture;\n"
    return wrapper, body

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-mame-sid")
    parser.add_argument("--vivado-bin", type=Path, default=Path("C:/Xilinx/2025.2/Vivado/bin"))
    parser.add_argument("--cxx", type=Path, default=Path("C:/msys64/ucrt64/bin/g++.exe"))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    root_build = (ROOT / "build").resolve()
    if build == root_build or not build.is_relative_to(root_build):
        parser.error("Build directory must be a dedicated child of this checkout's build/")
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"
    receipt.unlink(missing_ok=True)
    production = ROOT / "hdl/core/c64/ports/fpga64_sid_iec.vhd"
    harness = ROOT / "tests/c64_mame/sid_socket_harness.cpp"
    leaves = [ROOT / name for name in (
        "third_party/c64/upstream/rtl/sid/sid_envelope.sv",
        "third_party/c64/upstream/rtl/sid/sid_voice.sv",
        "hdl/core/c64/ports/sid_dac.sv", "hdl/core/c64/ports/sid_filter.sv",
        "hdl/core/c64/ports/sid_tables.sv", "hdl/core/c64/ports/sid_top.sv",
        "hdl/sim/tb_c64_sid_socket.sv")]
    tracked = [Path(__file__).resolve(), production, harness, *leaves, *[UPSTREAM / name for name in PINNED]]
    hashes = lambda: {p.relative_to(ROOT).as_posix(): digest(p) for p in tracked}
    initial = hashes()
    result = {"status": "FAIL", "reference_commit": COMMIT, "source_sha256": initial,
        "limits": ["MAME SID socket call routing and stereo gains only; SID register/audio devices are doubles in the reference.",
                   "Production SID leaves additionally exercise register isolation, paddle reads, stereo output and native enable holds.",
                   "No SID waveform/filter equivalence, whole-machine bus arbitration, routed timing or board acceptance claim."]}
    environment = {k:v for k,v in os.environ.items() if k.upper() != "PATH"}
    environment["PATH"] = str(args.cxx.parent) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    guard = [sys.executable, ROOT / "scripts/run_native_tool.py", "--"]
    def run(name: str, command: list, expected_failure=False) -> str:
        argv = [str(arg) for arg in command]
        child = subprocess.run(argv, cwd=build, env=environment, capture_output=True, text=True,
            errors="replace", timeout=300, creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        output = child.stdout + child.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        failed = bool(child.returncode) or bool(re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I))
        if failed != expected_failure:
            raise RuntimeError(f"{name}: unexpected status {child.returncode}\n{output[-14000:]}")
        return output
    try:
        for name, expected in PINNED.items():
            if digest(UPSTREAM / name) != expected:
                raise ValueError("Pinned MAME source changed: " + name)
        single = (UPSTREAM / "sid.cpp").read_text()
        dual = (UPSTREAM / "dualsid.cpp").read_text()
        inline = re.findall(r"\tbool sid2_selected\(offs_t offset\) const \{[^\n]+\}", (UPSTREAM / "dualsid.h").read_text())
        if len(inline) != 1: raise ValueError("Pinned SID selector changed")
        notice = "// Extracted unchanged from MAME " + COMMIT + ".\n// license:BSD-3-Clause; copyright-holders:Curt Coder\n"
        (build / "mame_sid_select.inc").write_text(notice + inline[0] + "\n")
        bodies = [method(single,"c64_sid_6581_device::"+n) for n in ("read","write")]
        bodies += [method(dual,"c64_sid_dual6581_device::"+n) for n in ("device_reset","read","write")]
        (build / "mame_sid_methods.inc").write_text(notice + "\n\n".join(bodies) + "\n")
        wrapper, block = subsystem(production.read_text())
        observed = build / "c64_sid_socket_observed.vhd"
        observed.write_text(wrapper)
        result["verbatim_production_sid_block_sha256"] = hashlib.sha256(block.encode()).hexdigest()
        vectors = build / "sid_vectors.txt"
        vectors.unlink(missing_ok=True)
        executable = build / "mame_sid_socket.exe"
        executable.unlink(missing_ok=True)
        run("oracle-build", [*guard,args.cxx,"-std=c++17","-O2","-Wall","-Wextra","-I",build,harness,"-o",executable])
        run("oracle-vectors", [*guard,executable,vectors])
        if len(vectors.read_text().splitlines()) != 6144: raise ValueError("Incomplete MAME vectors")
        suffix = ".bat" if os.name=="nt" else ""
        vendor = lambda name: args.vivado_bin / (name + suffix)
        run("xvlog-imported", [*guard,vendor("xvlog"),"--nolog","--sv","--relax",*leaves[:2]])
        run("xvlog", [*guard,vendor("xvlog"),"--nolog","--sv",*leaves[2:]])
        run("xvhdl", [*guard,vendor("xvhdl"),"--nolog","--2008",observed])
        run("xelab", [*guard,vendor("xelab"),"--nolog","--relax","--timescale","1ns/1ps","--snapshot","sid_socket_sim","tb_c64_sid_socket"])
        output = run("simulate", [*guard,vendor("xsim"),"--nolog","sid_socket_sim","--runall"])
        markers = [line for line in output.splitlines() if line.startswith("PASS: C64 SID socket ")]
        if len(markers)!=1 or "vectors=12288 audio_cases=6 filtered_cases=6" not in markers[0]:
            raise ValueError("Missing full SID pass marker")
        lines = vectors.read_text().splitlines()
        fields = lines[3072].split()
        if fields != ["2","1","d400","0"]: raise ValueError("Negative-control target changed")
        fields[-1]="1";lines[3072]=" ".join(fields)
        wrong = build / "sid_vectors_corrupt.txt"
        wrong.write_text("\n".join(lines)+"\n")
        # Keep the default fixture filename to avoid Windows batch rewriting of
        # '=' in simulator plusargs. Always restore the exact oracle bytes.
        golden_vectors = vectors.read_bytes()
        try:
            vectors.write_bytes(wrong.read_bytes())
            negative = run("negative-control", [*guard,vendor("xsim"),"--nolog","sid_socket_sim","--runall"], True)
        finally:
            vectors.write_bytes(golden_vectors)
        if "left select differs from MAME" not in negative:
            raise ValueError("Negative control failed for an unrelated reason")
        if hashes()!=initial: raise ValueError("Reference/production sources changed during test")
        result.update(status="PASS", vectors=6144, register_and_decode_checks=12288, audio_cases=6, filtered_cases=6,
            summary=markers[0], audio_summaries=[line for line in output.splitlines() if line.startswith("SID_AUDIO ")],
            filtered_summaries=[line for line in output.splitlines() if line.startswith("SID_FILTER ")],
            filtered_settling_summaries=[line for line in output.splitlines() if line.startswith("SID_FILTER_SETTLE ")],
            negative_control="PASS: corrupted MAME selected-chip vector rejected", sources_unchanged=True,
            evidence_sha256={name:digest(build/name) for name in ("mame_sid_methods.inc","mame_sid_select.inc",
                "c64_sid_socket_observed.vhd","sid_vectors.txt","sid_vectors_corrupt.txt","simulate.log","negative-control.log")})
        print(markers[0])
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        receipt.write_text(json.dumps(result,indent=2)+"\n")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error),file=sys.stderr)
        raise SystemExit(1)
