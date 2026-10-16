#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production Apple IIgs adapter with synthetic CPU and HDD fixtures."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-adapter-tests")
    p.add_argument("--hdd-rom",type=Path,default=ROOT/"build/apple-iigs-assets/hdd.rom")
    p.add_argument("--trace",action="store_true",help="include a bounded CPU address trace")
    p.add_argument("--trace-hdd",action="store_true",help="include native command completion diagnostics")
    a=p.parse_args();build=a.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    data=a.hdd_rom.read_bytes()
    if len(data)!=2304:raise ValueError("licensed, assembled SmartPort ROM must be 2304 bytes")
    fixture=build/"hdd.hex";fixture.write_text("\n".join(f"{b:02x}" for b in data)+"\n")
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
            print(text[-16000:]);raise ValueError(f"adapter {name} failed: {log}")
        return text
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/"multitini_apple_iigs_loader.sv",core/"multitini_apple_iigs_cache.sv",core/"multitini_apple_iigs_adapter.sv",
        ROOT/"hdl/sim/tb_apple_iigs_adapter.sv"]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"hdl/sim/tb_apple_iigs_native_woz_cases.sv",ROOT/"hdl/sim/tb_apple_iigs_smartport_cases.sv",Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes();directory=build/"obj";top="tb_apple_iigs_adapter"
    run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
        "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),
        "-I"+str(ROOT/"hdl/sim"),"--Mdir",directory,*sources],"generate")
    executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
    if os.name=="nt":
        inc=runtime.parent/"share/verilator/include"
        run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(directory),
            *sorted(directory.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",executable],"compile")
    else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],"compile")
    out=run([executable,"+HDD_ROM="+fixture.as_posix()]+(["+TRACE"] if a.trace else [])+(["+TRACE_HDD"] if a.trace_hdd else []),"simulate")
    if out.count("PASS:")!=19:raise ValueError("full adapter did not pass seven CPU speeds, legacy HDD DMA, eight-unit SmartPort stock/max/bypass/reset/actual-ROM/metadata and native WOZ/eject")
    if hashes()!=before:raise ValueError("adapter sources changed during regression; rerun the current sources")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"files":before,"hdd_rom_sha256":hashlib.sha256(data).hexdigest(),
        "checks":["seven configured native 65816 speeds with CLI","native 16-bit long RAM store and STP","cached and software-bypassed HDD DMA reads and writes at57MHz CPU setting",
            "eight SmartPort units at stock/max57 plus cache bypass,32-bit LBA and24-bit bank-crossing DMA with CPU cache coherence",
            "actual licensed C70D/C800 SmartPort ROM extended READ at stock/max,banked parameter/buffer and preserved A/P/X/Y/SP returns",
            "rapid different/same-unit mount metadata,combined DMA-blocked eject masks,native unmount publication before host completion",
            "all 512 write bytes","failed HDD write completes with explicit I/O error and releases CPU",
            "native 3.5 clean eject and dirty eject with simultaneous last write",
            "native bitstream lookahead, known flux prefetch and byte-boundary write read-modify-write",
            "late cache stall release during IWM immediate-mode transition",
            "native eject preserves mounted image and exact track bytes during failed-write retry"],
        "limits":["Synthetic ROM only. No Apple ROM boot, timing closure or physical hardware is implied."]},indent=2)+"\n")
    print(out.strip());return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
