#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce the NES Xilinx clock-enable and explicit mapper-mux port."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/nes/upstream/rtl'
TARGET = ROOT / 'hdl/core/nes/ports'
MODULES = ('DmaController', 'NES', 'VsProtection', 'VsCoinInput',
           'VramAddressGen', 'ClockGen', 'Sprite', 'SpriteSet', 'OAMEval',
           'SpriteAddressGen', 'SpriteAddressGenEx', 'BgPainter', 'PaletteRam',
           'PPU', 'ppu_video_phase', 'ppu_video_color', 'ppu_video_dac',
           'LenCounterUnit', 'EnvelopeUnit', 'SquareChan', 'TriangleChan',
           'NoiseChan', 'DmcChan', 'FrameCtr', 'APU',
           'MMC1', 'MMC2', 'MMC3', 'MMC4', 'Mapper28', 'Mapper66', 'Mapper71',
           'cart_top', 'CODES')


def balanced_end(text, pos):
    depth = 0
    for index in range(pos, len(text)):
        if text[index] == '(':
            depth += 1
        elif text[index] == ')':
            depth -= 1
            if depth == 0:
                return index + 1
    raise ValueError('unclosed port/parameter list')


def strict_declarations(text):
    # Quartus accepts a late declaration after an earlier implicit scalar.
    # Vivado rejects it. Hoist original module-level declarations, splitting
    # net initializers into continuous assignments at their original position.
    def fix(match):
        block=match[0]
        port_start=block.index('(')
        if '#(' in block[:port_start+1].replace(' ',''):
            port_start=block.index('(',balanced_end(block,port_start))
        elif re.match(r'module\s+\w+\s*#',block):
            port_start=block.index('(',balanced_end(block,port_start))
        header_end=balanced_end(block,port_start)+1
        header,body=block[:header_end],block[header_end:]
        declarations=[]
        def move(m):
            declaration=m[0]
            if declaration.startswith('wire ') and '=' in declaration:
                lhs,rhs=declaration[:-1].split('=',1)
                # Standard pinned net initializers contain a single net.
                name=re.search(r'([A-Za-z_]\w*)(?:\s*\[[^]]*\])*\s*$',lhs)[1]
                declarations.append(lhs.rstrip()+';')
                return 'assign '+name+' = '+rhs+';'
            declarations.append(declaration)
            return ''
        body=re.sub(r'(?m)^(?:wire|reg|logic|localparam|parameter)\s[^;]*;',move,body)
        # Constants must be visible in array bounds before declarations.
        constants=[x for x in declarations if x.startswith(('localparam ','parameter '))]
        others=[x for x in declarations if x not in constants]
        return header+'\n'+'\n'.join(constants+others)+'\n'+body
    return re.sub(r'(?m)^module\b.*?\bendmodule',fix,text,flags=re.S).replace('always begin','always @* begin')


def gate(text):
    # All machine state remains in the service clock domain. One master CE
    # advances the original master edge; memory may hold it without gating a
    # fabric clock. Combinational-only modules need no extra input.
    for name in MODULES:
        pattern = rf'\bmodule\s+{name}\b(?:\s*#\([^;]*?\))?\s*\('
        text, count = re.subn(pattern, lambda m: m[0] + '\n    input ce_master,', text)
    text = re.sub(r'((?:always|always_ff)\s*@\s*\(posedge clk\))',
                  r'\1 if (ce_master)', text)
    # Positional instances occur only for eReg_SavestateV and are unchanged.
    for name in MODULES:
        pattern = rf'(?m)^(\s*){name}\b\s*(?:#\('
        for match in reversed(list(re.finditer(rf'(?m)^\s*{name}\b', text))):
            pos = match.end()
            while text[pos].isspace(): pos += 1
            if text[pos:pos+1] == '#':
                pos = text.index('(', pos)
                pos = balanced_end(text, pos)
            inst = re.match(r'\s*\w+\s*\(', text[pos:])
            if inst:
                pos += inst.end()
                prefix=re.sub(r'//[^\n]*|/\*.*?\*/','',text[pos:],flags=re.S)
                named=prefix.lstrip().startswith('.')
                insert='\n    .ce_master(ce_master),' if named else 'ce_master, '
                text = text[:pos] + insert + text[pos:]
    return text


def selected_mapper(source, names):
    text = source.read_text()
    result = '// Derived from pinned MiSTer NES. See third_party/nes/README.md.\n'
    for name in names:
        match = re.search(rf'(?m)^module {name}\b.*?\bendmodule', text, re.S)
        if not match: raise ValueError(name)
        block = match[0].replace('inout', 'output')
        # Upstream drives a shared tri-state mapper bus. Each selected mapper
        # now has separate output wires and disabled outputs are zero; cart_top
        # reduces those outputs explicitly. No internal tri-state inference.
        block = re.sub(r"(\d+)'h[Zz]\b", r"\1'h0", block)
        result += block + '\n\n'
    return gate(result)


def cart():
    text = (SOURCE / 'cart.sv').read_text()
    header = text[text.index('module cart_top'):text.index(');')+2]
    selected = [('MMC1','mmc1','me[1]'), ('Mapper28','map28','me[0]|me[2]|me[3]|me[7]'),
                ('MMC2','mmc2','me[9]'), ('MMC3','mmc3','me[4]'),
                ('MMC4','mmc4','me[10]'), ('Mapper66','map66','me[11]|me[66]'),
                ('Mapper71','map71','me[71]')]
    buses = {'prg_addr_b':22,'chr_addr_b':22,'prg_dout_b':8,'chr_dout_b':8,
             'prg_allow_b':1,'chr_allow_b':1,'vram_a10_b':1,'vram_ce_b':1,
             'irq_b':1,'flags_out_b':16,'audio_out_b':16}
    out = header + '\nwire [13:0] chr_ain = chr_ex ? chr_ain_ex : chr_ain_orig;\n'
    out += 'reg [1023:0] me;\nalways @* begin me=0; me[flags[7:0]]=1; end\n'
    for bus,width in buses.items():
        out += f'wire [{width-1}:0] {bus}[0:6];\n'
    for index,(module,inst,en) in enumerate(selected):
        match = re.search(rf'(?m)^{module}\s+{inst}\s*\(.*?\n\);', text,re.S)
        if not match: raise ValueError(inst)
        block=match[0]
        block = re.sub(r'\.enable\s*\([^\n]*', f'.enable(({en}) & ~reset),',block)
        block = re.sub(r'\.SaveStateBus_Dout\s*\([^\n]*', '.SaveStateBus_Dout()',block)
        for bus in buses:
            block=re.sub(rf'\b{bus}\b(?=\s*\))', f'{bus}[{index}]',block)
        out += block+'\n'
        if module != 'Mapper28': out+=f"assign chr_dout_b[{index}]=8'd0;\n"
    for bus,width in buses.items():
        out+=f'wire [{width-1}:0] mux_{bus} = '+' | '.join(f'{bus}[{i}]' for i in range(7))+';\n'
    out+='''
always @* begin
    {prg_aout, prg_allow, chr_aout, vram_a10, vram_ce, chr_allow,
     prg_dout, chr_dout, irq, audio} =
    {3'b000, mux_prg_addr_b, mux_prg_allow_b, mux_chr_addr_b,
     mux_vram_a10_b, mux_vram_ce_b, mux_chr_allow_b,
     mux_prg_dout_b, mux_chr_dout_b, mux_irq_b, mux_audio_out_b};
    mapper_addr=0; mapper_data_out=0; mapper_prg_write=0; mapper_ovr=0;
    diskside=0;
    has_chr_dout=mux_flags_out_b[0]; prg_bus_write=mux_flags_out_b[1];
    prg_conflict=mux_flags_out_b[2]; has_savestate=0;
    prg_conflict_d0=mux_flags_out_b[4]; has_flashsaves=0;
    if (!prg_aout[21]) prg_aout[20:0]={prg_aout[20:11]&prg_mask,prg_aout[10:0]};
    if (chr_aout[21:20]==2'b10) chr_aout[19:0]={chr_aout[19:11]&chr_mask[8:0],chr_aout[10:0]};
    if(vram_ce) chr_aout={11'b11_1010_0000_0,vram_a10,chr_ain[9:0]};
    if(prg_ain<16'h2000) begin prg_aout={14'b00011_1000_0000_0,prg_ain[10:0]}; prg_allow=1; end
end
assign SaveStateBus_Dout=64'd0;
assign Savestate_MAPRAMReadData=8'd0;
endmodule
'''
    return gate(out)


def nes():
    text=(SOURCE/'nes.v').read_text()
    # Remove unavailable MiSTer state/rewind transport. Keep the original
    # per-unit reset defaults and hardware state-register implementation.
    start=text.index('savestates savestates (')
    end=text.index('\nendmodule',start)
    text=text[:start]+'''// Multitini: save states/rewind are not exposed.
assign reset_ss=reset_nes;
assign reset_delay=reset_nes;
assign state_loaded=0;
assign SaveStateBus_Din=0;
assign SaveStateBus_Adr=0;
assign SaveStateBus_wren=0;
assign SaveStateBus_rst=reset_nes;
assign loading_savestate=0;
assign saving_savestate=0;
assign sleep_savestates=0;
assign sleep_rewind=0;
assign Savestate_RAMAddr=0;
assign Savestate_RAMRdEn=0;
assign Savestate_RAMWrEn=0;
assign Savestate_RAMWriteData=0;
assign Savestate_RAMType=0;
assign SAVE_out_Din=0;
assign SAVE_out_Adr=0;
assign SAVE_out_rnw=1;
assign SAVE_out_ena=0;
assign SAVE_out_be=0;
assign sleep_savestate=0;
'''+text[end:]
    # RCA composite output is not routed; remove its large analog model.
    start=text.index('composite_board composite_board(')
    end=text.index('\n);',start)+3
    text=text[:start]+'assign composite_a=0; assign composite_b=0;'+text[end:]
    # New visible pixel CE is the original PPU edge, before any scaler.
    text=text.replace('output [15:0] sample,','output pixel_ce,\n\toutput [15:0] sample,',1)
    text=text.replace('assign apu_ce = cpu_ce;', 'assign apu_ce = cpu_ce;\nassign pixel_ce=ppu_ce;')
    # The fixed NTSC wrapper never changes sys_type after power-up. Upstream
    # only initializes this PAL divider counter on a type transition or pause;
    # an unknown counter can otherwise suppress four NTSC master ticks.
    text=text.replace('wire skip_ppu_cycle = (cpu_tick_count == 4) && (ppu_tick == 0);',
        "wire skip_ppu_cycle = (sys_type == 2'b01) && (cpu_tick_count == 4) && (ppu_tick == 0);")
    text=text.replace("if (cpu_ce && (sys_type == 2'b01))\n\t\tcpu_tick_count",
        "if (reset_nes) cpu_tick_count <= 0;\n\telse if (cpu_ce && (sys_type == 2'b01))\n\t\tcpu_tick_count")
    text=text.replace('.enable (cpu_ce)', '.enable (cpu_ce & ce_master)')
    text=text.replace('.mode   (0)', ".mode   (2'd0)").replace('.BCD_en (0)', ".BCD_en (1'b0)")
    text=text.replace('wire [15:0] cpu_addr;',
        'wire [23:0] cpu_fulladdr;\nwire [15:0] cpu_addr=cpu_fulladdr[15:0];')
    text=text.replace('.A      (cpu_addr)', '.A      (cpu_fulladdr)')
    text=text.replace('input  [20:0] prg_mask,','input  [9:0] prg_mask,').replace(
        'input  [19:0] chr_mask,','input  [9:0] chr_mask,')
    text=text.replace('.rdy    (~pause_cpu),', ".rdy    (~pause_cpu),\n\t.Abort_n(1'b1),\n\t.SO_n(1'b1),")
    text=text.replace('assign cpumem_read  = (prg_read & prg_allow) | (prg_write && prg_conflict);',
        'assign cpumem_read  = !reset && ((prg_read & prg_allow) | (prg_write && prg_conflict));')
    text=text.replace('assign cpumem_write = ', 'assign cpumem_write = !reset && ')
    text=text.replace('assign ppumem_read  = chr_read;', 'assign ppumem_read  = !reset && chr_read;')
    text=text.replace('assign ppumem_write = ', 'assign ppumem_write = !reset && ')
    text=text.replace('.evenframe           (evenframe_paused)',
        '.evenframe           (evenframe_paused),\n\t.SaveStateBus_Din(64\'d0),\n\t.SaveStateBus_Adr(10\'d0),\n\t.SaveStateBus_wren(1\'b0),\n\t.SaveStateBus_rst(reset_nes)')
    # Declare ce_master only at NES, not the unrelated DMA/coin port positions.
    return gate(text)


def outputs():
    out={'nes.sv':nes(),'cart.sv':cart(),
         'ppu.sv':gate((SOURCE/'ppu.sv').read_text()),
         'apu.sv':gate((SOURCE/'apu.sv').read_text()),
         'cheatcodes.sv':gate((SOURCE/'cheatcodes.sv').read_text()),
         'MMC1.sv':selected_mapper(SOURCE/'mappers/MMC1.sv',['MMC1']),
         'MMC2.sv':selected_mapper(SOURCE/'mappers/MMC2.sv',['MMC2','MMC4']),
         'MMC3.sv':selected_mapper(SOURCE/'mappers/MMC3.sv',['MMC3']),
         'generic.sv':selected_mapper(SOURCE/'mappers/generic.sv',['Mapper28','Mapper66','Mapper71'])}
    # UxROM NES2 submapper2 specifies AND bus conflicts. The shared NES bus
    # already fetches the old ROM byte and stalls coherently before that write.
    # Preserve the subtype supplied by the canonical header instead of treating
    # it as the default no-conflict mapper2 implementation.
    original='assign prg_conflict = prg_ain[15] && (mapper == 3) && (submapper != 1);'
    if out['generic.sv'].count(original)!=1:
        raise ValueError('pinned Mapper28 bus-conflict expression changed')
    out['generic.sv']=out['generic.sv'].replace(original,
        'assign prg_conflict = prg_ain[15] && (((mapper == 3) && (submapper != 1)) || ((mapper == 2) && (submapper == 2)));')
    cheat=out['cheatcodes.sv']
    params=re.search(r'parameter ADDR_WIDTH.*?parameter MAX_CODES[^;]*;',cheat,re.S)[0]
    cheat=cheat.replace(params,'').replace('module CODES(',
        'module CODES #(parameter ADDR_WIDTH=16, DATA_WIDTH=8, MAX_CODES=32)(')
    cheat=cheat.replace('output genie_ovr','output logic genie_ovr').replace(
        'output [DATA_WIDTH - 1:0] genie_data','output logic [DATA_WIDTH - 1:0] genie_data')
    out['cheatcodes.sv']=cheat
    out['ppu.sv']=out['ppu.sv'].replace(
        'wire [8:0] vblank_start_sl, vblank_end_sl, vsync_start_sl;',
        'logic [8:0] vblank_start_sl, vblank_end_sl, vsync_start_sl;').replace(
        'wire skip_en;', 'logic skip_en;').replace(
        'inout   [1:0] sys_type', 'input   [1:0] sys_type')
    out['ppu.sv']=out['ppu.sv'].replace('.custom1    (0),', ".custom1    (1'b0),").replace(
        '.addr         (pram_addr),', ".extra_bits(2'd0),\n\t.addr         (pram_addr),")
    out['ppu.sv']=out['ppu.sv'].replace(
        "latched_dout <= 8'd0;\n\t\tclear <= rst_behavior;",
        "latched_dout <= 8'd0;\n\t\tread_2007_delayed<=0;\n\t\twrite_2007_delayed<=0;\n\t\tclear <= rst_behavior;")
    # These newer upstream optional pins are otherwise left floating in the
    # board wrapper. Tie the analog/ultrasonic option off and propagate cold
    # reset for the documented PPU power-up behavior.
    out['nes.sv']=out['nes.sv'].replace('.rst_behavior     (ppu_rst_behavior),',
        '.rst_behavior     (ppu_rst_behavior),\n\t.cold_reset(cold_reset),').replace(
        '.audio_channels (audio_channels),', '.audio_channels (audio_channels),\n\t.allow_us(1\'b0),')
    # Missing optional square-2 input is a known source-wrapper omission.
    out['apu.sv']=out['apu.sv'].replace('.sq2          (1\'b1),',
        '.sq2          (1\'b1),\n\t\t.allow_us(allow_us),')
    # Master-derived enables remain stable between native master ticks. The
    # outer CE gate prevents a high CPU/PPU CE from running multiple times.
    for key,text in out.items():
        text=strict_declarations(text)
        # Explicit variables for procedural outputs; SystemVerilog logic also
        # permits the original single-driver continuous assignments.
        text=re.sub(r'\bwire\b','logic',text)
        text=re.sub(r'\boutput[ \t]+(?!reg\b|logic\b)(?=\[|[A-Za-z_])','output logic ',text)
        out[key]='import regs_savestates::*;\n'+'\n'.join(line.rstrip() for line in text.splitlines())+'\n'
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');args=p.parse_args()
    TARGET.mkdir(parents=True,exist_ok=True)
    failed=[]
    for name,text in outputs().items():
        path=TARGET/name
        if args.check:
            if not path.exists() or path.read_text()!=text: failed.append(name)
        else:path.write_text(text,encoding='utf-8',newline='\n')
    if failed: raise SystemExit('Stale NES ports: '+', '.join(failed))
    print('NES deterministic source ports verified' if args.check else 'NES source ports generated')

if __name__=='__main__':main()
