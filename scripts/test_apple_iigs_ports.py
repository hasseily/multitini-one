#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify inferred IIgs RAM/ROM ports and the unchanged CPU stall contract."""
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
UPSTREAM=ROOT/"third_party/apple-iigs/upstream"
PORTS=ROOT/"hdl/core/apple_iigs/ports"

def machine_sources() -> list[Path]:
    cpu=UPSTREAM/"rtl/65C816"
    result=[cpu/"P65C816_pkg.sv"]+sorted(path for path in cpu.glob("*") if path.suffix in (".sv",".v") and path.name not in ("P65C816_pkg.sv","P65C816.sv","mcode.sv"))
    result+=sorted(PORTS.glob("*.sv"))
    result += [UPSTREAM/"rtl"/relative for relative in ("mmu.sv","syncram.v",
        "paddle_timer.v","video_timing.v","clock_divider.v","iwm_woz.v","iwm_flux.v","flux_drive.v","smartport_dev.v",
        "scc_iigs_wrapper.v","scc8530.v")]
    return result

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT/"build/apple-iigs-import-tests")
    parser.add_argument("--skip-cpu", action="store_true", help="omit compiled CPU simulation")
    parser.add_argument("--test",choices=("ports","stall","interrupts","mcode","boundaries"),action="append",help="run selected directed simulations")
    args=parser.parse_args()
    build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    def tool(name):
        return str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    def run(command,name):
        path=build/(name+".log")
        with path.open("w") as stream:
            proc=subprocess.run([str(part).replace('\\','/') for part in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        diagnostic=path.read_text(errors="replace")
        if proc.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",diagnostic):
            print(diagnostic[-14000:]);raise ValueError(f"IIgs {name} failed: {path}")
        return diagnostic
    sources=machine_sources()
    captured=sources+[UPSTREAM/"rtl/dhr_lut.vh",Path(__file__).resolve(),ROOT/"scripts/port_apple_iigs_core.py",
        ROOT/"hdl/sim/tb_apple_iigs_ports.sv",ROOT/"hdl/sim/tb_apple_iigs_cpu_stall.sv",
        ROOT/"hdl/sim/tb_apple_iigs_interrupts.sv",ROOT/"hdl/sim/tb_apple_iigs_mcode.sv",
        ROOT/"hdl/sim/tb_apple_iigs_cpu_boundaries.sv"]
    def hashes():return {path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes()
    verilator=tool("verilator_bin") if os.name=="nt" else tool("verilator")
    def compile_sim(top,hdl):
        directory=build/top
        run([verilator,"--cc","--exe","--main","--timing","-Wno-fatal","--top-module",top,
             "--Mdir",directory,*hdl],top+"-generate")
        executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
        if os.name=="nt":
            # Avoid the MSYS make/Perl runtime; it cannot create its object
            # namespace in this sandbox. Compile the generated C++ directly.
            include=runtime.parent/"share/verilator/include"
            run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT",
                 "-I"+str(include),"-I"+str(include/"vltstd"),"-I"+str(directory),
                 *sorted(directory.glob("*.cpp")),include/"verilated.cpp",
                 include/"verilated_timing.cpp",include/"verilated_threads.cpp","-o",executable],top+"-compile")
        else:
            run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],top+"-compile")
        return executable
    run([verilator,"--lint-only","-Wno-fatal","--top-module","iigs","-I"+str(UPSTREAM/"rtl"),*sources],"machine-lint")
    selected=args.test or (["ports"] if args.skip_cpu else ["ports","stall","interrupts","mcode","boundaries"])
    checks=["complete machine source lint"]
    if "mcode" in selected:
        reference=build/"mcode_reference.sv"
        original=(UPSTREAM/"rtl/65C816/mcode.sv").read_text()
        if original.count("module mcode\n")!=1:raise ValueError("Pinned microcode entry point changed")
        reference.write_text(original.replace("module mcode\n","module mcode_reference\n"))
        executable=compile_sim("tb_apple_iigs_mcode",[UPSTREAM/"rtl/65C816/P65C816_pkg.sv",PORTS/"mcode.sv",reference,ROOT/"hdl/sim/tb_apple_iigs_mcode.sv"])
        output=run([executable],"mcode-simulate")
        if "PASS:" not in output:raise ValueError("IIgs microcode equivalence did not report PASS")
        print(output.strip(),flush=True);checks.append("microcode all2048 words and EN/reset/state0 equivalence")
    if "ports" in selected:
        executable=compile_sim("tb_apple_iigs_ports",[PORTS/"bram.sv",PORTS/"runtime_rom.sv",PORTS/"prtc.sv",ROOT/"hdl/sim/tb_apple_iigs_ports.sv"])
        output=run([executable],"ports-simulate")
        if "PASS:" not in output:raise ValueError("IIgs RAM/ROM test did not report PASS")
        print(output.strip(),flush=True)
        checks += ["inferred BRAM latency/enable/write-first", "runtime ROM loading", "blank and restored PRAM"]
    if any(test in selected for test in ("stall","interrupts","boundaries")):
        cpu=[path for path in sources if path.parent.name=="65C816" or path.name in ("P65C816.sv","mcode.sv")]
        for test in ("stall","interrupts","boundaries"):
            if test not in selected:continue
            name={"stall":"tb_apple_iigs_cpu_stall","interrupts":"tb_apple_iigs_interrupts","boundaries":"tb_apple_iigs_cpu_boundaries"}[test]
            executable=compile_sim(name,cpu+[ROOT/("hdl/sim/"+name+".sv")])
            output=run([executable],test+"-simulate")
            if "PASS:" not in output:raise ValueError("IIgs CPU directed test did not report PASS")
            print(output.strip(),flush=True);checks.append("65C816 "+test)
    if hashes()!=before:raise ValueError("IIgs inputs changed while testing")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"files":before,"checks":checks,
        "limits":["No Apple ROM boot, synthesis/timing, emulator media or physical board tests are implied."]},indent=2)+"\n")
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
