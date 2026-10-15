#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the acceleration compatibility gates and real shadow-RAM CPU throughput."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-accel-gates-tests")
    p.add_argument("--baseline-iigs",type=Path,help="also prove all three gates fail using this preserved old iigs.sv")
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
            print(text[-16000:]);raise ValueError(f"acceleration gates {name} failed: {log}")
        return text
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/"multitini_apple_iigs_loader.sv",core/"multitini_apple_iigs_cache.sv",core/"multitini_apple_iigs_adapter.sv",
        ROOT/"hdl/sim/tb_apple_iigs_accel_gates.sv"]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT/"scripts/port_apple_iigs_core.py",Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes();directory=build/"obj";top="tb_apple_iigs_accel_gates"
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
    if out.count("PASS:")!=8 or "COMPARE shadow_ram=49664" not in out or "COMPARE mounted_motor=64" not in out:
        raise ValueError("acceleration gate groups/comparison coverage did not all pass")
    rows=[{"workload":work,"speed":int(s),"primary_ticks":int(t),"cpu_advances":int(c)} for work,s,t,c in
          re.findall(r"MEASURE (shadow_kernel|native_write_kernel) speed=(\d+) primary_ticks=(\d+) cpu_advances=(\d+)",out)]
    if [(row["workload"],row["speed"]) for row in rows]!=[(w,s) for w in ("shadow_kernel","native_write_kernel") for s in (0,6)]:
        raise ValueError("missing real CPU shadow-read/native-write kernel rates")
    for row in rows:row["measured_hz"]=row["cpu_advances"]*57272728/row["primary_ticks"]
    negatives=[]
    if a.baseline_iigs:
        baseline=a.baseline_iigs.resolve()
        old_sources=[baseline if path==core/"ports/iigs.sv" else path for path in sources]
        olddir=build/"baseline-obj"
        run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
            "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),
            "--Mdir",olddir,*old_sources],"baseline-generate")
        oldexe=olddir/("V"+top+(".exe" if os.name=="nt" else ""))
        if os.name=="nt":
            run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(olddir),
                *sorted(olddir.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",oldexe],"baseline-compile")
        else:run([tool("make"),"-C",olddir,"-f","V"+top+".mk","-j8"],"baseline-compile")
        for scenario,message in (("ONLY_PADDLE","IRQ dispatch falsely arms paddle holdoff"),("ONLY_SHADOW","fast shadow RAM read incorrectly classified native"),
                                 ("ONLY_MOTOR","unmounted IWM motor latch falsely clamps accelerator")):
            log=build/("negative-"+scenario.lower()+".log")
            with log.open("w") as stream:r=subprocess.run([str(oldexe),"+"+scenario],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
            text=log.read_text(errors="replace")
            if r.returncode==0 or message not in text:raise ValueError("baseline negative did not fail the intended gate: "+scenario)
            negatives.append({"scenario":scenario,"expected_failure":message,"exit_code":r.returncode,"log_sha256":hashlib.sha256(log.read_bytes()).hexdigest()})
    if hashes()!=before:raise ValueError("gate sources changed during regression; rerun current sources")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"result":"PASS","files":before,
        "measurements":rows,"negative_controls":negatives,
        "baseline_iigs_sha256":hashlib.sha256(a.baseline_iigs.read_bytes()).hexdigest() if a.baseline_iigs else None,
        "checks":["C071-C07F read/write bank aliases never trigger paddle compatibility delay",
            "C070 read/write and C064-C067 retain four-ms delay and restore acceleration",
            "IOLC-inhibited C070 RAM has no IO side effect",
            "all shadow/config RAM read/write classes and direct E0/E1 native RAM/ROM",
            "actual DDR and native slow-RAM shadow write copies, exact single retirement",
            "all64 mounted-family/motor combinations preserve raw status and native motor cadence",
            "empty-drive IWM command retains separate two-ms access hold then restores acceleration",
            "real 65816 IRQ dispatch enters shadow-RAM warm loop at stock and maximum",
            "native shadow-store workload preserves1024 exact writes at each rate"],
        "limits":["Synthetic ROM only; no full-system boot, FPGA timing or physical hardware claim.",
            "Shadow-read loop excludes256 warmup iterations; native-store loop excludes16 warmup iterations.",
            "CPU advances include instruction-internal cycles. Native-store rates include DDR and native bridge latency.",
            "57.272728MHz is the nominal primary clock, not a measured physical oscillator."]},indent=2)+"\n")
    print(out.strip());return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
