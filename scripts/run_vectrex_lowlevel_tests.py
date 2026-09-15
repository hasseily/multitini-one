#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run the isolated CodeSprite backend through the native guard."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bios",type=Path,help="Optional local 8192-byte Vectrex BIOS")
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    build=root/"build/vectrex-lowlevel"
    build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    cxx=str(runtime/"g++.exe") if os.name=="nt" else "g++"
    executable=build/("lowlevel_tests.exe" if os.name=="nt" else "lowlevel_tests")
    launcher=[sys.executable,"-u",str(root/"scripts/run_native_tool.py"),"--"]
    compile_command=[cxx,"-std=c++17","-O2","-Wall","-Wextra","-Wpedantic","-Werror",
        "-fwrapv","-fno-strict-aliasing",str(root/"firmware/vectrex32/lowlevel.cpp"),
        str(root/"firmware/vectrex32/lowlevel_tests.cpp"),"-o",str(executable)]
    execute=[str(executable)]+([str(args.bios.resolve())] if args.bios else [])
    for command in (compile_command,execute):
        result=subprocess.run(launcher+command,cwd=root,check=False)
        if result.returncode:return result.returncode
    return 0
if __name__=="__main__":raise SystemExit(main())
