/* SPDX-License-Identifier: GPL-3.0-only */
/* Native test bridge: the production portable DOS engine with ordinary host
 * file callbacks under one caller-created test directory. No ROM contents. */
#include "multitini_c64_iec_dos.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define EXPORT __declspec(dllexport)
#else
#include <unistd.h>
#define EXPORT
#endif

static mt_iec_dos_t dos;
static char backing[1024];
static unsigned char names[16][256];
static size_t lengths[16];
static int opening[16],sent[16];
static uint16_t sent_token[16];

static int resolve(const char *path,char *out,size_t capacity)
{
    static const char root[]="0:/media";
    if(strncmp(path,root,sizeof(root)-1)!=0 ||
       (path[sizeof(root)-1]!=0 && path[sizeof(root)-1]!='/') ||
       strstr(path,"..") || strchr(path,'\\')) return 33;
    return snprintf(out,capacity,"%s%s",backing,path+sizeof(root)-1)>=(int)capacity?33:0;
}
static int fs_list(void *ctx,const char *path,mt_iec_list_fn visitor,void *user)
{
    char actual[1536],child[1792];struct dirent *entry;struct stat st;
    DIR *directory;int rc=resolve(path,actual,sizeof(actual));(void)ctx;
    if(rc)return rc;
    directory=opendir(actual);if(!directory)return 62;
    while((entry=readdir(directory))!=NULL){
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        if(snprintf(child,sizeof(child),"%s/%s",actual,entry->d_name)>=(int)sizeof(child) || stat(child,&st)!=0){closedir(directory);return 74;}
        rc=visitor(user,entry->d_name,(size_t)st.st_size,(st.st_mode&S_IFDIR)!=0);
        if(rc){closedir(directory);return rc;}
    }
    closedir(directory);return 0;
}
static int fs_open(void *ctx,const char *path,const char *mode,void **handle,size_t *size)
{
    char actual[1536];struct stat st;FILE *f;int rc=resolve(path,actual,sizeof(actual));(void)ctx;
    *handle=NULL;*size=0;if(rc)return rc;
    if(!strcmp(mode,"r"))mode="rb";
    else if(!strcmp(mode,"x"))mode="wbx";
    else return 33;
    f=fopen(actual,mode);if(!f)return 62;
    if(stat(actual,&st)!=0){fclose(f);return 74;}
    *handle=f;*size=(size_t)st.st_size;return 0;
}
static int fs_read(void *ctx,void *handle,uint8_t *out,size_t cap,size_t *used)
{(void)ctx;*used=fread(out,1,cap,handle);return ferror(handle)?74:0;}
static int fs_write(void *ctx,void *handle,const uint8_t *data,size_t size,size_t *used)
{(void)ctx;*used=fwrite(data,1,size,handle);return *used==size?0:72;}
static int fs_close(void *ctx,void **handle)
{int rc;(void)ctx;if(!*handle)return 0;rc=fclose(*handle);*handle=NULL;return rc?74:0;}
static int fs_remove(void *ctx,const char *path)
{char actual[1536];int rc=resolve(path,actual,sizeof(actual));(void)ctx;return rc?rc:remove(actual)==0?0:62;}
static int fs_rename(void *ctx,const char *old,const char *new)
{char a[1536],b[1536];int rc=resolve(old,a,sizeof(a));(void)ctx;if(!rc)rc=resolve(new,b,sizeof(b));return rc?rc:rename(a,b)==0?0:74;}
static int fs_mkdir(void *ctx,const char *path)
{char actual[1536];int rc=resolve(path,actual,sizeof(actual));(void)ctx;if(rc)return rc;
#ifdef _WIN32
return _mkdir(actual)==0?0:63;
#else
return mkdir(actual,0700)==0?0:63;
#endif
}
static int fs_commit(void *ctx,const char *temporary,const char *target,bool replace)
{
    char a[1536],b[1536];struct stat st;int rc=resolve(temporary,a,sizeof(a));(void)ctx;
    if(!rc)rc=resolve(target,b,sizeof(b));
    if(rc)return rc;
    if(stat(b,&st)==0){if(!replace)return 63;if(remove(b)!=0)return 74;}
    return rename(a,b)==0?0:74;
}
static bool fs_protected(void *ctx,const char *path){(void)ctx;(void)path;return false;}

EXPORT int test_iec_init(const char *root)
{
    mt_iec_fs_t fs={NULL,fs_list,fs_open,fs_read,fs_write,fs_close,fs_remove,fs_rename,fs_mkdir,fs_commit,fs_protected};
    if(!root || strlen(root)>=sizeof(backing))return -1;
    strcpy(backing,root);memset(opening,0,sizeof(opening));memset(sent,0,sizeof(sent));
    memset(lengths,0,sizeof(lengths));return mt_iec_dos_init(&dos,&fs,"0:/media");
}
/* Test-only event adapter. Only the actual listener ACK confirms a peeked
 * byte; READ and UNTALK never consume it. This is not the MMIO service. */
EXPORT uint32_t test_iec_event(uint32_t event)
{
    unsigned type=(event>>12)&15U,ch=(event>>8)&15U;uint8_t byte=(uint8_t)event;
    uint32_t reply=event&0xffff0000U;bool eoi=false,available=false;int rc=0;
    switch(type){
    case 0: memset(opening,0,sizeof(opening));memset(sent,0,sizeof(sent));rc=mt_iec_dos_reset(&dos);break;
    case 1: opening[ch]=1;lengths[ch]=0;break;
    case 2: rc=mt_iec_dos_close(&dos,ch);sent[ch]=0;opening[ch]=0;break;
    case 3:case 4:
        if(opening[ch]){if(lengths[ch]>=sizeof(names[ch]))rc=33;else names[ch][lengths[ch]++]=byte;}
        else rc=mt_iec_dos_write(&dos,ch,&byte,1,type==4);
        break;
    case 5:
        if(opening[ch]){rc=mt_iec_dos_open(&dos,ch,names[ch],lengths[ch]);opening[ch]=0;}
        break;
    case 6: mt_iec_dos_begin_talk(&dos,ch);sent[ch]=0;break;
    case 7: sent[ch]=0;break;
    case 8: opening[ch]=0;sent[ch]=0;break;
    case 9: break;
    case 10:
        if(sent[ch] && sent_token[ch]==(event>>16))mt_iec_dos_consume(&dos,ch);
        sent[ch]=0;break;
    default:return 0xffffffffU;
    }
    if(type!=6 && type!=9)return 0xffffffffU;
    rc=mt_iec_dos_peek(&dos,ch,&byte,&eoi,&available);
    if(rc || !available)return reply|0x200U;
    sent[ch]=1;sent_token[ch]=(uint16_t)(event>>16);
    return reply|(eoi?0x100U:0U)|byte;
}
EXPORT int test_iec_finish(void){return mt_iec_dos_quiesce(&dos);}
