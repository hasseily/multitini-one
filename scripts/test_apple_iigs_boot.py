#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check actual ROM3 RTC/disk bootstrap milestones with private runtime fixtures.

This test does not embed or distribute Apple ROMs or disk images. It checks
the complete production loader, CPU, memory path and native HDD/IWM interfaces.
A successful result establishes bounded bootstrap progress, not a desktop,
application compatibility, FPGA timing or physical hardware qualification.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zlib
from test_apple_iigs_ports import machine_sources

ROOT=Path(__file__).resolve().parents[1]

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def disk_payload(path: Path) -> tuple[bytes,dict]:
    raw=path.read_bytes()
    if path.suffix.lower() not in (".po",".hdv",".2mg"):
        raise ValueError("use a ProDOS-order .po/.hdv or .2mg HDD fixture")
    if path.suffix.lower()==".2mg" and raw[:4]!=b"2IMG":
        raise ValueError("2MG fixture has no 2IMG header")
    offset=0
    size=len(raw)
    if raw[:4]==b"2IMG":
        if len(raw)<64:
            raise ValueError("truncated 2IMG header")
        header=int.from_bytes(raw[8:10],"little")
        image_format=int.from_bytes(raw[12:16],"little")
        offset=int.from_bytes(raw[24:28],"little")
        size=int.from_bytes(raw[28:32],"little")
        blocks=int.from_bytes(raw[20:24],"little")
        if size==0:
            size=blocks*512
        if header<64 or image_format!=1 or offset<header or size!=blocks*512:
            raise ValueError("boot fixture must be a valid ProDOS-order 2IMG image")
    if not size or size%512 or size>33554432 or offset+size>len(raw):
        raise ValueError("HDD fixture must contain 512-byte blocks, at most 32 MiB")
    payload=raw[offset:offset+size]
    return payload,{"path":str(path.resolve()),"bytes":len(raw),"sha256":sha(raw),
                    "payload_offset":offset,"payload_bytes":size,"payload_sha256":sha(payload)}

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom-source",type=Path,required=True,help="private directory containing rom3.rom, char.rom, adb3.rom and hdd.rom")
    media_args=parser.add_mutually_exclusive_group(required=True)
    media_args.add_argument("--disk",type=Path,help="private bootable ProDOS-order HDD fixture (.2mg, .po or .hdv)")
    media_args.add_argument("--floppy-woz",type=Path,help="private virtual WOZ2 generated from the exact raw 800K floppy fixture")
    parser.add_argument("--raw-floppy",type=Path,help="original private .po/.2mg 800K image, required with --floppy-woz for provenance and boot opcode")
    parser.add_argument("--floppy-provenance",type=Path,help="optional JSON evidence recording the production raw-to-WOZ conversion")
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-boot-tests")
    parser.add_argument("--max-primary-ticks",type=int,default=120000000)
    parser.add_argument("--min-reads",type=int,default=2)
    parser.add_argument("--speed",type=int,choices=range(7),default=0)
    parser.add_argument("--compatibility-flags",type=lambda value:int(value,0),default=0x10,
                        help="runtime config byte3: 0x10=fresh IRQ-off default; 0x17=all delays off; 0=legacy defaults")
    parser.add_argument("--trace-start",type=int,default=0)
    parser.add_argument("--trace-end",type=int,default=0)
    parser.add_argument("--require-irq-dispatch",action="store_true",help="also require execution of the native E1:0010 interrupt handler")
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/"build"):
        raise ValueError("private fixture outputs must stay under the ignored repository build directory")
    if (build/"validation.json").exists():
        raise ValueError("validation already exists; select a fresh --build-dir")
    if args.max_primary_ticks<1 or args.min_reads<1 or args.trace_start<0 or args.trace_end<0:
        raise ValueError("tick/read budgets must be positive; trace positions must be nonnegative")
    if not 0<=args.compatibility_flags<=0x1f:
        raise ValueError("compatibility flags must fit the five supported runtime bits")
    build.mkdir(parents=True,exist_ok=True)
    fixtures=build/"private"
    fixtures.mkdir(exist_ok=True)
    roms={}
    for name,size in (("rom3",262144),("char",4096),("adb3",4096),("hdd",256)):
        path=args.rom_source/(name+".rom")
        data=path.read_bytes()
        if len(data)!=size or len(set(data))<2:
            raise ValueError(f"{name}.rom must contain {size} nonblank bytes")
        roms[name]={"path":str(path.resolve()),"bytes":len(data),"sha256":sha(data)}
        (fixtures/(name+".hex")).write_text("\n".join(f"{byte:02x}" for byte in data)+"\n")
    expected_boot_opcode=-1
    if args.floppy_woz:
        if not args.raw_floppy:
            raise ValueError("--floppy-woz requires the original --raw-floppy image")
        raw_floppy,original=disk_payload(args.raw_floppy)
        if len(raw_floppy)!=819200:
            raise ValueError("the native 3.5-inch case requires an exact 800K raw floppy")
        payload=args.floppy_woz.read_bytes()
        if len(payload)<80 or len(payload)>33554432 or payload[:8]!=b"WOZ2\xff\x0a\x0d\x0a":
            raise ValueError("floppy fixture must be a bounded WOZ2 image")
        crc=int.from_bytes(payload[8:12],"little")
        if crc and crc!=(zlib.crc32(payload[12:])&0xffffffff):
            raise ValueError("WOZ2 CRC does not match its exact bytes")
        media={"path":str(args.floppy_woz.resolve()),"bytes":len(payload),"sha256":sha(payload),
               "payload_offset":0,"payload_bytes":len(payload),"payload_sha256":sha(payload),
               "mount_slot":2,"transport":"native IWM/WOZ2","original_raw":original}
        expected_boot_opcode=raw_floppy[1]
        if args.floppy_provenance:
            provenance=args.floppy_provenance.read_bytes()
            json.loads(provenance)
            media["conversion_provenance"]={"path":str(args.floppy_provenance.resolve()),"bytes":len(provenance),"sha256":sha(provenance)}
    else:
        if args.raw_floppy or args.floppy_provenance:
            raise ValueError("raw floppy/provenance options require --floppy-woz")
        payload,media=disk_payload(args.disk)
        media.update({"mount_slot":0,"transport":"native HDD DMA"})
    (fixtures/"disk.bin").write_bytes(payload)
    core=ROOT/"hdl/core/apple_iigs"
    sources=machine_sources()+[core/"multitini_apple_iigs_loader.sv",core/"multitini_apple_iigs_cache.sv",
        core/"multitini_apple_iigs_adapter.sv",ROOT/"hdl/sim/apple_iigs_bufgce.sv",ROOT/"hdl/sim/tb_apple_iigs_boot.sv"]
    captured=sources+[ROOT/"third_party/apple-iigs/upstream/rtl/dhr_lut.vh",ROOT/"scripts/port_apple_iigs_core.py",
                     ROOT/"scripts/test_apple_iigs_ports.py",Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():sha(path.read_bytes()) for path in captured}
    before=hashes()
    snapshot=build/"captured"
    for path in captured:
        target=snapshot/path.relative_to(ROOT)
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(path.read_bytes())
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    def tool(name):
        return str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    commands={}
    def run(command,name):
        command=[str(part).replace("\\","/") for part in command]
        commands[name]=command
        log=build/(name+".log")
        with log.open("w") as stream:
            result=subprocess.run(command,cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        text=log.read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed",text):
            raise ValueError(f"actual ROM boot {name} failed: {log}\n{text[-8000:]}")
        return text
    top="tb_apple_iigs_boot"
    obj=build/"obj"
    executable=obj/("V"+top+(".exe" if os.name=="nt" else ""))
    record={"schema_version":1,"status":"RUNNING","files":before,"private_inputs":{"roms":roms,"media":media},
            "git_head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
            "configuration":{"rom":3,"speed":args.speed,"compatibility_flags":args.compatibility_flags,
                             "pram":"fresh defaults","read_only_disk":True,
                             "max_primary_ticks":args.max_primary_ticks,"minimum_disk_reads":args.min_reads,
                             "require_native_irq_dispatch":args.require_irq_dispatch},
            "limits":["Bounded RTC and actual disk-bootstrap progress; no GS/OS desktop or game compatibility assertion.",
                      "ARM file transport is replaced by a read-only host sector service; DDR uses a variable-latency simulation model.",
                      "Private fixture bytes remain only in ignored build/private files.",
                      "No FPGA timing closure or physical board qualification is implied."],"commands":commands}
    started=time.monotonic()
    try:
        run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing","-Wno-fatal",
             "--top-module",top,"-I"+str(snapshot/"third_party/apple-iigs/upstream/rtl"),"--Mdir",obj,
             *(snapshot/path.relative_to(ROOT) for path in sources)],"generate")
        if os.name=="nt":
            inc=runtime.parent/"share/verilator/include"
            run([tool("g++"),"-std=c++20","-O2","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(obj),
                 *sorted(obj.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",executable],"compile")
        else:
            run([tool("make"),"-C",obj,"-f","V"+top+".mk","-j8"],"compile")
        text=run([executable,"+FIXTURES="+fixtures.as_posix(),"+DISK="+(fixtures/"disk.bin").as_posix(),
                  "+TICKS="+str(args.max_primary_ticks),"+SPEED="+str(args.speed),"+MIN_READS="+str(args.min_reads),
                  "+COMPAT_FLAGS="+str(args.compatibility_flags),
                  "+REQUIRE_IRQ="+str(int(args.require_irq_dispatch)),
                  "+FLOPPY="+str(int(args.floppy_woz is not None)),"+BOOT_OPCODE="+str(expected_boot_opcode),
                  "+TRACE_START="+str(args.trace_start),"+TRACE_END="+str(args.trace_end)],"simulate")
        if "PASS: actual ROM3 RTC and disk bootstrap milestones" not in text:
            raise ValueError("actual-ROM bootstrap PASS marker is absent")
        record["milestones"]=[line for line in text.splitlines() if line.startswith(("BOOT_MILESTONE","BOOT_RESULT"))]
        if hashes()!=before:
            raise ValueError("current sources changed during actual-ROM test; captured result is not a current-source qualification")
        record["source_hashes_unchanged"]=True
        record["status"]="PASS"
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        record["status"]="FAIL"
        record["error"]=str(error)
        raise
    finally:
        record["wall_seconds"]=time.monotonic()-started
        record["evidence_files"]={name:{"bytes":path.stat().st_size,"sha256":sha(path.read_bytes())}
            for name in ("generate.log","compile.log","simulate.log") if (path:=build/name).exists()}
        (build/"validation.json").write_text(json.dumps(record,indent=2)+"\n")
    print("PASS: actual ROM3 RTC and disk-bootstrap milestones",build)
    return 0

if __name__=="__main__":
    try:
        raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}",file=sys.stderr)
        raise SystemExit(1)
