/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production RGB565 references without device access. Output is
 * AX01 CSV: point_peak_green is measured before interpolation; stripe contrast
 * and point_sigma_output_px include output scaling and horizontal smoothing.
 * Optional arguments select the fixed per-core reference ratio (default 4/1).
 * The historical CT02 probe is available at commit dd4e978. */
#include "multitini_video_frame.h"
#include "multitini_video_effects.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

enum { W=64, H=16, N=W*H, MAX_W=W*6 };
static uint16_t input[N], output[N], filtered[MAX_W];
static uint32_t history[N], base[N], scratch[N];
static unsigned green(uint16_t pixel) { return (pixel>>5)&63U; }
static double variance(const uint16_t *row, unsigned n) {
    double mass=0,first=0,second=0;
    for(unsigned x=0;x<n;++x) {
        const double v=green(row[x]);mass+=v;first+=x*v;second+=x*x*v;
    }
    if(mass==0) return 0;
    const double value=second/mass-(first/mass)*(first/mass);
    return value>0 ? value:0; /* Avoid negative roundoff before sqrt. */
}
static bool scale_and_filter(unsigned num,unsigned den,unsigned filter) {
    if(num==0 || den==0) return false;
    const unsigned length=W*num/den;
    if(length==0 || length>MAX_W) return false;
    mt_video_effects_settings_t settings;
    mt_video_effects_tables_t tables;
    mt_video_effects_defaults(&settings);settings.enabled=1;settings.softness=(uint8_t)filter;
    if(!mt_video_effects_build(&settings,&tables)) return false;
    const uint16_t *row=output+(H/2)*W;
    for(unsigned x=0;x<length;++x) {
        const unsigned sx=x*den/num;
        filtered[x]=mt_video_effects_reference_scaled_rgb565(&tables,
            row[sx==0 ? sx:sx-1],row[sx],row[sx+1==W ? sx:sx+1],
            sx==0,sx+1==W,false,x,H/2,num,den);
    }
    return true;
}
int main(int argc,char **argv) {
    unsigned reference_num=4U,reference_den=1U;
    if(argc!=1 && argc!=3) return 2;
    if(argc==3) {
        char *end_num,*end_den;
        const unsigned long n=strtoul(argv[1],&end_num,10),d=strtoul(argv[2],&end_den,10);
        if(*argv[1]=='\0' || *argv[2]=='\0' || *end_num!='\0' || *end_den!='\0' ||
           n<1U || n>64U || d<1U || d>64U)return 2;
        reference_num=(unsigned)n;reference_den=(unsigned)d;
    }
    const unsigned scales[][2]={{1,1},{3,2},{2,1},{3,1},{4,1},{5,1},{6,1}};
    puts("scale_num,scale_den,vertical_smoothing,horizontal_smoothing,point_peak_green,stripe_contrast_green,stripe_contrast_percent,point_sigma_output_px");
    for(unsigned s=0;s<sizeof(scales)/sizeof(scales[0]);++s) {
        const unsigned num=scales[s][0],den=scales[s][1];
        for(unsigned blur=0;blur<4;++blur)for(unsigned filter=0;filter<4;++filter) {
            const mt_video_frame_options_t opt={1,0,(uint8_t)blur,0,(uint8_t)filter};
            memset(input,0,sizeof(input));input[(H/2)*W+W/2]=0x07e0;
            if(!mt_video_frame_process_scaled(input,output,history,base,scratch,W,H,reference_num,reference_den,&opt,0,true)) return 1;
            const unsigned peak=green(output[(H/2)*W+W/2]);
            if(!scale_and_filter(num,den,filter)) return 1;
            const double var=variance(filtered,W*num/den);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)input[y*W+x]=(x&1) ? 0x07e0:0;
            if(!mt_video_frame_process_scaled(input,output,history,base,scratch,W,H,reference_num,reference_den,&opt,0,true)) return 1;
            if(!scale_and_filter(num,den,filter)) return 1;
            double high=0,low=0;unsigned hc=0,lc=0;
            for(unsigned x=(W/4)*num/den;x<(3*W/4)*num/den;++x) {
                if((x*den/num)&1) {high+=green(filtered[x]);++hc;}
                else {low+=green(filtered[x]);++lc;}
            }
            if(hc==0 || lc==0) return 1;
            const double contrast=high/hc-low/lc;
            printf("%u,%u,%u,%u,%u,%.6f,%.6f,%.6f\n",num,den,blur,filter,peak,contrast,contrast*100/63,sqrt(var));
        }
    }
    return ferror(stdout) ? 1:0;
}
