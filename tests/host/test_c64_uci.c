/* SPDX-License-Identifier: GPL-3.0-only */
/* Host MMIO model exercises production engine independently of its RTL.
 * Delayed operation ACK and CPU actions are explicit, never auto-success. */
#include "multitini_c64_uci.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t s,s2,op;uint8_t memory[2048];unsigned reads,writes,ops,pending,delay,mutate_read;bool drop;} bus_t;
typedef struct {unsigned begins,polls,nexts,aborts,resets,wait,abort_wait;size_t length,consumed;uint8_t saved[896];bool more,failed,abort_failed;} target_t;
static uint32_t read32(void *context,uint32_t address){bus_t *b=context;b->reads++;if(address==MT_REG_C64_UCI_STATUS)return b->s;if(address==MT_REG_C64_UCI_STATUS2)return b->s2;
    assert(address>=MT_C64_UCI_BUFFER_OFFSET&&address+4<=MT_C64_UCI_BUFFER_OFFSET+2048);unsigned at=address-MT_C64_UCI_BUFFER_OFFSET;uint32_t v=(uint32_t)b->memory[at]|((uint32_t)b->memory[at+1]<<8)|((uint32_t)b->memory[at+2]<<16)|((uint32_t)b->memory[at+3]<<24);
    if(b->mutate_read){b->mutate_read--;b->s^=1U<<22;}return v;
}
static void write32(void *context,uint32_t address,uint32_t value){bus_t *b=context;b->writes++;if(address==MT_REG_C64_UCI_HANDSHAKE){assert(!b->pending);b->op=value;b->pending=b->delay?b->delay:1;b->ops++;b->s|=MT_C64_UCI_STATUS_HANDSHAKE_BUSY;return;}
    assert(address>=MT_C64_UCI_BUFFER_OFFSET+896&&address+4<=MT_C64_UCI_BUFFER_OFFSET+2048);unsigned at=address-MT_C64_UCI_BUFFER_OFFSET;for(unsigned i=0;i<4;i++)b->memory[at+i]=(uint8_t)(value>>(8*i));
}
static void hardware(bus_t *b){if(!b->pending||--b->pending)return;unsigned ack=((b->s2>>19)+1)&3;b->s2=(b->s2&~(3U<<19))|(ack<<19);b->s&=~MT_C64_UCI_STATUS_HANDSHAKE_BUSY;
    if(b->drop){b->s2|=1U<<21;b->drop=false;return;}b->s2&=~(1U<<21);
    unsigned op=b->op&15;switch(op){case MT_C64_UCI_OP_LAST:case MT_C64_UCI_OP_MORE:b->s=(b->s&~3U)|(op==MT_C64_UCI_OP_LAST?3:2);b->s2&=~MT_C64_UCI_STATUS2_CMD_BUSY;break;
    case MT_C64_UCI_OP_NEXT:b->s&=~8U;b->s2&=~1023U;break;
    case MT_C64_UCI_OP_ABORT_DONE:b->s&=~7U;b->s2&=~MT_C64_UCI_STATUS2_CMD_BUSY;break;
    case MT_C64_UCI_OP_IDLE_ABORT_DONE:b->s&=~MT_C64_UCI_STATUS_IDLE_ABORT;break;
    case MT_C64_UCI_OP_RESET:b->s&=~15U;b->s2&=~MT_C64_UCI_STATUS2_CMD_BUSY;break;
    default:assert(0);}
}
static mt_uci_result_t begin(void *context,const uint8_t c[896],size_t length,mt_uci_reply_t *r){target_t *t=context;t->begins++;t->length=length;memcpy(t->saved,c,896);if(t->failed)return MT_UCI_FAILED;if(t->wait)return MT_UCI_WAIT;mt_uci_reply_text(r,"hello","00,OK");r->more=t->more;return MT_UCI_REPLY;}
static mt_uci_result_t poll_target(void *context,mt_uci_reply_t *r){target_t *t=context;t->polls++;if(t->wait&&--t->wait)return MT_UCI_WAIT;mt_uci_reply_text(r,"later","00,OK");r->more=t->more;return MT_UCI_REPLY;}
static mt_uci_result_t next(void *context,size_t consumed,mt_uci_reply_t *r){target_t *t=context;t->nexts++;t->consumed=consumed;mt_uci_reply_text(r,"last","00,OK");return MT_UCI_REPLY;}
static mt_uci_result_t abort_target(void *context,size_t consumed){target_t *t=context;t->aborts++;t->consumed=consumed;if(t->abort_failed)return MT_UCI_FAILED;if(t->abort_wait&&--t->abort_wait)return MT_UCI_WAIT;return MT_UCI_REPLY;}
static void reset(void *context){((target_t*)context)->resets++;}
static void setup(bus_t *b,mt_uci_engine_t *e,mt_core_link_t *link,target_t *t){memset(b,0,sizeof(*b));memset(t,0,sizeof(*t));b->s=MT_C64_UCI_STATUS_VALID|MT_C64_UCI_STATUS_ENABLED;link->context=b;link->read32=read32;link->write32=write32;mt_uci_engine_init(e,link);mt_uci_target_t target={t,begin,poll_target,next,abort_target,reset};mt_uci_engine_target(e,3,&target);}
static void push(bus_t *b,const uint8_t *command,size_t length){unsigned seq=(((b->s>>18)&15)+1)&15;memcpy(b->memory,command,length);b->s=(b->s&~(0x3ff00U|(15U<<18)|15U))|1|((uint32_t)length<<8)|(seq<<18);b->s2|=MT_C64_UCI_STATUS2_CMD_BUSY;}
static void steps(bus_t *b,mt_uci_engine_t *e,unsigned count){while(count--){unsigned before=b->reads+b->writes;mt_uci_engine_poll(e);assert(b->reads+b->writes-before<600);hardware(b);}}
int main(void){bus_t b;mt_uci_engine_t e;mt_core_link_t link;target_t t;uint8_t c[896]={3,1,0x12,0x34};
    setup(&b,&e,&link,&t);b.s&=~MT_C64_UCI_STATUS_ENABLED;steps(&b,&e,4096);assert(t.resets==1&&!t.aborts);assert(mt_uci_engine_quiesce(&e)&&t.resets==1);
    b.s|=MT_C64_UCI_STATUS_ENABLED;t.wait=100;push(&b,c,2);steps(&b,&e,1);b.s&=~MT_C64_UCI_STATUS_ENABLED;steps(&b,&e,4096);assert(t.resets==2&&t.aborts==1&&!e.active);
    setup(&b,&e,&link,&t);b.delay=5;push(&b,c,4);steps(&b,&e,1);assert(t.begins==1&&b.pending==4&&b.ops==1&&(b.op&15)==MT_C64_UCI_OP_LAST);assert(!memcmp(b.memory+896,"hello",5));steps(&b,&e,12);assert(t.begins==1&&b.ops==1&&(b.s&3)==3);
    /* Per-target fixed parameter reads retain only previous submitted bytes. */
    b.s&=~3U;c[2]=0x56;push(&b,c,3);steps(&b,&e,12);assert(t.begins==2&&t.length==3&&t.saved[2]==0x56&&t.saved[3]==0x34);
    setup(&b,&e,&link,&t);t.wait=4;push(&b,c,2);steps(&b,&e,3);assert(t.begins==1&&t.polls==2&&b.ops==0);steps(&b,&e,4);assert(t.polls==4&&b.ops==1);
    setup(&b,&e,&link,&t);t.more=true;push(&b,c,2);steps(&b,&e,3);assert((b.s&3)==2);b.s=(b.s&~3U)|1|8;b.s2=(b.s2&~1023U)|3;steps(&b,&e,5);assert(t.nexts==1&&t.consumed==3&&(b.s&3)==3&&b.ops==3);
    setup(&b,&e,&link,&t);t.wait=100;t.abort_wait=3;push(&b,c,2);steps(&b,&e,1);b.s|=4;b.s2|=7;steps(&b,&e,2);assert(t.aborts==2&&!b.ops);steps(&b,&e,3);assert(t.aborts==3&&t.consumed==7&&(b.s&3)==0&&(b.op&15)==3);
    setup(&b,&e,&link,&t);t.wait=100;push(&b,c,2);steps(&b,&e,1);b.s^=1U<<22;b.s&=~3U;b.s2=0;steps(&b,&e,1);assert(t.aborts==1&&t.resets==1&&!e.active);
    setup(&b,&e,&link,&t);b.mutate_read=1;push(&b,c,2);steps(&b,&e,1);assert(t.begins==0&&b.ops==0);steps(&b,&e,3);assert(t.begins==1);
    setup(&b,&e,&link,&t);b.delay=3;push(&b,c,2);steps(&b,&e,1);b.s|=4;b.drop=true;steps(&b,&e,8);assert(t.aborts==1&&(b.s&3)==0&&b.ops==2);
    setup(&b,&e,&link,&t);b.s|=MT_C64_UCI_STATUS_IDLE_ABORT;steps(&b,&e,3);assert(!t.aborts&&b.ops==1&&(b.op&15)==6);
    setup(&b,&e,&link,&t);c[0]=0x83;push(&b,c,2);steps(&b,&e,3);assert(t.begins==1&&(b.op&15)==15&&(b.s&3)==0);
    setup(&b,&e,&link,&t);c[0]=7;push(&b,c,2);steps(&b,&e,3);assert(!t.begins&&!memcmp(b.memory+896,"NO TARGET",9));
    setup(&b,&e,&link,&t);c[0]=3;push(&b,c,2);b.s2|=1U<<23;steps(&b,&e,3);assert(!t.begins&&!memcmp(b.memory+1792,"21,UNKNOWN COMMAND",18));
    setup(&b,&e,&link,&t);push(&b,c,0);steps(&b,&e,3);assert(!t.begins&&(b.op&15)==4&&((b.op>>4)&1023)==0);
    setup(&b,&e,&link,&t);t.failed=true;push(&b,c,2);steps(&b,&e,3);assert(!memcmp(b.memory+1792,"87,INTERNAL ERROR",17));
    setup(&b,&e,&link,&t);t.wait=100;push(&b,c,2);steps(&b,&e,1);t.abort_wait=2;assert(!mt_uci_engine_quiesce(&e));assert(mt_uci_engine_quiesce(&e)&&!e.active&&t.resets==1);
    setup(&b,&e,&link,&t);c[0]=0x83;t.more=true;t.abort_wait=3;push(&b,c,2);steps(&b,&e,1);assert(e.reset_pending&&b.ops==0);steps(&b,&e,2);assert(t.aborts==2&&b.ops==0);steps(&b,&e,3);assert(t.aborts==3&&t.consumed==0&&b.ops==1&&(b.op&15)==15&&!e.active);
    setup(&b,&e,&link,&t);c[0]=3;push(&b,c,2);steps(&b,&e,3);b.s2|=4;t.abort_wait=3;c[0]=7;push(&b,c,2);steps(&b,&e,2);assert(t.aborts==2&&e.target==3&&b.ops==1);steps(&b,&e,3);assert(t.aborts==3&&t.consumed==4&&e.target==7&&b.ops==2);
    setup(&b,&e,&link,&t);c[0]=3;t.wait=100;t.abort_failed=true;push(&b,c,2);steps(&b,&e,1);b.s|=4;steps(&b,&e,4);assert(e.faulted&&e.active&&t.aborts==1&&b.ops==0);assert(!mt_uci_engine_quiesce(&e)&&t.resets==0);
    setup(&b,&e,&link,&t);push(&b,c,2);steps(&b,&e,3);t.abort_failed=true;assert(!mt_uci_engine_quiesce(&e)&&e.faulted&&t.resets==0&&e.active);
    puts("PASS UCI engine delayedACK/WAIT/MORE/consumedABORT/staleparameters/resetepoch/drop/idleabort/noReply/unknown/overflow/quiesce/boundedMMIO");return 0;
}
