#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the actual top-level staging-RAM decode/AR gate with the real AXI slave."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/amiga_cdc_model.sv',
 'hdl/common/multitini_async_mailbox.sv','hdl/common/multitini_axi3_regs.sv',
 'hdl/core/amiga/multitini_amiga_ide_block.sv','hdl/core/amiga/multitini_amiga_diagnostics.sv','hdl/core/amiga/multitini_amiga_host.sv',
 'hdl/sim/tb_amiga_ide_block_axi.sv']
TOP='hdl/board/multitini_one_top.sv'
def top_slice(mutate):
    text=(ROOT/TOP).read_text();parts=[]
    for name in ('uci_buffer_wr_select','amiga_host_rd_select','amiga_host_wr_select',
                 'amiga_block_rd_select','amiga_block_wr_select','uci_read_port_free'):
        match=re.search(r'    wire '+name+r'=.*?;',text,re.S)
        if not match:raise RuntimeError('Owning top declaration missing:'+name)
        parts.append(match.group(0))
    gate=re.search(r'    assign m_axi_arready=.*?;',text).group(0)
    valid=re.search(r'\.s_axi_arvalid\((m_axi_arvalid[^)]*)\)',text).group(1)
    ram_addr=re.search(r'\.host_block_ram_read_address\((.*?)\)',text).group(1)
    if 'reg_rd_data = amiga_host_rd_select ? amiga_host_status : amiga_block_rd_select ? amiga_block_rdata :' not in text:
        raise RuntimeError('Owning top read mux changed; review this slice')
    if mutate:parts[-1]='wire uci_read_port_free=1\'b1;'
    return '''// Generated from current production top expressions; not a parallel register implementation.
module amiga_ide_block_top_slice(
 input wire control_clk,clk,resetn,input wire[31:0]awaddr,wdata,araddr,
 input wire awvalid,wvalid,bready,arvalid,rready,
 output wire awready,wready,bvalid,arready,rvalid,output wire[1:0]bresp,rresp,output wire[31:0]rdata);
 localparam ACTIVE_MACHINE=7;
 wire reg_wr_en,reg_rd_en,reg_rd_early_en;wire[15:0]reg_wr_addr,reg_rd_addr,reg_rd_early_addr;
 wire[31:0]reg_wr_data,reg_rd_data,amiga_host_status,amiga_block_rdata;wire[3:0]reg_wr_strb;
 wire m_axi_arvalid=arvalid;wire m_axi_arready,axi_arready;
'''+'\n'.join(parts)+'\n'+gate+'''
 assign arready=m_axi_arready;
 assign reg_rd_data=amiga_host_rd_select ? amiga_host_status : amiga_block_rd_select ? amiga_block_rdata:32'd0;
 multitini_axi3_regs regs_i(.aclk(control_clk),.aresetn(resetn),
 .s_axi_awid(12'd0),.s_axi_awaddr(awaddr),.s_axi_awlen(4'd0),.s_axi_awsize(3'd2),.s_axi_awburst(2'd1),
 .s_axi_awlock(2'd0),.s_axi_awcache(4'd0),.s_axi_awprot(3'd0),.s_axi_awqos(4'd0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
 .s_axi_wid(12'd0),.s_axi_wdata(wdata),.s_axi_wstrb(4'hf),.s_axi_wlast(1'b1),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
 .s_axi_bid(),.s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
 .s_axi_arid(12'd0),.s_axi_araddr(araddr),.s_axi_arlen(4'd0),.s_axi_arsize(3'd2),.s_axi_arburst(2'd1),
 .s_axi_arlock(2'd0),.s_axi_arcache(4'd0),.s_axi_arprot(3'd0),.s_axi_arqos(4'd0),
 .s_axi_arvalid('''+valid+'''),.s_axi_arready(axi_arready),
 .s_axi_rid(),.s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rlast(),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
 .reg_wr_en(reg_wr_en),.reg_wr_addr(reg_wr_addr),.reg_wr_data(reg_wr_data),.reg_wr_strb(reg_wr_strb),
 .reg_rd_en(reg_rd_en),.reg_rd_addr(reg_rd_addr),.reg_rd_early_en(reg_rd_early_en),.reg_rd_early_addr(reg_rd_early_addr),.reg_rd_data(reg_rd_data));
 multitini_amiga_host host_i(.control_clk(control_clk),.control_resetn(resetn),
 .write(reg_wr_en&&amiga_host_wr_select),.command(reg_wr_data),.status(amiga_host_status),
 .block_write(reg_wr_en&&amiga_block_wr_select),.block_read(reg_rd_en&&amiga_block_rd_select),
 .block_address(reg_wr_addr),.block_read_address(reg_rd_addr),.block_ram_read_address('''+ram_addr+'''),
 .block_wstrb(reg_wr_strb),.block_rdata(amiga_block_rdata),.clk(clk),.reset(!resetn),.guest_reset(1'b0),.chip_tick(resetn),
 .io_fpga(),.io_uio(),.io_strobe(),.io_din(),.io_wait(1'b0),.io_dout(16'd0),
 .ide_req(6'd0),.ide_address(),.ide_read(),.ide_write(),.ide_wdata(),.ide_rdata(16'd0));
 endmodule
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/amiga-ide-block-axi-tests')
    p.add_argument('--mutation',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    tracked=SOURCES+[TOP,'scripts/test_amiga_ide_block_axi.py']
    def hashes():return {n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}
    before=hashes();wrapper=out/'production_top_slice.sv';wrapper.write_text(top_slice(a.mutation))
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    def tool(n):return str(runtime/(n+'.exe')) if os.name=='nt' else n
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    for stage,command in [('compile',[tool('iverilog'),'-g2012','-s','tb_amiga_ide_block_axi','-o',str(out/'axi.vvp'),
                         *[str(ROOT/n) for n in SOURCES],str(wrapper)]),('simulate',[tool('vvp'),str(out/'axi.vvp')])]:
        r=subprocess.run(guard+command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        log=r.stdout+r.stderr;(out/(stage+'.log')).write_text(log);print(log[-10000:],flush=True)
        if r.returncode:
            if not(a.mutation and stage=='simulate' and 'AXI stage read collision' in log):return r.returncode
            break
    else:
        if a.mutation:raise RuntimeError('Shared RAM collision mutation escaped')
        if 'PASS AMIGA BULK IDE AXI' not in log:raise RuntimeError('AXI fixture did not finish')
    if before!=hashes():raise RuntimeError('Inputs changed during AXI qualification')
    (out/'validation.json').write_text(json.dumps({'status':'EXPECTED_FAILURE_REPRODUCED' if a.mutation else 'PASS',
        'source_sha256':before,'generated_top_slice_sha256':hashlib.sha256(wrapper.read_bytes()).hexdigest(),
        'board_evidence':False},indent=2)+'\n');return 0
if __name__=='__main__':raise SystemExit(main())
