#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain MAME REU comparisons with explicit VICE compatibility differences.

The 19-bit comparison executes verbatim MOS8726 methods. The 24-bit fixtures
are separate directed expectations; MAME does not model a 16 MiB REU here.
Four named cases record the deliberate VICE differences. The independent
test_c64_vice_reu.py comparison is authoritative. Neither test establishes
native transfer throughput or physical DDR proof.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN = "122719e8d13e2fdb7dd33886455484207de59fc8"
REFERENCE = ROOT / "third_party/mame_reference/upstream/src/devices/machine/mos8726.cpp"
REFERENCE_SHA = "f95f408b4bca5986df531138cbbb5cc9c5a61e29ee643882d483182de3068e35"


def extract(source: str, name: str) -> str:
    match = re.search(r"^(?:void|uint8_t) mos8726_device::" + name + r"\([^\n]*\)\n\{", source, re.M)
    if not match:
        raise ValueError("Missing pinned method " + name)
    cursor, depth = match.end(), 1
    while depth:
        if source[cursor] == "{": depth += 1
        elif source[cursor] == "}": depth -= 1
        cursor += 1
    return source[match.start():cursor]


class Vectors:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.cases: list[str] = []

    def put(self, op: int, a: int = 0, b: int = 0) -> None:
        self.lines.append(f"{op} {a:x} {b:x}")

    def case(self, name: str) -> None:
        self.cases.append(name)
        self.put(0, len(self.cases), len(self.cases) % 3)

    def write(self, addr: int, data: int) -> None: self.put(1, addr, data)
    def read(self, addr: int) -> None: self.put(2, addr)
    def setup(self, c64: int, reu: int, length: int, acr: int = 0, imr: int = 0) -> None:
        for addr, data in ((2,c64 & 255),(3,c64 >> 8),(4,reu & 255),(5,(reu >> 8)&255),
                           (6,reu >> 16),(7,length & 255),(8,length >> 8),(9,imr),(10,acr)):
            self.write(addr,data)
    def dump(self) -> None:
        for addr in range(32): self.read(addr)
    def save(self, path: Path) -> None: path.write_text("\n".join(self.lines)+"\n")


def mame_vectors() -> Vectors:
    v = Vectors()
    v.case("reset-and-register-masks")
    v.dump()
    for addr in range(32): v.write(addr, 0xff if addr != 1 else 0x60)
    v.dump()
    for op, label in enumerate(("stash","fetch","swap","verify")):
        for acr in (0,0x40,0x80,0xc0):
            for auto in (False,True):
                v.case(f"{label}-fixed-{acr:02x}-autoload-{int(auto)}")
                v.setup(0x20fe,0x03fffe,4,acr,0xe0)
                if op == 3:
                    # Match the first three accesses, then fail the final one.
                    for i in range(4):
                        ca=0x20fe+(0 if acr&0x80 else i)
                        ra=0x03fffe+(0 if acr&0x40 else i)
                        v.put(6,ra,(ca^(ca>>8)^0x5a)&255)
                v.write(1,0x90|op|(0x20 if auto else 0));v.put(4);v.dump()
    for length in (1,2,5):
        for mismatch in range(length):
            v.case(f"verify-fault-{mismatch}-length-{length}")
            v.setup(0x3000,0x12340,length,0,0xa0)
            for i in range(length):
                value=(0x3000+i)^0x30^0x5a
                v.put(6,0x12340+i,(value^(1 if i==mismatch else 0))&255)
            v.write(1,0x93);v.put(4);v.dump()
    for op in (0,1,2,3):
        v.case(f"wrap-c64-and-512k-op-{op}")
        v.setup(0xffff,0x07ffff,3,0,0xe0)
        v.write(1,0x90|op);v.put(4);v.dump()
    v.case("zero-length-means-65536")
    v.setup(0xfffe,0x7fffe,0)
    v.write(1,0x90);v.put(4);v.dump()
    v.case("deferred-ff00-trigger-and-repeat")
    v.setup(0x2000,0x400,3,0,0xc0)
    v.write(1,0xa0);v.put(4);v.read(1);v.put(3);v.put(4);v.dump()
    v.put(3);v.put(4);v.dump() # completion cleared execute; FF00 must not restart.
    v.case("irq-mask-does-not-retroactively-assert-or-clear")
    v.setup(0x2000,0x400,1)
    v.write(1,0x90);v.put(4);v.write(9,0xc0);v.put(4)
    v.write(1,0x90);v.put(4);v.write(9,0);v.put(4);v.read(0);v.read(0)
    v.case("shadow-byte-updates-after-nonautoload")
    v.setup(0x20ff,0x2ffff,2)
    v.write(1,0x90);v.put(4)
    v.write(2,0x45);v.write(4,0x67);v.write(7,3);v.dump()
    v.write(3,0x50);v.write(5,0x60);v.write(8,0);v.dump()
    return v


def extension_vectors() -> tuple[Vectors,list[str]]:
    v=Vectors(); expected: list[str]=[]; ram: dict[int,int]={}
    def initial(a: int) -> int: return (a^(a>>8)^(a>>16)^0xa5)&255
    def start(name: str) -> None:
        v.case(name);ram.clear();expected.append(f"E {len(v.cases)}")
    def transfer(ca: int,ra: int,data: list[int],fetch: bool=False) -> None:
        v.setup(ca,ra,len(data));v.write(1,0x91 if fetch else 0x90)
        for i,value in enumerate(data):
            c=(ca+i)&65535;r=(ra&0xf80000)|((ra+i)&0x7ffff)
            if fetch:
                value=ram.get(r,initial(r))
                expected.extend((f"M R {r:06x} {value:02x}",f"C W {c:06x} {value:02x}"))
            else:
                v.put(5,c,value)
                ram[r]=value
                expected.extend((f"C R {c:06x} {value:02x}",f"M W {r:06x} {value:02x}"))
        # Initial C64 pokes must precede the command, before DMA can start.
        if not fetch:
            pokes=v.lines[-len(data):];del v.lines[-len(data):]
            command=v.lines.pop();v.lines.extend(pokes);v.lines.append(command)
        if fetch:
            r=(ra&0xf80000)|((ra+len(data))&0x7ffff);expected.append(f"M R {r:06x} {ram.get(r,initial(r)):02x}")
        v.put(4);expected.append("S 0 0")
        finalca=(ca+len(data))&65535;finalra=(ra&0xf80000)|((ra+len(data))&0x7ffff)
        values={0:0x50,1:0x11 if fetch else 0x10,2:finalca&255,3:finalca>>8,
                4:finalra&255,5:(finalra>>8)&255,6:(finalra>>16)|0xf8,7:1,8:0,9:0x1f,10:0x3f}
        for reg,val in values.items():
            v.read(reg);expected.append(f"R {reg:02x} {val:02x} 0")
    start("16mb-upper-window-rec-wrap-both-directions")
    transfer(0xfffe,0xfffffe,[0x12,0x34,0x56]);transfer(0x4000,0xfffffe,[0]*3,True)
    start("full-eight-bank-bits-no-alias")
    for i,addr in enumerate((0x000123,0x080123,0x800123,0xff0123)):
        transfer(0x2000,addr,[0x80+i])
    for addr in (0x000123,0x080123,0x800123,0xff0123):transfer(0x4000,addr,[0],True)
    return v,expected


def compare(actual: list[str], expected: list[str]) -> None:
    if actual != expected:
        first=next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),min(len(actual),len(expected)))
        raise RuntimeError(f"REU trace differs at {first}: actual={actual[first:first+4]}, expected={expected[first:first+4]}; lengths {len(actual)}/{len(expected)}")


def compare_historical_mame(actual: list[str], expected: list[str], names: list[str]) -> list[dict]:
    """Keep MAME bytes intact and gate four documented VICE disagreements.

    These narrow fixture deltas describe historical differences, not a new
    oracle. The independent unchanged VICE-method suite is authoritative.
    Any additional difference, or disappearance of an expected one, fails.
    """
    def groups(lines):
        out={}; current=None
        for line in lines:
            if line.startswith('E '): current=int(line[2:]); out[current]=[]
            else: out[current].append(line)
        return out
    aa,bb=groups(actual),groups(expected)
    if set(aa)!=set(bb) or len(aa)!=len(names): raise RuntimeError('MAME case boundaries differ')
    declared={
        'verify-fault-0-length-2':'VICE peeks the equal last byte after a penultimate mismatch and sets EOB',
        'verify-fault-3-length-5':'VICE peeks the equal last byte after a penultimate mismatch and sets EOB',
        'irq-mask-does-not-retroactively-assert-or-clear':'VICE mask enable raises IRQ from existing completion status',
        'shadow-byte-updates-after-nonautoload':'VICE low/high REU address writes preserve the current bank latch',
    }
    differences=[]
    for i,name in enumerate(names,1):
        a,b=aa[i],bb[i]
        if name not in declared:
            compare(a,b); continue
        if a==b: raise RuntimeError('Expected historical MAME disagreement disappeared: '+name)
        adjusted=b.copy()
        if name.startswith('verify-fault-'):
            addr,reu,value=(0x3001,0x12341,0x6b) if name.endswith('length-2') else (0x3004,0x12344,0x6e)
            at=adjusted.index('S 1 0')
            adjusted[at:at]=[f'C R {addr:06x} {value:02x}',f'M R {reu:06x} {value:02x}']
            adjusted[adjusted.index('R 00 b0 0')]='R 00 f0 0'
        elif name.startswith('irq-mask-'):
            zero_samples=[j for j,row in enumerate(adjusted) if row=='S 0 0']
            if len(zero_samples)!=2: raise RuntimeError('Historical mask fixture changed')
            adjusted[zero_samples[1]]='S 1 0'
        else:
            adjusted=['R 06 fb 0' if row=='R 06 fa 0' else row for row in adjusted]
        compare(a,adjusted)
        differences.append({'case':name,'status':'EXPECTED_DIFFERENCE','reason':declared[name],
                            'rtl_events':len(a),'mame_events':len(b)})
    return differences


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/c64-reu-tests")
    args=parser.parse_args();build=args.build_dir.resolve()
    if build==(ROOT/"build").resolve() or not build.is_relative_to((ROOT/"build").resolve()):parser.error("build dir must be a dedicated build/ child")
    build.mkdir(parents=True,exist_ok=True);receipt=build/"validation.json";receipt.unlink(missing_ok=True)
    paths=[Path(__file__),REFERENCE,ROOT/"tests/c64_mame/reu_harness.cpp",ROOT/"hdl/sim/tb_c64_reu.sv",
           ROOT/"hdl/core/c64/multitini_c64_reu.sv",ROOT/"hdl/sim/tb_c64_reu_transport.sv",ROOT/"scripts/run_native_tool.py"]
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    result={"schema":1,"status":"FAIL","reference_commit":PIN,"source_sha256":hashes,
            "limits":["Historical MAME comparison retains unchanged reference output and explicitly gates four named expected VICE disagreements; test_c64_vice_reu.py is authoritative.",
                      "16 MiB synthetic expectations use the 512 KiB REC window and write-only upper bank latch; unchanged VICE methods separately verify both capacity profiles.",
                      "Ordered accepted transfers/registers/IRQ are compared; machine scheduling, native transfer throughput and physical DDR require separate validation."]}
    try:
        if hashlib.sha256(REFERENCE.read_bytes()).hexdigest()!=REFERENCE_SHA:raise RuntimeError("Pinned REU source changed")
        src=REFERENCE.read_text();constants=src[src.index("namespace {"):src.index("} // anonymous namespace")+len("} // anonymous namespace")]
        methods=[extract(src,name) for name in ("device_reset","execute_run","reu_r","reu_w","transfer_byte","start_transfer","end_transfer","update_irq","read","write","ff00_w")]
        generated=build/"mame_reu_methods.inc"
        generated.write_text("// Extracted verbatim from pinned MAME; BSD-3-Clause; Curt Coder.\n"+constants+"\n"+"\n\n".join(methods)+"\n")
        runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
        guard=[sys.executable,str(ROOT/"scripts/run_native_tool.py"),"--"] if os.name=="nt" else []
        def run(name: str,cmd: list[object],timeout: int=240) -> str:
            command=[str(x) for x in cmd]
            proc=subprocess.run(command,cwd=build,capture_output=True,text=True,errors="replace",timeout=timeout)
            out=proc.stdout+proc.stderr;(build/(name+".log")).write_text(out)
            if proc.returncode or re.search(r"\bFATAL\b|\bERROR:",out):raise RuntimeError(name+" failed:\n"+out[-5000:])
            return out
        compiler=runtime/"g++.exe" if os.name=="nt" else "g++"
        oracle=build/"oracle.exe"
        run("oracle-compile",guard+[compiler,"-std=c++17","-O2","-Wall","-Wextra","-I",build,ROOT/"tests/c64_mame/reu_harness.cpp","-o",oracle])
        iv=runtime/"iverilog.exe" if os.name=="nt" else "iverilog"
        vv=runtime/"vvp.exe" if os.name=="nt" else "vvp"
        checks=[]
        for bits in (19,24):
            fixture=build/f"stimuli-{bits}.txt";actualpath=build/f"rtl-{bits}.trace";expectedpath=build/f"expected-{bits}.trace"
            for p in (actualpath,expectedpath):p.unlink(missing_ok=True)
            if bits==19:
                vectors=mame_vectors();vectors.save(fixture)
                run("oracle-run",guard+[oracle,fixture,expectedpath]);expected=expectedpath.read_text().splitlines()
            else:
                vectors,expected=extension_vectors();vectors.save(fixture);expectedpath.write_text("\n".join(expected)+"\n")
            exe=build/f"reu-{bits}.vvp"
            run(f"compile-{bits}",guard+[iv,"-g2012","-s","tb_c64_reu",f"-Ptb_c64_reu.ADDRESS_BITS={bits}","-o",exe,
                                         ROOT/"hdl/core/c64/multitini_c64_reu.sv",ROOT/"hdl/sim/tb_c64_reu.sv"])
            out=run(f"simulate-{bits}",guard+[vv,exe,"+STIMULI="+fixture.as_posix(),"+TRACE="+actualpath.as_posix()],600)
            if "PASS: C64 REU" not in out:raise RuntimeError("Missing simulation pass marker")
            actual=actualpath.read_text().splitlines()
            historical=compare_historical_mame(actual,expected,vectors.cases) if bits==19 else []
            if bits!=19: compare(actual,expected)
            checks.append({"address_bits":bits,"status":"PASS","cases":vectors.cases,"case_count":len(vectors.cases),"trace_events":len(actual),
                           "fixture_sha256":hashlib.sha256(fixture.read_bytes()).hexdigest(),"trace_sha256":hashlib.sha256(actualpath.read_bytes()).hexdigest(),
                           "expected_sha256":hashlib.sha256(expectedpath.read_bytes()).hexdigest(),
                           "expected_historical_differences":historical})
            print(f"PASS: REU {bits}-bit {len(vectors.cases)} cases, {len(actual)} trace events, {len(historical)} declared MAME disagreements",flush=True)
        transport=build/"transport.vvp"
        run("compile-transport",guard+[iv,"-g2012","-s","tb_c64_reu_transport","-o",transport,
                                       ROOT/"hdl/core/c64/multitini_c64_reu.sv",ROOT/"hdl/sim/tb_c64_reu_transport.sv"])
        transport_output=run("simulate-transport",guard+[vv,transport])
        markers=re.findall(r"^PASS: C64 REU transport.*$",transport_output,re.M)
        if len(markers)!=1:raise RuntimeError("Missing transport pass marker")
        print(markers[0],flush=True)
        checks.append({"name":"reset-disable-fault-transport","status":"PASS","summary":markers[0]})
        altered=expected.copy();altered[1]=altered[1]+" CORRUPTED"
        try:compare(altered,expected)
        except RuntimeError:negative=True
        else:raise RuntimeError("Trace corruption negative control failed")
        after={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        if after!=hashes:raise RuntimeError("REU test sources changed during validation")
        result.update(status="PASS",checks=checks,negative_control_rejected=negative,
                      generated_methods_sha256=hashlib.sha256(generated.read_bytes()).hexdigest())
    except Exception as error:
        result["error"]=str(error);raise
    finally:receipt.write_text(json.dumps(result,indent=2)+"\n")
    return 0


if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print("ERROR: "+str(error),file=sys.stderr);raise SystemExit(1)
