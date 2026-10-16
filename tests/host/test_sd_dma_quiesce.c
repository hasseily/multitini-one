/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uintptr_t UINTPTR;
typedef u8 BYTE;
typedef u8 DSTATUS;
typedef enum { RES_OK,RES_ERROR,RES_NOTRDY,RES_PARERR } DRESULT;
typedef struct { struct { UINTPTR BaseAddress; } Config;u32 IsBusy,IsReady; } XSdPs;
#define FILE_SYSTEM_INTERFACE_SD 1
#define XPAR_XSDPS_NUM_INSTANCES 1
#define XSDPS_NUM_INSTANCES 2U
#define STA_NOINIT 1U
#define XST_SUCCESS 0
#define FALSE 0U
#define MT_SD_QUIESCE_RESET 1U
#define XSDPS_PSR_INHIBIT_CMD_MASK 1U
#define XSDPS_PSR_INHIBIT_DAT_MASK 2U
#define XSDPS_PSR_DAT_ACTIVE_MASK 4U
#define XSDPS_PSR_WR_ACTIVE_MASK 0x100U
#define XSDPS_PSR_RD_ACTIVE_MASK 0x200U
#define XSDPS_SWRST_ALL_MASK 1U
#define XSDPS_SW_RST_OFFSET 0x2fU
static DSTATUS Stat[XSDPS_NUM_INSTANCES];
static XSdPs SdInstance[XSDPS_NUM_INSTANCES];
static UINTPTR BaseAddress[XSDPS_NUM_INSTANCES];
static u32 present,reset_bit,resets;
static int reset_status,base_missing,keep_active,keep_reset;
static DSTATUS disk_status(BYTE drive)
{ assert(drive<XSDPS_NUM_INSTANCES);BaseAddress[drive]=base_missing ? 0U:0x1000U;return Stat[drive]; }
static u32 XSdPs_GetPresentStatusReg(UINTPTR base)
{ assert(base==0x1000U);return present; }
static u8 XSdPs_ReadReg8(UINTPTR base,u32 offset)
{ assert(base==0x1000U && offset==XSDPS_SW_RST_OFFSET);return (u8)reset_bit; }
static int XSdPs_Reset(XSdPs *instance,u8 mask)
{
    assert(instance==&SdInstance[0] && mask==XSDPS_SWRST_ALL_MASK);
    assert((Stat[0]&STA_NOINIT)!=0U);++resets;
    if(reset_status==XST_SUCCESS) {
        if(!keep_active)present=0U;
        reset_bit=keep_reset ? 1U:0U;
    }
    return reset_status;
}
#include "../../firmware/platform/xilinx/sd_quiesce_diskio.inc"
static void clear(void)
{
    memset(Stat,0,sizeof(Stat));memset(SdInstance,0,sizeof(SdInstance));
    memset(BaseAddress,0,sizeof(BaseAddress));memset(MultitiniSdFault,0,sizeof(MultitiniSdFault));
    present=reset_bit=resets=0U;reset_status=base_missing=keep_active=keep_reset=0;
    SdInstance[0].Config.BaseAddress=0x1000U;SdInstance[0].IsReady=1U;SdInstance[0].IsBusy=1U;
}
int main(void)
{
    BYTE recovered=99U;
    clear();assert(MultitiniSdQuiesce(0U,&recovered)==RES_OK && recovered==0U && resets==0U);
    assert(Stat[0]==0U && SdInstance[0].IsReady==1U);
    const u32 flags[]={1U,2U,4U,0x100U,0x200U};
    for(unsigned index=0U;index<sizeof(flags)/sizeof(flags[0]);++index) {
        clear();present=flags[index];
        assert(MultitiniSdQuiesce(0U,&recovered)==RES_OK && recovered==MT_SD_QUIESCE_RESET);
        assert(resets==1U && (Stat[0]&STA_NOINIT)!=0U && SdInstance[0].IsReady==0U && SdInstance[0].IsBusy==0U);
    }
    clear();MultitiniSdFault[0]=1U;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_OK && recovered==1U && resets==1U);
    clear();present=4U;reset_status=1;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_ERROR && recovered==0U && MultitiniSdFault[0]==1U);
    assert((Stat[0]&STA_NOINIT)!=0U);
    reset_status=0;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_OK && recovered==1U && resets==2U);
    clear();present=4U;keep_reset=1;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_ERROR && MultitiniSdFault[0]==1U);
    clear();present=4U;keep_active=1;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_ERROR && MultitiniSdFault[0]==1U);
    clear();SdInstance[0].Config.BaseAddress=0U;present=4U;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_OK && recovered==1U);
    clear();SdInstance[0].Config.BaseAddress=0U;base_missing=1;
    assert(MultitiniSdQuiesce(0U,&recovered)==RES_NOTRDY && recovered==0U && resets==0U);
    assert(MultitiniSdQuiesce(2U,&recovered)==RES_PARERR);
    assert(MultitiniSdQuiesce(0U,NULL)==RES_PARERR);
    puts("PASS actual shared SD quiesce: idle, five activity bits, fault, reset timeout, stuck reset/activity, retry and missing controller");
    return 0;
}
