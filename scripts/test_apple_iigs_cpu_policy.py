#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove that the CPU boundary policy rejects changed results and missing tags."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from test_apple_iigs_cpu import PIN, REGISTERS, ROOT

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-cpu-optimized")
    parser.add_argument("--vectors",type=Path,default=ROOT/"build/65816-single-step")
    args=parser.parse_args()
    build=args.build_dir.resolve();executable=build/"model/cpu-tests.exe"
    policy_path=ROOT/"hdl/sim/apple_iigs_cpu_known_vectors.json"
    policy=json.loads(policy_path.read_text())
    if policy["commit"]!=PIN:raise ValueError("Policy source pin changed")
    entry=next(v for v in policy["vectors"] if v["kind"]=="extended_stack")
    vector_path=args.vectors/"v1"/entry["file"]
    raw=vector_path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=policy["vector_file_sha256"][entry["file"]]:
        raise ValueError("Policy source file changed")
    test=json.loads(raw)[entry["index"]]
    canonical=json.dumps(test,sort_keys=True,separators=(",",":")).encode()
    if hashlib.sha256(canonical).hexdigest()!=entry["sha256"]:raise ValueError("Policy source vector changed")
    results=[]
    for case in ("known_control","missing_allowlist_tag","changed_final_register","changed_write_data"):
        boundary=copy.deepcopy(entry)
        tagged=case!="missing_allowlist_tag"
        if case=="changed_final_register":boundary["wdc_final"]["a"]^=1
        if case=="changed_write_data":boundary["wdc_writes"][1][1]^=1
        vectors=build/(case+".bin")
        with vectors.open("wb") as f:
            def write(*values):f.write(struct.pack("<"+"I"*len(values),*values))
            def pairs(values):
                write(len(values))
                for address,value in values:write(address,value)
            write(1,int(tagged),0xfc,entry["index"],int(tagged))
            for state in ("initial","final"):write(*(test[state][key] for key in REGISTERS))
            for state in ("initial","final"):pairs(test[state]["ram"])
            write(len(test["cycles"]))
            if tagged:
                write(*(boundary["wdc_final"][key] for key in REGISTERS))
                pairs(boundary["wdc_final"]["ram"]);pairs(boundary["wdc_writes"])
        env=os.environ.copy();env["PATH"]="C:/msys64/ucrt64/bin"+os.pathsep+env.get("PATH","")
        result=subprocess.run([executable,vectors],text=True,capture_output=True,env=env)
        output=result.stdout+result.stderr;(build/(case+".log")).write_text(output)
        expected=0 if case=="known_control" else 1
        if result.returncode!=expected:raise ValueError(case+" returned unexpected status: "+output)
        results.append({"case":case,"expected_exit_status":expected,"actual_exit_status":result.returncode,"output":output.strip()})
        print("PASS: "+case+(" classified separately" if expected==0 else " rejected"))
    files=[policy_path,ROOT/"hdl/sim/apple_iigs_single_step.cpp",Path(__file__).resolve()]
    (build/"policy-validation.json").write_text(json.dumps({"schema_version":1,"commit":PIN,
        "files":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files},"checks":results},indent=2)+"\n")
    return 0

if __name__=="__main__":raise SystemExit(main())
