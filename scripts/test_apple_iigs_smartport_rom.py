#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute assembled SmartPort firmware on the production65816 with an independent MMIO model."""
from __future__ import annotations
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
from build_apple_iigs_smartport_rom import build as build_rom
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-smartport-rom-tests")
    p.add_argument("--acme",default="C:/Users/hasse/tools/acme/acme.exe" if os.name=="nt" else "acme")
    a=p.parse_args();out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    rom=build_rom(out/"assets",a.acme)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"));env=dict(os.environ)
    env["PATH"]=str(runtime)+os.pathsep+env.get("PATH","");env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    cpu=ROOT/"third_party/apple-iigs/upstream/rtl/65C816"
    sources=[cpu/"P65C816_pkg.sv"]+sorted(f for f in cpu.glob("*") if f.suffix in (".sv",".v") and f.name not in ("P65C816_pkg.sv","P65C816.sv","mcode.sv"))+[ROOT/"hdl/core/apple_iigs/ports/P65C816.sv",ROOT/"hdl/core/apple_iigs/ports/mcode.sv"]
    cpp=ROOT/"hdl/sim/apple_iigs_smartport_rom.cpp"
    tracked=sources+[cpp,ROOT/"hdl/core/apple_iigs/roms/smartport.a65",ROOT/"scripts/build_apple_iigs_smartport_rom.py",Path(__file__).resolve()]
    hashes=lambda:{f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in tracked}
    before=hashes();model=out/"model";exe=model/"rom-tests.exe"
    def run(cmd,name):
        with (out/(name+".log")).open("w") as f:r=subprocess.run([str(s).replace("\\","/") for s in cmd],cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT)
        text=(out/(name+".log")).read_text(errors="replace")
        if r.returncode:print(text[-10000:]);raise ValueError(f"SmartPort{name}failed")
        return text
    stamp=out/"model_inputs.json"
    if not exe.exists() or not stamp.exists() or json.loads(stamp.read_text())!=before:
        run([runtime/"verilator_bin.exe","--cc","--public-flat-rw","--top-module","P65C816","--Mdir",model,"-Wno-fatal",*sources],"generate")
        inc=runtime.parent/"share/verilator/include"
        run([runtime/"g++.exe","-std=c++20","-O2","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(model),*sorted(model.glob("*.cpp")),cpp,inc/"verilated.cpp",inc/"verilated_dpi.cpp",inc/"verilated_threads.cpp","-o",exe],"compile")
        stamp.write_text(json.dumps(before,indent=2)+"\n")
    text=run([exe,rom],"simulate");print(text.strip())
    match=re.search(r"SmartPort ROM: (\d+) exact cases, (\d+) DMA initiations PASS",text)
    stack=re.search(r"SmartPort stack: peak(\d+) bytes including caller/helper frames PASS",text)
    if not match or not stack or int(stack[1])!=35 or hashes()!=before:raise ValueError("Missing result, invalid stack peak or changed sources")
    result={"schema":1,"status":"PASS","files":before,"ROM":json.loads((rom.parent/"provenance.json").read_text()),
        "cases":int(match[1]),"DMA_initiations":int(match[2]),"peak_stack_bytes_including_caller_helpers":int(stack[1]),"checks":["8unitstandard/extendedSTATUS,DIB,READ,WRITE","banked4bytepointerand32bitLBA","exact3/5inline-returnskipincludingpage/bank0wrap","stackpagewrap,SP/E/I/D/B/scratchpreservation","exact35bytepeakstackincludingcallerandhelperframes","actualparameterlistandresponse/controlspanoverflowguards","offline/protected/badblock/errorfromF1afterstaleF0","hostcount8evenholes,emptydriveDIB","INIT,resetCONTROL,preformattedFORMATandblock-onlyerrors","legacyProDOSandboot/fallback"],
        "limits":["ProductionCPU executesROM against independentmockcontroller; nativeMMIO,DDRcachecoherence,actualROM/System6boot,routedtimingandphysicalhardware require separateintegration gates."]}
    (out/"validation.json").write_text(json.dumps(result,indent=2)+"\n");return 0
if __name__=="__main__":raise SystemExit(main())
