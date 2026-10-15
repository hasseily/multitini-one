#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and execute NES content, transaction and real USB adapter tests."""
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
GUARD = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/core-nes/runtime-tests")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    temporary = build / "temp"
    temporary.mkdir(exist_ok=True)
    os.environ["TEMP"] = os.environ["TMP"] = str(temporary)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name: str) -> str:
        value = str(runtime / f"{name}.exe") if os.name == "nt" else shutil.which(name)
        if value is None: raise FileNotFoundError(name)
        return value
    tracked = [*ROOT.glob("firmware/common/*.c"), *ROOT.glob("firmware/include/*.h"),
        ROOT / "CMakeLists.txt", ROOT / "firmware/platform/xilinx/main.c",
        ROOT / "firmware/platform/xilinx/usb_hid_service.c",
        ROOT / "firmware/platform/xilinx/nes_content_service.c",
        ROOT / "firmware/platform/xilinx/nes_content_service.h",
        ROOT / "firmware/platform/xilinx/persistent_save.c",
        ROOT / "tests/host/test_nes_runtime.c", ROOT / "tests/host/test_nes_content_service.c",
        ROOT / "tests/host/test_usb_hid_runtime.c", Path(__file__).resolve()]
    def hashes():
        return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    before = hashes()
    checks = []
    def run(name, arguments):
        result = subprocess.run(GUARD + list(map(str,arguments)),cwd=ROOT,capture_output=True,text=True)
        output = result.stdout + result.stderr
        (build / f"{name}.log").write_text(output,encoding="utf-8")
        if result.returncode: raise RuntimeError(f"{name} failed:\n{output[-12000:]}")
        checks.append(name)
        return output
    run("configure",[tool("cmake"),"-S",ROOT,"-B",build,"-G","Ninja",
        f"-DCMAKE_C_COMPILER={tool('gcc')}",f"-DCMAKE_CXX_COMPILER={tool('g++')}",
        f"-DCMAKE_MAKE_PROGRAM={tool('ninja')}","-DCMAKE_BUILD_TYPE=Debug"])
    run("compile",[tool("cmake"),"--build",build,"--parallel","6","--target",
        "test_nes_runtime","test_nes_content_service","test_usb_hid_runtime",
        "xilinx_main_compile","usb_hid_service_compile"])
    output=run("test",[tool("ctest"),"--test-dir",build,"--output-on-failure","-R","^(nes_runtime|nes_content_service|usb_hid_runtime)$"])
    print(output.strip())
    if hashes() != before: raise RuntimeError("Runtime test inputs changed while tests ran")
    (build / "validation.json").write_text(json.dumps({"checks":checks,"source_sha256":before,
        "limits":"Native production firmware tests; no FPGA or hardware acceptance."},indent=2)+"\n")
    return 0

if __name__ == "__main__": raise SystemExit(main())
