// SPDX-License-Identifier: GPL-3.0-only
// Actual P65 CPU executes the assembled ROM; MMIO is an independent protocol model.
#include "VP65C816.h"
#include "VP65C816___024root.h"
#include "verilated.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
struct Case {
    std::string name;
    unsigned cmd=0,unit=1,selector=0,count=3,error=0,bytes=0;
    uint32_t params=0x3400,buffer=0x9000,lba=0x345678,capacity=0x12345678;
    unsigned stack=0x1f0,caller=0x2000,i=0;
    bool mounted=true,readonly=false,pointer_bad=false,buffer_bad=false,control_bad=false,legacy=false,boot=false;
};
static uint8_t pattern(uint32_t lba,unsigned unit,unsigned i) {return uint8_t((lba^(unit*37))+i*13);}
int main(int argc,char**argv) {
    try {
        require(argc==2,"usage: rom-tests hdd.rom");
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)),{});
        require(rom.size()==2304,"assembled ROM size");
        std::vector<Case> cases;
        auto add=[&](std::string n,unsigned cmd,unsigned bytes=0,unsigned error=0){Case c;c.name=n;c.cmd=cmd;c.bytes=bytes;c.error=error;cases.push_back(c);};
        for(unsigned ext:{0U,0x40U})for(unsigned unit=1;unit<=8;unit++) {
            add("status-"+std::to_string(ext)+"-"+std::to_string(unit),ext,ext?5:4);cases.back().unit=unit;
            add("dib-"+std::to_string(ext)+"-"+std::to_string(unit),ext,ext?26:25);cases.back().unit=unit;cases.back().selector=3;
            add("read-"+std::to_string(ext)+"-"+std::to_string(unit),ext|1,512);cases.back().unit=unit;
            add("write-"+std::to_string(ext)+"-"+std::to_string(unit),ext|2);cases.back().unit=unit;
        }
        for(unsigned ext:{0U,0x40U}) {
            add("host-status",ext,8);cases.back().unit=0;
            add("empty-drive-status",ext,ext?5:4);cases.back().mounted=false;
            add("empty-drive-dib",ext,ext?26:25);cases.back().mounted=false;cases.back().selector=3;
            add("readonly-dib",ext,ext?26:25);cases.back().readonly=true;cases.back().selector=3;
            add("unsupported-status",ext,0,0x21);cases.back().selector=1;
            add("bad-count",ext,0,4);cases.back().count=2;
            add("unassigned-unit",ext,0,0x28);cases.back().unit=9;
            add("controller-read",ext|1,0,0x11);cases.back().unit=0;
            add("offline-read",ext|1,0,0x2f);cases.back().mounted=false;
            add("protected-write",ext|2,0,0x2b);cases.back().readonly=true;
            add("bad-block",ext|1,0,0x2d);cases.back().capacity=1;
            add("init-controller",ext|5);cases.back().count=1;cases.back().unit=0;
            add("init-unit",ext|5,0,0x11);cases.back().count=1;
            add("format-preformatted",ext|3);cases.back().count=1;
            add("format-protected",ext|3,0,0x2b);cases.back().count=1;cases.back().readonly=true;
            add("format-offline",ext|3,0,0x2f);cases.back().count=1;cases.back().mounted=false;
            add("control-reset-device",ext|4);
            add("control-reset-controller",ext|4);cases.back().unit=0;
            add("control-bad-list",ext|4,0,0x22);cases.back().control_bad=true;
            add("control-unsupported",ext|4,0,0x21);cases.back().selector=1;
            add("control-no-interrupt",ext|4,0,0x1f);cases.back().selector=3;
            for(unsigned op=6;op<=9;op++) {add("character-command-rejected",ext|op,0,1);cases.back().count=op<8?1:4;}
        }
        add("extended-high-list-byte",0x40,0,0x22);cases.back().pointer_bad=true;
        add("extended-high-buffer-byte",0x41,0,0x22);cases.back().buffer_bad=true;
        add("extended-address-overflow-read",0x41,0,0x22);cases.back().buffer=0xffff00;
        add("extended-address-overflow-write",0x42,0,0x22);cases.back().buffer=0xfffe01;
        add("standard-address-overflow",1,0,0x22);cases.back().buffer=0xfe01;
        add("extended-last-byte-boundary",0x41,512);cases.back().buffer=0xfffe00;
        for(unsigned ext:{0U,0x40U}) {
            uint32_t end=ext?0xffffff:0xffff;
            add("status-address-overflow",ext,0,0x22);cases.back().buffer=end;
            add("dib-address-overflow",ext,0,0x22);cases.back().buffer=end-(ext?24:23);cases.back().selector=3;
            add("host-address-overflow",ext,0,0x22);cases.back().buffer=end-6;cases.back().unit=0;
            add("control-address-overflow",ext|4,0,0x22);cases.back().buffer=end;
            add("status-exact-end",ext,ext?5:4);cases.back().buffer=end-(ext?4:3);
            add("dib-exact-end",ext,ext?26:25);cases.back().buffer=end-(ext?25:24);cases.back().selector=3;
            add("host-exact-end",ext,8);cases.back().buffer=end-7;cases.back().unit=0;
            add("control-exact-end",ext|4);cases.back().buffer=end-1;
            add("status-list-overflow",ext,0,0x22);cases.back().params=end-(ext?5:3);
            add("read-list-overflow",ext|1,0,0x22);cases.back().params=end-(ext?8:5);
            add("status-list-exact-end",ext,ext?5:4);cases.back().params=end-(ext?6:4);
            add("read-list-exact-end",ext|1,512);cases.back().params=end-(ext?9:6);
            add("format-list-exact-end",ext|3);cases.back().params=end-1;cases.back().count=1;
        }
        add("unknown-command",0x80,0,1);
        add("banked-list-buffer-boundaries",0x41,512);cases.back().params=0x02fffd;cases.back().buffer=0x12ff00;cases.back().lba=0xa5234567;cases.back().capacity=0xffffffff;
        add("banked-write-boundaries",0x42);cases.back().params=0x03fffc;cases.back().buffer=0x12ff00;cases.back().lba=0xa5234567;cases.back().capacity=0xffffffff;
        add("extended-status-bank-boundary",0x40,26);cases.back().params=0x03fffe;cases.back().buffer=0x11fff8;cases.back().selector=3;
        add("inline-page-boundary",0x41,512);cases.back().caller=0x20fc;
        add("inline-bank-zero-wrap",0x40,5);cases.back().caller=0xfffc;
        for(unsigned stack:{0x100U,0x101U,0x11fU,0x1ffU})for(unsigned i:{0U,4U}) {
            add("stack-wrap-I-preservation",0x40,5);cases.back().stack=stack;cases.back().i=i;
        }
        for(unsigned op=0;op<4;op++)for(unsigned drive=0;drive<2;drive++) {
            add("legacy-ProDOS",op);auto&c=cases.back();c.legacy=true;c.unit=drive?0xf0:0x70;c.lba=0x3456;
            if(op==0)c.bytes=0xffff;
        }
        add("legacy-offline",1,0,0x2f);cases.back().legacy=true;cases.back().unit=0x70;cases.back().mounted=false;
        add("boot-disk",1);cases.back().boot=true;
        add("boot-fallback",1);cases.back().boot=true;cases.back().mounted=false;
        unsigned passed=0,total_dma=0,total_peak_stack=0;
        VerilatedContext context;VP65C816 cpu(&context);auto*r=cpu.rootp;
        for(const auto&c:cases) {
            std::vector<uint8_t> ram(1<<24,0x5a);std::copy(rom.begin(),rom.end(),ram.begin()+0xc700);
            std::array<uint8_t,16> regs{};unsigned busy=0,dma=0,hold=0,writes=0,rom_fetch=0,control_reads=0,reg_writes=0;
            uint32_t observed_buffer=0,observed_lba=0;unsigned observed_unit=0;
            auto flags=[&](){return uint8_t((c.mounted?1:0)|(c.readonly?2:0)|0x10);}; // only one mounted, host still enumerates all8
            auto put16=[&](uint32_t a,unsigned v){ram[a&0xffffff]=v;ram[(a+1)&0xffffff]=v>>8;};
            auto put32=[&](uint32_t a,uint32_t v){for(unsigned i=0;i<4;i++)ram[(a+i)&0xffffff]=v>>(8*i);};
            uint32_t p=c.params,b=c.buffer;
            ram[p]=c.count;ram[(p+1)&0xffffff]=c.unit;put16(p+2,b);
            unsigned off=4;
            if(c.cmd&0x40){ram[(p+4)&0xffffff]=b>>16;ram[(p+5)&0xffffff]=c.buffer_bad?1:0;off=6;}
            unsigned op=c.cmd&0x3f;
            if(op==1||op==2)put32(p+off,c.lba);
            else ram[(p+off)&0xffffff]=c.selector;
            if(op==4){ram[b]=c.control_bad?1:0;ram[(b+1)&0xffffff]=0;}
            if(op==2)for(unsigned i=0;i<512;i++)ram[(b+i)&0xffffff]=pattern(c.lba,c.legacy?1+(c.unit>>7):c.unit,i);
            std::array<uint8_t,32> saved{};for(unsigned i=0;i<32;i++)saved[i]=ram[0x60+i]=uint8_t(i*17+7);
            ram[0x42]=c.cmd;ram[0x43]=c.unit;put16(0x44,b);put16(0x46,c.lba);
            unsigned pc=c.caller,end=(pc+(c.legacy?3:(c.cmd&0x40)?8:6))&65535;
            ram[pc]=0x20;put16(pc+1,c.legacy?0xc70a:0xc70d);
            if(!c.legacy){ram[(pc+3)&65535]=c.cmd;put16((pc+4)&65535,p);if(c.cmd&0x40){ram[(pc+6)&65535]=p>>16;ram[(pc+7)&65535]=c.pointer_bad?1:0;}}
            ram[end]=0xdb;
            std::array<uint8_t,32> wrapped{};std::copy_n(ram.begin(),32,wrapped.begin());
            cpu.CLK=0;cpu.RST_N=0;cpu.CE=1;cpu.RDY_IN=1;cpu.IRQ_N=1;cpu.NMI_N=1;cpu.ABORT_N=1;cpu.eval();cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();cpu.RST_N=1;
            r->P65C816__DOT__A=0xa53c;r->P65C816__DOT__X=0x35;r->P65C816__DOT__Y=0x47;
            r->P65C816__DOT__SP=c.stack;r->P65C816__DOT__SPW=c.stack;r->P65C816__DOT__P=0x130|c.i;
            r->P65C816__DOT__oldXF=1;r->P65C816__DOT__PBR=0;r->P65C816__DOT__DBR=0;r->P65C816__DOT__D=0;
            r->P65C816__DOT__AddrGen__DOT__PCr=c.boot?0xc700:pc;r->P65C816__DOT__GotInterrupt=0;r->P65C816__DOT__IsResetInterrupt=0;cpu.eval();
            bool done=false;unsigned peak_stack=0;
            for(unsigned cycle=0;cycle<100000;cycle++) {
                // Measure from before the caller JSR, including its two bytes,
                // the saved P/B/scratch and every nested helper return frame.
                unsigned depth=(c.stack-r->P65C816__DOT__SP)&255;
                peak_stack=std::max(peak_stack,depth);
                require(depth<=35,c.name+" exceeds35-byte total stack allowance: "+std::to_string(depth));
                if(hold){cpu.CE=0;hold--;cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();continue;}cpu.CE=1;
                const uint32_t a=cpu.A_OUT&0xffffff;uint8_t data=ram[a];
                if(cpu.WE&&(cpu.VPA||cpu.VDA)) {
                    if(op==4&&cpu.VDA&&!cpu.VPA&&(a==b||a==((b+1)&0xffffff)))control_reads++;
                    if(cpu.VPA&&cpu.VDA&&((!c.boot&&a==end)||(c.boot&&a==(c.mounted?0x801:0xc600)))){done=true;break;}
                    if(cpu.VPA&&a>=0xc800&&a<=0xcfff)rom_fetch++;
                    if(a>=0xc0f0&&a<=0xc0ff) {
                        unsigned reg=a&15;
                        if(reg==15)data=flags();
                        else if(reg>=11)data=uint8_t((c.mounted?c.capacity:0)>>(8*(reg-11)));
                        else if(reg==9)data=uint8_t(c.capacity);else if(reg==10)data=uint8_t(c.capacity>>8);
                        else if(reg==0) {
                            observed_unit=(regs[2]&0x80)?regs[3]:1+(regs[3]>>7);
                            observed_buffer=regs[4]|(regs[5]<<8)|(regs[8]<<16);
                            observed_lba=regs[6]|(regs[7]<<8)|(uint32_t(regs[9])<<16)|(uint32_t(regs[10])<<24);
                            unsigned command=regs[2]&0x3f;uint8_t error=0;
                            if(observed_unit<1||observed_unit>8)error=0x28;else if(!c.mounted)error=0x2f;
                            else if(command==2&&c.readonly)error=0x2b;else if(command&&command<3&&observed_lba>=c.capacity)error=0x2d;
                            if(command==1||command==2){dma++;hold=17;total_dma++;}
                            if(!error&&command==1){for(unsigned i=0;i<512;i++)ram[(observed_buffer+i)&0xffffff]=pattern(observed_lba,observed_unit,i);if(c.boot){ram[0x800]=1;ram[0x801]=0xdb;}}
                            if(!error&&command==2){for(unsigned i=0;i<512;i++)require(ram[(observed_buffer+i)&0xffffff]==pattern(observed_lba,observed_unit,i),c.name+" DMA write byte");writes++;}
                            regs[1]=error;busy=3;data=0; // initiating F0 intentionally returns stale success
                        } else if(reg==1){data=busy?0x80:regs[1];if(busy)busy--;}
                        else data=regs[reg];
                    }
                }
                cpu.D_IN=data;cpu.eval();
                if(!cpu.WE&&(cpu.VPA||cpu.VDA)) {if(a>=0xc0f0&&a<=0xc0ff){regs[a&15]=cpu.D_OUT;reg_writes++;}else if(a<0xc700||a>0xcfff)ram[a]=cpu.D_OUT;}
                cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();
            }
            require(done,c.name+" timeout PC="+std::to_string(r->P65C816__DOT__AddrGen__DOT__PCr));
            require(rom_fetch>0,c.name+" never executed expansion ROM");
            if(!c.boot) {
                require((r->P65C816__DOT__A&255)==c.error,c.name+" wrong A error actual="+std::to_string(r->P65C816__DOT__A&255));
                require(bool(r->P65C816__DOT__P&1)==bool(c.error),c.name+" wrong C");
                require((r->P65C816__DOT__P&0x10c)==(0x100|c.i),c.name+" E/I/D changed");
                require((r->P65C816__DOT__A>>8)==0xa5,c.name+" B changed");
                require(r->P65C816__DOT__SP==c.stack,c.name+" SP changed");
                require(r->P65C816__DOT__D==0&&r->P65C816__DOT__DBR==0,c.name+" D/DBR changed");
                require((r->P65C816__DOT__X|(r->P65C816__DOT__Y<<8))==c.bytes,c.name+" wrong X/Y count");
                if(c.pointer_bad||c.buffer_bad)require(dma==0&&ram[b]==0x5a,c.name+" invalid pointer touched payload memory");
                if(c.name.find("address-overflow")!=std::string::npos)require(dma==0,c.name+" overflow launched DMA");
                if(c.name.find("list-overflow")!=std::string::npos)require(dma==0&&reg_writes==0,c.name+" bad list staged controller registers");
                if(c.name=="control-address-overflow")require(control_reads==0,c.name+" invalid reset list was read");
                if(c.name.find("address-overflow")!=std::string::npos&&op==0)for(unsigned i=0;i<32;i++)require(ram[i]==wrapped[i],c.name+" status wrote wrapped bank0 bytes");
                if(!c.legacy&&op!=1&&op!=2)require(dma==0&&writes==0,c.name+" non-data call launched DMA");
                if(!c.error&&op==2)require(writes==1,c.name+" write did not transfer exactly once");
                for(unsigned i=0;i<32;i++)require(ram[0x60+i]==saved[i],c.name+" scratch not restored");
                if(!c.error&&op==1){require(dma==1,c.name+" DMA count");require(observed_buffer==b&&observed_lba==c.lba,c.name+" truncated DMA address");for(unsigned i=0;i<512;i++)require(ram[(b+i)&0xffffff]==pattern(c.lba,c.legacy?1+(c.unit>>7):c.unit,i),c.name+" read byte");}
                if(!c.error&&!c.legacy&&op==0) {
                    if(c.unit==0){require(ram[b]==8,c.name+" host must enumerate8 includingholes");for(unsigned i=1;i<8;i++)require(ram[b+i]==0,c.name+" reserved host byte");}
                    else {uint8_t general=c.readonly?0xa4:0xe8;if(c.mounted)general|=0x10;require(ram[b]==general,c.name+" general status");uint32_t capacity=c.mounted?c.capacity:0;if(!(c.cmd&0x40))capacity=std::min(capacity,0xffffffU);for(unsigned i=0;i<((c.cmd&0x40)?4:3);i++)require(ram[(b+1+i)&0xffffff]==uint8_t(capacity>>(i*8)),c.name+" geometry");if(c.selector==3){unsigned off=(c.cmd&0x40)?5:4;require(ram[(b+off)&0xffffff]==13,c.name+" DIB ID length");for(unsigned i=0;i<16;i++)require(ram[(b+off+1+i)&0xffffff]==uint8_t("Multitini HDD   "[i]),c.name+" DIB name");require(ram[(b+off+17)&0xffffff]==2&&ram[(b+off+18)&0xffffff]==0x80&&ram[(b+off+19)&0xffffff]==0&&ram[(b+off+20)&0xffffff]==1,c.name+" DIB type/extended subtype/version");}}
                }
            }
            total_peak_stack=std::max(total_peak_stack,peak_stack);
            passed++;std::printf("PASS %s stack_peak=%u\n",c.name.c_str(),peak_stack);
        }
        require(total_peak_stack==35,"expected measured peak35 including caller/helper frames");
        std::printf("SmartPort stack: peak%u bytes including caller/helper frames PASS\n",total_peak_stack);
        std::printf("SmartPort ROM: %u exact cases, %u DMA initiations PASS\n",passed,total_dma);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
