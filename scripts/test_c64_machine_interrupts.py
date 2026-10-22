#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production CPU/VIC interrupt integration with original synthetic ROMs.

This exercises actual machine bus steals, not a whole-machine MAME oracle.
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

sys.dont_write_bytecode = True
from test_c64_machine_schedule import instrument, instrument_wrapper

ROOT = Path(__file__).resolve().parents[1]


def synthetic_memory(late: bool = False) -> bytes:
    memory=bytearray([0xea]*65536)
    program=bytearray.fromhex("78 D8 A2 FF 9A A9 2F 85 00 A9 37 85 01")
    def store(value: int, address: int) -> None:
        program.extend((0xa9,value,0x8d,address&255,address>>8))
    for address in (0xdc0d,0xdd0d):store(0x7f,address)
    for address in (0x2200,0x2201,0x2202,0x2204,0xd01a):store(0,address)
    if late:store(1,0x2200)
    store(0 if late else 0x1b,0xd011)  # DEN plus Y scroll 3: real badlines begin at line 51.
    store(0x14,0xd018)
    for sprite in range(8):
        store(80,0xd001+2*sprite)
        memory[0x07f8+sprite]=0x40
    store(0 if late else 0xff,0xd015)
    program.extend(bytes.fromhex("58 4C 00 09"))
    memory[0x0800:0x0800+len(program)]=program
    loop=bytearray.fromhex("EE 02 22 "*8)
    # Keep RMW writes running through the sprite window after both interrupts.
    # Branches return to $0900; handler counters must stay exactly one.
    for address in (0x2200,0x2201):
        loop.extend((0xad,address&255,address>>8,0xc9,2 if late and address==0x2201 else 1,0xd0))
        loop.append((-len(loop)-1)&255)
    loop.extend(bytes.fromhex("A9 A5 8D 04 22 4C 00 09"))
    memory[0x0900:0x0900+len(loop)]=loop
    memory[0x0a00:0x0a06]=bytes.fromhex("48 EE 00 22 68 40")
    memory[0x0b00:0x0b06]=bytes.fromhex("48 EE 01 22 68 40")
    return bytes(memory)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/c64-machine-interrupts-fixed")
    parser.add_argument("--vivado-bin",type=Path,default=Path(os.environ.get("XILINX_VIVADO","C:/Xilinx/2025.2/Vivado"))/"bin")
    args=parser.parse_args();build=args.build_dir.resolve();base=(ROOT/"build").resolve()
    if build==base or not build.is_relative_to(base):parser.error("build directory must be a dedicated child of build/")
    build.mkdir(parents=True,exist_ok=True);receipt=build/"validation.json";receipt.unlink(missing_ok=True)
    production=ROOT/"hdl/core/c64/ports/fpga64_sid_iec.vhd"
    wrapper=ROOT/"hdl/core/c64/ports/cpu_6510.vhd"
    machine=instrument(production.read_text())
    ports="    test_vic_x : out unsigned(9 downto 0);\n    test_vic_y : out unsigned(8 downto 0);\n    test_cpu_sync : out std_logic;\n"
    machine_extra=machine.replace("port(\n","port(\n"+ports,1)
    vic_connection="\tdebugX => test_vic_x,\n\tdebugY => test_vic_y,\n"
    cpu_connection="\tdebug_sync => test_cpu_sync,\n"
    if machine.count("\tirq_n => irq_vic\n")!=1 or machine.count("\tdebug_read => observedCpuRead,\n")!=1:
        raise RuntimeError("machine observation anchors changed")
    machine_extra=machine_extra.replace("\tirq_n => irq_vic\n",vic_connection+"\tirq_n => irq_vic\n",1)
    machine_extra=machine_extra.replace("\tdebug_read => observedCpuRead,\n",cpu_connection+"\tdebug_read => observedCpuRead,\n",1)
    if machine_extra.replace(ports,"",1).replace(vic_connection,"",1).replace(cpu_connection,"",1)!=machine:
        raise RuntimeError("additional observation changed machine behavior")
    observed=build/"fpga64_sid_iec_observed.vhd";observed.write_text(machine_extra)
    wrapped=instrument_wrapper(wrapper.read_text())
    sync_port="\t\tdebug_sync : out std_logic;\n";sync_wire="\t\tSync => debug_sync,\n"
    if wrapped.count("\t\tR_W_n   => localWe,\n")!=1:raise RuntimeError("CPU sync observation anchor changed")
    wrapped_extra=wrapped.replace("\tport (\n","\tport (\n"+sync_port,1).replace("\t\tR_W_n   => localWe,\n",sync_wire+"\t\tR_W_n   => localWe,\n",1)
    if wrapped_extra.replace(sync_port,"",1).replace(sync_wire,"",1)!=wrapped:raise RuntimeError("sync observation changed wrapper behavior")
    observed_wrapper=build/"cpu_6510_observed.vhd";observed_wrapper.write_text(wrapped_extra)
    memory=synthetic_memory();memory_hex="\n".join(f"{b:02x}" for b in memory)+"\n"
    (build/"machine_memory.hex").write_text(memory_hex)
    late_memory=synthetic_memory(True);late_hex="\n".join(f"{b:02x}" for b in late_memory)+"\n"
    (build/"late_memory.hex").write_text(late_hex)
    vhdl=[];section=""
    for raw in (ROOT/"hdl/hdl_sources.txt").read_text().splitlines():
        line=raw.strip()
        if line.startswith("["):section=line
        elif section=="[c64]" and line and not line.startswith("#"):
            path=(ROOT/"hdl"/line.split("|")[0].strip()).resolve()
            if path.suffix==".vhd":vhdl.append(path)
    sv=[ROOT/p for p in ("third_party/c64/upstream/rtl/mos6526.v","hdl/core/c64/ports/sid_dac.sv",
        "hdl/core/c64/ports/sid_filter.sv","third_party/c64/upstream/rtl/sid/sid_envelope.sv",
        "hdl/core/c64/ports/sid_tables.sv","third_party/c64/upstream/rtl/sid/sid_voice.sv",
        "hdl/core/c64/ports/sid_top.sv","hdl/core/c64/multitini_c64_local_ram.sv","hdl/sim/tb_c64_machine_interrupts.sv")]
    tracked=[Path(__file__).resolve(),ROOT/"scripts/test_c64_machine_schedule.py",ROOT/"hdl/hdl_sources.txt",*vhdl,*sv]
    def hashes() -> dict[str,str]:return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    initial=hashes();vhdl=[observed if p==production else observed_wrapper if p==wrapper else p for p in vhdl]
    env={k:v for k,v in os.environ.items() if k.upper()!="PATH"}
    env["PATH"]="C:/msys64/ucrt64/bin"+os.pathsep+os.environ.get("PATH",os.environ.get("Path",""))
    def run(name: str, command: list[object]) -> str:
        result=subprocess.run([p.as_posix() if isinstance(p,Path) else str(p) for p in command],cwd=build,
            env=env,capture_output=True,text=True,errors="replace",timeout=600,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        output=result.stdout+result.stderr;(build/(name+".log")).write_text(output)
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b",output,re.I):
            receipt.write_text(json.dumps({"status":"FAIL","phase":name,"source_sha256":initial,
                "diagnostic":output[-12000:],"scope":"Production machine integration, not a MAME differential"},indent=2)+"\n")
            raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-12000:]}")
        return output
    suffix=".bat" if os.name=="nt" else "";top="tb_c64_machine_interrupts"
    run("xvlog_imported",[args.vivado_bin/("xvlog"+suffix),"--nolog","--sv","--relax",*[p for p in sv if "third_party" in p.parts]])
    run("xvlog",[args.vivado_bin/("xvlog"+suffix),"--nolog","--sv",*[p for p in sv if "third_party" not in p.parts]])
    run("xvhdl",[args.vivado_bin/("xvhdl"+suffix),"--nolog","--2008",*vhdl])
    run("xelab",[args.vivado_bin/("xelab"+suffix),"--nolog","--relax","--timescale","1ns/1ps","--snapshot",top+"_sim",top])
    output=run("simulate",[args.vivado_bin/("xsim"+suffix),"--nolog",top+"_sim","--runall"])
    summaries=[line for line in output.splitlines() if line.startswith("INTERRUPT_SUMMARY ")]
    late_summaries=[line for line in output.splitlines() if line.startswith("LATE_NMI_SUMMARY ")]
    passed=[line for line in output.splitlines() if line.startswith("PASS: C64 machine interrupt integration")]
    stable=initial==hashes() and (build/"machine_memory.hex").read_text()==memory_hex and (build/"late_memory.hex").read_text()==late_hex
    okay=len(summaries)==4 and len(late_summaries)==4 and len(passed)==1 and stable
    receipt.write_text(json.dumps({"status":"PASS" if okay else "FAIL","case_count":len(summaries)+len(late_summaries),
        "checks":summaries+late_summaries,"source_sha256":initial,"sources_unchanged":stable,
        "synthetic_memory_sha256":hashlib.sha256(memory).hexdigest(),
        "late_memory_sha256":hashlib.sha256(late_memory).hexdigest(),
        "observation_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (observed,observed_wrapper)},
        "scope":"Actual production CPU, VIC, scheduler and RAM; IRQ/NMI during real BA-low reads and late NMI before/after bus issue",
        "limits":["PAL/NTSC stock and dense CPU modes; synthetic program and ROMs only.",
                  "Independent integration assertions, not an executable whole-machine MAME comparison.",
                  "Does not establish all raster-write timing, all interrupt edges, routed FPGA timing or physical hardware acceptance."]},indent=2)+"\n")
    if not okay:raise RuntimeError("missing eight integration cases/pass marker or changed source inputs")
    print("\n".join(summaries+late_summaries+passed));return 0


if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print("ERROR: "+str(error),file=sys.stderr);raise SystemExit(1)
