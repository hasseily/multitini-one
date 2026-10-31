#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare production native CPU phase expressions to the pinned MiSTer top."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT / "build/amiga-phase-proof")
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build"):raise SystemExit("Use a repository build directory")
    build.mkdir(parents=True,exist_ok=True)
    upstream=ROOT / "third_party/amiga/upstream/Minimig.sv"
    wrapper=ROOT / "hdl/core/amiga/multitini_amiga_machine.sv"
    clocks=ROOT / "hdl/core/amiga/ports/amiga_clk.v"
    source=upstream.read_text()
    start=source.index("always @(posedge clk_114) begin",source.index("wire cpu_type = cpucfg[1]"))
    end=source.index("wire  [1:0] cpu_state;",start)
    reference=source[start:end]
    reference=reference.replace("clk_114","fast").replace("cpu_ph1","ph1").replace("cpu_ph2","ph2")
    reference,count=re.subn(r"\s*ram_cs <=[^;]+;","",reference)
    if count!=1:raise SystemExit("Pinned phase reference changed")
    actual=wrapper.read_text()
    expressions=[]
    for name in ("cpu_ph1","cpu_ph2"):
        match=re.search(rf"wire\s+{name}\s*=\s*([^;]+);",actual)
        if match is None:raise SystemExit(f"Missing production expression {name}")
        expressions.append(match.group(1).replace("cpu_reset_n","cpu_rst").replace("chip_c1","c1").replace("chip_c3","c3"))
    template="""// Generated original test driver; reference phase block is GPL-3.0-or-later
// MiSTer Minimig.sv by Alexey Melnikov; full source/notices remain in third_party.
`timescale 1ns/1ps
module proof;
 reg clk=0,reset=1,cpu_rst=0,run=1;always #5 clk=~clk;
 reg[1:0]phase=0;
 reg gate=0;always_latch if(!clk)gate=reset||run||(phase!=1);
 wire fast=clk&&gate;
 always@(posedge fast or posedge reset)if(reset)phase<=0;else phase<=phase+1'b1;
 wire native=phase[1];wire en7,en7n,c1,c3,cck;wire[9:0]eclk;
 amiga_clk clocks(.clk_28(native),.clk7_en(en7),.clk7n_en(en7n),.c1(c1),.c3(c3),.cck(cck),.eclk(eclk),.reset_n(!reset));
 reg ph1=0,ph2=0,cyc=0;
REFERENCE
 integer native_edges=0,checked=0;always@(posedge native)begin
  native_edges=native_edges+1;
  if(cpu_rst&&native_edges>20)begin
   if(ph1!==(PH1) || ph2!==(PH2))$fatal(1,"phase mismatch edge%0d ref%b%b C1C3=%b%b",native_edges,ph1,ph2,c1,c3);
   checked=checked+1;
  end
 end
 integer i;
 initial begin
  repeat(8)@(negedge clk);reset=0;repeat(80)@(negedge clk);cpu_rst=1;
  for(i=0;i<400;i=i+1)begin
   repeat(1+(i%41))@(negedge clk);run=0;repeat(1+(i%19))@(negedge clk);run=1;
   if(i%27==0)begin cpu_rst=0;repeat(33)@(negedge clk);cpu_rst=1;end
  end
  if(checked<1000)$fatal(1,"insufficient checks");
  $display("PASS exact native CPU phase table: %0d sampled edges",checked);$finish;
 end
endmodule
"""
    tb=build / "phase_proof.sv"
    tb.write_text(template.replace("REFERENCE",reference).replace("PH1",expressions[0]).replace("PH2",expressions[1]))
    inputs=[upstream,wrapper,clocks,Path(__file__).resolve(),tb]
    receipt={"schema_version":1,"board_evidence":False,"sources":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    guard=[sys.executable,str(ROOT / "scripts/run_native_tool.py"),"--"]
    commands=[("compile",["C:/msys64/ucrt64/bin/iverilog.exe","-g2012","-s","proof","-o",str(build / "proof.vvp"),str(clocks),str(tb)]),
              ("simulate",["C:/msys64/ucrt64/bin/vvp.exe",str(build / "proof.vvp")])]
    for name,command in commands:
        result=subprocess.run(guard+command,cwd=ROOT,capture_output=True,text=True)
        (build / f"{name}.log").write_text(result.stdout+result.stderr)
        if result.returncode:print(result.stdout+result.stderr);return result.returncode
    if "PASS exact native CPU phase table" not in result.stdout:raise SystemExit("Phase proof did not complete")
    for name,expected in receipt["sources"].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest()!=expected:raise SystemExit(f"Input changed during phase proof: {name}")
    receipt["result"]="passed"
    (build / "report.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(result.stdout.strip())
    return 0


if __name__=="__main__":raise SystemExit(main())
