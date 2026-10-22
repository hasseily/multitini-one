#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify KEGS slot ownership and mapped-ROM isolation through the real IIgs CPU adapter."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-mmu-slots-tests")
    a=p.parse_args();build=a.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env={key:value for key,value in os.environ.items() if key.upper()!="PATH"}
    env["PATH"]=os.environ.get("PATH",os.environ.get("Path",""))
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    tool=lambda name:str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    def run(command,name):
        log=build/(name+".log")
        with log.open("w") as out:r=subprocess.run([str(s).replace("\\","/") for s in command],cwd=build,env=env,stdout=out,stderr=subprocess.STDOUT)
        text=log.read_text(errors="replace")
        if r.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",text):
            print(text[-16000:]);raise ValueError(f"MMU slots {name} failed: {log}")
        return text
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/"multitini_apple_iigs_loader.sv",core/"multitini_apple_iigs_cache.sv",core/"multitini_apple_iigs_adapter.sv",
        ROOT/"hdl/sim/tb_apple_iigs_mmu_slots.sv"]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"scripts/port_apple_iigs_core.py",Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes();directory=build/"obj";top="tb_apple_iigs_mmu_slots"
    run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
        "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),
        "-I"+str(ROOT/"hdl/sim"),"--Mdir",directory,*sources],"generate")
    executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
    if os.name=="nt":
        inc=runtime.parent/"share/verilator/include"
        run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(directory),
            *sorted(directory.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",executable],"compile")
    else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],"compile")
    out=run([executable],"simulate")
    if out.count("PASS:")!=2:raise ValueError("stock and accelerated slot cases must pass")
    if hashes()!=before:raise ValueError("MMU slot sources changed during regression; rerun current sources")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"result":"PASS","files":before,
        "reference":"KEGS 1.38 moremem.c slot state and config.c ROM01 reserved-bank behavior",
        "checks":["INTCXROM and SLOTC3ROM selection", "C3 read/write INTC8 latch and CFFF read/write release",
            "External slot7 C8 owner with sticky internal override", "Mapped C1-CF accesses retire natively at both speeds",
            "Mapped Cxxx writes preserve hidden LC RAM in banks 00/01/E0/E1", "C071 and firmware aliases in all four I/O banks",
            "IOLC inhibit permits fast-bank Cxxx RAM", "Arkanoid SHR background restore by bank03 LDA/STA at 2000 and 93A0", "ROM01 FC/FD zero reads", "Absent banks 80/E2/F0/F8 zero reads and discarded stores at sizing and general addresses"],
        "limits":["Synthetic ROM only; no full-system boot, FPGA timing or physical hardware claim."]},indent=2)+"\n")
    print(out.strip());return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
