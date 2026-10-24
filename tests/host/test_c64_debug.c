/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_debug.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    mt_c64_debug_t service;
    uint32_t regs[128];unsigned reads,writes,pauses,opens,closes,file_writes,allocs,releases,applies;
    uint64_t clock;bool paused,cartridge,busy,reu_on,stuck_invalidate,fail_copy,overflow;
    uint32_t peek,host;size_t size,copy_bytes,capture_pop,wav_size;
    unsigned drains,host_reads;uint8_t reu[8192],source[8192],wav_header[44];
    char output[8192];
} mock_t;
static uint32_t read32(void *context,uint32_t offset)
{
    mock_t *m=context;++m->reads;
    if(offset==MT_REG_C64_REU_HOST){++m->host_reads;return m->host|MT_C64_REU_HOST_IDLE|((m->host&MT_C64_REU_HOST_UA_HOLD)?MT_C64_REU_HOST_UA_HELD:0);}
    if(offset==MT_REG_C64_RAM_PEEK)return (m->peek&MT_C64_RAM_PEEK_COUNTER_MASK)|((m->peek^0x55U)&255U);
    if(offset==MT_REG_DEBUG_STATUS)return 512U|MT_DEBUG_STATUS_AUDIO_PRESENT|(m->overflow?MT_DEBUG_STATUS_OVERFLOW:0);
    if(offset==MT_REG_AUDIO_CAPTURE_DATA){++m->capture_pop;return UINT32_C(0x80007fff);}
    return m->regs[offset/4];
}
static void write32(void *context,uint32_t offset,uint32_t value)
{
    mock_t *m=context;++m->writes;m->regs[offset/4]=value;
    if(offset==MT_REG_C64_RAM_PEEK)m->peek=value;
    if(offset==MT_REG_C64_REU_HOST){m->host=value;if(!m->stuck_invalidate)m->host|=(value&15U)<<8;}
}
static uint64_t milliseconds(void *context){return ((mock_t *)context)->clock;}
static void progress(void *context){++((mock_t *)context)->clock;}
static void pause_machine(void *context,bool value){mock_t *m=context;m->paused=value;++m->pauses;}
static bool paused(void *context){return ((mock_t *)context)->paused;}
static void output(void *context,const char *text)
{
    mock_t *m=context;size_t used=strlen(m->output),len=strlen(text);
    if(used+len+2<sizeof m->output){memcpy(m->output+used,text,len);m->output[used+len]='\n';m->output[used+len+1]=0;}
}
static void apply_turbo(void *context,bool apply)
{
    mock_t *m=context;++m->applies;
    const uint32_t previous=m->regs[MT_REG_C64_CPU_CONFIG/4];
    m->regs[MT_REG_C64_CPU_CONFIG/4]=mt_c64_debug_effective_overlay(&m->service,MT_C64_CPU_CONFIG_DEFAULT,previous,apply);
}
static bool cartridge(void *c){return ((mock_t *)c)->cartridge;}
static bool content_busy(void *c){return ((mock_t *)c)->busy;}
static bool reu_enabled(void *c){return ((mock_t *)c)->reu_on;}
static bool file_open(void *c,const char *name,size_t *size){mock_t *m=c;assert(!strcmp(name,"test file.reu"));++m->opens;*size=m->size;return true;}
static bool file_read(void *c,size_t offset,void *data,size_t size){mock_t *m=c;if(m->fail_copy)return false;assert(offset+size<=sizeof m->source);memcpy(data,m->source+offset,size);return true;}
static void file_close(void *c){++((mock_t *)c)->closes;}
static bool file_write(void *c,const char *name,const void *data,size_t size)
{
    mock_t *m=c;++m->file_writes;assert(strstr(name,"0:/multitini/capture/")==name);
    assert(size>=44);m->wav_size=size;memcpy(m->wav_header,data,44);
    const uint8_t *p=data;assert(p[44]==0 && p[45]==0x80 && p[46]==0xff && p[47]==0x7f);return true;
}
static bool reu_read(void *c,uint32_t offset,void *data,size_t size){mock_t *m=c;assert(offset+size<=sizeof m->reu);memcpy(data,m->reu+offset,size);return true;}
static bool reu_write(void *c,uint32_t offset,const void *data,size_t size){mock_t *m=c;assert(offset+size<=sizeof m->reu);memcpy(m->reu+offset,data,size);m->copy_bytes+=size;return true;}
static void drain(void *c){++((mock_t *)c)->drains;}
static void *allocate(void *c,size_t size){++((mock_t *)c)->allocs;return malloc(size);}
static void release(void *c,void *data){++((mock_t *)c)->releases;free(data);}
static void command(mock_t *m,const char *line){assert(mt_c64_debug_command(&m->service,line));}
static void setup(mock_t *m)
{
    memset(m,0,sizeof *m);m->size=4097;
    for(unsigned i=0;i<sizeof m->source;++i){m->source[i]=(uint8_t)(i^0x73U);m->reu[i]=0xa5;}
    const mt_c64_debug_ops_t ops={m,read32,write32,milliseconds,progress,pause_machine,paused,output,apply_turbo,
        cartridge,content_busy,reu_enabled,file_open,file_read,file_close,file_write,reu_read,reu_write,drain,allocate,release};
    mt_c64_debug_init(&m->service,&ops,false);
    const mt_c64_capabilities_t caps={UINT32_C(0x8000001f),UINT32_C(0xbfffffff),UINT32_C(0x80003f01),true,true,true,true,true};
    mt_c64_debug_activate(&m->service,&caps,1);
}
int main(void)
{
    mock_t m;setup(&m);
    const char *debug_commands[]={":c64diag on",":uastat",":turbostat",":speed 15",":turbo c64u on",":iec holdoff 20",":iec release on",":peek 1000 256",":ramdump 0000 65536",":reudump 0000 64",":audiocap 1 tone 1953 1967"};
    unsigned reads=m.reads,writes=m.writes,pauses=m.pauses,opens=m.opens;
    for(unsigned i=0;i<sizeof debug_commands/sizeof debug_commands[0];++i)command(&m,debug_commands[i]);
    mt_c64_debug_tick(&m.service);
    assert(m.reads==reads && m.writes==writes && m.pauses==pauses && m.opens==opens && !m.service.capture);
    assert(strstr(m.output,"debug is off; send :debug on"));
    command(&m,":debug on");command(&m,":c64diag on");command(&m,":speed 15");
    assert(m.service.armed && m.service.override_active && (m.regs[MT_REG_C64_CPU_CONFIG/4]&0x3fU)==0x1fU);
    const uint32_t applied=m.regs[MT_REG_C64_CPU_CONFIG/4];command(&m,":speed 15");assert(m.regs[MT_REG_C64_CPU_CONFIG/4]==applied);
    command(&m,":turbo c64u on");command(&m,":iec holdoff 20");command(&m,":iec release on");
    command(&m,":uastat");command(&m,":turbostat");
    const unsigned host_reads=m.host_reads;m.service.caps.reu_host=false;
    command(&m,":uastat");assert(m.host_reads==host_reads);m.service.caps.reu_host=true;
    assert(m.service.trace && (m.service.debug_bits&MT_C64_CPU_CONFIG_STATIC_RELEASE));
    command(&m,":peek 1234 3");assert(!m.paused && strstr(m.output,"001234: 61 60 63"));
    reads=m.reads;writes=m.writes;pauses=m.pauses;
    command(&m,":peek ffff 2");command(&m,":peek 0000 257");command(&m,":ramdump 0 65537");
    assert(m.reads==reads && m.writes==writes && m.pauses==pauses);
    command(&m,":reudump 10 16");assert(!m.paused);
    command(&m,":audiocap 1 test");assert(m.service.capture);
    command(&m,":debug on");assert(m.regs[MT_REG_DEBUG_CONTROL/4]&MT_DEBUG_CONTROL_AUDIO_RUN);
    while(m.service.capture)mt_c64_debug_tick(&m.service);
    assert(m.capture_pop==48000 && m.wav_size==192044 && m.file_writes==1 && m.releases==1);
    assert(!memcmp(m.wav_header,"RIFF",4) && !memcmp(m.wav_header+8,"WAVEfmt ",8));
    assert(m.wav_header[22]==2 && m.wav_header[34]==16 && strstr(m.output,"frames=48000 overflow=0 peak=32768"));
    command(&m,":audiocap 1");m.overflow=true;while(m.service.capture)mt_c64_debug_tick(&m.service);
    assert(strstr(m.output,"Audio capture INVALID"));m.overflow=false;
    command(&m,":audiocap 10 cancelled");assert(m.service.capture);command(&m,":debug off");
    assert(!m.service.capture && !m.service.session && !m.service.armed && !m.service.override_active && !m.service.trace && m.service.debug_bits==0);
    assert(m.regs[MT_REG_DEBUG_CONTROL/4]==0 && (m.regs[MT_REG_C64_CPU_CONFIG/4]&MT_C64_CPU_CONFIG_DEBUG_MASK)==0);
    assert(m.releases==3);
    /* Public preload works with debug off, retains neighbouring bytes and
     * reports disabled RAM expansion without changing its configuration. */
    command(&m,":reuload \"test file.reu\" 31");
    assert(m.copy_bytes==4097 && m.drains==1 && m.closes==1 && !m.paused && m.reu[30]==0xa5 && m.reu[4128]==0xa5);
    assert(!memcmp(m.reu+31,m.source,4097));
    assert(strstr(m.output,"turn on RAM expansion to use it"));
    m.cartridge=true;opens=m.opens;writes=m.writes;command(&m,":reuload \"test file.reu\"");assert(m.opens==opens && m.writes==writes);m.cartridge=false;
    m.stuck_invalidate=true;command(&m,":reuload \"test file.reu\"");
    assert(m.service.reu_host.reload_required && m.paused && (m.host&MT_C64_REU_HOST_UA_HOLD));
    writes=m.writes;opens=m.opens;command(&m,":reuload \"test file.reu\"");assert(m.writes==writes && m.opens==opens);
    command(&m,":debug on");command(&m,":c64diag on");
    mt_c64_debug_deactivate(&m.service);assert(m.service.session && m.service.armed && !m.service.active);
    mt_c64_capabilities_t old={0};reads=m.reads;writes=m.writes;mt_c64_debug_activate(&m.service,&old,0);
    command(&m,":uastat");command(&m,":peek 0");command(&m,":speed 1");assert(m.reads==reads && m.writes==writes);
    /* A synthetic exactly-bin-centred tone has known Goertzel energy. */
    uint8_t pcm[4800*4];
    for(unsigned n=0;n<4800;++n){const int16_t sample=(int16_t)lround(12000.0*sin(6.283185307179586*1000.0*n/48000.0));
        pcm[n*4]=(uint8_t)sample;pcm[n*4+1]=(uint8_t)((uint16_t)sample>>8);pcm[n*4+2]=pcm[n*4];pcm[n*4+3]=pcm[n*4+1];}
    const double energy=mt_c64_debug_goertzel(pcm,4800,1000),noise=mt_c64_debug_goertzel(pcm,4800,1200);
    assert(fabs(energy/36000000.0-1.0)<0.001 && noise<energy*1e-8);
    puts("PASS: C64 debug gating, overlay, peek, preload, capture, WAV and Goertzel");return 0;
}
