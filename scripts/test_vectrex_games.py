#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare local Vectrex game acceptance inputs and record their provenance.

Assets and all generated results remain under build/. Downloading a public
test input does not grant a content license or add it to the source release.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.request
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_COMMIT = "945088b8862c09aa556f3d0c3efa07eeaa1a5b5b"
SOURCE_PAGE = "https://www.atarihq.com/vectrex/emu.htm"
ARCHIVE_URL = "https://www.atarihq.com/vectrex/everygam.zip"
ARCHIVE_SHA256 = "c9c0214061116a7d0961cbfac6f26741e656ae84c87733a4abfeb2675096ab89"
GAME_FILES = {
    "armor_attack": "ARMOR.BIN",
    "berzerk": "BERZERK.BIN",
    "clean_sweep": "SWEEP.BIN",
    "scramble": "SCRAMBLE.BIN",
    "spike": "SPIKE.BIN",
    "star_castle": "CASTLE.BIN",
}


def save_asset(directory: Path, name: str, data: bytes, source: str,
               source_page: str | None = None) -> dict:
    path = directory / f"{name}.bin"
    path.write_bytes(data)
    path.with_suffix(".mem").write_text("".join(f"{byte:02x}\n" for byte in data), encoding="ascii")
    return {"name": name, "path": str(path.relative_to(ROOT)), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "source": source,
            "source_page": source_page, "rights": "NOASSERTION",
            "use": "Local simulation input; excluded from source control"}


def prepare(args: argparse.Namespace) -> Path:
    directory = args.assets.resolve()
    if not directory.is_relative_to(ROOT / "build"):
        raise ValueError("Acceptance assets must stay in this checkout's build directory")
    directory.mkdir(parents=True, exist_ok=True)
    manifest_path = directory / "manifest.json"
    if manifest_path.exists() and not args.bios_vhdl and not args.download:
        # A replay only reads prepared assets. Separate concurrent runs must
        # not rewrite a shared manifest while another run is reading it.
        return manifest_path
    entries = {}
    if manifest_path.exists():
        entries = {entry["name"]: entry for entry in json.loads(manifest_path.read_text())["assets"]}
    if args.bios_vhdl:
        source = args.bios_vhdl.read_text(encoding="utf-8")
        declaration = re.search(r"signal\s+rom_data\s*:\s*rom\s*:=\s*\((.*?)\);", source, re.S | re.I)
        if declaration is None:
            raise ValueError("No BIOS ROM initializer found")
        data = bytes(int(byte, 16) for byte in re.findall(r'X"([0-9a-fA-F]{2})"', declaration.group(1)))
        if len(data) != 8192:
            raise ValueError(f"Expected 8192 BIOS bytes, got {len(data)}")
        entries["bios"] = save_asset(directory, "bios", data,
            f"https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/{UPSTREAM_COMMIT}/rtl/bios_rom.vhd")
        entries["bios"]["extraction"] = "8-bit constants in rom_data initializer only; no machine HDL imported"
    if args.download:
        archive_path = directory / "everygam.zip"
        if not archive_path.exists():
            with urllib.request.urlopen(ARCHIVE_URL, timeout=30) as response:
                archive_path.write_bytes(response.read(1048576))
        if hashlib.sha256(archive_path.read_bytes()).hexdigest() != ARCHIVE_SHA256:
            raise ValueError("Public game archive changed; review and repin before use")
        archive = zipfile.ZipFile(archive_path)
        for name in args.games:
            if name == "minestorm":
                continue
            path = directory / f"{name}.bin"
            if name in entries and path.is_file():
                actual = hashlib.sha256(path.read_bytes()).hexdigest()
                if actual != entries[name]["sha256"]:
                    raise ValueError(f"Cached test asset changed: {path}")
                continue
            url = ARCHIVE_URL + "#" + GAME_FILES[name]
            data = archive.read(GAME_FILES[name])
            if not (1024 <= len(data) <= 32768 and len(data) & (len(data)-1) == 0):
                raise ValueError(f"Unexpected cartridge size from {url}: {len(data)}")
            if b"g GCE" not in data[:32] and b"GCE" not in data[:32]:
                raise ValueError(f"Missing ordinary Vectrex cartridge header from {url}")
            entries[name] = save_asset(directory, name, data, url, SOURCE_PAGE)
            entries[name]["archive_sha256"] = ARCHIVE_SHA256
            print(f"Prepared {name}: {len(data)} bytes {entries[name]['sha256']}", flush=True)
    manifest = {"generated_utc": datetime.now(timezone.utc).isoformat(),
                "content_policy": "docs/CONTENT_POLICY.md",
                "notice": "Sources are ordinary public downloads. Rights remain NOASSERTION; local test use only.",
                "assets": list(entries.values())}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def run_tool(command: list[str], log: Path, environment: dict | None = None) -> None:
    launcher = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.run(launcher + command, cwd=ROOT, stdout=output,
                                 stderr=subprocess.STDOUT, check=False, env=environment)
    if process.returncode:
        print(log.read_text(encoding="utf-8", errors="replace")[-12000:], flush=True)
        raise RuntimeError(f"Tool failed ({process.returncode}); see {log}")


def render_snapshots(prefix: Path) -> list[dict]:
    from PIL import Image
    snapshots = []
    for path in sorted(prefix.parent.glob(prefix.name + "_[0-9][0-9].pgm")):
        image = Image.open(path)
        png = path.with_suffix(".png")
        image.save(png)
        snapshots.append({"path": str(png.relative_to(ROOT)), "bbox": image.getbbox(),
                          "sha256": hashlib.sha256(image.tobytes()).hexdigest()})
    return snapshots


def package_audio(prefix: Path) -> dict | None:
    pcm_path = prefix.with_name(prefix.name+"_audio.s24le")
    if not pcm_path.exists():
        return None
    pcm = pcm_path.read_bytes()
    if len(pcm)%6:
        raise ValueError("Incomplete stereo24-bit audio sample")
    wav_path = pcm_path.with_suffix(".wav")
    with wave.open(str(wav_path),"wb") as output:
        output.setnchannels(2)
        output.setsampwidth(3)
        output.setframerate(48000)
        output.writeframes(pcm)
    channels=[]
    for channel in range(2):
        values=[int.from_bytes(pcm[offset:offset+3],"little",signed=True)
                for offset in range(channel*3,len(pcm),6)]
        count=len(values)
        mean=sum(values)/max(1,count)
        peak=max((abs(value) for value in values),default=0)
        rms=math.sqrt(sum((value-mean)**2 for value in values)/max(1,count))
        channels.append({"peak_fraction":peak/8388608,"dc_fraction":mean/8388608,
                         "ac_rms_fraction":rms/8388608,
                         "full_scale_samples":sum(value in (-8388608,8388607) for value in values),
                         "changed_samples":sum(a!=b for a,b in zip(values,values[1:]))})
    return {"path":str(wav_path.relative_to(ROOT)),"sample_rate":48000,"sample_width":24,
            "frames":len(pcm)//6,"seconds":len(pcm)/288000,
            "sha256":hashlib.sha256(pcm).hexdigest(),"channels":channels}


def simulate(args: argparse.Namespace, manifest_path: Path) -> None:
    assets = {entry["name"]: entry for entry in json.loads(manifest_path.read_text())["assets"]}
    if "bios" not in assets:
        raise ValueError("Prepare a BIOS with --bios-vhdl before simulation")
    for name in ["bios", *(name for name in args.games if name != "minestorm")]:
        if name not in assets:
            raise ValueError(f"Missing test asset {name}; use --download or prepare the selected local input")
        path = ROOT / assets[name]["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != assets[name]["sha256"]:
            raise ValueError(f"Test asset does not match manifest: {path}")
    output = ROOT / "build/vectrex-games" / args.run_name
    if not output.resolve().is_relative_to(ROOT / "build"):
        raise ValueError("Simulation output must stay in build/")
    output.mkdir(parents=True, exist_ok=True)
    local_manifest = output / "assets.json"
    local_manifest.write_text(json.dumps({"assets": list(assets.values())}, indent=2)+"\n", encoding="utf-8")
    (output / "runner.py").write_bytes(Path(__file__).read_bytes())
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    iverilog = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    vvp = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    sources = ["third_party/vectrex/cpu/mc6809i.v",
               "hdl/core/vectrex/chips/via6522.v", "hdl/core/vectrex/chips/YM2149.sv",
               "hdl/core/vectrex/multitini_vectrex_cpu.sv",
               "hdl/core/vectrex/multitini_vectrex_beam.sv",
               "hdl/core/vectrex/multitini_vectrex_renderer.sv",
               "hdl/core/vectrex/multitini_vectrex_machine.sv", "hdl/sim/tb_vectrex_machine.sv"]
    source_hashes = {path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in sources}
    if args.backend == "iverilog":
        executable = output / "tb_vectrex_machine.vvp"
        compile_command = [iverilog, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_vectrex_machine",
                           f"-Ptb_vectrex_machine.CLOCK_HZ={args.clock_hz}",
                           f"-Ptb_vectrex_machine.WIDTH={args.width}",
                           f"-Ptb_vectrex_machine.HEIGHT={args.height}", "-o", str(executable)]
        run_tool(compile_command + [str(ROOT / path) for path in sources], output / "compile.log")
        simulation_command = [vvp, str(executable)]
    else:
        model = output / "model"
        model.mkdir(exist_ok=True)
        executable = model / ("vectrex_game_sim.exe" if os.name == "nt" else "vectrex_game_sim")
        verilator = str(runtime / "verilator_bin.exe") if os.name == "nt" else "verilator"
        kit = Path(os.environ.get("VERILATOR_ROOT", str(runtime.parent / "share/verilator")
                                  if os.name == "nt" else "/usr/share/verilator"))
        environment = dict(os.environ, VERILATOR_ROOT=str(kit))
        compile_command = [verilator, "--cc", "--exe", "--main", "--timing", "--top-module", "tb_vectrex_machine",
                           f"-GCLOCK_HZ={args.clock_hz}", f"-GWIDTH={args.width}", f"-GHEIGHT={args.height}",
                           "--Mdir", str(model), "--Wno-fatal"]
        run_tool(compile_command + [str(ROOT/path) for path in sources], output / "compile.log", environment)
        # Compile the generated model directly. This avoids shell-dependent
        # archive recipes in the MSYS2 makefiles while preserving the same
        # Verilator runtime and a guarded native compiler process.
        cxx = str(runtime / "g++.exe") if os.name == "nt" else "g++"
        # Use the current generated class list, rather than picking up stale
        # split files left by an older model in the same output directory.
        classes = (model/"Vtb_vectrex_machine_classes.mk").read_text()
        generated = re.findall(r"^[ \t]+(Vtb_vectrex_machine\w*)[ \t]+\\$",classes,re.M)
        if not generated:
            raise RuntimeError("Verilator did not emit its expected model class list")
        cpp = [str(model/(name+".cpp")) for name in generated]
        cpp += [str(kit/"include"/name) for name in ("verilated.cpp", "verilated_timing.cpp", "verilated_threads.cpp")]
        run_tool([cxx, "-std=c++20", "-O2", "-pthread", "-DVL_TIME_CONTEXT",
                  "-I"+str(kit/"include"), "-I"+str(kit/"include/vltstd"), "-I"+str(model),
                  *cpp, "-o", str(executable)], output / "native-build.log", environment)
        simulation_command = [str(executable)]
    if source_hashes != {path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in sources}:
        raise RuntimeError("HDL changed during compilation; rerun to obtain an unambiguous source record")
    results = []
    def simulate_one(name: str) -> dict:
        prefix = output / name
        command = [*simulation_command, f"+BIOS={(ROOT / assets['bios']['path']).with_suffix('.mem')}",
                   f"+OUT={prefix}", f"+CYCLES={args.cycles}", f"+CAPTURE_EVERY={args.capture_every}",
                   f"+CONTROLS={int(not args.idle)}"]
        if args.warm_reset_cycle:
            command.append(f"+WARM_RESET_CYCLE={args.warm_reset_cycle}")
        if name != "minestorm":
            command += [f"+CART={(ROOT / assets[name]['path']).with_suffix('.mem')}",
                        f"+CART_BYTES={assets[name]['bytes']}"]
        if args.trace_cycles:
            command += [f"+TRACE_START={max(0,args.cycles-args.trace_cycles)}", f"+TRACE_END={args.cycles}"]
        print(f"Simulating {name}: {args.cycles/1500000:.3f} emulated seconds at {args.clock_hz}Hz fabric", flush=True)
        start = time.monotonic()
        run_tool(command, output / f"{name}.log")
        log = (output / f"{name}.log").read_text()
        if "PASS: Vectrex machine executed" not in log:
            raise RuntimeError(f"No simulation completion marker for {name}")
        stats = (output / f"{name}_stats.csv").read_text().splitlines()
        total = next(line.split(",")[1:] for line in stats if line.startswith("TOTAL,"))
        fields = ("cpu_cycles", "bus_reads", "bus_writes", "via_writes", "ram_writes", "cartridge_reads",
                  "beam_points", "lit_points", "audio_changes", "button_reads", "unique_fetch_addresses",
                  "renderer_overflows", "fetch_hash")
        result = dict(zip(fields, [int(value,16) if index==12 else int(value) for index,value in enumerate(total)]))
        game_stats = next((line.split(",")[1:] for line in stats if line.startswith("GAME,")),None)
        if game_stats:
            result.update({"game_instruction_fetches":int(game_stats[0]),
                           "first_game_cycle":int(game_stats[1]),"first_game_pc":game_stats[2]})
            if len(game_stats)>=5:
                result.update({"actual_button_reads":int(game_stats[3]),"active_button_reads":int(game_stats[4])})
        dac_stats = next((line.split(",")[1:] for line in stats if line.startswith("DAC,")),None)
        if dac_stats:
            result.update({"dac_changes":int(dac_stats[0]),"dac_nonzero_samples":int(dac_stats[1])})
        result.update({"game": name, "host_seconds": round(time.monotonic()-start,3),
                       "snapshots": render_snapshots(prefix),"audio":package_audio(prefix)})
        print(f"Completed {name}: {result['via_writes']} VIA writes, {result['lit_points']} lit points, "
              f"{result['renderer_overflows']} overflows in {result['host_seconds']} host seconds",flush=True)
        return result
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(simulate_one,name) for name in args.games]
        for future in as_completed(futures):
            results.append(future.result())
            report = {"clock_hz":args.clock_hz,"width":args.width,"height":args.height,"backend":args.backend,
                      "controls":"idle" if args.idle else "deterministic start, movement and fire replay",
                      "warm_reset_cycle":args.warm_reset_cycle,
                      "source_sha256":source_hashes,
                      "asset_manifest":str(local_manifest.relative_to(ROOT)), "results":results,
                      "evidence_limit":"Simulation activity and captured frames; visual review required for gameplay acceptance. No physical board test."}
            (output/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=ROOT / "build/vectrex-games/assets")
    parser.add_argument("--bios-vhdl", type=Path)
    parser.add_argument("--download", action="store_true", help="Fetch selected ordinary public test cartridges")
    parser.add_argument("--games", nargs="+", choices=["minestorm", *GAME_FILES], default=["minestorm", *GAME_FILES])
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--cycles", type=int, default=30000000, help="6809 clock cycles; cold BIOS and music can take over 15 seconds")
    parser.add_argument("--capture-every", type=int, default=1500000)
    parser.add_argument("--trace-cycles", type=int, default=37500)
    parser.add_argument("--clock-hz", type=int, default=24000000)
    parser.add_argument("--width", type=int, default=135)
    parser.add_argument("--height", type=int, default=180)
    parser.add_argument("--run-name", default="native-24mhz")
    parser.add_argument("--idle", action="store_true")
    parser.add_argument("--backend", choices=["verilator", "iverilog"], default="verilator")
    parser.add_argument("--jobs", type=int, default=1, help="Independent game simulations to run at once")
    parser.add_argument("--warm-reset-cycle", type=int, default=0,
                        help="Exercise the public reset input after this many CPU cycles; does not patch BIOS/RAM")
    args = parser.parse_args()
    if args.cycles <= 0 or args.capture_every < 0 or args.trace_cycles < 0 or args.jobs <= 0:
        parser.error("Cycle counts must be positive (capture/trace may be zero)")
    manifest = prepare(args)
    print(manifest, flush=True)
    if not args.prepare_only:
        simulate(args, manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
