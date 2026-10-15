#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/run Windows host checks with installed Visual Studio Clang sanitizers.

No packages are installed. Matching ASan and VC runtime DLLs are copied only
beside generated executables under build/. Every native launch uses the guarded
launcher, so missing tool dependencies cannot open desktop loader dialogs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GUARD = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]


def newest(paths):
    found = [path for path in paths if path.is_file()]
    if not found:
        raise RuntimeError("Required installed tool/runtime was not found")
    return max(found, key=lambda path: path.stat().st_mtime_ns)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / "build/host-video-effects-vs-sanitize")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This runner selects the installed Visual Studio Windows toolchain")
    build = args.build_dir.resolve()
    if not build.is_relative_to((ROOT / "build").resolve()):
        parser.error("--build-dir must stay inside this checkout's ignored build directory")
    build.mkdir(parents=True, exist_ok=True)
    temporary=build/'temp'
    temporary.mkdir(exist_ok=True)
    os.environ['TEMP']=str(temporary)
    os.environ['TMP']=str(temporary)
    vs = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
    clang = args.clang or newest(vs.glob("*/*/VC/Tools/Llvm/x64/bin/clang.exe"))
    clang = clang.resolve()
    clangxx = clang.with_name("clang++.exe")
    resource_root = clang.parent.parent / "lib/clang"
    asan = newest(resource_root.glob("*/lib/windows/clang_rt.asan_dynamic-x86_64.dll"))
    vc_root = clang.parents[4]
    crt = newest((vc_root / "Redist/MSVC").glob("*/x64/Microsoft.VC*.CRT/vcruntime140.dll"))
    sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"
    rc = newest(sdk.glob("*/x64/rc.exe"))
    manifest_tool = rc.with_name("mt.exe")
    ninja = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin")) / "ninja.exe"
    for path in (clang, clangxx, asan, crt, rc, manifest_tool, ninja):
        if not path.is_file():
            raise RuntimeError(f"Installed dependency is missing: {path}")
    audit = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "compiler": str(clang), "build_directory": str(build),
        "build_type": "Debug", "runtime_library": "MultiThreadedDLL",
        "sanitizers": ["address", "undefined"], "recover": False,
        "instrumented_libraries": ["multitini_common", "xilinx_supervisor_settings_host"],
        "scope_note": "Other linked libraries retain their existing CMake instrumentation settings.",
        "runtime_dlls": [{"source": str(path), "sha256": sha256(path)} for path in (asan, crt)],
        "commands": [], "passed": False,
    }

    def run(arguments, log_name):
        command = GUARD + [str(value) for value in arguments]
        audit["commands"].append(command)
        with (build / log_name).open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((build / log_name).read_text(encoding="utf-8", errors="replace"))
            raise RuntimeError(f"Command failed; see {build / log_name}")

    try:
        run([clang, "--version"], "compiler-version.log")
        audit["compiler_version"] = (build / "compiler-version.log").read_text().strip()
        run(["cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Debug", "-DMULTITINI_SANITIZE=ON",
             "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
             f"-DCMAKE_C_COMPILER={clang.as_posix()}", f"-DCMAKE_CXX_COMPILER={clangxx.as_posix()}",
             f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}", f"-DCMAKE_RC_COMPILER={rc.as_posix()}",
             f"-DCMAKE_MT={manifest_tool.as_posix()}"], "configure.log")
        run(["cmake", "--build", build, "-j", str(args.jobs)], "build.log")
        # DLL search tries the executable directory before system locations.
        # PATH alone may select an incompatible already-installed VC runtime.
        destinations = {path.parent for path in build.rglob("*.exe")}
        for destination in destinations:
            for dependency in (asan, crt):
                shutil.copy2(dependency, destination / dependency.name)
        audit["runtime_destinations"] = sorted(str(path) for path in destinations)
        run(["ctest", "--test-dir", build, "--output-on-failure", "--timeout", "30"], "ctest.log")
        audit["passed"] = True
        print((build / "ctest.log").read_text(encoding="utf-8", errors="replace"))
    finally:
        (build / "validation.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
