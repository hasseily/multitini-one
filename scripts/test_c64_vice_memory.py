#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare DMA zero-page backing writes with unchanged VICE 3.10 handlers.

The RTL slice is the verbatim production write-data assignment. This checks
the CPU/DMA distinction, not full PLA mapping or physical board behavior.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from c64_vice_oracle import ROOT, REFERENCE, COMMIT, digest, verify_reference


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/c64-vice-memory')
    parser.add_argument('--vivado-bin', type=Path, default=Path('C:/Xilinx/2025.2/Vivado/bin'))
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT/'build' or not out.is_relative_to(ROOT/'build'):
        parser.error('Use a dedicated build/ child')
    out.mkdir(parents=True, exist_ok=True)
    receipt_path = out/'validation.json'
    receipt_path.unlink(missing_ok=True)
    verify_reference()
    reference = REFERENCE/'upstream/vice/src/c64/c64memsc.c'
    rtl = ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd'
    inputs = [reference, rtl, Path(__file__), ROOT/'scripts/c64_vice_oracle.py']
    snapshot = lambda: {p.relative_to(ROOT).as_posix(): digest(p) for p in inputs}
    before = snapshot()
    env = {k:v for k,v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = os.environ.get('PATH', os.environ.get('Path', ''))
    guard = [sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--'] if os.name == 'nt' else []
    result = {'status':'FAIL', 'reference_commit':COMMIT, 'source_sha256':before,
              'scope':'Unchanged VICE zero_read_dma/zero_store_dma versus verbatim production ramDout; no full-machine decode or timing claim.'}

    def run(name, command):
        p = subprocess.run(guard+list(map(str,command)), cwd=out, env=env,
                           capture_output=True, text=True, timeout=120)
        (out/(name+'.log')).write_text(p.stdout+p.stderr, encoding='utf-8')
        if p.returncode:
            raise RuntimeError(name+': '+(p.stdout+p.stderr)[-5000:])

    try:
        src = reference.read_text()
        extracted = '\n'.join(function(src, s) for s in
            ('uint8_t zero_read_dma(uint16_t addr)', 'void zero_store_dma(uint16_t addr, uint8_t value)'))
        (out/'vice_dma.c').write_text('''/* VICE extracted handlers retain GPL-2.0-or-later; see pinned source. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int c64_256k_enabled=0,plus256k_enabled=0,vbank=0;
static uint8_t mem_ram[65536];
static uint8_t c64_256k_ram_segment0_read(uint16_t a){(void)a;abort();}
static uint8_t plus256k_ram_low_read(uint16_t a){(void)a;abort();}
static void c64_256k_ram_segment0_store(uint16_t a,uint8_t v){(void)a;(void)v;abort();}
static void plus256k_ram_low_store(uint16_t a,uint8_t v){(void)a;(void)v;abort();}
'''+extracted+'''
int main(void){
 for(unsigned a=0;a<256;a++)for(unsigned d=0;d<256;d+=85)for(unsigned f=0;f<256;f+=85){
  mem_ram[a]=0x3c;zero_store_dma((uint16_t)a,(uint8_t)d);
  if(zero_read_dma((uint16_t)a)!=d)return 2;
  printf("%u %u %u %u\\n",a,d,f,mem_ram[a]);
 }return 0;
}
''')
        compiler = Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))/'gcc.exe' if os.name=='nt' else 'gcc'
        run('compile-reference',[compiler,'-std=c11','-Wall','-Wextra','-Werror','vice_dma.c','-o','vice_dma.exe'])
        run('vectors',[out/'vice_dma.exe'])
        (out/'vectors.txt').write_bytes((out/'vectors.log').read_bytes())
        assignments = re.findall(r'\bramDout\s*<=.*?;',rtl.read_text(),re.S)
        if len(assignments)!=1:raise RuntimeError('Ambiguous production write-data assignment')
        (out/'production_write_data.vhd').write_text('''library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;
entity production_write_data is port(systemWe,dma_active:in std_logic;systemAddr:in unsigned(15 downto 0);lastVicDi,cpuDo:in unsigned(7 downto 0);ramDout:out unsigned(7 downto 0));end;
architecture rtl of production_write_data is begin
'''+assignments[0]+'\nend;\n')
        (out/'tb.vhd').write_text('''library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;use std.textio.all;
entity tb is end; architecture sim of tb is
signal a:unsigned(15 downto 0);signal d,f,q:unsigned(7 downto 0);signal dma:std_logic:='1';
begin
dut:entity work.production_write_data port map('1',dma,a,f,d,q);
process
file input_file:text open read_mode is "vectors.txt";
file output_file:text open write_mode is "rtl.txt";
variable l,o:line;variable av,dv,fv,expected:integer;
begin
while not endfile(input_file) loop
 readline(input_file,l);read(l,av);read(l,dv);read(l,fv);read(l,expected);
 a<=to_unsigned(av,16);d<=to_unsigned(dv,8);f<=to_unsigned(fv,8);wait for 1 ns;
 write(o,av);write(o,string'(" "));write(o,dv);write(o,string'(" "));write(o,fv);
 write(o,string'(" "));write(o,to_integer(q));writeline(output_file,o);
end loop;
-- Preserve the separately qualified CPU floating-bus behavior at its port.
dma<='0';d<=x"5a";f<=x"a5";
for addr in 0 to 2 loop
 a<=to_unsigned(addr,16);wait for 1 ns;
 if addr<2 then assert q=x"a5" report "CPU port backing changed" severity failure;
 else assert q=x"5a" report "Ordinary CPU RAM write changed" severity failure;end if;
end loop;
report "VICE_MEMORY_COMPLETE";std.env.stop;wait;
end process;end;
''')
        run('analyze',[args.vivado_bin/'xvhdl.bat','--nolog','--2008','production_write_data.vhd','tb.vhd'])
        run('elaborate',[args.vivado_bin/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','dma_memory','tb'])
        run('simulate',[args.vivado_bin/'xsim.bat','--nolog','dma_memory','--runall'])
        expected=[list(map(int,l.split())) for l in (out/'vectors.txt').read_text().splitlines()]
        actual=[list(map(int,l.split())) for l in (out/'rtl.txt').read_text().splitlines()]
        if len(expected)!=4096 or actual!=expected:raise RuntimeError('DMA backing comparison failed')
        if 'VICE_MEMORY_COMPLETE' not in (out/'simulate.log').read_text():raise RuntimeError('Missing completion')
        old_bug=[row[:3]+[row[2] if row[0]<2 else row[3]] for row in actual]
        if sum(a!=e for a,e in zip(old_bug,expected))!=24:raise RuntimeError('Old DMA bug not detected')
        if snapshot()!=before:raise RuntimeError('Sources changed during comparison')
        result.update(status='PASS', cases=4096, cpu_control_cases=3,
                      sources_unchanged=True, old_bug_negative_control='24 mismatches rejected')
    except Exception as error:
        result['error']=str(error)
        raise
    finally:
        result['evidence_sha256']={p.name:digest(p) for p in out.iterdir() if p.is_file() and p.suffix in ('.c','.vhd','.txt','.log')}
        receipt_path.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS VICE DMA backing: 4096 cases, 3 CPU controls, old-bug negative control')
    return 0


if __name__=='__main__':raise SystemExit(main())
