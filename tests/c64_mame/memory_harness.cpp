// SPDX-License-Identifier: GPL-3.0-only
// Execute the original MAME C64 handlers conditional on supplied PLA outputs.
// Device doubles report calls; they do not emulate VIC/SID/CIA/cartridge state.
// The required Commodore PLA fuse dump is deliberately not supplied here.
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using offs_t = uint32_t;
#define BIT(value, bit) (((value) >> (bit)) & 1)
#define A15 BIT(offset, 15)
#define A14 BIT(offset, 14)
#define A13 BIT(offset, 13)
#define A12 BIT(offset, 12)
#define VA13 BIT(va, 13)
#define VA12 BIT(va, 12)

struct device_double {
    uint8_t value=0xa5;
    bool patterned=false;
    unsigned address_mask=0xffff;
    uint8_t floating=0xa5;
    uint16_t last_address{};
    uint8_t last_data{};
    unsigned reads{},writes{};
    int phi0_r() const { return 1; }
    uint8_t bus_r() const { return patterned ? floating : value; }
    uint8_t read(offs_t address) { reads++;last_address=address;return patterned ? uint8_t(value^(address&address_mask)) : value; }
    void write(offs_t address,uint8_t data) { writes++;last_address=address;last_data=data; }
};
struct ram_double {
    std::array<uint8_t,65536> bytes{};
    uint8_t *pointer() { return bytes.data(); }
};
struct pla_double {
    uint8_t output=0xff;
    uint16_t last_input{};
    uint8_t read(uint32_t input) {last_input=input;return output;}
};
struct expansion_double {
    unsigned reads{},writes{};
    int roml{},romh{},io1{},io2{};
    uint8_t data{};
    int game=1,exrom=1,mode=0;
    bool patterned=false;
    int game_r(offs_t,int,int,int,int,int) const {return game;}
};
class c64_state {
public:
    enum { PLA_OUT_CASRAM,PLA_OUT_BASIC,PLA_OUT_KERNAL,PLA_OUT_CHAROM,
           PLA_OUT_GRW,PLA_OUT_IO,PLA_OUT_ROML,PLA_OUT_ROMH };
    device_double vic,sid,cia1,cia2;
    ram_double ram;
    pla_double pla;
    expansion_double expansion;
    device_double *m_vic=&vic,*m_cia1=&cia1,*m_cia2=&cia2;
    ram_double *m_ram=&ram;
    pla_double *m_pla=&pla;
    expansion_double *m_exp=&expansion;
    std::array<uint8_t,8192> basic{},kernal{};
    std::array<uint8_t,4096> character{};
    std::array<uint8_t,1024> m_color_ram{};
    uint8_t *m_basic=basic.data(),*m_kernal=kernal.data(),*m_charom=character.data();
    int m_loram=1,m_hiram=1,m_charen=1,m_va14=1,m_va15=1;
    int exp_exrom_r(offs_t,int,int,int) { return expansion.exrom; }
    uint8_t sid_r(offs_t address) { return sid.read(address); }
    void sid_w(offs_t address,uint8_t data) {sid.write(address,data);}
    uint8_t exp_cd_r(offs_t address,uint8_t data,int,int,int roml,int romh,int io1,int io2) {
        expansion.reads++;expansion.data=data;expansion.roml=roml;expansion.romh=romh;
        expansion.io1=io1;expansion.io2=io2;
        if(expansion.patterned) {
            if(!roml || !romh) return uint8_t(0x96^(address&0x1fff)^((address&0x1fff)>>8));
            if((!io1 || !io2) && expansion.mode) return uint8_t((expansion.mode==1?0x69:0x3c)^(address&0xff));
        }
        return data;
    }
    void exp_cd_w(offs_t,uint8_t data,int,int,int roml,int romh,int io1,int io2) {
        expansion.writes++;expansion.data=data;expansion.roml=roml;expansion.romh=romh;
        expansion.io1=io1;expansion.io2=io2;
    }
    c64_state() {
        for(unsigned i=0;i<65536;i++)ram.bytes[i]=uint8_t((i>>8)^i);
        basic.fill(0xb1);kernal.fill(0xe2);character.fill(0xc3);m_color_ram.fill(0x0b);
        sid.value=0x54;cia1.value=0xc1;cia2.value=0xc2;
    }
    int read_pla(offs_t offset,offs_t va,int rw,int aec,int ba);
    uint8_t read_memory(offs_t offset,offs_t va,int aec,int ba);
    void write_memory(offs_t offset,uint8_t data,int aec,int ba);
};
#include "mame_memory.inc"

// Batch mode compares the actual handlers with HDL observations. PLA output is
// explicitly supplied by the caller from DUT decode observations, NOT a MAME
// fuse-map oracle. No machine ROM or PLA dump is required or imported.
static int batch(const char *input,const char *output) {
    std::ifstream in(input);
    std::ofstream out(output);
    if(!in || !out) throw std::runtime_error("cannot open conditional handler batch files");
    out<<"id,result,ram_after,color_after,pla_input,vic_reads,vic_writes,sid_reads,sid_writes,cia1_reads,cia1_writes,cia2_reads,cia2_writes,roml,romh,io1,io2,exp_data\n";
    unsigned id,owner,write,address,vicaddr,bank,game,exrom,ba,floating,data,mode,pla,count=0;
    while(in>>id>>owner>>write>>std::hex>>address>>vicaddr>>bank>>game>>exrom>>ba>>floating>>data>>mode>>pla>>std::dec) {
        c64_state s;
        s.m_loram=bank&1;s.m_hiram=(bank>>1)&1;s.m_charen=(bank>>2)&1;
        s.m_va14=!BIT(vicaddr,14);s.m_va15=!BIT(vicaddr,15);
        s.expansion.game=game;s.expansion.exrom=exrom;s.expansion.mode=mode;s.expansion.patterned=true;
        s.pla.output=pla;s.vic.floating=floating;
        s.vic.patterned=s.sid.patterned=s.cia1.patterned=s.cia2.patterned=true;
        s.vic.value=0xa5;s.vic.address_mask=0x3f;s.sid.address_mask=0x1f;
        s.cia1.address_mask=s.cia2.address_mask=0xf;
        for(unsigned i=0;i<s.basic.size();i++) {s.basic[i]=uint8_t(0xb1^i^(i>>8));s.kernal[i]=uint8_t(0xe2^i^(i>>8));}
        for(unsigned i=0;i<s.character.size();i++) s.character[i]=uint8_t(0xc3^i^(i>>8));
        for(unsigned i=0;i<s.m_color_ram.size();i++)s.m_color_ram[i]=uint8_t((0xb^i^(i>>4))&15);
        int result=-1;
        // CPU VA is undriven high on reads and zero in the MAME write handler.
        // VIC offset is its 14-bit bus; bank selection is supplied separately.
        unsigned offset=owner?address:(vicaddr&0x3fff);
        unsigned va=owner?0x3fff:(vicaddr&0x3fff);
        if(write)s.write_memory(offset,data,owner,ba);
        else result=s.read_memory(offset,va,owner,ba);
        out<<id<<','<<result<<','<<unsigned(s.ram.bytes[address])<<','<<unsigned(s.m_color_ram[address&0x3ff])<<','<<s.pla.last_input
           <<','<<s.vic.reads<<','<<s.vic.writes<<','<<s.sid.reads<<','<<s.sid.writes<<','<<s.cia1.reads<<','<<s.cia1.writes<<','<<s.cia2.reads<<','<<s.cia2.writes
           <<','<<s.expansion.roml<<','<<s.expansion.romh<<','<<s.expansion.io1<<','<<s.expansion.io2<<','<<unsigned(s.expansion.data)<<'\n';
        count++;
    }
    if(!in.eof() || count==0)throw std::runtime_error("malformed or empty conditional handler input");
    out.flush();
    if(!out)throw std::runtime_error("conditional handler output write failed");
    std::cout<<"MAME conditional handlers: "<<count<<" DUT-derived PLA vectors executed\n";
    return 0;
}

int main(int argc,char **argv) {
    try {
        if(argc==4 && std::string(argv[1])=="--batch-dut-pla")return batch(argv[2],argv[3]);
        if(argc!=2) throw std::runtime_error("output.csv argument required");
        FILE *out=std::fopen(argv[1],"wb");
        if(!out) throw std::runtime_error("cannot create memory-handler vectors");
        std::fprintf(out,"case,address,va,aec,ba,va14,va15,pla_output,rw,input,result,ram_after,color_after,pla_input,vic_reads,vic_writes,sid_reads,sid_writes,cia1_reads,cia1_writes,cia2_reads,cia2_writes,exp_reads,exp_writes,exp_data,roml,romh,io1,io2\n");
        unsigned count=0;
        auto run=[&](const std::string &name,uint16_t address,uint16_t va,int aec,int ba,int va14,int va15,
                     uint8_t select,char rw,uint8_t input,int expected_result,int expected_ram=-1,int expected_color=-1) {
            c64_state state;
            state.m_va14=va14;state.m_va15=va15;state.pla.output=select;
            int result=-1;
            if(rw=='R')result=state.read_memory(address,va,aec,ba);
            else state.write_memory(address,input,aec,ba);
            if(result!=expected_result || (expected_ram>=0 && state.ram.bytes[address]!=expected_ram) ||
               (expected_color>=0 && state.m_color_ram[address&0x3ff]!=expected_color))
                throw std::runtime_error("MAME handler fixture failed: "+name);
            std::fprintf(out,"%s,%04x,%04x,%d,%d,%d,%d,%02x,%c,%02x,%d,%02x,%02x,%04x,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%02x,%d,%d,%d,%d\n",
                name.c_str(),address,va,aec,ba,va14,va15,select,rw,input,result,state.ram.bytes[address],
                state.m_color_ram[address&0x3ff],state.pla.last_input,state.vic.reads,state.vic.writes,
                state.sid.reads,state.sid.writes,state.cia1.reads,state.cia1.writes,state.cia2.reads,state.cia2.writes,
                state.expansion.reads,state.expansion.writes,state.expansion.data,state.expansion.roml,state.expansion.romh,
                state.expansion.io1,state.expansion.io2);
            count++;
        };
        run("cpu_ram_read",0x2356,0x3fff,1,1,1,1,0xfe,'R',0,0x75);
        run("cpu_ram_write",0x2356,0,1,1,1,1,0xfe,'W',0x6d,-1,0x6d);
        run("unselected_read_open_bus",0x9000,0x3fff,1,1,1,1,0xff,'R',0,0xa5);
        run("unselected_write",0x2356,0,1,1,1,1,0xff,'W',0x6d,-1,0x75);
        run("port0_underlying_ram",0,0,1,1,1,1,0xfe,'W',0x2f,-1,0xa5);
        run("port1_underlying_ram",1,0,1,1,1,1,0xfe,'W',0x15,-1,0xa5);
        run("basic_read",0xa123,0x3fff,1,1,1,1,0xfd,'R',0,0xb1);
        run("kernal_read",0xe123,0x3fff,1,1,1,1,0xfb,'R',0,0xe2);
        run("character_read",0xd123,0x3fff,1,1,1,1,0xf7,'R',0,0xc3);
        run("rom_priority_over_ram",0xa123,0x3fff,1,1,1,1,0xfc,'R',0,0xb1);
        run("ram_write_under_basic",0xa123,0,1,1,1,1,0xfc,'W',0x6d,-1,0x6d);
        for(int bank=0;bank<4;bank++)
            run("vic_ram_bank"+std::to_string(bank),0x0213,0x0213,0,0,!(bank&1),!(bank&2),0xfe,'R',0,
                uint8_t(((bank<<14)|0x0213)>>8)^0x13);
        for(int page=0;page<16;page++) {
            uint16_t address=0xd000|(page<<8)|0x25;
            int expected=page<4?0xa5:page<8?0x54:page<12?0xab:page==12?0xc1:page==13?0xc2:0xa5;
            run("io_read_page"+std::to_string(page),address,0x3fff,1,1,1,1,0xdf,'R',0,expected);
            run("io_write_page"+std::to_string(page),address,0,1,1,1,1,0xcf,'W',0x6d,-1,
                uint8_t((address>>8)^address),(page>=8&&page<12)?0xd:0xb);
        }
        run("color_write_without_grw",0xd825,0,1,1,1,1,0xdf,'W',0x6d,-1,0xfd,0xb);
        run("expansion_roml_select",0x8000,0x3fff,1,1,1,1,0xbf,'R',0,0xa5);
        run("expansion_romh_select",0xe000,0x3fff,1,1,1,1,0x7f,'R',0,0xa5);
        std::fclose(out);
        std::cout<<"MAME C64 handler vectors: "<<count<<" passed (supplied PLA outputs; peripheral doubles)\n";
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<"\n";return 1;}
}
