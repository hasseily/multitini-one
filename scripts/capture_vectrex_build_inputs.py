#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture or verify exact source bytes for a selected Multitini FPGA build."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import io
import tarfile
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(machine: str, *, amiga_ap68040: bool = False) -> list[Path]:
    selected = {"common", machine.lower()}
    if amiga_ap68040:
        selected.add("amiga_ap68040")
    paths = {
        ROOT / "hdl/hdl_sources.txt",
        ROOT / "scripts/create_project.tcl",
        ROOT / "scripts/build_and_export_xsa.tcl",
        ROOT / "scripts/run_native_tool.py",
        *[ROOT / "scripts" / name for name in (
            "configure_vivado_run_profile.tcl", "vivado_run_threads.tcl",
            "optimize_c64_control_sets.tcl",
            "c64_timing_policy.tcl",
            "finish_c64_timing.tcl", "optimize_c64_cpu_margin.tcl",
            "c64_placement_margin.tcl", "apply_c64_placement_margin.tcl",
            "apply_fabric_timing_margin.tcl", "clear_fabric_timing_margin.tcl",
            "finish_video_timing.tcl", "check_video_bus_constraints.tcl",
            "check_timing_coverage.tcl", "check_w5100s_constraints.tcl")],
        Path(__file__).resolve(),
    }
    # Redistributable build snapshots include the rules and source credits.
    paths.update(path for base in ("licenses", "provenance", "spec")
                 for path in (ROOT / base).rglob("*") if path.is_file())
    paths.update((ROOT / "LICENSE", ROOT / "MULTITINI_ONE_PLAN.md"))
    if machine == "C64":
        # Retain complete firmware dependencies of extension, network and
        # integration tests rather than a partial list of transitive headers.
        paths.update(path for path in (ROOT / "firmware").rglob("*")
                     if path.is_file() and path.suffix in {".c", ".h", ".inc"})
        paths.update(ROOT / name for name in (
            "CMakeLists.txt", "scripts/test_network.py", "scripts/test_w5100s_bridge.py",
            "scripts/port_c64_sid.py", "tests/test_timing_coverage.py",
            "hdl/sim/tb_multitini_w5100s_bridge.sv",
            "tests/host/net_model.h", "tests/host/test_net.c", "tests/host/test_net_service.c",
            "tests/host/test_net_edges.c", "tests/host/test_http_json_alloc.c",
            "tests/host/test_net_uci_http.c", "tests/host/c64_uci_storage_fixture.h",
            "tests/host/c64_sid_ultimate_oracle.cpp", "tests/host/c64_sid_presets.c", "tests/host/c64_title_runtime.inc",
            "tests/host/c64_uci_quiesce_cases.inc",
            "third_party/c64/upstream/rtl/sid/sid_top.sv",
            "third_party/c64/upstream/rtl/sid/sid_voice.sv"))
        paths.update(path for base in ("third_party/ultimate_firmware_reference", "third_party/c64/patches")
                     for path in (ROOT / base).rglob("*") if path.is_file())
        paths.update((ROOT / "docs").glob("C64_*.md"))
        paths.update((ROOT / "scripts").glob("test_c64_*.py"))
        paths.update((ROOT / "scripts").glob("check_c64_*.tcl"))
        paths.add(ROOT / "scripts/ua_model.py")
        # The real-KERNAL IEC test loads private ROMs at runtime; capture only
        # its simulator, portable DOS engine and host scaffolding dependencies.
        paths.update((ROOT / "scripts/c64sim").glob("*.py"))
        paths.add(ROOT / "scripts/c64sim/README.md")
        paths.add(ROOT / "scripts/private_core_roms.py")
        paths.update(ROOT / name for name in (
            "tests/host/c64_iec_kernal_backend.c", "tests/host/c64_iec_fixture.h",
            "firmware/common/c64_iec_dos.c", "firmware/include/multitini_c64_iec_dos.h",
            "firmware/platform/xilinx/c64_iec_service.c",
            "firmware/platform/xilinx/c64_iec_service.h",
            "firmware/platform/xilinx/main.c",
            "firmware/platform/xilinx/sd_storage.h",
            "firmware/include/multitini_core_link.h",
            "firmware/include/multitini_regs.h", "firmware/include/multitini_c64_interface.h"))
        paths.update((ROOT / "firmware/include").glob("multitini_*_interface.h"))
        paths.update(path for path in (ROOT / "tests/xilinx_stubs").glob("*.h"))
        paths.add(ROOT / "hdl/sim/c64_ua_upstream_bridge.vhd")
        paths.add(ROOT / "hdl/sim/c64_uci_upstream_bridge.vhd")
        paths.add(ROOT / "hdl/sim/c64_ua_reu_contention.inc")
        paths.add(ROOT / "hdl/sim/tb_multitini_audio_capture.sv")
        paths.add(ROOT / "hdl/sim/tb_multitini_word_mailbox.sv")
        for base in ("third_party/ultimate_reference", "tests/c64_ua", "tests/c64_turbo", "c64ua"):
            paths.update(path for path in (ROOT / base).rglob("*")
                         if path.is_file() and "__pycache__" not in path.parts)
        paths.update((ROOT / "tests").glob("test_c64_*.py"))
        paths.update((ROOT / "tests/host").glob("test_c64_*.c"))
        paths.add(ROOT / "docs/C64_RAM_CPU.md")
        paths.add(ROOT / "docs/C64_EXPANSIONS.md")
        paths.add(ROOT / "docs/C64_VICE310_COMPARISON.md")
        paths.add(ROOT / "third_party/c64/import_manifest.json")
        # Retain the exact originals alongside the local conformance ports.
        paths.update(ROOT / "third_party/c64/upstream/rtl" / name for name in (
            "t65/T65.vhd", "cpu_6510.vhd", "fpga64_sid_iec.vhd",
            "fpga64_rgbcolor.vhd", "video_vicII_656x.vhd", "video_sync.vhd"))
        paths.update(path for path in (ROOT / "third_party/mame_reference").rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
        paths.update(path for path in (ROOT / "tests/c64_mame").rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
        paths.update(path for base in ("third_party/vice_reference", "tests/c64_vice")
                     for path in (ROOT / base).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
        paths.update((ROOT / "scripts").glob("*c64_vice*.py"))
        paths.update(ROOT / "scripts" / name for name in (
            "c64_mame_oracle.py", "test_c64_acceleration.py",
            "test_c64_reu.py", "test_c64_reu_machine.py",
            "test_c64_ddr.py", "test_c64_reu_cache.py", "test_c64_reu_native.py",
            "test_c64_expansion_infrastructure.py",
            "check_c64_local_ram_synthesis.tcl", "port_c64_vic.py", "port_c64_cia.py"))
        paths.add(ROOT / "third_party/c64/upstream/rtl/mos6526.v")
        paths.add(ROOT / "hdl/sim/tb_control_regs.sv")
        paths.update((ROOT / "scripts").glob("test_c64_machine*.py"))
        paths.update((ROOT / "scripts").glob("*c64_mame*.py"))
        paths.update((ROOT / "hdl/sim").glob("tb_c64_*.sv"))
    if machine == "GENESIS":
        # Preserve enclosed license notices, exact originals for every helper
        # port, and the scripts that reproduce those ports in public bundles.
        paths.update(path for path in (ROOT / "third_party/genesis").rglob("*")
                     if path.is_file())
        paths.update(ROOT / "scripts" / name for name in (
            "generate_genesis_interface.py", "port_genesis_core.py", "audit_genesis_import.py",
            "test_genesis_vdp_pair.py", "test_genesis.py", "lint_genesis.py"))
        paths.update(ROOT / "hdl/sim" / name for name in (
            "xilinx_primitives.sv", "genesis_primitives.sv", "video_fifo_model.sv"))
        paths.update((ROOT / "hdl/sim").glob("tb_genesis_*.sv"))
    if machine == "APPLE_IIGS":
        paths.add(ROOT / "docs/APPLE_IIGS_LEGACY_VIDEO.md")
        paths.add(ROOT / "docs/APPLE_IIGS_DOOM_GS.md")
        paths.update(ROOT / name for name in (
            "docs/APPLE_IIGS_MMU_AUDIT.md", "docs/APPLE_IIGS_ARKANOID.md",
            "hdl/sim/apple_iigs_mmu_rom3_capture.vh",
            "hdl/sim/fixtures/iigs_mmu_rom03.json"))
        paths.update((ROOT / "scripts").glob("test_apple_iigs_mmu_*.py"))
        # Keep the native observation program and decoder with its HDL fixture.
        # Generated captures, private ROMs and Python caches are not source.
        paths.update(path for path in (ROOT / "tools/iigs-mmu-probe").iterdir()
                     if path.is_file() and (path.suffix in {".py", ".s", ".cfg", ".md", ".txt"}
                                            or path.name == "LICENSE"))
        paths.update(path for path in (ROOT / "third_party/apple-iigs").rglob("*")
                     if path.is_file())
        paths.update(path for path in (ROOT / "hdl/core/apple_iigs/roms").rglob("*")
                     if path.is_file())
        paths.update((ROOT / "scripts").glob("*apple_iigs_smartport*.py"))
        paths.update((ROOT / "scripts").glob("test_apple_iigs_fast_floppy*.py"))
        paths.update((ROOT / "hdl/sim").glob("apple_iigs_smartport*.cpp"))
        paths.update(ROOT / "scripts" / name for name in (
            "port_apple_iigs_core.py", "audit_apple_iigs_import.py", "test_apple_iigs_video.py",
            "test_apple_iigs_video_bridge.py",
            "test_apple_iigs_shr_edges.py",
            "test_apple_iigs_legacy_video.py", "synth_apple_iigs_legacy_video.tcl",
            "test_apple_iigs_slowram.py",
            "test_apple_iigs_logical_cache.py",
            "test_apple_iigs_cpu_peek.py",
            "test_apple_iigs_boot.py",
            "test_apple_iigs_irq_dispatch.py",
            "test_apple_iigs_iolc_vectors.py",
            "test_apple_iigs_adb_empty_talk.py",
            "test_apple_iigs_iwm.py",
            "test_apple_iigs_accel_gates.py",
            "test_apple_iigs_paused_settings.py",
            "test_apple_iigs_ports.py",
            "test_apple_iigs_native_clock.py",
            # Memory-path evidence: cache, posted writes, burst fills, the
            # machine-clock HP2 master and the accelerated bus phases.
            "test_apple_iigs_runtime.py",
            "test_apple_iigs_adapter.py",
            "test_gs_memory_phases.py",
            "test_gs_acceleration.py",
            "test_gs_external_cpu.py",
            "check_apple_iigs_phase_constraints.tcl"))
        paths.add(ROOT / "hdl/sim/apple_iigs_bufgce.sv")
        paths.update((ROOT / "hdl/sim").glob("tb_apple_iigs_*.sv"))
        paths.update((ROOT / "hdl/sim").glob("tb_gs_*.sv"))
    if machine == "AMIGA":
        # Preserve exact imported originals and every portable transformation;
        # user Kickstart and media files remain outside public source bundles.
        paths.update(path for path in (ROOT / "third_party/amiga").rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts
                     and (amiga_ap68040 or "ap68040" not in path.parts))
        paths.update(path for path in (ROOT / "software/amiga").rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
        paths.update((ROOT / "scripts").glob("*amiga*.py"))
        paths.update((ROOT / "tests").glob("test_amiga_*.py"))
        paths.add(ROOT / "hdl/core/apple_iigs/multitini_apple_iigs_ddr_path.sv")
        paths.add(ROOT / "scripts/build_phos1.py")
        paths.add(ROOT / "hdl/sim/xilinx_primitives.sv")
        paths.update((ROOT / "hdl/sim").glob("amiga_*.sv"))
        paths.update((ROOT / "hdl/sim").glob("tb_amiga_*.sv"))
        paths.update((ROOT / "hdl/sim").glob("amiga_*.vhd"))
        paths.update((ROOT / "docs").glob("AMIGA_*.md"))
    if machine == "NES":
        paths.update(path for path in (ROOT / "third_party/nes").rglob("*")
                     if path.is_file())
        paths.update(ROOT / "scripts" / name for name in (
            "port_nes_core.py", "audit_nes_import.py", "test_nes.py",
            "test_nes_video.py", "build_core_nes.py", "build_phos1.py", "package_core_nes.py"))
        paths.update(ROOT / name for name in (
            "hdl/sim/xilinx_primitives.sv", "hdl/sim/video_fifo_model.sv",
            "docs/NES_PLAN.md"))
        paths.update((ROOT / "hdl/sim").glob("tb_nes_*.sv"))
    section = "common"
    for line in (ROOT / "hdl/hdl_sources.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section in selected:
            paths.add((ROOT / "hdl" / line.split("|")[0].strip()).resolve())
    for path in paths:
        if not path.is_relative_to(ROOT) or not path.is_file():
            raise ValueError(f"Missing or external build input: {path}")
    return sorted(paths)


def archive_inputs(output: Path, record: dict) -> None:
    archive = output.with_suffix(".sources.tar.gz")
    with tarfile.open(archive, "w:gz") as stream:
        for name, expected in record["files"].items():
            data = (ROOT / name).read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"Build input changed while archiving: {name}")
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            entry.mode = 0o644
            stream.addfile(entry, io.BytesIO(data))
    record["archive"] = {"name": archive.name, "sha256": digest(archive)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", choices=("C64", "GBC", "VECTREX", "GENESIS", "APPLE_IIGS", "NES", "AMIGA", "SUPERVISOR"))
    parser.add_argument("--c64-video-standard", choices=("PAL", "NTSC"), default="PAL")
    parser.add_argument("--c64-timing-policy", choices=("default", "dev8-over-0.050", "dev3-over-0.010"), default="default")
    parser.add_argument("--gbc-psram", action="store_true")
    parser.add_argument("--genesis-pal", action="store_true")
    parser.add_argument("--amiga-aga-pal", action="store_true")
    parser.add_argument("--amiga-ap68040", action="store_true")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("The generated manifest must stay in this checkout's build directory")
    if args.verify:
        record = json.loads(output.read_text())
        expected = record["files"]
        current = {str(path.relative_to(ROOT).as_posix()): digest(path)
                   for path in inputs(record["machine"], amiga_ap68040=record["configuration"].get("amiga_ap68040", False))}
        if current != expected:
            changed = sorted(path for path in set(current) | set(expected)
                             if current.get(path) != expected.get(path))
            raise ValueError(f"FPGA build inputs changed: {', '.join(changed)}")
        if "archive" in record:
            archive = record["archive"]
            if Path(archive["name"]).name != archive["name"] or digest(output.parent / archive["name"]) != archive["sha256"]:
                raise ValueError("Source archive changed")
        print(f"Verified {len(current)} unchanged {record['machine']} build inputs")
        return 0
    if not args.machine:
        parser.error("--machine is required when capturing inputs")
    if args.gbc_psram and args.machine != "GBC":
        parser.error("--gbc-psram requires --machine GBC")
    if args.genesis_pal and args.machine != "GENESIS":
        parser.error("--genesis-pal requires --machine GENESIS")
    if args.amiga_aga_pal and args.machine != "AMIGA":
        parser.error("--amiga-aga-pal requires --machine AMIGA")
    if args.amiga_ap68040 and not args.amiga_aga_pal:
        parser.error("--amiga-ap68040 requires --amiga-aga-pal")
    if args.c64_timing_policy != "default" and args.machine != "C64":
        parser.error("--c64-timing-policy requires --machine C64")
    record = {
        "schema": 1,
        "machine": args.machine,
        "configuration": {
            "c64_video_standard": args.c64_video_standard if args.machine == "C64" else None,
            "gbc_psram": args.gbc_psram,
            "genesis_pal": args.genesis_pal,
        },
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "files": {path.relative_to(ROOT).as_posix(): digest(path)
                  for path in inputs(args.machine, amiga_ap68040=args.amiga_ap68040)},
        "note": "Source bytes, not a clean-commit assertion; retain the matching source archive with the image.",
    }
    if args.machine == "C64":
        record["configuration"]["c64_timing_policy"] = args.c64_timing_policy
    if args.amiga_aga_pal:
        record["configuration"]["amiga_aga_pal"] = True
    if args.amiga_ap68040:
        record["configuration"]["amiga_ap68040"] = True
    output.parent.mkdir(parents=True, exist_ok=True)
    archive_inputs(output, record)
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(record['files'])} {args.machine} build inputs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
