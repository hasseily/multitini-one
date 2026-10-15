/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_usb_storage_mode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    bool leased,connected,ejected;
    int quiesce_error,acquire_error,connect_error,stop_result,remount_error;
    unsigned quiesces,acquires,connects,polls,stops,releases,restarts,cancels;
    char events[128];unsigned event_count;
} f;
static void event(char e) { assert(f.event_count+1U<sizeof(f.events));f.events[f.event_count++]=e; }
static int quiesce(void *p) { (void)p;++f.quiesces;event('Q');return f.quiesce_error; }
static int acquire(void *p)
{ (void)p;assert(!f.leased && !f.connected);++f.acquires;event('A');if(f.acquire_error)return -1;f.leased=true;return 0; }
static int connect(void *p)
{ (void)p;assert(f.leased);++f.connects;event('C');if(f.connect_error)return -1;f.connected=true;return 0; }
static void poll(void *p) { (void)p;assert(f.leased);++f.polls;event('P'); }
static bool host(void *p) { (void)p;return f.ejected; }
static int stop(void *p)
{ (void)p;assert(f.leased);++f.stops;event('S');if(f.stop_result==0)f.connected=false;return f.stop_result; }
static int release(void *p)
{ (void)p;assert(f.leased && !f.connected);++f.releases;event('F');if(f.remount_error)return -1;f.leased=false;return 0; }
static int restart(void *p)
{ (void)p;assert(!f.leased && !f.connected);++f.restarts;event('R');return 0; }
static void cancel(void *p) { (void)p;assert(!f.leased && !f.connected);++f.cancels;event('X'); }
static mt_usb_sd_mode_t mode;
static void reset(void)
{
    memset(&f,0,sizeof(f));
    const mt_usb_sd_ops_t ops={NULL,quiesce,acquire,connect,poll,host,stop,release,restart,cancel};
    mt_usb_sd_mode_init(&mode,&ops);
}
int main(void)
{
    reset();f.quiesce_error=-1; /* e.g. an in-flight dirty disk cannot drain */
    assert(mt_usb_sd_mode_enter(&mode)!=0 && mode.state==MT_USB_SD_IDLE);
    assert(strcmp(f.events,"QX")==0 && f.acquires==0U);
    reset();f.acquire_error=-1;
    assert(mt_usb_sd_mode_enter(&mode)!=0 && strcmp(f.events,"QAX")==0);
    reset();f.connect_error=-1;
    assert(mt_usb_sd_mode_enter(&mode)!=0 && f.leased && mode.state==MT_USB_SD_STOPPING);
    mt_usb_sd_mode_poll(&mode);assert(mode.state==MT_USB_SD_IDLE && strcmp(f.events,"QACPSFR")==0);
    reset();assert(mt_usb_sd_mode_enter(&mode)==0 && strcmp(f.events,"QAC")==0);
    assert(mt_usb_sd_mode_enter(&mode)!=0 && f.quiesces==1U);
    mt_usb_sd_mode_poll(&mode);assert(f.polls==1U && f.releases==0U && f.leased && f.connected);
    f.stop_result=1;mt_usb_sd_mode_stop(&mode);mt_usb_sd_mode_poll(&mode);
    assert(mode.state==MT_USB_SD_STOPPING && f.leased && f.connected && f.releases==0U);
    f.stop_result=-1;mt_usb_sd_mode_poll(&mode);
    assert(mode.state==MT_USB_SD_STOP_FAILED && f.leased && f.releases==0U);
    f.stop_result=0;f.remount_error=-1;mt_usb_sd_mode_poll(&mode);
    assert(mode.state==MT_USB_SD_REMOUNT_FAILED && !f.connected && f.leased && f.restarts==0U);
    const unsigned stop_calls=f.stops;mt_usb_sd_mode_poll(&mode);
    assert(f.stops==stop_calls && f.leased && f.restarts==0U);
    f.remount_error=0;mt_usb_sd_mode_poll(&mode);
    assert(mode.state==MT_USB_SD_IDLE && !f.leased && f.restarts==1U);
    reset();assert(mt_usb_sd_mode_enter(&mode)==0);f.ejected=true;mt_usb_sd_mode_poll(&mode);
    assert(mode.state==MT_USB_SD_IDLE && !f.leased && strcmp(f.events,"QACPSFR")==0);
    puts("PASS USB SD exclusive modal: flush failure, connect failure, pending writes, sync failure, remount retry and host eject");
    return 0;
}
