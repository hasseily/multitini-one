#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run pinned independent SingleStepTests against the imported 65C816 CPU."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import urlopen

ROOT=Path(__file__).resolve().parents[1]
PIN="c5f9c8b32c7d9a207340df6960571bbda252c8fa"
REGISTERS=("pc","s","p","a","x","y","dbr","d","pbr","e")
DEFAULT_OPS=("08","22","28","40","69","6b","cb","db","e9")

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vectors",type=Path,default=ROOT/"build/65816-single-step")
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-cpu-tests")
    parser.add_argument("--opcodes",nargs="+",default=DEFAULT_OPS)
    parser.add_argument("--all-opcodes",action="store_true",help="run all256 opcodes in both modes")
    parser.add_argument("--fetch",action="store_true",help="download missing pinned MIT vectors into the ignored build checkout")
    parser.add_argument("--fetch-only",action="store_true",help="download without running the CPU")
    parser.add_argument("--accept-documented-boundaries",action="store_true",help="separately classify the44 pinned WDC boundary disagreements; keep unexpected failures fatal")
    parser.add_argument("--limit",type=int,default=0,help="test this many per opcode/mode; zero selects all10000")
    args=parser.parse_args()
    if args.all_opcodes:args.opcodes=[f"{value:02x}" for value in range(256)]
    head=subprocess.check_output(["git","-C",str(args.vectors),"rev-parse","HEAD"],text=True).strip()
    if head!=PIN:raise ValueError("65816 vectors differ from pinned source")
    build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    paths=[args.vectors/"v1"/(op+"."+mode+".json") for op in args.opcodes for mode in ("e","n")]
    if args.fetch or args.fetch_only:
        entries=subprocess.check_output(["git","-C",str(args.vectors),"ls-tree","-r",PIN],text=True)
        blob_hashes={line.split("\t",1)[1]:line.split()[2] for line in entries.splitlines()}
        def fetch(path):
            relative=path.relative_to(args.vectors).as_posix()
            expected=blob_hashes[relative]
            def blob_hash(data):return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
            if path.is_file() and blob_hash(path.read_bytes())==expected:return False
            url="https://raw.githubusercontent.com/SingleStepTests/65816/"+PIN+"/"+relative
            with urlopen(url,timeout=60) as response:data=response.read()
            if blob_hash(data)!=expected:raise ValueError("Pinned test vector Git blob changed: "+relative)
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            return True
        completed=0;downloaded=0
        with ThreadPoolExecutor(max_workers=4) as pool:
            tasks=[pool.submit(fetch,path) for path in paths+[args.vectors/"LICENCE",args.vectors/"README.md"]]
            for task in as_completed(tasks):
                downloaded+=task.result();completed+=1
                if completed%32==0:print(f"65816 pinned input fetch: {completed}/{len(tasks)} checked",flush=True)
        print(f"65816 pinned inputs: {len(tasks)} checked, {downloaded} downloaded",flush=True)
        if args.fetch_only:return 0
    hashes={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    policy_path=ROOT/"hdl/sim/apple_iigs_cpu_known_vectors.json"
    policy=json.loads(policy_path.read_text())
    if policy["commit"]!=PIN or len(policy["vectors"])!=44:
        raise ValueError("Documented boundary policy source/count changed")
    known={(entry["file"],entry["index"]):entry for entry in policy["vectors"]}
    if len(known)!=44:raise ValueError("Duplicate documented boundary vector")
    if args.accept_documented_boundaries:
        for name,expected in policy["vector_file_sha256"].items():
            if name in hashes and hashes[name]!=expected:
                raise ValueError("Documented boundary vector file changed: "+name)
    selected_boundaries=[]
    vectors=build/"vectors.bin";count=0
    with vectors.open("wb") as f:
        f.write(struct.pack("<II",0,0))
        for path in paths:
            tests=json.loads(path.read_text())
            for index,test in enumerate(tests[:args.limit] if args.limit else tests):
                def write(*values):f.write(struct.pack("<"+"I"*len(values),*values))
                boundary=known.get((path.name,index)) if args.accept_documented_boundaries else None
                if boundary:
                    canonical=json.dumps(test,sort_keys=True,separators=(",",":")).encode()
                    if test["name"]!=boundary["name"] or hashlib.sha256(canonical).hexdigest()!=boundary["sha256"]:
                        raise ValueError("Documented boundary vector changed: "+test["name"])
                    selected_boundaries.append(boundary)
                write(int(path.name[:2],16),index,0 if not boundary else 1 if boundary["kind"]=="extended_stack" else 2)
                for state in ("initial","final"):write(*(test[state][key] for key in REGISTERS))
                for state in ("initial","final"):
                    write(len(test[state]["ram"]))
                    for address,value in test[state]["ram"]:write(address,value)
                write(len(test["cycles"]));count+=1
                if boundary:
                    write(*(boundary["wdc_final"][key] for key in REGISTERS))
                    for pairs in (boundary["wdc_final"]["ram"],boundary["wdc_writes"]):
                        write(len(pairs))
                        for address,value in pairs:write(address,value)
        f.seek(0);f.write(struct.pack("<II",count,len(selected_boundaries)))
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy();env["PATH"]=str(runtime)+os.pathsep+env.get("PATH","")
    env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    cpu=ROOT/"third_party/apple-iigs/upstream/rtl/65C816"
    sources=[cpu/"P65C816_pkg.sv"]+sorted(path for path in cpu.glob("*") if path.suffix in (".sv",".v") and path.name not in ("P65C816_pkg.sv","P65C816.sv","mcode.sv"))+[ROOT/"hdl/core/apple_iigs/ports/P65C816.sv",ROOT/"hdl/core/apple_iigs/ports/mcode.sv"]
    directory=build/"model";executable=directory/"cpu-tests.exe"
    model_inputs={path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sources+[ROOT/"hdl/sim/apple_iigs_single_step.cpp",policy_path,Path(__file__).resolve()]}
    stamp=build/"model_inputs.json"
    def run(command,stage):
        log=build/(stage+".log")
        with log.open("w") as f:result=subprocess.run([str(value).replace("\\","/") for value in command],cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        text=log.read_text(errors="replace")
        if result.returncode:print(text[-12000:]);raise ValueError(f"65816 {stage} failed: {log}")
        return text
    if not executable.is_file() or not stamp.is_file() or json.loads(stamp.read_text())!=model_inputs:
        run([runtime/"verilator_bin.exe","--cc","--public-flat-rw","--top-module","P65C816","--Mdir",directory,"-Wno-fatal",*sources],"generate")
        include=runtime.parent/"share/verilator/include"
        run([runtime/"g++.exe","-std=c++20","-O2","-pthread","-DVL_TIME_CONTEXT","-I"+str(include),"-I"+str(include/"vltstd"),"-I"+str(directory),
             *sorted(directory.glob("*.cpp")),ROOT/"hdl/sim/apple_iigs_single_step.cpp",
             include/"verilated.cpp",include/"verilated_dpi.cpp",include/"verilated_threads.cpp","-o",executable],"compile")
        stamp.write_text(json.dumps(model_inputs,indent=2)+"\n")
    command=[executable,vectors]
    output=run(command,"simulate");print(output.strip())
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"vector_source":"https://github.com/SingleStepTests/65816",
        "commit":PIN,"vector_files":hashes,"files":model_inputs,"tests":count,"checks":["final registers","final relevant RAM"],
        "documented_boundary_policy":args.accept_documented_boundaries,"result":output.strip(),
        "documented_boundaries":selected_boundaries,
        "boundary_reference":"https://www.wdc65xx.com/wdc/documentation/w65c816s.pdf page50 sections7.1 and7.2.2",
        "limits":["Single instruction injection; bus-cycle accuracy and interrupts/WAI/STP sequencing require directed tests.",
                  "44 documented vector disagreements are counted separately; they are not claimed as matching golden passes."]},indent=2)+"\n")
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
