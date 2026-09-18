#!/usr/bin/env python3
"""Build/run the actual board video settings handlers with mocked MMIO/storage.

GCC whole-program LTO removes unrelated firmware startup/services from the
included main.c; the settings encoder and video handlers are compiled unchanged.
Every native compiler/test launch uses run_native_tool.py on Windows and Linux.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=("C:/msys64/ucrt64/bin/gcc.exe" if os.name == "nt" else "gcc"))
    parser.add_argument("--build-dir", type=Path, default=root / "build/video-settings-runtime")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / ("test_video_settings_runtime.exe" if os.name == "nt" else "test_video_settings_runtime")
    launcher = [sys.executable, str(root / "scripts/run_native_tool.py"), "--cwd", str(root), "--"]
    compile_command = [
        args.cc, "-std=c11", "-O2", "-flto", "-fwhole-program",
        "-ffunction-sections", "-fdata-sections", "-UNDEBUG",
        "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion",
        "-Wshadow", "-Wstrict-prototypes", "-Itests/xilinx_stubs",
        "-Ifirmware/include", "-Ifirmware/platform/xilinx",
        "tests/host/test_video_settings_runtime.c",
        "firmware/platform/xilinx/supervisor_settings.c",
        "firmware/common/video_effects.c", "firmware/common/video_effects_frontend.c",
        "-Wl,--gc-sections", "-lm", "-o", str(binary),
    ]
    for command in (compile_command, [str(binary)]):
        result = subprocess.run(launcher + command, check=False)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
