#!/usr/bin/env python3
"""Run a build/test tool with its Windows runtime and no modal loader dialogs.

Usage: python scripts/run_native_tool.py [--cwd DIR] -- TOOL [ARG ...]
The environment change is confined to this process and its children.
"""

from __future__ import annotations

import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a tool command is required after --")

    environment = os.environ.copy()
    flags = 0
    if os.name == "nt":
        # Child processes inherit this mode: loader/critical failures return
        # errors to automation instead of blocking the user's desktop.
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
        runtime = Path(environment.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
        required = ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
        missing = [name for name in required if not (runtime / name).is_file()]
        if missing:
            print(f"Missing UCRT runtime files in {runtime}: {', '.join(missing)}",
                  file=sys.stderr)
            return 2
        environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", "")
        flags = subprocess.CREATE_NO_WINDOW
    try:
        process = subprocess.Popen(command, cwd=args.cwd, env=environment,
                                   creationflags=flags, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   errors="replace", bufsize=1)
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
        returncode = process.wait()
    except OSError as error:
        print(f"Could not start {command[0]}: {error}", file=sys.stderr)
        return 2
    if returncode:
        print(f"Tool failed: {command[0]} (exit {returncode:#x})",
              file=sys.stderr)
    return returncode if 0 <= returncode <= 255 else 1


if __name__ == "__main__":
    raise SystemExit(main())
