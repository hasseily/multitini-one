/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_debug.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(mt_c64_debug_t *s, const char *format, ...)
{
    char text[320];va_list ap;va_start(ap,format);
    (void)vsnprintf(text,sizeof text,format,ap);va_end(ap);
    if(s->ops.output)s->ops.output(s->ops.context,text);
}
static uint32_t rd(mt_c64_debug_t *s, uint32_t address)
{ return s->ops.read32(s->ops.context,address); }
static void wr(mt_c64_debug_t *s, uint32_t address, uint32_t value)
{ s->ops.write32(s->ops.context,address,value); }
static uint64_t now(mt_c64_debug_t *s)
{ return s->ops.milliseconds(s->ops.context); }
static void progress(mt_c64_debug_t *s)
{ s->ops.progress(s->ops.context); }
static bool debug_registers(const mt_c64_debug_t *s)
{ return s->active && (s->caps.turbo || s->caps.ram_peek || s->caps.audio_capture); }
static void apply(mt_c64_debug_t *s, bool changed)
{ if(s->active && s->caps.turbo && s->ops.apply_turbo)s->ops.apply_turbo(s->ops.context,changed); }
static void abort_capture(mt_c64_debug_t *s, bool hardware)
{
    if(s->capture) {
        if(hardware && debug_registers(s))wr(s,MT_REG_DEBUG_CONTROL,s->session ? MT_DEBUG_CONTROL_ENABLE : 0);
        if(s->ops.release)s->ops.release(s->ops.context,s->capture);
        s->capture=NULL;s->capture_frames=0;s->capture_target=0;
    }
}
void mt_c64_debug_init(mt_c64_debug_t *s, const mt_c64_debug_ops_t *ops, bool c64u_default)
{
    if(!s)return;
    memset(s,0,sizeof *s);if(ops)s->ops=*ops;
    s->c64u_default=c64u_default;s->debug_bits=c64u_default ? MT_C64_CPU_CONFIG_C64U_TOP : 0;
}
void mt_c64_debug_activate(mt_c64_debug_t *s, const mt_c64_capabilities_t *caps, unsigned variant)
{
    if(!s || !caps)return;
    abort_capture(s,false);s->caps=*caps;s->variant=(uint8_t)variant;s->active=true;
    s->reu_host.reload_required=false;s->override_active=false;s->trace=false;
    if(debug_registers(s))wr(s,MT_REG_DEBUG_CONTROL,s->session ? MT_DEBUG_CONTROL_ENABLE : 0);
    apply(s,false);
    if(caps->cpu2 & MT_C64_CPU_CAPS2_DIAGNOSTIC_IMAGE)say(s,"C64 diagnostic build");
}
void mt_c64_debug_deactivate(mt_c64_debug_t *s)
{
    if(!s)return;
    abort_capture(s,true);s->active=false;s->override_active=false;s->trace=false;
    memset(&s->caps,0,sizeof s->caps);
}
void mt_c64_debug_clear_override(mt_c64_debug_t *s)
{ if(s)s->override_active=false; }
uint32_t mt_c64_debug_effective_overlay(const mt_c64_debug_t *s, uint32_t config,
    uint32_t previous, bool apply_change)
{
    if(!s)return config;
    config=(config & ~MT_C64_CPU_CONFIG_DEBUG_MASK) | s->debug_bits;
    if(s->armed && s->session)config|=MT_C64_CPU_CONFIG_DIAGNOSTICS;
    if(s->override_active && s->session)
        config=(config & ~(MT_C64_CPU_CONFIG_SPEED_MASK|MT_C64_CPU_CONFIG_MODE_MASK)) |
               0x10U | mt_c64_clamp_speed(s->override_index,(uint16_t)s->caps.cpu);
    unsigned counter=(previous>>12)&15U;
    if(apply_change && ((config^previous)&0x7FU))counter=(counter+1U)&15U;
    return (config & ~MT_C64_CPU_CONFIG_APPLY_MASK) | (counter<<12);
}
static bool number(const char *text, unsigned base, uint32_t maximum, uint32_t *value)
{
    if(!text || !*text || *text=='-' || *text=='+')return false;
    if(*text=='$'){++text;base=16;}
    if(!*text)return false;
    errno=0;char *end=NULL;unsigned long n=strtoul(text,&end,(int)base);
    if(errno || !end || *end || n>maximum)return false;
    *value=(uint32_t)n;return true;
}
static int tokens(char *line, char **argv, int limit)
{
    int argc=0;char *p=line;
    while(*p) {
        while(isspace((unsigned char)*p))++p;
        if(!*p)break;
        if(argc==limit)return -1;
        if(*p=='"') {
            argv[argc++]=++p;while(*p && *p!='"')++p;
            if(!*p)return -1;
            *p++=0;if(*p && !isspace((unsigned char)*p))return -1;
        } else {
            argv[argc++]=p;while(*p && !isspace((unsigned char)*p))++p;
            if(*p)*p++=0;
        }
    }
    return argc;
}
static bool onoff(const char *word, bool *value)
{
    if(!strcmp(word,"on")){*value=true;return true;}
    if(!strcmp(word,"off")){*value=false;return true;}
    return false;
}
static bool ready(mt_c64_debug_t *s)
{
    if(s->active)return true;
    say(s,"C64 is not active");return false;
}
static bool wait_pause(mt_c64_debug_t *s)
{
    const uint64_t begin=now(s);
    s->ops.pause(s->ops.context,true);
    while(!s->ops.paused(s->ops.context)) {
        if(now(s)-begin>=100)return false;
        progress(s);
    }
    return true;
}
static bool wait_reu(mt_c64_debug_t *s)
{
    const uint64_t begin=now(s);
    while(!(rd(s,MT_REG_C64_REU_HOST)&MT_C64_REU_HOST_IDLE)) {
        if(now(s)-begin>=1000)return false;
        progress(s);
    }
    return true;
}
static void turbo_status(mt_c64_debug_t *s, bool initial)
{
    const uint32_t config=rd(s,MT_REG_C64_CPU_CONFIG),status=rd(s,MT_REG_C64_CPU_STATUS);
    if(initial || config!=s->last_config || status!=s->last_status) {
        if(initial)say(s,"C64 CAPS=%08lx CAPS2=%08lx variant=%u timing=%u diagnostic=%u",
            (unsigned long)s->caps.cpu,(unsigned long)s->caps.cpu2,s->variant,
            (unsigned)(s->caps.cpu2&255U),(unsigned)((s->caps.cpu2&MT_C64_CPU_CAPS2_DIAGNOSTIC_IMAGE)!=0));
        say(s,"C64 CONFIG=%08lx STATUS=%08lx index=%u fast=%u cause=%u sticky=%x D031=%02x apply=%u",
            (unsigned long)config,(unsigned long)status,(unsigned)(status&15U),
            (unsigned)((status>>4)&1U),(unsigned)((status>>6)&3U),(unsigned)((status>>24)&7U),
            (unsigned)((status>>8)&255U),(unsigned)((config>>12)&15U));
        s->last_config=config;s->last_status=status;
    }
    s->trace_time=now(s);
}
static void ua_status(mt_c64_debug_t *s)
{
    if(!s->caps.ultimate_audio){say(s,"Ultimate Audio not available on this image");return;}
    const uint32_t status=rd(s,MT_REG_C64_UA_STATUS),config=rd(s,MT_REG_C64_EXPANSION_CONFIG);
    const uint32_t host=s->caps.reu_host ? rd(s,MT_REG_C64_REU_HOST) : 0;
    say(s,"UA status=%08lx fault=%u underrun=%u clip=%u defer=%u playing=%02x count=%u fetches=%lu REU-misses=%lu REU=%u UA=%u idle=%u hold=%u held=%u armed=%u",
        (unsigned long)status,(unsigned)(status&1U),(unsigned)((status>>1)&1U),(unsigned)((status>>2)&1U),
        (unsigned)((status>>3)&1U),(unsigned)((status>>8)&127U),(unsigned)(status>>16),
        (unsigned long)rd(s,MT_REG_C64_UA_FETCHES),(unsigned long)rd(s,MT_REG_C64_REU_MISSES),
        (unsigned)((config>>3)&1U),(unsigned)((config>>4)&1U),(unsigned)((host>>12)&1U),
        (unsigned)((host>>13)&1U),(unsigned)((host>>14)&1U),(unsigned)s->armed);
    /* CAPS2 diagnostic-image is not a probe capability: an F6 image also
     * sets it. The optional, unimplemented G9 probe at 0x0FC stays untouched. */
    if(!s->caps.reu_host)say(s,"UA host idle/hold/held unavailable on this image");
}
static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc^=byte;
    for(unsigned i=0;i<8;++i)crc=(crc>>1)^((0U-(crc&1U))&UINT32_C(0xedb88320));
    return crc;
}
static void dump(mt_c64_debug_t *s, bool reu, uint32_t address, uint32_t length)
{
    if(reu ? !s->caps.reu_host : !s->caps.ram_peek) {say(s,"Memory readback not available on this image");return;}
    if(s->reu_host.reload_required){say(s,"REU cache did not clear; reload the C64 core");return;}
    if(s->capture || (s->ops.content_busy && s->ops.content_busy(s->ops.context))){say(s,"Busy; try again");return;}
    bool was_paused=s->ops.paused(s->ops.context),ok=false;
    uint32_t crc=UINT32_C(0xffffffff),counter=0;
    if(!wait_pause(s)){say(s,"C64 did not pause; try again");goto finish;}
    if(reu && !wait_reu(s)){say(s,"REU busy; try again");goto finish;}
    if(!reu)counter=(rd(s,MT_REG_C64_RAM_PEEK)>>16)&15U;
    for(uint32_t done=0;done<length;) {
        uint8_t bytes[16];const unsigned amount=length-done<16 ? (unsigned)(length-done) : 16;
        if(reu) {
            if(!s->ops.reu_read || !s->ops.reu_read(s->ops.context,address+done,bytes,amount)){say(s,"REU read failed");goto finish;}
        } else for(unsigned i=0;i<amount;++i) {
            counter=(counter+1U)&15U;wr(s,MT_REG_C64_RAM_PEEK,(counter<<16)|(address+done+i));
            const uint64_t start=now(s);uint32_t value;
            for(;;) {
                value=rd(s,MT_REG_C64_RAM_PEEK);
                if(((value>>16)&15U)==counter)break;
                if(now(s)-start>=100){say(s,"C64 RAM read timed out");goto finish;}
                progress(s);
            }
            if(value&MT_C64_RAM_PEEK_REFUSED){say(s,"C64 RAM read refused");goto finish;}
            bytes[i]=(uint8_t)value;
        }
        char line[80];int used=snprintf(line,sizeof line,"%06lx:",(unsigned long)(address+done));
        for(unsigned i=0;i<amount;++i) {
            used+=snprintf(line+(size_t)used,sizeof line-(size_t)used," %02x",bytes[i]);crc=crc_byte(crc,bytes[i]);
        }
        say(s,"%s",line);done+=amount;progress(s);
    }
    ok=true;
finish:
    if(!was_paused && !s->reu_host.reload_required)s->ops.pause(s->ops.context,false);
    if(ok)say(s,"%lu bytes CRC32=%08lx",(unsigned long)length,(unsigned long)(crc^UINT32_C(0xffffffff)));
}
static uint32_t host_read(void *context){return rd(context,MT_REG_C64_REU_HOST);}
static void host_write(void *context,uint32_t value){wr(context,MT_REG_C64_REU_HOST,value);}
static void host_pause(void *context,bool pause){mt_c64_debug_t *s=context;s->ops.pause(s->ops.context,pause);}
static bool host_paused(void *context){mt_c64_debug_t *s=context;return s->ops.paused(s->ops.context);}
static uint64_t host_time(void *context){return now(context);}
static void host_progress(void *context){progress(context);}
static void host_drain(void *context){mt_c64_debug_t *s=context;s->ops.drain_writes(s->ops.context);}
/* Per-load cursor, kept in its own stack context rather than persistent
 * settings. The generic preload function invokes copy in ascending blocks. */
typedef struct {mt_c64_debug_t *s;uint32_t base;size_t file_offset;} load_context_t;
static uint32_t load_read(void *c){return host_read(((load_context_t *)c)->s);}
static void load_write(void *c,uint32_t v){host_write(((load_context_t *)c)->s,v);}
static void load_pause(void *c,bool v){host_pause(((load_context_t *)c)->s,v);}
static bool load_paused(void *c){return host_paused(((load_context_t *)c)->s);}
static uint64_t load_time(void *c){return host_time(((load_context_t *)c)->s);}
static void load_progress(void *c){host_progress(((load_context_t *)c)->s);}
static void load_drain(void *c){host_drain(((load_context_t *)c)->s);}
static bool load_copy(void *context,uint32_t offset,size_t size)
{
    load_context_t *c=context;uint8_t bytes[4096];
    if(size>sizeof bytes || offset<c->base || offset-c->base!=c->file_offset)return false;
    if(!c->s->ops.file_read(c->s->ops.context,c->file_offset,bytes,size) ||
       !c->s->ops.reu_write(c->s->ops.context,offset,bytes,size))return false;
    c->file_offset+=size;return true;
}
mt_c64_reu_load_result_t mt_c64_debug_load_reu(mt_c64_debug_t *s,const char *path,uint32_t offset)
{
    if(!s || !path)return MT_REU_LOAD_BAD_ARGUMENT;
    if(!s->active || !s->caps.reu_host){say(s,"REU image load not available on this image");return MT_REU_LOAD_UNAVAILABLE;}
    if(s->reu_host.reload_required){say(s,"REU cache did not clear; reload the C64 core");return MT_REU_LOAD_RELOAD_REQUIRED;}
    const bool cartridge=s->ops.cartridge && s->ops.cartridge(s->ops.context);
    const bool busy=s->capture || (s->ops.content_busy && s->ops.content_busy(s->ops.context));
    if(cartridge){say(s,"Eject the cartridge before loading REU memory");return MT_REU_LOAD_CARTRIDGE;}
    if(busy){say(s,"Busy; try again");return MT_REU_LOAD_BUSY;}
    if(!s->ops.file_open || !s->ops.file_read || !s->ops.file_close || !s->ops.reu_write || !s->ops.drain_writes){say(s,"REU file service unavailable");return MT_REU_LOAD_BAD_ARGUMENT;}
    size_t size=0;
    if(!s->ops.file_open(s->ops.context,path,&size)){say(s,"Cannot open REU image");return MT_REU_LOAD_BAD_ARGUMENT;}
    load_context_t context={s,offset,0};
    const mt_c64_reu_host_ops_t ops={&context,load_read,load_write,load_pause,load_paused,load_time,load_progress,load_copy,load_drain};
    const mt_c64_reu_load_result_t result=mt_c64_reu_load(&s->reu_host,&ops,true,s->caps.ultimate_audio,cartridge,busy,offset,size);
    s->ops.file_close(s->ops.context);
    if(result==MT_REU_LOAD_OK && s->ops.reu_enabled && !s->ops.reu_enabled(s->ops.context))say(s,"REU image loaded; turn on RAM expansion to use it");
    else say(s,"%s",mt_c64_reu_load_message(result));
    return result;
}
static void le16(uint8_t *p,uint16_t value){p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void le32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8));}
void mt_c64_debug_wav_header(uint8_t h[44],uint32_t frames)
{
    memset(h,0,44);memcpy(h,"RIFF",4);le32(h+4,36U+frames*4U);memcpy(h+8,"WAVEfmt ",8);
    le32(h+16,16);le16(h+20,1);le16(h+22,2);le32(h+24,48000);le32(h+28,192000);
    le16(h+32,4);le16(h+34,16);memcpy(h+36,"data",4);le32(h+40,frames*4U);
}
double mt_c64_debug_goertzel(const uint8_t *pcm,size_t frames,double frequency)
{
    if(!pcm || !frames || frequency<=0 || frequency>=24000)return 0;
    const double coefficient=2.0*cos(6.2831853071795864769*frequency/48000.0);
    double s1=0,s2=0;
    for(size_t n=0;n<frames;++n) {
        const uint8_t *p=pcm+n*4;
        const int32_t l=(int16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
        const int32_t r=(int16_t)((uint16_t)p[2]|((uint16_t)p[3]<<8));
        const double value=(l+r)*0.5+coefficient*s1-s2;s2=s1;s1=value;
    }
    const double energy=(s1*s1+s2*s2-coefficient*s1*s2)/((double)frames*(double)frames);
    return energy>0 ? energy : 0;
}
static void capture_start(mt_c64_debug_t *s,unsigned seconds,const char *name)
{
    if(!s->caps.audio_capture){say(s,"Audio capture not available on this image");return;}
    if(s->capture){say(s,"Audio capture already running");return;}
    if(!s->ops.allocate || !s->ops.release || !s->ops.file_write){say(s,"Audio capture file service unavailable");return;}
    if(!name)name="c64-audio";
    for(const char *p=name;*p;++p)if(!isalnum((unsigned char)*p) && *p!='-' && *p!='_') {say(s,"Capture name uses letters, digits, '-' or '_'");return;}
    if(strlen(name)>80){say(s,"Capture name too long");return;}
    s->capture_target=(size_t)seconds*48000;s->capture_frames=0;s->capture_overflow=false;s->capture_peak=0;
    s->capture=s->ops.allocate(s->ops.context,44+s->capture_target*4);
    if(!s->capture){say(s,"Not enough memory for audio capture");return;}
    (void)snprintf(s->capture_path,sizeof s->capture_path,"0:/multitini/capture/%s.wav",name);
    wr(s,MT_REG_DEBUG_CONTROL,MT_DEBUG_CONTROL_ENABLE|MT_DEBUG_CONTROL_AUDIO_CLEAR);
    wr(s,MT_REG_DEBUG_CONTROL,MT_DEBUG_CONTROL_ENABLE|MT_DEBUG_CONTROL_AUDIO_RUN);
    s->capture_started=now(s);say(s,"Audio capture started: %u seconds",seconds);
}
static void capture_finish(mt_c64_debug_t *s,bool timed_out)
{
    wr(s,MT_REG_DEBUG_CONTROL,MT_DEBUG_CONTROL_ENABLE);
    mt_c64_debug_wav_header(s->capture,(uint32_t)s->capture_frames);
    const bool saved=s->ops.file_write(s->ops.context,s->capture_path,s->capture,44+s->capture_frames*4);
    say(s,"Audio capture %s: frames=%lu overflow=%u peak=%u %s",
        (timed_out || s->capture_overflow || !saved)?"INVALID":"complete",
        (unsigned long)s->capture_frames,(unsigned)s->capture_overflow,s->capture_peak,saved?s->capture_path:"file write failed");
    for(unsigned t=0;t<s->tone_count;++t) {
        const double center=mt_c64_debug_goertzel(s->capture+44,s->capture_frames,s->tones[t]);
        double neighbours[16];unsigned count=0;
        for(int n=-8;n<=8;++n)if(n) {
            const double frequency=(double)s->tones[t]+n*25.0;
            if(frequency>0 && frequency<24000)neighbours[count++]=mt_c64_debug_goertzel(s->capture+44,s->capture_frames,frequency);
        }
        for(unsigned a=1;a<count;++a)for(unsigned b=a;b>0 && neighbours[b]<neighbours[b-1];--b) {
            const double tmp=neighbours[b];neighbours[b]=neighbours[b-1];neighbours[b-1]=tmp;
        }
        const double median=count ? (neighbours[(count-1)/2]+neighbours[count/2])*0.5 : 0;
        const double db=10.0*log10((center+1e-20)/(median+1e-20));
        say(s,"Tone %u Hz energy=%.9g neighbourhood-median=%.9g ratio=%.2f dB",s->tones[t],center,median,db);
    }
    abort_capture(s,false);
}
void mt_c64_debug_tick(mt_c64_debug_t *s)
{
    if(!s || !s->active || !s->session)return;
    if(s->capture) {
        const uint32_t status=rd(s,MT_REG_DEBUG_STATUS);
        s->capture_overflow|=(status&MT_DEBUG_STATUS_OVERFLOW)!=0;
        unsigned level=(unsigned)(status&1023U);if(level>512)level=512;
        for(unsigned n=0;n<level && s->capture_frames<s->capture_target;++n) {
            const uint32_t word=rd(s,MT_REG_AUDIO_CAPTURE_DATA);
            const uint16_t left=(uint16_t)(word>>16),right=(uint16_t)word;
            uint8_t *target=s->capture+44+s->capture_frames*4;le16(target,left);le16(target+2,right);
            const int32_t l=(int16_t)left,r=(int16_t)right;
            const unsigned peak=(unsigned)((l<0?-l:l)>(r<0?-r:r)?(l<0?-l:l):(r<0?-r:r));
            if(peak>s->capture_peak)s->capture_peak=peak;
            ++s->capture_frames;
        }
        const bool timed_out=now(s)-s->capture_started>(s->capture_target/48)+2000;
        if(s->capture_frames==s->capture_target || timed_out)capture_finish(s,timed_out);
    }
    if(s->trace && s->caps.turbo && now(s)-s->trace_time>=100)turbo_status(s,false);
}
bool mt_c64_debug_command(mt_c64_debug_t *s,const char *input)
{
    if(!s || !input)return false;
    while(isspace((unsigned char)*input))++input;
    if(*input==':')++input;
    char command[24];size_t n=0;
    while(input[n] && !isspace((unsigned char)input[n]) && n<sizeof command-1){command[n]=input[n];++n;}
    command[n]=0;
    const bool debug=!strcmp(command,"debug"),load=!strcmp(command,"reuload");
    const bool known=debug || load || !strcmp(command,"c64diag") || !strcmp(command,"uastat") ||
        !strcmp(command,"speed") || !strcmp(command,"turbo") || !strcmp(command,"iec") || !strcmp(command,"turbostat") ||
        !strcmp(command,"peek") || !strcmp(command,"ramdump") || !strcmp(command,"reudump") || !strcmp(command,"audiocap");
    if(!known)return false;
    if(!debug && !load && !s->session){say(s,"debug is off; send :debug on");return true;}
    char line[512];if(strlen(input)>=sizeof line){say(s,"Command too long");return true;}
    strcpy(line,input);char *args[16];const int argc=tokens(line,args,16);
    if(argc<1){say(s,"Invalid command syntax");return true;}
    bool value=false;uint32_t number_value=0;
    if(debug) {
        if(argc==1){say(s,"Debug is %s",s->session?"on":"off");return true;}
        if(argc!=2 || !onoff(args[1],&value)){say(s,"Usage: :debug on|off");return true;}
        if(value==s->session){say(s,"Debug is already %s",value?"on":"off");return true;}
        s->session=value;
        if(!value) {
            abort_capture(s,true);s->armed=false;s->override_active=false;s->trace=false;
            s->debug_bits=s->c64u_default?MT_C64_CPU_CONFIG_C64U_TOP:0;
            apply(s,true);
        }
        if(debug_registers(s))wr(s,MT_REG_DEBUG_CONTROL,value?MT_DEBUG_CONTROL_ENABLE:0);
        say(s,value?"Debug on: C64 turbo/IEC experiments, diagnostics, RAM/REU readback, audio capture and Ultimate Audio status unlocked":"Debug off; release settings restored");
        return true;
    }
    if(!ready(s))return true;
    if(load) {
        if(argc<2 || argc>3 || (argc==3 && !number(args[2],0,MT_C64_REU_DDR_SIZE,&number_value)))say(s,"Usage: :reuload FILE.reu [OFFSET]");
        else (void)mt_c64_debug_load_reu(s,args[1],number_value);
    } else if(!strcmp(command,"uastat")) {
        if(argc!=1)say(s,"Usage: :uastat");else ua_status(s);
    } else if(!strcmp(command,"c64diag")) {
        if(argc!=2 || !onoff(args[1],&value))say(s,"Usage: :c64diag on|off");
        else if(!s->caps.turbo)say(s,"C64 diagnostics not available on this image");
        else {s->armed=value;apply(s,false);say(s,"C64 diagnostics %s",value?"armed":"off");}
    } else if(!strcmp(command,"turbostat")) {
        if(argc!=1)say(s,"Usage: :turbostat");
        else if(!s->caps.turbo)say(s,"C64 turbo not available on this image");
        else {turbo_status(s,true);s->trace=true;}
    } else if(!strcmp(command,"speed")) {
        if(!s->caps.turbo)say(s,"C64 turbo not available on this image");
        else if(argc>2 || (argc==2 && !number(args[1],10,15,&number_value)))say(s,"Usage: :speed [0..15]");
        else {s->override_active=argc==2;s->override_index=(uint8_t)number_value;apply(s,true);say(s,"Speed override %s",s->override_active?"active (not saved)":"cleared");}
    } else if(!strcmp(command,"turbo")) {
        if(argc!=3 || strcmp(args[1],"c64u") || !onoff(args[2],&value))say(s,"Usage: :turbo c64u on|off");
        else if(!(s->caps.cpu&MT_C64_CPU_CAPS_C64U_TOP))say(s,"C64U top-row experiment unavailable");
        else {s->debug_bits=(s->debug_bits&~MT_C64_CPU_CONFIG_C64U_TOP)|(value?MT_C64_CPU_CONFIG_C64U_TOP:0);apply(s,false);}
    } else if(!strcmp(command,"iec")) {
        if(argc!=3)say(s,"Usage: :iec holdoff 500|100|50|20 or :iec release on|off");
        else if(!strcmp(args[1],"release") && onoff(args[2],&value)) {
            if(!(s->caps.cpu2&MT_C64_CPU_CAPS2_STATIC_RELEASE))say(s,"IEC release experiment unavailable");
            else {s->debug_bits=(s->debug_bits&~MT_C64_CPU_CONFIG_STATIC_RELEASE)|(value?MT_C64_CPU_CONFIG_STATIC_RELEASE:0);apply(s,false);}
        } else if(!strcmp(args[1],"holdoff") && number(args[2],10,500,&number_value) &&
                   (number_value==500 || number_value==100 || number_value==50 || number_value==20)) {
            if(!(s->caps.cpu2&MT_C64_CPU_CAPS2_IEC_HOLDOFF))say(s,"IEC holdoff experiment unavailable");
            else {const unsigned field=number_value==500?0:(number_value==100?1:(number_value==50?2:3));
                s->debug_bits=(s->debug_bits&~MT_C64_CPU_CONFIG_HOLDOFF_MASK)|(field<<16);apply(s,false);}
        } else say(s,"Usage: :iec holdoff 500|100|50|20 or :iec release on|off");
    } else if(!strcmp(command,"peek") || !strcmp(command,"ramdump") || !strcmp(command,"reudump")) {
        const bool reu=!strcmp(command,"reudump"),peek=!strcmp(command,"peek");
        const uint32_t size=reu?MT_C64_REU_DDR_SIZE:65536U;uint32_t length=peek?1:0;
        if(argc<2 || argc>3 || (!peek && argc!=3) || !number(args[1],16,size-1,&number_value) ||
           (argc==3 && !number(args[2],0,peek?256U:size,&length)) || !length || length>size-number_value)
            say(s,"Invalid memory range (peek count <=256; main RAM <=65536; REU <=16 MB)");
        else dump(s,reu,number_value,length);
    } else if(!strcmp(command,"audiocap")) {
        if(argc<2 || !number(args[1],10,10,&number_value) || number_value==0){say(s,"Usage: :audiocap 1..10 [NAME|tone F1 F2 ...]");return true;}
        if(s->capture){say(s,"Audio capture already running");return true;}
        s->tone_count=0;const char *name=NULL;
        if(argc>=3 && !strcmp(args[2],"tone")) {
            if(argc<4 || argc>11){say(s,"Supply 1..8 tone frequencies");return true;}
            for(int t=3;t<argc;++t) {
                uint32_t frequency=0;
                if(!number(args[t],10,23999,&frequency) || !frequency){say(s,"Tone frequencies must be 1..23999 Hz");return true;}
                s->tones[s->tone_count++]=frequency;
            }
        } else if(argc==3)name=args[2];else if(argc>3){say(s,"Invalid audio capture arguments");return true;}
        capture_start(s,(unsigned)number_value,name);
    }
    return true;
}
