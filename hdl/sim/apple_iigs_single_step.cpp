// SPDX-License-Identifier: GPL-3.0-only
// Functional register/RAM oracle is pinned SingleStepTests, not the RTL.
#include "VP65C816.h"
#include "VP65C816___024root.h"
#include "verilated.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static uint32_t read32(FILE *file) {
    uint32_t value=0;
    if(std::fread(&value,4,1,file)!=1) { std::fprintf(stderr,"Truncated vectors\n");std::exit(2); }
    return value;
}
struct State { uint32_t pc,s,p,a,x,y,dbr,d,pbr,e; };
static State read_state(FILE *f) {
    State s;auto *p=reinterpret_cast<uint32_t*>(&s);
    for(unsigned i=0;i<10;i++)p[i]=read32(f);
    return s;
}
static std::vector<uint32_t> read_ram(FILE *f,std::vector<uint8_t>&ram,bool load) {
    unsigned count=read32(f);std::vector<uint32_t> addresses;
    for(unsigned i=0;i<count;i++) {
        uint32_t address=read32(f),data=read32(f);addresses.push_back(address);addresses.push_back(data);
        if(load)ram[address]=data;
    }
    return addresses;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *f=std::fopen(argv[1],"rb");if(!f)return 2;
    VerilatedContext context;
    VP65C816 cpu(&context);auto *r=cpu.rootp;
    cpu.CE=1;cpu.RDY_IN=1;cpu.IRQ_N=1;cpu.NMI_N=1;cpu.ABORT_N=1;
    std::vector<uint8_t> ram(1<<24,0);std::vector<uint32_t> touched;
    unsigned count=read32(f),expected_boundaries=read32(f),failures=0,passed=0,known_boundaries=0,groups[512]={0},fields[10]={0},ram_failures=0;
    for(unsigned test=0;test<count;test++) {
        uint32_t opcode=read32(f),index=read32(f),boundary_kind=read32(f);
        State initial=read_state(f),expected=read_state(f);
        for(auto address:touched)ram[address]=0;touched.clear();
        auto initial_ram=read_ram(f,ram,true),expected_ram=read_ram(f,ram,false);
        unsigned expected_cycles=read32(f);
        State wdc_expected{};std::vector<uint32_t> wdc_ram,wdc_writes,writes,reads;
        if(boundary_kind) {
            // Only the Python runner may issue these tags, after validating
            // the pinned raw file and complete named-vector SHA256 hashes.
            wdc_expected=read_state(f);
            wdc_ram=read_ram(f,ram,false);wdc_writes=read_ram(f,ram,false);
        }
        for(unsigned i=0;i<initial_ram.size();i+=2)touched.push_back(initial_ram[i]);
        cpu.CLK=0;cpu.RST_N=0;cpu.eval();cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();
        cpu.RST_N=1;
        r->P65C816__DOT__A=initial.a;r->P65C816__DOT__X=initial.x;r->P65C816__DOT__Y=initial.y;
        // The input suite permits arbitrary S high bytes when E=1, but a
        // running 65816 has the hard-wired emulation stack page $01.
        const uint16_t stack=initial.e ? 0x100|(initial.s&255) : initial.s;
        r->P65C816__DOT__SP=stack;r->P65C816__DOT__SPW=stack;
        r->P65C816__DOT__P=initial.p|(initial.e<<8);r->P65C816__DOT__oldXF=(initial.p>>4)&1;
        r->P65C816__DOT__PBR=initial.pbr;r->P65C816__DOT__DBR=initial.dbr;r->P65C816__DOT__D=initial.d;
        r->P65C816__DOT__AddrGen__DOT__PCr=initial.pc;
        r->P65C816__DOT__GotInterrupt=0;r->P65C816__DOT__IsResetInterrupt=0;
        cpu.eval();
        bool completed=false;
        const bool move=opcode==0x44 || opcode==0x54;
        for(unsigned cycle=0;cycle<(move?expected_cycles:64);cycle++) {
            cpu.D_IN=ram[cpu.A_OUT];cpu.eval();
            if(!cpu.WE && (cpu.VDA || cpu.VPA)) {
                ram[cpu.A_OUT]=cpu.D_OUT;touched.push_back(cpu.A_OUT);
                if(boundary_kind){writes.push_back(cpu.A_OUT);writes.push_back(cpu.D_OUT);}
            } else if(boundary_kind && cpu.WE && (cpu.VDA || cpu.VPA))reads.push_back(cpu.A_OUT);
            cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();
            if(move) {if(cycle+1==expected_cycles){completed=true;break;}}
            else if(r->P65C816__DOT__STATE==0 || r->P65C816__DOT__WAIExec || r->P65C816__DOT__STPExec) {completed=true;break;}
        }
        State actual{r->P65C816__DOT__AddrGen__DOT__PCr,r->P65C816__DOT__SP,r->P65C816__DOT__P&255,
            r->P65C816__DOT__A,r->P65C816__DOT__X,r->P65C816__DOT__Y,r->P65C816__DOT__DBR,
            r->P65C816__DOT__D,r->P65C816__DOT__PBR,r->P65C816__DOT__P>>8};
        bool okay=completed;
        auto *a=reinterpret_cast<uint32_t*>(&actual),*e=reinterpret_cast<uint32_t*>(&expected);
        for(unsigned field=0;field<10;field++)if(a[field]!=e[field]){okay=false;fields[field]++;}
        for(unsigned i=0;i<expected_ram.size();i+=2)if(ram[expected_ram[i]]!=expected_ram[i+1]){okay=false;ram_failures++;}
        bool known=false;
        if(!okay && completed && boundary_kind) {
            auto *wdc=reinterpret_cast<uint32_t*>(&wdc_expected);
            known=writes==wdc_writes;
            for(unsigned field=0;field<10;field++)if(a[field]!=wdc[field])known=false;
            for(unsigned i=0;i<wdc_ram.size();i+=2)if(ram[wdc_ram[i]]!=wdc_ram[i+1])known=false;
            if(boundary_kind==1) {
                // WDC page50 §7.1 extended stack: exact two-byte write
                // sequence at $0100 then $00FF, architectural S=$01FE.
                if(opcode!=0xfc || !initial.e || (initial.s&255)!=0 || actual.s!=0x1fe ||
                   writes!=std::vector<uint32_t>{0x100,((initial.pc+2)>>8)&255,0xff,(initial.pc+2)&255})known=false;
            } else if(boundary_kind==2) {
                // WDC page50 §7.2.2: exact named pointer read wraps from
                // $F4FF to $F400. $F500 must not supply the high byte.
                if(opcode!=0xe1 || !initial.e || index!=8668 || initial.d!=0xf400 || initial.x!=0x4f ||
                   std::find(reads.begin(),reads.end(),0xf4ff)==reads.end() ||
                   std::find(reads.begin(),reads.end(),0xf400)==reads.end() ||
                   std::find(reads.begin(),reads.end(),0xf500)!=reads.end())known=false;
            } else known=false;
        }
        if(okay && !boundary_kind)passed++;
        else if(known) {
            known_boundaries++;
            std::printf("BOUNDARY %02x %c %u kind=%s exact WDC registers/RAM/writes\n",opcode,initial.e?'e':'n',index+1,
                boundary_kind==1?"extended_stack":"direct_page_pointer");
        }
        else {
            failures++;
            if(++groups[opcode|(initial.e<<8)]<=2) {
                std::printf("FAIL opcode=%02x mode=%s vector=%u expected_cycles=%u\n",opcode,initial.e?"emulation":"native",index,expected_cycles);
                const char *names[]={"PC","SP","P","A","X","Y","DBR","D","PBR","E"};
                for(unsigned field=0;field<10;field++)if(a[field]!=e[field])std::printf("  %s actual=%x expected=%x\n",names[field],a[field],e[field]);
                for(unsigned i=0;i<expected_ram.size();i+=2)if(ram[expected_ram[i]]!=expected_ram[i+1])std::printf("  RAM[%06x] actual=%02x expected=%02x\n",expected_ram[i],ram[expected_ram[i]],expected_ram[i+1]);
            }
        }
    }
    if(known_boundaries!=expected_boundaries) {
        std::printf("FAIL documented boundary count actual=%u expected=%u\n",known_boundaries,expected_boundaries);failures++;
    }
    std::printf("SingleStep65816: %u exact golden passes, %u unexpected failures, %u total (final registers and RAM)\n",passed,failures,count);
    std::printf("Documented WDC boundary disagreements: %u (separate from exact golden passes)\n",known_boundaries);
    for(unsigned group=0;group<512;group++)if(groups[group])std::printf("Failures opcode=%02x mode=%s count=%u\n",group&255,group&256?"emulation":"native",groups[group]);
    for(unsigned field=0;field<10;field++)if(fields[field])std::printf("Field failure[%u]=%u\n",field,fields[field]);
    if(ram_failures)std::printf("RAM mismatches=%u\n",ram_failures);
    std::fflush(stdout);std::fclose(f);return failures?1:0;
}
