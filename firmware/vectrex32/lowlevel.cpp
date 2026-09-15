// SPDX-License-Identifier: GPL-3.0-only
#include "lowlevel.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

struct mt_v32_lowlevel {
    mt_v32_callbacks cb;
    mt_v32_lowlevel_registers registers{};
    unsigned cpu_wait=0;
    uint8_t ram[1024]{},dpram[2048]{},psg[16]{};
    unsigned code_cursor=0;
    uint64_t cycles=0,next_service=0;
    mt_v32_lowlevel_status status=MT_V32_LL_OK;
    char error[160]{};
    uint8_t ora=0,orb=1,ddra=255,ddrb=0x9f,acr=0x98,pcr=0xce,ier=0,ifr=0;
    uint16_t t1=0xffff,t1_latch=127,t2=0xffff;
    uint8_t t2_low=0,shift=0;
    bool t1_armed=false,t2_armed=false,pb7=true;
    unsigned t1_hold=0,t2_hold=0;
    bool cb1=true,shift_bit=false,shift_active=false;
    unsigned shift_count=0;
    bool ca2_handshake=true,cb2_handshake=true;
    unsigned ca2_pulse=0,cb2_pulse=0;
    uint8_t buttons=0,psg_address=0,psg_last_mode=0,psg_last_data=0;
    int8_t axes[4]{};
    int32_t x=0,y=0;
    int8_t held_y=0,reference=0;
    uint8_t intensity=127,pattern=255;
    struct Controls { uint8_t pa,pb;bool zero; } delay[13]{};
    unsigned delay_pointer=0;
    bool line_pending=false;
    int32_t line_x0=0,line_y0=0,line_x1=0,line_y1=0;
    int64_t line_dx=0,line_dy=0;
    uint8_t line_intensity=0;

    explicit mt_v32_lowlevel(const mt_v32_callbacks &callbacks):cb(callbacks) {}
    void fail(mt_v32_lowlevel_status code,const char *message) {
        if(status==MT_V32_LL_OK){status=code;std::snprintf(error,sizeof error,"%s",message);}
    }
    void memory_error(const char *operation,uint16_t address) {
        if(status==MT_V32_LL_OK){status=MT_V32_LL_MEMORY;
            std::snprintf(error,sizeof error,"Unsupported 6809 %s at $%04X",operation,unsigned(address));}
    }
    bool accepted(int value){if(!value)fail(MT_V32_LL_CANCELLED,"Execution cancelled");return value!=0;}
    bool ca2() const {
        unsigned mode=(pcr>>1)&7;
        return mode==6?false:mode==4?ca2_handshake:mode==5?ca2_pulse==0:true;
    }
    bool cb2() const {
        if(acr&0x10)return shift_bit;
        unsigned mode=(pcr>>5)&7;
        return mode==6?false:mode==4?cb2_handshake:mode==5?cb2_pulse==0:true;
    }
    uint8_t sound_input() const {
        if(psg_last_mode!=1)return 255;
        if(psg_address==14)return (psg[7]&0x40)?uint8_t(psg[14]&~buttons):uint8_t(~buttons);
        return psg[psg_address];
    }
    uint8_t pa() const { return uint8_t((ora&ddra)|(sound_input()&~ddra)); }
    uint8_t pb() const {
        unsigned select=(((orb&ddrb)|~ddrb)>>1)&3;
        uint8_t pins=uint8_t(0xdf|(axes[select]>int8_t(pa())?0x20:0));
        uint8_t output=uint8_t((orb&0x7f)|((acr&0x80)?(pb7?0x80:0):(orb&0x80)));
        return uint8_t((output&ddrb)|(pins&~ddrb));
    }
    void sound_bus() {
        unsigned mode=(pb()>>3)&3;
        uint8_t data=pa();
        if(mode==3 && data<16)psg_address=data;
        if(mode==2 && (psg_last_mode!=2 || data!=psg_last_data)){
            static const uint8_t masks[16]={255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255};
            psg[psg_address]=data&masks[psg_address];
            if(psg_address<14 && cb.psg && status==MT_V32_LL_OK){
                flush_line();
                if(status==MT_V32_LL_OK)
                    accepted(cb.psg(cb.context,psg_address,psg[psg_address]));
            }
        }
        psg_last_mode=uint8_t(mode);psg_last_data=data;
    }
    void flush_line() {
        if(line_pending && status==MT_V32_LL_OK && cb.line)
            accepted(cb.line(cb.context,line_x0,line_y0,line_x1,line_y1,line_intensity,255));
        line_pending=false;
    }
    void segment(int32_t old_x,int32_t old_y,bool lit) {
        int64_t dx=int64_t(x)-old_x,dy=int64_t(y)-old_y;
        if(!lit){flush_line();return;}
        if(line_pending && line_x1==old_x && line_y1==old_y &&
            line_dx==dx && line_dy==dy && line_intensity==intensity){line_x1=x;line_y1=y;return;}
        flush_line();line_pending=true;line_x0=old_x;line_y0=old_y;line_x1=x;line_y1=y;
        line_dx=dx;line_dy=dy;line_intensity=intensity;
    }
    static int32_t add_coordinate(int32_t value,int delta) {
        int64_t sum=int64_t(value)+delta;
        return int32_t(std::max<int64_t>(INT32_MIN,std::min<int64_t>(INT32_MAX,sum)));
    }
    void tick() {
        ++cycles;
        if(cycles>=next_service){next_service=cycles+256;
            if(cb.service && !accepted(cb.service(cb.context)))return;}
        // Integrate the pin state that occupied this cycle. Sampling after
        // timer expiry would lose the first low-PB7 cycle of every vector.
        Controls sampled={pa(),pb(),!ca2()};
        if(ca2_pulse)--ca2_pulse;
        if(cb2_pulse)--cb2_pulse;
        if(t1_hold)--t1_hold;
        else if(t1==0){
            t1=0xffff;
            if(t1_armed){ifr|=0x40;
                if(acr&0x40){t1=t1_latch;t1_hold=1;pb7=!pb7;}
                else{t1_armed=false;pb7=true;}}
        }else --t1;
        bool t2_low_event=false;
        if(t2_hold)--t2_hold;
        else if(!(acr&0x20)){
            unsigned mode=(acr>>2)&7;
            if((t2&255)==0 && (mode==1 || mode==4 || mode==5)){
                t2=uint16_t((t2&0xff00)|t2_low);t2_hold=1;t2_low_event=true;
            }else{
                if(t2==0 && t2_armed){ifr|=0x20;t2_armed=false;}
                --t2;
            }
        }
        unsigned mode=(acr>>2)&7;
        bool shift_clock=(mode==2 || mode==6) ||
                         ((mode==1 || mode==4 || mode==5) && t2_low_event);
        if(shift_active && shift_clock){
            cb1=!cb1;
            if(!cb1 && mode>=4){shift_bit=(shift&0x80)!=0;shift=uint8_t((shift<<1)|(shift>>7));}
            if(cb1){
                if(mode<4)shift=uint8_t((shift<<1)|1);
                if(++shift_count==8){shift_count=0;
                    if(mode!=4){ifr|=4;shift_active=false;}}
            }
        }
        Controls old=delay[delay_pointer];
        delay[delay_pointer]=sampled;
        delay_pointer=(delay_pointer+1)%13;
        if(!(old.pb&1)){
            switch((old.pb>>1)&3){
                case 0:held_y=int8_t(old.pa);break;
                case 1:reference=int8_t(old.pa);break;
                case 2:intensity=(old.pa&0x80)?0:uint8_t(old.pa&127);break;
                case 3:
                    if(old.pa)fail(MT_V32_LL_MEMORY,"External DAC audio has no Vectrex32 callback");
                    break;
            }
        }
        int32_t old_x=x,old_y=y;
        if(old.zero){x=0;y=0;}
        else if(!(old.pb&0x80)){
            x=add_coordinate(x,int(int8_t(old.pa))-reference);
            y=add_coordinate(y,int(held_y)-reference);
        }
        segment(old_x,old_y,cb2() && intensity!=0);
    }
    void advance(unsigned count){while(count-- && status==MT_V32_LL_OK)tick();}
    void port_access(bool port_a) {
        if(port_a){ifr&=uint8_t(~2);if(!(pcr&2))ifr&=uint8_t(~1);ca2_handshake=false;ca2_pulse=2;}
        else{ifr&=uint8_t(~16);if(!(pcr&32))ifr&=uint8_t(~8);cb2_handshake=false;cb2_pulse=2;}
    }
    uint8_t via_read(unsigned reg) {
        switch(reg){
            case 0:port_access(false);return pb();
            case 1:port_access(true);return pa();
            case 2:return ddrb;case 3:return ddra;
            case 4:ifr&=uint8_t(~0x40);return uint8_t(t1);
            case 5:return uint8_t(t1>>8);
            case 6:return uint8_t(t1_latch);case 7:return uint8_t(t1_latch>>8);
            case 8:ifr&=uint8_t(~0x20);return uint8_t(t2);
            case 9:return uint8_t(t2>>8);
            case 10:ifr&=uint8_t(~4);shift_count=0;shift_active=((acr>>2)&7)!=0;cb1=true;return shift;
            case 11:return acr;case 12:return pcr;
            case 13:return uint8_t(ifr|((ifr&ier&127)?128:0));
            case 14:return uint8_t(ier|128);case 15:return pa();
        }return 255;
    }
    void via_write(unsigned reg,uint8_t value) {
        switch(reg){
            case 0:orb=value;port_access(false);break;
            case 1:ora=value;port_access(true);break;
            case 2:ddrb=value;break;case 3:ddra=value;break;
            case 4:case 6:t1_latch=uint16_t((t1_latch&0xff00)|value);break;
            case 5:t1_latch=uint16_t((t1_latch&255)|(unsigned(value)<<8));t1=t1_latch;
                t1_hold=1;t1_armed=true;pb7=false;ifr&=uint8_t(~0x40);break;
            case 7:t1_latch=uint16_t((t1_latch&255)|(unsigned(value)<<8));ifr&=uint8_t(~0x40);break;
            case 8:t2_low=value;break;
            case 9:t2=uint16_t((unsigned(value)<<8)|t2_low);t2_hold=1;t2_armed=true;ifr&=uint8_t(~0x20);break;
            case 10:shift=value;shift_count=0;shift_active=((acr>>2)&7)!=0;cb1=true;ifr&=uint8_t(~4);break;
            case 11:acr=value;break;case 12:pcr=value;break;
            case 13:ifr&=uint8_t(~value);break;
            case 14:ier=value&128?uint8_t(ier|(value&127)):uint8_t(ier&~value);break;
            case 15:ora=value;break;
        }
        sound_bus();
    }
    uint8_t read(uint16_t address) {
        if(address<0x8000)return dpram[address&2047];
        if(address>=0xc800 && address<0xd000)return ram[address&1023];
        if(address>=0xd000 && address<0xd800)return via_read(address&15);
        if(address>=0xe000){uint8_t value=0;
            if(cb.read_bios && cb.read_bios(cb.context,address,&value,1))return value;
            memory_error("BIOS read",address);return 0;}
        memory_error("read",address);return 0;
    }
    void write(uint16_t address,uint8_t value) {
        if(address<0x8000)dpram[address&2047]=value;
        else if(address>=0xc800 && address<0xd000)ram[address&1023]=value;
        else if(address>=0xd000 && address<0xd800)via_write(address&15,value);
        else memory_error("write",address);
    }
};

namespace {
mt_v32_lowlevel *active_cpu=nullptr;
mt_v32_lowlevel *active_operation=nullptr;
// BIOS callbacks also run during deferred Peek, outside the CPU loop. Keep
// their context alive and reject callback re-entry through the same guard.
struct OperationGuard {
    explicit OperationGuard(mt_v32_lowlevel *m,bool cpu=false){
        active_operation=m;
        if(cpu)active_cpu=m;
    }
    ~OperationGuard(){active_cpu=nullptr;active_operation=nullptr;}
    OperationGuard(const OperationGuard&)=delete;
    OperationGuard& operator=(const OperationGuard&)=delete;
};
void illegal_opcode(const char *,...){
    if(active_cpu)active_cpu->fail(MT_V32_LL_OPCODE,"Unsupported 6809 opcode or addressing mode");
}
namespace cpu_chip {
// Only the CPU chip is included. The original VecX machine is not used.
[[maybe_unused]] void e6809_reset(void);
#define printf illegal_opcode
#include "../../third_party/vectrex32/e6809/e6809.c"
#undef printf
#undef einline
}
unsigned char cpu_read(unsigned address){
    active_cpu->advance(1);
    return active_cpu->status==MT_V32_LL_OK?active_cpu->read(uint16_t(address)):0;
}
void cpu_write(unsigned address,unsigned char value){
    active_cpu->advance(1);
    if(active_cpu->status==MT_V32_LL_OK)active_cpu->write(uint16_t(address),value);
}
void load_registers(mt_v32_lowlevel *m){
    const auto &r=m->registers;
    cpu_chip::reg_a=r.a;cpu_chip::reg_b=r.b;cpu_chip::reg_dp=r.dp;cpu_chip::reg_cc=r.cc;
    cpu_chip::reg_x=r.x;cpu_chip::reg_y=r.y;cpu_chip::reg_u=r.u;cpu_chip::reg_s=r.s;cpu_chip::reg_pc=r.pc;
    cpu_chip::irq_status=m->cpu_wait;
}
void save_registers(mt_v32_lowlevel *m){
    auto &r=m->registers;
    r.a=uint8_t(cpu_chip::reg_a);r.b=uint8_t(cpu_chip::reg_b);r.dp=uint8_t(cpu_chip::reg_dp);r.cc=uint8_t(cpu_chip::reg_cc);
    r.x=uint16_t(cpu_chip::reg_x);r.y=uint16_t(cpu_chip::reg_y);r.u=uint16_t(cpu_chip::reg_u);
    r.s=uint16_t(cpu_chip::reg_s);r.pc=uint16_t(cpu_chip::reg_pc);m->cpu_wait=cpu_chip::irq_status;
}
bool not_busy(mt_v32_lowlevel *m){
    if(!active_operation)return true;
    if(m)m->fail(MT_V32_LL_REENTRANT,"Reentrant low-level operation");
    active_operation->fail(MT_V32_LL_REENTRANT,"Reentrant low-level operation");
    return false;
}
bool accessible(mt_v32_lowlevel *m){
    if(!m || !not_busy(m))return false;
    m->status=MT_V32_LL_OK;m->error[0]=0;return true;
}
}

extern "C" {
mt_v32_lowlevel *mt_v32_lowlevel_create(const mt_v32_callbacks *callbacks){
    if(!callbacks || !not_busy(nullptr))return nullptr;
    auto *m=new(std::nothrow) mt_v32_lowlevel(*callbacks);
    if(m)mt_v32_lowlevel_reset(m);
    return m;
}
void mt_v32_lowlevel_destroy(mt_v32_lowlevel *m){if(m && not_busy(m))delete m;}
void mt_v32_lowlevel_reset(mt_v32_lowlevel *m){
    if(!m || !not_busy(m))return;
    mt_v32_callbacks callbacks=m->cb;*m=mt_v32_lowlevel(callbacks);
    m->registers.dp=0xd0;m->registers.s=0xcbea;m->registers.cc=0x50;
    m->ram[0x28]=5;m->ram[0x3d]=0x30;m->ram[0x3e]=0x75;m->ram[0x7d]=1;
    for(auto &control:m->delay)control={0,0x81,false};
}
void mt_v32_lowlevel_begin_frame(mt_v32_lowlevel *m){if(m && not_busy(m))m->code_cursor=0;}
void mt_v32_lowlevel_set_pen(mt_v32_lowlevel *m,const mt_v32_lowlevel_pen *pen){
    if(!m || !pen || !not_busy(m))return;
    m->x=pen->x;m->y=pen->y;m->intensity=std::min<uint8_t>(127,pen->intensity);
    m->pattern=pen->pattern;m->t1_latch=uint16_t((m->t1_latch&0xff00)|pen->scale);
}
void mt_v32_lowlevel_get_pen(const mt_v32_lowlevel *m,mt_v32_lowlevel_pen *pen){
    if(m && pen)*pen={m->x,m->y,uint8_t(m->t1_latch),m->intensity,m->pattern};
}
void mt_v32_lowlevel_set_controls(mt_v32_lowlevel *m,const int controls[2][6]){
    if(!m || !controls || !not_busy(m))return;
    m->buttons=0;
    for(unsigned player=0;player<2;++player){
        for(unsigned axis=0;axis<2;++axis)m->axes[player*2+axis]=int8_t(std::max(-128,std::min(127,controls[player][axis])));
        for(unsigned button=0;button<4;++button)if(controls[player][button+2])m->buttons|=uint8_t(1u<<(player*4+button));
    }
}
void mt_v32_lowlevel_get_registers(const mt_v32_lowlevel *m,mt_v32_lowlevel_registers *registers){if(m && registers)*registers=m->registers;}
void mt_v32_lowlevel_set_registers(mt_v32_lowlevel *m,const mt_v32_lowlevel_registers *registers){if(m && registers && not_busy(m))m->registers=*registers;}
mt_v32_lowlevel_status mt_v32_lowlevel_execute(mt_v32_lowlevel *m,const uint8_t *bytes,size_t count,uint64_t budget){
    if(!m)return MT_V32_LL_ARGUMENT;
    if(!accessible(m))return m->status;
    if((!bytes && count) || count>2048-m->code_cursor || !budget){
        m->fail(MT_V32_LL_ARGUMENT,"CodeSprite exceeds its 2KiB frame arena or has no cycle budget");return m->status;}
    if(!count)return MT_V32_LL_OK;
    budget=std::min<uint64_t>(budget,10000000);
    unsigned start=m->code_cursor,end=start+unsigned(count);
    std::memcpy(m->dpram+start,bytes,count);m->code_cursor=end;m->registers.pc=uint16_t(start);
    uint64_t initial_cycles=m->cycles;m->next_service=m->cycles;
    OperationGuard operation(m,true);
    cpu_chip::e6809_read8=cpu_read;cpu_chip::e6809_write8=cpu_write;load_registers(m);
    while(uint16_t(cpu_chip::reg_pc)!=end && m->status==MT_V32_LL_OK){
        if(m->cycles-initial_cycles>=budget){m->fail(MT_V32_LL_BUDGET,"CodeSprite cycle budget exhausted");break;}
        uint64_t before=m->cycles;
        unsigned elapsed=cpu_chip::e6809_sstep((m->ifr&m->ier&127)!=0,0);
        uint64_t accesses=m->cycles-before;
        if(elapsed>accesses)m->advance(unsigned(elapsed-accesses));
        else if(!accesses)m->advance(1);
    }
    save_registers(m);m->flush_line();return m->status;
}
mt_v32_lowlevel_status mt_v32_lowlevel_peek_dpram(mt_v32_lowlevel *m,unsigned offset,uint8_t *bytes,size_t count){
    if(!m)return MT_V32_LL_ARGUMENT;
    if(!accessible(m))return m->status;
    if(offset>2048 || count>2048-offset || (!bytes && count)){m->fail(MT_V32_LL_ARGUMENT,"DPRAM read outside 0..2047");return m->status;}
    if(count)std::memcpy(bytes,m->dpram+offset,count);
    return m->status;
}
mt_v32_lowlevel_status mt_v32_lowlevel_poke_dpram(mt_v32_lowlevel *m,unsigned offset,const uint8_t *bytes,size_t count){
    if(!m)return MT_V32_LL_ARGUMENT;
    if(!accessible(m))return m->status;
    if(offset>2048 || count>2048-offset || (!bytes && count)){m->fail(MT_V32_LL_ARGUMENT,"DPRAM write outside 0..2047");return m->status;}
    if(count)std::memcpy(m->dpram+offset,bytes,count);
    return m->status;
}
mt_v32_lowlevel_status mt_v32_lowlevel_read_memory(mt_v32_lowlevel *m,uint16_t address,uint8_t *bytes,size_t count){
    if(!m)return MT_V32_LL_ARGUMENT;
    if(!accessible(m))return m->status;
    if(!bytes || count<1 || count>16 || count>65536u-address){m->fail(MT_V32_LL_ARGUMENT,"CPU Peek requires 1..16 bytes without address wrap");return m->status;}
    OperationGuard operation(m);
    for(size_t i=0;i<count && m->status==MT_V32_LL_OK;++i)bytes[i]=m->read(uint16_t(address+i));
    return m->status;
}
const char *mt_v32_lowlevel_error(const mt_v32_lowlevel *m){return m?m->error:"Missing low-level context";}
uint64_t mt_v32_lowlevel_cycles(const mt_v32_lowlevel *m){return m?m->cycles:0;}
}
