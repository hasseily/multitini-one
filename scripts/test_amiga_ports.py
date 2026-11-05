#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the real mixed-language Minimig ECS machine and run its host smoke.

This verifies source portability, configuration and global timeline hold. It
does not claim Kickstart boot, DDR throughput, FPGA timing or board behavior.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def machine_sources() -> list[Path]:
    source=ROOT / "third_party/amiga/upstream/rtl"
    replaced={p.name for p in (ROOT / "hdl/core/amiga/ports").glob("*.v")}
    result=sorted(p for p in source.glob("*.v") if p.name not in replaced)
    result += [source / "fx68k/fx68kAlu.sv",source / "fx68k/uaddrPla.sv"]
    result += sorted((ROOT / "hdl/core/amiga/ports").glob("*.v"))
    result += [p for p in sorted((ROOT / "hdl/core/amiga/ports").glob("*.sv")) if p.name!="multitini_amiga_ddr_path.sv"]
    result += [ROOT / "hdl/core/amiga/multitini_amiga_display_mailbox.sv",
               ROOT / "hdl/core/amiga/multitini_amiga_machine.sv"]
    return result


def ap68040_sources() -> list[Path]:
    manifest = (ROOT / "hdl/hdl_sources.txt").read_text()
    section = manifest.split("[amiga_ap68040]", 1)[1].split("[", 1)[0]
    return [(ROOT / "hdl" / line.split("|")[0].strip()).resolve()
            for line in section.splitlines() if line.strip() and not line.startswith("#")]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT / "build/amiga-ports-test")
    parser.add_argument("--vivado",type=Path,default=Path("C:/Xilinx/2025.2/Vivado"))
    parser.add_argument("--cpu",action="store_true",help="Execute original stock and accelerated CPU diagnostics through production memory")
    parser.add_argument("--ap68040",action="store_true",help="Run the AGA AP68040 diagnostic")
    parser.add_argument("--interface-delay",action="store_true",help="Model 1 ns service data/control delay and 0.1 ns native clock insertion in generated test fixtures")
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build"):
        raise SystemExit("Select a build directory inside this repository")
    build.mkdir(parents=True,exist_ok=True)
    tools=args.vivado / "bin/unwrapped/win64.o"
    vhdlbase=ROOT / "third_party/amiga/upstream/rtl/tg68k"
    vhdl=[vhdlbase / name for name in ("TG68K_Pack.vhd","TG68K_ALU.vhd","TG68K_MMU030.vhd","TG68KdotC_Kernel.vhd","TG68K.vhd")]
    sources=machine_sources()+[ROOT / "hdl/sim/tb_amiga_ports.sv"]
    if args.cpu or args.ap68040:
        sources += [ROOT / "hdl/core/amiga/multitini_amiga_memory.sv",ROOT / "hdl/sim/tb_amiga_cpu.sv"]
    include_dirs = ["-i", str(ROOT / "third_party/amiga/upstream/rtl")]
    if args.ap68040:
        sources = ap68040_sources() + sources
        ap_rtl = ROOT / "third_party/amiga/ap68040/rtl"
        include_dirs += ["-i", str(ap_rtl), "-i", str(ap_rtl / "fpu")]
    fixture_sources=list(sources)
    if args.interface_delay:
        ports=ROOT / "hdl/sim/tb_amiga_ports.sv"
        delayed_ports=build / "tb_amiga_ports_delayed.sv"
        text=ports.read_text()
        if text.count("assign O=I && enabled;")!=1:raise SystemExit("Clock model changed")
        delayed_ports.write_text(text.replace("assign O=I && enabled;","assign #0.1 O=I && enabled;"))
        sources[sources.index(ports)]=delayed_ports
        if args.cpu or args.ap68040:
            cpu=ROOT / "hdl/sim/tb_amiga_cpu.sv"
            delayed_cpu=build / "tb_amiga_cpu_delayed.sv"
            text=cpu.read_text()
            start=text.index("    multitini_amiga_machine #(")
            end=text.index("    multitini_amiga_memory memory_i(",start)
            block=text[start:end]
            declarations=[]
            for name,width in (("chip_rdata",16),("chip_rdata2",16),("chip_rdata48",48),
                               ("cpu_rdata",16),("cpu_rdata2",16),("cpu_ready",1),("run_enable",1)):
                declarations.append(f"    wire [{width-1}:0] delayed_{name}; assign #1 delayed_{name}={name};")
                block=block.replace(f"({name})",f"(delayed_{name})")
            text=text[:start]+"\n".join(declarations)+"\n"+block+text[end:]
            delayed_cpu.write_text(text)
            sources[sources.index(cpu)]=delayed_cpu
    assets=[ROOT / "hdl/core/amiga/ports/m68k_cyc_idx.hex",ROOT / "hdl/core/amiga/ports/m68k_ea_idx.hex",ROOT / "third_party/amiga/upstream/rtl/fx68k/microrom.mem",ROOT / "third_party/amiga/upstream/rtl/fx68k/nanorom.mem"]
    for asset in assets: shutil.copyfile(asset,build / asset.name)
    receipt={"schema_version":1,"board_evidence":False,"interface_delay_ns":1 if args.interface_delay else 0,"native_clock_delay_ns":0.1 if args.interface_delay else 0,"sources":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in dict.fromkeys(vhdl+fixture_sources+sources+assets+[Path(__file__).resolve()])}}
    (build / "inputs.json").write_text(json.dumps(receipt,indent=2)+"\n")
    def run(name: str,arguments: list[str]) -> None:
        executable=args.vivado / "bin" / f"{name}.bat"
        command=[sys.executable,str(ROOT / "scripts/run_native_tool.py"),"--cwd",str(build),"--","cmd.exe","/d","/c",str(executable)]+arguments
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,errors="replace")
        (build / f"{name}.stdout.log").write_text(result.stdout+result.stderr)
        if result.returncode:
            print((result.stdout+result.stderr)[-12000:])
            raise SystemExit(result.returncode)
        print(f"{name}: passed")
    run("xvhdl",["--2008","--work","work"]+[str(p) for p in vhdl])
    sv_options = ["--sv", "--work", "work"]
    if args.ap68040:
        sv_options.append("--relax")  # Upstream AP68040 permits forward references.
    run("xvlog",sv_options+include_dirs+[str(p) for p in sources])
    run("xelab",["work.tb_amiga_ports","-s","amiga_ports","--debug","typical","--timescale","1ns/1ps","--mt","auto"])
    run("xsim",["amiga_ports","--runall"])
    log=(build / "xsim.stdout.log").read_text()
    if "PASS Amiga portable RAM" not in log or "Fatal:" in log:
        raise SystemExit("Amiga smoke did not report success")
    run("xelab",["work.tb_amiga_aga_fetch","-s","amiga_aga_fetch","--timescale","1ns/1ps"])
    run("xsim",["amiga_aga_fetch","--runall"])
    log=(build / "xsim.stdout.log").read_text()
    (build / "amiga_aga_fetch.log").write_text(log)
    if "PASS AGA bitplane and sprite fetches" not in log or "Fatal:" in log:
        raise SystemExit("AGA fetch coherence check did not report success")
    profiles = [("stock",0),("turbo",3),("half",7),("aga_stock",7),("aga_turbo",3)] if args.cpu else []
    if args.ap68040:
        profiles.append(("aga_040",3))
    if profiles:
        for profile,mode in profiles:
            snapshot=f"amiga_cpu_{profile}"
            relax = ["--relax"] if profile == "aga_040" else []
            run("xelab",relax+[f"work.tb_amiga_cpu_{profile}","-s",snapshot,"--debug","typical","--timescale","1ns/1ps","--mt","auto"])
            run("xsim",[snapshot,"--runall"])
            log=(build / "xsim.stdout.log").read_text()
            (build / f"{snapshot}.log").write_text(log)
            if f"PASS Amiga CPU mode={mode}" not in log or "Fatal:" in log:
                raise SystemExit(f"Amiga CPU profile {profile} did not report success; see {build / (snapshot + '.log')}")
    receipt["result"]="passed"
    (build / "report.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print("PASS real Minimig ECS source elaboration and portable host smoke")
    return 0


if __name__=="__main__": raise SystemExit(main())
