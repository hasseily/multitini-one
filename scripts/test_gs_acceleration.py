#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure production GS adapter execution at seven host rates and software100%."""
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

from test_apple_iigs_ports import machine_sources

ROOT=Path(__file__).resolve().parents[1]
PRIMARY_HZ=57272728
# A 2-tick NTSC line stretch every912 native ticks costs about0.22%.
# 0.5% also bounds finite-window phase rounding, without masking cache stalls.
RATE_TOLERANCE=0.005

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/core-gs/acceleration-tests")
    parser.add_argument("--hdd-rom",type=Path,default=ROOT/"build/apple-iigs-assets/hdd.rom")
    parser.add_argument("--bypass-only",action="store_true",help="run only the software cache-disable exact-read test")
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    data=args.hdd_rom.read_bytes()
    if len(data)!=256:raise ValueError("Licensed assembled HDD ROM must be256 bytes")
    fixture=build/"hdd.hex";fixture.write_text("\n".join(f"{b:02x}" for b in data)+"\n")
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    def tool(name):return str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    def run(command,name):
        log=build/(name+".log")
        with log.open("w") as stream:
            result=subprocess.run([str(p).replace("\\","/") for p in command],cwd=build,env=env,
                stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",output):
            print(output[-14000:]);raise ValueError(f"Acceleration {name} failed: {log}")
        return output
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/(name+".sv") for name in
        ("multitini_apple_iigs_loader","multitini_apple_iigs_cache","multitini_apple_iigs_adapter")]
    sources+=[ROOT/"hdl/sim/tb_gs_acceleration.sv"]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"scripts/test_apple_iigs_ports.py",Path(__file__).resolve()]
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();top="tb_gs_acceleration";directory=build/"obj"
    run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
        "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),"--Mdir",directory,*sources],"generate")
    executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
    if os.name=="nt":
        include=runtime.parent/"share/verilator/include"
        run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(include),
            "-I"+str(include/"vltstd"),"-I"+str(directory),*sorted(directory.glob("*.cpp")),
            include/"verilated.cpp",include/"verilated_timing.cpp",include/"verilated_threads.cpp","-o",executable],"compile")
    else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],"compile")
    output=run([executable,"+HDD_ROM="+fixture.as_posix()]+(["+BYPASS_ONLY"] if args.bypass_only else []),"simulate")
    pattern=r"MEASURE speed=(\d+) primary_ticks=(\d+) cpu_advances=(\d+) iterations=(\d+) cache_misses=(\d+) cache_hits=(\d+) cache_wait_ticks=(\d+) software_max=(\d+) cache_bypass=(\d+) memory_reads=(\d+)"
    keys=("speed","primary_ticks","cpu_advances","iterations","cache_misses","cache_hits","cache_wait_ticks","software_max","cache_bypass","memory_reads")
    measurements=[dict(zip(keys,map(int,match))) for match in re.findall(pattern,output)]
    expected=[6] if args.bypass_only else list(range(7))+[6,6]
    if [m["speed"] for m in measurements]!=expected or output.count("PASS:")!=len(expected):
        raise ValueError("Acceleration test did not complete selected speeds/software maximum/cache bypass")
    if hashes()!=before:raise ValueError("Production or test sources changed during acceleration test")
    previous=0.0
    for measure in measurements:
        speed=measure["speed"]
        measure["nominal_hz"]=PRIMARY_HZ/((20,16,12,8,4,2,1)[speed])
        measure["measured_hz"]=measure["cpu_advances"]*PRIMARY_HZ/measure["primary_ticks"]
        measure["nominal_fraction"]=measure["measured_hz"]/measure["nominal_hz"]
        measure["relative_to_native"]=measure["measured_hz"]/(measurements[0]["cpu_advances"]*PRIMARY_HZ/measurements[0]["primary_ticks"])
        if measure["cache_bypass"]:
            if measure["cache_misses"]!=measure["memory_reads"] or measure["cache_misses"]==0 or measure["cache_wait_ticks"]==0:
                raise ValueError(f"Cache bypass did not request every completed CPU read: {measure}")
        elif abs(measure["nominal_fraction"]-1)>RATE_TOLERANCE or (not measure["software_max"] and measure["measured_hz"]<=previous):
            raise ValueError(f"Acceleration speed{speed} rate incorrect: {measure}")
        previous=measure["measured_hz"]
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"files":before,
        "hdd_rom_sha256":hashlib.sha256(data).hexdigest(),"primary_hz":PRIMARY_HZ,
        "rate_tolerance":RATE_TOLERANCE,"measurements":measurements,
        "checks":["production adapter runtime configuration0..6 and software100% maximum","normal ZipGS delay defaults; IRQs enabled by CLI",
            "16384 native 16-bit loop iterations;256 warmup iterations excluded",
            "145151 accepted CPU advances measured for every setting","zero cache misses in measured loop",
            "strict rate ordering and primary divide-by20/16/12/8/4/2/1 within0.5%",
            "exact final CPU registers and native long RAM store"],
        "limits":["Synthetic register-only warm-ROM workload; excludes setup, cache misses, I/O and final store.",
            "57.272728 MHz nominal primary clock; physical oscillator and hardware performance not measured.",
            "Normal IRQ delay deliberately restores native speed with I=1; this fixture executes CLI."]},indent=2)+"\n")
    print(output.strip())
    for m in measurements:
        print(f"Speed{m['speed']}: {m['measured_hz']/1e6:.6f} MHz, {m['relative_to_native']:.6f}x native, {m['nominal_fraction']:.6%} nominal")
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
