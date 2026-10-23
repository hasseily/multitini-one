// SPDX-License-Identifier: GPL-3.0-only
// Isolated VICE 3.10 REU reference adapter. Generated method bodies are unchanged.
// VICE reu.c: GPL-2.0-or-later, authors preserved in the pinned source.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#define DEBUG_LOG(level,args) ((void)0)
#define BITS_ARE_ALL_SET(x,mask) (((x)&(mask))==(mask))
#define BITS_ARE_ALL_UNSET(x,mask) (((x)&(mask))==0)
using log_t=int;
#define LOG_DEFAULT 0
using reu_ba_check_callback_t=int(void);
using reu_ba_steal_callback_t=void(void);
using CLOCK=uint64_t;
static CLOCK maincpu_clk=0;
static int reu_enabled=0,reu_size_kb=0;
static unsigned int reu_size=0;
static int irq=0,cpu_ba=0;
static struct {int io_source_valid;} reu_io2_device;
static FILE *trace=nullptr,*clocks=nullptr;
static uint8_t c64[65536];
static int ba_start=-1,ba_end=-1,case_id=0;
static int check_ba(){return int(maincpu_clk)>=ba_start&&int(maincpu_clk)<ba_end;}
static void steal_ba(){while(check_ba())++maincpu_clk;}
static void maincpu_set_irq(unsigned int,int value){irq=value;}
static void machine_handle_pending_alarms(int){}
static void log_message(int,const char*,...){}
static uint8_t mem_dma_read(uint16_t addr){auto v=c64[addr];if(trace)fprintf(trace,"C R %06x %02x\n",addr,v);if(clocks)fprintf(clocks,"%d,%llu,R,%04x,%02x\n",case_id,(unsigned long long)maincpu_clk,addr,v);return v;}
static void mem_dma_store(uint16_t addr,uint8_t v){c64[addr]=v;if(trace)fprintf(trace,"C W %06x %02x\n",addr,v);if(clocks)fprintf(clocks,"%d,%llu,W,%04x,%02x\n",case_id,(unsigned long long)maincpu_clk,addr,v);}
int reu_dma(int);
void reu_dma_start(void);
#include "vice_reu_declarations.inc"
static int reu_activate(){return 0;}
static int reu_deactivate(){return 0;}
#include "vice_reu_methods.inc"
static void initialize(int bits){
    reu_enabled=0;set_reu_size(1<<(bits-10),nullptr);reu_enabled=1;
    reu_ram=new uint8_t[reu_size];reu_ba_register(check_ba,steal_ba,&cpu_ba,1);
}
static void reset_case(){
    reu_reset();irq=cpu_ba=0;maincpu_clk=0;ba_start=ba_end=-1;
    for(unsigned i=0;i<65536;i++)c64[i]=uint8_t(i^(i>>8)^0x5a);
    for(unsigned i=0;i<reu_size;i++)reu_ram[i]=uint8_t(i^(i>>8)^(i>>16)^0xa5);
}
static void drain(){if(cpu_ba&1){cpu_ba&=~1;reu_dma_start();}}
int main(int argc,char**argv){try{
    if(argc==3&&std::string(argv[1])=="timing"){
        initialize(24);clocks=fopen(argv[2],"w");if(!clocks)throw std::runtime_error("timing output");
        fprintf(clocks,"case,cycle,access,address,data\n");
        for(int op=0;op<4;op++)for(int length: {2,6}){
            reset_case();case_id=op*10+length;ba_start=1;ba_end=4;
            // Identical data for verify: controlled BA schedule is the only variable.
            for(int i=0;i<length;i++)reu_ram[0x10000+i]=c64[0x2000+i];
            rec.base_computer=0x2000;rec.base_reu=0;rec.bank_reu=1;rec.transfer_length=length;rec.command=0x90|op;
            reu_dma(1);drain();fprintf(clocks,"%d,%llu,END,0000,%02x\n",case_id,(unsigned long long)maincpu_clk,rec.status);
        }
        // No BA stealing: isolate early-error hold and the unclocked final
        // peek after a penultimate error. Case = 100 + length*10 + mismatch.
        for(int length: {2,3,5})for(int mismatch=0;mismatch<length;mismatch++){
            reset_case();case_id=100+length*10+mismatch;
            for(int i=0;i<length;i++)reu_ram[0x10000+i]=c64[0x2000+i]^(i==mismatch?1:0);
            rec.base_computer=0x2000;rec.base_reu=0;rec.bank_reu=1;rec.transfer_length=length;rec.command=0x93;
            reu_dma(1);drain();fprintf(clocks,"%d,%llu,END,0000,%02x\n",case_id,(unsigned long long)maincpu_clk,rec.status);
        }
        fflush(clocks);if(ferror(clocks))throw std::runtime_error("timing write");fclose(clocks);return 0;
    }
    if(argc!=5)throw std::runtime_error("args: bits vectors trace clocks");
    initialize(atoi(argv[1]));std::ifstream in(argv[2]);trace=fopen(argv[3],"w");clocks=fopen(argv[4],"w");if(!in||!trace||!clocks)throw std::runtime_error("streams");
    unsigned op,a,b;
    while(in>>std::dec>>op>>std::hex>>a>>b){switch(op){
      case 0: reset_case();case_id=a;fprintf(trace,"E %u\n",a);break;
      case 1: reu_io2_store(a,b);break;
      case 2: {auto value=reu_io2_read(a);fprintf(trace,"R %02x %02x %x\n",a,value,irq);break;}
      case 3: reu_dma(-1);break;
      case 4: drain();fprintf(trace,"S %x %x\n",irq,cpu_ba&1);break;
      case 5: c64[a&65535]=b;break;
      case 6: reu_ram[a&(reu_size-1)]=b;break;
      case 7: fprintf(trace,"Q %06x %02x\n",a,reu_ram[a&(reu_size-1)]);break;
      default:throw std::runtime_error("unknown operation");
    }}
    if(!in.eof())throw std::runtime_error("bad input");fflush(trace);fflush(clocks);if(ferror(trace)||ferror(clocks))throw std::runtime_error("write failed");fclose(trace);fclose(clocks);delete[]reu_ram;return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
