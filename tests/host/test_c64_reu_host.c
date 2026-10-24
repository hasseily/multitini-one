/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_reu_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t ms;
    unsigned fail_pause, fail_hold, busy, fail_copy, fail_invalidations;
    unsigned writes, invalidates, resumes, copies, barriers;
    uint32_t host, done, copied_offset;
    size_t copied_size;
    bool paused;
    char trace[128]; unsigned length;
} mock_t;
static void event(mock_t *m, char e) { assert(m->length<127); m->trace[m->length++]=e; }
static uint32_t read_host(void *context) {
    mock_t *m=context;
    return m->host | (m->done << 8) | (m->busy ? 0 : MT_C64_REU_HOST_IDLE) |
        ((m->host & MT_C64_REU_HOST_UA_HOLD) && !m->fail_hold ? MT_C64_REU_HOST_UA_HELD : 0);
}
static void write_host(void *context, uint32_t value) {
    mock_t *m=context; m->writes++;
    assert(m->paused);
    if ((m->host & 15) != (value & 15)) {
        event(m,'I'); m->invalidates++; assert(m->barriers==1);
        if (m->invalidates > m->fail_invalidations) m->done=value & 15;
    } else event(m,(value & MT_C64_REU_HOST_UA_HOLD) ? 'H' : 'U');
    m->host=value;
}
static void pause_machine(void *context, bool pause) {
    mock_t *m=context; m->paused=pause;
    event(m,pause ? 'P' : 'R'); if (!pause) m->resumes++;
}
static bool paused(void *context) { mock_t *m=context; return m->paused && !m->fail_pause; }
static uint64_t milliseconds(void *context) { return ((mock_t*)context)->ms; }
static void progress(void *context) { ((mock_t*)context)->ms += 10; }
static bool copy_bytes(void *context, uint32_t offset, size_t size) {
    mock_t *m=context; assert(m->paused && !m->busy && m->invalidates==0);
    event(m,'C'); m->copies++; m->copied_offset=offset; m->copied_size += size;
    return !m->fail_copy;
}
static void drain(void *context) { mock_t *m=context; event(m,'B'); m->barriers++; }
static mt_c64_reu_load_result_t run(mock_t *m, mt_c64_reu_host_t *s, bool ua) {
    mt_c64_reu_host_ops_t o={m,read_host,write_host,pause_machine,paused,milliseconds,progress,copy_bytes,drain};
    return mt_c64_reu_load(s,&o,true,ua,false,false,3,5001);
}
int main(void) {
    mock_t m={0}; mt_c64_reu_host_t s={0};
    assert(run(&m,&s,true)==MT_REU_LOAD_OK);
    assert(!strcmp(m.trace,"PHCCBIUR") && m.copied_size==5001 && m.copied_offset==4099);
    assert(m.resumes==1 && m.invalidates==1 && m.barriers==1 && !s.reload_required);
    memset(&m,0,sizeof m); m.fail_invalidations=1;
    assert(run(&m,&s,true)==MT_REU_LOAD_OK);
    assert(!strcmp(m.trace,"PHCCBIIUR") && m.ms>=100 && m.ms<200);
    memset(&m,0,sizeof m); m.fail_invalidations=2;
    assert(run(&m,&s,true)==MT_REU_LOAD_RELOAD_REQUIRED);
    assert(!strcmp(m.trace,"PHCCBII") && s.reload_required && m.paused && m.resumes==0);
    assert(m.host & MT_C64_REU_HOST_UA_HOLD);
    unsigned writes=m.writes;
    assert(run(&m,&s,true)==MT_REU_LOAD_RELOAD_REQUIRED && m.writes==writes);
    memset(&m,0,sizeof m); s.reload_required=false; m.fail_copy=1;
    assert(run(&m,&s,true)==MT_REU_LOAD_PARTIAL);
    assert(!strcmp(m.trace,"PHCBIUR") && !s.reload_required);
    memset(&m,0,sizeof m); m.fail_copy=1; m.fail_invalidations=2;
    assert(run(&m,&s,true)==MT_REU_LOAD_RELOAD_REQUIRED);
    assert(!strcmp(m.trace,"PHCBII") && s.reload_required && m.paused);
    for (unsigned cause=0; cause<3; ++cause) {
        memset(&m,0,sizeof m); s.reload_required=false;
        if (cause==0) m.fail_pause=1;
        if (cause==1) m.fail_hold=1;
        if (cause==2) m.busy=1;
        assert(run(&m,&s,true)==(cause==0 ? MT_REU_LOAD_PAUSE_TIMEOUT :
            cause==1 ? MT_REU_LOAD_HOLD_TIMEOUT : MT_REU_LOAD_REU_BUSY));
        assert(!m.copies && !m.barriers && !m.invalidates && m.resumes==1 && !m.paused);
        assert(!(m.host & MT_C64_REU_HOST_UA_HOLD));
    }
    memset(&m,0,sizeof m); assert(run(&m,&s,false)==MT_REU_LOAD_OK);
    assert(!strcmp(m.trace,"PCCBIR"));
    memset(&m,0,sizeof m); m.fail_invalidations=2;
    assert(run(&m,&s,false)==MT_REU_LOAD_RELOAD_REQUIRED);
    assert(!strcmp(m.trace,"PCCBII") && !(m.host & MT_C64_REU_HOST_UA_HOLD));
    memset(&m,0,sizeof m); s.reload_required=false;
    mt_c64_reu_host_ops_t o={&m,read_host,write_host,pause_machine,paused,milliseconds,progress,copy_bytes,drain};
    assert(mt_c64_reu_load(&s,&o,false,true,false,false,0,1)==MT_REU_LOAD_UNAVAILABLE);
    assert(mt_c64_reu_load(&s,&o,true,true,true,false,0,1)==MT_REU_LOAD_CARTRIDGE);
    assert(mt_c64_reu_load(&s,&o,true,true,false,true,0,1)==MT_REU_LOAD_BUSY);
    assert(mt_c64_reu_load(&s,&o,true,true,false,false,0x1000000,1)==MT_REU_LOAD_RANGE);
    assert(mt_c64_reu_load(&s,&o,true,true,false,false,0xffffffff,1)==MT_REU_LOAD_RANGE);
    assert(mt_c64_reu_load(&s,&o,true,true,false,false,0,SIZE_MAX)==MT_REU_LOAD_RANGE);
    assert(m.length==0 && m.writes==0);
    puts("PASS REU preload order, partial copy, all timeouts, one retry, reload latch and bounds");
}
