#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check ordered video tuples and native cartridge-cache holds in PAL and NTSC."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def video_lane(adapter):
    start = '    // The VIC consumes ext_frame_run on the rising edge.'
    end = '    // Sample the signed SID outputs at 48 kHz'
    if adapter.count(start) != 1 or adapter.count(end) != 1:
        raise ValueError('Production video block anchors changed')
    block = adapter[adapter.index(start):adapter.index(end)]
    return '''module cart_video_lane #(parameter bit NTSC_MODE=0)(
    input logic core_clk,core_reset_n,core_paused,ext_frame_run,
    input logic [7:0] raw_r,raw_g,raw_b,
    input logic raw_hsync,raw_vsync,
    output logic [7:0] video_r,video_g,video_b,
    output logic video_de,video_hsync,video_vsync,video_valid);
    logic video_advance_q,sync_pixel_tick,sync_pixel_tick_d;
    logic sync_hsync,sync_vsync,hblank,vblank;
''' + block + '\nendmodule\n'


def adapter_probe(fixture):
    # Reuse the existing full-adapter wiring, memory responder and native bus
    # helpers, with a separate active-display workload. No RTL is replaced.
    anchor = '    initial begin\n        for(integer a=0;a<512;a++)backing'
    if fixture.count(anchor) != 1:
        raise ValueError('Cartridge fixture stimulus anchor changed')
    prefix = fixture.split(anchor)[0]
    prefix = prefix.replace('module tb_c64_cart_coexist #', 'module tb_c64_cart_video_stall #', 1)
    before = 'cart_delay<=bulk_load?0:4+(cart_bytes%5);'
    if prefix.count(before) != 1:
        raise ValueError('Cartridge backend delay anchor changed')
    prefix = prefix.replace(before, 'cart_delay<=cart_write?0:4+(cart_reads%23);', 1)
    return prefix + ADAPTER_STIMULUS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--phase', choices=('all', 'tuple', 'adapter'), default='all')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        parser.error('fresh dedicated build/ child required')
    out.mkdir(parents=True)
    sources = [ROOT / 'hdl/generated/multitini_regs_pkg.sv',
               ROOT / 'third_party/appletini/hdl/cdc_bit_sync.sv',
               ROOT / 'hdl/common/multitini_async_mailbox.sv']
    section = ''
    for raw in (ROOT / 'hdl/hdl_sources.txt').read_text().splitlines():
        line = raw.strip()
        if line.startswith('['):
            section = line
        elif section == '[c64]' and line and not line.startswith('#'):
            path = (ROOT / 'hdl' / line.split('|')[0].strip()).resolve()
            if path.suffix in ('.sv', '.v', '.vhd'):
                sources.append(path)
    bench = ROOT / 'hdl/sim/tb_c64_cart_video.sv'
    adapter = ROOT / 'hdl/core/c64/multitini_c64_adapter.sv'
    sync = ROOT / 'hdl/core/c64/ports/video_sync.vhd'
    fixture = ROOT / 'hdl/sim/tb_c64_cart_coexist.sv'
    evidence = list(dict.fromkeys(sources + [bench, fixture, Path(__file__).resolve(),
        ROOT / 'scripts/run_native_tool.py', ROOT / 'hdl/hdl_sources.txt']))
    receipt = {'status': 'FAIL', 'phase': args.phase, 'cases': [], 'negative_controls': [],
               'source_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in evidence}}
    for p in evidence:
        target = out / 'sources' / p.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
    snap = lambda p: out / 'sources' / p.relative_to(ROOT)
    vivado = Path(os.environ.get('XILINX_VIVADO', 'C:/Xilinx/2025.2/Vivado')) / 'bin'

    def run(folder, name, argv, expected_failure=None):
        result = subprocess.run([sys.executable, ROOT / 'scripts/run_native_tool.py', '--cwd', folder,
            '--', *argv], capture_output=True, text=True, errors='replace', timeout=900)
        log = result.stdout + result.stderr
        (folder / (name + '.log')).write_text(log)
        bad = result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:', log, re.I)
        if expected_failure:
            if not bad or expected_failure not in log:
                raise RuntimeError(name + ' negative control not rejected: ' + log[-2500:])
        elif bad:
            raise RuntimeError(name + ': ' + log[-5000:])
        return log

    try:
        if args.phase in ('all', 'tuple'):
            for ntsc in (0, 1):
                folder = out / ('tuple-ntsc' if ntsc else 'tuple-pal')
                folder.mkdir()
                lane = folder / 'lane.sv'
                lane.write_text(video_lane(snap(adapter).read_text()))
                wrapper = folder / 'profile.sv'
                wrapper.write_text(f'module profile; tb_c64_cart_video #(.NTSC({ntsc})) test(); endmodule\n')
                run(folder, 'vhdl', [vivado / 'xvhdl.bat', '--nolog', '--2008', snap(sync)])
                run(folder, 'sv', [vivado / 'xvlog.bat', '--nolog', '--sv', lane, snap(bench), wrapper])
                run(folder, 'elaborate', [vivado / 'xelab.bat', '--nolog', '--mt', 'off', '--timescale', '1ns/1ps', '-s', 'tuple_sim', 'profile'])
                log = run(folder, 'positive', [vivado / 'xsim.bat', '--nolog', 'tuple_sim', '--runall'])
                match = re.search(rf'VIDEO TUPLES PASS NTSC={ntsc} tuples=(\d+) phase_length_pairs=16 stalls=(\d+)', log)
                if not match:
                    raise RuntimeError('Missing complete tuple/phase inventory')
                receipt['cases'].append({'kind': 'tuple', 'ntsc': ntsc, 'tuples': int(match[1]), 'phase_length_pairs': 16})
                print(match[0], flush=True)
            folder = out / 'tuple-pal'
            original = (folder / 'lane.sv').read_text()
            before = '.advance(video_advance_q)'
            if original.count(before) != 1:
                raise RuntimeError('Always-advancing negative anchor changed')
            mutant = folder / 'always_advance.sv'
            mutant.write_text(original.replace(before, ".advance(1'b1)", 1))
            run(folder, 'negative-compile', [vivado / 'xvlog.bat', '--nolog', '--sv', mutant])
            run(folder, 'negative-elaborate', [vivado / 'xelab.bat', '--nolog', '--mt', 'off', '--timescale', '1ns/1ps', '-s', 'always_advance_sim', 'profile'])
            run(folder, 'negative', [vivado / 'xsim.bat', '--nolog', 'always_advance_sim', '--runall'], 'VIDEO tuple mismatch')
            receipt['negative_controls'].append({'name': 'original_always_advancing_video', 'rejected': True})
            print('VIDEO negative original always-advancing video rejected', flush=True)
        if args.phase in ('all', 'adapter'):
            for ntsc in (0, 1):
                folder = out / ('adapter-ntsc' if ntsc else 'adapter-pal')
                folder.mkdir()
                probe = folder / 'probe.sv'
                probe.write_text(adapter_probe(snap(fixture).read_text()))
                wrapper = folder / 'profile.sv'
                wrapper.write_text(f'module profile; tb_c64_cart_video_stall #(.NTSC({ntsc})) test(); endmodule\n')
                sv = [p for p in sources if p.suffix != '.vhd']
                run(folder, 'imported', [vivado / 'xvlog.bat', '--nolog', '--sv', '--relax', *[snap(p) for p in sv if 'third_party' in p.parts]])
                run(folder, 'local', [vivado / 'xvlog.bat', '--nolog', '--sv', *[snap(p) for p in sv if 'third_party' not in p.parts], probe, wrapper])
                run(folder, 'vhdl', [vivado / 'xvhdl.bat', '--nolog', '--2008', *[snap(p) for p in sources if p.suffix == '.vhd']])
                run(folder, 'elaborate', [vivado / 'xelab.bat', '--nolog', '--relax', '--mt', 'off', '--timescale', '1ns/1ps', '-s', 'adapter_sim', 'profile'])
                log = run(folder, 'positive', [vivado / 'xsim.bat', '--nolog', 'adapter_sim', '--runall'])
                match = re.search(rf'VIDEO CACHE PASS NTSC={ntsc} held_pixels=0 native=(\d+) sync=(\d+) cartreads=1024', log)
                if not match:
                    raise RuntimeError('Missing active-display cartridge/cache evidence')
                receipt['cases'].append({'kind': 'adapter', 'ntsc': ntsc, 'cart_reads': 1024, 'held_pixels': 0})
                print(match[0], flush=True)
        receipt['sources_unchanged'] = all(sha(ROOT / p) == digest for p, digest in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:
            raise RuntimeError('Sources changed during validation')
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        print(str(error), flush=True)
    receipt['artifact_sha256'] = {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*')
        if p.is_file() and 'sources' not in p.relative_to(out).parts and p.suffix in ('.sv', '.log', '.csv')}
    (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt['status'] != 'PASS'


ADAPTER_STIMULUS = r'''
    integer sample_mode=0, wait_age=0, held_pixels=0, source_pixels=0, sync_pixels=0;
    integer native_line_pixels=0,sync_line_pixels=0,line_core_clocks=0,line_waits=0,line_count=0;
    reg old_hsync=0;
    integer trace;
    always @(posedge clk) begin
        if(sample_mode!=0)begin
            line_core_clocks++;
            if(!dut.ext_frame_run)begin wait_age++;line_waits++;end else wait_age=0;
            if(dut.ext_frame_run&&dut.ext_phase[1:0]==3)begin source_pixels++;native_line_pixels++;end
            if(dut.sync_pixel_tick)begin sync_pixels++;sync_line_pixels++;end
            // Exclude pending pixels from the two-cycle capture pipeline.
            if(wait_age>4&&dut.video_valid)held_pixels++;
            if(dut.raw_hsync&&!old_hsync)begin
                $fdisplay(trace,"%0d,%0d,%0d,%0d,%0d,%0d",sample_mode,line_count,line_core_clocks,native_line_pixels,sync_line_pixels,line_waits);
                if(line_count>0)begin
                    check(native_line_pixels==(NTSC?520:504),"native pixel line length remains fixed");
                    check(sync_line_pixels==native_line_pixels,"video ticks preserve native line length");
                end
                line_count++;line_core_clocks=0;native_line_pixels=0;sync_line_pixels=0;line_waits=0;
            end
        end else begin
            line_count=0;line_core_clocks=0;native_line_pixels=0;sync_line_pixels=0;line_waits=0;
        end
        old_hsync=dut.raw_hsync;
    end
    initial begin
        trace=$fopen("lines.csv","w");$fdisplay(trace,"mode,line,core_clocks,native_pixels,sync_pixels,wait_clocks");
        for(integer a=0;a<512;a++)backing[a]=0;
        for(integer a=0;a<2048;a++)buffer[a]=0;
        for(integer a=0;a<8192;a++)rom[a]=8'hea;
        rom[0]=8'h4c;rom[1]=0;rom[2]=8'he0;
        rom[8190]=0;rom[8191]=8'he0;rom[8188]=0;rom[8189]=8'he0;rom[8186]=0;rom[8187]=8'he0;
        repeat(8)@(negedge clk);reset=0;
        for(integer kind=1;kind<=3;kind++)for(integer a=0;a<(kind==3?4096:8192);a++)
            send(kind,a,kind==3?4096:8192,kind==2?rom[a]:8'hea);
        for(integer a=0;a<32768;a++)begin dut.main_ram_i.main_low[a]=8'h5a;dut.main_ram_i.main_high[a]=8'h5a;end
        for(integer a=0;a<1024;a++)dut.main_ram_i.colour[a]=4'h1;
        pause_request=0;repeat(20000)@(negedge clk);
        force dut.cpu_island_i.cpu_enable=0;
        force dut.ext_cpu_addr=probe_addr;force dut.ext_cpu_we=probe_we;
        force dut.ext_cpu_do=probe_data;force dut.ext_cpu_io=8'h37;
        attach(87,0,4,16384,7);
        native_write(16'hd015,0);native_write(16'hd016,8'h08);native_write(16'hd018,8'h14);
        native_write(16'hd020,8'h0e);native_write(16'hd021,6);native_write(16'hd011,8'h1b);
        repeat(NTSC?600000:700000)@(negedge clk);
        sample_mode=1;repeat(24000)@(negedge clk);sample_mode=0;
        check(held_pixels==0,"baseline has no halted source pixels");
        $display("VIDEO BASELINE NTSC=%0d native=%0d sync=%0d",NTSC,source_pixels,sync_pixels);
        repeat(10)@(negedge clk);
        sample_mode=2;
        for(integer i=0;i<1024;i++)begin
            if(i%64==0)native_write(16'hde00,(i/64)&3);
            read_value(16'h8000+((i%1024)*8),8'h40+((i/64)&3),"ROM fetch remains correct");
        end
        sample_mode=0;
        check(held_pixels==0,"no new video pixels during cartridge hold");
        $display("VIDEO CACHE PASS NTSC=%0d held_pixels=%0d native=%0d sync=%0d cartreads=%0d",NTSC,held_pixels,source_pixels,sync_pixels,cart_reads);
        $fclose(trace);$finish;
    end
    initial begin #100000000;$fatal(1,"VIDEO STALL timeout");end
endmodule
'''


if __name__ == '__main__':
    raise SystemExit(main())
