#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify real native/emulation IRQ vectors honor the IOLC RAM mapping."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-iolc-vectors-tests")
    p.add_argument("--upstream-negative-control",action="store_true",help="reproduce incorrect ROM vectors under IOLC with the pinned original upstream MMU")
    a=p.parse_args();build=a.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    tool=lambda name:str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    def run(command,name):
        log=build/(name+".log")
        with log.open("w") as out:r=subprocess.run([str(s).replace("\\","/") for s in command],cwd=build,env=env,stdout=out,stderr=subprocess.STDOUT)
        text=log.read_text(errors="replace")
        if r.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",text):
            print(text[-16000:]);raise ValueError(f"IOLC vectors {name} failed: {log}")
        return text
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/"multitini_apple_iigs_loader.sv",core/"multitini_apple_iigs_cache.sv",core/"multitini_apple_iigs_adapter.sv",
        ROOT/"hdl/sim/tb_apple_iigs_iolc_vectors.sv"]
    if a.upstream_negative_control:
        sources=[s for s in sources if s.name!="mmu.sv"]
        sources.append(ROOT/"third_party/apple-iigs/upstream/rtl/mmu.sv")
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"scripts/port_apple_iigs_core.py",Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes();directory=build/"obj";top="tb_apple_iigs_iolc_vectors"
    run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
        "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),
        "-I"+str(ROOT/"hdl/sim"),"--Mdir",directory,*sources],"generate")
    executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
    if os.name=="nt":
        inc=runtime.parent/"share/verilator/include"
        run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(directory),
            *sorted(directory.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",executable],"compile")
    else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],"compile")
    out=run([executable]+(["+UPSTREAM_NEGATIVE_CONTROL"] if a.upstream_negative_control else []),"simulate")
    if out.count("PASS:")!=8:raise ValueError("all eight speed/mode/IOLC IRQ vector cases must pass")
    if hashes()!=before:raise ValueError("IOLC vector sources changed during regression; rerun current sources")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"result":"PASS","files":before,
        "checks":["Stock and maximum effective CPU speed asserted on both IRQ vector reads with Zip IRQ delay disabled", "Native and emulation IRQ entry from CLI/WAI", "RAM IRQ vector installed by real CPU",
            "Original upstream MMU incorrectly forces ROM under IOLC" if a.upstream_negative_control else "Normal LC retains ROM vector; IOLC exposes RAM vector",
            "Both vector bytes and distinct ISR completion markers"], "upstream_negative_control":a.upstream_negative_control,
        "limits":["Synthetic ROM only; no full-system boot, FPGA timing or physical hardware claim."]},indent=2)+"\n")
    print(out.strip());return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
