/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_uci.h"
#include <string.h>

void mt_uci_reply_clear(mt_uci_reply_t *r) { memset(r,0,sizeof(*r)); }
void mt_uci_reply_status(mt_uci_reply_t *r,const char *s)
{
    size_t n=s?strlen(s):0;if(n>sizeof(r->status))n=sizeof(r->status);
    if(n)memcpy(r->status,s,n);
    r->status_length=n;
}
void mt_uci_reply_text(mt_uci_reply_t *r,const char *data,const char *status)
{
    mt_uci_reply_clear(r);size_t n=data?strlen(data):0;
    if(n>sizeof(r->data))n=sizeof(r->data);
    if(n)memcpy(r->data,data,n);
    r->data_length=n;mt_uci_reply_status(r,status);
}
static uint32_t rd(mt_uci_engine_t *e,uint32_t a){return e->link->read32(e->link->context,a);}
static void wr(mt_uci_engine_t *e,uint32_t a,uint32_t d){e->link->write32(e->link->context,a,d);}
static void queue_op(mt_uci_engine_t *e,unsigned op,size_t data,size_t status)
{
    e->pending_op=(uint32_t)op|((uint32_t)data<<4)|((uint32_t)status<<16)|
        ((uint32_t)((e->snapshot>>18)&15)<<25)|((uint32_t)((e->snapshot>>22)&7)<<29);
    e->op_pending=true;
}
static void write_bytes(mt_uci_engine_t *e,unsigned offset,const uint8_t *p,size_t n)
{
    for(size_t i=0;i<n;i+=4){
        uint32_t word=0;
        for(size_t j=0;j<4&&i+j<n;j++)word|=(uint32_t)p[i+j]<<(8*j);
        wr(e,MT_C64_UCI_BUFFER_OFFSET+offset+(uint32_t)i,word);
    }
}
static void publish(mt_uci_engine_t *e)
{
    mt_uci_reply_t *r=&e->reply;
    if(r->data_length>sizeof(r->data)||r->status_length>sizeof(r->status))
        mt_uci_reply_text(r,NULL,"87,INTERNAL ERROR");
    if(e->no_reply) {
        /* A suppressed streaming reply still owns target buffers/socket.
         * Finish bounded cancellation before acknowledging RESET. */
        if(r->more)e->reset_pending=true;
        else queue_op(e,MT_C64_UCI_OP_RESET,0,0);
    }
    else {
        write_bytes(e,MT_C64_UCI_REPLY_OFFSET,r->data,r->data_length);
        write_bytes(e,MT_C64_UCI_TEXT_OFFSET,r->status,r->status_length);
        /* The platform link maps ordered device memory. A read on the same
         * slave completes the buffer writes before the operation is sent. */
        (void)rd(e,MT_REG_C64_UCI_STATUS);
        queue_op(e,r->more?MT_C64_UCI_OP_MORE:MT_C64_UCI_OP_LAST,r->data_length,r->status_length);
    }
    e->reply_pending=false;e->waiting=false;
}
static void result(mt_uci_engine_t *e,mt_uci_result_t r)
{
    if(r==MT_UCI_FAILED){mt_uci_reply_text(&e->reply,NULL,"87,INTERNAL ERROR");r=MT_UCI_REPLY;}
    e->waiting=r==MT_UCI_WAIT;e->reply_pending=r==MT_UCI_REPLY;
}
void mt_uci_engine_init(mt_uci_engine_t *e,mt_core_link_t *link)
{memset(e,0,sizeof(*e));e->link=link;}
void mt_uci_engine_target(mt_uci_engine_t *e,unsigned id,const mt_uci_target_t *target)
{if(e&&id<MT_UCI_TARGET_COUNT){if(target)e->targets[id]=*target;else memset(&e->targets[id],0,sizeof(e->targets[id]));}}

bool mt_uci_engine_quiesce(mt_uci_engine_t *e)
{
    if(!e)return true;
    if(e->faulted)return false;
    if(e->active){
        mt_uci_target_t *t=&e->targets[e->target];
        mt_uci_result_t r=t->abort?t->abort(t->context,e->no_reply?0:e->response_consumed):MT_UCI_REPLY;
        if(r==MT_UCI_FAILED){e->faulted=true;return false;}
        if(r==MT_UCI_WAIT)return false;
    }
    for(unsigned i=0;i<MT_UCI_TARGET_COUNT;i++)if(e->targets[i].reset)e->targets[i].reset(e->targets[i].context);
    e->active=false;e->waiting=false;e->reply_pending=false;e->op_pending=false;
    e->canceling=false;e->reset_pending=false;e->initialized=false;e->awaiting_ack=false;
    return true;
}

void mt_uci_engine_poll(mt_uci_engine_t *e)
{
    if(!e||e->faulted||!e->link||!e->link->read32||!e->link->write32)return;
    e->snapshot=rd(e,MT_REG_C64_UCI_STATUS);
    e->snapshot2=rd(e,MT_REG_C64_UCI_STATUS2);
    uint32_t s=e->snapshot,two=e->snapshot2;
    unsigned state=s&3,seq=(s>>18)&15,epoch=(s>>22)&15,ack=(two>>19)&3;
    e->response_consumed=two&1023;
    if(!(s&MT_C64_UCI_STATUS_VALID)||!(s&MT_C64_UCI_STATUS_ENABLED)||
        (s&MT_C64_UCI_STATUS_SUSPENDED)){
        (void)mt_uci_engine_quiesce(e);return;
    }
    if(e->initialized && e->epoch!=epoch){
        if(!mt_uci_engine_quiesce(e))return;
    }
    if(!e->initialized){e->initialized=true;e->epoch=(uint8_t)epoch;e->sequence=(uint8_t)((seq+15)&15);}
    if(e->awaiting_ack){
        if(ack==e->op_ack)return;
        e->awaiting_ack=false;
        /* A reset/new PUSH may obsolete an op in transit. Re-read current
         * state on the next pass instead of committing saved handler output. */
        if(two&(1U<<21)){e->reply_pending=false;e->waiting=false;return;}
    }
    bool abort_pending=(s&4)!=0,idle_abort=(s&MT_C64_UCI_STATUS_IDLE_ABORT)!=0;
    if(abort_pending||idle_abort||e->canceling||e->reset_pending){
        e->canceling=true;
        mt_uci_target_t *target=&e->targets[e->target];
        mt_uci_result_t canceled=e->active&&target->abort?
            target->abort(target->context,e->no_reply?0:e->response_consumed):MT_UCI_REPLY;
        if(canceled==MT_UCI_FAILED){e->faulted=true;return;}
        if(canceled==MT_UCI_WAIT)return;
        e->canceling=false;e->active=false;e->waiting=false;e->reply_pending=false;
        queue_op(e,abort_pending?MT_C64_UCI_OP_ABORT_DONE:idle_abort?
                 MT_C64_UCI_OP_IDLE_ABORT_DONE:MT_C64_UCI_OP_RESET,0,0);
        e->reset_pending=false;
    } else if(!e->op_pending){
        bool command_busy=(two&MT_C64_UCI_STATUS2_CMD_BUSY)!=0;
        if(state==1&&command_busy&&(!e->active||seq!=e->sequence)){
            if(e->active){
                mt_uci_target_t *previous=&e->targets[e->target];
                mt_uci_result_t canceled=previous->abort?
                    previous->abort(previous->context,e->no_reply?0:e->response_consumed):MT_UCI_REPLY;
                if(canceled==MT_UCI_FAILED){e->faulted=true;return;}
                if(canceled==MT_UCI_WAIT)return;
                e->active=false;
            }
            size_t length=(s>>8)&1023;
            if(length>sizeof(e->command))length=sizeof(e->command);
            for(size_t i=0;i<length;i+=4){
                uint32_t word=rd(e,MT_C64_UCI_BUFFER_OFFSET+(uint32_t)i);
                for(size_t j=0;j<4&&i+j<length;j++)e->command[i+j]=(uint8_t)(word>>(j*8));
            }
            uint32_t check=rd(e,MT_REG_C64_UCI_STATUS);
            if(((check^s)&0x03FC0000U)||(check&4))return;
            e->sequence=(uint8_t)seq;e->target=length?e->command[0]&15:0;
            e->no_reply=length&&(e->command[0]&128)!=0;e->active=true;
            mt_uci_reply_clear(&e->reply);
            if(two&(1U<<23)){
                mt_uci_reply_status(&e->reply,"21,UNKNOWN COMMAND");e->reply_pending=true;
            }else if(length==0)e->reply_pending=true;
            else if(length<2){mt_uci_reply_status(&e->reply,"21,UNKNOWN COMMAND");e->reply_pending=true;}
            else {
                mt_uci_target_t *target=&e->targets[e->target];
                if(target->begin)result(e,target->begin(target->context,e->command,length,&e->reply));
                else {mt_uci_reply_text(&e->reply,e->command[1]==1?"NO TARGET":NULL,
                        e->command[1]==1?"00,OK":"21,UNKNOWN COMMAND");e->reply_pending=true;}
            }
        }else if(e->active&&e->waiting){
            mt_uci_target_t *target=&e->targets[e->target];
            result(e,target->poll?target->poll(target->context,&e->reply):MT_UCI_FAILED);
        }else if(e->active&&state==1&&(s&8)&&!e->reply_pending){
            mt_uci_target_t *target=&e->targets[e->target];
            mt_uci_reply_clear(&e->reply);
            result(e,target->next?target->next(target->context,e->response_consumed,&e->reply):MT_UCI_REPLY);
            queue_op(e,MT_C64_UCI_OP_NEXT,0,0);
        }
        if(e->reply_pending&&!e->op_pending)publish(e);
    }
    if(e->op_pending&&!(s&MT_C64_UCI_STATUS_HANDSHAKE_BUSY)){
        e->op_ack=(uint8_t)ack;e->awaiting_ack=true;e->op_pending=false;
        wr(e,MT_REG_C64_UCI_HANDSHAKE,e->pending_op);
    }
}
