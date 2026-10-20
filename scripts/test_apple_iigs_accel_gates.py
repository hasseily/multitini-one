#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the acceleration compatibility gates, Mega II fast paths and real CPU/DMA throughput."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/apple-iigs-accel-gates-tests")
    p.add_argument("--negative-controls",action="store_true",
        help="also prove compatibility source mutations fail: slot second native edge, IWM media gate, single native slow-RAM "
             "write edge, IWM odd-address mode write, IWM motor-on exclusion in the mode register and in the immediate "
             "mode view, C070-only paddle trigger, shadow-write fast class, sound GLU native hold, and that hold for "
             "reads")
    p.add_argument("--joystick-negative-control",action="store_true",
        help="also prove removing the adapter's unsigned-to-signed analog conversion fails the joystick check")
    p.add_argument("--floppy-idle-negative-control",action="store_true",
        help="also prove restoring FPGA605 whole-motor slowdown fails selected-spinning real CPU work")
    p.add_argument("--floppy-policy-negative-controls",action="store_true",
        help="also prove removing guest-native, Disk II motor-request, active-write or routed-cleanup guards fails")
    a=p.parse_args();build=a.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
    env=os.environ.copy()
    if os.name=="nt":
        env["PATH"]=str(runtime)+os.pathsep+str(runtime.parents[1]/"usr/bin")+os.pathsep+env.get("PATH","")
        env["VERILATOR_ROOT"]=(runtime.parent/"share/verilator").as_posix()
    tool=lambda name:str(runtime/(name+".exe")) if os.name=="nt" else shutil.which(name)
    def run(command,name,cwd=None):
        log=build/(name+".log")
        with log.open("w") as out:r=subprocess.run([str(s).replace("\\","/") for s in command],cwd=cwd or build,env=env,stdout=out,stderr=subprocess.STDOUT)
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
    before=hashes();top="tb_apple_iigs_accel_gates"
    inc=runtime.parent/"share/verilator/include"
    def build_bench(source_list,directory,label):
        run([tool("verilator_bin") if os.name=="nt" else tool("verilator"),"--cc","--exe","--main","--timing",
            "-Wno-fatal","--top-module",top,"-I"+str(ROOT/"third_party/apple-iigs/upstream/rtl"),
            "-I"+str(ROOT/"hdl/sim"),"--Mdir",directory,*source_list],label+"generate")
        executable=directory/("V"+top+(".exe" if os.name=="nt" else ""))
        if os.name=="nt":
            run([tool("g++"),"-std=c++20","-O1","-pthread","-DVL_TIME_CONTEXT","-I"+str(inc),"-I"+str(inc/"vltstd"),"-I"+str(directory),
                *sorted(directory.glob("*.cpp")),inc/"verilated.cpp",inc/"verilated_timing.cpp",inc/"verilated_threads.cpp","-o",executable],label+"compile")
        else:run([tool("make"),"-C",directory,"-f","V"+top+".mk","-j8"],label+"compile")
        return executable
    executable=build_bench(sources,build/"obj","")
    out=run([executable],"simulate")
    if out.count("PASS:")!=16 or "COMPARE shadow_ram=49664" not in out or "COMPARE mounted_motor=2048" not in out:
        raise ValueError("acceleration gate groups/comparison coverage did not all pass")
    floppy=[{"scenario":int(s),"family":int(f),"native_request":int(n),"primary_ticks":int(t),"cpu_advances":int(c)}
            for s,f,n,t,c in re.findall(r"MEASURE idle_floppy scenario=(\d+) family=(\d+) native_request=(\d+) primary_ticks=(\d+) cpu_advances=(\d+)",out)]
    if [(r["scenario"],r["family"],r["native_request"]) for r in floppy]!=[(0,0,0),(1,1,0),(2,1,1)] or len({r["cpu_advances"] for r in floppy})!=1:
        raise ValueError("missing equal-work real CPU floppy fast/native comparisons")
    for r in floppy:r["measured_hz"]=r["cpu_advances"]*57272728/r["primary_ticks"]
    kernels=("shadow_kernel","shadow_store_kernel","mega2_store_kernel","mega2_load_kernel")
    rows=[{"workload":work,"speed":int(s),"primary_ticks":int(t),"cpu_advances":int(c)} for work,s,t,c in
          re.findall(r"MEASURE (shadow_kernel|shadow_store_kernel|mega2_store_kernel|mega2_load_kernel) speed=(\d+) primary_ticks=(\d+) cpu_advances=(\d+)",out)]
    if [(row["workload"],row["speed"]) for row in rows]!=[(w,s) for w in kernels for s in (0,6)]:
        raise ValueError("missing real CPU shadow-read/shadow-store/Mega II store kernel rates")
    for row in rows:row["measured_hz"]=row["cpu_advances"]*57272728/row["primary_ticks"]
    dma=[{"target":target,"speed":int(s),"bytes":int(b),"primary_ticks":int(t),"min_delta":int(lo),"max_delta":int(hi)} for target,s,b,t,lo,hi in
         re.findall(r"MEASURE dma_(e1|shadow) speed=(\d+) bytes=(\d+) primary_ticks=(\d+) min_delta=(\d+) max_delta=(\d+)",out)]
    if [(row["target"],row["speed"]) for row in dma]!=[("e1",0),("shadow",0),("e1",6),("shadow",6)] or any(row["bytes"]!=512 for row in dma):
        raise ValueError("missing HDD DMA pace rows")
    tick=lambda work,speed:next(row["primary_ticks"] for row in rows if row["workload"]==work and row["speed"]==speed)
    dma_row=lambda target,speed:next(row for row in dma if row["target"]==target and row["speed"]==speed)
    # Stock invariance: the native divider fixes these stock cadences. The
    # shadow kernel and shadow store values are the published M0.5.3 records;
    # posted writes keep the stock shadow store off DDR latency. The two
    # bank-E1 values have no M0.5.3 record: they are pinned to the first
    # measurement on the M0.5.4 sources, taken before the primary-clock
    # slow-RAM read/write staging (stock never uses that path).
    stock={"shadow_kernel":592636,"shadow_store_kernel":167508,"mega2_store_kernel":167004,"mega2_load_kernel":167004}
    for work,expected in stock.items():
        if tick(work,0)!=expected:raise ValueError(f"stock {work} changed: {tick(work,0)} primary ticks, expected {expected}")
    # Stock E1 DMA bytes are 1 MHz sync cycles (56 ticks, 64 with the NTSC
    # long cycle). 511 gaps hold 7 or 8 long cycles depending on where the
    # transfer starts on the 912-tick line, so check the pace, not one total.
    # Shadowed stock DMA writes DDR too; posted writes keep the same pace.
    for target in ("e1","shadow"):
        slow=dma_row(target,0)
        if slow["min_delta"]!=56 or slow["max_delta"]!=64 or not 511*56+7*8<=slow["primary_ticks"]<=511*56+8*8:
            raise ValueError(f"stock {target} DMA pace changed: {slow}")
    # At maximum a store costs at most one extra primary tick, when it
    # retires on the video read edge while the previous store is staged.
    row=lambda work,speed:next(r for r in rows if r["workload"]==work and r["speed"]==speed)
    for work in ("shadow_store_kernel","mega2_store_kernel"):
        fast_row=row(work,6)
        if fast_row["primary_ticks"]-fast_row["cpu_advances"]>1024:
            raise ValueError(f"{work} at maximum spends more than one extra tick per store: {fast_row}")
        if tick(work,0)/tick(work,6)<20:
            raise ValueError(f"{work} maximum/stock speedup {tick(work,0)/tick(work,6):.2f}x is below 20x")
    ratio=tick("mega2_store_kernel",0)/tick("mega2_store_kernel",6)
    load_ratio=tick("mega2_load_kernel",0)/tick("mega2_load_kernel",6)
    if load_ratio<8:raise ValueError(f"Mega II load loop maximum/stock speedup {load_ratio:.2f}x is below 8x")
    glu=[{"speed":int(s),"held_request_ticks":int(h)} for s,h in re.findall(r"MEASURE sound_glu speed=(\d+) holds=(\d+)",out)]
    if [row["speed"] for row in glu]!=[3,6] or any(row["held_request_ticks"]==0 for row in glu):
        raise ValueError(f"missing sound GLU hold rows: {glu}")
    # Accelerated DMA: three CLK14 ticks per byte, plus the NTSC line stretch.
    fast=dma_row("e1",6)
    if fast["min_delta"]!=12 or fast["max_delta"]>20 or fast["primary_ticks"]>511*12+64:
        raise ValueError(f"accelerated E1 DMA is not paced at three CLK14 ticks per byte: {fast}")
    negatives=[]
    if a.negative_controls or a.joystick_negative_control or a.floppy_idle_negative_control or a.floppy_policy_negative_controls:
        adapter=core/"multitini_apple_iigs_adapter.sv";iigs=core/"ports/iigs.sv"
        mutations=(("slot_edge",adapter,"(cpu_native_two_edge ? native_prepared2 : native_prepared)","native_prepared","+ONLY_DMA","slot-7 ROM byte"),
            ("iwm_media",iigs,"(iwm_holdoff == 15'd0 || (EXTERNAL_CPU && !iwm_selected_media))","iwm_holdoff == 15'd0","+ONLY_MOTOR",
             "empty-drive IWM access held the accelerator"),
            ("native_edge",iigs,"(bus_phi2 & NATIVE_EDGE)","bus_phi2","+ONLY_SHADOW","must update the slow-RAM port exactly once"),
            ("iwm_mode",core/"ports/iwm_woz.sv",
             ("A[0] && write_mode_q6 && write_mode_q7 &&","bus_addr[0] && immediate_q6 && immediate_q7 &&"),
             ("(A[3:0] == 4'hF) && write_mode_q6 && write_mode_q7 &&","(bus_addr == 4'hF) &&"),
             "+ONLY_IWM_MODE","IWM odd-address mode write ignored"),
            ("iwm_motor_on",core/"ports/iwm_woz.sv",
             (" &&\n                                !(A[3:1] == 3'b100 && A[0]);",
              " &&\n                !(bus_addr[3:1] == 3'b100 && bus_addr[0])) begin"),
             (";",") begin"),"+ONLY_IWM_MODE","IWM motor-on write changed the mode register"),
            ("paddle_alias",iigs,"(addr_bef[11:0] == 12'h070)","(addr_bef[7:4] == 4'h7)","+ONLY_PADDLE",
             "IRQ dispatch falsely arms paddle holdoff"),
            ("shadow_write_class",iigs,"cpu_slowram_read || cpu_slowram_write || cpu_mega2_rom_read",
             "cpu_slowram_read || cpu_mega2_rom_read","+ONLY_SHADOW","RAM shadow class"),
            ("iwm_motor_on_view",core/"ports/iwm_woz.sv"," &&\n                             !(bus_addr[3:1] == 3'b100);",";",
             "+ONLY_IWM_MODE","IWM motor-on write changed the immediate mode view"),
            ("sound_hold",adapter,"&& !cpu_native_hold &&","&&","+ONLY_SOUND","sound GLU"),
            ("sound_read_hold",iigs,"assign cpu_native_hold = external_accel && native_snd_sel && snd_busy;",
             "assign cpu_native_hold = external_accel && native_snd_sel && snd_busy && we;","+ONLY_SOUND","sound GLU read-back"))
        if not a.negative_controls:mutations=()
        if a.joystick_negative_control:
            mutations+=(("joystick_unsigned",adapter,".joystick_l_analog_0(analog_xy ^ 16'h8080)",
                ".joystick_l_analog_0(analog_xy)","+ONLY_JOYSTICK","joystick neutral after reset"),)
        if a.negative_controls or a.floppy_idle_negative_control:
            mutations+=(("floppy_blanket_motor",iigs,
                ("!(floppy_motor_on && (|DISK_READY[1:0]) && !EXTERNAL_CPU)",
                 "!(floppy35_motor_on && (|DISK_READY[3:2]) && !EXTERNAL_CPU)"),
                ("!(floppy_motor_on && (|DISK_READY[1:0]))",
                 "!(floppy35_motor_on && (|DISK_READY[3:2]) && (!EXTERNAL_CPU || DISK35[6]))"),
                "+ONLY_FLOPPY_IDLE","selected spinning Sony fast work failed to regain maximum cadence"),)
        if a.negative_controls or a.floppy_policy_negative_controls:
            mutations+=(("floppy_guest_native",iigs,
                "(!CYAREG[7] || (!DISK35[6] && CYAREG[2] && iwm_motor_command))",
                "(!DISK35[6] && CYAREG[2] && iwm_motor_command)",
                "+ONLY_FLOPPY_IDLE","long floppy guest-native countdown resumed acceleration"),
                ("floppy_motor_request",iigs,"(!DISK35[6] && CYAREG[2] && iwm_motor_command)","1'b0",
                 "+ONLY_MOTOR","floppy guest timing matrix failed"),
                ("floppy_write_guard",iigs,"(floppy_guest_native_request || iwm_write_timing)","floppy_guest_native_request",
                 "+ONLY_MOTOR","IWM active write lost native timing after access lease"),
                ("floppy_write_routing",core/"ports/iwm_woz.sv","WRITE_TIMING_ACTIVE <= disk_write_mode && disk_mounted;",
                 "WRITE_TIMING_ACTIVE <= disk_write_mode && SELECTED_MEDIA;",
                 "+ONLY_MOTOR","Sony write cleanup deselection lost native timing"))
        for name,original,before_text,after_text,plusarg,message in mutations:
            text=original.read_text(encoding="utf-8")
            befores=before_text if isinstance(before_text,tuple) else (before_text,)
            afters=after_text if isinstance(after_text,tuple) else (after_text,)
            for before_part,after_part in zip(befores,afters):
                if text.count(before_part)!=1:raise ValueError("negative-control mutation anchor changed: "+name)
                text=text.replace(before_part,after_part)
            mutated_dir=build/("negative-"+name);mutated_dir.mkdir(exist_ok=True)
            mutated=mutated_dir/original.name;mutated.write_text(text,encoding="utf-8",newline="\n")
            exe=build_bench([mutated if path==original else path for path in sources],mutated_dir/"obj","negative-"+name+"-")
            log=build/("negative-"+name+".log")
            with log.open("w") as stream:r=subprocess.run([str(exe),plusarg],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
            text=log.read_text(errors="replace")
            if r.returncode==0 or message not in text:raise ValueError("negative control did not fail the intended check: "+name)
            negatives.append({"mutation":name,"expected_failure":message,"exit_code":r.returncode,"log_sha256":hashlib.sha256(log.read_bytes()).hexdigest()})
    if hashes()!=before:raise ValueError("gate sources changed during regression; rerun current sources")
    (build/"validation.json").write_text(json.dumps({"schema_version":1,"result":"PASS","files":before,
        "measurements":rows,"dma_measurements":dma,"sound_glu":glu,"floppy_measurements":floppy,"floppy_timing_cases":2048,"mega2_store_speedup":ratio,"mega2_load_speedup":load_ratio,
        "negative_controls":negatives,"negative_controls_run":a.negative_controls,
        "joystick_negative_control_run":a.joystick_negative_control,
        "floppy_idle_negative_control_run":a.floppy_idle_negative_control,
        "floppy_policy_negative_controls_run":a.floppy_policy_negative_controls,
        "checks":["production 0x4101 input handshake preserves all256 unsigned values per axis, independent endpoints and reset/release center; unassigned second joystick stays centered",
            "C070 read/write triggers produce C064/C065 bit7 expiry at native paddle timing for center and independent minimum/maximum axes",
            "C071-C07F read/write bank aliases never trigger paddle compatibility delay",
            "C070 read/write and C064-C067 retain four-ms delay and restore acceleration",
            "IOLC-inhibited C070 RAM has no IO side effect",
            "all shadow/config RAM read/write classes fast, mirror writes on the slow-RAM port; E0/E1 RAM/LC ROM fast, E0/E1 C000-CFFF native",
            "fast and stock shadow and Mega II writes update the slow-RAM port exactly once; DDR copy posted and checked after drain",
            "fast Mega II reads/writes honor bank latch, ALTZP, RAMRD/RAMWRT, LC bank1/bank2/write protect/ROM; video port coherent",
            "all2048 family/drive/media/motor/guest-request/command combinations preserve raw state and honor selected mounted guest timing requests",
            "real 65816 selected-spinning and deselected Sony work both reach maximum cadence; an equal-work guest-native countdown outlives the two-ms lease without accelerating",
            "Disk II command motor bit retains native timing past the lease; motor-off releases acceleration while the spindle coasts",
            "actual IWM write-mode entry remains native beyond two ms, through Sony C031 cleanup deselection, and for routed drive2; empty routed drives do not hold; Q7 clear plus guard expiry restores selected-spinning acceleration",
            "IWM access hold applies only with media in the selected drive (family and drive select), then expires after two ms",
            "IWM mode register latches on any odd address except motor-on with Q6=Q7=1 while idle; the immediate mode view agrees on the motor-on write; mode bit 2 makes motor-off immediate",
            "every slow-RAM port-A write is staged once and never lands on the video read edge",
            "real 65816 bank-E1 load loop exact; each fast Mega II read takes three primary ticks; stock cadence unchanged",
            "real 65816 sound GLU register stores, auto-increment wave upload and their read-back exact at speeds 3 and 6 via the GLU native hold",
            "real 65816 IRQ dispatch enters shadow-RAM warm loop at stock and maximum",
            "shadow and bank-E1 store loops preserve 1024 exact slow-RAM writes at each rate; shadow DDR retirements exact, merged lanes bounded; stock cadence unchanged; at maximum at most one extra tick per store and at least 20x stock",
            "real 65816 slot-7 ROM copy at maximum returns exact bytes; HDD DMA into E1 and shadowed bank 00 exact at stock and maximum",
            "stock HDD DMA into E1 and shadowed bank 00 unchanged; accelerated DMA paced at three CLK14 ticks per byte"],
        "limits":["Synthetic ROM only; no full-system boot, FPGA timing or physical hardware claim.",
            "Shadow-read loop excludes256 warmup iterations; store loops exclude16 warmup iterations.",
            "CPU advances include instruction-internal cycles. The bench DDR model answers in a few cycles; board DDR latency is not modeled.",
            "57.272728MHz is the nominal primary clock, not a measured physical oscillator."]},indent=2)+"\n")
    print(out.strip());return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f"ERROR: {error}",file=sys.stderr);raise SystemExit(1)
