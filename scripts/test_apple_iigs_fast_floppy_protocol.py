#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real-65816 formatted floppy reads, writes and durable eject in both modes.

Only mechanical spindle/ready fixture state is forced. Controller registers,
polls, data accesses, serial writes, track RAM RMW and eject commands are real.
The original sector patterns are generated locally; no game or Apple ROM ships.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
from test_apple_iigs_ports import machine_sources


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Track(ctypes.Structure):
    _fields_ = [("offset", ctypes.c_uint32), ("bits", ctypes.c_uint32),
                ("block", ctypes.c_uint16), ("blocks", ctypes.c_uint16),
                ("raw_bytes", ctypes.c_uint16)]


def nibble_bytes(bits, count):
    """Remove sync padding using an independent high-bit shift-register reader."""
    result = bytearray()
    value = 0
    for bit in range(count):
        value = ((value << 1) | ((bits[bit // 8] >> (7 - bit % 8)) & 1)) & 255
        if value & 128:
            result.append(value)
            value = 0
    return bytes(result)


def pacing(value):
    fields = tuple(int(part, 0) for part in value.split(","))
    if len(fields) != 6:
        raise argparse.ArgumentTypeError("use c036,CPUcycles,ditherNumerator,ditherDenominator,ditherPhase,initialCycles")
    c036, cycles, numerator, denominator, phase, initial = fields
    if c036 not in (0, 128) or not 12 <= cycles <= 128 or denominator < 1 or not 0 <= numerator < denominator or phase < 0 or initial < 0 or initial == 1:
        raise argparse.ArgumentTypeError("invalid explicit guest writer pacing")
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--hdd-rom", type=Path, required=True)
    parser.add_argument("--baseline606", action="store_true")
    parser.add_argument("--source-capture", type=Path,
                        help="replay the pinned production HDL from an earlier protocol capture")
    parser.add_argument("--family", choices=["35", "525"], action="append")
    parser.add_argument("--mode", choices=["normal", "fast"], action="append")
    parser.add_argument("--drain-controls", action="store_true",
                        help="also pause/eject at input strobe and both RMW stages in all four primary phases")
    parser.add_argument("--drain-only", action="store_true")
    parser.add_argument("--resume-controls", action="store_true",
                        help="also resume a paused incoming bit and finish a checksum-valid full track")
    parser.add_argument("--transition-controls", action="store_true",
                        help="also isolate speed, speaker/paddle, IRQ and SmartPort eligibility gates")
    parser.add_argument("--pause-phase", type=int, choices=range(4), action="append")
    parser.add_argument("--pause-stage", type=int, choices=range(3), action="append")
    parser.add_argument("--normal-525-pacing", type=pacing,
                        help="explicit original guest writer: c036,CPUcycles,ditherNumerator,ditherDenominator,ditherPhase,initialCycles")
    parser.add_argument("--fast-525-pacing", type=pacing, default=pacing("0,32,0,1,0,0"))
    parser.add_argument("--writer-bank-cycles", type=int, default=4)
    parser.add_argument("--writer-bank-chunk", type=int, default=512)
    args = parser.parse_args()
    if "525" in (args.family or ["35", "525"]) and not args.drain_only and "normal" in (args.mode or (["normal"] if args.baseline606 else ["normal", "fast"])) and args.normal_525_pacing is None:
        raise ValueError("Normal5.25 qualification requires explicit --normal-525-pacing; its native I/O bridge must be measured independently")
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or build.exists():
        raise ValueError("Select a fresh repository build directory")
    build.mkdir(parents=True)
    snapshot = build / "captured"
    core = ROOT / "hdl/core/apple_iigs"
    sources = machine_sources() + [core / "multitini_apple_iigs_loader.sv",
        core / "multitini_apple_iigs_cache.sv", core / "multitini_apple_iigs_adapter.sv",
        ROOT / "hdl/sim/apple_iigs_bufgce.sv", ROOT / "hdl/sim/tb_apple_iigs_fast_floppy_protocol.sv"]
    if args.source_capture:
        if args.baseline606:
            raise ValueError("Select one source baseline")
        prior = args.source_capture.resolve()
        if not prior.is_relative_to(ROOT / "build"):
            raise ValueError("Source baseline must stay under repository build")
        baseline = prior / "captured"
        receipt = json.loads((prior / "validation.json").read_text())
        if receipt["status"] != "PASS":
            raise ValueError("Source baseline must have a passing pinned receipt")
        for name, expected in receipt["files"].items():
            if sha(baseline / name) != expected:
                raise ValueError("Source baseline changed: " + name)
        sources = [baseline / q.relative_to(ROOT) if q.name != "tb_apple_iigs_fast_floppy_protocol.sv" and
                   (baseline / q.relative_to(ROOT)).exists() else q for q in sources]
    if args.baseline606:
        prior = ROOT / "build/iigs-floppy-policy/rom3-final606-180m"
        baseline = prior / "captured"
        receipt = json.loads((prior / "validation.json").read_text())
        if receipt["status"] != "PASS":
            raise ValueError("606 source capture did not pass")
        for name, expected in receipt["files"].items():
            if sha(baseline / name) != expected:
                raise ValueError("606 source capture changed: " + name)
        if sha(baseline / "hdl/core/apple_iigs/ports/iigs.sv") != "70f35366a566add5d27a1740c2074e1c8634714f1d183705f51392e19b9acb52":
            raise ValueError("606 policy source identity changed")
        sources = [q for q in sources if q.name not in ("smartport_dev.v", "smartport_dev.sv")]
        sources += [baseline / "third_party/apple-iigs/upstream/rtl/smartport_dev.v"]
        sources = [baseline / q.relative_to(ROOT) if (baseline / q.relative_to(ROOT)).exists() else q for q in sources]
    helper_sources = [ROOT / "firmware/common/iigs_content.c", ROOT / "firmware/common/media_browser.c"]
    captured = sources + helper_sources + list((ROOT / "firmware/include").glob("*.h")) + [
        ROOT / "third_party/apple-iigs/upstream/rtl/dhr_lut.vh", ROOT / "scripts/port_apple_iigs_core.py",
        ROOT / "scripts/test_apple_iigs_ports.py", Path(__file__).resolve()]
    hashes = {q.relative_to(ROOT).as_posix(): sha(q) for q in captured}
    for path in captured:
        target = snapshot / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
    hdd = args.hdd_rom.resolve()
    if len(hdd.read_bytes()) != 2304 or sha(hdd) != "c61cf963a86377131e118ca02c312099630afc85dfd3d89cdcfd88e3a9e8063e":
        raise ValueError("Expected assembled licensed slot ROM")
    (build / "hdd.hex").write_text("\n".join(f"{b:02x}" for b in hdd.read_bytes()) + "\n")
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    env = os.environ.copy()
    env["PATH"] = str(runtime) + os.pathsep + str(runtime.parents[1] / "usr/bin") + os.pathsep + env.get("PATH", "")
    env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()
    commands = {}
    record = {"schema_version": 1, "status": "RUNNING", "baseline606": args.baseline606,
        "files": hashes, "commands": commands, "cases": {},
        "hdd_rom": {"path": str(hdd), "bytes": 2304, "sha256": sha(hdd)},
        "limits": ["Original generated sectors and synthetic guest program; no Apple ROM or game fixture.",
            "Base, pause/resume and drain cases force only mechanical spin-up/drive-ready state; no protocol, data-ready, position, Q6/Q7 or write state is forced.",
            "The four isolated eligibility controls additionally force one observed speed/compatibility/IRQ/SmartPort eligibility input, then release it; they do not force controller protocol state.",
            "CPU polling reads, serial writes, full-sector checksums, native Sony eject and adapter durable save are exercised.",
            "This does not establish complete filesystem/formatter compatibility, FPGA timing or physical hardware behavior."]}
    record["writer_configuration"] = {"normal525": args.normal_525_pacing, "fast525": args.fast_525_pacing,
        "bank_jump_cycles": args.writer_bank_cycles, "bank_data_bytes": args.writer_bank_chunk,
        "meaning": "Original synthetic guest instructions, independently qualified by actual IWM primary/virtual timestamps and full decoded sectors; no production clock is changed."}
    started = time.monotonic()
    (build / "validation.json").write_text(json.dumps(record, indent=2) + "\n")

    def run(command, name):
        command = [str(q).replace("\\", "/") for q in command]
        commands[name] = command
        with (build / (name + ".log")).open("w") as stream:
            result = subprocess.run(command, cwd=build, env=env, stdout=stream, stderr=subprocess.STDOUT)
        text = (build / (name + ".log")).read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", text):
            raise ValueError(name + " failed: " + text[-6000:])
        return text

    try:
        library_path = build / "floppy-codec.dll"
        run([runtime / "gcc.exe", "-shared", "-O2", "-I" + str(snapshot / "firmware/include"),
             *(snapshot / q.relative_to(ROOT) for q in helper_sources), "-o", library_path], "codec")
        library = ctypes.CDLL(str(library_path))
        library.mt_iigs_floppy_track.restype = ctypes.c_bool
        library.mt_iigs_floppy_encode.restype = ctypes.c_bool
        library.mt_iigs_floppy_decode.restype = ctypes.c_bool
        byte_array = ctypes.c_ubyte * 10240
        raw_array = ctypes.c_ubyte * 6144
        fixtures = {}
        for family in args.family or ["35", "525"]:
            fmt = 2 if family == "35" else 1
            track = Track()
            if not library.mt_iigs_floppy_track(fmt, 0, ctypes.byref(track)):
                raise ValueError("Track geometry rejected")
            expected = bytes(((i * 37) ^ (i >> 3) ^ 0x5b) & 255 for i in range(track.raw_bytes))
            initial = bytes(((i * 19) ^ (i >> 2) ^ 0xc7) & 255 for i in range(track.raw_bytes))
            raw = raw_array.from_buffer_copy(expected + bytes(6144 - len(expected)))
            old = raw_array.from_buffer_copy(initial + bytes(6144 - len(initial)))
            encoded, original = byte_array(), byte_array()
            if not library.mt_iigs_floppy_encode(fmt, 0, raw, encoded) or not library.mt_iigs_floppy_encode(fmt, 0, old, original):
                raise ValueError("Original sector fixture encode rejected")
            # Preserve every physical bit, including the zero padding of
            # 10-bit self-sync bytes. Flattening these to8-bit FF makes the
            # first prologue depend on the arbitrary initial rotational phase.
            stream = bytes(encoded)[:(track.bits + 7) // 8]
            if len(stream) > 10240:
                raise ValueError("Guest write stream exceeds fixture bounds")
            name = "fixture-" + family
            (build / (name + "-stream.hex")).write_text("\n".join(f"{b:02x}" for b in stream) + "\n")
            # Two sides are present for Sony; only side0 is selected and written.
            sides = 2 if family == "35" else 1
            image = bytearray(1536 + track.blocks * 512 * sides)
            image[:8] = b"WOZ2\xff\x0a\x0d\x0a"
            struct.pack_into("<4sI", image, 12, b"INFO", 60)
            image[20:22] = bytes([2, 2 if family == "35" else 1])
            image[59] = 16 if family == "35" else 32
            struct.pack_into("<4sI", image, 80, b"TMAP", 160)
            image[88:248] = bytes([255]) * 160
            struct.pack_into("<4sI", image, 248, b"TRKS", len(image) - 256)
            for side in range(sides):
                image[88 + side] = side
                block = 3 + side * track.blocks
                struct.pack_into("<HHI", image, 256 + side * 8, block, track.blocks, track.bits)
                image[block * 512:block * 512 + track.blocks * 512] = bytes(original)[:track.blocks * 512]
            (build / (name + ".bin")).write_bytes(image)
            fixtures[family] = {"format": fmt, "track": track, "initial": initial, "expected": expected,
                "stream": stream, "image": image, "name": name}
        top = "tb_apple_iigs_fast_floppy_protocol"
        obj = build / "obj"
        exe = obj / ("V" + top + ".exe")
        inc = runtime.parent / "share/verilator/include"
        run([runtime / "verilator_bin.exe", "--cc", "--exe", "--main", "--timing", "-Wno-fatal",
             "--top-module", top, "-I" + str(snapshot / "third_party/apple-iigs/upstream/rtl"), "--Mdir", obj,
             *(["-DBASELINE606"] if args.baseline606 else []),
             *(["-DHAS_DRAIN_HELPERS"] if "accepted_write_strobe" in next(q for q in sources if q.name == "flux_drive.sv").read_text() else []),
             *(snapshot / q.relative_to(ROOT) for q in sources)], "generate")
        run([runtime / "g++.exe", "-std=c++20", "-O2", "-pthread", "-DVL_TIME_CONTEXT",
             "-I" + str(inc), "-I" + str(inc / "vltstd"), "-I" + str(obj), *sorted(obj.glob("*.cpp")),
             inc / "verilated.cpp", inc / "verilated_timing.cpp", inc / "verilated_threads.cpp", "-o", exe], "compile")
        record["executable_sha256"] = sha(exe)
        for family, fixture in fixtures.items():
            cases = [] if args.drain_only else [(mode, -1, 0, False, -1) for mode in args.mode or (["normal"] if args.baseline606 else ["normal", "fast"])]
            if args.drain_controls or args.drain_only:
                if args.baseline606:
                    raise ValueError("Drain controls qualify the new primary-clock island")
                cases += [("fast", phase, stage, False, -1) for phase in args.pause_phase or range(4)
                          for stage in args.pause_stage or range(3)]
            if args.resume_controls:
                cases += [("fast", 0, 2, True, -1)]
            if args.transition_controls:
                cases += [("fast", 0, 2, False, transition) for transition in range(4)]
            for mode, phase, stage, resume, transition in cases:
                if args.baseline606 and mode != "normal":
                    raise ValueError("606 has no fast option")
                name = family + "-" + mode
                if phase >= 0:
                    name += "-drain" + str(phase) + "-stage" + str(stage)
                if resume:
                    name += "-resume"
                if transition >= 0:
                    name += "-transition" + str(transition)
                prefix = (build / name).as_posix()
                writer = args.fast_525_pacing if mode == "fast" else args.normal_525_pacing or pacing("0,32,0,1,0,0")
                text = run([exe, "+HDD_ROM=" + (build / "hdd.hex").as_posix(),
                    "+DISK=" + (build / (fixture["name"] + ".bin")).as_posix(),
                    "+STREAM=" + (build / (fixture["name"] + "-stream.hex")).as_posix(),
                    "+STREAM_BYTES=" + str(len(fixture["stream"])), "+FAMILY35=" + str(int(family == "35")),
                    "+FAST=" + str(int(mode == "fast")), "+TRACK_BITS=" + str(fixture["track"].bits),
                    "+TRACK_BLOCKS=" + str(fixture["track"].blocks), "+OUTPUT=" + prefix,
                    "+DRAIN_PHASE=" + str(phase), "+DRAIN_STAGE=" + str(stage),
                    "+RESUME_AFTER_PAUSE=" + str(int(resume)), "+TRANSITION=" + str(transition),
                    "+WRITER_C036=" + str(writer[0]), "+WRITER_CYCLES=" + str(writer[1]),
                    "+DITHER_NUMERATOR=" + str(writer[2]), "+DITHER_PERIOD=" + str(writer[3]),
                    "+DITHER_PHASE=" + str(writer[4]), "+WRITER_PHASE_CYCLES=" + str(writer[5]),
                    "+WRITER_BANK_CYCLES=" + str(args.writer_bank_cycles), "+WRITER_BANK_CHUNK=" + str(args.writer_bank_chunk)], name)
                marker = "PASS: actual CPU formatted floppy read/write and durable eject" if phase < 0 or resume else "PASS: paused final-bit drain and durable eject"
                if marker not in text:
                    raise ValueError("Missing full protocol PASS")
                written = Path(prefix + "-written.bin").read_bytes()
                received = Path(prefix + "-read.bin").read_bytes()
                host_saved = Path(prefix + "-saved.bin").read_bytes()
                for label, bits, expected in [("write", written, fixture["expected"]),
                                              ("read", received, fixture["initial"]),
                                              ("saved", host_saved, fixture["expected"])]:
                    if len(bits) != 10240:
                        raise ValueError(label + " has wrong captured byte count")
                    if phase >= 0 and not resume and label != "read":
                        if written != host_saved:
                            raise ValueError("Paused eject changed a captured physical track bit")
                        continue
                    decoded = raw_array()
                    if not library.mt_iigs_floppy_decode(fixture["format"], 0, byte_array.from_buffer_copy(bits), decoded):
                        raise ValueError(name + " " + label + " failed full-track checksum/address validation")
                    if bytes(decoded)[:len(expected)] != expected:
                        raise ValueError(name + " " + label + " changed a decoded sector byte")
                record["cases"][name] = {"status": "PASS", "raw_bytes": len(fixture["expected"]),
                    "raw_sha256": hashlib.sha256(fixture["expected"]).hexdigest(),
                    "stream_bytes": len(fixture["stream"]), "stream_sha256": hashlib.sha256(fixture["stream"]).hexdigest(),
                    "track_bits": fixture["track"].bits, "written_sha256": sha(Path(prefix + "-written.bin")),
                    "pause_phase": phase, "pause_stage": stage if phase >= 0 else None,
                    "resume_after_pause": resume,
                    "eligibility_transition": transition if transition >= 0 else None,
                    "forced_scope": "mechanics plus isolated eligibility observation" if transition >= 0 else "mechanics only",
                    "read_sha256": sha(Path(prefix + "-read.bin")), "saved_sha256": sha(Path(prefix + "-saved.bin")),
                    "results": [line for line in text.splitlines() if line.startswith(("PROTOCOL_RESULT", "PASS:"))]}
                record["cases"][name]["write_cadence"] = [line for line in text.splitlines()
                    if line.startswith(("WRITE_CADENCE", "PROTOCOL_WRITE_GAP"))]
                stores = Path(prefix + "-stores.txt")
                if stores.exists():
                    record["cases"][name]["exact_write_stores"] = {"path": str(stores), "sha256": sha(stores), "bytes": stores.stat().st_size}
        record["speed_comparisons"] = {}
        for family in fixtures:
            normal = record["cases"].get(family + "-normal")
            fast_case = record["cases"].get(family + "-fast")
            if normal is None or fast_case is None:
                continue
            def metrics(case):
                line = next(line for line in case["results"] if "transfer_primary_ticks=" in line)
                return {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", line)}
            n, f = metrics(normal), metrics(fast_case)
            progress = f["selected_position_changes"] / n["selected_position_changes"]
            elapsed = n["transfer_primary_ticks"] / f["transfer_primary_ticks"]
            if not 3.98 <= progress <= 4.02 or elapsed < 2.5:
                raise ValueError("Fast option did not accelerate actual media and CPU transfer progress")
            if abs(f["selected_kernel_ops"] - n["selected_kernel_ops"]) > 3:
                raise ValueError("Fast option changed unrelated selected-spindle CPU throughput")
            if any(f[key] != n[key] for key in ("native_edges", "video_samples")) or abs(f["audio_samples"] - n["audio_samples"]) > 1:
                raise ValueError("Fast option changed shared peripheral timebases")
            record["speed_comparisons"][family] = {"status": "PASS", "disk_progress_ratio": progress,
                "formatted_transfer_speedup": elapsed, "kernel_dispatch_difference": f["selected_kernel_ops"] - n["selected_kernel_ops"]}
        if any(sha(snapshot / name) != h for name, h in hashes.items()):
            raise ValueError("Captured source changed")
        record["captured_sources_unchanged"] = True
        record["current_sources_unchanged"] = all(sha(ROOT / name) == h for name, h in hashes.items())
        record["status"] = "PASS"
    except BaseException as error:
        record.update(status="FAIL", error=str(error))
        raise
    finally:
        record["wall_seconds"] = time.monotonic() - started
        record["evidence_files"] = {q.name: {"bytes": q.stat().st_size, "sha256": sha(q)}
                                    for q in build.glob("*.log")}
        (build / "validation.json").write_text(json.dumps(record, indent=2) + "\n")
    print("PASS actual CPU formatted read/write cases", build)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
