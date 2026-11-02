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

// Native GS/OS-style PEA/PHA/XCE/RTS entry, followed by a standard STATUS
// inline list. The ROM01 IRQ-return sequence releases C800, then accesses
// the slot recorded at $07F8. This model separates that bus ownership from
// the CPU: executing an unowned expansion byte is recorded before inventing
// any particular floating-bus opcode or subsequent crash signature.
static bool irq_reselect_case(VP65C816&cpu,const std::vector<uint8_t>&rom,unsigned saved_slot,bool entry_gap) {
    auto*r=cpu.rootp;
    std::vector<uint8_t> ram(1<<24,0xea);std::copy(rom.begin(),rom.end(),ram.begin()+0xc700);
    auto put16=[&](unsigned a,unsigned v){ram[a]=uint8_t(v);ram[a+1]=uint8_t(v>>8);};
    const std::array<uint8_t,5> epilogue{0x28,0xd8,0xc9,0x01,0x60};
    auto found=std::search(rom.begin(),rom.end(),epilogue.begin(),epilogue.end());
    require(found!=rom.end(),"IRQ fixture cannot find PLP/CLD/CMP/RTS epilogue");
    unsigned after_plp=0xc700+unsigned(found-rom.begin())+1;
    const bool protected_handoff=found<rom.begin()+256;
    // Locate the existing C7 SmartPort handoff, without pinning its padding.
    const std::array<uint8_t,4> jump{0x28,0x4c,0x00,0xc8};
    auto handoff=std::search(rom.begin(),rom.begin()+256,jump.begin(),jump.end());
    const std::array<uint8_t,15> protected_jump{0x08,0x78,0x48,0xa9,0xc7,0x8d,0xf8,0x07,0x68,0x2c,0xff,0xcf,0x4c,0x00,0xc8};
    auto protected_entry=std::search(rom.begin(),rom.begin()+256,protected_jump.begin(),protected_jump.end());
    require(protected_handoff ? protected_entry!=rom.begin()+256:handoff!=rom.begin()+256,"IRQ fixture cannot find C7 handoff");
    const unsigned trigger=entry_gap ? 0xc700+unsigned((protected_handoff?protected_entry:handoff)-rom.begin())+(protected_handoff?12:1):after_plp;
    const unsigned start=0xae00,ret=0xae15,end=0xae1b;
    const std::array<uint8_t,11> capsule{0xf4,0x15,0xae,0xa9,0x0c,0xc7,0x48,0x38,0xfb,0xa9,0x3c};
    std::copy(capsule.begin(),capsule.end(),ram.begin()+start);
    ram[ret]=0x60;ram[ret+1]=0;put16(ret+2,0xac1d);ram[ret+4]=0xea;ram[ret+5]=0xea;ram[end]=0xdb;
    ram[0xac1d]=3;ram[0xac1e]=0;put16(0xac1f,0xac40);ram[0xac21]=0;
    ram[0x7f8]=uint8_t(saved_slot);
    // The real capsule's still-pending software-owned restoration tuple.
    const std::array<uint8_t,6> saved{0x30,0x00,0x84,0x00,0xbd,0xe0};
    std::copy(saved.begin(),saved.end(),ram.begin()+0x1f6);
    put16(0xfffe,0x4000);
    // Preserve full A/X/Y/D/DBR in native mode; reselect exactly as ROM01
    // does after BIT CFFF, then return through the original emulation frame.
    const std::vector<uint8_t> handler{
        0x18,0xfb,0xc2,0x30,0x48,0xda,0x5a,0x0b,0x8b,0xe2,0x20,
        0xad,0xf8,0x07,0xeb,0xa9,0x00,0xaa,0x2c,0xff,0xcf,0xbd,0x00,0x00,
        0xc2,0x20,0xab,0x2b,0x7a,0xfa,0x68,0x38,0xfb,0x40};
    std::copy(handler.begin(),handler.end(),ram.begin()+0x4000);
    cpu.CLK=0;cpu.RST_N=0;cpu.CE=1;cpu.RDY_IN=1;cpu.IRQ_N=1;cpu.NMI_N=1;cpu.ABORT_N=1;
    cpu.eval();cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();cpu.RST_N=1;
    r->P65C816__DOT__A=0x1234;r->P65C816__DOT__X=0xc7;r->P65C816__DOT__Y=0;
    r->P65C816__DOT__SP=0x1f5;r->P65C816__DOT__SPW=0x1f5;r->P65C816__DOT__P=0x10;
    r->P65C816__DOT__oldXF=1;r->P65C816__DOT__PBR=0;r->P65C816__DOT__DBR=0;r->P65C816__DOT__D=0;
    r->P65C816__DOT__AddrGen__DOT__PCr=start;r->P65C816__DOT__GotInterrupt=0;r->P65C816__DOT__IsResetInterrupt=0;cpu.eval();
    bool internal_c8=true,owner=false,injected=false,irq_seen=false,irq_returned=false,done=false,lost=false;
    unsigned irq_entries=0,release_reads=0,reselect_reads=0,loss_pc=0,irq_return_pc=0;
    std::array<uint8_t,16> regs{};
    for(unsigned cycle=0;cycle<10000;cycle++) {
        const unsigned a=cpu.A_OUT&0xffffff;uint8_t data=ram[a];
        if(cpu.WE&&(cpu.VPA||cpu.VDA)) {
            const bool fetch=cpu.VPA&&cpu.VDA&&r->P65C816__DOT__STATE==0;
            if(fetch&&a==trigger&&!injected) {
                require(bool(r->P65C816__DOT__P&4)==(protected_handoff&&entry_gap),"IRQ fixture trigger mask state differs");
                require(r->P65C816__DOT__D==0&&r->P65C816__DOT__DBR==0&&(r->P65C816__DOT__P&0x130)==0x130,
                    "IRQ fixture violates SmartPort E/D/DBR/M/X entry state");
                injected=true;cpu.IRQ_N=0;
            }
            if(fetch&&a==0x4000) {
                cpu.IRQ_N=1;irq_seen=true;irq_entries++;
                const unsigned s=r->P65C816__DOT__SP;
                irq_return_pc=ram[0x100+((s+2)&255)]|(unsigned(ram[0x100+((s+3)&255)])<<8);
            }
            if(a==0xcfff) {data=0xc0;internal_c8=false;owner=false;if(irq_seen)release_reads++;}
            else if(a>=0xc100&&a<0xc800) {
                owner=(a>>8)==0xc7;
                if((a>>8)==0xc3)internal_c8=true;
                if(irq_seen&&!cpu.VPA)reselect_reads++;
            }
            if(fetch&&a>=0xc800&&a<0xcfff&&(!owner||internal_c8)) {
                require(irq_seen,"C800 masked before the injected IRQ");lost=true;loss_pc=a;break;
            }
            if(irq_seen&&fetch&&a==irq_return_pc)irq_returned=true;
            if(fetch&&a==end) {done=true;break;}
            if(a>=0xc0f0&&a<=0xc0ff) {
                const unsigned reg=a&15;
                data=reg==15 ? 0x11:reg>=11 ? uint8_t(0x12345678U>>(8*(reg-11))):regs[reg];
            }
        }
        cpu.D_IN=data;cpu.eval();
        if(!cpu.WE&&(cpu.VPA||cpu.VDA)) {
            if(a>=0xc0f0&&a<=0xc0ff)regs[a&15]=cpu.D_OUT;
            else if(a<0xc700||a>0xcfff)ram[a]=cpu.D_OUT;
        }
        cpu.CLK=1;cpu.eval();cpu.CLK=0;cpu.eval();
    }
    require(injected&&irq_seen&&irq_entries==1&&release_reads==1,"IRQ fixture did not execute one interrupt/CFFF release");
    require((protected_handoff||saved_slot!=0) ? reselect_reads==1:reselect_reads==0,"IRQ fixture did not read the effective saved slot");
    const unsigned word=ram[0x1f4]|(unsigned(ram[0x1f5])<<8);
    if(protected_handoff||saved_slot==0xc7) {
        require(done&&irq_returned&&!lost,"saved C7 IRQ control did not return to the caller");
        require((r->P65C816__DOT__A&0xffff)==0xc700&&r->P65C816__DOT__X==8&&r->P65C816__DOT__Y==0,
            "saved C7 IRQ control result registers differ");
        require(r->P65C816__DOT__SP==0x1f5&&!(r->P65C816__DOT__P&9),"saved C7 IRQ control SP/C/D differ");
        if(protected_handoff)require(ram[0x7f8]==0xc7&&irq_return_pc<0xc800,"protected call did not set MSLOT or accepted IRQ inside C800");
    } else require(lost&&!done,"different saved slot did not expose C800 ownership loss");
    std::printf("IRQ_RESELECT window=%s saved_slot=%02X effective_MSLOT=%02X protected_C7=%u result=%s irq_return=%04X loss_pc=%04X A=%04X X=%04X Y=%04X S=%04X P=%03X pending_return=%04X CFFF=%u slot_reads=%u\n",
        entry_gap?"entry":"epilogue",saved_slot,ram[0x7f8],unsigned(protected_handoff),lost?"C800_LOST":"CALLER_RETURN",irq_return_pc,loss_pc,
        r->P65C816__DOT__A,r->P65C816__DOT__X,r->P65C816__DOT__Y,r->P65C816__DOT__SP,r->P65C816__DOT__P,word,release_reads,reselect_reads);
    return lost;
}
int main(int argc,char**argv) {
    try {
        require(argc==2,"usage: rom-tests hdd.rom");
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)),{});
        require(rom.size()==2304,"assembled ROM size");
        const bool protected_handoff=rom[0x11]==0x78;
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
            bool done=false,internal_c8=true,entry_seen=false;unsigned peak_stack=0,cfff_reads=0;
            std::array<unsigned,5> entry_state{};
            auto caller_state=[&](){return std::array<unsigned,5>{r->P65C816__DOT__A,r->P65C816__DOT__P,r->P65C816__DOT__X,r->P65C816__DOT__Y,r->P65C816__DOT__SP};};
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
                    if(cpu.VPA&&cpu.VDA&&a==(c.boot?(protected_handoff?0xc70a:0xc757):c.legacy?0xc70a:0xc70d)&&!entry_seen) {
                        entry_state=caller_state();entry_seen=true;
                    }
                    // Firmware callers may arrive with the IIgs internal C800
                    // latch set. CFFF releases it after returning the old byte.
                    if(a==0xcfff) {data=0xc0;internal_c8=false;cfff_reads++;}
                    else if(cpu.VPA&&a>=0xc800&&a<0xcfff) {
                        require(!internal_c8,c.name+" entered masked C800 before releasing INTC8ROM");
                        if(!rom_fetch) {
                            require(entry_seen,c.name+" missed C7 entry");
                            if(protected_handoff) {
                                auto live=caller_state();
                                require(live[0]==entry_state[0]&&live[2]==entry_state[2]&&live[3]==entry_state[3],c.name+" protected C7 changed A/X/Y");
                                require((live[1]&0x104)==0x104&&live[4]==(0x100|((entry_state[4]-1)&255)),c.name+" protected C7 did not mask IRQ/save one P byte");
                                require(ram[0x100+((live[4]+1)&255)]==uint8_t(entry_state[1]),c.name+" protected C7 did not save original P");
                                require(ram[0x7f8]==0xc7,c.name+" protected C7 did not publish MSLOT");
                            } else require(caller_state()==entry_state,c.name+" C7 trampoline changed A/P/X/Y/SP");
                        }
                        rom_fetch++;
                    }
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
            require(cfff_reads>0,c.name+" did not release prelatched internal C800");
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
        std::printf("SmartPort ROM: %u exact cases, %u DMA initiations PASS\n",passed,total_dma);
        unsigned lost=0;for(bool entry:{false,true})for(unsigned slot:{0xc7U,0U,0xc3U})lost+=irq_reselect_case(cpu,rom,slot,entry);
        require(lost==(protected_handoff?0U:4U),"IRQ reselect characterization count differs");
        require(lost==0,"SmartPort IRQ return loses C800 when the previous MSLOT differs from C7");
        std::printf("SmartPort IRQ reselect: 6 cases, %u caller returns, %u C800 losses PASS\n",6-lost,lost);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
