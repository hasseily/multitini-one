#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the complete PHO1 matrix from captured sources with normal signoff gates."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD_ID = "50484F31"
MATRIX = {"supervisor": ("SUPERVISOR", False, False),
          "c64-pal": ("C64", False, False), "gbc": ("GBC", True, False),
          "vectrex": ("VECTREX", False, False),
          "genesis-ntsc": ("GENESIS", False, False),
          "genesis-pal": ("GENESIS", False, True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/phos1/hardware")
    parser.add_argument("--build-id", default=BUILD_ID, help="Eight hexadecimal digits identifying every image")
    parser.add_argument("--image", choices=tuple(MATRIX), action="append")
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--vivado", type=Path, default=Path("C:/Xilinx/2025.2/Vivado/bin/vivado.bat"))
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id):
        parser.error("--build-id must contain exactly eight hexadecimal digits")
    args.build_id = args.build_id.upper()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        parser.error("Output must be a fresh child of the checkout build directory")
    images = args.image or list(MATRIX)
    if len(images) != len(set(images)):
        parser.error("Duplicate image")
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]

    def build(name):
        machine, psram, pal = MATRIX[name]
        folder = output / name
        folder.mkdir(parents=True, exist_ok=False)
        (folder / "temp").mkdir()
        environment = dict(os.environ, TEMP=str(folder / "temp"), TMP=str(folder / "temp"))
        # Remove inherited variant selections; every build names all applicable settings.
        for key in ("MULTITINI_GENESIS_PAL", "MULTITINI_ENABLE_GBC_PSRAM", "MULTITINI_C64_VIDEO_STANDARD"):
            environment.pop(key, None)
        settings = {"MULTITINI_VIVADO_DIR": folder.as_posix(), "MULTITINI_MACHINE": machine,
                    "MULTITINI_BUILD_ID": args.build_id}
        if machine == "C64": settings["MULTITINI_C64_VIDEO_STANDARD"] = "PAL"
        if machine == "GBC": settings["MULTITINI_ENABLE_GBC_PSRAM"] = str(int(psram))
        if machine == "GENESIS": settings["MULTITINI_GENESIS_PAL"] = str(int(pal))
        config = "".join(f"set ::env({key}) {{{value}}}\n" for key, value in settings.items())
        for kind, script in (("generate", "create_project"), ("build", "build_and_export_xsa")):
            (folder / f"{kind}.tcl").write_text(config +
                f"source {{{(ROOT / 'scripts' / (script + '.tcl')).as_posix()}}}\n", encoding="utf-8")
        capture = [sys.executable, ROOT / "scripts/capture_vectrex_build_inputs.py",
                   "--machine", machine, "--output", folder / "source-inputs.json"]
        if psram: capture.append("--gbc-psram")
        if pal: capture.append("--genesis-pal")
        subprocess.run(list(map(str, capture)), cwd=ROOT, env=environment, check=True)
        record = json.loads((folder / "source-inputs.json").read_text())
        with zipfile.ZipFile(folder / "source-inputs.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for filename, digest in record["files"].items():
                data = (ROOT / filename).read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError("Source changed during capture: " + filename)
                archive.writestr(filename, data)
            for filename in ("source-inputs.json", "generate.tcl", "build.tcl"):
                archive.write(folder / filename, "build-config/" + filename)
        print("START " + name, flush=True)
        for kind in ("generate", "build"):
            command = guard + [str(args.vivado), "-mode", "batch", "-source", str(folder / f"{kind}.tcl"),
                              "-log", str(folder / f"{kind}.log"), "-journal", str(folder / f"{kind}.jou")]
            with (folder / f"{kind}-console.log").open("w", encoding="utf-8") as log:
                subprocess.run(command, cwd=folder, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([sys.executable, str(ROOT / "scripts/capture_vectrex_build_inputs.py"),
                        "--output", str(folder / "source-inputs.json"), "--verify"], cwd=ROOT, check=True)
        print("PASS " + name, flush=True)
        return name

    failed = []
    # Complete supervisor startup before other PS7 IP builds share the tool state.
    if set(images) == set(MATRIX):
        try:
            build("supervisor")
        except Exception as error:
            failed.append("supervisor")
            print(f"FAIL supervisor: {error}", flush=True)
        images = [name for name in images if name != "supervisor"]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(build, name): name for name in images}
        for future in as_completed(pending):
            try:
                future.result()
            except Exception as error:
                failed.append(pending[future])
                print(f"FAIL {pending[future]}: {error}", flush=True)
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
