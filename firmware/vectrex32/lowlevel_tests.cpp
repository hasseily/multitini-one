// SPDX-License-Identifier: GPL-3.0-only
#include "lowlevel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static void require(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Fixture {
    std::vector<uint8_t> bios=std::vector<uint8_t>(8192,0x12);
    bool bios_present=true;
    bool cancel_line=false;
    bool reenter_in_bios=false;
    unsigned reentry=0;
    mt_v32_lowlevel *reentrant_target=nullptr;
    const mt_v32_callbacks *callbacks=nullptr;
    mt_v32_lowlevel *created=nullptr;
    mt_v32_lowlevel_status nested_status=MT_V32_LL_OK;
    unsigned services=0,cancel_after=0;
    struct Line {int32_t x0,y0,x1,y1;uint8_t z;};
    std::vector<Line> lines;
    std::vector<unsigned> sounds;
    std::string order;
};
static void reenter(Fixture &f){
    unsigned action=f.reentry;f.reentry=0;
    const uint8_t nop=0x12;uint8_t value=0;
    switch(action){
        case 1:f.nested_status=mt_v32_lowlevel_execute(f.reentrant_target,&nop,1,100);break;
        case 2:mt_v32_lowlevel_destroy(f.reentrant_target);break;
        case 3:f.created=mt_v32_lowlevel_create(f.callbacks);break;
        case 4:mt_v32_lowlevel_reset(f.reentrant_target);break;
        case 5:f.nested_status=mt_v32_lowlevel_read_memory(f.reentrant_target,0xc800,&value,1);break;
    }
}
static int service(void *context){auto &f=*static_cast<Fixture*>(context);
    if(!f.reenter_in_bios)reenter(f);
    return ++f.services!=f.cancel_after;}
static int line(void *context,int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t z,uint8_t pattern){
    auto &f=*static_cast<Fixture*>(context);require(pattern==255,"VIA bit patterns must be represented by lit segments");
    f.lines.push_back({x0,y0,x1,y1,z});f.order+='L';return !f.cancel_line;
}
static int sound(void *context,unsigned reg,uint8_t value){auto &f=*static_cast<Fixture*>(context);f.sounds.push_back(reg*256+value);f.order+='P';return 1;}
static int bios(void *context,uint16_t address,uint8_t *data,size_t size){
    auto &f=*static_cast<Fixture*>(context);
    if(f.reenter_in_bios)reenter(f);
    if(!f.bios_present || address<0xe000 || size>65536u-address)return 0;
    std::memcpy(data,f.bios.data()+address-0xe000,size);return 1;
}
using Code=std::vector<uint8_t>;
static void imm_store(Code &code,uint8_t value,uint16_t address){code.insert(code.end(),{0x86,value,0xb7,uint8_t(address>>8),uint8_t(address)});}
static void wait_cycles(Code &code,unsigned count){code.insert(code.end(),(count+1)/2,0x12);}
static void psg_write(Code &code,uint8_t reg,uint8_t value){
    imm_store(code,reg,0xd001);imm_store(code,0x19,0xd000);imm_store(code,1,0xd000);
    imm_store(code,value,0xd001);imm_store(code,0x11,0xd000);imm_store(code,1,0xd000);
}
static mt_v32_lowlevel_status run(mt_v32_lowlevel *m,const Code &code,uint64_t budget=100000){return mt_v32_lowlevel_execute(m,code.data(),code.size(),budget);}
static void okay(mt_v32_lowlevel *m,const Code &code){auto status=run(m,code);if(status)std::fprintf(stderr,"%s\n",mt_v32_lowlevel_error(m));require(status==MT_V32_LL_OK,"bytecode execution");}
static uint8_t memory(mt_v32_lowlevel *m,uint16_t address){uint8_t value=0;require(mt_v32_lowlevel_read_memory(m,address,&value,1)==MT_V32_LL_OK,"memory read");return value;}
int main(int argc,char **argv){
    Fixture f;mt_v32_callbacks cb{};cb.context=&f;cb.service=service;cb.line=line;cb.psg=sound;cb.read_bios=bios;
    f.callbacks=&cb;
    auto *m=mt_v32_lowlevel_create(&cb);require(m!=nullptr,"context allocation");
    okay(m,{0x86,0x55,0xb7,0xc8,0x80});
    okay(m,{0x8b,3,0xb7,0xc8,0x81});
    require(memory(m,0xc880)==0x55 && memory(m,0xcc81)==0x58,"RAM/register persistence and RAM mirror");
    mt_v32_lowlevel_registers regs{};mt_v32_lowlevel_get_registers(m,&regs);
    require(regs.a==0x58 && regs.pc==10 && regs.s==0xcbea && regs.dp==0xd0,"fallthrough and register state");
    mt_v32_lowlevel_begin_frame(m);okay(m,{0x8b,1});
    mt_v32_lowlevel_get_registers(m,&regs);require(regs.a==0x59,"frame must preserve CPU registers");
    uint8_t data[16]={0xaa,0xbb};
    require(mt_v32_lowlevel_poke_dpram(m,1500,data,2)==MT_V32_LL_OK,"DPRAM poke");
    require(memory(m,0x0ddc)==0xaa,"DPRAM physical mirror");
    require(mt_v32_lowlevel_peek_dpram(m,1500,data,2)==MT_V32_LL_OK && data[1]==0xbb,"DPRAM peek");
    require(mt_v32_lowlevel_peek_dpram(m,2047,data,2)==MT_V32_LL_ARGUMENT,"DPRAM bounds");
    require(mt_v32_lowlevel_read_memory(m,0xffff,data,2)==MT_V32_LL_ARGUMENT,"CPU address wrap");
    require(mt_v32_lowlevel_read_memory(m,0xc800,data,17)==MT_V32_LL_ARGUMENT,"deferred Peek length limit");
    require(mt_v32_lowlevel_read_memory(m,0x8000,data,1)==MT_V32_LL_MEMORY,"unsupported map read is explicit");
    mt_v32_lowlevel_reset(m);
    f.bios[0x100]=0xc6;f.bios[0x101]=3;f.bios[0x102]=0x5c;f.bios[0x103]=0x39;
    okay(m,{0xbd,0xe1,0x00,0xf7,0xc8,0x82});
    mt_v32_lowlevel_get_registers(m,&regs);
    require(memory(m,0xc882)==4 && regs.s==0xcbea,"external BIOS code and nested return stack");
    f.bios_present=false;require(run(m,{0xbd,0xe1,0x00})==MT_V32_LL_MEMORY,"missing BIOS is explicit");f.bios_present=true;
    mt_v32_lowlevel_reset(m);
    Code timer;imm_store(timer,3,0xd004);imm_store(timer,0,0xd005);
    timer.insert(timer.end(),{0x86,0x40,0xb5,0xd0,0x0d,0x27,0xfb,0xb6,0xd0,0x00,0xb7,0xc8,0x83});
    okay(m,timer);require((memory(m,0xc883)&128)!=0,"Timer1 completion raises PB7");
    Code serial;imm_store(serial,0x96,0xd00a);wait_cycles(serial,40);okay(m,serial);
    require((memory(m,0xd00d)&4)!=0,"eight serial bits complete with IFR2");
    Code timer2;imm_store(timer2,1,0xd008);imm_store(timer2,0,0xd009);
    timer2.insert(timer2.end(),{0x86,0x20,0xb5,0xd0,0x0d,0x27,0xfb});okay(m,timer2);
    mt_v32_lowlevel_reset(m);f.lines.clear();f.sounds.clear();f.order.clear();
    mt_v32_lowlevel_pen pen{100,200,127,90,255};mt_v32_lowlevel_set_pen(m,&pen);
    Code drawing;imm_store(drawing,0,0xd00b);imm_store(drawing,0xee,0xd00c);
    imm_store(drawing,0,0xd001);imm_store(drawing,0x82,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,0x83,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,10,0xd001);imm_store(drawing,0x80,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,0x81,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,20,0xd001);imm_store(drawing,0x81,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,1,0xd000);wait_cycles(drawing,60);imm_store(drawing,0x81,0xd000);wait_cycles(drawing,30);
    imm_store(drawing,0xce,0xd00c);imm_store(drawing,0x98,0xd00b);
    psg_write(drawing,8,15);psg_write(drawing,13,9);psg_write(drawing,13,9);
    okay(m,drawing);mt_v32_lowlevel_get_pen(m,&pen);
    std::printf("Directed vector end=(%d,%d), segments=%zu\n",pen.x,pen.y,f.lines.size());
    require(pen.x>100 && pen.y>200 && (pen.x-100)==2*(pen.y-200),"VIA sample/hold and integrator slope");
    require(!f.lines.empty() && f.sounds.size()==3 && f.sounds[0]==8*256+15,"vector/PSG callbacks");
    require(f.order.find('L')<f.order.find('P'),"drawing callbacks precede subsequent PSG writes");
    require(f.sounds[1]==13*256+9 && f.sounds[2]==13*256+9,"repeated envelope writes are preserved");
    mt_v32_lowlevel_reset(m);f.lines.clear();f.sounds.clear();f.cancel_line=true;
    Code cancel_drawing;imm_store(cancel_drawing,0x80,0xd00b);imm_store(cancel_drawing,0xee,0xd00c);
    psg_write(cancel_drawing,8,15);
    require(run(m,cancel_drawing)==MT_V32_LL_CANCELLED && f.sounds.empty(),
            "line cancellation must prevent the following PSG callback");f.cancel_line=false;
    mt_v32_lowlevel_reset(m);
    int controls[2][6]={{80,-80,1,0,0,1},{0,0,0,0,0,0}};mt_v32_lowlevel_set_controls(m,controls);
    Code buttons;imm_store(buttons,14,0xd001);imm_store(buttons,0x19,0xd000);imm_store(buttons,1,0xd000);
    imm_store(buttons,0,0xd003);imm_store(buttons,9,0xd000);
    buttons.insert(buttons.end(),{0xb6,0xd0,0x01,0xb7,0xc8,0x84});okay(m,buttons);
    require(memory(m,0xc884)==0xf6,"AY8912 input returns both pressed buttons");
    mt_v32_lowlevel_reset(m);require(run(m,{0x20,0xfe},1000)==MT_V32_LL_BUDGET,"runaway budget");
    require(mt_v32_lowlevel_cycles(m)>=1000 && mt_v32_lowlevel_cycles(m)<1020,"budget overshoot is one instruction at most");
    mt_v32_lowlevel_reset(m);f.services=0;f.cancel_after=2;
    require(run(m,{0x20,0xfe},100000)==MT_V32_LL_CANCELLED,"periodic cancellation");
    require(mt_v32_lowlevel_cycles(m)<300,"cancellation service interval");f.cancel_after=0;
    mt_v32_lowlevel_reset(m);require(run(m,{2})==MT_V32_LL_OPCODE,"unsupported opcode is explicit");
    mt_v32_lowlevel_reset(m);require(run(m,{0x86,1,0xb7,0xe0,0})==MT_V32_LL_MEMORY,"BIOS writes rejected");
    mt_v32_lowlevel_reset(m);require(run(m,Code(2049,0x12))==MT_V32_LL_ARGUMENT,"code arena bounds");
    auto *other=mt_v32_lowlevel_create(&cb);mt_v32_lowlevel_reset(m);
    okay(m,{0x86,40});okay(other,{0x86,10});okay(m,{0x8b,2});
    mt_v32_lowlevel_get_registers(m,&regs);require(regs.a==42,"contexts preserve independent CPU registers");
    mt_v32_lowlevel_get_registers(other,&regs);require(regs.a==10,"second context remains independent");
    f.reentrant_target=m;
    for(unsigned action=1;action<=5;++action){
        mt_v32_lowlevel_reset(m);f.reenter_in_bios=false;f.reentry=action;
        require(run(m,{0x12})==MT_V32_LL_REENTRANT,"service callback re-entry must be rejected");
        require(f.created==nullptr,"callback must not create an uninitialized context");
        mt_v32_lowlevel_reset(m);f.reenter_in_bios=true;f.reentry=action;
        require(mt_v32_lowlevel_read_memory(m,0xe100,data,1)==MT_V32_LL_REENTRANT,
                "deferred BIOS Peek must guard callback context lifetime");
        require(f.created==nullptr,"BIOS callback must not create a context");
    }
    f.reenter_in_bios=false;f.reentrant_target=other;f.reentry=1;mt_v32_lowlevel_reset(m);
    require(run(m,{0x12})==MT_V32_LL_REENTRANT && f.nested_status==MT_V32_LL_REENTRANT,
            "cross-context callback execution must be rejected on both contexts");
    mt_v32_lowlevel_reset(m);okay(m,{0x86,42});
    mt_v32_lowlevel_get_registers(m,&regs);require(regs.a==42,"context remains usable after rejected destruction");
    mt_v32_lowlevel_destroy(other);
    if(argc==2){
        std::ifstream input(argv[1],std::ios::binary);f.bios.assign(std::istreambuf_iterator<char>(input),{});
        require(f.bios.size()==8192,"external test BIOS length");mt_v32_lowlevel_reset(m);f.lines.clear();
        // These are ordinary machine-code calls into the supplied BIOS, not
        // special-cased subroutine replacements in the adapter.
        okay(m,{0xbd,0xf1,0x4c,0xbd,0xf2,0xa9,0xcc,0x10,0x20,0xbd,0xf3,0x12,
                0xcc,0xf0,0x10,0xbd,0xf3,0xdf});
        require(!f.lines.empty(),"real BIOS line routine must produce vectors");
        mt_v32_lowlevel_get_pen(m,&pen);
        // Init_VIA calls Recalibrate, which leaves scale FF. Each ramp is
        // (scale+2) cycles: move(+32,+16), then draw(+16,-16).
        require(pen.scale==255 && pen.x==48*257 && pen.y==0,
                "real BIOS endpoint must follow timer scale and signed deltas");
        std::printf("BIOS vector call: %zu emitted segments, end=(%d,%d), %llu cycles\n",f.lines.size(),pen.x,pen.y,
                    static_cast<unsigned long long>(mt_v32_lowlevel_cycles(m)));
        pen={0,0,127,127,255};mt_v32_lowlevel_set_pen(m,&pen);
        okay(m,{0xcc,0x10,0x20,0xbd,0xf3,0x12,0xcc,0xf0,0x10,0xbd,0xf3,0xdf});
        mt_v32_lowlevel_get_pen(m,&pen);
        require(pen.x==48*129 && pen.y==0,"caller pen scale must reach real BIOS timer");
    }
    mt_v32_lowlevel_destroy(m);
    std::puts("PASS: low-level bytecode, BIOS calls, persistence, VIA/AY, drawing order, bounds and cancellation");
}
