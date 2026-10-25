/* SPDX-License-Identifier: GPL-3.0-only */
/* Emit production resolver tables for HDL integration fixtures. */
#include "multitini_c64_sid.h"
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *f=fopen(argv[1],"w");if(!f)return 3;
    for(unsigned preset=MT_SID_SINGLE;preset<=MT_SID_EIGHT;preset++) {
        mt_c64_sid_model_t model;mt_c64_sid_table_t table;
        if(!mt_c64_sid_preset(&model,preset)||!mt_c64_sid_build(&model,8,true,&table))return 4;
        for(unsigned word=0;word<MT_C64_SID_WORDS;word++)if(fprintf(f,"%08x\n",(unsigned)table.words[word])<0)return 5;
    }
    if(fclose(f))return 6;
    puts("PASS SID presets tables=6");return 0;
}
