#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare production memory datapaths with MAME handlers using DUT-derived PLA.

This deliberately does not establish independent PLA truth-table conformance.
The executable MAME handlers receive explicitly recorded HDL decode selections;
they independently execute read routing, peripheral selects and RAM/color
backing effects. Peripheral register writes are not observed by the HDL bench.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN = "122719e8d13e2fdb7dd33886455484207de59fc8"
DECODE = ["cs_ramLoc", "cs_romLoc", "cs_CharLoc", "vicCharLoc", "cs_vicLoc", "cs_sidLoc",
          "cs_colorLoc", "cs_cia1Loc", "cs_cia2Loc", "cs_ioELoc", "cs_ioFLoc", "cs_romLLoc",
          "cs_romHLoc", "cs_UMAXromHLoc", "cs_UMAXnomapLoc"]


def fixtures() -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    def add(owner: int, write: int, address: int, vic: int, bank: int, game: int,
            exrom: int, floating: int, data: int = 0x6d, mode: int = 0,
            label: str = "matrix", fixed_decode: int = -1) -> None:
        rows.append(dict(id=len(rows), owner=owner, write=write, address=address, vic=vic,
                         bank=bank, game=game, exrom=exrom, ba=len(rows)&1, floating=floating,
                         data=data, mode=mode, label=label, fixed_decode=fixed_decode))
    addresses = sorted(set([0,1,2,0x00ff,0x0100,0x0fff,0x1000,0x2345,0x7fff,0x8000,0x9123,
        0x9fff,0xa000,0xa123,0xbfff,0xc000,0xcfff,0xd000,0xdfff,0xe000,0xe123,0xffff]
        + [0xd000+(page<<8)+low for page in range(16) for low in (0,0x25,0xff)]))
    for bank in range(8):
        for game in range(2):
            for exrom in range(2):
                for address in addresses:
                    for floating in (0xa5,0x5a):
                        for mode in (range(3) if 0xde00<=address<=0xdfff else (0,)):
                            add(1,0,address,((bank&3)<<14)|0x1234,bank,game,exrom,floating,mode=mode)
        # Cartridge stores require the full cartridge mapper, excluded here.
        for address in addresses:
            for data in (0,0x6d,0xff):
                add(1,1,address,((bank&3)<<14)|0x1234,bank,1,1,0x5a,data)
    for bank in range(4):
        for game in range(2):
            for exrom in range(2):
                for va in (0,0x0123,0x0fff,0x1000,0x1234,0x1fff,0x2000,0x2fff,0x3000,0x3456,0x3fff):
                    for floating in (0xa5,0x5a):
                        add(0,0,(bank<<14)|va,(bank<<14)|va,7,game,exrom,floating)
    # Explicit independent smoke expectations; these are hand-authored mapping
    # fixtures, NOT derived from the supplied-PLA MAME handler oracle.
    named = [("normal_basic",0xa123,7,1,1,1), ("normal_kernal",0xe123,7,1,1,1),
             ("charen_low_character",0xd123,3,1,1,2), ("all_banked_out_ram",0xd123,0,1,1,0),
             ("normal_vic",0xd025,7,1,1,4), ("normal_sid",0xd425,7,1,1,5),
             ("normal_color",0xd825,7,1,1,6), ("normal_cia1",0xdc25,7,1,1,7),
             ("normal_cia2",0xdd25,7,1,1,8), ("normal_io1",0xde25,7,1,1,9),
             ("normal_io2",0xdf25,7,1,1,10), ("8k_cart_roml",0x8123,7,1,0,11),
             ("16k_cart_romh",0xa123,7,0,0,12), ("ultimax_unmapped",0x2345,7,0,1,14)]
    for name,address,bank,game,exrom,bit in named:
        add(1,0,address,0x1234,bank,game,exrom,0xa5,label=name,fixed_decode=1<<bit)
    return rows


def supplied_pla(row: dict[str, int], observed: dict[str, int]) -> int:
    """Transparent DUT-to-PLA bridge, not an independent hardware PLA model."""
    flags=observed["decode"]
    select=0xff
    if flags&1: select &= ~1
    if flags&2: select &= ~(4 if row["address"]&0x4000 else 2)
    if flags&12: select &= ~8
    if flags&0x7f0: select &= ~0x20
    if flags&(1<<6): select &= ~0x10
    if flags&(1<<11): select &= ~0x40
    if flags&((1<<12)|(1<<13)): select &= ~0x80
    return select


def compare(row: dict[str, int], actual: dict[str, int], reference: dict[str, int]) -> list[str]:
    errors=[]
    def check(key: str, value: int, expected: int) -> None:
        if value!=expected: errors.append(f"{row['id']} {key}: HDL {value:#x}, MAME/fixture {expected:#x}")
    check("result",actual["result"],reference["result"])
    check("RAM backing byte",actual["ram_after"],reference["ram_after"])
    check("color backing nibble",actual["color_after"],reference["color_after"]&15)
    check("system address",actual["system_address"],row["address"] if row["owner"] else row["vic"])
    check("system write",actual["system_write"],row["write"])
    public=0
    for key,bit in (("vic",4),("sid",5),("cia1",7),("cia2",8)):
        calls=reference[key+"_reads"]+reference[key+"_writes"]
        check(key+" access count",calls,int(bool(actual["public_select"]&(1<<bit))))
        check(key+" direction",reference[key+"_writes"],calls*row["write"])
        if calls: public|=1<<bit
    # MAME directly indexes color RAM, so its selection is the supplied IO
    # output and subpage, not a peripheral-double call.
    if not(row["pla"]&0x20) and 8<=((row["address"]>>8)&15)<=11: public|=1<<6
    if not reference["io1"]: public|=1<<9
    if not reference["io2"]: public|=1<<10
    if not reference["roml"]: public|=1<<11
    if not reference["romh"]: public|=1<<(12 if row["owner"] else 13)
    check("peripheral/cart selects",actual["public_select"],public)
    # CASRAM is not the HDL transaction request: ROM and cartridge accesses also
    # request a machine memory response. Check that wiring separately.
    check("memory request wiring",actual["ram_request"],int(bool(actual["decode"]&0x7807)))
    offset=row["address"] if row["owner"] else row["vic"]&0x3fff
    va=0 if row["write"] else (0x3fff if row["owner"] else row["vic"]&0x3fff)
    packed=((va>>12&1)<<15)|((va>>13&1)<<14)|(row["game"]<<13)|(row["exrom"]<<12)|((1-row["write"])<<11)|((1-row["owner"])<<10)|(row["ba"]<<9)
    packed|=((offset>>12&1)<<8)|((offset>>13&1)<<7)|((offset>>14&1)<<6)|((offset>>15&1)<<5)|((1-(row["vic"]>>14&1))<<4)|(row["bank"]<<1)
    check("MAME packed PLA input pins",reference["pla_input"],packed)
    if row["fixed_decode"]>=0:check("independent named decode fixture",actual["decode"],row["fixed_decode"])
    return errors


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/c64-mame-expanded-memory")
    parser.add_argument("--vivado-bin",type=Path,default=Path(os.environ.get("XILINX_VIVADO","C:/Xilinx/2025.2/Vivado"))/"bin")
    args=parser.parse_args();build=args.build_dir.resolve()
    build_root=(ROOT/"build").resolve()
    if build==build_root or not build.is_relative_to(build_root):parser.error("build directory must be a dedicated child inside build/")
    build.mkdir(parents=True,exist_ok=True);receipt=build/"validation.json";receipt.unlink(missing_ok=True)
    (build/"mismatches.txt").unlink(missing_ok=True)
    (build/"hdl_observed.csv").unlink(missing_ok=True)
    (build/"mame_conditional_handlers.csv").unlink(missing_ok=True)
    manifest=ROOT/"third_party/mame_reference/import_manifest.json"
    bus=ROOT/"hdl/core/c64/multitini_c64_buslogic.vhd"
    # The locally ported machine source is the authoritative write-data owner.
    candidates=list((ROOT/"hdl/core/c64").rglob("fpga64_sid_iec.vhd"))
    if len(candidates)!=1:raise RuntimeError("expected one production fpga64_sid_iec.vhd")
    machine=candidates[0]
    dprom=ROOT/"hdl/core/c64/ports/dprom.vhd";spram=ROOT/"hdl/core/c64/ports/spram.vhd"
    ram=ROOT/"hdl/core/c64/multitini_c64_local_ram.sv";bench=ROOT/"hdl/sim/tb_c64_mame_memory.sv"
    files=[Path(__file__).resolve(),ROOT/"scripts/c64_mame_oracle.py",ROOT/"scripts/run_native_tool.py",manifest,
           ROOT/"tests/c64_mame/oracle_harness.cpp",ROOT/"tests/c64_mame/memory_harness.cpp",bus,machine,dprom,spram,ram,bench]
    files += [manifest.parent/"upstream"/item["path"] for item in json.loads(manifest.read_text())["files"]]
    def hashes() -> dict[str,str]:return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    initial=hashes()
    env={k:v for k,v in os.environ.items() if k.upper()!="PATH"}
    env["PATH"]=os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin")+os.pathsep+os.environ.get("PATH",os.environ.get("Path",""))
    def run(name: str, command: list[object]) -> str:
        cmd=[p.as_posix() if isinstance(p,Path) else str(p) for p in command]
        result=subprocess.run(cmd,cwd=build,env=env,capture_output=True,text=True,errors="replace",timeout=300)
        output=result.stdout+result.stderr;(build/(name+".log")).write_text(output,encoding="utf-8")
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b",output,re.I):raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-10000:]}")
        return output
    # Add output wires only. Entity, processes and original assignments are
    # copied verbatim; this observation copy never enters product manifests.
    original=bus.read_text(encoding="utf-8")
    instrumented,n=re.subn(r"(\s*cs_UMAXromH\s*: out std_logic)(\s*\n\s*\);)",r"\1;\n        observed_decode : out std_logic_vector(14 downto 0)\2",original)
    if n!=1:raise RuntimeError("decode observation port anchor changed")
    observation="\n".join(f"    observed_decode({i}) <= {name};" for i,name in enumerate(DECODE))+"\n"
    if instrumented.count("end architecture;")!=1:raise RuntimeError("decode observation architecture anchor changed")
    instrumented=instrumented.replace("end architecture;",observation+"end architecture;")
    observed=build/"observed_buslogic.vhd";observed.write_text(instrumented,encoding="utf-8")
    source=machine.read_text(encoding="utf-8")
    assignments=re.findall(r"\bramDout\s*<=.*?;",source,re.S)
    if len(assignments)!=1:raise RuntimeError("production ramDout assignment not unique")
    helper=build/"memory_write_data.vhd"
    helper.write_text("library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;\nentity c64_memory_write_data is port(systemWe:in std_logic;systemAddr:in unsigned(15 downto 0);lastVicDi,cpuDo:in unsigned(7 downto 0);ramDout:out unsigned(7 downto 0));end;\narchitecture rtl of c64_memory_write_data is signal dma_active:std_logic:=\'0\';begin\n"+assignments[0]+"\nend;\n",encoding="utf-8")
    rows=fixtures()
    fields=("address","vic","bank","game","exrom","ba","floating","data","mode")
    stimulus="".join(f"{r['id']} {r['owner']} {r['write']} "+" ".join(f"{r[k]:x}" for k in fields)+"\n" for r in rows)
    (build/"stimulus.txt").write_text(stimulus,encoding="ascii")
    (build/"cases.json").write_text(json.dumps(rows,indent=2)+"\n",encoding="utf-8")
    suffix=".bat" if os.name=="nt" else ""
    run("xvhdl",[args.vivado_bin/("xvhdl"+suffix),"--nolog","--2008",dprom,spram,observed,helper])
    run("xvlog",[args.vivado_bin/("xvlog"+suffix),"--nolog","--sv",ram,bench])
    run("xelab",[args.vivado_bin/("xelab"+suffix),"--nolog","--timescale","1ns/1ps","--snapshot","tb_c64_mame_memory_sim","tb_c64_mame_memory"])
    output=run("simulate",[args.vivado_bin/("xsim"+suffix),"--nolog","tb_c64_mame_memory_sim","--runall"])
    if f"PASS: C64 memory HDL observations {len(rows)} isolated transactions" not in output:raise RuntimeError("missing complete HDL observation pass marker")
    def read_csv(path: Path) -> list[dict[str,int]]:
        with path.open(newline="",encoding="utf-8") as handle:return [{k:int(v) for k,v in row.items()} for row in csv.DictReader(handle)]
    actual=read_csv(build/"hdl_observed.csv")
    if len(actual)!=len(rows):raise RuntimeError("HDL observation count mismatch")
    for row,observed_row in zip(rows,actual):
        if row["id"]!=observed_row["id"]:raise RuntimeError("HDL observation ordering mismatch")
        row["pla"]=supplied_pla(row,observed_row)
    conditional_input="".join(f"{r['id']} {r['owner']} {r['write']} "+" ".join(f"{r[k]:x}" for k in (*fields,"pla"))+"\n" for r in rows)
    (build/"dut_derived_pla_input.txt").write_text(conditional_input,encoding="ascii")
    oracle=build/"oracle"
    run("oracle-build",[sys.executable,ROOT/"scripts/c64_mame_oracle.py","--build-dir",oracle])
    run("conditional-handlers",[sys.executable,ROOT/"scripts/run_native_tool.py","--",oracle/("mame_c64_memory_oracle.exe" if os.name=="nt" else "mame_c64_memory_oracle"),"--batch-dut-pla",build/"dut_derived_pla_input.txt",build/"mame_conditional_handlers.csv"])
    reference=read_csv(build/"mame_conditional_handlers.csv")
    if len(reference)!=len(rows):raise RuntimeError("MAME reference count mismatch")
    failures=[]
    for row,a,r in zip(rows,actual,reference):
        if row["id"]!=r["id"]:raise RuntimeError("MAME reference ordering mismatch")
        failures.extend(compare(row,a,r))
    if failures:
        (build/"mismatches.txt").write_text("\n".join(failures)+"\n",encoding="utf-8")
    negatives=[]
    controls=[("result",1,next(i for i,r in enumerate(rows) if r["label"]=="normal_basic")),
              ("public_select",1<<4,0),
              ("ram_after",1,next(i for i,r in enumerate(rows) if r["write"] and r["address"]==0)),
              ("system_address",1<<14,next(i for i,r in enumerate(rows) if not r["owner"])),
              ("decode",1,next(i for i,r in enumerate(rows) if r["label"]=="normal_basic"))]
    for field,mask,index in ([] if failures else controls):
        corrupted=dict(actual[index]);corrupted[field]^=mask
        detected=compare(rows[index],corrupted,reference[index])
        if not detected:raise RuntimeError("negative control escaped comparator: "+field)
        negatives.append({"mutation":field+f" XOR {mask:#x}","case_id":rows[index]["id"],"rejected":True,"diagnostic":detected[0]})
    if (build/"stimulus.txt").read_text(encoding="ascii")!=stimulus:
        raise RuntimeError("memory stimulus changed during validation")
    if (build/"dut_derived_pla_input.txt").read_text(encoding="ascii")!=conditional_input:
        raise RuntimeError("conditional MAME input changed during validation")
    after=hashes()
    if initial!=after:raise RuntimeError("memory reference/production source changed during validation: "+", ".join(k for k in initial if initial[k]!=after[k]))
    evidence=["stimulus.txt","cases.json","hdl_observed.csv","dut_derived_pla_input.txt","mame_conditional_handlers.csv","observed_buslogic.vhd","memory_write_data.vhd"]
    if failures:evidence.append("mismatches.txt")
    receipt.write_text(json.dumps({"status":"FAIL" if failures else "PASS","reference_commit":PIN,"source_sha256":initial,
        "scope":"Production HDL datapaths versus actual MAME memory handlers CONDITIONAL ON DUT-DERIVED PLA OUTPUTS",
        "counts":{"cases":len(rows),"cpu_reads":sum(r["owner"] and not r["write"] for r in rows),"cpu_writes":sum(r["write"] for r in rows),"vic_reads":sum(not r["owner"] for r in rows),"bank_switch_combinations":8,"game_exrom_combinations":4,"vic_banks":4,"independent_hand_authored_decode_fixtures":sum(r["fixed_decode"]>=0 for r in rows)},
        "mismatch_count":len(failures),"mismatch_sample":failures[:25],
        "negative_controls_status":"SKIPPED: comparison failed; no proven matching pair" if failures else "PASS",
        "negative_controls":negatives,"evidence_sha256":{p:hashlib.sha256((build/p).read_bytes()).hexdigest() for p in evidence},
        "limits":["DUT-derived supplied PLA outputs are circular for decoder selection. This is NOT independent MAME PLA truth-table conformance; no PLA fuse dump is included.","Fourteen named decode expectations are independent hand-authored smoke fixtures, not MAME truth-table evidence.","The actual pinned MAME read_pla packing is compared for every case, including GAME/EXROM, CHAREN/HIRAM/LORAM, CPU/VIC AEC polarity, RW, BA, VA12/13 and VA14 bank input.","Original synthetic ROM/RAM data and peripheral/cartridge doubles test RAM/color backing effects, peripheral selects and read routing. The HDL bench does not observe peripheral write addresses/data or register effects. Cartridge writes and DMA are excluded.","VIC accesses use active address ownership. BA varies only in the MAME input-packing check; inactive VIC address fallback and RTL BA/AEC arbitration are not tested here.","These isolated accepted transactions do not test scheduler/arbitration edge timing, ROM compatibility, analog open-bus behavior, FPGA timing or hardware acceptance."]},indent=2)+"\n",encoding="utf-8")
    if failures:raise RuntimeError(f"{len(failures)} conditional handler mismatches; FAIL receipt: {receipt}\n"+"\n".join(failures[:25]))
    print(f"PASS: {len(rows)} production memory transactions vs conditional MAME handlers; DUT-derived PLA; {len(negatives)} negative controls rejected")
    return 0


if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print("ERROR: "+str(error),file=sys.stderr);raise SystemExit(1)
