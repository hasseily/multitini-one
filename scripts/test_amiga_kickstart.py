#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run a user-owned Kickstart through the real ECS CPU/memory path.

The ROM remains an external private input. A generated testbench extends the
original legal CPU diagnostic harness with sparse RAM and a native frame dump.
An execution checkpoint is not, by itself, a full OS or board-boot acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from test_amiga_ports import ROOT,machine_sources


def once(text,before,after):
    if text.count(before)!=1:raise ValueError(f"Kickstart fixture base changed at {before[:80]!r}")
    return text.replace(before,after)


def fixture(cycles: int) -> str:
    text=(ROOT / "hdl/sim/tb_amiga_cpu.sv").read_text()
    text=text.replace("tb_amiga_cpu","tb_amiga_kickstart")
    text=once(text,"    reg [63:0] chip_store=0,fast_store=0,blit_store=0,repeat_store=0;", """    reg [63:0] chip_store=0,fast_store=0;
    reg [7:0] kick_rom[0:1048575];
    reg [63:0] ram_store[int unsigned];
    initial $readmemh("kick.hex",kick_rom);
    wire [23:0] video_rgb;
    wire video_hs,video_vs,video_hb,video_vb,video_ntsc,video_field;
    wire [11:0] scan_x,scan_y;
    wire [15:0] scan_pixel;
    wire scan_valid,scan_start;
    wire [31:0] malformed;
    reg consumed_tick=0;
    always @(posedge clk)consumed_tick<=!reset && chip_tick;
    multitini_amiga_video_normalizer normalizer(
        .clk(clk),.reset(reset),.ready(!guest_reset),.chip_tick(consumed_tick),
        .capture_mode(2'd0),.native_lace(1'b0),.diw_hstart(9'd129),.diw_vstart(11'd44),
        .hsync_n(video_hs),.vsync_n(video_vs),.ntsc(video_ntsc),.field_short(video_field),.hblank(video_hb),.vblank(video_vb),.rgb(video_rgb),
        .scan_x(scan_x),.scan_y(scan_y),.rgb565(scan_pixel),.frame_start(scan_start),.valid(scan_valid),.malformed_frames(malformed));
    reg [15:0] frame_pixels[0:640*512-1];
    integer frame_count=0,colored=0,rom_fetches=0;
    always @(negedge clk)if(scan_valid)begin
        if(scan_x<640 && scan_y<512)begin
            frame_pixels[scan_y*640+scan_x]=scan_pixel;
            if(scan_pixel!=0)colored=colored+1;
        end
        if(scan_x==639 && scan_y==511)begin
            frame_count=frame_count+1;
            $display("Kickstart native frame %0d, nonblack samples=%0d, CPU address=%h",frame_count,colored,dut.cpu.cpu_addr);
        end
    end
    always @(posedge chip_clk)if(dut.cpu.clkena_p_throttled && dut.cpu.cpu_req && cpu_state==0 && dut.cpu.cpu_addr[31:19]==13'h001f)
        rom_fetches=rom_fetches+1;
""")
    text=once(text,".rtc(65'd0),.cpucfg(cpucfg),.cpu_reset_n(cpu_reset_n));", ".rtc(65'd0),.cpucfg(cpucfg),.cpu_reset_n(cpu_reset_n),\n        .field(video_field),.rgb(video_rgb),.hsync_n(video_hs),.vsync_n(video_vs),.hblank(video_hb),.vblank(video_vb),.ntsc(video_ntsc));")
    start=text.index("    function automatic [63:0] read_line")
    end=text.index("    endfunction",start)+len("    endfunction")
    text=text[:start]+"""    function automatic [63:0] read_line(input [23:0] address);
        integer n,offset;
        begin
            read_line=0;
            if(address>=24'h780000 && address<24'h800000)begin
                offset=524288+(address-24'h780000);
                for(n=0;n<8;n=n+1)read_line[8*n+:8]=kick_rom[offset+n];
            end else if(address>=24'h600000 && address<24'h680000)begin
                offset=address-24'h600000;
                for(n=0;n<8;n=n+1)read_line[8*n+:8]=kick_rom[offset+n];
            end else if(ram_store.exists(address[23:3]))read_line=ram_store[address[23:3]];
        end
    endfunction"""+text[end:]
    start=text.index("                if(held_write)begin")
    end=text.index("                response_data<=read_line",start)
    text=text[:start]+"""                if(held_write)begin
                    if(!ram_store.exists(held_addr[23:3]))ram_store[held_addr[23:3]]=64'd0;
                    for(lane=0;lane<8;lane=lane+1)if(held_mask[lane])
                        ram_store[held_addr[23:3]][lane*8+:8]=held_data[lane*8+:8];
                end
"""+text[end:]
    start=text.index("    initial begin\n",text.index("    task automatic config_cpu"))
    end=text.index("\nendmodule",start)
    text=text[:start]+"""    integer image_handle,pixel_index;
    reg [15:0] image_pixel;
    initial begin
        for(pixel_index=0;pixel_index<640*512;pixel_index=pixel_index+1)frame_pixels[pixel_index]=0;
        repeat(8)@(negedge clk);reset=0;
        repeat(32)@(negedge chip_clk);config_cpu();
        force dut.chipset.CONTROL1.rst_cnt=3'd4;
        guest_reset=0;
        for(cycles=0;cycles<MAX_CYCLES;cycles=cycles+1)begin
            @(negedge clk);
            if(memory_fault)$fatal(1,"Private Kickstart memory adapter fault");
        end
        image_handle=$fopen("kickstart-frame.ppm","wb");
        $fwrite(image_handle,"P6\\n640 512\\n255\\n");
        for(pixel_index=0;pixel_index<640*512;pixel_index=pixel_index+1)begin
            image_pixel=frame_pixels[pixel_index];
            $fwrite(image_handle,"%c%c%c",{image_pixel[15:11],image_pixel[15:13]},
                    {image_pixel[10:5],image_pixel[10:9]},{image_pixel[4:0],image_pixel[4:2]});
        end
        $fclose(image_handle);
        if(transactions<100 || rom_fetches<100)$fatal(1,"Kickstart execution did not progress");
        $display("PASS Kickstart execution checkpoint: CPU=%h ROMfetches=%d DDRtransactions=%d frames=%d nonblack=%d malformed=%d",dut.cpu.cpu_addr,rom_fetches,transactions,frame_count,colored,malformed);
        $finish;
    end""".replace("MAX_CYCLES",str(cycles))+text[end:]
    startup="""
    initial begin
        // Configuration INIT=0 on the 19 unreset Agnus counter FFs.
        dut.chipset.AGNUS1.bc1.hpos[8:1]=8'd0;
        dut.chipset.AGNUS1.bc1.vpos=11'd0;
    end
"""
    text=text.replace("\nendmodule",startup+"\nendmodule",1)
    return "// Generated by scripts/test_amiga_kickstart.py; no system ROM bytes.\n"+text


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kickstart",type=Path,required=True)
    parser.add_argument("--cycles",type=int,default=8_000_000)
    parser.add_argument("--build-dir",type=Path,default=ROOT / "build/amiga-kickstart-test")
    parser.add_argument("--vivado",type=Path,default=Path("C:/Xilinx/2025.2/Vivado"))
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or args.cycles<1000:raise SystemExit("Select repository build directory and a positive execution budget")
    build.mkdir(parents=True,exist_ok=True)
    rom=args.kickstart.read_bytes()
    if len(rom) not in (262144,524288,1048576):raise SystemExit("Kickstart must contain 256 KiB, 512 KiB or 1 MiB")
    staged=rom*(1048576//len(rom))
    (build / "kick.hex").write_text("".join(f"{b:02x}\n" for b in staged))
    tb=build / "tb_amiga_kickstart.sv";tb.write_text(fixture(args.cycles))
    vhdlbase=ROOT / "third_party/amiga/upstream/rtl/tg68k"
    vhdl=[vhdlbase / p for p in ("TG68K_Pack.vhd","TG68K_ALU.vhd","TG68K_MMU030.vhd","TG68KdotC_Kernel.vhd","TG68K.vhd")]
    sources=machine_sources()+[ROOT / "hdl/sim/tb_amiga_ports.sv",ROOT / "hdl/core/amiga/multitini_amiga_memory.sv",ROOT / "hdl/board/multitini_amiga_video_normalizer.sv",tb]
    assets=[ROOT / "hdl/core/amiga/ports/m68k_cyc_idx.hex",ROOT / "hdl/core/amiga/ports/m68k_ea_idx.hex",ROOT / "third_party/amiga/upstream/rtl/fx68k/microrom.mem",ROOT / "third_party/amiga/upstream/rtl/fx68k/nanorom.mem"]
    for p in assets:shutil.copyfile(p,build / p.name)
    inputs=vhdl+sources+assets+[Path(__file__).resolve(),ROOT / "hdl/sim/tb_amiga_cpu.sv"]
    receipt={"schema_version":1,"board_evidence":False,"cycles":args.cycles,"rom":{"input_path":str(args.kickstart.resolve()),"input_bytes":len(rom),"sha256":hashlib.sha256(rom).hexdigest(),"private_external_input":True},"limitations":["Native four-frame reset timer shortened by test fixture","The 19 unreset Agnus beam-counter FFs model FPGA configuration INIT=0","Execution checkpoint is not a completed OS boot or physical-board acceptance"],"sources":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (build / "inputs.json").write_text(json.dumps(receipt,indent=2)+"\n")
    def run(name,argv):
        result=subprocess.run([sys.executable,str(ROOT / "scripts/run_native_tool.py"),"--cwd",str(build),"--","cmd.exe","/d","/c",str(args.vivado / "bin" / f"{name}.bat"),*map(str,argv)],capture_output=True,text=True,errors="replace")
        (build / f"{name}.stdout.log").write_text(result.stdout+result.stderr)
        if result.returncode:
            print((result.stdout+result.stderr)[-12000:]);raise SystemExit(result.returncode)
        print(f"{name}: passed",flush=True)
    run("xvhdl",["--2008","--work","work",*vhdl])
    run("xvlog",["--sv","--work","work","-i",ROOT / "third_party/amiga/upstream/rtl",*sources])
    run("xelab",["work.tb_amiga_kickstart_turbo","-s","amiga_kickstart","--debug","typical","--timescale","1ns/1ps","--mt","auto"])
    run("xsim",["amiga_kickstart","--runall"])
    log=(build / "xsim.stdout.log").read_text()
    if "PASS Kickstart execution checkpoint" not in log or "Fatal:" in log:raise SystemExit(f"Private Kickstart failed; see {build / 'xsim.stdout.log'}")
    for name,expected in receipt["sources"].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest()!=expected:raise SystemExit(f"Input changed during Kickstart execution: {name}")
    receipt["result"]="execution_checkpoint_passed"
    receipt["frame_image"]="kickstart-frame.ppm"
    (build / "report.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print("PASS external Kickstart execution checkpoint; inspect frame image and limits")
    return 0


if __name__=="__main__":raise SystemExit(main())
