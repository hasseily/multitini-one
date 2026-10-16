#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Probe real CPU/cache write retirement for every primary DDR response phase."""
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

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/core-gs/memory-phase-tests")
    parser.add_argument("--hdd-rom",type=Path,default=ROOT/"build/apple-iigs-assets/hdd.rom")
    parser.add_argument("--speed",type=int,default=6,choices=range(7))
    parser.add_argument("--cache-bypass",action="store_true",help="disable caches through actual ZipGS software registers")
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
    def run(command,name,strict=True):
        log=build/(name+".log")
        with log.open("w") as stream:
            result=subprocess.run([str(p).replace("\\","/") for p in command],cwd=build,env=env,
                stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors="replace")
        if strict and (result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",output)):
            print(output[-14000:]);raise ValueError(f"Memory phase {name} failed: {log}")
        return result.returncode,output
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/(name+".sv") for name in
        ("multitini_apple_iigs_loader","multitini_apple_iigs_cache","multitini_apple_iigs_adapter")]
    sources+=[ROOT/"hdl/sim/tb_gs_memory_phases.sv"]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"scripts/test_apple_iigs_ports.py",Path(__file__).resolve()]
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();top="tb_gs_memory_phases";directory=build/"obj"
    run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
        "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),"--Mdir",directory,*sources],"generate")
    executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
    if os.name=="nt":
        include=runtime.parent/"share/verilator/include"
        run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(include),
            "-I"+str(include/"vltstd"),"-I"+str(directory),*sorted(directory.glob("*.cpp")),
            include/"verilated.cpp",include/"verilated_timing.cpp",include/"verilated_threads.cpp","-o",executable],"compile")
    else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],"compile")
    experiments=[]
    pattern=r"SUMMARY phase=(\d+) delayed_BUFG=(\d+) prime_cache=(\d+) DDR_write_responses=(\d+) phase3_races=(\d+) data_mismatches=(\d+) failures=(\d+)"
    keys=("phase","delayed_bufg","prime_cache","ddr_write_responses","phase3_races","data_mismatches","failures")
    scenarios=[(prime,delayed,phase,None,None,0) for prime in (False,True) for delayed in (False,True) for phase in range(4)]
    scenarios += [(prime,delayed,3,assert_phase,release_phase,0) for prime in (False,True) for delayed in (False,True)
                  for assert_phase in (2,3) for release_phase in (2,3)]
    # Pause a target read-back stalled on its line fill or bypass read (kind 1)
    # or on draining that line's posted writes (kind 2). These runs store and
    # read back one line at a time; unprimed destinations make each line miss.
    scenarios += [(False,False,3,assert_phase,release_phase,kind) for kind in (1,2)
                  for assert_phase in (2,3) for release_phase in (2,3)]
    for prime,delayed,phase,assert_phase,release_phase,kind in scenarios:
        name=f"phase-{phase}-"+("delayed" if delayed else "zero-delay")+("-primed" if prime else "")
        if assert_phase is not None:name+=f"-pause-{assert_phase}-{release_phase}"+(f"-kind{kind}" if kind else "")
        command=[executable,"+HDD_ROM="+fixture.as_posix(),"+RESPONSE_PHASE="+str(phase),"+SPEED="+str(args.speed)]
        if delayed:command.append("+DELAYED_BUFG")
        if prime:command.append("+PRIME_CACHE")
        if args.cache_bypass:command.append("+CACHE_BYPASS")
        if assert_phase is not None:command += ["+PAUSE_ASSERT_PHASE="+str(assert_phase),"+PAUSE_RELEASE_PHASE="+str(release_phase),"+PAUSE_KIND="+str(kind)]
        status,output=run(command,name,strict=False)
        summaries=re.findall(pattern,output)
        summary=dict(zip(keys,map(int,summaries[-1]))) if summaries else None
        holds=re.findall(r"HOLD_CHECKS native=(\d+) pause=(\d+) pause_events=(\d+) late_pause_advances=(\d+) assert_phase=(-?\d+) release_phase=(-?\d+) kind=(\d+)",output)
        hold=dict(zip(("native_hold_checks","pause_hold_checks","pause_events","late_pause_advances","assert_phase","release_phase","pause_kind"),map(int,holds[-1]))) if holds else None
        passed=status==0 and "PASS:" in output and summary is not None and summary["failures"]==0 and hold is not None
        experiments.append({"name":name,"status":"PASS" if passed else "FAIL","exit_status":status,
            "summary":summary,"native_hold":hold,"log":str(build/(name+".log")),
            "diagnostics":[line for line in output.splitlines() if any(s in line for s in
                ("PHASE3_RACE","NATIVE_COMMIT_RACE","DATA_MISMATCH","COUNT_ERROR","CPU_FINAL_ERROR","RAM_ERROR","PAUSE_ERROR","RESPONSE_ERROR","DRAIN_ERROR"))]})
        print(name+": "+("PASS" if passed else "FAIL")+(" "+str(summary) if summary else " missing summary"),flush=True)
    unchanged=hashes()==before
    passed=unchanged and all(test["status"]=="PASS" for test in experiments)
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"status":"PASS" if passed else "FAIL",
        "files":before,"source_hashes_unchanged":unchanged,"hdd_rom_sha256":hashlib.sha256(data).hexdigest(),
        "experiments":experiments,
        "configured_speed":args.speed,"cache_bypass":args.cache_bypass,"checks":["real native CLI CPU at configured speed with normal ZipGS controls",
            "16 unique aligned8-byte words,32 posted byte writes; same-word bytes may merge into16..32 DDR writes",
            "Both initially absent cache lines and preallocated hits with partial-byte merges",
            "DDR accepted lanes, acknowledged lanes, CPU write advances and cache enqueues counted exactly once per byte; accepted lanes carry final data",
            "every CPU write advance is the cache enqueue edge; DDR compared only after memory_busy drains",
            "CPU readback compares every word and reaches the exact final state",
            "receipt phases0..3 with stable half-cycle inputs and varied2..6 cycle minimum delays",
            "Pause assert/release phases2/3 while four posted stores are in flight to DDR; primary CE0 freezes CPU state",
            "Both phase2 and phase3 pause preserve outstanding posted writes and resume exactly once",
            "Read pauses: four target read-backs paused during their line fill or bypass read, or while that line's posted writes drain; each resumes once with the stored byte"],
        "limits":["Synthetic program and auxiliary ROMs; physical board behavior is not measured.",
            "Both zero-delay BUFG and1 ps propagation are tested to expose parent/child delta-order sensitivity."]},indent=2)+"\n")
    if not unchanged:raise ValueError("Production/test sources changed during probe; rerun before trusting evidence")
    return 0 if passed else 1

if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
