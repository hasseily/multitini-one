// SPDX-License-Identifier: GPL-3.0-only
// Device-framework shim for the verbatim/generated BSD-3-Clause MAME methods.
// No opcode, ALU, interrupt sampling or RDY rule is implemented in this shim.
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using u16 = uint16_t;
using u32 = uint32_t;
using offs_t = uint32_t;
constexpr int CLEAR_LINE = 0, ASSERT_LINE = 1;
#define BIT(value, bit) (((value) >> (bit)) & 1)

struct callback_stub {
    void operator()(int) {}
    bool isunset() const { return true; }
};

class m6502_device {
public:
    enum { IRQ_LINE, APU_IRQ_LINE, NMI_LINE, V_LINE, RDY_LINE, STATE_RESET = 0xff00 };
    enum { F_N=0x80,F_V=0x40,F_E=0x20,F_T=0x20,F_B=0x10,F_D=8,F_I=4,F_Z=2,F_C=1 };
    uint16_t m_PPC{},m_NPC{},m_PC{},m_SP{},m_TMP{};
    uint8_t m_TMP2{},m_A{},m_X{},m_Y{},m_P{},m_IR{};
    int m_inst_state_base{},m_inst_state{},m_inst_substate{},m_icount{},m_count_before_instruction_step{};
    uint32_t m_XPC{};
    bool m_nmi_state{},m_irq_state{},m_apu_irq_state{},m_v_state{},m_rdy_state{};
    bool m_nmi_pending{},m_irq_taken{},m_sync{},m_inhibit_interrupts{},m_irq_sampled{},m_nmi_sampled{};
    callback_stub m_sync_w, m_write_port;
    uint8_t m_pullup{},m_floating{},m_dir{},m_port{},m_drive{},m_falloff_mask{};
    uint32_t m_falloff_cycles[8]{};
    uint64_t m_falloff_deadline[8]{};
    uint8_t external_port=0xa0;
    uint64_t cycle{};
    std::array<uint8_t,65536> ram{};
    FILE *trace=nullptr;
    uint16_t start=0x0800,stop_address=0x0204;
    uint8_t stop_data=0xa5;
    bool tracing=false,done=false;
    unsigned sequence{};

    m6502_device() {
#include "mame_initialization.inc"
        reset_port();
    }
    uint64_t total_cycles() const { return cycle; }
    uint8_t m_read_port() const { return external_port; }
    bool debugger_enabled() const { return false; }
    void debugger_instruction_hook(offs_t) {}
    void debugger_wait_hook() {}
    void standard_irq_callback(int,uint16_t) {}
    bool access_to_be_redone() const { return false; }
    offs_t pc_to_external(u16 pc) { return pc; }
#include "mame_inline.inc"
    void record(uint16_t address,char direction,uint8_t data) {
        if (direction=='R' && m_sync && address==start) tracing=true;
        if (tracing) std::fprintf(trace,"%u,%04x,%c,%02x,%llu,%d,%02x\n",sequence++,address,direction,data,
                                  static_cast<unsigned long long>(cycle),m_sync?1:0,get_port());
        if (tracing && direction=='W' && address==stop_address && data==stop_data) done=true;
    }
    uint8_t read(uint16_t address) {
        uint8_t value=ram[address];
        if(address==0) value=dir_r();
        else if(address==1) value=port_r();
        record(address,'R',value);
        return value;
    }
    uint8_t read_arg(uint16_t address) { return read(address); }
    uint8_t read_sync(uint16_t address) { return read(address); }
    void write(uint16_t address,uint8_t value) {
        ram[address]=value;
        record(address,'W',value);
        if(address==0) dir_w(value);
        else if(address==1) port_w(value);
    }
    void do_exec_full();
    void do_exec_partial();
#include "mame_declarations.inc"
};

#include "mame_methods.inc"
#include "mame_opcodes.inc"

int port_vectors(const char *path) {
    m6502_device cpu;
    cpu.set_pulls(0x17,0xc8);
    cpu.set_floating_falloff(0xc0,350000);
    cpu.external_port=0x17;
    FILE *out=std::fopen(path,"wb");
    if(!out) throw std::runtime_error("cannot create port vectors");
    struct stimulus { uint64_t cycle; unsigned operation,value; };
    // operation: 0 reset, 1 DDR write, 2 latch write, 3 external pins, 4 sample.
    // Deadline edges, data writes while input, re-driving/releasing and warm
    // reset use actual m6510 methods. No expected port behavior is coded here.
    const stimulus inputs[] = {
        {0,0,0},{1,1,0x00},{2,2,0x00},{350000,4,0},{350001,4,0},
        {350002,1,0x08},{350003,1,0x00},{350004,2,0xff},{350005,4,0},
        {350010,1,0xc8},{350011,1,0x08},{350012,1,0x00},
        {700010,4,0},{700011,4,0},{700012,4,0},
        {700020,1,0xc0},{700021,2,0x80},{700022,1,0x00},
        {1050021,4,0},{1050022,4,0},
        {1050030,1,0xc0},{1050031,2,0xc0},{1050032,1,0x80},
        {1050040,1,0x00},{1400031,4,0},{1400032,4,0},{1400039,4,0},{1400040,4,0},
        {1400050,3,0x00},{1400051,4,0},{1400052,3,0xff},{1400053,4,0},
        {1400060,0,0},{1400061,4,0},{1400062,3,0x17},{1400063,1,0x2f},
        {1400064,2,0x15},{1400065,4,0},{1750062,4,0},{1750063,4,0},
        {1750070,1,0xff},{1750071,2,0x00},{1750072,1,0x00},{2100072,4,0}
    };
    for(const auto &input:inputs) {
        cpu.cycle=input.cycle;
        switch(input.operation) {
            case 0:cpu.reset_port();break;
            case 1:cpu.dir_w(uint8_t(input.value));break;
            case 2:cpu.port_w(uint8_t(input.value));break;
            case 3:cpu.external_port=uint8_t(input.value);break;
            default:break;
        }
        std::fprintf(out,"%llu %u %02x %02x %02x %02x %02x\n",
            static_cast<unsigned long long>(cpu.cycle),input.operation,input.value,
            cpu.external_port,cpu.dir_r(),cpu.port_r(),cpu.get_port());
    }
    std::fclose(out);
    std::cout<<"MAME C64 port vectors: "<<std::size(inputs)<<" events\n";
    return 0;
}

int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--port-vectors") return port_vectors(argv[2]);
        if(argc<8 || argc>9) throw std::runtime_error("image trace start stop_address stop_data port_input max_cycles [events]");
        m6502_device cpu;
        std::ifstream image(argv[1],std::ios::binary);
        image.read(reinterpret_cast<char *>(cpu.ram.data()),cpu.ram.size());
        if(image.gcount()!=65536 || image.peek()!=EOF) throw std::runtime_error("image must be exactly 65536 bytes");
        cpu.trace=std::fopen(argv[2],"wb");
        if(!cpu.trace) throw std::runtime_error("cannot create trace");
        std::fprintf(cpu.trace,"sequence,address,rw,data,cycle,sync,port\n");
        cpu.start=std::stoul(argv[3],nullptr,16);cpu.stop_address=std::stoul(argv[4],nullptr,16);
        cpu.stop_data=std::stoul(argv[5],nullptr,16);cpu.external_port=std::stoul(argv[6],nullptr,16);
        uint64_t limit=std::stoull(argv[7]);
        struct event { uint64_t cycle; int ready,irq,nmi; };
        std::vector<event> events;
        if(argc==9) {
            std::ifstream input(argv[8]);
            if(!input) throw std::runtime_error("cannot read events");
            event row{};
            while(input>>row.cycle>>row.ready>>row.irq>>row.nmi) events.push_back(row);
            if(!input.eof()) throw std::runtime_error("malformed event row");
        }
        size_t next=0;
        for(;cpu.cycle<limit && !cpu.done;cpu.cycle++) {
            while(next<events.size() && events[next].cycle==cpu.cycle) {
                cpu.execute_set_input(cpu.RDY_LINE,events[next].ready);
                cpu.execute_set_input(cpu.IRQ_LINE,events[next].irq);
                cpu.execute_set_input(cpu.NMI_LINE,events[next].nmi);
                next++;
            }
            cpu.m_icount=1;
            cpu.execute_run();
        }
        std::fclose(cpu.trace);
        if(!cpu.done) throw std::runtime_error("sentinel write not reached before cycle limit");
        std::cout<<"MAME6510 trace: "<<cpu.sequence<<" accesses, "<<cpu.cycle<<" cycles\n";
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<"\n";return 1; }
}
