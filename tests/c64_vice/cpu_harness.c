/* SPDX-License-Identifier: GPL-3.0-only
 * Host adapter for the unchanged VICE3.10 cycle-exact CPU. CPU execution,
 * fetches, interrupt polling/acknowledgement and steal handling come from
 * pinned VICE bodies. Flat RAM, pin scheduling, trace and inactive host
 * callbacks are test infrastructure. No ROM or third-party program is used.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <limits.h>
typedef uint64_t CLOCK;
#define CLOCK_MAX UINT64_MAX
#include "6510core.h"
#define OPINFO_NUMBER(x) ((x)&255)
#include "interrupt.inc"
static CLOCK maincpu_clk,stolen_cycles;
static unsigned int last_opcode_info;
static interrupt_cpu_status_t status,*maincpu_int_status=&status;
static int maincpu_ba_low_flags,check_ba_low,maincpu_alarm_context;
#define MAINCPU_BA_LOW_VICII 1
#define MAINCPU_BA_LOW_REU 2
static unsigned char memory[65536], controls[4096];
static FILE *output;
static jmp_buf done;
static int sync_pending,ready=1;
static unsigned int address_register;
static void pins(void) {
 unsigned c=controls[maincpu_clk];ready=(c>>2)&1;
 interrupt_set_irq(&status,0,(c>>1)&1,maincpu_clk);
 interrupt_set_nmi(&status,0,c&1,maincpu_clk);
 maincpu_ba_low_flags=ready?0:MAINCPU_BA_LOW_VICII;
}
static void vicii_steal_cycles(void) {while(!ready){if(++maincpu_clk>=2048)longjmp(done,2);pins();}}
static void reu_dma_start(void){abort();}
static CLOCK alarm_context_next_pending_clk(int a){(void)a;return CLOCK_MAX;}
static void alarm_context_dispatch(int a,CLOCK t){(void)a;(void)t;abort();}
void interrupt_log_wrong_nirq(void){abort();}
void interrupt_log_wrong_nnmi(void){abort();}
void interrupt_fixup_int_clk(interrupt_cpu_status_t *cs,CLOCK t,CLOCK *o){(void)cs;*o=t;}
#include "cpu_hooks.inc"
static int vicii_cycle(void){if(maincpu_clk>=2048)longjmp(done,2);pins();return ready?0:MAINCPU_BA_LOW_VICII;}
#include "clock.inc"
static uint8_t load(unsigned addr,int dummy){
 (void)dummy;check_ba();addr&=65535;
 uint8_t data=memory[addr];
 fprintf(output,"%llu,%04x,R,%02x,%d\n",(unsigned long long)maincpu_clk,addr,data,sync_pending);sync_pending=0;
 return data;
}
static void store(unsigned addr,unsigned data){addr&=65535;data&=255;
 fprintf(output,"%llu,%04x,W,%02x,0\n",(unsigned long long)maincpu_clk,addr,data);memory[addr]=data;
 if(addr==0x0204 && data==0xa5)longjmp(done,1);
}
#define LOAD(a) load((a),0)
#define LOAD_DUMMY(a) load((a),1)
#define LOAD_ZERO(a) LOAD((a)&255)
#define LOAD_ZERO_DUMMY(a) LOAD_DUMMY((a)&255)
#define LOAD_CHECK_BA_LOW(a) LOAD(a)
#define LOAD_CHECK_BA_LOW_DUMMY(a) LOAD_DUMMY(a)
#define STORE(a,d) store((a),(d))
#define STORE_DUMMY(a,d) STORE(a,d)
#define STORE_ZERO(a,d) STORE((a)&255,d)
#define STORE_ZERO_DUMMY(a,d) STORE_ZERO(a,d)
#define PUSH(v) store(0x100+(reg_sp--),(v))
#define PULL() load(0x100+(++reg_sp),0)
#define STACK_PEEK() load(0x100+reg_sp,1)
#define JUMP(a) do{reg_pc=(uint16_t)(a);sync_pending=1;}while(0)
#define CLK maincpu_clk
#define CPU_LOG_ID 0
#define LOG_DEFAULT 0
#define SKIP_CYCLE 0
#define REWIND_FETCH_OPCODE(x)
#define CPU_INT_STATUS maincpu_int_status
#define LAST_OPCODE_INFO last_opcode_info
#define LAST_OPCODE_ADDR address_register
#define CPU_IS_JAMMED cpu_jammed
#define ALARM_CONTEXT maincpu_alarm_context
#define CALLER 0
#define ANE_LOG_LEVEL 0
#define LXA_LOG_LEVEL 0
#define GLOBAL_REGS global_regs
#define STATIC_ASSERT(x) _Static_assert((x),#x)
#define P_SIGN 0x80
#define P_OVERFLOW 0x40
#define P_UNUSED 0x20
#define P_BREAK 0x10
#define P_DECIMAL 8
#define P_INTERRUPT 4
#define P_ZERO 2
#define P_CARRY 1
#define MI_STEP 1
#define MI_BREAK 2
#define MI_WATCH 4
#define MI_OP 8
#define DMA_ON_RESET
#define DMA_FUNC abort()
#define CPU_ADDITIONAL_RESET()
#define JAM() longjmp(done,3)
#define ROM_TRAP_ALLOWED() 0
#define ROM_TRAP_HANDLER() 0
static int monitor_mask[1],maincpu_profiling;
static void profile_sample_start(unsigned p){(void)p;}
static void profile_sample_finish(CLOCK a,CLOCK b){(void)a;(void)b;}
static void profile_int(unsigned a,unsigned b,unsigned c,CLOCK d){(void)a;(void)b;(void)c;(void)d;}
static void profile_jsr(unsigned a,unsigned b,unsigned c){(void)a;(void)b;(void)c;}
static void profile_rtx(unsigned a){(void)a;}
static void monitor_check_icount_interrupt(void){abort();}
static void monitor_check_icount(unsigned p){(void)p;abort();}
static int monitor_check_breakpoints(int a,unsigned b){(void)a;(void)b;abort();return 0;}
static int monitor_check_watchpoints(unsigned a,unsigned b){(void)a;(void)b;abort();return 0;}
static void monitor_startup(int a){(void)a;abort();}
static void cpu_reset(void){abort();}
void interrupt_ack_reset(interrupt_cpu_status_t *s){(void)s;abort();}
void interrupt_do_trap(interrupt_cpu_status_t *s,uint16_t p){(void)s;(void)p;abort();}
void interrupt_ack_dma(interrupt_cpu_status_t *s){(void)s;abort();}
static void machine_handle_pending_alarms(int a){(void)a;}
static void log_warning(int a,const char *s,...){(void)a;(void)s;abort();}
static void log_message(int a,const char *s,...){(void)a;(void)s;}
struct {unsigned pc;uint8_t a,x,y,sp,p,n,z;}global_regs;
#define ALLOW_UNALIGNED_ACCESS
#include "fetch.inc"
static void execute(void){
 uint8_t reg_a=0,reg_x=0,reg_y=0,reg_sp=0xfd,reg_p=0x24,flag_n=0,flag_z=1;
 unsigned reg_pc=0x0800;int bank_start=0,bank_limit=0,cpu_jammed=0;uint8_t *bank_base=memory;
 for(;;){sync_pending=1;
#include "6510dtvcore.c"
 }
}
int main(int argc,char **argv){
 if(argc!=4)return 2;FILE*f=fopen(argv[1],"rb");if(!f||fread(memory,1,65536,f)!=65536)return 2;fclose(f);
 memset(controls,4,sizeof controls);f=fopen(argv[2],"r");if(!f)return 2;for(int i=0;i<2048;i++){unsigned x;if(fscanf(f,"%x",&x)!=1)return 2;controls[i]=x;}fclose(f);
 static unsigned pending;status.num_ints=1;status.pending_int=&pending;status.last_opcode_info_ptr=&last_opcode_info;status.irq_clk=status.nmi_clk=status.irq_pending_clk=CLOCK_MAX;
 output=fopen(argv[3],"w");if(!output)return 2;fputs("cycle,address,rw,data,sync\n",output);pins();int result=setjmp(done);if(!result)execute();fclose(output);return result==1?0:result;
}
