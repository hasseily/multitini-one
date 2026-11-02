#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the focused 65C816 IRQ/RTI regression without compiling the machine."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-native-interrupt-tests")
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/"build"):
        raise ValueError("test output must stay under the repository build directory")
    build.mkdir(parents=True,exist_ok=True)
    cpu=ROOT/"third_party/apple-iigs/upstream/rtl/65C816"
    sources=[cpu/"P65C816_pkg.sv"]+sorted(p for p in cpu.glob("*") if p.suffix in (".sv",".v") and p.name not in ("P65C816_pkg.sv","P65C816.sv","mcode.sv"))
    sources += [ROOT/"hdl/core/apple_iigs/ports"/name for name in ("P65C816.sv","mcode.sv")]
    sources += [ROOT/"hdl/sim/tb_apple_iigs_interrupts.sv"]
    inputs=sources+[Path(__file__).resolve()]
    def hashes():
        return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before=hashes()
    captured=build/"captured"
    for source in inputs:
        target=captured/source.relative_to(ROOT)
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(source.read_bytes())
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    def tool(name):
        return str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    commands={}
    def run(command,name):
        command=[str(part).replace("\\","/") for part in command]
        commands[name]=command
        log=build/(name+".log")
        with log.open("w") as stream:
            result=subprocess.run(command,cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",output):
            raise ValueError(f"IRQ/RTI {name} failed: {log}\n{output[-10000:]}")
        return output
    top="tb_apple_iigs_interrupts"
    obj=build/"obj"
    record={"schema_version":1,"status":"RUNNING","files":before,"commands":commands,
            "limits":["Synthetic CPU-only execution; no native machine bridge, GS/OS boot, FPGA timing or hardware proof."]}
    try:
        run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing","-Wno-fatal","--top-module",top,"--Mdir",obj,
             *(captured/p.relative_to(ROOT) for p in sources)],"generate")
        executable=obj/("V"+top+(".exe" if os.name=="nt" else ""))
        if os.name=="nt":
            include=runtime.parent/"share/verilator/include"
            run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(include),"-I"+str(include/"vltstd"),"-I"+str(obj),
                 *sorted(obj.glob("*.cpp")),include/"verilated.cpp",include/"verilated_timing.cpp",include/"verilated_threads.cpp","-o",executable],"compile")
        else:
            run([tool("make"),"-C",obj,"-f","V"+top+".mk","-j8"],"compile")
        output=run([executable],"simulate")
        if "PASS: ten native IRQ/RTI cases" not in output:
            raise ValueError("native IRQ/RTI PASS marker absent")
        if hashes()!=before:
            raise ValueError("IRQ/RTI source changed while testing")
        record["status"]="PASS"
        record["native_cases"]=[line for line in output.splitlines() if line.startswith("NATIVE_IRQ_RTI ")]
        print(output.strip())
    except Exception as error:
        record["status"]="FAIL"
        record["error"]=str(error)
        raise
    finally:
        record["evidence_files"]={name:{"bytes":path.stat().st_size,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
            for name in ("generate.log","compile.log","simulate.log") if (path:=build/name).exists()}
        (build/"validation.json").write_text(json.dumps(record,indent=2)+"\n")
    return 0


if __name__=="__main__":
    try:
        raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}",file=sys.stderr)
        raise SystemExit(1)
